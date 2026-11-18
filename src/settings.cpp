#include "settings.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static void applyOne(const char* k, const char* v, void* ctx) {
  auto unknown = (void (*)(const char*))ctx;
  float f = (float)atof(v);
  if (!strcmp(k, "roll_warn")) cfg.rollWarnDeg = clampf(f, 1, 60);
  else if (!strcmp(k, "roll_danger")) cfg.rollDangerDeg = clampf(f, 1, 60);
  else if (!strcmp(k, "pitch_warn")) cfg.pitchWarnDeg = clampf(f, 1, 60);
  else if (!strcmp(k, "pitch_danger")) cfg.pitchDangerDeg = clampf(f, 1, 60);
  else if (!strcmp(k, "buzzer")) cfg.buzzer = f != 0;
  else if (!strcmp(k, "backlight")) cfg.backlight = (uint8_t)clampf(f, 5, 100);
  else if (!strcmp(k, "log")) cfg.log = f != 0;
  else if (!strcmp(k, "log_interval")) cfg.logIntervalS = clampf(f, 0.2f, 60);
  else if (!strcmp(k, "obd_name")) {
    size_t i = 0;
    for (; v[i] && i < sizeof cfg.obdName - 1; i++) cfg.obdName[i] = (char)tolower((unsigned char)v[i]);
    cfg.obdName[i] = 0;
  } else if (unknown) unknown(k);
}

void apply(const char* text, void (*unknown)(const char*)) {
  parseKv(text, applyOne, (void*)unknown);
  // danger must lie beyond warn
  if (cfg.rollDangerDeg < cfg.rollWarnDeg + 1) cfg.rollDangerDeg = cfg.rollWarnDeg + 1;
  if (cfg.pitchDangerDeg < cfg.pitchWarnDeg + 1) cfg.pitchDangerDeg = cfg.pitchWarnDeg + 1;
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
      "obd_name = %s\n",
      cfg.rollWarnDeg, cfg.rollDangerDeg, cfg.pitchWarnDeg, cfg.pitchDangerDeg, cfg.buzzer ? 1 : 0,
      (unsigned)cfg.backlight, cfg.log ? 1 : 0, cfg.logIntervalS, cfg.obdName);
  return n < 0 ? 0 : (size_t)n;
}

}  // namespace settings
