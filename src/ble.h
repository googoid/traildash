// BLE link to the iPhone app (ios/InclinoLink): the phone connects as a central
// and writes GPS speed + course to one characteristic, about once per second.
#pragma once
#include <stdint.h>

// Shared with the iOS app — keep in sync with ios/InclinoLink/NavProtocol.swift
#define NAV_SERVICE_UUID "CCCF9685-6127-47E1-9884-2E68EF1D44A3"
#define NAV_CHAR_UUID    "1CCA3ED6-DCF2-4A66-A435-AB5016547E5A"

// Settings (same service). Text, "key=value" lines, '\n'-separated:
//   read   "v=1", device status (card, sensor, level, leveling: 0/1, read-only),
//          then every setting in config.txt syntax (settings::formatCompact)
//   write  the settings to change, e.g. "backlight=60\n"; clamped like config.txt,
//          applied at once and saved to config.txt (with an SD card). Read again to
//          see the values in effect.
#define CFG_CHAR_UUID    "1219E800-95D4-4A08-9572-1D76DCB4B1FF"
#define CFG_PROTO_VER    1
#define CFG_MAX_LEN      320
// Actions: write one byte. The device confirms with a toast on its screen.
#define CTRL_CHAR_UUID   "CA5380E5-BE5F-4B00-8875-DB58900DAEAC"
enum : uint8_t { CMD_NONE = 0, CMD_SET_LEVEL = 1, CMD_RESET_PEAKS = 2, CMD_RESET_TRIP = 3 };

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
// Phone app actions (CMD_*): the last one requested, cleared by taking it.
uint8_t takeCommand();
// Device status reported with the settings (call regularly).
void setStatus(bool card, bool sensor, bool levelSet, bool leveling);
}
