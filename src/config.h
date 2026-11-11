#pragma once
// ============================================================================
//  Car inclinometer — Waveshare ESP32-S3-Touch-LCD-2.8C
// ============================================================================

// ---------------- user settings ----------------
#define ROLL_WARN_DEG        20.0f   // amber from here
#define ROLL_DANGER_DEG      30.0f   // red + alarm from here
#define PITCH_WARN_DEG       25.0f
#define PITCH_DANGER_DEG     35.0f

#define BUZZER_ALARM         1       // 1 = chirp while any angle is in the red zone
#define BACKLIGHT_PERCENT    85      // 0..100

#define LONG_PRESS_MS        2000    // hold to set "level"
#define DOUBLE_TAP_MS        350     // double-tap resets peak values

// GPS speed screen (data from the iPhone app over BLE)
#define BLE_DEVICE_NAME      "Inclinometer"
#define NAV_STALE_MS         3000    // no packet for this long -> show "--"
#define TRIP_MIN_KMH         2.0f    // below this, speed counts as 0 (GPS jitter when parked)
#define MAX_SPEED_ACC_KMH    5.0f    // max speed only from fixes with speed accuracy better than this

// Fusion tuning (Mahony filter). Lower KP = steadier on bumpy roads, slower to settle.
#define FUSION_KP            0.6f
#define FUSION_KI            0.05f
#define FUSION_KP_MIN        0.05f   // minimum gravity pull (bounds drift during long manoeuvres)
// Accelerometer is ignored while the car is clearly accelerating/braking/cornering:
#define ACC_TRUST_GATE_G     0.12f   // |a| deviating from 1g by more than this -> distrust
#define GYRO_TRUST_GATE_DPS  6.0f    // turning faster than this -> distrust

// ---------------- board pins (do not change) ----------------
#define PIN_I2C_SDA     15
#define PIN_I2C_SCL     7
#define PIN_LCD_SPI_CLK 2
#define PIN_LCD_SPI_MOSI 1
#define PIN_BACKLIGHT   6
#define PIN_TOUCH_INT   16
#define PIN_BAT_ADC     4
#define PIN_BOOT_BTN    0

#define LCD_HSYNC 38
#define LCD_VSYNC 39
#define LCD_DE    40
#define LCD_PCLK  41
#define LCD_DATA_PINS {5, 45, 48, 47, 21, 14, 13, 12, 11, 10, 9, 46, 3, 8, 18, 17}
#define LCD_PCLK_HZ (14 * 1000 * 1000)   // ~50 Hz refresh (less PSRAM load)

// TCA9554 I/O expander bits (EXIO1..8 -> bit0..7)
#define EXIO_LCD_RST  0
#define EXIO_TP_RST   1
#define EXIO_LCD_CS   2
#define EXIO_SD_CS    3
#define EXIO_BUZZER   7

#define I2C_ADDR_TCA9554 0x20
#define I2C_ADDR_QMI8658 0x6B
#define I2C_ADDR_GT911   0x5D
