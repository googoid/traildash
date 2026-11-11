// BLE link to the iPhone app (ios/InclinoLink): the phone connects as a central
// and writes GPS speed + course to one characteristic, about once per second.
#pragma once
#include <stdint.h>

// Shared with the iOS app — keep in sync with ios/InclinoLink/Protocol.swift
#define NAV_SERVICE_UUID "CCCF9685-6127-47E1-9884-2E68EF1D44A3"
#define NAV_CHAR_UUID    "1CCA3ED6-DCF2-4A66-A435-AB5016547E5A"

// Packet v1, little-endian, 10 bytes:
//   u8  version (=1)
//   u8  flags        bit0 speed valid, bit1 course valid
//   u16 speed        0.01 m/s
//   u16 speedAcc     0.01 m/s (0xFFFF = unknown)
//   u16 course       0.01 deg, 0 = north, clockwise
//   u16 courseAcc    0.01 deg (0xFFFF = unknown)
#define NAV_FLAG_SPEED  0x01
#define NAV_FLAG_COURSE 0x02

struct NavFix {
  bool  speedValid = false, courseValid = false;
  float speedMs = 0;       // m/s
  float speedAccMs = -1;   // m/s, <0 = unknown
  float courseDeg = 0;     // 0..360
  float courseAccDeg = -1; // deg, <0 = unknown
  uint32_t rxMs = 0;       // millis() when received
  uint32_t seq = 0;        // increments with every packet
};

namespace blelink {
// Call during setup, BEFORE the RGB display starts (BT init reads/writes NVS flash).
bool begin();
bool connected();
NavFix latest();   // copy of the most recent packet (seq == 0: none yet)
}
