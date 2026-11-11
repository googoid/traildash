#include "fusion.h"
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

namespace fusion {

struct V3 { float x, y, z; };
static inline V3 v3(float x, float y, float z) { return {x, y, z}; }
static inline float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static inline V3 cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
static inline V3 scale(V3 a, float k) { return {a.x * k, a.y * k, a.z * k}; }
static inline V3 sub(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
static inline float len(V3 a) { return sqrtf(dot(a, a)); }
static inline V3 norm(V3 a) { float l = len(a); return l > 1e-9f ? scale(a, 1.f / l) : a; }

// mount rotation: car = M * sensor  (rows: forward, left, up in sensor coords)
static V3 mF = {0, 0, -1}, mL = {-1, 0, 0}, mU = {0, 1, 0};
static bool levelStored = false;

static V3 gyroBias = {0, 0, 0};          // sensor frame, dps
static float q0 = 1, q1 = 0, q2 = 0, q3 = 0;
static V3 iErr = {0, 0, 0};
static V3 accLP = {0, 0, 1};             // low-passed accel, sensor frame
static float stillTime = 0;
// bias-independent motion statistics (exponential moving mean / variance of raw gyro & accel)
static V3 gMean = {0, 0, 0};
static float gVar = 1, aVar = 1;
static float settleTime = 0;             // fast convergence after boot / recal
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

void begin() {
  levelStored = loadMount();
  Serial.printf("[fusion] level reference %s\n", levelStored ? "loaded" : "not set");
}

static bool loadBias(V3& b) {
  Preferences p;
  p.begin("incl", false);
  float v[3];
  bool ok = p.getBytes("gbias", v, sizeof v) == sizeof v;
  p.end();
  if (ok) b = {v[0], v[1], v[2]};
  return ok;
}

static void storeBias(V3 b) {
  Preferences p;
  p.begin("incl", false);
  float v[3] = {b.x, b.y, b.z};
  p.putBytes("gbias", v, sizeof v);
  p.end();
}

// Call before the display starts (it may write the bias to flash).
bool calibrateGyro(int samples) {
  V3 sum = {0, 0, 0}, sumA = {0, 0, 0}, sq = {0, 0, 0};
  int got = 0;
  for (int i = 0; i < samples; i++) {
    ImuSample s;
    if (imu::read(s)) {
      sum = v3(sum.x + s.gx, sum.y + s.gy, sum.z + s.gz);
      sq = v3(sq.x + s.gx * s.gx, sq.y + s.gy * s.gy, sq.z + s.gz * s.gz);
      sumA = v3(sumA.x + s.ax, sumA.y + s.ay, sumA.z + s.az);
      got++;
    }
    delay(4);
  }
  if (!got) return false;
  V3 mean = scale(sum, 1.f / got);
  float var = (sq.x + sq.y + sq.z) / got - dot(mean, mean);
  accLP = scale(sumA, 1.f / got);
  bool still = var < 0.5f * 0.5f * 3 && len(mean) < 15.f;   // < ~0.5 dps noise per axis
  V3 stored;
  bool haveStored = loadBias(stored);
  if (still) {
    gyroBias = mean;
    if (!haveStored || len(sub(stored, mean)) > 0.15f) storeBias(mean);
  } else if (haveStored) {
    gyroBias = stored;
  }
  gMean = gyroBias;
  // No stored level yet: assume the current orientation is level until the user sets it.
  if (!levelStored) buildMount(accLP);
  settleTime = 1.5f;
  Serial.printf("[fusion] gyro bias %.2f %.2f %.2f dps (%s)\n", gyroBias.x, gyroBias.y, gyroBias.z,
                still ? "measured" : (haveStored ? "moving, using stored" : "moving, will learn when still"));
  return still;
}

void update(const ImuSample& s, float dt) {
  V3 aS = {s.ax, s.ay, s.az};
  V3 gS = {s.gx - gyroBias.x, s.gy - gyroBias.y, s.gz - gyroBias.z};
  float k = fminf(1.f, dt / 0.05f);
  accLP = v3(accLP.x + (aS.x - accLP.x) * k, accLP.y + (aS.y - accLP.y) * k, accLP.z + (aS.z - accLP.z) * k);

  // ---- level calibration (averaging) ----
  if (calBusy) {
    calSum = v3(calSum.x + aS.x, calSum.y + aS.y, calSum.z + aS.z);
    if (++calN >= 250) {
      buildMount(scale(calSum, 1.f / calN));
      saveMount();
      calBusy = false;
      settleTime = 1.0f;
      q0 = 1; q1 = q2 = q3 = 0; iErr = {0, 0, 0};
    }
  }

  // ---- stationary detection (independent of the bias estimate) + gyro bias tracking ----
  float aMag = len(aS);
  float accDev = fabsf(aMag - 1.f);
  {
    float km = fminf(1.f, dt / 0.4f);
    V3 raw = {s.gx, s.gy, s.gz};
    V3 d = sub(raw, gMean);
    gMean = v3(gMean.x + d.x * km, gMean.y + d.y * km, gMean.z + d.z * km);
    gVar += (dot(d, d) - gVar) * km;
    float da = aMag - len(accLP);
    aVar += (da * da - aVar) * km;
  }
  bool quiet = gVar < 0.8f * 0.8f * 3 && aVar < 0.02f * 0.02f && accDev < 0.06f;
  if (quiet) stillTime += dt; else stillTime = 0;
  bool still = stillTime > 1.0f;
  if (still) {   // the mean rate while still IS the bias; follow it (time constant ~2 s)
    float kb = fminf(1.f, dt / 2.f);
    gyroBias = v3(gyroBias.x + (gMean.x - gyroBias.x) * kb, gyroBias.y + (gMean.y - gyroBias.y) * kb,
                  gyroBias.z + (gMean.z - gyroBias.z) * kb);
    gS = {s.gx - gyroBias.x, s.gy - gyroBias.y, s.gz - gyroBias.z};
  }
  float gMag = len(gS);

  // ---- Mahony AHRS in the car frame ----
  V3 a = toCar(aS);
  V3 w = scale(toCar(gS), (float)M_PI / 180.f);   // rad/s

  float trustA = fmaxf(0.f, 1.f - accDev / ACC_TRUST_GATE_G);
  float trustG = fmaxf(0.f, 1.f - (gMag / GYRO_TRUST_GATE_DPS));
  float trust = trustA * trustG;
  float kp = FUSION_KP * trust;
  // never let the gyro run completely open-loop: a weak pull towards gravity bounds any drift
  if (accDev < 0.3f) kp = fmaxf(kp, FUSION_KP_MIN);
  if (still) kp = fmaxf(kp, 2.0f);
  if (settleTime > 0) { kp = 10.f; trust = 1.f; settleTime -= dt; }

  float an = len(a);
  if (an > 0.3f && kp > 0) {
    a = scale(a, 1.f / an);
    // estimated "up" in body frame
    V3 v = {2 * (q1 * q3 - q0 * q2), 2 * (q0 * q1 + q2 * q3), q0 * q0 - q1 * q1 - q2 * q2 + q3 * q3};
    V3 e = cross(a, v);
    if (trust > 0.5f || still) {   // integral term soaks up residual gyro bias (car frame)
      iErr = v3(iErr.x + e.x * FUSION_KI * dt, iErr.y + e.y * FUSION_KI * dt, iErr.z + e.z * FUSION_KI * dt);
      float il = len(iErr), lim = 0.05f;   // ~3 dps
      if (il > lim) iErr = scale(iErr, lim / il);
    }
    w = v3(w.x + kp * e.x + iErr.x, w.y + kp * e.y + iErr.y, w.z + kp * e.z + iErr.z);
  }
  // integrate quaternion
  float hx = 0.5f * dt * w.x, hy = 0.5f * dt * w.y, hz = 0.5f * dt * w.z;
  float n0 = q0 - q1 * hx - q2 * hy - q3 * hz;
  float n1 = q1 + q0 * hx + q2 * hz - q3 * hy;
  float n2 = q2 + q0 * hy - q1 * hz + q3 * hx;
  float n3 = q3 + q0 * hz + q1 * hy - q2 * hx;
  float qn = 1.f / sqrtf(n0 * n0 + n1 * n1 + n2 * n2 + n3 * n3);
  q0 = n0 * qn; q1 = n1 * qn; q2 = n2 * qn; q3 = n3 * qn;

  V3 up = {2 * (q1 * q3 - q0 * q2), 2 * (q0 * q1 + q2 * q3), q0 * q0 - q1 * q1 - q2 * q2 + q3 * q3};
  att.pitch = atan2f(up.x, sqrtf(up.y * up.y + up.z * up.z)) * 180.f / (float)M_PI;
  att.roll = atan2f(up.y, up.z) * 180.f / (float)M_PI;
  att.trust = trust;
  att.still = still;
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
void rawGravity(float& x, float& y, float& z) { x = accLP.x; y = accLP.y; z = accLP.z; }

}  // namespace fusion
