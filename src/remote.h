// Remote IMU (remote/ firmware: XIAO ESP32-C6 + MPU-6050/6500) connected over BLE.
// It writes batches of raw samples to a characteristic in ble.cpp; they are queued
// here and the sensor task feeds them to the tilt filter instead of the built-in IMU.
// Protocol: shared/imu_link.h.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "sensors.h"

namespace remote {

bool begin();                                     // allocate the queue (PSRAM)

// BLE host task (ble.cpp)
void onPacket(const uint8_t* p, size_t n, uint16_t conn);
void onDisconnect(uint16_t conn);
bool isRemoteConn(uint16_t conn);

bool connected();                                 // BLE link is up
bool live();                                      // samples arrived within REMOTE_STALE_MS
// Sensor task: take up to `max` queued samples (oldest first); dt = their sample period.
int take(ImuSample* out, int max, float& dt);

struct Stats { uint32_t packets, samples, lost, overflow; };
Stats stats();

}  // namespace remote
