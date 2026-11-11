// UI rendering — pure C++ (no Arduino), so it can also be built on a PC
// to produce preview screenshots (see tools/preview).
#pragma once
#include <stdint.h>
#include <stddef.h>

enum Screen : uint8_t { SCREEN_DUAL = 0, SCREEN_SPEED, SCREEN_BUBBLE, SCREEN_PEAKS, SCREEN_COUNT };
enum NavState : uint8_t { NAV_NO_PHONE = 0, NAV_NO_FIX, NAV_LIVE };

struct UiState {
  float pitch = 0, roll = 0;            // deg, smoothed (+pitch nose up, +roll right side down)
  float peakRollL = 0, peakRollR = 0;   // positive magnitudes
  float peakPitchUp = 0, peakPitchDn = 0;
  Screen screen = SCREEN_DUAL;
  float holdProgress = 0;               // 0..1 long-press progress (0 = not pressing)
  bool  leveling = false;               // level calibration in progress
  bool  levelSet = true;                // a level reference is stored
  const char* toast = nullptr;          // transient message
  float toastAge = 0;                   // seconds since toast appeared
  float t = 0;                          // seconds since boot (animations)
  float trust = 1;                      // accelerometer trust 0..1
  float fps = 0;
  bool  debug = false;
  bool  testPattern = false;            // display diagnostics (serial 'p')
  // GPS speed screen
  uint8_t nav = NAV_NO_PHONE;           // NavState
  float speedKmh = -1;                  // <0 = no fresh data
  float courseDeg = -1;                 // 0..360, <0 = unknown
  float maxKmh = 0, tripKm = 0;
  // boot splash (when splash1 != nullptr)
  const char* splash1 = nullptr;
  const char* splash2 = nullptr;
  float splashProgress = 0;
};

namespace ui {
// alloc: returns a buffer for 480*480 RGB565 pixels (e.g. PSRAM).
// flush: writes CPU cache back to memory for a buffer (so DMA can read it); may be null.
bool begin(void* (*alloc)(size_t), void (*flush)(void*, size_t));

// Static background for the current screen (full frame, or nullptr if the screen
// draws every pixel itself). The strip buffer must be pre-filled from it.
const uint16_t* background(const UiState& s);
// Build the frame's display list (call once per frame, before renderStrip).
void prepare(const UiState& s);
// Identity of a strip's content (after prepare); equal hash => identical pixels.
uint32_t stripHash(int y0, int rows, const UiState& s);
// Draw rows [y0, y0+rows) of the frame into buf (480*rows pixels).
void renderStrip(uint16_t* buf, int y0, int rows, const UiState& s);
// Convenience: background + full frame (used by the PC preview).
void renderFull(uint16_t* fb, const UiState& s);
bool inDanger(const UiState& s);
}
