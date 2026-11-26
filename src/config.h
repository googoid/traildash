#pragma once
// ============================================================================
//  TrailDash car dash display — Waveshare ESP32-S3-Touch-LCD-2.8C
// ============================================================================

// ---------------- user settings ----------------
// These are defaults: /incl/config.txt on the SD card overrides them (see settings.cpp).
#define ROLL_WARN_DEG        20.0f   // amber from here
#define ROLL_DANGER_DEG      30.0f   // red + alarm from here
#define PITCH_WARN_DEG       25.0f
#define PITCH_DANGER_DEG     35.0f

#define BUZZER_ALARM         1       // 1 = chirp while any angle is in the red zone
#define BACKLIGHT_PERCENT    85      // 0..100

#define LONG_PRESS_MS        2000    // hold to set "level"
#define DOUBLE_TAP_MS        350     // double-tap resets peak values
#define SWIPE_START_PX       25      // finger moved this far sideways: a swipe (no tap / hold)
#define SWIPE_MIN_PX         60      // released at least this far: change screen (left = next)
#define SLIDE_MS             300     // screen change slide animation
#define SLIDE_HZ             60      // panel refresh during the slide: 30 fps (50 Hz would be 25)

// SD card
#define LOG_ENABLED          1       // drive log CSV, one file per power-up
#define LOG_INTERVAL_S       1.0f    // seconds between log rows
#define LOG_SYNC_S           10      // rows reach the card at least this often (lost on power cut otherwise)
#define STATE_SAVE_S         30      // trip/max speed saved at most this often (peaks: within 2 s)

// GPS speed screen (data from the iPhone app over BLE)
#define BLE_DEVICE_NAME      "TrailDash"
#define NAV_STALE_MS         3000    // no packet for this long -> show "--"
#define TRIP_MIN_KMH         2.0f    // below this, speed counts as 0 (GPS jitter when parked)
#define MAX_SPEED_ACC_KMH    5.0f    // max speed only from fixes with speed accuracy better than this

// OBD-II BLE adapter (ELM327, e.g. Vgate iOS-Vlink): advertised name contains one of these (lower case)
#define OBD_NAME_KEYS        {"vlink", "obd", "icar", "elm327"}
#define OBD_CMD_TIMEOUT_MS   1500    // one PID request (K-line can take ~300 ms)
#define OBD_SEARCH_TIMEOUT_MS 15000  // first request: the adapter tries every protocol
#define OBD_RESCAN_MS        3000    // pause between scans when no adapter was found
// Probe: log the Toyota gear / selector data blocks to /incl/probe.txt when they change
#define OBD_PROBE            1       // config.txt: obd_probe

// Speed comparison screen: car (OBD) vs GPS
#define CMP_MIN_KMH          20.f    // average only above this (GPS speed is noisy when slow)
#define SPEED_CORRECTION_PCT 0.f     // car (OBD) speed correction, % (config.txt: speed_correction)

// Engine screen
#define ENGINE_REDLINE_RPM   5000    // red zone of the rev gauge (config.txt: redline)
#define ENGINE_GAUGE_RPM     6000    // full scale
#define COOLANT_COLD_C       60      // blue below (engine still cold)
#define COOLANT_HOT_C        105     // amber from here
#define COOLANT_DANGER_C     110     // red from here
// Fuel use from the mass airflow sensor: fuel = air / AFR. Exact at cruise (closed loop);
// reads low under full throttle, when the engine runs rich.
#define FUEL_AFR             14.7f   // stoichiometric air/fuel ratio of petrol
#define FUEL_G_PER_L         745.f   // density of petrol
#define FUEL_SMOOTH_S        2.0f    // smoothing of the instant consumption

// Remote IMU (remote/: XIAO ESP32-C6 + MPU-6050) — the only tilt sensor
#define REMOTE_FORWARD       "+x"    // remote sensor axis pointing (roughly) forward: +x -x +y -y +z -z
#define REMOTE_STALE_MS      500     // no samples this long -> "NO SENSOR"

// Tilt filter. Accelerating/braking is taken out of the pitch using the car's speed (OBD, else GPS).
#define TILT_GYRO_CARRY      1       // 1 = gyro turns the reading instantly, accel keeps it true (recommended)
                                     // 0 = accel only; gyro just tells the filter when to follow faster
#if TILT_GYRO_CARRY
#define TILT_SMOOTH_S        2.5f    // accel smoothing (higher = calmer numbers on rough roads)
#define TILT_FAST_S          0.3f    // smoothing while catching up a large error
#else
#define TILT_SMOOTH_S        1.2f    // smoothing while steady (higher = calmer, but slower on slow tilts)
#define TILT_FAST_S          0.05f   // smoothing while the car is tilting (lower = snappier)
#endif
#define TILT_GYRO_MIN_DPS    2.0f    // [mode 0] tilt rate below this = not tilting (road shake)
#define TILT_GYRO_FULL_DPS   6.0f    // [mode 0] tilt rate at which the filter is fully fast
#define TILT_GYRO_HOLD_S     0.4f    // [mode 0] stay fast this long after the rotation stops
#define TILT_GYRO_AVG_S      30.0f   // gyro bias tracking time constant
#define TILT_GYRO_BIAS_GATE_DPS 3.0f // bias is only tracked while rotating slower than this
// a large, persistent accelerometer deviation speeds the filter up (gross errors can't stick)
#define TILT_DETECT_S        0.4f    // change detector time constant
#define TILT_ADAPT_MIN_DEG   5.0f    // deviation where it starts to speed up
#define TILT_ADAPT_DEG       10.0f   // deviation where it is fully fast
#define TILT_BUMP_G          0.15f   // |a| jumping more than this from its average = bump, sample ignored
#define TILT_BUMP_HOLD_S     0.5f    // ...but never ignore for longer than this
#define TILT_ACC_SPAN_MS     500     // forward accel = speed change over at least this long
#define TILT_ACC_SMOOTH_S    0.15f   // smoothing of that acceleration
#define TILT_ACC_DELAY_MS    500     // how late that acceleration is (OBD reply + span); accel is delayed to match
#define TILT_ACC_GAP_MS      1500    // no speed reading this long -> no correction
#define DISPLAY_HYST_DEG     0.3f    // whole-degree readout changes only this far past the .5 boundary

// ---------------- board pins (do not change) ----------------
#define PIN_I2C_SDA     15
#define PIN_I2C_SCL     7
#define PIN_LCD_SPI_CLK 2
#define PIN_LCD_SPI_MOSI 1
#define PIN_BACKLIGHT   6
#define PIN_TOUCH_INT   16
#define PIN_BAT_ADC     4
#define PIN_BOOT_BTN    0
// microSD, 1-bit SDMMC. CLK/CMD are the panel's init SPI pins (free after display init); D3 = EXIO4.
#define PIN_SD_CLK      2
#define PIN_SD_CMD      1
#define PIN_SD_D0       42

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
#define I2C_ADDR_GT911   0x5D
