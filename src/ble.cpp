#include "ble.h"
#include "config.h"
#include "remote.h"
#include "settings.h"
#include "imu_link.h"
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <esp_heap_caps.h>

// the remote IMU finds TrailDash by the advertised NAV service
static constexpr bool streq(const char* a, const char* b) { return *a == *b && (!*a || streq(a + 1, b + 1)); }
static_assert(streq(NAV_SERVICE_UUID, IMU_LINK_ADV_UUID), "shared/imu_link.h must advertise-match NAV_SERVICE_UUID");

namespace blelink {

static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
static NavFix fix;
static volatile int nConnected = 0;   // phone + remote IMU
static constexpr int MAX_PERIPHERAL_LINKS = 2;   // see CONFIG_BT_NIMBLE_MAX_CONNECTIONS (+1 for the OBD adapter)

static uint16_t rd16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }

class ServerCb : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* s, NimBLEConnInfo& info) override {
    nConnected = s->getConnectedCount();
    // ~30-50 ms interval is plenty for 1 Hz data and keeps the radio quiet
    // (the remote IMU asks for a shorter one once it identifies itself, see ImuCb)
    s->updateConnParams(info.getConnHandle(), 24, 40, 0, 400);
    Serial.printf("[ble] connected (%d)\n", nConnected);
    // keep advertising so the other one (phone / remote IMU) can still connect
    if (nConnected < MAX_PERIPHERAL_LINKS) NimBLEDevice::startAdvertising();
  }
  void onDisconnect(NimBLEServer* s, NimBLEConnInfo& info, int reason) override {
    remote::onDisconnect(info.getConnHandle());
    nConnected = s->getConnectedCount();
    Serial.printf("[ble] disconnected, reason %d (%d left)\n", reason, nConnected);
  }
};

class NavCb : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* c, NimBLEConnInfo& info) override {
    NimBLEAttValue v = c->getValue();
    if (v.size() < 10 || v[0] != 1) return;
    const uint8_t* p = v.data();
    NavFix f;
    f.speedValid = p[1] & NAV_FLAG_SPEED;
    f.courseValid = p[1] & NAV_FLAG_COURSE;
    f.speedMs = rd16(p + 2) * 0.01f;
    uint16_t sa = rd16(p + 4);
    f.speedAccMs = sa == 0xFFFF ? -1.f : sa * 0.01f;
    f.courseDeg = fmodf(rd16(p + 6) * 0.01f, 360.f);
    uint16_t ca = rd16(p + 8);
    f.courseAccDeg = ca == 0xFFFF ? -1.f : ca * 0.01f;
    f.rxMs = millis();
    portENTER_CRITICAL(&mux);
    f.seq = fix.seq + 1;
    fix = f;
    portEXIT_CRITICAL(&mux);
  }
};

// Settings for the phone app (see ble.h). Runs in the NimBLE host task.
static volatile uint8_t command = CMD_NONE;
static volatile uint8_t status = 0;   // bit0 card, bit1 sensor, bit2 level set, bit3 leveling
static char* cfgBuf = nullptr;        // PSRAM (internal RAM is short); only the host task uses it

static bool cfgBuffer() {
  if (!cfgBuf) cfgBuf = (char*)heap_caps_malloc(CFG_MAX_LEN + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  return cfgBuf;
}

class CfgCb : public NimBLECharacteristicCallbacks {
  void onRead(NimBLECharacteristic* c, NimBLEConnInfo& info) override {
    if (!cfgBuffer()) return;
    char* buf = cfgBuf;
    const size_t size = CFG_MAX_LEN;
    uint8_t st = status;
    int n = snprintf(buf, size, "v=%d\ncard=%d\nsensor=%d\nlevel=%d\nleveling=%d\n", CFG_PROTO_VER,
                     st & 1, (st >> 1) & 1, (st >> 2) & 1, (st >> 3) & 1);
    n += settings::formatCompact(buf + n, size - n);
    c->setValue((const uint8_t*)buf, n);   // long reads: called once, the rest is read from this copy
  }
  void onWrite(NimBLECharacteristic* c, NimBLEConnInfo& info) override {
    if (!cfgBuffer()) return;
    NimBLEAttValue v = c->getValue();
    size_t n = v.size() < CFG_MAX_LEN ? v.size() : CFG_MAX_LEN;
    memcpy(cfgBuf, v.data(), n);
    cfgBuf[n] = 0;
    bool ok = settings::update(cfgBuf);
    Serial.printf("[ble] settings from the app%s\n", ok ? "" : ": nothing known in it");
  }
};

class CtrlCb : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* c, NimBLEConnInfo& info) override {
    NimBLEAttValue v = c->getValue();
    if (v.size() == 1 && v[0] >= CMD_SET_LEVEL && v[0] <= CMD_RESET_TRIP_B) command = v[0];
  }
};

uint8_t takeCommand() { return __atomic_exchange_n(&command, (uint8_t)CMD_NONE, __ATOMIC_ACQ_REL); }

void setStatus(bool card, bool sensor, bool levelSet, bool leveling) {
  status = (card ? 1 : 0) | (sensor ? 2 : 0) | (levelSet ? 4 : 0) | (leveling ? 8 : 0);
}

// Remote IMU (shared/imu_link.h): batches of raw samples, ~25 writes/s.
class ImuCb : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* c, NimBLEConnInfo& info) override {
    uint16_t h = info.getConnHandle();
    if (!remote::isRemoteConn(h))   // first packet: 15-30 ms interval, so every packet finds an event
      NimBLEDevice::getServer()->updateConnParams(h, 12, 24, 0, 400);
    NimBLEAttValue v = c->getValue();
    remote::onPacket(v.data(), v.size(), h);
  }
};

bool begin() {
  remote::begin();
  if (!NimBLEDevice::init(BLE_DEVICE_NAME)) return false;
  NimBLEServer* srv = NimBLEDevice::createServer();
  srv->setCallbacks(new ServerCb());
  srv->advertiseOnDisconnect(true);

  NimBLEService* svc = srv->createService(NAV_SERVICE_UUID);
  NimBLECharacteristic* ch = svc->createCharacteristic(
      NAV_CHAR_UUID, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
  ch->setCallbacks(new NavCb());
  svc->createCharacteristic(CFG_CHAR_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE, CFG_MAX_LEN)
      ->setCallbacks(new CfgCb());
  svc->createCharacteristic(CTRL_CHAR_UUID, NIMBLE_PROPERTY::WRITE, 1)->setCallbacks(new CtrlCb());

  NimBLEService* imuSvc = srv->createService(IMU_SERVICE_UUID);
  NimBLECharacteristic* imuCh = imuSvc->createCharacteristic(
      IMU_CHAR_UUID, NIMBLE_PROPERTY::WRITE_NR, IMU_LINK_HDR + IMU_LINK_MAX_SAMPLES * IMU_LINK_SAMPLE);
  imuCh->setCallbacks(new ImuCb());

  // 128-bit UUID in the advertisement (iOS can only scan by service UUID in the
  // background); the name doesn't fit next to it, so it goes in the scan response.
  NimBLEAdvertisementData ad, sr;
  ad.setFlags(BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP);
  ad.addServiceUUID(NimBLEUUID(NAV_SERVICE_UUID));
  sr.setName(BLE_DEVICE_NAME);
  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  adv->setAdvertisementData(ad);
  adv->setScanResponseData(sr);
  adv->enableScanResponse(true);
  bool ok = adv->start();
  Serial.printf("[ble] advertising as \"%s\"%s\n", BLE_DEVICE_NAME, ok ? "" : " - FAILED");
  return ok;
}

bool connected() { return nConnected - (remote::connected() ? 1 : 0) > 0; }

NavFix latest() {
  portENTER_CRITICAL(&mux);
  NavFix f = fix;
  portEXIT_CRITICAL(&mux);
  return f;
}

}  // namespace blelink
