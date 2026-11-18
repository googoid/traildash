// Runtime settings: defaults from config.h, overridden by /incl/config.txt on the SD card.
// Pure C++ (no Arduino), so ui.cpp keeps building for the PC preview.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "config.h"

struct Settings {
  float rollWarnDeg = ROLL_WARN_DEG;
  float rollDangerDeg = ROLL_DANGER_DEG;
  float pitchWarnDeg = PITCH_WARN_DEG;
  float pitchDangerDeg = PITCH_DANGER_DEG;
  bool buzzer = BUZZER_ALARM;
  uint8_t backlight = BACKLIGHT_PERCENT;
  bool log = LOG_ENABLED;              // drive log CSV on the SD card
  float logIntervalS = LOG_INTERVAL_S;
  char obdName[24] = "";               // extra OBD adapter name to look for ("" = built-in list only)
  char remoteForward[3] = REMOTE_FORWARD;   // its axis that points forward: "+x" .. "-z"
};
extern Settings cfg;

namespace settings {
// Calls fn for every "key = value" line ('#' starts a comment; spaces trimmed).
void parseKv(const char* text, void (*fn)(const char* key, const char* val, void* ctx), void* ctx);
// Apply a config file's text to cfg (unknown keys are reported through `unknown`, may be null).
void apply(const char* text, void (*unknown)(const char* key) = nullptr);
// The full config file for the current cfg, with comments.
size_t format(char* out, size_t size);
}  // namespace settings
