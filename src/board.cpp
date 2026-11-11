#include "board.h"
#include "config.h"
#include <Arduino.h>
#include <Wire.h>
#include "driver/spi_master.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "freertos/semphr.h"
#include "esp_private/gdma.h"
#include "esp_private/gdma_link.h"
#include "esp_cache.h"

namespace board {

// ============================================================== I2C
static SemaphoreHandle_t i2cMutex;
static uint8_t exioState = 0;

struct I2CLock {
  I2CLock() { xSemaphoreTakeRecursive(i2cMutex, portMAX_DELAY); }
  ~I2CLock() { xSemaphoreGiveRecursive(i2cMutex); }
};

bool i2cWrite(uint8_t addr, uint8_t reg, const uint8_t* data, size_t len) {
  I2CLock l;
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (len) Wire.write(data, len);
  return Wire.endTransmission(true) == 0;
}

bool i2cRead(uint8_t addr, uint8_t reg, uint8_t* data, size_t len) {
  I2CLock l;
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(addr, (uint8_t)len) != len) return false;
  for (size_t i = 0; i < len; i++) data[i] = Wire.read();
  return true;
}

bool i2cWrite16(uint8_t addr, uint16_t reg, const uint8_t* data, size_t len) {
  I2CLock l;
  Wire.beginTransmission(addr);
  Wire.write(reg >> 8);
  Wire.write(reg & 0xFF);
  if (len) Wire.write(data, len);
  return Wire.endTransmission(true) == 0;
}

bool i2cRead16(uint8_t addr, uint16_t reg, uint8_t* data, size_t len) {
  I2CLock l;
  Wire.beginTransmission(addr);
  Wire.write(reg >> 8);
  Wire.write(reg & 0xFF);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(addr, (uint8_t)len) != len) return false;
  for (size_t i = 0; i < len; i++) data[i] = Wire.read();
  return true;
}

void exio(uint8_t bit, bool level) {
  I2CLock l;
  if (level) exioState |= (1 << bit); else exioState &= ~(1 << bit);
  i2cWrite(I2C_ADDR_TCA9554, 0x01, &exioState, 1);
}

void begin() {
  i2cMutex = xSemaphoreCreateRecursiveMutex();
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, 400000);
  delay(50);
  // Outputs: LCD reset high, touch reset high, LCD CS high, SD CS high, buzzer LOW.
  exioState = (1 << EXIO_LCD_RST) | (1 << EXIO_TP_RST) | (1 << EXIO_LCD_CS) | (1 << EXIO_SD_CS);
  i2cWrite(I2C_ADDR_TCA9554, 0x01, &exioState, 1);   // output register first (no buzzer blip)
  uint8_t cfg = 0x00;                                 // all pins output
  i2cWrite(I2C_ADDR_TCA9554, 0x03, &cfg, 1);
}

void buzzer(bool on) { exio(EXIO_BUZZER, on); }

float batteryVolts() {
  return analogReadMilliVolts(PIN_BAT_ADC) * 3.0f / 1000.f / 0.980952f;
}

// ============================================================== backlight
void setBacklight(uint8_t percent) {
  static bool attached = false;
  if (!attached) { ledcAttach(PIN_BACKLIGHT, 20000, 10); attached = true; }
  if (percent > 100) percent = 100;
  ledcWrite(PIN_BACKLIGHT, (uint32_t)percent * 1023 / 100);
}

// ============================================================== ST7701 (3-wire SPI init)
static spi_device_handle_t spi;

static void stCmd(uint8_t c) {
  spi_transaction_t t = {};
  t.cmd = 0; t.addr = c;
  spi_device_polling_transmit(spi, &t);
}
static void stData(uint8_t d) {
  spi_transaction_t t = {};
  t.cmd = 1; t.addr = d;
  spi_device_polling_transmit(spi, &t);
}
static void stSeq(uint8_t c, std::initializer_list<uint8_t> data) {
  stCmd(c);
  for (uint8_t d : data) stData(d);
}

static void st7701Init() {
  // hardware reset via expander
  exio(EXIO_LCD_RST, false); delay(10);
  exio(EXIO_LCD_RST, true);  delay(50);

  spi_bus_config_t bus = {};
  bus.mosi_io_num = PIN_LCD_SPI_MOSI;
  bus.miso_io_num = -1;
  bus.sclk_io_num = PIN_LCD_SPI_CLK;
  bus.quadwp_io_num = -1;
  bus.quadhd_io_num = -1;
  bus.max_transfer_sz = 64;
  spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO);
  spi_device_interface_config_t dev = {};
  dev.command_bits = 1;   // D/C bit
  dev.address_bits = 8;   // the byte itself
  dev.mode = 0;
  dev.clock_speed_hz = 10 * 1000 * 1000;
  dev.spics_io_num = -1;  // CS is on the IO expander
  dev.queue_size = 1;
  spi_bus_add_device(SPI2_HOST, &dev, &spi);

  exio(EXIO_LCD_CS, false); delay(5);

  // Vendor init sequence for the 2.8" round panel (from Waveshare reference code)
  stSeq(0xFF, {0x77, 0x01, 0x00, 0x00, 0x13});
  stSeq(0xEF, {0x08});
  stSeq(0xFF, {0x77, 0x01, 0x00, 0x00, 0x10});
  stSeq(0xC0, {0x3B, 0x00});
  stSeq(0xC1, {0x10, 0x0C});
  stSeq(0xC2, {0x07, 0x0A});
  stSeq(0xC7, {0x00});
  stSeq(0xCC, {0x10});
  stSeq(0xCD, {0x08});
  stSeq(0xB0, {0x05, 0x12, 0x98, 0x0E, 0x0F, 0x07, 0x07, 0x09, 0x09, 0x23, 0x05, 0x52, 0x0F, 0x67, 0x2C, 0x11});
  stSeq(0xB1, {0x0B, 0x11, 0x97, 0x0C, 0x12, 0x06, 0x06, 0x08, 0x08, 0x22, 0x03, 0x51, 0x11, 0x66, 0x2B, 0x0F});
  stSeq(0xFF, {0x77, 0x01, 0x00, 0x00, 0x11});
  stSeq(0xB0, {0x5D});
  stSeq(0xB1, {0x3E});
  stSeq(0xB2, {0x81});
  stSeq(0xB3, {0x80});
  stSeq(0xB5, {0x4E});
  stSeq(0xB7, {0x85});
  stSeq(0xB8, {0x20});
  stSeq(0xC1, {0x78});
  stSeq(0xC2, {0x78});
  stSeq(0xD0, {0x88});
  stSeq(0xE0, {0x00, 0x00, 0x02});
  stSeq(0xE1, {0x06, 0x30, 0x08, 0x30, 0x05, 0x30, 0x07, 0x30, 0x00, 0x33, 0x33});
  stSeq(0xE2, {0x11, 0x11, 0x33, 0x33, 0xF4, 0x00, 0x00, 0x00, 0xF4, 0x00, 0x00, 0x00});
  stSeq(0xE3, {0x00, 0x00, 0x11, 0x11});
  stSeq(0xE4, {0x44, 0x44});
  stSeq(0xE5, {0x0D, 0xF5, 0x30, 0xF0, 0x0F, 0xF7, 0x30, 0xF0, 0x09, 0xF1, 0x30, 0xF0, 0x0B, 0xF3, 0x30, 0xF0});
  stSeq(0xE6, {0x00, 0x00, 0x11, 0x11});
  stSeq(0xE7, {0x44, 0x44});
  stSeq(0xE8, {0x0C, 0xF4, 0x30, 0xF0, 0x0E, 0xF6, 0x30, 0xF0, 0x08, 0xF0, 0x30, 0xF0, 0x0A, 0xF2, 0x30, 0xF0});
  stSeq(0xE9, {0x36, 0x01});
  stSeq(0xEB, {0x00, 0x01, 0xE4, 0xE4, 0x44, 0x88, 0x40});
  stSeq(0xED, {0xFF, 0x10, 0xAF, 0x76, 0x54, 0x2B, 0xCF, 0xFF, 0xFF, 0xFC, 0xB2, 0x45, 0x67, 0xFA, 0x01, 0xFF});
  stSeq(0xEF, {0x08, 0x08, 0x08, 0x45, 0x3F, 0x54});
  stSeq(0xFF, {0x77, 0x01, 0x00, 0x00, 0x00});
  stCmd(0x11);           // sleep out
  delay(120);
  stSeq(0x3A, {0x66});
  stSeq(0x36, {0x00});
  stSeq(0x35, {0x00});
  stCmd(0x29);           // display on

  exio(EXIO_LCD_CS, true);
}

// ============================================================== GDMA memory copies
// The CPU reaches PSRAM through a 32-byte-line cache that is very slow for
// streaming whole frames (~18 MB/s here). The GDMA engine bypasses the cache,
// so frames are composed in internal SRAM strips and moved by DMA.
struct DmaPair {
  gdma_channel_handle_t tx = nullptr, rx = nullptr;
  gdma_link_list_handle_t ltx = nullptr, lrx = nullptr;
  SemaphoreHandle_t done = nullptr;
  volatile bool busy = false;
};
static DmaPair dmaIn, dmaOut;   // PSRAM->SRAM (backgrounds), SRAM->PSRAM (frames)

static bool IRAM_ATTR onDmaEof(gdma_channel_handle_t, gdma_event_data_t*, void* arg) {
  DmaPair* p = (DmaPair*)arg;
  BaseType_t woken = pdFALSE;
  xSemaphoreGiveFromISR(p->done, &woken);
  return woken == pdTRUE;
}

static bool dmaInit(DmaPair& p) {
  gdma_channel_alloc_config_t tc = {};
  tc.direction = GDMA_CHANNEL_DIRECTION_TX;
  tc.flags.reserve_sibling = 1;
  if (gdma_new_ahb_channel(&tc, &p.tx) != ESP_OK) return false;
  gdma_channel_alloc_config_t rc = {};
  rc.direction = GDMA_CHANNEL_DIRECTION_RX;
  rc.sibling_chan = p.tx;
  if (gdma_new_ahb_channel(&rc, &p.rx) != ESP_OK) return false;
  // M2M needs a real (unused) trigger id, not the -1 placeholder
  gdma_trigger_t trig = GDMA_MAKE_TRIGGER(GDMA_TRIG_PERIPH_M2M, 0);
  uint32_t freeMask = 0;
  gdma_get_free_m2m_trig_id_mask(p.tx, &freeMask);
  if (!freeMask) return false;
  trig.instance_id = __builtin_ctz(freeMask);
  gdma_connect(p.rx, trig);
  gdma_connect(p.tx, trig);
  gdma_transfer_config_t t = {};
  t.max_data_burst_size = 32;
  t.access_ext_mem = true;
  gdma_config_transfer(p.tx, &t);
  gdma_config_transfer(p.rx, &t);
  gdma_strategy_config_t st = {};
  st.owner_check = true;
  st.auto_update_desc = true;
  st.eof_till_data_popped = true;
  gdma_apply_strategy(p.tx, &st);
  gdma_apply_strategy(p.rx, &st);

  gdma_link_list_config_t lc = {};
  lc.num_items = 16;
  lc.item_alignment = 4;
  lc.buffer_alignment = 32;   // PSRAM (EDMA) needs 32-byte aligned chunks
  lc.flags.check_owner = 1;
  if (gdma_new_link_list(&lc, &p.ltx) != ESP_OK) return false;
  if (gdma_new_link_list(&lc, &p.lrx) != ESP_OK) return false;

  p.done = xSemaphoreCreateBinary();
  gdma_rx_event_callbacks_t cbs = {};
  cbs.on_recv_eof = onDmaEof;
  return gdma_register_rx_event_callbacks(p.rx, &cbs, &p) == ESP_OK;
}

static void dmaStart(DmaPair& p, void* dst, const void* src, size_t n) {
  gdma_buffer_mount_config_t tb = {};
  tb.buffer = (void*)src;
  tb.length = n;
  tb.flags.mark_eof = 1;
  tb.flags.mark_final = 1;
  gdma_link_mount_buffers(p.ltx, 0, &tb, 1, nullptr);
  gdma_buffer_mount_config_t rb = {};
  rb.buffer = dst;
  rb.length = n;
  rb.flags.mark_final = 1;
  gdma_link_mount_buffers(p.lrx, 0, &rb, 1, nullptr);
  xSemaphoreTake(p.done, 0);
  p.busy = true;
  gdma_start(p.rx, gdma_link_get_head_addr(p.lrx));
  gdma_start(p.tx, gdma_link_get_head_addr(p.ltx));
}

static void dmaWait(DmaPair& p) {
  if (!p.busy) return;
  if (xSemaphoreTake(p.done, pdMS_TO_TICKS(100)) != pdTRUE) {
    Serial.println("[dma] timeout");
    gdma_stop(p.tx); gdma_stop(p.rx);
    gdma_reset(p.tx); gdma_reset(p.rx);
  }
  p.busy = false;
}

void flushCache(void* p, size_t n) {
  esp_cache_msync(p, n, ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
}

// ============================================================== RGB panel
static esp_lcd_panel_handle_t panel;
static uint16_t* fbs[2];
static int backIdx = 1;
static SemaphoreHandle_t frameSem;
static uint16_t* strips[3];
static constexpr int STRIP_ROWS = 16;   // 3 strips must fit in internal DMA RAM next to the BLE controller
static constexpr size_t STRIP_BYTES = 480 * STRIP_ROWS * 2;
static constexpr size_t FB_BYTES = 480 * 480 * 2;

static bool IRAM_ATTR onFrameDone(esp_lcd_panel_handle_t, const esp_lcd_rgb_panel_event_data_t*, void*) {
  BaseType_t woken = pdFALSE;
  xSemaphoreGiveFromISR(frameSem, &woken);
  return woken == pdTRUE;
}

static bool createPanel() {
  esp_lcd_rgb_panel_config_t cfg = {};
  cfg.clk_src = LCD_CLK_SRC_DEFAULT;
  cfg.timings.pclk_hz = LCD_PCLK_HZ;
  cfg.timings.h_res = 480;
  cfg.timings.v_res = 480;
  cfg.timings.hsync_pulse_width = 8;
  cfg.timings.hsync_back_porch = 10;
  cfg.timings.hsync_front_porch = 50;
  cfg.timings.vsync_pulse_width = 2;
  cfg.timings.vsync_back_porch = 18;
  cfg.timings.vsync_front_porch = 8;
  cfg.timings.flags.pclk_active_neg = 0;
  cfg.data_width = 16;
  cfg.bits_per_pixel = 16;
  cfg.num_fbs = 2;
  cfg.bounce_buffer_size_px = 480 * 10;
  cfg.dma_burst_size = 64;
  cfg.hsync_gpio_num = LCD_HSYNC;
  cfg.vsync_gpio_num = LCD_VSYNC;
  cfg.de_gpio_num = LCD_DE;
  cfg.pclk_gpio_num = LCD_PCLK;
  cfg.disp_gpio_num = -1;
  const int pins[16] = LCD_DATA_PINS;
  for (int i = 0; i < 16; i++) cfg.data_gpio_nums[i] = pins[i];
  cfg.flags.fb_in_psram = 1;
  cfg.flags.bb_invalidate_cache = 1;   // scan-out must not leave frame lines in the cache (DMA writes them)

  if (esp_lcd_new_rgb_panel(&cfg, &panel) != ESP_OK) { panel = nullptr; return false; }

  esp_lcd_rgb_panel_event_callbacks_t cbs = {};
  cbs.on_frame_buf_complete = onFrameDone;
  esp_lcd_rgb_panel_register_event_callbacks(panel, &cbs, nullptr);

  esp_lcd_panel_reset(panel);
  esp_lcd_panel_init(panel);

  void *a, *b;
  if (esp_lcd_rgb_panel_get_frame_buffer(panel, 2, &a, &b) != ESP_OK) return false;
  fbs[0] = (uint16_t*)a;
  fbs[1] = (uint16_t*)b;
  memset(fbs[0], 0, FB_BYTES);
  memset(fbs[1], 0, FB_BYTES);
  // from now on only DMA writes the frame buffers: push out + drop any cached lines
  esp_cache_msync(fbs[0], FB_BYTES, ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_INVALIDATE);
  esp_cache_msync(fbs[1], FB_BYTES, ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_INVALIDATE);
  backIdx = 1;
  return true;
}

bool reserveDisplayMemory() {
  for (int i = 0; i < 3; i++) {
    if (!strips[i]) strips[i] = (uint16_t*)heap_caps_aligned_alloc(64, STRIP_BYTES, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    if (!strips[i]) return false;
  }
  return true;
}

bool displayBegin() {
  if (!reserveDisplayMemory()) return false;
  st7701Init();
  frameSem = xSemaphoreCreateBinary();
  if (!createPanel()) return false;
  if (!dmaInit(dmaIn) || !dmaInit(dmaOut)) { Serial.println("[dma] init failed"); return false; }
  return true;
}

static bool presentPending = false;
static uint32_t lastHash[2][480 / STRIP_ROWS];
static bool hashValid[2] = {false, false};

static void waitFree() {
  if (!presentPending) return;
  xSemaphoreTake(frameSem, pdMS_TO_TICKS(60));   // previous front buffer released
  presentPending = false;
}

int drawFrame(const uint16_t* bg, StripFn fn, HashFn hashFn, void* ctx) {
  const int n = 480 / STRIP_ROWS;
  int dirty[n], nd = 0;
  uint32_t hs[n];
  for (int k = 0; k < n; k++) {
    hs[k] = hashFn ? hashFn(k * STRIP_ROWS, STRIP_ROWS, ctx) : 0;
    if (!hashFn || !hashValid[backIdx] || hs[k] != lastHash[backIdx][k]) dirty[nd++] = k;
  }
  waitFree();
  uint16_t* fb = fbs[backIdx];
  // 3 SRAM strips rotate: one receiving its background, one being drawn, one being sent out.
  if (bg && nd) dmaStart(dmaIn, strips[0], bg + dirty[0] * STRIP_ROWS * 480, STRIP_BYTES);
  for (int i = 0; i < nd; i++) {
    const int k = dirty[i];
    uint16_t* cur = strips[i % 3];
    dmaWait(dmaIn);                                   // background of this strip has arrived
    if (bg && i + 1 < nd)                             // that buffer was last used 2 strips ago: already sent
      dmaStart(dmaIn, strips[(i + 1) % 3], bg + dirty[i + 1] * STRIP_ROWS * 480, STRIP_BYTES);
    fn(cur, k * STRIP_ROWS, STRIP_ROWS, ctx);
    dmaWait(dmaOut);                                  // previous strip is out
    dmaStart(dmaOut, fb + k * STRIP_ROWS * 480, cur, STRIP_BYTES);
  }
  dmaWait(dmaOut);
  memcpy(lastHash[backIdx], hs, sizeof hs);
  hashValid[backIdx] = hashFn != nullptr;
  return nd;
}

void displayPause() {
  if (!panel) return;
  waitFree();                       // no buffer switch in flight
  esp_lcd_panel_del(panel);         // stops scan-out + its interrupt, frees the frame buffers
  panel = nullptr;
}

bool displayResume() {
  if (panel) return true;
  presentPending = false;
  hashValid[0] = hashValid[1] = false;   // new (blank) buffers: redraw everything
  xSemaphoreTake(frameSem, 0);
  return createPanel();
}

void waitFrame() { waitFree(); }

// Diagnostics: average luminance of even vs odd rows in a region of the frame on screen.
void rowParityCheck(int x0, int y0, int x1, int y1, float& even, float& odd) {
  uint16_t* fb = fbs[backIdx ^ 1];
  esp_cache_msync(fb, FB_BYTES, ESP_CACHE_MSYNC_FLAG_DIR_M2C);   // see what DMA wrote, not stale cache
  double s[2] = {0, 0}; int n[2] = {0, 0};
  for (int y = y0; y < y1; y++)
    for (int x = x0; x < x1; x++) {
      uint16_t p = fb[y * 480 + x];
      s[y & 1] += ((p >> 11) & 31) * 8 + ((p >> 5) & 63) * 4 + (p & 31) * 8;
      n[y & 1]++;
    }
  even = n[0] ? s[0] / n[0] / 3 : 0;
  odd = n[1] ? s[1] / n[1] / 3 : 0;
}

void present() {
  esp_lcd_panel_draw_bitmap(panel, 0, 0, 480, 480, fbs[backIdx]);  // zero-copy: switches scan-out buffer
  xSemaphoreTake(frameSem, 0);                                      // drop a stale event
  presentPending = true;                                            // drawFrame() waits before reusing
  backIdx ^= 1;
}

}  // namespace board
