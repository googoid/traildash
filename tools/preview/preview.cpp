// Host-side preview: renders UI screens to BMP files so the design can be
// checked without flashing.
//
//   tools/preview/build.sh   -> writes preview/*.bmp (+ .png on macOS)
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <chrono>
#include "ui.h"

static void writeBmp(const std::string& path, const uint16_t* fb) {
  const int W = 480, H = 480, rowBytes = W * 3;
  FILE* f = fopen(path.c_str(), "wb");
  if (!f) return;
  uint32_t fileSize = 54 + rowBytes * H;
  uint8_t hdr[54] = {'B', 'M'};
  auto put32 = [&](int off, uint32_t v) { memcpy(hdr + off, &v, 4); };
  auto put16 = [&](int off, uint16_t v) { memcpy(hdr + off, &v, 2); };
  put32(2, fileSize); put32(10, 54); put32(14, 40); put32(18, W); put32(22, (uint32_t)-H);
  put16(26, 1); put16(28, 24); put32(34, rowBytes * H);
  fwrite(hdr, 1, 54, f);
  for (int y = 0; y < H; y++) {
    for (int x = 0; x < W; x++) {
      uint16_t p = fb[y * W + x];
      // mask outside the round panel
      float dx = x + 0.5f - 240, dy = y + 0.5f - 240;
      bool in = dx * dx + dy * dy <= 240.f * 240.f;
      uint8_t r = ((p >> 11) & 31) * 255 / 31, g = ((p >> 5) & 63) * 255 / 63, b = (p & 31) * 255 / 31;
      uint8_t px[3] = {in ? b : (uint8_t)18, in ? g : (uint8_t)18, in ? r : (uint8_t)18};
      fwrite(px, 1, 3, f);
    }
  }
  fclose(f);
}

int main(int argc, char** argv) {
  const char* outDir = argc > 1 ? argv[1] : "preview";
  static uint16_t fb[480 * 480];
  if (!ui::begin(malloc, nullptr)) return 1;

  struct Shot { const char* name; UiState s; };
  auto mk = [](Screen sc, float p, float r) { UiState s; s.screen = sc; s.pitch = p; s.roll = r; s.t = 1.0f;
    s.peakRollL = 14; s.peakRollR = 27; s.peakPitchUp = 31; s.peakPitchDn = 12; return s; };
  Shot shots[] = {
    {"dual_mild",    mk(SCREEN_DUAL, 8.3f, -6.4f)},
    {"dual_warn",    mk(SCREEN_DUAL, 17.f, 23.6f)},
    {"dual_danger",  mk(SCREEN_DUAL, -38.f, 31.f)},
    {"peaks",        mk(SCREEN_PEAKS, 0, 0)},
    {"hold",         mk(SCREEN_DUAL, 2.f, 1.f)},
    {"toast",        mk(SCREEN_DUAL, 0.f, 0.f)},
    {"speed",        mk(SCREEN_SPEED, 2.f, 1.f)},
    {"speed_nophone",mk(SCREEN_SPEED, 2.f, 1.f)},
    {"speed_hold",   mk(SCREEN_SPEED, 2.f, 1.f)},
    {"speed_obd",    mk(SCREEN_SPEED, 2.f, 1.f)},
    {"speed_obd_wait", mk(SCREEN_SPEED, 2.f, 1.f)},
    {"dual_nosensor", mk(SCREEN_DUAL, 0.f, 0.f)},
    {"engine",        mk(SCREEN_ENGINE, 0.f, 0.f)},
    {"engine_idle",   mk(SCREEN_ENGINE, 0.f, 0.f)},
    {"engine_hot",    mk(SCREEN_ENGINE, 0.f, 0.f)},
    {"engine_off",    mk(SCREEN_ENGINE, 0.f, 0.f)},
    {"compare",       mk(SCREEN_COMPARE, 0.f, 0.f)},
    {"compare_start", mk(SCREEN_COMPARE, 0.f, 0.f)},
  };
  shots[16].s.obdKmh = 103; shots[16].s.gpsKmh = 98.6f; shots[16].s.cmpKm = 37.2f; shots[16].s.cmpPct = 4.3f;
  shots[17].s.obdKmh = 12; shots[17].s.gpsKmh = -1;
  {   // cruising, warm
    UiState& e = shots[12].s;
    e.obdState = 4; e.rpm = 2140; e.coolantC = 88; e.volts = 14.1f; e.intakeC = 31; e.loadPct = 42;
    e.fuelLph = 11.8f; e.fuelL100 = 13.4f; e.avgL100 = 15.2f;
  }
  {   // cold idle, standing: L/h
    UiState& e = shots[13].s;
    e.obdState = 4; e.rpm = 780; e.coolantC = 44; e.volts = 12.9f; e.intakeC = 30; e.loadPct = 28;
    e.fuelLph = 1.6f;
  }
  {   // near redline, overheating, not charging
    UiState& e = shots[14].s;
    e.obdState = 4; e.rpm = 4870; e.coolantC = 108; e.volts = 12.4f; e.intakeC = 45; e.loadPct = 91;
    e.fuelLph = 38.f; e.fuelL100 = 41.5f; e.avgL100 = 16.0f;
  }
  shots[15].s.obdState = 3;
  shots[11].s.sensorOk = false;
  shots[4].s.holdProgress = 0.62f;
  shots[5].s.toast = "LEVEL SET"; shots[5].s.toastAge = 1.0f;
  for (int i = 6; i <= 8; i++) { shots[i].s.maxKmh = 112; shots[i].s.tripKm = 48.6f; }
  shots[6].s.nav = NAV_LIVE; shots[6].s.speedKmh = 87.4f; shots[6].s.courseDeg = 47.f;
  for (int i = 9; i <= 10; i++) { shots[i].s.maxKmh = 112; shots[i].s.tripKm = 48.6f; }
  shots[9].s.nav = NAV_OBD; shots[9].s.speedKmh = 64; shots[9].s.courseDeg = 47.f;
  shots[10].s.nav = NAV_LIVE; shots[10].s.speedKmh = 63; shots[10].s.courseDeg = 47.f; shots[10].s.obdNote = "OBD NO ANSWER";
  shots[8].s.nav = NAV_LIVE; shots[8].s.speedKmh = 0; shots[8].s.courseDeg = 312.f; shots[8].s.holdProgress = 0.45f;

  for (auto& sh : shots) {
    auto t0 = std::chrono::high_resolution_clock::now();
    // render in 32-row strips like the device does
    const uint16_t* bg = ui::background(sh.s);
    ui::prepare(sh.s);
    static uint16_t strip[480 * 32];
    for (int y0 = 0; y0 < 480; y0 += 32) {
      if (bg) memcpy(strip, bg + y0 * 480, sizeof strip);
      ui::renderStrip(strip, y0, 32, sh.s);
      memcpy(fb + y0 * 480, strip, sizeof strip);
    }
    auto t1 = std::chrono::high_resolution_clock::now();
    printf("%-12s %.2f ms (host)\n", sh.name, std::chrono::duration<double, std::milli>(t1 - t0).count());
    writeBmp(std::string(outDir) + "/" + sh.name + ".bmp", fb);
  }
  { UiState tp; tp.testPattern = true; ui::renderFull(fb, tp); writeBmp(std::string(outDir) + "/testpattern.bmp", fb); }
  UiState sp; sp.splash1 = "INCLINOMETER"; sp.splash2 = "starting"; sp.splashProgress = 0.4f;
  ui::renderFull(fb, sp);
  writeBmp(std::string(outDir) + "/splash.bmp", fb);
  return 0;
}
