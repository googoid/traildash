// microSD card (1-bit SDMMC: CLK GPIO2, CMD GPIO1, D0 GPIO42, D3 on EXIO4).
//
//  /incl/config.txt          settings, edited by hand (written with defaults if missing)
//  /incl/rlevel_a|b.txt      level reference (mount orientation of the remote IMU)
//  /incl/state_a|b.txt       peaks, max speed, trip, log counter
//  /incl/logs/NNNNN.csv      drive log, one file per power-up
//  /incl/obd.txt             OBD adapter diagnostics (connection, protocol, supported PIDs)
//
// Unlike the internal flash, the SD card can be written while the display runs.
// Records are written alternately to two files with a sequence number and a
// checksum, so a power cut in the middle of a write keeps the previous version.
#pragma once
#include <stdint.h>
#include <stddef.h>

struct PersistState {
  float peakRollL = 0, peakRollR = 0, peakPitchUp = 0, peakPitchDn = 0;
  float maxKmh = 0, tripKm = 0;
  float fuelL = 0, fuelKm = 0;   // fuel used and distance driven while it was measured (OBD), since trip reset
  uint32_t logNo = 0;   // number of the last drive log file
  uint8_t screen = 0;   // shown screen, restored at power-up
};

namespace storage {
// Mount the card and load config.txt into cfg. Call after board::displayBegin()
// (the panel's init SPI shares pins with the card) and before ui::begin().
bool begin();
bool mounted();

bool loadLevel(float m[9]);
bool saveLevel(const float m[9]);
bool loadState(PersistState& s);
bool saveState(const PersistState& s);

// Drive log
bool logOpen(uint32_t no, const char* header);
bool logWrite(const char* line);   // buffered
void logSync();                    // push buffered rows to the card (call every few seconds)
void logClose();

// Rewrite config.txt from cfg (settings changed in the phone app).
bool saveConfig();

// Append text to /incl/<name> (rotated to <name>.old above 512 KB).
bool appendText(const char* name, const char* text, size_t len);

// Serial access (laptop, no card reader): list the files, print the last `tail` bytes of one.
void listFiles();
void printFile(const char* name, size_t tail);
}  // namespace storage
