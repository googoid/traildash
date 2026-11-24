#pragma once
#include "sensors.h"

// Inclinometer: pitch/roll from the measured gravity direction, heavily smoothed,
// with the gyro used only short-term so real tilting shows up without lag (see
// tilt.cpp). Accelerating and braking are measured from the car's speed (setSpeed) and
// removed; without a speed source they show up as pitch.
//
// Car frame: X forward, Y left, Z up.
//
// Sensor: the remote IMU (remote.h), mounted anywhere solid in the car.
// Mounting calibration ("Set level"): with the car parked on level ground the
// measured gravity defines car-up. Car-forward is the sensor axis named by
// remote_forward in config.txt (made horizontal), so the sensor can sit at any tilt.

struct Attitude {
  float pitch;     // deg, + = nose up
  float roll;      // deg, + = right side down
  float adapt;     // 0..1 filter response: 0 = steady (heavy smoothing), 1 = catching up
  bool  bump;      // current sample rejected as a bump / pothole
};

namespace tilt {

void begin();                          // loads the stored mount calibration — call BEFORE the display starts
void update(const ImuSample& s, float dt);
// The filter seeds itself from the first ~0.1 s of samples (attitude holds meanwhile).
// Call reseed() when the sensor comes back after a gap (it may have been moved).
void reseed();
bool seeding();
// Car speed (m/s) and millis() when it was received; call whenever a new reading arrives.
// Its rate of change is the forward acceleration, which is taken out of the pitch.
// `source` tells the speed sources apart (OBD, GPS): a change starts the estimate over.
void setSpeed(float mps, uint32_t ms, uint8_t source);
Attitude attitude();

void startLevelCalibration();          // averages ~1 s of accel, then stores
bool levelCalibrationBusy();
bool isLevelCalibrated();               // a level reference is stored
bool hasUnsavedLevel();                 // new reference waiting to be written to flash
void commitSave();                      // write it — only while the display is paused (board::displayPause)
// SD card storage (see storage.h): the reference as 9 floats (forward, left, up axes)
bool takePendingLevel(float m[9]);      // new reference to store, clears the pending flag
bool getMount(float m[9]);              // false: none stored
void setMount(const float m[9]);        // loaded reference — call before the sensor task starts

void rawGravity(float& x, float& y, float& z);  // sensor-frame filtered accel (debug)

}  // namespace tilt
