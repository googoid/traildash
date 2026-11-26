// Remote IMU — Seeed XIAO ESP32-C6 + MPU-6050 / MPU-6500 (GY-521 style board).
//
// Wiring (XIAO pin -> IMU board):  3V3 -> VCC,  GND -> GND,  D4 (GPIO22) -> SDA,  D5 (GPIO23) -> SCL
// AD0 low or open = address 0x68, high = 0x69 (both are tried).
//
// Reads the IMU at 250 Hz, removes the gyro bias (measured at power-up while the car
// stands still, remembered in flash for power-ups while driving), and sends batches of
// 10 samples to TrailDash over BLE (~25 packets/s). All filtering, mounting and
// "set level" happen on TrailDash, exactly as for its built-in IMU.
//
// LED: fast blink = searching for TrailDash, short flash every 3 s = connected.
// Serial (115200): status every 2 s.
#include <Arduino.h>
#include <Wire.h>
#include <Preferences.h>
#include <NimBLEDevice.h>
#include "imu_link.h"

#define PIN_SDA       22   // D4
#define PIN_SCL       23   // D5
#define PIN_LED       15   // user LED, active low
#define SAMPLE_HZ     250
#define BATCH         10   // samples per packet (fewer if the MTU is small)

// gyro bias calibration at power-up
#define BIAS_SAMPLES  500  // 2 s
#define BIAS_MAX_SD   0.6f // deg/s: more spread than this = the car is moving, keep the stored bias

// ------------------------------------------------------------------ MPU-6050 / 6500
static uint8_t mpuAddr = 0x68;

enum : uint8_t {
  SMPLRT_DIV = 0x19, CONFIG = 0x1A, GYRO_CONFIG = 0x1B, ACCEL_CONFIG = 0x1C, ACCEL_CONFIG2 = 0x1D,
  ACCEL_XOUT_H = 0x3B, PWR_MGMT_1 = 0x6B, WHO_AM_I = 0x75,
};

static bool wr(uint8_t reg, uint8_t v) {
  Wire.beginTransmission(mpuAddr);
  Wire.write(reg);
  Wire.write(v);
  return Wire.endTransmission() == 0;
}

static bool rd(uint8_t reg, uint8_t* buf, size_t n) {
  Wire.beginTransmission(mpuAddr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(mpuAddr, (uint8_t)n) != n) return false;
  for (size_t i = 0; i < n; i++) buf[i] = Wire.read();
  return true;
}

static bool mpuBegin() {
  uint8_t id = 0;
  for (uint8_t a : {0x68, 0x69}) {
    mpuAddr = a;
    if (rd(WHO_AM_I, &id, 1)) break;
    id = 0;
  }
  if (!id) return false;
  Serial.printf("[imu] WHO_AM_I 0x%02x at 0x%02x (%s)\n", id, mpuAddr,
                id == 0x68 ? "MPU-6050" : id == 0x70 ? "MPU-6500" : id == 0x71 ? "MPU-9250" : "compatible");
  wr(PWR_MGMT_1, 0x80);              // reset
  delay(100);
  wr(PWR_MGMT_1, 0x01);              // wake, clock from the X gyro PLL
  delay(10);
  wr(CONFIG, 0x04);                  // DLPF ~20 Hz (gyro; accel too on the 6050)
  wr(SMPLRT_DIV, 1000 / SAMPLE_HZ - 1);
  wr(GYRO_CONFIG, 0x00);             // ±250 deg/s, 131 LSB/(deg/s)
  wr(ACCEL_CONFIG, 0x08);            // ±4 g, 8192 LSB/g
  if (id != 0x68) wr(ACCEL_CONFIG2, 0x04);   // 6500 family: separate accel DLPF ~20 Hz
  delay(50);
  return true;
}

struct Raw { float ax, ay, az, gx, gy, gz; };   // g, deg/s

static bool mpuRead(Raw& r) {
  uint8_t b[14];
  if (!rd(ACCEL_XOUT_H, b, 14)) return false;
  auto v = [&](int i) { return (int16_t)(b[i] << 8 | b[i + 1]); };
  r.ax = v(0) / 8192.f;  r.ay = v(2) / 8192.f;  r.az = v(4) / 8192.f;   // b[6..7] = temperature
  r.gx = v(8) / 131.f;   r.gy = v(10) / 131.f;  r.gz = v(12) / 131.f;
  return true;
}

// ------------------------------------------------------------------ gyro bias
static float bias[3] = {0, 0, 0};

static void calibrateBias() {
  double sum[3] = {0}, sq[3] = {0};
  int n = 0;
  for (int i = 0; i < BIAS_SAMPLES; i++) {
    Raw r;
    if (mpuRead(r)) {
      const float g[3] = {r.gx, r.gy, r.gz};
      for (int k = 0; k < 3; k++) { sum[k] += g[k]; sq[k] += g[k] * g[k]; }
      n++;
    }
    delay(1000 / SAMPLE_HZ);
  }
  Preferences p;
  p.begin("rimu", false);
  float sd = 0;
  for (int k = 0; k < 3 && n; k++) sd = fmaxf(sd, sqrtf(fmax(0.0, sq[k] / n - (sum[k] / n) * (sum[k] / n))));
  if (n > BIAS_SAMPLES / 2 && sd < BIAS_MAX_SD) {
    for (int k = 0; k < 3; k++) bias[k] = sum[k] / n;
    p.putBytes("bias", bias, sizeof bias);
    Serial.printf("[imu] gyro bias %+.2f %+.2f %+.2f deg/s (spread %.2f), saved\n", bias[0], bias[1], bias[2], sd);
  } else if (p.getBytes("bias", bias, sizeof bias) == sizeof bias) {
    Serial.printf("[imu] moving at power-up (spread %.2f): stored gyro bias %+.2f %+.2f %+.2f\n", sd, bias[0],
                  bias[1], bias[2]);
  } else {
    Serial.printf("[imu] moving at power-up (spread %.2f) and no stored bias - TrailDash will learn it\n", sd);
  }
  p.end();
}

// ------------------------------------------------------------------ BLE
static NimBLEClient* client = nullptr;
static NimBLERemoteCharacteristic* volatile chr = nullptr;   // set while connected and ready
static volatile int batch = BATCH;
static volatile bool found = false;
static NimBLEAddress target;

class ClientCb : public NimBLEClientCallbacks {
  void onDisconnect(NimBLEClient*, int reason) override {
    chr = nullptr;
    Serial.printf("[ble] disconnected, reason %d\n", reason);
  }
};

class ScanCb : public NimBLEScanCallbacks {
  void onResult(const NimBLEAdvertisedDevice* d) override {
    if (found || !d->isAdvertisingService(NimBLEUUID(IMU_LINK_ADV_UUID))) return;
    target = d->getAddress();
    found = true;
    Serial.printf("[ble] found TrailDash %s rssi %d\n", target.toString().c_str(), d->getRSSI());
    NimBLEDevice::getScan()->stop();
  }
};

static bool connect() {
  if (!client) {
    client = NimBLEDevice::createClient();
    client->setClientCallbacks(new ClientCb(), false);
    client->setConnectTimeout(5000);
    // 15..30 ms: one packet every 40 ms always finds a connection event
    client->setConnectionParams(12, 24, 0, 400);
  }
  if (!client->connect(target)) { Serial.println("[ble] connect failed"); return false; }
  NimBLERemoteService* svc = client->getService(IMU_SERVICE_UUID);
  NimBLERemoteCharacteristic* c = svc ? svc->getCharacteristic(IMU_CHAR_UUID) : nullptr;
  if (!c || !c->canWriteNoResponse()) {
    Serial.println("[ble] TrailDash has no remote-IMU characteristic (old firmware?)");
    client->disconnect();
    return false;
  }
  int fit = (client->getMTU() - 3 - IMU_LINK_HDR) / IMU_LINK_SAMPLE;
  batch = constrain(fit, 1, BATCH);
  Serial.printf("[ble] connected, MTU %u, %d samples per packet\n", client->getMTU(), batch);
  chr = c;
  return true;
}

// ------------------------------------------------------------------ sampling task
static uint32_t sentPackets = 0, failedPackets = 0, readErrors = 0;   // written by the sampling task only
static volatile float lastA[3];

static int16_t q(float v, float scale) { return (int16_t)constrain(lroundf(v * scale), -32768L, 32767L); }

static void sampleTask(void*) {
  uint8_t pkt[IMU_LINK_HDR + IMU_LINK_MAX_SAMPLES * IMU_LINK_SAMPLE];
  uint16_t seq = 0;      // number of the next sample
  int n = 0;             // samples in pkt
  TickType_t wake = xTaskGetTickCount();
  for (;;) {
    vTaskDelayUntil(&wake, pdMS_TO_TICKS(1000 / SAMPLE_HZ));
    Raw r;
    if (!mpuRead(r)) { readErrors++; continue; }
    lastA[0] = r.ax; lastA[1] = r.ay; lastA[2] = r.az;

    if (n == 0) {
      pkt[0] = IMU_LINK_VERSION;
      pkt[2] = seq & 0xFF; pkt[3] = seq >> 8;
      const uint16_t per = 1000000 / SAMPLE_HZ;
      pkt[4] = per & 0xFF; pkt[5] = per >> 8;
    }
    const int16_t v[6] = {q(r.ax, IMU_LINK_ACC_PER_G), q(r.ay, IMU_LINK_ACC_PER_G), q(r.az, IMU_LINK_ACC_PER_G),
                          q(r.gx - bias[0], IMU_LINK_GYR_PER_DPS), q(r.gy - bias[1], IMU_LINK_GYR_PER_DPS),
                          q(r.gz - bias[2], IMU_LINK_GYR_PER_DPS)};
    uint8_t* p = pkt + IMU_LINK_HDR + n * IMU_LINK_SAMPLE;
    for (int i = 0; i < 6; i++) { p[2 * i] = v[i] & 0xFF; p[2 * i + 1] = (uint16_t)v[i] >> 8; }
    seq++;
    if (++n < batch) continue;

    pkt[1] = n;
    NimBLERemoteCharacteristic* c = chr;
    if (c) {
      if (c->writeValue(pkt, IMU_LINK_HDR + n * IMU_LINK_SAMPLE, false)) sentPackets++;
      else failedPackets++;   // radio busy: these samples are lost, the receiver sees the seq gap
    }
    n = 0;
  }
}

// ------------------------------------------------------------------ setup / loop
void setup() {
  Serial.begin(115200);
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, HIGH);
  delay(500);
  Serial.println("\n[boot] remote IMU");

  Wire.begin(PIN_SDA, PIN_SCL, 400000);
  while (!mpuBegin()) {
    Serial.println("[imu] no MPU-6050/6500 found on D4/D5 - check wiring");
    for (int i = 0; i < 6; i++) { digitalWrite(PIN_LED, i & 1); delay(150); }
    delay(1000);
  }
  calibrateBias();

  NimBLEDevice::init("TrailDash IMU");
  NimBLEDevice::setMTU(IMU_LINK_HDR + IMU_LINK_MAX_SAMPLES * IMU_LINK_SAMPLE + 3);
  NimBLEDevice::setPower(9);   // dBm; the car is small but full of metal
  NimBLEScan* scan = NimBLEDevice::getScan();
  scan->setScanCallbacks(new ScanCb(), false);
  scan->setActiveScan(false);  // the service UUID is in the advertisement itself
  scan->setInterval(100);
  scan->setWindow(60);

  xTaskCreate(sampleTask, "imu", 4096, nullptr, 5, nullptr);
}

void loop() {
  static uint32_t lastStatus = 0;
  uint32_t now = millis();

  if (!chr) {
    // searching: blink while the scan runs
    found = false;
    NimBLEScan* scan = NimBLEDevice::getScan();
    scan->start(3000, false, true);
    while (scan->isScanning() && !found) {
      digitalWrite(PIN_LED, (millis() / 150) & 1);
      delay(20);
    }
    digitalWrite(PIN_LED, HIGH);
    if (found && !connect()) delay(1000);
  } else {
    digitalWrite(PIN_LED, (now % 3000) < 40 ? LOW : HIGH);   // short flash: connected
    delay(20);
  }

  if (now - lastStatus >= 2000) {
    lastStatus = now;
    Serial.printf("[status] %s  packets %lu sent, %lu failed  read errors %lu  acc %+.2f %+.2f %+.2f g\n",
                  chr ? "connected" : "searching", (unsigned long)sentPackets, (unsigned long)failedPackets,
                  (unsigned long)readErrors, lastA[0], lastA[1], lastA[2]);
  }
}
