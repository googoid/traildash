// Tiny antialiased 2D renderer for an RGB565 framebuffer.
//
// Everything (circles, rings, arcs, thick lines, rotated shapes) is turned
// into polygons and filled by one scanline rasterizer with 4x vertical
// supersampling and exact horizontal coverage — so all edges are smooth.
#pragma once
#include <stdint.h>
#include <math.h>
#include "fonts.h"

// ---------- colours ----------
// Colours are 0xRRGGBB everywhere in the API.
static inline uint16_t rgb565(uint32_t c) {
  return ((c >> 8) & 0xF800) | ((c >> 5) & 0x07E0) | ((c >> 3) & 0x001F);
}
uint16_t rgb565Dither(uint32_t c, int x, int y);  // ordered dithering for smooth gradients
uint32_t lerpColor(uint32_t a, uint32_t b, float t);
uint32_t scaleColor(uint32_t c, float k);

float fsin(float rad);   // table-based, ~1e-5 accuracy
float fcos(float rad);

// ---------- 2D affine transform ----------
struct Xf {
  float a = 1, b = 0, c = 0, d = 1, tx = 0, ty = 0;   // x' = a*x + c*y + tx ; y' = b*x + d*y + ty
  // rotation by deg (clockwise on screen, since y points down), uniform scale, then translate
  static Xf rot(float cx, float cy, float deg, float s = 1.f) {
    float r = deg * (float)M_PI / 180.f, cs = cosf(r) * s, sn = sinf(r) * s;
    Xf x; x.a = cs; x.b = sn; x.c = -sn; x.d = cs; x.tx = cx; x.ty = cy; return x;
  }
  inline void apply(float x, float y, float& ox, float& oy) const {
    ox = a * x + c * y + tx; oy = b * x + d * y + ty;
  }
};

// ---------- paint ----------
struct Paint {
  enum Type : uint8_t { Solid, Linear, Radial } type = Solid;
  uint32_t c0 = 0xFFFFFF, c1 = 0xFFFFFF;
  float x0 = 0, y0 = 0, x1 = 0, y1 = 0;  // Linear: from (x0,y0)=c0 to (x1,y1)=c1. Radial: centre (x0,y0), radius x1
  float alpha = 1.f;
  Paint() {}
  Paint(uint32_t c, float a = 1.f) : c0(c), c1(c), alpha(a) {}
  static Paint linear(float x0, float y0, uint32_t c0, float x1, float y1, uint32_t c1, float a = 1.f) {
    Paint p; p.type = Linear; p.x0 = x0; p.y0 = y0; p.x1 = x1; p.y1 = y1; p.c0 = c0; p.c1 = c1; p.alpha = a; return p;
  }
  static Paint radial(float cx, float cy, float r, uint32_t inner, uint32_t outer, float a = 1.f) {
    Paint p; p.type = Radial; p.x0 = cx; p.y0 = cy; p.x1 = r; p.c0 = inner; p.c1 = outer; p.alpha = a; return p;
  }
};

// ---------- path ----------
class Path {
 public:
  static constexpr int MAXPTS = 1400;
  static constexpr int MAXCONT = 48;
  void clear() { n_ = 0; nc_ = 0; open_ = false; }
  void moveTo(float x, float y);
  void lineTo(float x, float y);
  void close() { open_ = false; }

  // Each of these adds closed contour(s). Use the even-odd rule to cut holes.
  void rect(float x, float y, float w, float h, const Xf* xf = nullptr);
  void roundRect(float x, float y, float w, float h, float r, const Xf* xf = nullptr);
  void circle(float cx, float cy, float r, const Xf* xf = nullptr);
  void poly(const float* xy, int n, const Xf* xf = nullptr);
  // annular sector; angles in degrees, 0 = 12 o'clock, clockwise positive
  void arc(float cx, float cy, float r0, float r1, float a0, float a1);
  // thick line with round caps
  void line(float x0, float y0, float x1, float y1, float w);

  int n_ = 0, nc_ = 0;
  float x_[MAXPTS], y_[MAXPTS];
  int16_t cstart_[MAXCONT + 1];
 private:
  bool open_ = false;
};

// ---------- canvas ----------
class Canvas {
 public:
  static constexpr int W = 480, H = 480;
  // Full-frame target, or a horizontal strip [y0, y0+rows) of the frame.
  // Drawing always uses absolute screen coordinates.
  void setTarget(uint16_t* fb) { fb_ = fb; ty0_ = 0; ty1_ = H; }
  void setTarget(uint16_t* buf, int y0, int rows) { fb_ = buf; ty0_ = y0; ty1_ = y0 + rows; }
  int  targetY0() const { return ty0_; }
  int  targetY1() const { return ty1_; }
  inline uint16_t* row(int y) const { return fb_ + (y - ty0_) * W; }
  uint16_t* target() const { return fb_; }
  void setClip(int x0, int y0, int x1, int y1) { cx0_ = x0; cy0_ = y0; cx1_ = x1; cy1_ = y1; }
  void resetClip() { cx0_ = 0; cy0_ = 0; cx1_ = W; cy1_ = H; }

  void fill(const Path& p, const Paint& paint);

  // Display list: between beginRecord/endRecord, fill() and text() are recorded
  // (geometry computed once per frame); playback() rasterizes them into one strip.
  void beginRecord();
  void endRecord();
  void playback(uint16_t* buf, int y0, int rows);
  uint32_t hashBand(int y0, int y1) const;   // hash of recorded ops touching rows [y0, y1)
  void clear(uint32_t c);

  // convenience
  void circle(float cx, float cy, float r, const Paint& p) { path_.clear(); path_.circle(cx, cy, r); fill(path_, p); }
  void ring(float cx, float cy, float r0, float r1, const Paint& p) {
    path_.clear(); path_.circle(cx, cy, r1); path_.circle(cx, cy, r0); fill(path_, p);
  }
  void arc(float cx, float cy, float r0, float r1, float a0, float a1, const Paint& p) {
    path_.clear(); path_.arc(cx, cy, r0, r1, a0, a1); fill(path_, p);
  }
  void line(float x0, float y0, float x1, float y1, float w, const Paint& p) {
    path_.clear(); path_.line(x0, y0, x1, y1, w); fill(path_, p);
  }
  void rect(float x, float y, float w, float h, const Paint& p, const Xf* xf = nullptr) {
    path_.clear(); path_.rect(x, y, w, h, xf); fill(path_, p);
  }
  void roundRect(float x, float y, float w, float h, float r, const Paint& p, const Xf* xf = nullptr) {
    path_.clear(); path_.roundRect(x, y, w, h, r, xf); fill(path_, p);
  }
  void poly(const float* xy, int n, const Paint& p, const Xf* xf = nullptr) {
    path_.clear(); path_.poly(xy, n, xf); fill(path_, p);
  }

  // text: y is the baseline. align: -1 left, 0 centre, 1 right
  int  textWidth(const Font& f, const char* s) const;
  int  text(const Font& f, const char* s, float x, float y, uint32_t color, int align = -1, float alpha = 1.f);

  // soft glow: a stack of translucent rings around a circle
  void glowRing(float cx, float cy, float r, float width, uint32_t c, float strength);

  Path path_;   // scratch path for convenience calls

 private:
  uint16_t* fb_ = nullptr;
  int ty0_ = 0, ty1_ = H;
  int cx0_ = 0, cy0_ = 0, cx1_ = W, cy1_ = H;
  int16_t cov_[W + 2];   // coverage, 256 = full pixel
  bool recording_ = false;
  void rasterize(const float* px, const float* py, const int16_t* cstart, int ncont, const Paint& paint);
  int drawText(const Font& f, const char* s, float x, float y, uint32_t color, int align, float alpha);
  void blendSpan(int y, int xa, int xb, const Paint& p);
};

extern Canvas gfx;
