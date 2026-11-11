#include "tilt.h"
#include "config.h"
#include <Arduino.h>
#include <Preferences.h>
#include <math.h>

// Which sensor axis points OUT of the screen, and which points to the TOP edge
// of the display (sensor frame, unit vectors). Determined for the 2.8C board
// (lying face-up the QMI8658 reads az = -1 g, so +Z points into the screen).
#ifndef SENSOR_SCREEN_OUT
#define SENSOR_SCREEN_OUT {0.f, 0.f, -1.f}
#endif
#ifndef SENSOR_SCREEN_TOP
#define SENSOR_SCREEN_TOP {0.f, 1.f, 0.f}
#endif

// Signal chain (250 Hz):
//   accel (on-chip ~9 Hz LPF)
//   -> median of 5 per axis           removes single-sample spikes
//   -> bump gate                      |a| jumps away from its average -> hold the last value
//   -> slow pull of the estimate towards it (calm numbers)
//   -> rotate into the car frame      pitch / roll
// The filter runs in the sensor frame, so a new level reference applies instantly.
//
// TILT_GYRO_CARRY 1 (default) — complementary filter: the gyro turns the estimate
//   instantly when the car really tilts, the heavily smoothed accelerometer keeps it
//   true. Gyro errors cannot accumulate: they fade with TILT_SMOOTH_S. No trust gating,
//   no integral term, no in-motion bias learning (the causes of the old drift).
// TILT_GYRO_CARRY 0 — the angle comes only from the accelerometer; the gyro is used
//   purely as a detector (road shake doesn't rotate the car, driving onto a slope does)
//   that decides how fast the smoothing follows.
// In both modes a large, persistent accelerometer deviation speeds the filter up, so a
// gross error can never stick.

namespace tilt {

struct V3 { float x, y, z; };
static inline V3 v3(float x, float y, float z) { return {x, y, z}; }
static inline float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static inline V3 cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
static inline V3 scale(V3 a, float k) { return {a.x * k, a.y * k, a.z * k}; }
static inline V3 add(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
static inline V3 sub(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
static inline V3 lerp(V3 a, V3 b, float k) { return add(a, scale(sub(b, a), k)); }
static inline float len(V3 a) { return sqrtf(dot(a, a)); }
static inline V3 norm(V3 a) { float l = len(a); return l > 1e-9f ? scale(a, 1.f / l) : a; }
static inline float angleDeg(V3 a, V3 b) { return atan2f(len(cross(a, b)), dot(a, b)) * 180.f / (float)M_PI; }
static inline float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }

// mount rotation: car = M * sensor  (rows: forward, left, up in sensor coords)
static V3 mF = {0, 0, -1}, mL = {-1, 0, 0}, mU = {0, 1, 0};
static bool levelStored = false;

// filter state (sensor frame, g)
static V3 med[5];
static int medI = 0;
static V3 gFast = {0, 0, 1};             // quick estimate, used only to detect real changes
static V3 gOut = {0, 0, 1};              // filtered gravity -> displayed angles
static float magRef = 1;                 // slow average of |a| (bump reference)
static V3 gyroAvg = {0, 0, 0};           // gyro bias estimate (dps)
#if !TILT_GYRO_CARRY
static float gyroHold = 0;               // mode 0: gyro-based response, held briefly after rotation stops
#endif
static float bumpTime = 0;
static Attitude att = {0, 0, 0, false};

// level calibration accumulator
static bool calBusy = false;
static int calN = 0;
static V3 calSum = {0, 0, 0};

static void buildMount(V3 up) {
  const float so[3] = SENSOR_SCREEN_OUT, st[3] = SENSOR_SCREEN_TOP;
  V3 intoScreen = v3(-so[0], -so[1], -so[2]);
  V3 top = v3(st[0], st[1], st[2]);
  mU = norm(up);
  // choose the forward hint that is more horizontal
  V3 hint = fabsf(dot(intoScreen, mU)) < 0.75f ? intoScreen : top;
  mF = norm(sub(hint, scale(mU, dot(hint, mU))));
  mL = cross(mU, mF);
}

// Flash must not be touched while the RGB display runs (its interrupt reads
// PSRAM, which is unavailable during flash operations). A new level reference
// is therefore only queued here; the render task pauses the display and calls
// commitSave() (see main.cpp).
static float pendingM[9];
static volatile bool savePending = false;

static void saveMount() {
  float m[9] = {mF.x, mF.y, mF.z, mL.x, mL.y, mL.z, mU.x, mU.y, mU.z};
  memcpy(pendingM, m, sizeof m);
  levelStored = true;
  savePending = true;   // set last: pendingM is complete when this is seen
}

static bool loadMount() {
  Preferences p;
  p.begin("incl", false);   // read-write so a missing namespace isn't logged as an error
  float m[9];
  bool ok = p.getBytes("mount", m, sizeof(m)) == sizeof(m);
  p.end();
  if (ok) {
    mF = {m[0], m[1], m[2]}; mL = {m[3], m[4], m[5]}; mU = {m[6], m[7], m[8]};
  }
  return ok;
}

static inline V3 toCar(V3 s) { return {dot(mF, s), dot(mL, s), dot(mU, s)}; }

static void computeAngles() {
  V3 up = toCar(gOut);
  att.pitch = atan2f(up.x, sqrtf(up.y * up.y + up.z * up.z)) * 180.f / (float)M_PI;
  att.roll = atan2f(up.y, up.z) * 180.f / (float)M_PI;
}

void begin() {
  levelStored = loadMount();
  Serial.printf("[tilt] level reference %s\n", levelStored ? "loaded" : "not set");
}

void prime(int samples) {
  V3 sum = {0, 0, 0}, gsum = {0, 0, 0};
  int got = 0;
  for (int i = 0; i < samples; i++) {
    ImuSample s;
    if (imu::read(s)) { sum = add(sum, v3(s.ax, s.ay, s.az)); gsum = add(gsum, v3(s.gx, s.gy, s.gz)); got++; }
    delay(4);
  }
  V3 g = got ? scale(sum, 1.f / got) : v3(0, 0, 1);
  if (got) gyroAvg = scale(gsum, 1.f / got);
  for (V3& m : med) m = g;
  gFast = gOut = g;
  magRef = len(g);
  // No stored level yet: assume the current orientation is level until the user sets it.
  if (!levelStored) buildMount(g);
  computeAngles();
  Serial.printf("[tilt] primed from %d samples: pitch %.1f roll %.1f\n", got, att.pitch, att.roll);
}

static inline float median5(float a, float b, float c, float d, float e) {
  // minimal sorting network for the middle element
  float t;
#define SW(x, y) if (x > y) { t = x; x = y; y = t; }
  SW(a, b); SW(d, e); SW(a, c); SW(b, c); SW(a, d); SW(c, d); SW(b, e); SW(b, c); SW(d, e);
#undef SW
  return c;
}

void update(const ImuSample& s, float dt) {
  V3 aS = {s.ax, s.ay, s.az};

  // ---- gyro ----
  {
    V3 g = {s.gx, s.gy, s.gz};
    V3 w = sub(g, gyroAvg);
    // bias: follow very slowly, and only while not rotating
    if (len(w) < TILT_GYRO_BIAS_GATE_DPS) gyroAvg = lerp(gyroAvg, g, fminf(1.f, dt / TILT_GYRO_AVG_S));
#if TILT_GYRO_CARRY
    // turn the estimate with the car (gravity seen from a rotating body moves by -w x g)
    gOut = sub(gOut, cross(scale(w, (float)M_PI / 180.f * dt), gOut));
#else
    V3 up = norm(gOut);
    V3 tiltW = sub(w, scale(up, dot(w, up)));      // drop rotation about gravity (steering)
    float a = clamp01((len(tiltW) - TILT_GYRO_MIN_DPS) / (TILT_GYRO_FULL_DPS - TILT_GYRO_MIN_DPS));
    gyroHold = fmaxf(a, gyroHold - dt / TILT_GYRO_HOLD_S);
#endif
  }

  // ---- level calibration (averaging raw samples) ----
  if (calBusy) {
    calSum = add(calSum, aS);
    if (++calN >= 250) {
      buildMount(scale(calSum, 1.f / calN));
      saveMount();
      calBusy = false;
    }
  }

  // ---- spike removal ----
  med[medI] = aS;
  medI = (medI + 1) % 5;
  V3 r = {median5(med[0].x, med[1].x, med[2].x, med[3].x, med[4].x),
          median5(med[0].y, med[1].y, med[2].y, med[3].y, med[4].y),
          median5(med[0].z, med[1].z, med[2].z, med[3].z, med[4].z)};

  // ---- bump gate: a sudden change of |a| is a jolt, not a tilt ----
  float m = len(r);
  att.bump = fabsf(m - magRef) > TILT_BUMP_G;
  if (att.bump) bumpTime += dt; else bumpTime = 0;
  magRef += (m - magRef) * fminf(1.f, dt / 2.f);
  if (att.bump && bumpTime < TILT_BUMP_HOLD_S) {   // hold; after the timeout, accept anyway
    computeAngles();                               // (a new level reference still applies)
    return;
  }

  // ---- accelerometer pull (adaptive) ----
  gFast = lerp(gFast, r, fminf(1.f, dt / TILT_DETECT_S));
  float diff = angleDeg(gFast, gOut);              // how far the real angle has moved away
  att.adapt = clamp01((diff - TILT_ADAPT_MIN_DEG) / (TILT_ADAPT_DEG - TILT_ADAPT_MIN_DEG));
#if !TILT_GYRO_CARRY
  att.adapt = fmaxf(att.adapt, gyroHold);
#endif
  float tau = TILT_SMOOTH_S + (TILT_FAST_S - TILT_SMOOTH_S) * att.adapt;
  gOut = lerp(gOut, r, fminf(1.f, dt / tau));

  computeAngles();
}

Attitude attitude() { return att; }

void startLevelCalibration() {
  calSum = {0, 0, 0};
  calN = 0;
  calBusy = true;
}
bool levelCalibrationBusy() { return calBusy; }
bool isLevelCalibrated() { return levelStored; }
bool hasUnsavedLevel() { return savePending; }

void commitSave() {
  if (!savePending) return;
  float m[9];
  memcpy(m, pendingM, sizeof m);
  savePending = false;
  Preferences p;
  p.begin("incl", false);
  p.putBytes("mount", m, sizeof m);
  p.end();
}
void rawGravity(float& x, float& y, float& z) { x = gOut.x; y = gOut.y; z = gOut.z; }

}  // namespace tilt
