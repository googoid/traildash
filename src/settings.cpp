#include "settings.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

Settings cfg;

namespace settings {

void parseKv(const char* text, void (*fn)(const char*, const char*, void*), void* ctx) {
  char line[128];
  const char* p = text;
  while (*p) {
    size_t n = strcspn(p, "\r\n");
    size_t m = n < sizeof line - 1 ? n : sizeof line - 1;
    memcpy(line, p, m);
    line[m] = 0;
    p += n;
    while (*p == '\r' || *p == '\n') p++;

    if (char* h = strchr(line, '#')) *h = 0;
    char* eq = strchr(line, '=');
    if (!eq) continue;
    *eq = 0;
    char *k = line, *v = eq + 1;
    while (isspace((unsigned char)*k)) k++;
    while (isspace((unsigned char)*v)) v++;
    for (char* e = k + strlen(k); e > k && isspace((unsigned char)e[-1]);) *--e = 0;
    for (char* e = v + strlen(v); e > v && isspace((unsigned char)e[-1]);) *--e = 0;
    if (*k) fn(k, v, ctx);
  }
}

static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

// One "key = value" line into ctx->s. Values are clamped to sane ranges here, so whatever
// arrives (hand-edited file, phone app) can't break the device.
struct ApplyCtx {
  Settings* s;
  void (*unknown)(const char* key);
  int known = 0;   // settings recognised
};

static void applyOne(const char* k, const char* v, void* p) {
  ApplyCtx& c = *(ApplyCtx*)p;
  Settings& s = *c.s;
  float f = (float)atof(v);
  if (!strcmp(k, "roll_warn")) s.rollWarnDeg = clampf(f, 1, 60);
  else if (!strcmp(k, "roll_danger")) s.rollDangerDeg = clampf(f, 1, 60);
  else if (!strcmp(k, "pitch_warn")) s.pitchWarnDeg = clampf(f, 1, 60);
  else if (!strcmp(k, "pitch_danger")) s.pitchDangerDeg = clampf(f, 1, 60);
  else if (!strcmp(k, "buzzer")) s.buzzer = f != 0;
  else if (!strcmp(k, "backlight")) s.backlight = (uint8_t)clampf(f, 5, 100);
  else if (!strcmp(k, "log")) s.log = f != 0;
  else if (!strcmp(k, "log_interval")) s.logIntervalS = clampf(f, 0.2f, 60);
  else if (!strcmp(k, "obd_name")) {
    size_t i = 0;
    for (; v[i] && i < sizeof s.obdName - 1; i++) s.obdName[i] = (char)tolower((unsigned char)v[i]);
    s.obdName[i] = 0;
  } else if (!strcmp(k, "remote_forward")) {
    char sg = v[0] == '-' ? '-' : '+';
    char ax = (char)tolower((unsigned char)(v[0] == '-' || v[0] == '+' ? v[1] : v[0]));
    if (ax == 'x' || ax == 'y' || ax == 'z') { s.remoteForward[0] = sg; s.remoteForward[1] = ax; s.remoteForward[2] = 0; }
  } else if (!strcmp(k, "redline")) s.redlineRpm = clampf(f, 2000, ENGINE_GAUGE_RPM);
  else if (!strcmp(k, "obd_probe")) s.obdProbe = f != 0;
  else if (!strcmp(k, "speed_correction")) s.speedCorrPct = clampf(roundf(f * 10) / 10, -20, 20);
  else {
    if (c.unknown) c.unknown(k);
    return;
  }
  c.known++;
}

static void fixup(Settings& s) {
  // danger must lie beyond warn
  if (s.rollDangerDeg < s.rollWarnDeg + 1) s.rollDangerDeg = s.rollWarnDeg + 1;
  if (s.pitchDangerDeg < s.pitchWarnDeg + 1) s.pitchDangerDeg = s.pitchWarnDeg + 1;
}

void apply(const char* text, void (*unknown)(const char*)) {
  ApplyCtx c{&cfg, unknown};
  parseKv(text, applyOne, &c);
  fixup(cfg);
}

static volatile uint32_t rev = 0;
uint32_t revision() { return rev; }

bool update(const char* text) {
  Settings s = cfg;   // a copy: the other tasks never see a half-applied change
  ApplyCtx c{&s, nullptr};
  parseKv(text, applyOne, &c);
  if (!c.known) return false;
  fixup(s);
  if (memcmp(&s, &cfg, sizeof s) != 0) {   // padding is equal too: s started as a copy
    cfg = s;
    rev = rev + 1;
  }
  return true;
}

size_t formatCompact(char* out, size_t size) {
  int n = snprintf(out, size,
      "roll_warn=%g\nroll_danger=%g\npitch_warn=%g\npitch_danger=%g\nbuzzer=%d\nbacklight=%u\n"
      "log=%d\nlog_interval=%g\nobd_name=%s\nremote_forward=%s\nredline=%g\nobd_probe=%d\n"
      "speed_correction=%g\n",
      cfg.rollWarnDeg, cfg.rollDangerDeg, cfg.pitchWarnDeg, cfg.pitchDangerDeg, cfg.buzzer ? 1 : 0,
      (unsigned)cfg.backlight, cfg.log ? 1 : 0, cfg.logIntervalS, cfg.obdName, cfg.remoteForward,
      cfg.redlineRpm, cfg.obdProbe ? 1 : 0, cfg.speedCorrPct);
  return n < 0 ? 0 : (size_t)n < size ? (size_t)n : size - 1;
}

size_t format(char* out, size_t size) {
  int n = snprintf(out, size,
      "# Inclinometer settings. Edit on a computer, then restart the device.\n"
      "# Delete a line to get its default back; delete the file to rewrite it.\n"
      "\n"
      "# Warning (amber) and danger (red + alarm) angles, degrees\n"
      "roll_warn = %g\n"
      "roll_danger = %g\n"
      "pitch_warn = %g\n"
      "pitch_danger = %g\n"
      "\n"
      "# 1 = chirp while any angle is in the red zone\n"
      "buzzer = %d\n"
      "\n"
      "# Screen brightness, 5..100 %%\n"
      "backlight = %u\n"
      "\n"
      "# Drive log: one CSV file per power-up in /incl/logs (1 = on)\n"
      "log = %d\n"
      "# Seconds between log rows\n"
      "log_interval = %g\n"
      "\n"
      "# OBD adapter: part of its Bluetooth name, if it isn't found automatically\n"
      "obd_name = %s\n"
      "\n"
      "# Axis of the remote IMU board that points forward in the car: +x -x +y -y +z -z\n"
      "# (set level again after changing it)\n"
      "remote_forward = %s\n"
      "\n"
      "# Engine screen: rev gauge red zone from here, rpm\n"
      "redline = %g\n"
      "\n"
      "# 1 = also read the car's maker-specific data blocks and log them to probe.txt\n"
      "# (slows the normal OBD values a little)\n"
      "obd_probe = %d\n"
      "\n"
      "# Car (OBD) speed correction, %%: the car's speed is multiplied by 1 + this / 100.\n"
      "# +8 = the car shows 8 %% too little. The speed check screen suggests a value.\n"
      "speed_correction = %g\n",
      cfg.rollWarnDeg, cfg.rollDangerDeg, cfg.pitchWarnDeg, cfg.pitchDangerDeg, cfg.buzzer ? 1 : 0,
      (unsigned)cfg.backlight, cfg.log ? 1 : 0, cfg.logIntervalS, cfg.obdName, cfg.remoteForward,
      cfg.redlineRpm, cfg.obdProbe ? 1 : 0, cfg.speedCorrPct);
  return n < 0 ? 0 : (size_t)n;
}

}  // namespace settings
