#pragma once
#include "sensors.h"

// Inclinometer: pitch/roll from the measured gravity direction, heavily smoothed,
// with the gyro used only short-term so real tilting shows up without lag (see
// tilt.cpp). Accelerating and braking briefly show up as pitch — accepted by design.
//
// Car frame: X forward, Y left, Z up.
//
// Mounting calibration ("Set level"): with the car parked on level ground the
// measured gravity defines car-up. Car-forward is taken from the screen:
//  - screen roughly upright (dash / windshield mount): forward = into the screen
//  - screen roughly flat (console mount):              forward = top edge of screen
// So the unit can be mounted at any tilt and still read true pitch/roll.

// Which IMU feeds the filter. Each has its own level reference.
enum Source : uint8_t { SRC_BUILTIN = 0, SRC_REMOTE, SRC_COUNT };   // remote: remote.h

struct Attitude {
  float pitch;     // deg, + = nose up
  float roll;      // deg, + = right side down
  float adapt;     // 0..1 filter response: 0 = steady (heavy smoothing), 1 = catching up
  bool  bump;      // current sample rejected as a bump / pothole
};

namespace tilt {

void begin();                          // loads stored mount calibrations — call BEFORE the display starts
void prime(int samples);               // averages the accelerometer at boot to seed the filter
void update(const ImuSample& s, float dt);
// Switch sensor: its own level reference applies, and the filter re-seeds from its
// next samples (~0.1 s, attitude holds meanwhile; see seeding()).
void setSource(Source s);
Source source();
bool seeding();
Attitude attitude();

void startLevelCalibration();          // averages ~1 s of accel, then stores
bool levelCalibrationBusy();
bool isLevelCalibrated();               // a level reference is stored for the current source
bool hasUnsavedLevel();                 // new reference waiting to be written to flash
void commitSave();                      // write it — only while the display is paused (board::displayPause)
// SD card storage (see storage.h): the reference as 9 floats (forward, left, up axes)
bool takePendingLevel(float m[9], Source& s);   // new reference to store, clears the pending flag
bool getMount(Source s, float m[9]);    // false: none stored for that sensor
void setMount(Source s, const float m[9]);      // loaded reference — call before the sensor task starts

void rawGravity(float& x, float& y, float& z);  // sensor-frame filtered accel (debug)

}  // namespace tilt
