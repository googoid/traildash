#include "gfx.h"
#include <string.h>
#include <stdio.h>
#include <algorithm>

#ifdef ESP_PLATFORM
#include <esp_attr.h>
#define HOT IRAM_ATTR
#else
#define HOT
#endif

Canvas gfx;

// ------------------------------------------------------------------ colour
uint32_t lerpColor(uint32_t a, uint32_t b, float t) {
  if (t <= 0) return a;
  if (t >= 1) return b;
  int ar = a >> 16, ag = (a >> 8) & 255, ab = a & 255;
  int br = b >> 16, bg = (b >> 8) & 255, bb = b & 255;
  int r = ar + (int)((br - ar) * t), g = ag + (int)((bg - ag) * t), bl = ab + (int)((bb - ab) * t);
  return (r << 16) | (g << 8) | bl;
}

uint32_t scaleColor(uint32_t c, float k) {
  int r = (int)((c >> 16) * k), g = (int)(((c >> 8) & 255) * k), b = (int)((c & 255) * k);
  r = r > 255 ? 255 : r; g = g > 255 ? 255 : g; b = b > 255 ? 255 : b;
  return (r << 16) | (g << 8) | b;
}

static const uint8_t BAYER4[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};

HOT uint16_t rgb565Dither(uint32_t c, int x, int y) {
  int d = BAYER4[y & 3][x & 3];
  int r = (c >> 16) + (d >> 1), g = ((c >> 8) & 255) + (d >> 2), b = (c & 255) + (d >> 1);
  r = r > 255 ? 255 : r; g = g > 255 ? 255 : g; b = b > 255 ? 255 : b;
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

static inline uint16_t blend565(uint16_t dst, uint16_t src, uint32_t a5) {  // a5: 0..32
  uint32_t bg = (dst | ((uint32_t)dst << 16)) & 0x07E0F81F;
  uint32_t fg = (src | ((uint32_t)src << 16)) & 0x07E0F81F;
  uint32_t r = ((((fg - bg) * a5) >> 5) + bg) & 0x07E0F81F;
  return (uint16_t)((r >> 16) | r);
}

// ------------------------------------------------------------------ fast trig
static float SIN_T[1025];
static bool sinInit = false;
static void initSin() {
  for (int i = 0; i <= 1024; i++) SIN_T[i] = sinf(2.f * (float)M_PI * i / 1024.f);
  sinInit = true;
}
float fsin(float a) {
  if (!sinInit) initSin();
  float t = a * (1024.f / (2.f * (float)M_PI));
  t -= 1024.f * floorf(t / 1024.f);
  int i = (int)t;
  if (i >= 1024) i = 0;
  float f = t - i;
  return SIN_T[i] + (SIN_T[i + 1] - SIN_T[i]) * f;
}
float fcos(float a) { return fsin(a + (float)M_PI * 0.5f); }

// ------------------------------------------------------------------ path
void Path::moveTo(float x, float y) {
  if (nc_ >= MAXCONT || n_ >= MAXPTS) return;
  cstart_[nc_++] = n_;
  cstart_[nc_] = n_;
  open_ = true;
  lineTo(x, y);
}

void Path::lineTo(float x, float y) {
  if (!open_) { moveTo(x, y); return; }
  if (n_ >= MAXPTS) return;
  x_[n_] = x; y_[n_] = y; n_++;
  cstart_[nc_] = n_;
}

void Path::poly(const float* xy, int n, const Xf* xf) {
  for (int i = 0; i < n; i++) {
    float x = xy[2 * i], y = xy[2 * i + 1];
    if (xf) xf->apply(x, y, x, y);
    if (i == 0) moveTo(x, y); else lineTo(x, y);
  }
  close();
}

void Path::rect(float x, float y, float w, float h, const Xf* xf) {
  float p[8] = {x, y, x + w, y, x + w, y + h, x, y + h};
  poly(p, 4, xf);
}

void Path::roundRect(float x, float y, float w, float h, float r, const Xf* xf) {
  r = std::min(r, std::min(w, h) * 0.5f);
  const int seg = 6;
  float cxs[4] = {x + w - r, x + w - r, x + r, x + r};
  float cys[4] = {y + r, y + h - r, y + h - r, y + r};
  float start[4] = {-90, 0, 90, 180};  // math angles (deg), y down
  bool first = true;
  for (int k = 0; k < 4; k++) {
    for (int i = 0; i <= seg; i++) {
      float a = (start[k] + 90.f * i / seg) * (float)M_PI / 180.f;
      float px = cxs[k] + r * fcos(a), py = cys[k] + r * fsin(a);
      if (xf) xf->apply(px, py, px, py);
      if (first) { moveTo(px, py); first = false; } else lineTo(px, py);
    }
  }
  close();
}

static inline int circleSegs(float r) {
  int n = (int)(r * 0.5f) + 12;
  return n < 12 ? 12 : (n > 144 ? 144 : n);
}

void Path::circle(float cx, float cy, float r, const Xf* xf) {
  if (xf) { float tx, ty; xf->apply(cx, cy, tx, ty); cx = tx; cy = ty; r *= sqrtf(xf->a * xf->a + xf->b * xf->b); }
  int n = circleSegs(r);
  for (int i = 0; i < n; i++) {
    float a = 2.f * (float)M_PI * i / n;
    float px = cx + r * fcos(a), py = cy + r * fsin(a);
    if (i == 0) moveTo(px, py); else lineTo(px, py);
  }
  close();
}

void Path::arc(float cx, float cy, float r0, float r1, float a0, float a1) {
  if (a1 < a0) std::swap(a0, a1);
  float span = (a1 - a0) * (float)M_PI / 180.f;
  int n = (int)(span * r1 / 5.f) + 2;
  if (n > 120) n = 120;
  float b0 = a0 * (float)M_PI / 180.f;
  for (int i = 0; i <= n; i++) {
    float a = b0 + span * i / n;
    float px = cx + r1 * fsin(a), py = cy - r1 * fcos(a);
    if (i == 0) moveTo(px, py); else lineTo(px, py);
  }
  for (int i = n; i >= 0; i--) {
    float a = b0 + span * i / n;
    lineTo(cx + r0 * fsin(a), cy - r0 * fcos(a));
  }
  close();
}

void Path::line(float x0, float y0, float x1, float y1, float w) {
  float dx = x1 - x0, dy = y1 - y0, len = sqrtf(dx * dx + dy * dy);
  float r = w * 0.5f;
  float base = (len < 1e-4f) ? 0.f : atan2f(dy, dx);
  const int capSeg = (r > 3.f) ? 8 : (r > 1.2f ? 4 : 2);
  bool first = true;
  // cap around (x1,y1): from base-90 to base+90
  for (int i = 0; i <= capSeg; i++) {
    float a = base - (float)M_PI / 2 + (float)M_PI * i / capSeg;
    float px = x1 + r * fcos(a), py = y1 + r * fsin(a);
    if (first) { moveTo(px, py); first = false; } else lineTo(px, py);
  }
  for (int i = 0; i <= capSeg; i++) {
    float a = base + (float)M_PI / 2 + (float)M_PI * i / capSeg;
    lineTo(x0 + r * fcos(a), y0 + r * fsin(a));
  }
  close();
}

// ------------------------------------------------------------------ rasterizer
namespace {
constexpr int MAXE = Path::MAXPTS;
float eX[MAXE], eY0[MAXE], eY1[MAXE], eDx[MAXE];
int16_t eIdx[MAXE], eAct[MAXE];
}

void Canvas::clear(uint32_t c) {
  uint16_t p = rgb565(c);
  uint32_t v = p | ((uint32_t)p << 16);
  uint32_t* d = (uint32_t*)fb_;
  for (int i = 0; i < W * (ty1_ - ty0_) / 2; i++) d[i] = v;
}

HOT void Canvas::blendSpan(int y, int xa, int xb, const Paint& p) {
  uint16_t* row = this->row(y);
  const int32_t alpha256 = (int32_t)(p.alpha * 256.f + 0.5f);
  if (p.type == Paint::Solid) {
    const uint16_t src = rgb565(p.c0);
    for (int x = xa; x <= xb; x++) {
      int32_t c = cov_[x];
      cov_[x] = 0;
      if (c <= 0) continue;
      if (c > 256) c = 256;
      uint32_t a5 = (uint32_t)(c * alpha256 + 1024) >> 11;
      if (a5 >= 32) row[x] = src;
      else if (a5) row[x] = blend565(row[x], src, a5);
    }
    return;
  }
  const int r0 = p.c0 >> 16, g0 = (p.c0 >> 8) & 255, b0 = p.c0 & 255;
  const int dr = (int)(p.c1 >> 16) - r0, dg = (int)((p.c1 >> 8) & 255) - g0, db = (int)(p.c1 & 255) - b0;
  int32_t T = 0, DT = 0;
  if (p.type == Paint::Linear) {
    float gdx = p.x1 - p.x0, gdy = p.y1 - p.y0;
    float l2 = gdx * gdx + gdy * gdy;
    float ginv = l2 > 0 ? 1.f / l2 : 0;
    T = (int32_t)(((xa + 0.5f - p.x0) * gdx + (y + 0.5f - p.y0) * gdy) * ginv * 65536.f);
    DT = (int32_t)(gdx * ginv * 65536.f);
  }
  const float rinv = (p.type == Paint::Radial && p.x1 > 0) ? 65536.f / p.x1 : 0;
  const float dy = y + 0.5f - p.y0;
  for (int x = xa; x <= xb; x++, T += DT) {
    int32_t c = cov_[x];
    cov_[x] = 0;
    if (c <= 0) continue;
    if (c > 256) c = 256;
    int32_t t;
    if (p.type == Paint::Linear) t = T;
    else { float dx = x + 0.5f - p.x0; t = (int32_t)(sqrtf(dx * dx + dy * dy) * rinv); }
    t = t < 0 ? 0 : (t > 65536 ? 65536 : t);
    t >>= 8;  // 0..256
    uint32_t col = ((uint32_t)(r0 + ((dr * t) >> 8)) << 16) | ((uint32_t)(g0 + ((dg * t) >> 8)) << 8) | (uint32_t)(b0 + ((db * t) >> 8));
    uint16_t src = rgb565Dither(col, x, y);
    uint32_t a5 = (uint32_t)(c * alpha256 + 1024) >> 11;
    if (a5 >= 32) row[x] = src;
    else if (a5) row[x] = blend565(row[x], src, a5);
  }
}

// ------------------------------------------------------------------ display list
namespace {
constexpr int REC_PTS = 8000, REC_CONT = 1000, REC_ITEMS = 360;
float* rx = nullptr;
float* ry = nullptr;
int16_t* rcs = nullptr;
struct Item {
  uint8_t type;            // 0 fill, 1 text
  int16_t clip[4];
  float y0, y1;            // vertical extent
  Paint paint;             // fill
  int pt, nc, cs;          // fill: first point, contour count, first contour-start index
  const Font* font;        // text
  char str[28];
  float tx, ty, alpha;
  uint32_t color;
  int8_t align;
};
Item* items = nullptr;
int nItems = 0, nPts = 0, nCs = 0;
bool overflowWarned = false;
}

#ifdef ESP_PLATFORM
#include <esp_heap_caps.h>
#include <esp_memory_utils.h>
static void* recAlloc(size_t n) {
  void* p = heap_caps_malloc(n, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  return p ? p : heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}
#else
#include <stdlib.h>
static void* recAlloc(size_t n) { return malloc(n); }
#endif

void Canvas::beginRecord() {
  if (!items) {
    rx = (float*)recAlloc(REC_PTS * sizeof(float));
    ry = (float*)recAlloc(REC_PTS * sizeof(float));
    rcs = (int16_t*)recAlloc(REC_CONT * sizeof(int16_t));
    items = (Item*)recAlloc(REC_ITEMS * sizeof(Item));
#ifdef ESP_PLATFORM
    printf("[gfx] display list in %s RAM (%u B items)\n", esp_ptr_external_ram(items) ? "PSRAM" : "internal",
           (unsigned)(REC_ITEMS * sizeof(Item)));
#endif
  }
  recording_ = true; nItems = nPts = nCs = 0;
}
void Canvas::endRecord() { recording_ = false; }

void Canvas::playback(uint16_t* buf, int y0, int rows) {
  setTarget(buf, y0, rows);
  for (int i = 0; i < nItems; i++) {
    const Item& it = items[i];
    if (it.y1 < y0 || it.y0 >= y0 + rows) continue;
    setClip(it.clip[0], it.clip[1], it.clip[2], it.clip[3]);
    if (it.type == 0) rasterize(rx + it.pt, ry + it.pt, rcs + it.cs, it.nc, it.paint);
    else drawText(*it.font, it.str, it.tx, it.ty, it.color, it.align, it.alpha);
  }
  resetClip();
}

static inline uint32_t fnv(uint32_t h, const void* d, size_t n) {
  const uint8_t* p = (const uint8_t*)d;
  for (size_t i = 0; i < n; i++) { h ^= p[i]; h *= 16777619u; }
  return h;
}
static inline uint32_t fnvF(uint32_t h, float f) { return fnv(h, &f, 4); }

uint32_t Canvas::hashBand(int y0, int y1) const {
  uint32_t h = 2166136261u;
  for (int i = 0; i < nItems; i++) {
    const Item& it = items[i];
    if (it.y1 < y0 || it.y0 >= y1) continue;
    h = fnv(h, it.clip, sizeof it.clip);
    h = fnv(h, &it.type, 1);
    if (it.type == 0) {
      const Paint& p = it.paint;
      h = fnv(h, &p.type, 1); h = fnv(h, &p.c0, 4); h = fnv(h, &p.c1, 4);
      h = fnvF(h, p.x0); h = fnvF(h, p.y0); h = fnvF(h, p.x1); h = fnvF(h, p.y1); h = fnvF(h, p.alpha);
      int n = rcs[it.cs + it.nc];
      h = fnv(h, rx + it.pt, n * sizeof(float));
      h = fnv(h, ry + it.pt, n * sizeof(float));
    } else {
      h = fnv(h, &it.font, sizeof(void*));
      h = fnv(h, it.str, strlen(it.str));
      h = fnvF(h, it.tx); h = fnvF(h, it.ty); h = fnvF(h, it.alpha);
      h = fnv(h, &it.color, 4); h = fnv(h, &it.align, 1);
    }
  }
  return h;
}

void Canvas::fill(const Path& path, const Paint& paint) {
  if (path.nc_ == 0) return;
  if (!recording_) { rasterize(path.x_, path.y_, path.cstart_, path.nc_, paint); return; }
  float a = 1e9f, b = -1e9f;
  for (int i = 0; i < path.n_; i++) { a = std::min(a, path.y_[i]); b = std::max(b, path.y_[i]); }
  if (b < cy0_ || a >= cy1_) return;
  if (nItems >= REC_ITEMS || nPts + path.n_ > REC_PTS || nCs + path.nc_ + 1 > REC_CONT) {
    if (!overflowWarned) { overflowWarned = true; printf("[gfx] display list full\n"); }
    return;
  }
  Item& it = items[nItems++];
  it.type = 0;
  it.clip[0] = cx0_; it.clip[1] = cy0_; it.clip[2] = cx1_; it.clip[3] = cy1_;
  it.y0 = a; it.y1 = b + 1;
  it.paint = paint;
  it.pt = nPts; it.nc = path.nc_; it.cs = nCs;
  memcpy(rx + nPts, path.x_, path.n_ * sizeof(float));
  memcpy(ry + nPts, path.y_, path.n_ * sizeof(float));
  for (int c = 0; c <= path.nc_; c++) rcs[nCs + c] = path.cstart_[c];
  nPts += path.n_;
  nCs += path.nc_ + 1;
}

HOT void Canvas::rasterize(const float* px, const float* py, const int16_t* cstart, int ncont, const Paint& paint) {
  if (!fb_) return;
  const int clipY0 = std::max(cy0_, ty0_), clipY1 = std::min(cy1_, ty1_);
  {  // quick reject against the current strip / clip
    float a = 1e9f, b = -1e9f;
    for (int i = cstart[0]; i < cstart[ncont]; i++) { a = std::min(a, py[i]); b = std::max(b, py[i]); }
    if (b < clipY0 || a >= clipY1) return;
  }
  // Build edge table (only edges that reach into the clip band)
  int ne = 0;
  float minY = 1e9f, maxY = -1e9f, minX = 1e9f, maxX = -1e9f;
  for (int c = 0; c < ncont; c++) {
    int s = cstart[c], e = cstart[c + 1];
    int cnt = e - s;
    if (cnt < 2) continue;
    for (int i = 0; i < cnt; i++) {
      float xa = px[s + i], ya = py[s + i];
      int j = (i + 1 == cnt) ? s : s + i + 1;
      float xb = px[j], yb = py[j];
      minX = std::min(minX, xa); maxX = std::max(maxX, xa);
      if (ya == yb) continue;
      if (ya > yb) { std::swap(xa, xb); std::swap(ya, yb); }
      if (yb < clipY0 || ya >= clipY1) { minX = std::min(minX, xb); maxX = std::max(maxX, xb); continue; }
      eX[ne] = xa; eY0[ne] = ya; eY1[ne] = yb; eDx[ne] = (xb - xa) / (yb - ya);
      minY = std::min(minY, ya); maxY = std::max(maxY, yb);
      ne++;
    }
  }
  if (ne == 0) return;
  int y0 = std::max(clipY0, (int)floorf(minY));
  int y1 = std::min(clipY1 - 1, (int)ceilf(maxY));
  int xlo = std::max(cx0_, (int)floorf(minX));
  int xhi = std::min(cx1_ - 1, (int)ceilf(maxX));
  if (y0 > y1 || xlo > xhi) return;

  for (int i = 0; i < ne; i++) eIdx[i] = i;
  std::sort(eIdx, eIdx + ne, [](int16_t a, int16_t b) { return eY0[a] < eY0[b]; });

  int next = 0, nact = 0;
  float xs[64];
  const float xclipL = (float)cx0_, xclipR = (float)cx1_;

  for (int y = y0; y <= y1; y++) {
    // add edges that start before the bottom of this row, drop ones that ended
    while (next < ne && eY0[eIdx[next]] < y + 1) eAct[nact++] = eIdx[next++];
    int k = 0;
    for (int i = 0; i < nact; i++) if (eY1[eAct[i]] > y) eAct[k++] = eAct[i];
    nact = k;
    if (!nact) continue;

    int16_t spA[64], spB[64];
    int nsp = 0;
    for (int s = 0; s < 4; s++) {
      float sy = y + (s + 0.5f) * 0.25f;
      int nx = 0;
      for (int i = 0; i < nact && nx < 64; i++) {
        int e = eAct[i];
        if (sy >= eY0[e] && sy < eY1[e]) xs[nx++] = eX[e] + (sy - eY0[e]) * eDx[e];
      }
      if (nx < 2) continue;
      for (int i = 1; i < nx; i++) {  // insertion sort
        float v = xs[i]; int j = i - 1;
        while (j >= 0 && xs[j] > v) { xs[j + 1] = xs[j]; j--; }
        xs[j + 1] = v;
      }
      for (int i = 0; i + 1 < nx; i += 2) {  // even-odd
        float xa = std::max(xs[i], xclipL), xb = std::min(xs[i + 1], xclipR);
        if (xb <= xa) continue;
        int ia = (int)xa, ib = (int)xb;
        if (ib >= cx1_) ib = cx1_ - 1;
        if (nsp < 64) { spA[nsp] = ia; spB[nsp] = ib; nsp++; }
        if (ia == ib) { cov_[ia] += (int16_t)((xb - xa) * 64.f); continue; }
        cov_[ia] += (int16_t)((ia + 1 - xa) * 64.f);
        for (int x = ia + 1; x < ib; x++) cov_[x] += 64;
        if (ib < cx1_) cov_[ib] += (int16_t)((xb - ib) * 64.f);
      }
    }
    // merge the (up to 4 per span) sub-scanline spans and blend only covered pixels
    for (int i = 1; i < nsp; i++) {
      int16_t a = spA[i], b = spB[i]; int j = i - 1;
      while (j >= 0 && spA[j] > a) { spA[j + 1] = spA[j]; spB[j + 1] = spB[j]; j--; }
      spA[j + 1] = a; spB[j + 1] = b;
    }
    int i = 0;
    while (i < nsp) {
      int a = spA[i], b = spB[i];
      int j = i + 1;
      while (j < nsp && spA[j] <= b + 1) { b = std::max<int>(b, spB[j]); j++; }
      blendSpan(y, a, b, paint);
      i = j;
    }
  }
}

// ------------------------------------------------------------------ text
static const Glyph* findGlyph(const Font& f, uint16_t cp) {
  int lo = 0, hi = f.count - 1;
  while (lo <= hi) {
    int m = (lo + hi) >> 1;
    if (f.codes[m] == cp) return &f.glyphs[m];
    if (f.codes[m] < cp) lo = m + 1; else hi = m - 1;
  }
  return nullptr;
}

static uint16_t nextCp(const char*& s) {
  uint8_t c = (uint8_t)*s++;
  if (c < 0x80) return c;
  if ((c & 0xE0) == 0xC0 && *s) return ((c & 0x1F) << 6) | ((uint8_t)*s++ & 0x3F);
  if ((c & 0xF0) == 0xE0 && s[0] && s[1]) { uint16_t v = ((c & 0x0F) << 12) | (((uint8_t)s[0] & 0x3F) << 6) | ((uint8_t)s[1] & 0x3F); s += 2; return v; }
  return '?';
}

int Canvas::textWidth(const Font& f, const char* s) const {
  int w = 0;
  while (*s) { const Glyph* g = findGlyph(f, nextCp(s)); if (g) w += g->advance; }
  return w;
}

int Canvas::text(const Font& f, const char* s, float fx, float fy, uint32_t color, int align, float alpha) {
  if (!recording_) return drawText(f, s, fx, fy, color, align, alpha);
  int w = textWidth(f, s);
  if (nItems >= REC_ITEMS) return w;
  Item& it = items[nItems++];
  it.type = 1;
  it.clip[0] = cx0_; it.clip[1] = cy0_; it.clip[2] = cx1_; it.clip[3] = cy1_;
  it.y0 = fy - f.ascent - 4; it.y1 = fy + f.descent + 4;
  it.font = &f;
  strncpy(it.str, s, sizeof(it.str) - 1); it.str[sizeof(it.str) - 1] = 0;
  if (strlen(s) >= sizeof(it.str)) printf("[gfx] text cut to %u chars: \"%s\"\n", (unsigned)sizeof(it.str) - 1, s);
  it.tx = fx; it.ty = fy; it.color = color; it.align = (int8_t)align; it.alpha = alpha;
  return w;
}

int Canvas::drawText(const Font& f, const char* s, float fx, float fy, uint32_t color, int align, float alpha) {
  int w = textWidth(f, s);
  int x = (int)lroundf(fx), y = (int)lroundf(fy);
  if (align == 0) x -= w / 2; else if (align > 0) x -= w;
  uint16_t src = rgb565(color);
  int a255 = (int)(alpha * 255.f);
  while (*s) {
    const Glyph* g = findGlyph(f, nextCp(s));
    if (!g) continue;
    const uint8_t* bm = f.bitmap + g->offset;
    int gx = x + g->xoff, gy = y + g->yoff;
    if (gy >= ty1_ || gy + g->h <= ty0_) { x += g->advance; continue; }
    for (int j = 0; j < g->h; j++) {
      int py = gy + j;
      if (py < cy0_ || py >= cy1_ || py < ty0_ || py >= ty1_) continue;
      uint16_t* row = this->row(py);
      for (int i = 0; i < g->w; i++) {
        int px = gx + i;
        if (px < cx0_ || px >= cx1_) continue;
        uint32_t a = bm[j * g->w + i];
        if (!a) continue;
        uint32_t a5 = (a * (uint32_t)a255 * 32u + 32512u) / 65025u;  // (a/255)*(alpha)*32
        if (a5 >= 32) row[px] = src; else if (a5) row[px] = blend565(row[px], src, a5);
      }
    }
    x += g->advance;
  }
  return w;
}

void Canvas::glowRing(float cx, float cy, float r, float width, uint32_t c, float strength) {
  const int steps = 4;
  for (int i = 0; i < steps; i++) {
    float t = (i + 0.5f) / steps;
    float a = strength * (1.f - t) * (1.f - t) * 0.5f;
    float d0 = width * i / steps, d1 = width * (i + 1) / steps;
    ring(cx, cy, r + d0, r + d1, Paint(c, a));
    if (r - d1 > 0) ring(cx, cy, r - d1, r - d0, Paint(c, a));
  }
}
