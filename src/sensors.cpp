#include "sensors.h"
#include "board.h"
#include "config.h"
#include <Arduino.h>

// =============================================================== QMI8658
namespace imu {

enum : uint8_t {
  WHO_AM_I = 0x00, CTRL1 = 0x02, CTRL2 = 0x03, CTRL3 = 0x04, CTRL5 = 0x06,
  CTRL7 = 0x08, AX_L = 0x35, RESET = 0x60,
};

static bool wr(uint8_t reg, uint8_t v) { return board::i2cWrite(I2C_ADDR_QMI8658, reg, &v, 1); }

static constexpr float ACC_LSB = 1.f / 8192.f;   // ±4 g
static constexpr float GYR_LSB = 1.f / 128.f;    // ±256 dps

bool begin() {
  uint8_t id = 0;
  board::i2cRead(I2C_ADDR_QMI8658, WHO_AM_I, &id, 1);
  if (id != 0x05) {
    Serial.printf("[imu] unexpected WHO_AM_I 0x%02x\n", id);
    if (id == 0 || id == 0xFF) return false;
  }
  wr(RESET, 0xB0);
  delay(20);
  wr(CTRL1, 0x40);                  // register auto-increment, little endian
  wr(CTRL2, (0x1 << 4) | 0x05);     // accel ±4 g, 250 Hz
  wr(CTRL3, (0x4 << 4) | 0x05);     // gyro ±256 dps, 250 Hz
  // LPFs: accel mode 1 (3.63% ODR ≈ 9 Hz), gyro mode 3 (13.37% ODR ≈ 33 Hz)
  wr(CTRL5, (0x3 << 5) | 0x10 | (0x1 << 1) | 0x01);
  wr(CTRL7, 0x03);                  // enable accel + gyro
  delay(30);
  return true;
}

bool read(ImuSample& s) {
  uint8_t b[12];
  if (!board::i2cRead(I2C_ADDR_QMI8658, AX_L, b, 12)) return false;
  int16_t r[6];
  for (int i = 0; i < 6; i++) r[i] = (int16_t)(b[2 * i] | (b[2 * i + 1] << 8));
  s.ax = r[0] * ACC_LSB; s.ay = r[1] * ACC_LSB; s.az = r[2] * ACC_LSB;
  s.gx = r[3] * GYR_LSB; s.gy = r[4] * GYR_LSB; s.gz = r[5] * GYR_LSB;
  return true;
}

}  // namespace imu

// =============================================================== GT911
namespace touch {

static constexpr uint16_t REG_STATUS = 0x814E;
static constexpr uint16_t REG_POINT1 = 0x8150;
static constexpr uint16_t REG_PRODUCT = 0x8140;

bool begin() {
  // Reset with INT held low -> I2C address 0x5D
  pinMode(PIN_TOUCH_INT, OUTPUT);
  digitalWrite(PIN_TOUCH_INT, LOW);
  board::exio(EXIO_TP_RST, false);
  delay(10);
  board::exio(EXIO_TP_RST, true);
  delay(60);
  pinMode(PIN_TOUCH_INT, INPUT);
  delay(100);
  uint8_t id[4] = {0};
  bool ok = board::i2cRead16(I2C_ADDR_GT911, REG_PRODUCT, id, 3);
  Serial.printf("[touch] GT911 id '%c%c%c' %s\n", id[0], id[1], id[2], ok ? "ok" : "FAIL");
  return ok;
}

TouchPoint read() {
  static TouchPoint last = {false, 0, 0};
  uint8_t st = 0;
  if (!board::i2cRead16(I2C_ADDR_GT911, REG_STATUS, &st, 1)) return last;
  if (!(st & 0x80)) return last;        // no new data since last read: state unchanged
  uint8_t n = st & 0x0F;
  if (n == 0) {
    last.down = false;
  } else {
    uint8_t p[6];
    if (board::i2cRead16(I2C_ADDR_GT911, REG_POINT1, p, 6)) {
      last.down = true;
      last.x = p[0] | (p[1] << 8);
      last.y = p[2] | (p[3] << 8);
    }
  }
  uint8_t zero = 0;
  board::i2cWrite16(I2C_ADDR_GT911, REG_STATUS, &zero, 1);
  return last;
}

}  // namespace touch
