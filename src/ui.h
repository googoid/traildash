// UI rendering — pure C++ (no Arduino), so it can also be built on a PC
// to produce preview screenshots (see tools/preview).
#pragma once
#include <stdint.h>
#include <stddef.h>

enum Screen : uint8_t { SCREEN_SPEED = 0, SCREEN_ENGINE, SCREEN_DUAL, SCREEN_PEAKS, SCREEN_COMPARE, SCREEN_COUNT };
enum NavState : uint8_t { NAV_NO_PHONE = 0, NAV_NO_FIX, NAV_LIVE, NAV_OBD };   // NAV_OBD: speed from the car

struct UiState {
  float pitch = 0, roll = 0;            // deg, smoothed (+pitch nose up, +roll right side down)
  float peakRollL = 0, peakRollR = 0;   // positive magnitudes
  float peakPitchUp = 0, peakPitchDn = 0;
  Screen screen = SCREEN_SPEED;
  float holdProgress = 0;               // 0..1 long-press progress (0 = not pressing)
  bool  leveling = false;               // level calibration in progress
  bool  levelSet = true;                // a level reference is stored
  bool  sensorOk = true;                // tilt sensor (remote IMU) delivering; false = "NO SENSOR"
  const char* toast = nullptr;          // transient message
  float toastAge = 0;                   // seconds since toast appeared
  float t = 0;                          // seconds since boot (animations)
  float adapt = 0;                      // tilt filter response 0 (steady) .. 1 (tracking)
  float fps = 0;
  bool  debug = false;
  bool  testPattern = false;            // display diagnostics (serial 'p')
  // GPS speed screen
  uint8_t nav = NAV_NO_PHONE;           // NavState
  const char* obdNote = nullptr;        // OBD adapter state while it isn't supplying speed (null = none)
  float speedKmh = -1;                  // <0 = no fresh data
  float courseDeg = -1;                 // 0..360, <0 = unknown
  float maxKmh = 0, tripKm = 0;         // trip A + max speed (car's speed only)
  float tripBKm = 0;                    // trip B (car's speed only, reset on the engine screen)
  // engine screen (OBD); values < -900 = not available
  uint8_t obdState = 0;                 // obd::State: 0 scanning, 1 connecting, 2 init, 3 no answer, 4 live
  uint8_t gear = 0;                     // current gear (Toyota 21DA), 0 = unknown
  char selector = 0;                    // 'N' (P or N), 'R', 'D'; 0 = unknown
  float rpm = -999, coolantC = -999, volts = -999, intakeC = -999, loadPct = -999;
  float fuelLph = -999;                 // instant fuel flow, L/h
  float fuelL100 = -999;                // instant consumption, L/100 km (only while moving)
  float avgL100 = -999;                 // since trip B reset
  // speed comparison screen; < 0 = no fresh data
  float obdKmh = -1, gpsKmh = -1;
  float cmpKm = 0;                      // distance compared so far (both sources live, moving)
  float cmpPct = -999;                  // corrected car vs GPS over that distance, % (+ = car reads high)
  float cmpSuggestPct = -999;           // speed_correction that would make it 0
  float speedCorrPct = 0;               // speed_correction now
  // boot splash (when splash1 != nullptr)
  const char* splash1 = nullptr;
  const char* splash2 = nullptr;
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
