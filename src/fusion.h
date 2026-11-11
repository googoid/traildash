#pragma once
#include "sensors.h"

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
  float trust;     // 0..1 how much the accelerometer is currently trusted
  bool  still;     // vehicle is stationary
};

namespace fusion {

void begin();                          // loads stored mount calibration — call BEFORE the display starts
bool calibrateGyro(int samples);       // call at boot while still; returns false if moving
void update(const ImuSample& s, float dt);
Attitude attitude();

void startLevelCalibration();          // averages ~1 s of accel, then stores
bool levelCalibrationBusy();
bool isLevelCalibrated();               // a level reference has been stored in flash
bool hasUnsavedLevel();                 // new reference waiting to be written to flash
void commitSave();                      // write it — only while the display is paused (board::displayPause)

void rawGravity(float& x, float& y, float& z);  // sensor-frame filtered accel (debug)

}  // namespace fusion
