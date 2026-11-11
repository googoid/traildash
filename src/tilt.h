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

struct Attitude {
  float pitch;     // deg, + = nose up
  float roll;      // deg, + = right side down
  float adapt;     // 0..1 filter response: 0 = steady (heavy smoothing), 1 = catching up
  bool  bump;      // current sample rejected as a bump / pothole
};

namespace tilt {

void begin();                          // loads stored mount calibration — call BEFORE the display starts
void prime(int samples);               // averages the accelerometer at boot to seed the filter
void update(const ImuSample& s, float dt);
Attitude attitude();

void startLevelCalibration();          // averages ~1 s of accel, then stores
bool levelCalibrationBusy();
bool isLevelCalibrated();               // a level reference has been stored in flash
bool hasUnsavedLevel();                 // new reference waiting to be written to flash
void commitSave();                      // write it — only while the display is paused (board::displayPause)

void rawGravity(float& x, float& y, float& z);  // sensor-frame filtered accel (debug)

}  // namespace tilt
