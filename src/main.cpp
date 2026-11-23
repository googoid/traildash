// Car inclinometer for the Waveshare ESP32-S3-Touch-LCD-2.8C
//
//  Tap            next screen   (dual gauge -> speed -> peaks)
//  Hold 2 s       set level (park on flat ground first; stored in flash, screen blinks)
//                 on the speed screen: reset max speed + trip instead
//  Double-tap     reset peak values
//  BOOT button    short = next screen, long = same as hold
//
// Core 1: sensor task (remote IMU samples, tilt filter, touch, gestures, buzzer)
// Core 0: render task (UI into PSRAM double buffer), NimBLE host (GPS from the iPhone,
//         samples from the remote IMU)
//
// Tilt comes only from the remote IMU (remote/, over BLE). Without it the gauge shows
// "NO SENSOR"; angles, peaks and the alarm wait until it is back.
#include <Arduino.h>
#include <esp_heap_caps.h>
#include "config.h"
#include "board.h"
#include "sensors.h"
#include "tilt.h"
#include "ui.h"
#include "gfx.h"
#include "ble.h"
#include "obd.h"
#include "remote.h"
#include "settings.h"
#include "storage.h"

// ------------------------------------------------------------------ shared state
struct Shared {
  volatile float pitch = 0, roll = 0, adapt = 0;
  volatile float peakRollL = 0, peakRollR = 0, peakPitchUp = 0, peakPitchDn = 0;
  volatile uint8_t screen = SCREEN_SPEED;
  volatile float holdProgress = 0;
  volatile bool leveling = false;
  volatile bool levelSet = false;
  const char* volatile toast = nullptr;
  volatile uint32_t toastAt = 0;
  volatile bool debug = false;
  volatile float fps = 0;
  volatile bool testPattern = false;
  volatile bool fbCheck = false;
  volatile bool sensorOk = false;    // remote IMU live and the filter seeded
  // GPS (from the phone)
  volatile uint8_t nav = NAV_NO_PHONE;
  const char* volatile obdNote = nullptr;
  volatile float speedKmh = -1;   // <0 = no fresh data
  volatile float courseDeg = -1;  // <0 = unknown
  volatile float maxKmh = 0;
  volatile float tripKm = 0;
  // engine (OBD); < -900 = not available
  volatile uint8_t obdState = 0;
  volatile float rpm = -999, coolantC = -999, volts = -999, intakeC = -999, loadPct = -999;
  volatile uint8_t gear = 0;      // 0 = unknown
  volatile char selector = 0;     // 'N' (P or N), 'R', 'D'; 0 = unknown
  volatile float fuelLph = -999, fuelL100 = -999;
  volatile float fuelL = 0, fuelKm = 0;   // since trip reset (average consumption)
  // speed comparison: both sources separately, distances while both were live and moving
  volatile float obdKmh = -1, gpsKmh = -1;
  volatile float cmpObdKm = 0, cmpGpsKm = 0;
};
static Shared sh;

static void showToast(const char* msg) {
  sh.toast = msg;
  sh.toastAt = millis();
}

static void resetPeaks() {
  sh.peakRollL = 0; sh.peakRollR = 0; sh.peakPitchUp = 0; sh.peakPitchDn = 0;
}

static void resetTrip() { sh.maxKmh = 0; sh.tripKm = 0; sh.fuelL = 0; sh.fuelKm = 0; }

static void nextScreen() { sh.screen = (sh.screen + 1) % SCREEN_COUNT; }

static void startLevel() {
  if (tilt::levelCalibrationBusy()) return;
  if (!sh.sensorOk) { showToast("NO SENSOR"); return; }
  tilt::startLevelCalibration();
  sh.leveling = true;
}

// ------------------------------------------------------------------ gestures
struct PressTracker {
  bool down = false, longFired = false;
  uint32_t t0 = 0;
  uint32_t pendingTapAt = 0;   // single tap waiting for a possible second tap

  // returns hold progress 0..1
  float update(bool isDown, uint32_t now, bool allowDoubleTap) {
    float progress = 0;
    if (isDown && !down) { down = true; longFired = false; t0 = now; }
    if (isDown && down) {
      uint32_t held = now - t0;
      if (held > 350 && !longFired) {
        progress = (float)(held - 350) / (LONG_PRESS_MS - 350);
        if (progress >= 1.f) {
          longFired = true; pendingTapAt = 0; progress = 0;
          if (sh.screen == SCREEN_SPEED || sh.screen == SCREEN_ENGINE) { resetTrip(); showToast("TRIP RESET"); }
          else if (sh.screen == SCREEN_COMPARE) { sh.cmpObdKm = 0; sh.cmpGpsKm = 0; showToast("AVERAGE RESET"); }
          else startLevel();
        }
      }
    }
    if (!isDown && down) {
      down = false;
      if (!longFired && now - t0 < 350) {
        if (!allowDoubleTap) {
          nextScreen();
        } else if (pendingTapAt && now - pendingTapAt < DOUBLE_TAP_MS) {
          pendingTapAt = 0;
          resetPeaks();
          showToast("PEAKS RESET");
        } else {
          pendingTapAt = now;
        }
      }
    }
    if (pendingTapAt && !down && now - pendingTapAt >= DOUBLE_TAP_MS) {
      pendingTapAt = 0;
      nextScreen();
    }
    return progress;
  }
};

// ------------------------------------------------------------------ sensor task
// Each tick takes whatever the remote IMU sent since the last one (batches of ~10
// every 40 ms); each sample is filtered with its own sample period.
static constexpr int MAX_BATCH = 16;

static PersistState saved;   // what is on the card now
static void sensorTask(void*) {
  PressTracker touchPress, btnPress;
  uint32_t lastUs = micros();
  uint32_t n = 0;
  float pkPitch = 0, pkRoll = 0;           // low-passed for peak capture
  uint32_t lastNavSeq = 0, lastNavMs = millis();
  bool alarm = false;
  uint32_t lastPrint = 0;
  TickType_t wake = xTaskGetTickCount();
  const uint32_t bootMs = millis();
  bool live = false;
  ImuSample batch[MAX_BATCH];
  Attitude a = tilt::attitude();

  for (;;) {
    vTaskDelayUntil(&wake, pdMS_TO_TICKS(4));   // 250 Hz
    uint32_t nowUs = micros();
    float dt = (nowUs - lastUs) * 1e-6f;
    lastUs = nowUs;
    if (dt > 0.05f) dt = 0.05f;

    // ---- remote IMU: back after a gap -> re-seed (it may have been moved or restarted)
    bool nowLive = remote::live();
    if (nowLive != live) {
      live = nowLive;
      if (live) tilt::reseed();
      Serial.printf("[imu] remote sensor %s\n", live ? "live" : "LOST");
    }
    float sdt;
    int nS = remote::take(batch, MAX_BATCH, sdt);

    for (int i = 0; i < nS; i++) {
      bool wasBusy = tilt::levelCalibrationBusy();
      tilt::update(batch[i], sdt);
      if (wasBusy && !tilt::levelCalibrationBusy()) {
        sh.leveling = false;
        sh.levelSet = true;
        resetPeaks();
        showToast("LEVEL SET");
      }
      if (tilt::seeding()) continue;   // attitude holds until the filter is seeded
      a = tilt::attitude();

      // peaks (filtered ~0.3 s so pot-holes don't register)
      float k = sdt / 0.3f;
      pkPitch += (a.pitch - pkPitch) * k;
      pkRoll += (a.roll - pkRoll) * k;
      if (millis() - bootMs > 4000 && !sh.leveling) {
        if (pkRoll < 0 && -pkRoll > sh.peakRollL) sh.peakRollL = -pkRoll;
        if (pkRoll > 0 && pkRoll > sh.peakRollR) sh.peakRollR = pkRoll;
        if (pkPitch > 0 && pkPitch > sh.peakPitchUp) sh.peakPitchUp = pkPitch;
        if (pkPitch < 0 && -pkPitch > sh.peakPitchDn) sh.peakPitchDn = -pkPitch;
      }
    }
    bool ok = live && !tilt::seeding();
    if (ok && !sh.sensorOk) { pkPitch = a.pitch; pkRoll = a.roll; }   // no ramp from stale values
    sh.sensorOk = ok;
    sh.pitch = a.pitch;
    sh.roll = a.roll;
    sh.adapt = a.adapt;

    // ~50 Hz: touch, button, buzzer
    if (++n % 5 == 0) {
      uint32_t now = millis();
      TouchPoint tp = touch::read();
      float hp = touchPress.update(tp.down, now, true);
      float hb = btnPress.update(digitalRead(PIN_BOOT_BTN) == LOW, now, false);
      sh.holdProgress = fmaxf(hp, hb);

      // phone app (settings screen): actions, and the status it shows
      switch (blelink::takeCommand()) {
        case CMD_SET_LEVEL: startLevel(); break;
        case CMD_RESET_PEAKS: resetPeaks(); showToast("PEAKS RESET"); break;
        case CMD_RESET_TRIP: resetTrip(); showToast("TRIP RESET"); break;
        default: break;
      }
      blelink::setStatus(storage::mounted(), sh.sensorOk, sh.levelSet, sh.leveling);

      // GPS speed / heading / trip
      NavFix f = blelink::latest();
      bool fresh = f.seq && now - f.rxMs < NAV_STALE_MS;
      float navDt = (now - lastNavMs) * 1e-3f;
      lastNavMs = now;
      obd::Data od = obd::latest();
      bool obdSpeed = od.state == obd::OBD_LIVE && obd::fresh(od.speedMs, NAV_STALE_MS);
      sh.obdNote = od.state == obd::OBD_CONNECTING ? "OBD CONNECTING"
                 : od.state == obd::OBD_INIT       ? "OBD STARTING"
                 : od.state == obd::OBD_NO_ECU     ? "OBD NO ANSWER"
                 : od.state == obd::OBD_LIVE && !obdSpeed ? "OBD NO SPEED" : nullptr;
      if (obdSpeed) sh.nav = NAV_OBD;
      else if (!blelink::connected()) sh.nav = NAV_NO_PHONE;
      else sh.nav = fresh && f.speedValid ? NAV_LIVE : NAV_NO_FIX;
      // the car's speed, corrected (speedometer / tyre size error, measured on the speed check screen)
      const float carKmh = od.speedKmh * (1.f + cfg.speedCorrPct * 0.01f);
      if (sh.nav == NAV_OBD) {
        // the car's own speed: no GPS jitter, works in tunnels
        float kmh = carKmh;
        sh.speedKmh = kmh;
        sh.tripKm += kmh * navDt / 3600.f;
        if (kmh > sh.maxKmh) sh.maxKmh = kmh;
      } else if (sh.nav == NAV_LIVE) {
        float kmh = f.speedMs * 3.6f;
        if (kmh < TRIP_MIN_KMH) kmh = 0;
        sh.speedKmh = kmh;
        sh.tripKm += kmh * navDt / 3600.f;
        if (f.seq != lastNavSeq && f.speedAccMs >= 0 && f.speedAccMs * 3.6f < MAX_SPEED_ACC_KMH && kmh > sh.maxKmh)
          sh.maxKmh = kmh;
      } else {
        sh.speedKmh = -1;
      }
      // speed comparison (car vs GPS)
      {
        bool gpsOk = blelink::connected() && fresh && f.speedValid;
        sh.obdKmh = obdSpeed ? carKmh : -1;
        sh.gpsKmh = gpsOk ? (f.speedMs * 3.6f < TRIP_MIN_KMH ? 0 : f.speedMs * 3.6f) : -1;
        bool accurate = f.speedAccMs >= 0 && f.speedAccMs * 3.6f < MAX_SPEED_ACC_KMH;
        if (obdSpeed && gpsOk && accurate && od.speedKmh >= CMP_MIN_KMH) {
          // distances, not instant ratios: GPS lags ~1 s, which averages out over a drive.
          // Uncorrected, so the suggested correction doesn't depend on the one set now.
          sh.cmpObdKm += od.speedKmh * navDt / 3600.f;
          sh.cmpGpsKm += f.speedMs * 3.6f * navDt / 3600.f;
        }
      }

      // engine screen: OBD values, fuel use from the mass airflow
      {
        const bool live = od.state == obd::OBD_LIVE;
        auto val = [&](float v, uint32_t ms, uint32_t maxAge) { return live && obd::fresh(ms, maxAge) ? v : -999.f; };
        sh.obdState = od.state;
        sh.rpm = val(od.rpm, od.rpmMs, 3000);
        sh.gear = live && obd::fresh(od.gearMs, 3000) && od.gear >= 1 && od.gear <= 9 ? od.gear : 0;
        sh.selector = live && obd::fresh(od.selectorMs, 3000) ? od.selector : 0;
        sh.coolantC = val(od.coolantC, od.coolantMs, 10000);
        sh.intakeC = val(od.intakeC, od.intakeMs, 10000);
        sh.loadPct = val(od.loadPct, od.loadMs, 10000);
        sh.volts = val(od.volts, od.voltsMs, 20000);
        static float lph = -1;   // smoothed fuel flow
        if (live && obd::fresh(od.mafMs, 3000)) {
          float x = od.rpm > 0 ? od.mafGs * 3600.f / (FUEL_AFR * FUEL_G_PER_L) : 0;
          lph = lph < 0 ? x : lph + (x - lph) * fminf(1.f, navDt / FUEL_SMOOTH_S);
          sh.fuelL += x * navDt / 3600.f;
          if (obdSpeed) sh.fuelKm += carKmh * navDt / 3600.f;
        } else {
          lph = -1;
        }
        sh.fuelLph = lph >= 0 ? lph : -999;
        static float kmhS = 0;   // smoothed like the fuel flow, so the ratio is steady
        kmhS = obdSpeed ? kmhS + (carKmh - kmhS) * fminf(1.f, navDt / FUEL_SMOOTH_S) : 0;
        sh.fuelL100 = lph >= 0 && obdSpeed && kmhS >= 5 ? lph / kmhS * 100.f : -999;
      }

      if (fresh && f.courseValid) sh.courseDeg = f.courseDeg;
      else if (!fresh) sh.courseDeg = -1;   // keep last heading while stopped, drop it when data stops
      lastNavSeq = f.seq;

      if (!sh.sensorOk) alarm = false;   // no alarm on stale angles
      if (cfg.buzzer) {
        float ar = fabsf(pkRoll), ap = fabsf(pkPitch);
        if (!alarm && sh.sensorOk && (ar >= cfg.rollDangerDeg || ap >= cfg.pitchDangerDeg)) alarm = true;
        if (alarm && ar < cfg.rollDangerDeg - 2 && ap < cfg.pitchDangerDeg - 2) alarm = false;
        bool beep = alarm && !sh.leveling && (now % 800) < 70;
        static bool beeping = false;
        if (beep != beeping) { board::buzzer(beep); beeping = beep; }
      }
    }

    // serial debug: 2 Hz
    if (millis() - lastPrint > 500) {
      lastPrint = millis();
      float gx, gy, gz;
      tilt::rawGravity(gx, gy, gz);
      if (sh.sensorOk)
        Serial.printf("pitch %6.1f  roll %6.1f  adapt %.2f  bump %d  acc[sensor] %+.2f %+.2f %+.2f  fps %.0f\n",
                      a.pitch, a.roll, a.adapt, a.bump, gx, gy, gz, sh.fps);
      else
        Serial.printf("no sensor (remote IMU %s)  fps %.0f\n", remote::connected() ? "connected, no data" : "not connected", sh.fps);
    }
    while (Serial.available()) {
      int c = Serial.read();
      if (c == 'd') sh.debug = !sh.debug;
      else if (c == 'p') sh.testPattern = !sh.testPattern;
      else if (c == 'f') sh.fbCheck = true;
      else if (c == 'l') startLevel();
      else if (c == 'r') resetPeaks();
      else if (c >= '0' && c < '0' + SCREEN_COUNT) sh.screen = c - '0';
      else if (c == 't') resetTrip();
      else if (c == 'i') {
        remote::Stats rs = remote::stats();
        Serial.printf("[remote] %s%s, %lu packets, %lu samples, %lu lost, %lu overflow\n",
                      remote::connected() ? "connected" : "not connected", sh.sensorOk ? " (live)" : "",
                      (unsigned long)rs.packets, (unsigned long)rs.samples, (unsigned long)rs.lost,
                      (unsigned long)rs.overflow);
      }
      else if (c == 'v')
        Serial.printf("[state] screen %u, trip %.2f km, max %.0f km/h, fuel %.2f L over %.2f km, compared %.2f km\n",
                      sh.screen, sh.tripKm, sh.maxKmh, sh.fuelL, sh.fuelKm, sh.cmpGpsKm);
      else if (c == 's') storage::listFiles();
      else if (c == 'o') storage::printFile("obd.txt", 32 * 1024);
      else if (c == 'b') {   // "b" = end of the probe log, "b<KB>" = that many KB from its end
        long kb = Serial.parseInt();
        storage::printFile("probe.txt", (kb > 0 ? kb : 32) * 1024);
      }
      else if (c == 'g') {   // newest drive log (the current one is still being written)
        char name[24];
        uint32_t no = saved.logNo;
        for (int k = 0; k < 2 && no > 0; k++, no--) {
          snprintf(name, sizeof name, "logs/%05lu.csv", (unsigned long)no);
          storage::printFile(name, 8 * 1024);
        }
      }
      else if (c == 'c') {   // "c<number>": that drive log
        char name[24];
        snprintf(name, sizeof name, "logs/%05ld.csv", Serial.parseInt());
        storage::printFile(name, 256 * 1024);
      }
      else if (c == 'm')
        Serial.printf("[mem] internal free %u KB (largest %u KB), DMA free %u KB (largest %u KB), PSRAM %u KB\n",
                      (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
                      (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024),
                      (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA) / 1024),
                      (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA) / 1024),
                      (unsigned)(ESP.getFreePsram() / 1024));
    }
  }
}

// ------------------------------------------------------------------ render task
static void stripCb(uint16_t* buf, int y0, int rows, void* ctx) {
  ui::renderStrip(buf, y0, rows, *(const UiState*)ctx);
}
static uint32_t hashCb(int y0, int rows, void* ctx) {
  return ui::stripHash(y0, rows, *(const UiState*)ctx);
}
// Whole-degree readout with hysteresis: the shown value only moves once the
// angle is clearly past the rounding boundary, so the number never flickers.
static float holdDeg(float shown, float v) {
  return fabsf(v - shown) > 0.5f + DISPLAY_HYST_DEG ? roundf(v) : shown;
}

// Write a new level reference to flash. The RGB panel must be stopped for that
// (see board::displayPause), so the screen blanks for a moment.
static void saveLevel() {
  uint32_t t0 = millis();
  board::setBacklight(0);
  board::displayPause();
  tilt::commitSave();
  bool ok = board::displayResume();
  Serial.printf("[level] saved to flash in %u ms%s\n", (unsigned)(millis() - t0), ok ? "" : " - display restart FAILED");
  if (!ok) { delay(100); esp_restart(); }   // should not happen; a reboot recovers the display
}

static void renderTask(void*) {
  bool backlightOff = true;    // setup() leaves it off after the boot badge
  uint8_t backlight = 0;       // level set now (follows cfg.backlight, changed in the phone app)
  UiState st;
  uint32_t last = micros();
  const uint32_t bootMs = millis();
  float fpsAcc = 0; int fpsN = 0;

  for (;;) {
    // without an SD card the level goes to internal flash (display must pause for that)
    if (!storage::mounted() && tilt::hasUnsavedLevel()) { saveLevel(); backlightOff = true; }
    uint32_t nowUs = micros();
    float dt = (nowUs - last) * 1e-6f;
    last = nowUs;
    // smoothing is done in the tilt filter; only the readout hysteresis here
    st.pitch = holdDeg(st.pitch, sh.pitch);
    st.roll = holdDeg(st.roll, sh.roll);
    st.peakRollL = sh.peakRollL; st.peakRollR = sh.peakRollR;
    st.peakPitchUp = sh.peakPitchUp; st.peakPitchDn = sh.peakPitchDn;
    st.screen = (Screen)sh.screen;
    st.holdProgress = sh.holdProgress;
    st.leveling = sh.leveling;
    st.levelSet = sh.levelSet;
    st.sensorOk = sh.sensorOk;
    st.toast = sh.toast;
    st.toastAge = (millis() - sh.toastAt) * 1e-3f;
    if (st.toast && st.toastAge > 3.f) sh.toast = nullptr;
    st.t = (millis() - bootMs) * 1e-3f;
    st.adapt = sh.adapt;
    st.fps = sh.fps;
    st.debug = sh.debug;
    st.testPattern = sh.testPattern;
    st.nav = sh.nav;
    st.obdNote = sh.obdNote;
    st.speedKmh = sh.speedKmh;
    st.maxKmh = sh.maxKmh;
    st.tripKm = sh.tripKm;
    st.obdState = sh.obdState;
    // rpm arrives a few times a second: ease towards it so the rev needle sweeps
    st.gear = sh.gear;
    st.selector = sh.selector;
    if (sh.rpm < -900 || st.rpm < -900) st.rpm = sh.rpm;
    else st.rpm += (sh.rpm - st.rpm) * fminf(1.f, dt / 0.15f);
    st.coolantC = sh.coolantC; st.volts = sh.volts;
    st.intakeC = sh.intakeC; st.loadPct = sh.loadPct;
    st.fuelLph = sh.fuelLph; st.fuelL100 = sh.fuelL100;
    st.avgL100 = sh.fuelKm >= 1.f ? sh.fuelL / sh.fuelKm * 100.f : -999;
    st.obdKmh = sh.obdKmh; st.gpsKmh = sh.gpsKmh;
    st.cmpKm = sh.cmpGpsKm;
    {
      const float corr = 1.f + cfg.speedCorrPct * 0.01f;
      const bool enough = sh.cmpGpsKm >= 0.5f && sh.cmpObdKm > 0.1f;
      st.cmpPct = enough ? (sh.cmpObdKm * corr / sh.cmpGpsKm - 1.f) * 100.f : -999;   // with the correction
      st.cmpSuggestPct = enough ? (sh.cmpGpsKm / sh.cmpObdKm - 1.f) * 100.f : -999;
      st.speedCorrPct = cfg.speedCorrPct;
    }
    // heading: ease along the shortest way round, so the compass ring turns smoothly
    if (sh.courseDeg < 0) st.courseDeg = -1;
    else if (st.courseDeg < 0) st.courseDeg = sh.courseDeg;
    else {
      float d = fmodf(sh.courseDeg - st.courseDeg + 540.f, 360.f) - 180.f;
      float c = st.courseDeg + d * fminf(1.f, dt / 0.25f);
      if (fabsf(d) < 0.05f) c = sh.courseDeg;
      st.courseDeg = fmodf(c + 360.f, 360.f);
    }

    ui::prepare(st);
    board::drawFrame(ui::background(st), stripCb, hashCb, &st);
    board::present();
    if (backlightOff) {   // back on once a complete frame is in the new buffers
      board::waitFrame();
      backlight = cfg.backlight;
      board::fadeBacklight(0, backlight, 400);
      backlightOff = false;
    } else if (cfg.backlight != backlight) {
      backlight = cfg.backlight;
      board::setBacklight(backlight);
    }

    if (sh.fbCheck) {   // frame-buffer readback: is every row really drawn?
      sh.fbCheck = false;
      board::waitFrame();
      float e, o;
      board::rowParityCheck(140, 310, 200, 370, e, o);
      Serial.printf("[fb] solid area   even rows %.0f  odd rows %.0f (0..255)\n", e, o);
      board::rowParityCheck(0, 0, 480, 480, e, o);
      Serial.printf("[fb] whole frame  even rows %.0f  odd rows %.0f\n", e, o);
    }

    fpsAcc += dt; fpsN++;
    if (fpsAcc > 1.f) { sh.fps = fpsN / fpsAcc; fpsAcc = 0; fpsN = 0; }
  }
}

// ------------------------------------------------------------------ SD card

static PersistState currentState() {
  PersistState p = saved;
  p.peakRollL = sh.peakRollL; p.peakRollR = sh.peakRollR;
  p.peakPitchUp = sh.peakPitchUp; p.peakPitchDn = sh.peakPitchDn;
  p.maxKmh = sh.maxKmh; p.tripKm = sh.tripKm;
  p.fuelL = sh.fuelL; p.fuelKm = sh.fuelKm;
  p.screen = sh.screen;
  return p;
}

static void loadFromSd() {
  if (!storage::begin()) return;
  float m[9];
  if (storage::loadLevel(m)) {
    tilt::setMount(m);
    sh.levelSet = true;
    Serial.println("[sd] level reference loaded");
  } else if (tilt::getMount(m)) {   // first run with a card: copy the one from flash
    Serial.printf("[sd] level reference copied from flash%s\n", storage::saveLevel(m) ? "" : " - FAILED");
  }
  if (storage::loadState(saved)) {
    sh.peakRollL = saved.peakRollL; sh.peakRollR = saved.peakRollR;
    sh.peakPitchUp = saved.peakPitchUp; sh.peakPitchDn = saved.peakPitchDn;
    sh.maxKmh = saved.maxKmh; sh.tripKm = saved.tripKm;
    if (saved.screen < SCREEN_COUNT) sh.screen = saved.screen;
    sh.fuelL = saved.fuelL; sh.fuelKm = saved.fuelKm;
    Serial.printf("[sd] state loaded: trip %.1f km, max %.0f km/h, screen %u\n", saved.tripKm, saved.maxKmh, saved.screen);
  }
  if (cfg.log) {
    saved.logNo++;
    storage::saveState(saved);
  }
}

static void fmtOpt(char* b, size_t n, bool ok, const char* fmt, float v) {
  if (ok) snprintf(b, n, fmt, v); else b[0] = 0;
}

// Low priority: level reference, peaks/trip, drive log. SD writes can take
// tens of ms, so none of this runs in the sensor or render task.
static void storageTask(void*) {
  // Drive log: opened here, and started / stopped when the phone app switches it.
  // loadFromSd() reserved a log number if logging was on at power-up.
  bool logOn = false, logNoFree = cfg.log;
  auto updateLog = [&] {
    if (cfg.log == logOn) return;
    logOn = cfg.log;
    if (!logOn) { storage::logClose(); return; }
    if (!logNoFree) { saved.logNo++; storage::saveState(saved); }
    logNoFree = false;
    storage::logOpen(saved.logNo,
                     "t_s,pitch,roll,speed_kmh,speed_src,course,rpm,coolant_c,intake_c,load_pct,throttle_pct,obd_volts,maf_gs,fuel_lph,gear,selector\n");
  };
  updateLog();
  {
    char b[48];
    int n = snprintf(b, sizeof b, "\n=== power-up, drive log %05lu ===\n", (unsigned long)saved.logNo);
    storage::appendText("obd.txt", b, n);
    if (cfg.obdProbe) storage::appendText("probe.txt", b, n, 32u << 20);
  }
  uint32_t lastSave = millis(), lastLog = 0, lastSync = millis(), lastObd = 0;
  uint32_t cfgRev = settings::revision(), cfgChangedAt = 0;
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(100));
    uint32_t now = millis();

    // settings changed in the phone app: save once they've been still for a second
    // (a brightness slider sends a stream of changes)
    uint32_t rev = settings::revision();
    if (rev != cfgRev) { cfgRev = rev; cfgChangedAt = now ? now : 1; }
    if (cfgChangedAt && now - cfgChangedAt >= 1000) {
      cfgChangedAt = 0;
      bool ok = storage::saveConfig();
      Serial.printf("[sd] config.txt saved%s\n", ok ? "" : " - FAILED");
      if (!ok) showToast("SD WRITE FAILED");
    }
    updateLog();

    float m[9];
    if (tilt::takePendingLevel(m)) {
      bool ok = storage::saveLevel(m);
      Serial.printf("[sd] level reference saved%s\n", ok ? "" : " - FAILED");
      if (!ok) showToast("SD WRITE FAILED");
    }

    PersistState cur = currentState();
    bool quick = cur.peakRollL != saved.peakRollL || cur.peakRollR != saved.peakRollR ||
                 cur.peakPitchUp != saved.peakPitchUp || cur.peakPitchDn != saved.peakPitchDn ||
                 cur.screen != saved.screen;   // saved within 2 s
    bool trip = cur.maxKmh != saved.maxKmh || cur.tripKm != saved.tripKm || cur.fuelL != saved.fuelL;
    if ((quick && now - lastSave >= 2000) || (trip && now - lastSave >= STATE_SAVE_S * 1000)) {
      if (storage::saveState(cur)) saved = cur;
      else Serial.println("[sd] state save FAILED");
      lastSave = now;
    }

    if (logOn && now - lastLog >= (uint32_t)(cfg.logIntervalS * 1000)) {
      lastLog = now;
      obd::Data o = obd::latest();
      bool live = o.state == obd::OBD_LIVE;
      char spd[8], crs[8], rpm[8], cool[8], intk[8], load[8], thr[8], volt[8], maf[8], fuel[8], gear[4], line[196];
      fmtOpt(spd, sizeof spd, sh.speedKmh >= 0, "%.0f", sh.speedKmh);
      fmtOpt(crs, sizeof crs, sh.courseDeg >= 0, "%.0f", sh.courseDeg);
      fmtOpt(rpm, sizeof rpm, live && obd::fresh(o.rpmMs), "%.0f", o.rpm);
      fmtOpt(cool, sizeof cool, live && obd::fresh(o.coolantMs, 10000), "%.0f", o.coolantC);
      fmtOpt(intk, sizeof intk, live && obd::fresh(o.intakeMs, 10000), "%.0f", o.intakeC);
      fmtOpt(load, sizeof load, live && obd::fresh(o.loadMs, 10000), "%.0f", o.loadPct);
      fmtOpt(thr, sizeof thr, live && obd::fresh(o.throttleMs, 10000), "%.0f", o.throttlePct);
      fmtOpt(volt, sizeof volt, live && obd::fresh(o.voltsMs, 20000), "%.1f", o.volts);
      fmtOpt(maf, sizeof maf, live && obd::fresh(o.mafMs), "%.2f", o.mafGs);
      fmtOpt(fuel, sizeof fuel, sh.fuelLph > -900, "%.2f", sh.fuelLph);
      fmtOpt(gear, sizeof gear, sh.gear > 0, "%.0f", sh.gear);
      const char sel[2] = {sh.selector, 0};
      const char* src = sh.nav == NAV_OBD ? "obd" : sh.nav == NAV_LIVE ? "gps" : "";
      char pit[8], rol[8];   // empty while there is no sensor
      fmtOpt(pit, sizeof pit, sh.sensorOk, "%.1f", sh.pitch);
      fmtOpt(rol, sizeof rol, sh.sensorOk, "%.1f", sh.roll);
      snprintf(line, sizeof line, "%.1f,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s\n", now * 1e-3f, pit, rol,
               spd, src, crs, rpm, cool, intk, load, thr, volt, maf, fuel, gear, sel);
      storage::logWrite(line);
    }
    if (now - lastSync >= LOG_SYNC_S * 1000) { lastSync = now; storage::logSync(); }
    if (now - lastObd >= 2000) {
      lastObd = now;
      static char* ob = (char*)heap_caps_malloc(4096, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
      size_t n = ob ? obd::takeLog(ob, 4096) : 0;
      if (n) storage::appendText("obd.txt", ob, n);
      n = ob ? obd::takeProbe(ob, 4096) : 0;
      if (n) storage::appendText("probe.txt", ob, n, 32u << 20);
    }
  }
}

// ------------------------------------------------------------------ setup
static void* psAlloc(size_t n) { return heap_caps_aligned_alloc(64, n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); }

static void showSplash(const char* l1, const char* l2) {
  UiState st;
  st.splash1 = l1; st.splash2 = l2;
  ui::prepare(st);
  board::drawFrame(ui::background(st), stripCb, nullptr, &st);
  board::present();
}

void setup() {
  Serial.begin(115200);
  delay(50);
  Serial.printf("\n[boot] inclinometer  PSRAM %u KB free\n", (unsigned)(ESP.getFreePsram() / 1024));
  pinMode(PIN_BOOT_BTN, INPUT_PULLUP);

  gfx.beginRecord();   // allocate the display list early so it lands in internal RAM
  gfx.endRecord();
  // All flash (NVS) access must happen before the RGB display starts.
  tilt::begin();
  sh.levelSet = tilt::isLevelCalibrated();
  board::begin();
  board::setBacklight(0);
  if (!board::reserveDisplayMemory()) Serial.println("[boot] strip buffer alloc FAILED");
  // BLE also before the display: the BT stack touches NVS (PHY calibration).
  if (!blelink::begin()) Serial.println("[boot] BLE init FAILED");
  Serial.printf("[mem] internal DMA free %u KB, largest block %u KB\n",
                (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA) / 1024),
                (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA) / 1024));
  if (!board::displayBegin()) Serial.println("[boot] display init FAILED");
  // SD card after the display (shares the panel's init SPI pins), before the UI (settings)
  loadFromSd();
  if (!ui::begin(psAlloc, board::flushCache)) Serial.println("[boot] UI buffer alloc FAILED (PSRAM?)");

  const uint32_t splashT0 = millis();
  // boot badge: fade in, hold (~2 s on screen in total), fade out; the render
  // task fades the first real frame in (backlight starts "off" there)
  constexpr uint32_t SPLASH_MS = 2000, FADE_IN_MS = 600, FADE_OUT_MS = 500;
  showSplash("INCLINOMETER", "starting...");
  board::waitFrame();
  board::fadeBacklight(0, cfg.backlight, FADE_IN_MS);

  touch::begin();

  while (millis() - splashT0 < SPLASH_MS - FADE_OUT_MS) delay(10);
  board::fadeBacklight(cfg.backlight, 0, FADE_OUT_MS);

  if (!sh.levelSet) showToast("HOLD 2 s TO SET LEVEL");
  else if (!storage::mounted()) showToast("NO SD CARD");

  xTaskCreatePinnedToCore(sensorTask, "sensors", 6144, nullptr, 5, nullptr, 1);
  xTaskCreatePinnedToCore(renderTask, "render", 8192, nullptr, 3, nullptr, 0);
  // Internal RAM is nearly all taken by the display (strips, bounce buffers, display list)
  // and the BT controller. From here on, ordinary mallocs above 256 B (NimBLE client, FILE
  // buffers, std::string...) go to PSRAM; DMA buffers ask for internal memory explicitly.
  heap_caps_malloc_extmem_enable(256);
  obd::begin();   // BLE central: OBD adapter (no flash access)
  // stack in PSRAM: this task never runs while flash is written (it only exists with a card)
  if (storage::mounted())
    xTaskCreatePinnedToCoreWithCaps(storageTask, "sd", 6144, nullptr, 1, nullptr, 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

void loop() { vTaskDelay(portMAX_DELAY); }
