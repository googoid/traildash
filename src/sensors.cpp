#include "sensors.h"
#include "board.h"
#include "config.h"
#include <Arduino.h>

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
