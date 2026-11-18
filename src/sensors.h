#pragma once
#include <stdint.h>

// ---------------- IMU sample (from the remote IMU, see remote.h) ----------------
struct ImuSample {
  float ax, ay, az;   // g, sensor frame
  float gx, gy, gz;   // deg/s, sensor frame
};

// ---------------- GT911 touch ----------------
struct TouchPoint {
  bool down;
  int16_t x, y;
};

namespace touch {
bool begin();
TouchPoint read();
}
