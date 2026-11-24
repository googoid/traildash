#include "tilt.h"
#include "config.h"
#include "settings.h"
#include <Arduino.h>
#include <Preferences.h>
#include <math.h>
#include <esp_heap_caps.h>

// Signal chain (250 Hz):
//   accel (remote IMU, MPU-6050 DLPF ~20 Hz)
//   -> median of 5 per axis           removes single-sample spikes
//   -> minus the car's own forward acceleration (from its speed, see setSpeed)
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
static V3 mF = {1, 0, 0}, mL = {0, 1, 0}, mU = {0, 0, 1};
static bool levelStored = false;
// Seed the filter from the first samples (at start and whenever the sensor comes back
// after a gap); attitude holds meanwhile.
static constexpr int SEED_N = 25;
static int seedN = 0;   // <0 = seeded
static V3 seedSum = {0, 0, 0};

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
// forward acceleration from the speed readings (m/s^2)
static constexpr int SPD_N = 8;
static float spdV[SPD_N];
static uint32_t spdT[SPD_N];
static int spdI = 0, spdCount = 0;
static float spdAge = 1e9f;              // s since the last reading
static float accRaw = 0, accFwd = 0;
// The acceleration from the speed lags the accelerometer, so the accelerometer is compared
// with the estimate from as long ago, and the difference corrects the current estimate
// (the gyro still turns the reading without delay).
static constexpr int DLY_DECIM = 5;      // one entry per 5 samples (20 ms)
static constexpr int DLY_N = TILT_ACC_DELAY_MS / (4 * DLY_DECIM) + 1;
static V3* dlyA = nullptr;               // accelerometer, and the estimate at that time (PSRAM:
static V3* dlyG = nullptr;               // internal RAM is short)
static int dlyI = 0, dlyK = 0;
static Attitude att = {0, 0, 0, false};

// level calibration accumulator
static bool calBusy = false;
static int calN = 0;
static V3 calSum = {0, 0, 0};

// The sensor axis that points (roughly) forward, from cfg.remoteForward ("+x".."-z").
static V3 forwardAxis() {
  const char* a = cfg.remoteForward;
  float sg = a[0] == '-' ? -1.f : 1.f;
  char c = a[1];
  return c == 'y' ? v3(0, sg, 0) : c == 'z' ? v3(0, 0, sg) : v3(sg, 0, 0);
}

// "Set level" measures up; forward is the configured axis, made horizontal.
static void buildMount(V3 up) {
  mU = norm(up);
  V3 hint = forwardAxis();
  if (fabsf(dot(hint, mU)) > 0.75f) {   // configured axis points (nearly) up: take any horizontal one
    Serial.println("[tilt] remote_forward axis is vertical - check config.txt; forward is a guess");
    hint = fabsf(mU.x) < 0.75f ? v3(1, 0, 0) : v3(0, 1, 0);
  }
  mF = norm(sub(hint, scale(mU, dot(hint, mU))));
  mL = cross(mU, mF);
}

// Flash must not be touched while the RGB display runs (its interrupt reads
// PSRAM, which is unavailable during flash operations). A new level reference
// is therefore only queued here; the render task pauses the display and calls
// commitSave() (see main.cpp).
static float pendingM[9];
static volatile bool savePending = false;
// "mount_r": the key used when there was also a built-in IMU ("mount", now unused),
// so a level reference set for the remote IMU then still applies.
static const char* const NVS_KEY = "mount_r";

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
  bool ok = p.getBytes(NVS_KEY, m, sizeof(m)) == sizeof(m);
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
  dlyA = (V3*)heap_caps_calloc(2 * DLY_N, sizeof(V3), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  dlyG = dlyA ? dlyA + DLY_N : nullptr;
  levelStored = loadMount();
  Serial.printf("[tilt] level reference %s\n", levelStored ? "loaded" : "not set");
}

void reseed() {
  seedN = 0;
  seedSum = {0, 0, 0};
  if (calBusy) { calSum = {0, 0, 0}; calN = 0; }   // a running level calibration starts over
}

void setSpeed(float mps, uint32_t ms, uint8_t source) {
  static uint8_t lastSource = 0xFF;
  int last = (spdI + SPD_N - 1) % SPD_N;
  if (source != lastSource) { lastSource = source; spdCount = 0; }   // sources differ a little
  if (spdCount && ms == spdT[last]) return;                          // same reading again
  if (spdCount && ms - spdT[last] > TILT_ACC_GAP_MS) spdCount = 0;   // after a gap: start over
  spdV[spdI] = mps;
  spdT[spdI] = ms;
  spdI = (spdI + 1) % SPD_N;
  if (spdCount < SPD_N) spdCount++;
  spdAge = 0;
  // difference to the newest reading at least TILT_ACC_SPAN_MS old (quantised speed needs a span)
  for (int k = 1; k < spdCount; k++) {
    int j = (spdI + SPD_N - 1 - k) % SPD_N;
    uint32_t span = ms - spdT[j];
    if (span >= TILT_ACC_SPAN_MS || k == spdCount - 1) {
      if (span >= TILT_ACC_SPAN_MS / 2) {
        float a = (mps - spdV[j]) / (span * 1e-3f);
        accRaw = a > 8 ? 8 : (a < -8 ? -8 : a);   // no car does more than ~0.8 g
      }
      break;
    }
  }
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

  // ---- seed (first samples, or the sensor is back after a gap) ----
  if (seedN >= 0) {
    seedSum = add(seedSum, aS);
    if (++seedN < SEED_N) return;
    V3 g = scale(seedSum, 1.f / SEED_N);
    for (V3& m : med) m = g;
    for (int k = 0; dlyA && k < DLY_N; k++) dlyA[k] = dlyG[k] = g;
    gFast = gOut = g;
    magRef = len(g);
    if (!levelStored) buildMount(g);   // no reference yet: current orientation = level
    seedN = -1;
    computeAngles();
    return;
  }

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

  // ---- the car's own forward acceleration is not tilt ----
  if (dlyA && ++dlyK >= DLY_DECIM) { dlyK = 0; dlyA[dlyI] = r; dlyG[dlyI] = gOut; dlyI = (dlyI + 1) % DLY_N; }
  spdAge += dt;
  const bool haveSpeed = dlyA && spdAge < TILT_ACC_GAP_MS * 1e-3f;
  if (!haveSpeed) accRaw = 0;                      // no speed: nothing to take out, no need to wait
  accFwd += (accRaw - accFwd) * fminf(1.f, dt / TILT_ACC_SMOOTH_S);
  V3 gThen = gOut;
  if (haveSpeed) { r = dlyA[dlyI]; gThen = dlyG[dlyI]; }   // the oldest entries, ~TILT_ACC_DELAY_MS ago
  r = sub(r, scale(mF, accFwd / 9.80665f));

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
  float diff = angleDeg(gFast, gThen);              // how far the real angle has moved away
  att.adapt = clamp01((diff - TILT_ADAPT_MIN_DEG) / (TILT_ADAPT_DEG - TILT_ADAPT_MIN_DEG));
#if !TILT_GYRO_CARRY
  att.adapt = fmaxf(att.adapt, gyroHold);
#endif
  float tau = TILT_SMOOTH_S + (TILT_FAST_S - TILT_SMOOTH_S) * att.adapt;
  if (haveSpeed) tau = fmaxf(tau, 2e-3f * TILT_ACC_DELAY_MS);   // correcting from older data: not faster (would swing)
  gOut = add(gOut, scale(sub(r, gThen), fminf(1.f, dt / tau)));

  computeAngles();
}

Attitude attitude() { return att; }

void startLevelCalibration() {
  calSum = {0, 0, 0};
  calN = 0;
  calBusy = true;
}
bool levelCalibrationBusy() { return calBusy; }
bool seeding() { return seedN >= 0; }
bool isLevelCalibrated() { return levelStored; }
bool hasUnsavedLevel() { return savePending; }

void commitSave() {
  if (!savePending) return;
  float m[9];
  memcpy(m, pendingM, sizeof m);
  savePending = false;
  Preferences p;
  p.begin("incl", false);
  p.putBytes(NVS_KEY, m, sizeof m);
  p.end();
}
bool takePendingLevel(float m[9]) {
  if (!savePending) return false;
  memcpy(m, pendingM, sizeof pendingM);
  savePending = false;
  return true;
}

bool getMount(float m[9]) {
  const float v[9] = {mF.x, mF.y, mF.z, mL.x, mL.y, mL.z, mU.x, mU.y, mU.z};
  memcpy(m, v, sizeof v);
  return levelStored;
}

void setMount(const float m[9]) {
  mF = {m[0], m[1], m[2]}; mL = {m[3], m[4], m[5]}; mU = {m[6], m[7], m[8]};
  levelStored = true;
}

void rawGravity(float& x, float& y, float& z) { x = gOut.x; y = gOut.y; z = gOut.z; }

}  // namespace tilt
