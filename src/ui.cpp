#include "ui.h"
#include "gfx.h"
#include "config.h"
#include "settings.h"
#include <string.h>
#include <initializer_list>
#include <stdio.h>
#include <math.h>

namespace ui {

// ------------------------------------------------------------------ palette
static constexpr uint32_t BG_EDGE = 0x04070B;
static constexpr uint32_t BG_MID  = 0x101923;
static constexpr uint32_t TXT     = 0xEEF4F9;
static constexpr uint32_t DIM     = 0x7D8B99;
static constexpr uint32_t FAINT   = 0x26323F;
static constexpr uint32_t TICK    = 0x8796A5;
static constexpr uint32_t TICK2   = 0x45525F;
static constexpr uint32_t ACCENT  = 0x38D9F5;
static constexpr uint32_t GREEN   = 0x2EE6A6;
static constexpr uint32_t AMBER   = 0xFFB020;
static constexpr uint32_t RED     = 0xFF3B5C;
static constexpr uint32_t TIRE    = 0x16191E;
static constexpr uint32_t DARKP   = 0x2A3139;

static constexpr float CX = 240, CY = 240;
static constexpr size_t FB_BYTES = 480 * 480 * 2;

static uint16_t* bgDual = nullptr;
static uint16_t* bgPlain = nullptr;
static uint16_t* bgSpeed = nullptr;
static uint16_t* bgDualD = nullptr;     // same, with the red danger glow baked in
static uint16_t* bgPlainD = nullptr;
static uint16_t* bgSpeedD = nullptr;

// ------------------------------------------------------------------ helpers
static inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
static inline float deg2rad(float d) { return d * (float)M_PI / 180.f; }

static uint32_t sevColor(float v, float warn, float danger) {
  v = fabsf(v);
  if (v < warn - 3) return GREEN;
  if (v < warn) return lerpColor(GREEN, AMBER, (v - (warn - 3)) / 3.f);
  if (v < danger - 3) return AMBER;
  if (v < danger) return lerpColor(AMBER, RED, (v - (danger - 3)) / 3.f);
  return RED;
}

// local point (ox,oy) maps to (cx,cy), rotated by deg (clockwise) and scaled by s
static Xf pivotXf(float cx, float cy, float deg, float s, float ox, float oy) {
  Xf x = Xf::rot(cx, cy, deg, s);
  x.tx = cx - x.a * ox - x.c * oy;
  x.ty = cy - x.b * ox - x.d * oy;
  return x;
}

static Paint linX(const Xf& xf, float x0, float y0, uint32_t c0, float x1, float y1, uint32_t c1, float a = 1.f) {
  float X0, Y0, X1, Y1;
  xf.apply(x0, y0, X0, Y0);
  xf.apply(x1, y1, X1, Y1);
  return Paint::linear(X0, Y0, c0, X1, Y1, c1, a);
}

static void circleX(const Xf& xf, float s, float x, float y, float r, const Paint& p) {
  float X, Y; xf.apply(x, y, X, Y);
  gfx.circle(X, Y, r * s, p);
}

static void arcX(const Xf& xf, float deg, float s, float x, float y, float r0, float r1, float a0, float a1, const Paint& p) {
  float X, Y; xf.apply(x, y, X, Y);
  gfx.arc(X, Y, r0 * s, r1 * s, a0 + deg, a1 + deg, p);
}

static void lineX(const Xf& xf, float s, float x0, float y0, float x1, float y1, float w, const Paint& p) {
  float X0, Y0, X1, Y1;
  xf.apply(x0, y0, X0, Y0);
  xf.apply(x1, y1, X1, Y1);
  gfx.line(X0, Y0, X1, Y1, w * s, p);
}

// point on a circle around the screen centre; angle 0 = 12 o'clock, clockwise
static void polar(float r, float deg, float& x, float& y) {
  x = CX + r * sinf(deg2rad(deg));
  y = CY - r * cosf(deg2rad(deg));
}

static void radialLine(float r0, float r1, float deg, float w, const Paint& p) {
  float x0, y0, x1, y1;
  polar(r0, deg, x0, y0);
  polar(r1, deg, x1, y1);
  gfx.line(x0, y0, x1, y1, w, p);
}


static void fmtDeg(char* buf, float v) {
  int d = (int)lroundf(fabsf(v));
  snprintf(buf, 12, "%d°", d);
}

// ------------------------------------------------------------------ vehicles
// Rear view of an off-road SUV. Local coords: ground contact at y=0, y down.
static void drawRearTruck(const Xf& xf, float s, float deg, uint32_t sev) {
  // slope surface under the wheels
  {
    float slab[8] = {-128, 3, 128, 3, 128, 13, -128, 13};
    gfx.poly(slab, 4, linX(xf, 0, 3, scaleColor(sev, 0.55f), 0, 13, BG_MID), &xf);
    lineX(xf, s, -128, 2.5f, 128, 2.5f, 2.5f, Paint(sev));
  }
  // tyres
  for (int side = -1; side <= 1; side += 2) {
    float x0 = side < 0 ? -64 : 38;
    gfx.roundRect(x0, -32, 26, 34, 7, Paint(TIRE), &xf);
    gfx.roundRect(x0 + 3, -29, 20, 28, 5, Paint(0x22272E), &xf);
    for (int k = 0; k < 4; k++)  // tread
      lineX(xf, s, x0 + 5, -25 + k * 7, x0 + 21, -25 + k * 7, 1.6f, Paint(0x30363E));
  }
  // axle + diff
  gfx.rect(-40, -21, 80, 6, Paint(DARKP), &xf);
  circleX(xf, s, 0, -18, 9, Paint(0x343B44));
  // lower body
  gfx.roundRect(-68, -60, 136, 36, 7, linX(xf, 0, -60, 0xE4EAF0, 0, -24, 0x9AA6B2), &xf);
  // bumper
  gfx.roundRect(-70, -31, 140, 9, 3, Paint(0x262C33), &xf);
  // cabin
  {
    float cab[8] = {-60, -59, -49, -100, 49, -100, 60, -59};
    gfx.poly(cab, 4, linX(xf, 0, -100, 0xF4F7FA, 0, -59, 0xC5CED7), &xf);
    float win[8] = {-50, -65, -42, -94, 42, -94, 50, -65};
    gfx.poly(win, 4, linX(xf, 0, -94, 0x30506A, 0, -65, 0x141F2A), &xf);
    float glare[8] = {-30, -94, -18, -94, -34, -65, -46, -65};
    gfx.poly(glare, 4, Paint(0xFFFFFF, 0.10f), &xf);
  }
  // roof rack
  gfx.rect(-46, -107, 92, 5, Paint(DARKP), &xf);
  gfx.rect(-40, -103, 4, 4, Paint(DARKP), &xf);
  gfx.rect(36, -103, 4, 4, Paint(DARKP), &xf);
  // tail lights
  gfx.roundRect(-66, -56, 11, 16, 3, Paint(0xE5173F), &xf);
  gfx.roundRect(55, -56, 11, 16, 3, Paint(0xE5173F), &xf);
  gfx.roundRect(-64, -54, 4, 6, 1.5f, Paint(0xFF8A9E), &xf);
  gfx.roundRect(60, -54, 4, 6, 1.5f, Paint(0xFF8A9E), &xf);
  // spare wheel
  circleX(xf, s, 0, -42, 15, Paint(TIRE));
  circleX(xf, s, 0, -42, 8, linX(xf, 0, -50, 0xA9B4BF, 0, -34, 0x56606B));
  circleX(xf, s, 0, -42, 3, Paint(DARKP));
  (void)deg;
}

// Side view, nose to the right.
static void drawSideTruck(const Xf& xf, float s, float deg, uint32_t sev) {
  {
    float slab[8] = {-138, 3, 138, 3, 138, 13, -138, 13};
    gfx.poly(slab, 4, linX(xf, 0, 3, scaleColor(sev, 0.55f), 0, 13, BG_MID), &xf);
    lineX(xf, s, -138, 2.5f, 138, 2.5f, 2.5f, Paint(sev));
  }
  // spare wheel on the tailgate
  gfx.roundRect(-104, -78, 11, 42, 4, Paint(TIRE), &xf);
  // lower body
  {
    float body[14] = {-94, -22, -96, -62, -40, -64, 50, -66, 88, -60, 95, -46, 93, -22};
    gfx.poly(body, 7, linX(xf, 0, -66, 0xE4EAF0, 0, -22, 0x95A1AD), &xf);
  }
  // cabin + glass
  {
    float cab[8] = {-90, -63, -84, -104, 14, -104, 44, -65};
    gfx.poly(cab, 4, linX(xf, 0, -104, 0xF4F7FA, 0, -63, 0xCDD5DD), &xf);
    float w1[8] = {-80, -69, -76, -98, -34, -98, -34, -69};
    gfx.poly(w1, 4, linX(xf, 0, -98, 0x30506A, 0, -69, 0x141F2A), &xf);
    float w2[8] = {-28, -69, -28, -98, 10, -98, 34, -69};
    gfx.poly(w2, 4, linX(xf, 0, -98, 0x30506A, 0, -69, 0x141F2A), &xf);
    float glare[8] = {-6, -98, 2, -98, -12, -69, -20, -69};
    gfx.poly(glare, 4, Paint(0xFFFFFF, 0.10f), &xf);
    float mirror[8] = {30, -72, 38, -76, 40, -68, 32, -66};
    gfx.poly(mirror, 4, Paint(DARKP), &xf);
  }
  // door seam + handle
  lineX(xf, s, -31, -62, -31, -26, 1.4f, Paint(0x8A96A2));
  gfx.roundRect(-24, -54, 10, 3, 1.5f, Paint(0x6B7682), &xf);
  gfx.roundRect(-74, -54, 10, 3, 1.5f, Paint(0x6B7682), &xf);
  // roof rack
  gfx.rect(-78, -110, 86, 5, Paint(DARKP), &xf);
  // lights + bumpers
  gfx.roundRect(85, -58, 9, 8, 2.5f, Paint(0xFFE7A0), &xf);
  gfx.roundRect(-98, -58, 5, 10, 2, Paint(0xE5173F), &xf);
  gfx.roundRect(84, -34, 16, 10, 3, Paint(0x262C33), &xf);
  gfx.roundRect(-101, -34, 14, 10, 3, Paint(0x262C33), &xf);
  // wheel arches + tyres
  const float wx[2] = {-58, 60};
  for (float x : wx) {
    arcX(xf, deg, s, x, -22, 0, 27, -90, 90, Paint(0x1B2027));
    circleX(xf, s, x, -22, 22, Paint(TIRE));
    circleX(xf, s, x, -22, 13, linX(xf, x, -35, 0xB4BEC8, x, -9, 0x56606B));
    circleX(xf, s, x, -22, 4.5f, Paint(DARKP));
  }
}

// ------------------------------------------------------------------ static layers
static void drawBgBase() {
  Path& p = gfx.path_;
  p.clear(); p.rect(0, 0, 480, 480);
  gfx.fill(p, Paint::radial(CX, CY, 250, BG_MID, BG_EDGE));
  gfx.ring(CX, CY, 233, 237, Paint(0x1A2430));
}

static void drawScale(float centerDeg, float sign, float warn, float danger) {
  // zone band
  auto band = [&](float a0, float a1, uint32_t c, float alpha) {
    gfx.arc(CX, CY, 222, 228, centerDeg + a0, centerDeg + a1, Paint(c, alpha));
  };
  band(-warn, warn, GREEN, 0.28f);
  band(warn, danger, AMBER, 0.38f);
  band(-danger, -warn, AMBER, 0.38f);
  band(danger, 52, RED, 0.45f);
  band(-52, -danger, RED, 0.45f);
  // ticks + labels
  for (int a = -50; a <= 50; a += 5) {
    float d = centerDeg + a;
    bool major = (a % 10) == 0;
    radialLine(major ? 201 : 209, 217, d, major ? 2.6f : 1.6f, Paint(major ? TICK : TICK2));
    if (major && a != 0) {
      float x, y; polar(188, d, x, y);
      char b[4]; snprintf(b, sizeof b, "%d", abs(a));
      gfx.text(font_tiny, b, x, y + 5, DIM, 0);
    }
  }
  (void)sign;
}

static void buildDual() {
  gfx.setTarget(bgDual);
  drawBgBase();
  drawScale(0, 1, cfg.rollWarnDeg, cfg.rollDangerDeg);
  drawScale(180, 1, cfg.pitchWarnDeg, cfg.pitchDangerDeg);
  // centre divider
  gfx.line(64, 240, 416, 240, 1.4f, Paint(0x223140));
  float diamond[8] = {240, 235, 245, 240, 240, 245, 235, 240};
  gfx.poly(diamond, 4, Paint(0x2E4152));
  gfx.text(font_small, "ROLL", 70, 228, DIM, -1);
  gfx.text(font_small, "PITCH", 70, 266, DIM, -1);
  // fixed horizon references behind the vehicles
  for (int x = 120; x < 360; x += 14) {
    gfx.line(x, 112 + 40, x + 6, 112 + 40, 1.2f, Paint(0x2A3846));
    gfx.line(x, 370 + 40, x + 6, 370 + 40, 1.2f, Paint(0x2A3846));
  }
}

static void buildPlain() {
  gfx.setTarget(bgPlain);
  drawBgBase();
}

// speed screen layout
static constexpr float SP_HEAD_Y = 118, SP_STATUS_Y = 148, SP_NUM_Y = 292, SP_UNIT_Y = 324;
static constexpr float SP_CELL_L = 190, SP_CELL_R = 290, SP_LABEL_Y = 364, SP_VALUE_Y = 394;

static void buildSpeed() {
  gfx.setTarget(bgSpeed);
  drawBgBase();
  gfx.ring(CX, CY, 222.5f, 223.5f, Paint(0x1C2733));   // compass track
  gfx.text(font_small, "km/h", CX, SP_UNIT_Y, DIM, 0);
  gfx.line(150, 340, 330, 340, 1.2f, Paint(FAINT));
  gfx.line(CX, 350, CX, 400, 1.2f, Paint(FAINT));
  gfx.text(font_tiny, "MAX  km/h", SP_CELL_L, SP_LABEL_Y, DIM, 0);
  gfx.text(font_tiny, "TRIP  km", SP_CELL_R, SP_LABEL_Y, DIM, 0);
}

bool begin(void* (*alloc)(size_t), void (*flush)(void*, size_t)) {
  bgDual = (uint16_t*)alloc(FB_BYTES);
  bgPlain = (uint16_t*)alloc(FB_BYTES);
  bgDualD = (uint16_t*)alloc(FB_BYTES);
  bgPlainD = (uint16_t*)alloc(FB_BYTES);
  bgSpeed = (uint16_t*)alloc(FB_BYTES);
  bgSpeedD = (uint16_t*)alloc(FB_BYTES);
  if (!bgDual || !bgPlain || !bgDualD || !bgPlainD || !bgSpeed || !bgSpeedD) return false;
  buildDual();
  buildPlain();
  buildSpeed();
  uint16_t* src[3] = {bgDual, bgPlain, bgSpeed};
  uint16_t* dst[3] = {bgDualD, bgPlainD, bgSpeedD};
  for (int i = 0; i < 3; i++) {
    memcpy(dst[i], src[i], FB_BYTES);
    gfx.setTarget(dst[i]);
    gfx.glowRing(CX, CY, 239, 30, RED, 0.75f);
  }
  if (flush) for (uint16_t* b : {bgDual, bgPlain, bgSpeed, bgDualD, bgPlainD, bgSpeedD}) flush(b, FB_BYTES);
  return true;
}

bool inDanger(const UiState& s) {
  return fabsf(s.roll) >= cfg.rollDangerDeg || fabsf(s.pitch) >= cfg.pitchDangerDeg;
}

// ------------------------------------------------------------------ screens
static void pointer(float deg, uint32_t c) {
  float tri[6] = {0, -199, -9, -231, 9, -231};
  Xf xf = Xf::rot(CX, CY, deg);
  gfx.poly(tri, 3, Paint(c), &xf);
  float hl[6] = {0, -205, -3, -226, 3, -226};
  gfx.poly(hl, 3, Paint(0xFFFFFF, 0.35f), &xf);
}

static void peakMark(float deg) {
  float tri[6] = {0, -219, -5, -230, 5, -230};
  Xf xf = Xf::rot(CX, CY, deg);
  gfx.poly(tri, 3, Paint(0xC8D2DC, 0.55f), &xf);
}

static void renderDual(const UiState& s) {
  const float roll = clampf(s.roll, -52, 52), pitch = clampf(s.pitch, -52, 52);
  uint32_t rc = sevColor(s.roll, cfg.rollWarnDeg, cfg.rollDangerDeg);
  uint32_t pc = sevColor(s.pitch, cfg.pitchWarnDeg, cfg.pitchDangerDeg);

  // ---- ROLL (top) ----
  gfx.arc(CX, CY, 222, 228, fminf(0, roll), fmaxf(0, roll), Paint(rc));
  if (s.peakRollR > 0.5f) peakMark(clampf(s.peakRollR, 0, 52));
  if (s.peakRollL > 0.5f) peakMark(-clampf(s.peakRollL, 0, 52));
  pointer(roll, rc);

  gfx.setClip(0, 0, 480, 236);
  {
    const float sc = 0.80f;
    Xf xf = pivotXf(CX, 112, s.roll, sc, 0, -50);
    drawRearTruck(xf, sc, s.roll, rc);
  }
  gfx.resetClip();

  // ---- PITCH (bottom) ----  nose-up => vehicle turns counter-clockwise
  gfx.arc(CX, CY, 222, 228, 180 - fmaxf(0, pitch), 180 - fminf(0, pitch), Paint(pc));
  if (s.peakPitchUp > 0.5f) peakMark(180 - clampf(s.peakPitchUp, 0, 52));
  if (s.peakPitchDn > 0.5f) peakMark(180 + clampf(s.peakPitchDn, 0, 52));
  pointer(180 - pitch, pc);

  gfx.setClip(0, 244, 480, 480);
  {
    const float sc = 0.72f;
    Xf xf = pivotXf(CX, 370, -s.pitch, sc, 0, -52);
    drawSideTruck(xf, sc, -s.pitch, pc);
  }
  gfx.resetClip();

  // ---- numbers ----
  char b[12];
  fmtDeg(b, s.roll);
  gfx.text(font_huge, b, CX + 10, 230, fabsf(s.roll) >= cfg.rollWarnDeg ? rc : TXT, 0);
  fmtDeg(b, s.pitch);
  gfx.text(font_huge, b, CX + 10, 318, fabsf(s.pitch) >= cfg.pitchWarnDeg ? pc : TXT, 0);

  const char* rdir = s.roll > 0.5f ? "RIGHT" : (s.roll < -0.5f ? "LEFT" : "LEVEL");
  const char* pdir = s.pitch > 0.5f ? "UP" : (s.pitch < -0.5f ? "DOWN" : "LEVEL");
  gfx.text(font_small, rdir, 410, 228, DIM, 1);
  gfx.text(font_small, pdir, 410, 266, DIM, 1);
  // little direction arrows next to the words
  if (s.roll > 0.5f || s.roll < -0.5f) {
    float x = 410 - gfx.textWidth(font_small, rdir) - 12, y = 222;
    float sgn = s.roll > 0 ? 1.f : -1.f;
    float tri[6] = {x + 5 * sgn, y, x - 4 * sgn, y - 6, x - 4 * sgn, y + 6};
    gfx.poly(tri, 3, Paint(rc));
  }
  if (s.pitch > 0.5f || s.pitch < -0.5f) {
    float x = 410 - gfx.textWidth(font_small, pdir) - 12, y = 260;
    float sgn = s.pitch > 0 ? -1.f : 1.f;
    float tri[6] = {x, y + 5 * sgn, x - 6, y - 4 * sgn, x + 6, y - 4 * sgn};
    gfx.poly(tri, 3, Paint(pc));
  }
}

static void peakCell(float cx, float cy, const char* label, float v, float warn, float danger, int dir) {
  uint32_t c = v >= warn ? sevColor(v, warn, danger) : TXT;
  gfx.text(font_tiny, label, cx, cy - 34, DIM, 0);
  char b[12]; fmtDeg(b, v);
  gfx.text(font_big, b, cx + 6, cy + 20, c, 0);
  // severity bar
  float frac = clampf(v / (danger * 1.25f), 0, 1);
  gfx.roundRect(cx - 44, cy + 32, 88, 5, 2.5f, Paint(0x1E2935));
  if (frac > 0.02f) gfx.roundRect(cx - 44, cy + 32, 88 * frac, 5, 2.5f, Paint(sevColor(v, warn, danger)));
  (void)dir;
}

static void renderPeaks(const UiState& s) {
  gfx.text(font_small, "PEAK ANGLES", CX, 96, ACCENT, 0);
  gfx.line(170, 108, 310, 108, 1.2f, Paint(ACCENT, 0.35f));
  gfx.line(CX, 140, CX, 380, 1.2f, Paint(FAINT));
  gfx.line(110, 260, 370, 260, 1.2f, Paint(FAINT));
  peakCell(165, 200, "ROLL LEFT", s.peakRollL, cfg.rollWarnDeg, cfg.rollDangerDeg, -1);
  peakCell(315, 200, "ROLL RIGHT", s.peakRollR, cfg.rollWarnDeg, cfg.rollDangerDeg, 1);
  peakCell(165, 320, "NOSE UP", s.peakPitchUp, cfg.pitchWarnDeg, cfg.pitchDangerDeg, -1);
  peakCell(315, 320, "NOSE DOWN", s.peakPitchDn, cfg.pitchWarnDeg, cfg.pitchDangerDeg, 1);
  gfx.text(font_tiny, "DOUBLE-TAP TO RESET", CX, 412, DIM, 0);
  gfx.text(font_tiny, s.levelSet ? "LEVEL REFERENCE SET" : "LEVEL NOT SET  -  HOLD 2 s", CX, 432,
           s.levelSet ? 0x4E5D6B : AMBER, 0);
}

// Heading-up compass ring, big speed, max + trip.
static const char* compassName(float deg) {
  static const char* n[8] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
  return n[(int)floorf(fmodf(deg + 22.5f + 360.f, 360.f) / 45.f) % 8];
}

static void renderSpeed(const UiState& s) {
  const bool hasCourse = s.courseDeg >= 0;
  const float head = hasCourse ? s.courseDeg : 0;
  const float a = hasCourse ? 1.f : 0.35f;   // dim the ring when the heading is unknown

  // ---- compass ring (bearing b is drawn at screen angle b - heading) ----
  for (int b = 0; b < 360; b += 10) {
    float d = b - head;
    bool major = b % 30 == 0;
    radialLine(major ? 205 : 211, 220, d, major ? 2.6f : 1.6f, Paint(major ? TICK : TICK2, a));
  }
  for (int k = 0; k < 8; k++) {
    float d = k * 45 - head, x, y;
    bool card = k % 2 == 0;
    polar(card ? 186 : 188, d, x, y);
    static const char* n[8] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
    if (card) gfx.text(font_small, n[k], x, y + 6, k == 0 ? RED : TXT, 0, a);
    else gfx.text(font_tiny, n[k], x, y + 5, DIM, 0, a);
  }
  // fixed lubber line at 12 o'clock
  {
    float tri[6] = {CX, 26, CX - 8, 10, CX + 8, 10};
    gfx.poly(tri, 3, Paint(ACCENT));
    gfx.line(CX, 24, CX, 38, 2.4f, Paint(ACCENT));
  }

  // ---- heading + link status ----
  char b[24];
  if (hasCourse) {
    snprintf(b, sizeof b, "%s  %03d°", compassName(head), (int)lroundf(head) % 360);
    gfx.text(font_med, b, CX, SP_HEAD_Y, TXT, 0);
  } else {
    gfx.text(font_med, "- -", CX, SP_HEAD_Y, DIM, 0);
  }
  const bool live = s.nav == NAV_LIVE || s.nav == NAV_OBD;
  const char* st = s.nav == NAV_OBD ? "OBD" : s.nav == NAV_LIVE ? "GPS"
                 : (s.nav == NAV_NO_FIX ? "WAITING FOR GPS" : "PHONE NOT CONNECTED");
  char stb[48];
  if (s.obdNote && s.nav != NAV_OBD) {   // adapter found but no speed from it yet
    if (s.nav == NAV_LIVE) snprintf(stb, sizeof stb, "GPS  /  %s", s.obdNote);
    else snprintf(stb, sizeof stb, "%s", s.obdNote);
    st = stb;
  }
  uint32_t sc = live && !s.obdNote ? GREEN : AMBER;
  float tw = gfx.textWidth(font_tiny, st);
  gfx.circle(CX - tw / 2 - 9, SP_STATUS_Y - 5, 3.5f, Paint(sc));
  gfx.text(font_tiny, st, CX + 5, SP_STATUS_Y, live && !s.obdNote ? DIM : sc, 0);

  // ---- speed ----
  if (s.speedKmh >= 0) {
    snprintf(b, sizeof b, "%d", (int)lroundf(s.speedKmh));
    gfx.text(font_giant, b, CX, SP_NUM_Y, TXT, 0);
  } else {
    gfx.text(font_giant, "--", CX, SP_NUM_Y, FAINT, 0);
  }

  // ---- max / trip ----
  snprintf(b, sizeof b, "%d", (int)lroundf(s.maxKmh));
  gfx.text(font_med, b, SP_CELL_L, SP_VALUE_Y, s.maxKmh > 0 ? TXT : DIM, 0);
  if (s.tripKm < 1000.f) snprintf(b, sizeof b, "%.1f", s.tripKm);
  else snprintf(b, sizeof b, "%d", (int)s.tripKm);
  gfx.text(font_med, b, SP_CELL_R, SP_VALUE_Y, s.tripKm > 0.05f ? TXT : DIM, 0);
}

// Display diagnostics: single-pixel lines, per-channel ramps, AA shapes.
static void renderTestPattern() {
  gfx.rect(0, 0, 480, 480, Paint(0x000000));
  // 1-px horizontal lines: left block on EVEN rows only, right block on ODD rows only
  for (int y = 70; y < 110; y += 2) gfx.rect(110, y, 125, 1, Paint(0xFFFFFF));
  for (int y = 71; y < 111; y += 2) gfx.rect(245, y, 125, 1, Paint(0xFFFFFF));
  // 32-step ramps: R, G, B, grey
  const uint32_t base[4] = {0xFF0000, 0x00FF00, 0x0000FF, 0xFFFFFF};
  for (int r = 0; r < 4; r++)
    for (int i = 0; i < 32; i++) {
      float k = i / 31.f;
      gfx.rect(80 + i * 10, 125 + r * 34, 10, 28, Paint(scaleColor(base[r], k)));
    }
  // antialiased shapes
  gfx.circle(170, 340, 40, Paint(0xFFFFFF));
  gfx.ring(290, 340, 30, 33, Paint(0xFFFFFF));
  gfx.line(340, 300, 420, 380, 2.f, Paint(0xFFFFFF));
  gfx.text(font_med, "TEST", CX, 420, 0xFFFFFF, 0);
  gfx.text(font_tiny, "EVEN rows | ODD rows", CX, 62, 0xFFFFFF, 0);
}

// ------------------------------------------------------------------ overlays
static void pageDots(const UiState& s) {
  for (int i = 0; i < SCREEN_COUNT; i++) {
    float y = CY + (i - (SCREEN_COUNT - 1) * 0.5f) * 14;
    if (i == s.screen) gfx.circle(463, y, 4.2f, Paint(ACCENT));
    else gfx.circle(463, y, 3, Paint(0x3A4756));
  }
}

static void overlays(const UiState& s) {
  if (inDanger(s)) {   // soft glow is baked into the background; pulse a thin rim on top
    float a = 0.5f + 0.5f * sinf(s.t * 9.f);
    gfx.ring(CX, CY, 232, 241, Paint(RED, 0.25f + 0.6f * a));
  }
  pageDots(s);

  if (s.holdProgress > 0.02f || s.leveling) {
    gfx.circle(CX, CY, 150, Paint(0x03060A, 0.86f));
    gfx.ring(CX, CY, 150, 151.5f, Paint(ACCENT, 0.35f));
    if (s.leveling) {
      float a0 = fmodf(s.t * 360.f, 360.f);
      gfx.arc(CX, CY, 130, 136, a0, a0 + 90, Paint(ACCENT));
      gfx.text(font_med, "LEVELING", CX, 238, TXT, 0);
      gfx.text(font_tiny, "keep the car still", CX, 264, DIM, 0);
    } else {
      gfx.arc(CX, CY, 130, 136, 0, 360.f * clampf(s.holdProgress, 0, 1), Paint(ACCENT));
      bool trip = s.screen == SCREEN_SPEED;
      gfx.text(font_med, trip ? "RESET TRIP" : "SET LEVEL", CX, 236, TXT, 0);
      gfx.text(font_tiny, trip ? "clears max speed & distance" : "park on flat ground & keep holding", CX, 262, DIM, 0);
    }
  }

  if (s.toast && s.toastAge < 2.6f) {
    float a = clampf(s.toastAge / 0.15f, 0, 1) * clampf((2.6f - s.toastAge) / 0.35f, 0, 1);
    float w = gfx.textWidth(font_med, s.toast) + 56;
    gfx.roundRect(CX - w / 2, 300, w, 54, 27, Paint(0x0B1118, 0.92f * a));
    gfx.text(font_med, s.toast, CX, 336, TXT, 0, a);
  }

  if (s.debug) {
    char b[32]; snprintf(b, sizeof b, "%.0f fps  adapt %.2f", s.fps, s.adapt);
    gfx.text(font_tiny, b, CX, 470, DIM, 0);
  }
}

static void renderSplash(const UiState& s) {
  Xf xf = pivotXf(CX, 200, 0, 0.9f, 0, -50);
  drawRearTruck(xf, 0.9f, 0, ACCENT);
  gfx.text(font_small, s.splash1, CX, 300, ACCENT, 0);
  if (s.splash2) gfx.text(font_tiny, s.splash2, CX, 326, DIM, 0);
  gfx.arc(CX, CY, 222, 228, 0, 360, Paint(0x1A2430));
  if (s.splashProgress > 0) gfx.arc(CX, CY, 222, 228, 0, 360 * clampf(s.splashProgress, 0, 1), Paint(ACCENT));
}

const uint16_t* background(const UiState& s) {
  if (s.splash1) return bgPlain;
  const bool d = inDanger(s);
  switch (s.screen) {
    case SCREEN_DUAL:   return d ? bgDualD : bgDual;
    case SCREEN_PEAKS:  return d ? bgPlainD : bgPlain;
    case SCREEN_SPEED:  return d ? bgSpeedD : bgSpeed;
    default:            return nullptr;   // drawn procedurally
  }
}

void prepare(const UiState& s) {
  gfx.resetClip();
  gfx.beginRecord();
  if (s.testPattern) {
    renderTestPattern();
  } else if (s.splash1) {
    renderSplash(s);
  } else {
    switch (s.screen) {
      case SCREEN_DUAL:    renderDual(s); break;
      case SCREEN_PEAKS:   renderPeaks(s); break;
      case SCREEN_SPEED:   renderSpeed(s); break;
      default: break;
    }
    overlays(s);
  }
  gfx.endRecord();
}

uint32_t stripHash(int y0, int rows, const UiState& s) {
  uint32_t h = gfx.hashBand(y0, y0 + rows);
  uintptr_t bg = (uintptr_t)background(s);
  h = (h ^ (uint32_t)bg) * 16777619u;
  h = (h ^ (uint32_t)s.screen) * 16777619u;
  return h;
}

void renderStrip(uint16_t* buf, int y0, int rows, const UiState& s) {
  gfx.setTarget(buf, y0, rows);
  gfx.resetClip();
  gfx.playback(buf, y0, rows);
}

void renderFull(uint16_t* fb, const UiState& s) {
  const uint16_t* bg = background(s);
  if (bg) memcpy(fb, bg, FB_BYTES);
  prepare(s);
  renderStrip(fb, 0, 480, s);
}

}  // namespace ui
