#include "obd.h"
#include "config.h"
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <ctype.h>
#include <string.h>

namespace obd {

static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
static Data data;

static void setState(State s) {
  portENTER_CRITICAL(&mux);
  data.state = s;
  portEXIT_CRITICAL(&mux);
}

Data latest() {
  portENTER_CRITICAL(&mux);
  Data d = data;
  portEXIT_CRITICAL(&mux);
  return d;
}

bool fresh(uint32_t ms, uint32_t maxAgeMs) { return ms && millis() - ms < maxAgeMs; }

// ------------------------------------------------------------------ BLE
static NimBLEClient* client = nullptr;
static const NimBLERemoteCharacteristic* chrTx = nullptr;   // we write commands here
static volatile bool found = false;
static NimBLEAddress foundAddr;

// adapter response, filled by the notify callback until the '>' prompt
static char rx[512];
static size_t rxLen = 0;            // guarded by mux
static volatile bool prompt = false;

static bool nameMatches(const std::string& n) {
  std::string l = n;
  for (auto& c : l) c = tolower((unsigned char)c);
  for (const char* key : OBD_NAME_KEYS)
    if (l.find(key) != std::string::npos) return true;
  return false;
}

class ScanCb : public NimBLEScanCallbacks {
  void onResult(const NimBLEAdvertisedDevice* d) override {
    if (found || !d->haveName()) return;
    if (!nameMatches(d->getName())) return;
    Serial.printf("[obd] found \"%s\" %s rssi %d\n", d->getName().c_str(),
                  d->getAddress().toString().c_str(), d->getRSSI());
    foundAddr = d->getAddress();
    found = true;
    NimBLEDevice::getScan()->stop();
  }
};

static void onNotify(NimBLERemoteCharacteristic*, uint8_t* p, size_t n, bool) {
  portENTER_CRITICAL(&mux);
  for (size_t i = 0; i < n; i++) {
    char c = (char)p[i];
    if (c == '>') { prompt = true; continue; }
    if (rxLen < sizeof rx - 1) rx[rxLen++] = c;
  }
  rx[rxLen] = 0;
  portEXIT_CRITICAL(&mux);
}

// Find the UART-like characteristics: one that notifies, one that takes writes.
// Vgate adapters use either one characteristic for both or a FFF0-style pair,
// so we just take the first match in a non-standard service.
static bool findChars() {
  chrTx = nullptr;
  const NimBLERemoteCharacteristic* chrRx = nullptr;
  for (auto* svc : client->getServices(true)) {
    std::string su = svc->getUUID().toString();
    Serial.printf("[obd]  service %s\n", su.c_str());
    for (auto* c : svc->getCharacteristics(true)) {
      Serial.printf("[obd]    char %s%s%s%s%s\n", c->getUUID().toString().c_str(),
                    c->canNotify() ? " notify" : "", c->canIndicate() ? " indicate" : "",
                    c->canWrite() ? " write" : "", c->canWriteNoResponse() ? " write-nr" : "");
    }
    // skip GAP / GATT / device information
    if (svc->getUUID() == NimBLEUUID((uint16_t)0x1800) || svc->getUUID() == NimBLEUUID((uint16_t)0x1801) ||
        svc->getUUID() == NimBLEUUID((uint16_t)0x180A))
      continue;
    const NimBLERemoteCharacteristic *r = nullptr, *t = nullptr;   // both must be in the same service
    for (auto* c : svc->getCharacteristics()) {
      if (!r && (c->canNotify() || c->canIndicate())) r = c;
      if (!t && (c->canWrite() || c->canWriteNoResponse())) t = c;
    }
    if (r && t && !chrRx) { chrRx = r; chrTx = t; }   // keep listing the rest for the log
  }
  if (!chrRx || !chrTx) { Serial.println("[obd] no UART characteristics found"); return false; }
  Serial.printf("[obd] rx %s  tx %s\n", chrRx->getUUID().toString().c_str(), chrTx->getUUID().toString().c_str());
  return chrRx->subscribe(chrRx->canNotify(), onNotify);
}

// ------------------------------------------------------------------ ELM327
// Send a command and wait for the '>' prompt. Response (without prompt) in out.
static bool cmd(const char* c, char* out, size_t outSz, uint32_t timeoutMs = OBD_CMD_TIMEOUT_MS) {
  if (!client || !client->isConnected()) return false;
  portENTER_CRITICAL(&mux);
  rxLen = 0; rx[0] = 0; prompt = false;
  portEXIT_CRITICAL(&mux);

  char line[40];
  int n = snprintf(line, sizeof line, "%s\r", c);
  bool nr = !chrTx->canWrite();
  for (int i = 0; i < n; i += 20)   // default ATT MTU payload
    if (!chrTx->writeValue((const uint8_t*)line + i, min(20, n - i), !nr)) return false;

  uint32_t t0 = millis();
  while (!prompt && millis() - t0 < timeoutMs) {
    if (!client->isConnected()) return false;
    vTaskDelay(pdMS_TO_TICKS(5));
  }
  portENTER_CRITICAL(&mux);
  // copy, dropping CR/LF/spaces at the ends; keep line breaks as '\n'
  size_t j = 0;
  for (size_t i = 0; i < rxLen && j < outSz - 1; i++) {
    char ch = rx[i];
    if (ch == '\r') ch = '\n';
    if (ch == '\n' && (j == 0 || out[j - 1] == '\n')) continue;
    out[j++] = ch;
  }
  while (j && (out[j - 1] == '\n' || out[j - 1] == ' ')) j--;
  out[j] = 0;
  bool ok = prompt;
  portEXIT_CRITICAL(&mux);
  return ok;
}

static int hexv(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  c = toupper((unsigned char)c);
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

// Find "41 <pid>" in a mode-01 response and read n data bytes after it.
// Works with or without spaces; takes the first ECU that answers.
static bool parse01(const char* resp, uint8_t pid, uint8_t* out, int n) {
  char hex[256];
  size_t k = 0;
  for (const char* p = resp; *p && k < sizeof hex - 1; p++) {
    if (*p == '\n') hex[k++] = '|';
    else if (hexv(*p) >= 0) hex[k++] = toupper((unsigned char)*p);
  }
  hex[k] = 0;
  char key[5];
  snprintf(key, sizeof key, "41%02X", pid);
  for (char* s = strstr(hex, key); s; s = strstr(s + 1, key)) {
    const char* d = s + 4;
    int i = 0;
    for (; i < n; i++) {
      int hi = hexv(d[2 * i]), lo = d[2 * i] ? hexv(d[2 * i + 1]) : -1;
      if (hi < 0 || lo < 0) break;
      out[i] = (uint8_t)(hi << 4 | lo);
    }
    if (i == n) return true;
  }
  return false;
}

static bool noAnswer(const char* r) {
  return strstr(r, "NO DATA") || strstr(r, "UNABLE") || strstr(r, "ERROR") || strstr(r, "STOPPED") ||
         strstr(r, "BUS INIT") || strstr(r, "?");
}

static uint8_t supported[8];   // bitmap of mode-01 PIDs 0x00..0x3F... (bit for PID p at [p/8])
static bool isSupported(uint8_t pid) {
  if (pid == 0) return true;
  uint8_t i = pid - 1;
  return i < 64 && (supported[i / 8] & (0x80 >> (i % 8)));
}

// 0100 (and 0120 if announced): which PIDs the engine computer supports.
static bool querySupported(uint32_t timeoutMs) {
  char r[256];
  uint8_t b[4];
  memset(supported, 0, sizeof supported);
  if (!cmd("0100", r, sizeof r, timeoutMs)) return false;
  if (!parse01(r, 0x00, b, 4)) {
    Serial.printf("[obd] 0100 -> \"%s\"\n", r);
    return false;
  }
  memcpy(supported, b, 4);
  if (b[3] & 0x01) {
    if (cmd("0120", r, sizeof r) && parse01(r, 0x20, b, 4)) memcpy(supported + 4, b, 4);
  }
  Serial.print("[obd] supported PIDs:");
  for (int p = 1; p <= 64; p++)
    if (isSupported(p)) Serial.printf(" %02X", p);
  Serial.println();
  return true;
}

static bool elmInit() {
  char r[256];
  // ATZ resets the adapter; it answers with its version string
  if (!cmd("ATZ", r, sizeof r, 3000)) { Serial.println("[obd] ATZ: no prompt"); return false; }
  Serial.printf("[obd] ATZ -> \"%s\"\n", r);
  vTaskDelay(pdMS_TO_TICKS(200));
  static const char* setup[] = {"ATE0", "ATL0", "ATS0", "ATH0", "ATAT1", "ATSP0"};
  for (const char* c : setup) {
    if (!cmd(c, r, sizeof r)) { Serial.printf("[obd] %s: no prompt\n", c); return false; }
  }
  return true;
}

static void store(float& v, uint32_t& ms, float x) {
  portENTER_CRITICAL(&mux);
  v = x; ms = millis();
  portEXIT_CRITICAL(&mux);
}

// Read one PID; false on no answer.
static bool poll(uint8_t pid) {
  if (!isSupported(pid)) return true;
  char c[8], r[256];
  uint8_t b[2];
  snprintf(c, sizeof c, "01%02X", pid);
  if (!cmd(c, r, sizeof r)) return false;
  int n = (pid == 0x0C) ? 2 : 1;
  if (!parse01(r, pid, b, n)) return !noAnswer(r) && r[0];   // odd reply: ignore, don't count as dead
  switch (pid) {
    case 0x0D: store(data.speedKmh, data.speedMs, b[0]); break;
    case 0x0C: store(data.rpm, data.rpmMs, (b[0] * 256 + b[1]) / 4.f); break;
    case 0x05: store(data.coolantC, data.coolantMs, b[0] - 40.f); break;
    case 0x0F: store(data.intakeC, data.intakeMs, b[0] - 40.f); break;
    case 0x04: store(data.loadPct, data.loadMs, b[0] * 100.f / 255.f); break;
    case 0x11: store(data.throttlePct, data.throttleMs, b[0] * 100.f / 255.f); break;
  }
  return true;
}

static void pollVolts() {
  char r[64];
  if (!cmd("ATRV", r, sizeof r)) return;
  float v = atof(r);
  if (v > 5 && v < 20) store(data.volts, data.voltsMs, v);
}

static void logStatus() {
  Data d = latest();
  Serial.printf("[obd] %3.0f km/h  %4.0f rpm  coolant %3.0f C  intake %3.0f C  load %3.0f%%  thr %3.0f%%  %.1f V\n",
                d.speedKmh, d.rpm, d.coolantC, d.intakeC, d.loadPct, d.throttlePct, d.volts);
}

// Talk to the car until the adapter disconnects.
static void session() {
  char r[64];
  int fails = 0;
  bool live = false;
  uint32_t lastLog = 0;
  int slow = 0;
  static const uint8_t slowPids[] = {0x05, 0x0F, 0x04, 0x11};

  while (client->isConnected()) {
    if (!live) {
      setState(OBD_INIT);
      Serial.println("[obd] searching protocol (0100)...");
      if (!querySupported(OBD_SEARCH_TIMEOUT_MS)) {
        setState(OBD_NO_ECU);
        Serial.println("[obd] no answer from the car - ignition on?");
        for (int i = 0; i < 30 && client->isConnected(); i++) vTaskDelay(pdMS_TO_TICKS(100));
        continue;
      }
      if (cmd("ATDPN", r, sizeof r)) {
        const char* p = r;
        if (*p == 'A') p++;   // "A6" = automatic, protocol 6
        portENTER_CRITICAL(&mux);
        data.protocol = (uint8_t)hexv(*p);
        portEXIT_CRITICAL(&mux);
      }
      if (cmd("ATDP", r, sizeof r)) Serial.printf("[obd] protocol %u: %s\n", data.protocol, r);
      live = true;
      fails = 0;
      setState(OBD_LIVE);
    }

    // speed + rpm every round, one slow value each round, voltage now and then
    bool ok = poll(0x0D);
    ok = poll(0x0C) && ok;
    ok = poll(slowPids[slow % sizeof slowPids]) && ok;
    if (++slow % 8 == 0) pollVolts();

    fails = ok ? 0 : fails + 1;
    if (fails >= 5) {   // car stopped answering (engine off?) -> search again
      Serial.println("[obd] car stopped answering");
      live = false;
      continue;
    }
    if (millis() - lastLog > 2000) { lastLog = millis(); logStatus(); }
  }
}

static void task(void*) {
  NimBLEScan* scan = NimBLEDevice::getScan();
  scan->setScanCallbacks(new ScanCb(), false);
  scan->setActiveScan(true);   // the name is often only in the scan response
  scan->setInterval(100);
  scan->setWindow(50);         // leave radio time for the phone link

  for (;;) {
    setState(OBD_SCANNING);
    found = false;
    scan->start(5000, false, true);
    while (scan->isScanning() && !found) vTaskDelay(pdMS_TO_TICKS(50));
    if (!found) { vTaskDelay(pdMS_TO_TICKS(OBD_RESCAN_MS)); continue; }

    setState(OBD_CONNECTING);
    if (!client) {
      client = NimBLEDevice::createClient();
      client->setConnectTimeout(5000);
    }
    if (!client->connect(foundAddr)) {
      Serial.println("[obd] connect failed");
      vTaskDelay(pdMS_TO_TICKS(2000));
      continue;
    }
    Serial.printf("[obd] connected, MTU %u\n", client->getMTU());
    if (findChars() && elmInit()) session();
    if (client->isConnected()) client->disconnect();
    Serial.println("[obd] disconnected");
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

// Stack in PSRAM (internal RAM is short). The task does no flash access itself; while the
// display pauses for a flash write (no SD card) it is simply not scheduled.
void begin() {
  xTaskCreatePinnedToCoreWithCaps(task, "obd", 6144, nullptr, 2, nullptr, 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

}  // namespace obd
