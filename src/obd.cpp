#include "obd.h"
#include "config.h"
#include "settings.h"
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <ctype.h>
#include <stdarg.h>
#include <string.h>

namespace obd {

static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
static Data data;

// ------------------------------------------------------------------ diagnostics log
// Everything about the connection goes to Serial and into a buffer that the SD task
// appends to /incl/obd.txt, so a test drive without a laptop can be read afterwards.
static constexpr size_t LOG_BUF = 4096;
static char* logBuf = nullptr;   // PSRAM (internal RAM is short), allocated in begin()
static size_t logLen = 0;   // guarded by mux

static void olog(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
static void olog(const char* fmt, ...) {
  char line[256];
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(line, sizeof line, fmt, ap);
  va_end(ap);
  if (n <= 0) return;
  if (n > (int)sizeof line - 1) n = sizeof line - 1;
  Serial.print(line);
  char ts[16];
  int t = snprintf(ts, sizeof ts, "%8.1f ", millis() * 1e-3f);
  portENTER_CRITICAL(&mux);
  if (logBuf && logLen + t + n < LOG_BUF) {   // full: drop (the SD task empties it every few seconds)
    memcpy(logBuf + logLen, ts, t);
    memcpy(logBuf + logLen + t, line, n);
    logLen += t + n;
  }
  portEXIT_CRITICAL(&mux);
}

size_t takeLog(char* out, size_t size) {
  portENTER_CRITICAL(&mux);
  size_t n = logLen < size ? logLen : size;
  memcpy(out, logBuf, n);
  memmove(logBuf, logBuf + n, logLen - n);
  logLen -= n;
  portEXIT_CRITICAL(&mux);
  return n;
}

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
  if (cfg.obdName[0] && l.find(cfg.obdName) != std::string::npos) return true;
  for (const char* key : OBD_NAME_KEYS)
    if (l.find(key) != std::string::npos) return true;
  return false;
}

class ScanCb : public NimBLEScanCallbacks {
  void onResult(const NimBLEAdvertisedDevice* d) override {
    if (found || !d->haveName()) return;
    if (!nameMatches(d->getName())) return;
    olog("[obd] found \"%s\" %s rssi %d\n", d->getName().c_str(),
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
    olog("[obd]  service %s\n", su.c_str());
    for (auto* c : svc->getCharacteristics(true)) {
      olog("[obd]    char %s%s%s%s%s\n", c->getUUID().toString().c_str(),
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
  if (!chrRx || !chrTx) { olog("[obd] no UART characteristics found\n"); return false; }
  olog("[obd] rx %s  tx %s\n", chrRx->getUUID().toString().c_str(), chrTx->getUUID().toString().c_str());
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

static bool countOk = true;    // adapter takes a response count ("010D1": return after one reply)
static uint8_t supported[8];   // bitmap of mode-01 PIDs 0x00..0x3F... (bit for PID p at [p/8])
static bool isSupported(uint8_t pid) {
  if (pid == 0) return true;
  uint8_t i = pid - 1;
  return i < 64 && (supported[i / 8] & (0x80 >> (i % 8)));
}

// 0100 (and 0120 if announced): which PIDs the engine computer supports.
static bool querySupported(uint32_t timeoutMs, bool verbose) {
  char r[256];
  uint8_t b[4];
  memset(supported, 0, sizeof supported);
  if (!cmd("0100", r, sizeof r, timeoutMs)) return false;
  if (!parse01(r, 0x00, b, 4)) {
    if (verbose) olog("[obd] 0100 -> \"%s\"\n", r);
    return false;
  }
  memcpy(supported, b, 4);
  if (b[3] & 0x01) {
    if (cmd("0120", r, sizeof r) && parse01(r, 0x20, b, 4)) memcpy(supported + 4, b, 4);
  }
  char list[200];
  size_t k = 0;
  for (int p = 1; p <= 64; p++)
    if (isSupported(p)) k += snprintf(list + k, sizeof list - k, " %02X", p);
  list[k] = 0;
  olog("[obd] supported PIDs:%s\n", list);
  return true;
}

static bool elmInit() {
  char r[256];
  // ATZ resets the adapter; it answers with its version string
  if (!cmd("ATZ", r, sizeof r, 3000)) { olog("[obd] ATZ: no prompt\n"); return false; }
  olog("[obd] ATZ -> \"%s\"\n", r);
  vTaskDelay(pdMS_TO_TICKS(200));
  static const char* setup[] = {"ATE0", "ATL0", "ATS0", "ATH0", "ATAT1", "ATSP0"};
  for (const char* c : setup) {
    if (!cmd(c, r, sizeof r)) { olog("[obd] %s: no prompt\n", c); return false; }
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
  const bool count = countOk && data.protocol >= 6 && data.protocol <= 9;   // CAN: one reply, no waiting
  snprintf(c, sizeof c, count ? "01%02X1" : "01%02X", pid);
  if (!cmd(c, r, sizeof r)) return false;
  if (count && strchr(r, '?')) { countOk = false; olog("[obd] adapter doesn't take response counts\n"); return true; }
  int n = (pid == 0x0C || pid == 0x10) ? 2 : 1;
  if (!parse01(r, pid, b, n)) return !noAnswer(r) && r[0];   // odd reply: ignore, don't count as dead
  switch (pid) {
    case 0x0D: store(data.speedKmh, data.speedMs, b[0]); break;
    case 0x0C: store(data.rpm, data.rpmMs, (b[0] * 256 + b[1]) / 4.f); break;
    case 0x05: store(data.coolantC, data.coolantMs, b[0] - 40.f); break;
    case 0x0F: store(data.intakeC, data.intakeMs, b[0] - 40.f); break;
    case 0x04: store(data.loadPct, data.loadMs, b[0] * 100.f / 255.f); break;
    case 0x11: store(data.throttlePct, data.throttleMs, b[0] * 100.f / 255.f); break;
    case 0x10: store(data.mafGs, data.mafMs, (b[0] * 256 + b[1]) / 100.f); break;
  }
  return true;
}

static void pollVolts() {
  char r[64];
  if (!cmd("ATRV", r, sizeof r)) return;
  float v = atof(r);
  if (v > 5 && v < 20) store(data.volts, data.voltsMs, v);
}

// ------------------------------------------------------------------ Toyota data blocks
// A 2005 Toyota gives the gear only through its own mode-21 "data blocks" (21 xx) of the engine
// ECU, which also runs the gearbox: block 21DA byte A = current gear (1..5, found by probing).
// When the ECU answers 21DA at connect, the header stays on 7E0 for everything (the mode-01 PIDs
// come from the same ECU), so no header switching per request.
//
// The selector comes from blocks 21E6 and 21D9 (see toyotaStep). With cfg.obdProbe, these blocks
// are logged to probe.txt whenever they change, with speed, rpm, throttle, gear and selector.
static constexpr size_t PROBE_BUF = 16384;
static char* probeBuf = nullptr;   // PSRAM; guarded by mux
static size_t probeLen = 0;

static void plog(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
static void plog(const char* fmt, ...) {
  char line[600];
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(line, sizeof line, fmt, ap);
  va_end(ap);
  if (n <= 0) return;
  if (n > (int)sizeof line - 1) n = sizeof line - 1;
  char ts[16];
  int t = snprintf(ts, sizeof ts, "%9.2f ", millis() * 1e-3f);
  Serial.printf("[probe] %s%s", ts, line);   // live too: watch it over USB while testing in the car
  portENTER_CRITICAL(&mux);
  if (probeBuf && probeLen + t + n < PROBE_BUF) {
    memcpy(probeBuf + probeLen, ts, t);
    memcpy(probeBuf + probeLen + t, line, n);
    probeLen += t + n;
  }
  portEXIT_CRITICAL(&mux);
}

size_t takeProbe(char* out, size_t size) {
  portENTER_CRITICAL(&mux);
  size_t n = probeLen < size ? probeLen : size;
  if (n) memcpy(out, probeBuf, n);
  memmove(probeBuf, probeBuf + n, probeLen - n);
  probeLen -= n;
  portEXIT_CRITICAL(&mux);
  return n;
}

static uint32_t fnv(const char* s) {
  uint32_t h = 2166136261u;
  for (; *s; s++) h = (h ^ (uint8_t)*s) * 16777619u;
  return h;
}

static bool isHexStr(const char* s, size_t n) {
  for (size_t i = 0; i < n; i++) if (hexv(s[i]) < 0) return false;
  return true;
}

// Keep only our reply's lines: the ISO-TP length ("01E"), its frames ("0:61..", "1:..") or a
// single frame "61xx..". Other lines are mode-01 answers to some other tester on the bus.
// Result on one line, frames separated by spaces. Returns true if a complete reply is there.
static bool cleanReply(char* r, uint8_t lid, bool* truncated) {
  char out[512];
  size_t k = 0;
  char key[5];
  snprintf(key, sizeof key, "61%02X", lid);
  int expect = -1, got = 0;
  bool single = false;
  for (char* line = r; *line;) {
    char* end = strchr(line, '\n');
    size_t n = end ? (size_t)(end - line) : strlen(line);
    bool keep = false;
    if (n == 3 && isHexStr(line, 3) && expect < 0) {   // multi-frame length
      expect = hexv(line[0]) * 256 + hexv(line[1]) * 16 + hexv(line[2]);
      keep = true;
    } else if (n >= 3 && expect >= 0 && hexv(line[0]) >= 0 && line[1] == ':') {
      keep = true;
      got += (int)(n - 2) / 2;
    } else if (n >= 4 && !strncasecmp(line, key, 4)) {
      keep = single = true;
    }
    if (keep && k + n + 2 < sizeof out) {
      if (k) out[k++] = ' ';
      memcpy(out + k, line, n);
      k += n;
    }
    line += n;
    if (*line == '\n') line++;
  }
  out[k] = 0;
  strcpy(r, out);
  bool multiOk = expect > 0 && got >= expect && strncasecmp(strchr(out, ':') ? strchr(out, ':') + 1 : "", key, 4) == 0;
  if (truncated) *truncated = expect > 0 && got < expect;
  return single || multiOk;
}

static bool toyota = false;    // engine ECU answers 21DA; header kept at 7E0

// One data block. `single`: the reply fits one CAN frame, so the response count can be used
// (this adapter cuts multi-frame replies short with it). false = no answer.
static bool readBlock(uint8_t lid, char* r, size_t size, bool single) {
  char c[8];
  const bool count = single && countOk;
  snprintf(c, sizeof c, count ? "21%02X1" : "21%02X", lid);
  if (!cmd(c, r, size)) return false;
  if (count && strchr(r, '?')) {   // adapter doesn't know response counts
    countOk = false;
    olog("[obd] adapter doesn't take response counts\n");
    return readBlock(lid, r, size, single);
  }
  return cleanReply(r, lid, nullptr);
}

static bool setHeader(uint16_t ecu) {
  char c[12], r[32];
  snprintf(c, sizeof c, "ATSH%03X", ecu);
  return cmd(c, r, sizeof r);
}

static inline uint8_t hexByte(const char* p) { return (uint8_t)(hexv(p[0]) * 16 + hexv(p[1])); }

// 21DA byte A -> gear (1..5)
static bool readGear(char* r, size_t size) {
  if (!readBlock(0xDA, r, size, true)) return false;
  uint8_t g = hexByte(r + 4);
  portENTER_CRITICAL(&mux);
  data.gear = g; data.gearMs = millis();
  portEXIT_CRITICAL(&mux);
  return true;
}

// Called once the car answers: does the engine ECU give 21DA on 7E0 (with mode 01 still working)?
static void detectToyota() {
  char r[128];
  uint8_t b[1];
  toyota = false;
  if (data.protocol < 6 || data.protocol > 9 || !setHeader(0x7E0)) return;
  for (int i = 0; i < 2 && !toyota; i++)   // twice: a stray frame from another tester may get in
    toyota = readGear(r, sizeof r) && cmd("010D", r, sizeof r) && parse01(r, 0x0D, b, 1);
  if (toyota) olog("[obd] Toyota gear (21DA) available\n");
  else setHeader(0x7DF);
}

// Selector (found by moving the lever through P R N D 4 3 2 L, parked, engine running):
//   21E6 byte A bit 0x10 set -> P or N (neutral start switch; the ECU has no separate P input,
//                               no block tells P from N). Bit 0x04 is the brake pedal.
//   21D9 bytes C, D (multi-frame; frame 1 holds the gearbox temperature):
//     R 04 00   D 00 04   4 00 06   3 00 01   2 02 00   L 03 00
static uint32_t hashDA = 0, hashE6 = 0, hashD9 = 0;

// 21D9 bytes C, D -> 'R' 'D' '4' '3' '2' 'L', 0 = no lever switch closed (between positions)
static char rangeFromD9(uint8_t c, uint8_t d) {
  if (c == 0x04) return 'R';
  if (c == 0x03) return 'L';
  if (c == 0x02) return '2';
  if (d & 0x02) return '4';
  if (d & 0x04) return 'D';
  if (d & 0x01) return '3';
  return 0;
}

static void logBlock(uint8_t lid, uint32_t& hash, const char* r) {
  if (!cfg.obdProbe) return;
  uint32_t h = fnv(r);
  if (h == hash) return;
  hash = h;
  Data d = latest();
  plog("21%02X  %3.0f km/h %4.0f rpm %3.0f%% thr gear %u%c | %s\n", lid, d.speedKmh, d.rpm, d.throttlePct,
       d.gear, d.selector ? d.selector : '?', r);
}

static void toyotaStep() {
  static char r[512];
  if (readGear(r, sizeof r)) logBlock(0xDA, hashDA, r);
  if (!readBlock(0xE6, r, sizeof r, true)) return;
  logBlock(0xE6, hashE6, r);
  char sel = 'N';
  if (!(hexByte(r + 4) & 0x10)) {
    if (!readBlock(0xD9, r, sizeof r, false)) return;
    logBlock(0xD9, hashD9, r);
    const char* f0 = strstr(r, "0:61D9");   // first frame: 61 D9 A B C D
    if (!f0 || strlen(f0) < 14) return;
    sel = rangeFromD9(hexByte(f0 + 10), hexByte(f0 + 12));
    if (!sel) return;   // lever between positions: keep the last one
  }
  portENTER_CRITICAL(&mux);
  data.selector = sel; data.selectorMs = millis();
  portEXIT_CRITICAL(&mux);
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
  int attempts = 0;   // protocol searches without an answer (logged first and every 20th)
  uint32_t lastLog = 0;
  int slow = 0;
  static const uint8_t slowPids[] = {0x05, 0x0F, 0x04, 0x11};

  while (client->isConnected()) {
    if (!live) {
      setState(OBD_INIT);
      bool verbose = attempts % 20 == 0;
      if (verbose) olog("[obd] searching protocol (0100)...\n");
      if (!querySupported(OBD_SEARCH_TIMEOUT_MS, verbose)) {
        setState(OBD_NO_ECU);
        if (verbose) olog("[obd] no answer from the car - ignition on? (attempt %d)\n", attempts + 1);
        attempts++;
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
      if (cmd("ATDP", r, sizeof r)) olog("[obd] protocol %u: %s\n", data.protocol, r);
      if (cmd("ATRV", r, sizeof r)) olog("[obd] adapter voltage %s\n", r);
      live = true;
      fails = 0;
      attempts = 0;
      setState(OBD_LIVE);
      detectToyota();
      if (cfg.obdProbe) plog("[probe] car answering, protocol %u%s\n", data.protocol, toyota ? ", gear from 21DA" : "");
    }

    // speed, rpm, airflow every round, one slow value each round, voltage now and then
    bool ok = poll(0x0D);
    ok = poll(0x0C) && ok;
    ok = poll(0x10) && ok;
    ok = poll(slowPids[slow % sizeof slowPids]) && ok;
    if (++slow % 8 == 0) pollVolts();
    if (toyota && ok) toyotaStep();

    fails = ok ? 0 : fails + 1;
    if (fails >= 5) {   // car stopped answering (engine off?) -> search again
      olog("[obd] car stopped answering\n");
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
      olog("[obd] connect failed\n");
      vTaskDelay(pdMS_TO_TICKS(2000));
      continue;
    }
    olog("[obd] connected, MTU %u\n", client->getMTU());
    if (findChars() && elmInit()) session();
    if (client->isConnected()) client->disconnect();
    olog("[obd] disconnected\n");
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

// Stack in PSRAM (internal RAM is short). The task does no flash access itself; while the
// display pauses for a flash write (no SD card) it is simply not scheduled.
void begin() {
  logBuf = (char*)heap_caps_malloc(LOG_BUF, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (cfg.obdProbe) probeBuf = (char*)heap_caps_malloc(PROBE_BUF, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  xTaskCreatePinnedToCoreWithCaps(task, "obd", 6144, nullptr, 2, nullptr, 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

}  // namespace obd
