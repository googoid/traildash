#pragma once
#include <stdint.h>

// ---------------- QMI8658 6-axis IMU ----------------
struct ImuSample {
  float ax, ay, az;   // g, sensor frame
  float gx, gy, gz;   // deg/s, sensor frame
};

namespace imu {
bool begin();                 // 250 Hz, ±4 g, ±256 dps, on-chip low-pass filters
bool read(ImuSample& s);
}

// ---------------- GT911 touch ----------------
struct TouchPoint {
  bool down;
  int16_t x, y;
};

namespace touch {
bool begin();
TouchPoint read();
}
