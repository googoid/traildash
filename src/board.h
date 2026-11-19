#pragma once
#include <stdint.h>
#include <stddef.h>

namespace board {

bool i2cWrite(uint8_t addr, uint8_t reg, const uint8_t* data, size_t len);
bool i2cRead(uint8_t addr, uint8_t reg, uint8_t* data, size_t len);
bool i2cWrite16(uint8_t addr, uint16_t reg, const uint8_t* data, size_t len);
bool i2cRead16(uint8_t addr, uint16_t reg, uint8_t* data, size_t len);

void begin();                 // I2C + IO expander
void exio(uint8_t bit, bool level);

// Grab the internal DMA strip buffers early (before BLE etc. fragment internal RAM).
bool reserveDisplayMemory();
bool displayBegin();          // ST7701 + RGB panel (double buffered, PSRAM)
// Compose the next frame in SRAM strips: each strip is pre-filled from `bg`
// (a full-frame PSRAM image, may be null) and handed to `fn` to draw on.
// Strips whose hash matches what this buffer already holds are skipped.
typedef void (*StripFn)(uint16_t* buf, int y0, int rows, void* ctx);
typedef uint32_t (*HashFn)(int y0, int rows, void* ctx);
int drawFrame(const uint16_t* bg, StripFn fn, HashFn hash, void* ctx);   // returns strips redrawn
void present();               // queue the composed frame (non-blocking; next drawFrame waits)
void waitFrame();
void rowParityCheck(int x0, int y0, int x1, int y1, float& even, float& odd);   // diagnostics             // block until the last presented frame is on screen
void flushCache(void* p, size_t n);   // CPU cache -> PSRAM (before DMA reads it)
// Stop the RGB panel so flash can be written safely (its interrupt reads PSRAM,
// which is unreachable during flash operations), then start it again.
// Call from the render task only, between frames; turn the backlight off first.
void displayPause();
bool displayResume();

void setBacklight(uint8_t percent);
// Blocking smooth fade between two backlight levels (percent), perceptually even.
void fadeBacklight(uint8_t fromPercent, uint8_t toPercent, uint32_t ms);
void buzzer(bool on);
float batteryVolts();

}  // namespace board
