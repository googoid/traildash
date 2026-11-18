#include "remote.h"
#include "config.h"
#include "imu_link.h"
#include <Arduino.h>
#include <esp_heap_caps.h>

namespace remote {

static constexpr int QLEN = 128;            // ~0.5 s at 250 Hz
struct Q { int16_t v[6]; };
static Q* q = nullptr;                      // PSRAM (internal RAM is short)
static int head = 0, count = 0;             // guarded by mux
static float period = 0.004f;
static uint32_t lastRxMs = 0;
static uint16_t nextSeq = 0;
static bool haveSeq = false;
static Stats st = {0, 0, 0, 0};
static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;

static constexpr uint16_t NO_CONN = 0xFFFF;
static volatile uint16_t conn = NO_CONN;

bool begin() {
  q = (Q*)heap_caps_malloc(QLEN * sizeof(Q), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  return q != nullptr;
}

void onPacket(const uint8_t* p, size_t n, uint16_t c) {
  if (!q || n < IMU_LINK_HDR || p[0] != IMU_LINK_VERSION) return;
  int cnt = p[1];
  if (cnt < 1 || cnt > IMU_LINK_MAX_SAMPLES || n < (size_t)(IMU_LINK_HDR + cnt * IMU_LINK_SAMPLE)) return;
  uint16_t seq = p[2] | p[3] << 8;
  uint16_t perUs = p[4] | p[5] << 8;
  if (conn != c) {
    conn = c;
    Serial.printf("[remote] IMU connected (handle %u)\n", c);
  }
  portENTER_CRITICAL(&mux);
  if (haveSeq) {
    uint16_t gap = (uint16_t)(seq - nextSeq);
    if (gap && gap < 0x8000) st.lost += gap;
  }
  nextSeq = seq + cnt;
  haveSeq = true;
  if (perUs >= 1000 && perUs <= 50000) period = perUs * 1e-6f;
  for (int i = 0; i < cnt; i++) {
    const uint8_t* s = p + IMU_LINK_HDR + i * IMU_LINK_SAMPLE;
    if (count == QLEN) {                     // sensor task stalled: drop the oldest
      head = (head + 1) % QLEN; count--; st.overflow++;
    }
    Q& d = q[(head + count) % QLEN];
    for (int k = 0; k < 6; k++) d.v[k] = (int16_t)(s[2 * k] | s[2 * k + 1] << 8);
    count++;
  }
  st.packets++;
  st.samples += cnt;
  lastRxMs = millis();
  portEXIT_CRITICAL(&mux);
}

void onDisconnect(uint16_t c) {
  if (c != conn) return;
  conn = NO_CONN;
  portENTER_CRITICAL(&mux);
  haveSeq = false;
  portEXIT_CRITICAL(&mux);
  Serial.println("[remote] IMU disconnected");
}

bool isRemoteConn(uint16_t c) { return c != NO_CONN && c == conn; }
bool connected() { return conn != NO_CONN; }

bool live() {
  portENTER_CRITICAL(&mux);
  bool ok = st.samples && millis() - lastRxMs < REMOTE_STALE_MS;
  portEXIT_CRITICAL(&mux);
  return ok && connected();
}

int take(ImuSample* out, int max, float& dt) {
  int n = 0;
  portENTER_CRITICAL(&mux);
  dt = period;
  while (n < max && count) {
    const Q& d = q[head];
    ImuSample& s = out[n++];
    s.ax = d.v[0] / IMU_LINK_ACC_PER_G; s.ay = d.v[1] / IMU_LINK_ACC_PER_G; s.az = d.v[2] / IMU_LINK_ACC_PER_G;
    s.gx = d.v[3] / IMU_LINK_GYR_PER_DPS; s.gy = d.v[4] / IMU_LINK_GYR_PER_DPS; s.gz = d.v[5] / IMU_LINK_GYR_PER_DPS;
    head = (head + 1) % QLEN;
    count--;
  }
  portEXIT_CRITICAL(&mux);
  return n;
}

Stats stats() {
  portENTER_CRITICAL(&mux);
  Stats s = st;
  portEXIT_CRITICAL(&mux);
  return s;
}

}  // namespace remote
