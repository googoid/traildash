// OBD-II over a BLE ELM327 adapter (e.g. Vgate iOS-Vlink).
// The ESP32 is the BLE central here (the iPhone link in ble.cpp stays a peripheral):
// it scans for the adapter, finds its notify/write characteristic(s), initialises the
// ELM327 with automatic protocol search and polls standard mode-01 PIDs.
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace obd {

enum State : uint8_t {
  OBD_SCANNING = 0,   // looking for the adapter
  OBD_CONNECTING,     // BLE connect + service discovery
  OBD_INIT,           // AT commands, protocol search
  OBD_NO_ECU,         // adapter OK, but the car doesn't answer (ignition off?)
  OBD_LIVE,           // polling
};

struct Data {
  State state = OBD_SCANNING;
  // value + millis() when it was last received (0 = never)
  float speedKmh = 0;   uint32_t speedMs = 0;
  float rpm = 0;        uint32_t rpmMs = 0;
  float coolantC = 0;   uint32_t coolantMs = 0;
  float intakeC = 0;    uint32_t intakeMs = 0;
  float loadPct = 0;    uint32_t loadMs = 0;
  float throttlePct = 0; uint32_t throttleMs = 0;
  float volts = 0;      uint32_t voltsMs = 0;
  uint8_t protocol = 0; // ELM327 protocol number (ATDPN): 6..9 = CAN, 3..5 = K-line
};

// Call after blelink::begin() (NimBLE must be initialised). Starts a background task.
void begin();
Data latest();
// True when `ms` (one of the *Ms fields) is recent enough to show.
bool fresh(uint32_t ms, uint32_t maxAgeMs = 3000);
// Take up to `size` bytes of pending diagnostics text (connection, protocol, supported PIDs).
size_t takeLog(char* out, size_t size);

}  // namespace obd
