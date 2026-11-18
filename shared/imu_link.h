// Remote IMU link: XIAO ESP32-C6 + MPU-6050/6500 (remote/) -> inclinometer (src/).
// Shared by both firmwares — change it in one place only.
//
// The remote is the BLE central: it scans for the inclinometer (which advertises
// NAV_SERVICE_UUID, see src/ble.h), connects, and writes batches of raw samples
// without response to IMU_CHAR_UUID, ~25 times a second. The inclinometer runs its
// own tilt filter and level calibration on them, exactly as for the built-in IMU.
#pragma once
#include <stdint.h>

// What the inclinometer advertises (= NAV_SERVICE_UUID in src/ble.h); the remote scans for it.
#define IMU_LINK_ADV_UUID "CCCF9685-6127-47E1-9884-2E68EF1D44A3"
// Service + characteristic the remote writes to (not advertised, found after connecting).
#define IMU_SERVICE_UUID "65782F4E-DE29-444B-A97D-F39CCFCD31B0"
#define IMU_CHAR_UUID    "9E3E1389-5989-4505-90F7-813B28633235"

// Packet v1, little-endian:
//   u8  version (=1)
//   u8  count        samples in this packet, 1..IMU_LINK_MAX_SAMPLES
//   u16 seq          number of the first sample (wraps; gaps = lost samples)
//   u16 periodUs     sample period
//   count x { i16 ax, ay, az   (1/8192 g)
//             i16 gx, gy, gz } (1/128 deg/s, gyro bias already removed)
// Axes are the sensor's own (as printed on the IMU board).
#define IMU_LINK_VERSION      1
#define IMU_LINK_HDR          6
#define IMU_LINK_SAMPLE       12
#define IMU_LINK_MAX_SAMPLES  16
#define IMU_LINK_ACC_PER_G    8192.f
#define IMU_LINK_GYR_PER_DPS  128.f
