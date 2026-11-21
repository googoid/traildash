# Car Inclinometer — Waveshare ESP32-S3-Touch-LCD-2.8C

Round 480×480 off-road inclinometer: tilting vehicle silhouettes,
peak-angle memory, with amber/red warning zones and a buzzer alarm,
plus a GPS speed / heading / trip screen fed by an iPhone app over Bluetooth LE.

## Build & flash

```sh
~/.platformio/penv/bin/pio run -t upload
```

## Development — use git

The project is under git (branch `main`). Work like this:

- Check `git status` / `git log --oneline` before starting, so you know the current state.
- Commit a working state before any risky change (filter rewrites, display/flash code), and
  commit each finished change separately with a message that says *why*.
- Don't make ad-hoc backup copies of files — revert with git instead
  (`git diff`, `git restore <file>`, `git revert <commit>`).
- Generated files are ignored (`.pio`, preview `.bmp`/binary, Xcode `xcuserdata`); the preview
  PNGs, enclosure STEP/STL and renders are committed — regenerate and commit them with the change
  that affects them.

## Use

| Action | Effect |
|---|---|
| Tap | Next screen: speed → engine → inclinometer → peaks → speed check. The screen shown is remembered across power-ups (SD card). |
| Hold 2 s | **Set level** — park on flat ground first. The screen blinks off briefly while it is saved. |
| Hold 2 s on the speed or engine screen | Reset max speed, trip distance and average fuel use |
| Hold 2 s on the speed check screen | Restart the car-vs-GPS average |
| Double-tap | Reset peak angles |
| BOOT button | Short = next screen, long = same as hold |

Mount the display wherever you like; the angles come from the remote IMU (below). Mount that
rigidly, then **set level once** on flat ground with it connected.

## GPS speed — iPhone app (`ios/`)

Open `ios/InclinoLink.xcodeproj` in Xcode, pick your team under *Signing & Capabilities*
(change the bundle ID if Xcode asks), and run it on the iPhone. Allow Location and Bluetooth.

- The app finds the inclinometer (advertising as `Inclinometer`), remembers it, and from then on
  iOS reconnects automatically whenever the car powers up — also with the phone locked.
- It sends speed + course about once per second; the device shows `--` after 3 s without data.
- Max speed and trip distance are computed on the device and kept on the SD card (without a card
  they reset at power-up). Distance is speed × time, so it isn't counted while there is no speed source.
- With an OBD-II BLE adapter (ELM327, e.g. Vgate iOS-Vlink) in the car, speed comes from the car
  instead (status shows **OBD**); see `src/obd.cpp`.
- The **speed screen** looks like a car cluster: a rev needle (OBD rpm; it rests on 0 without OBD,
  red zone from `redline`), the direction of travel next to a lamp (green = speed source live, amber =
  OBD adapter not delivering yet, grey = no speed), the speed, and trip / max / source below it.
- The **engine screen** (OBD only) shows a rev gauge (red zone: `redline` in config.txt), coolant
  (blue = cold, amber from 105 °C, red from 110 °C), adapter voltage, fuel use now (L/100 km, or L/h
  when standing) and the average since the trip reset, plus intake temperature and engine load.
  Fuel use is calculated from the mass airflow sensor (air ÷ 14.7): right at cruise, low under full
  throttle. The adapter reads ~0.3 V below the battery, so 12.8–13.0 V at idle is normal.
- The **speed check** screen shows the car's (OBD) and the phone's GPS speed side by side, the
  difference now, and the average difference over the distance driven with both above 20 km/h
  (GPS lags about a second, which averages out). That is the speedometer / tyre-size error.
- GPS runs only while the inclinometer is connected. Set Location to **Always** so GPS can start
  when the device connects while the phone is locked.
- iOS doesn't reopen a swiped-away app for Bluetooth. The in-app option *Restart after swipe-away*
  covers that with low-power location monitoring (the location arrow then stays on).
- With a free Apple ID the app must be re-installed from Xcode every 7 days.

### Device settings in the app

*InclinoLink → Device Settings* (while the inclinometer is connected) changes the `config.txt`
settings live: warning/danger angles, alarm sound, brightness (the screen follows the slider), rev
gauge red zone, drive log on/off + interval, remote IMU forward axis, OBD adapter name and probe.
Changes apply at once and are written to `config.txt` a second after the last one (without an SD
card they last until power-off). It also has **Set level**, **Reset peaks** and **Reset trip**.
Only the OBD probe needs a restart; a new forward axis needs *Set level* again.
Debug builds started with the launch argument `-demoDevice` show the screen with a pretend device.

Protocol (service/characteristic UUIDs, 10-byte packet, settings text, action bytes) is documented in `src/ble.h`.

## Remote IMU (`remote/`) — the tilt sensor

Pitch and roll come from a separate sensor — Seeed **XIAO ESP32-C6** + **MPU-6050/6500** (GY-521
style board) — mounted somewhere solid in the car (e.g. bolted to the floor/tunnel), streaming to the
display over Bluetooth LE. The display's own IMU is not used, so the display can sit anywhere.

Wiring: XIAO `3V3`→VCC, `GND`→GND, `D4` (GPIO22)→SDA, `D5` (GPIO23)→SCL.

```sh
cd remote && ~/.platformio/penv/bin/pio run -t upload
```

- Mount it with the board's **X arrow pointing forward** (or set `remote_forward` in `config.txt`,
  e.g. `-y`), any tilt is fine. Power it up with the car standing still: it measures its gyro bias in the
  first 2 s (and remembers it for power-ups while driving).
- It finds the inclinometer on its own (LED: fast blink = searching, short flash every 3 s = connected).
- Without data for 0.5 s the gauge shows **NO SENSOR** (no angles, no alarm, peaks paused); when the
  sensor is back the filter re-seeds from it in ~0.1 s.
- Raw samples (250 Hz, sent 10 per packet) go through the tilt filter on the display.
  Protocol: `shared/imu_link.h`. Serial `i` on the display prints link statistics.

## Settings and SD card

`src/config.h` holds the defaults (and filter tuning). With a microSD card (FAT32) in the display,
the user settings are in `/incl/config.txt` — written with the defaults on first start; edit it on a
computer and restart. The card also stores:

| File | Content |
|---|---|
| `/incl/config.txt` | warning/danger angles, buzzer, backlight, drive log on/off + interval, OBD adapter name, remote IMU forward axis, rev gauge red zone, OBD probe on/off |
| `/incl/rlevel_a.txt`, `rlevel_b.txt` | level reference (of the remote IMU) |
| `/incl/state_a.txt`, `state_b.txt` | peak angles, max speed, trip, fuel used for the average, drive log counter |
| `/incl/logs/NNNNN.csv` | drive log, one file per power-up: angles, speed + source, heading, OBD values |
| `/incl/probe.txt` | OBD probe (`obd_probe = 1`, Toyota only): the gear / selector data blocks (21DA, 21E6, 21D3), each reply logged when it changes, with speed / rpm / throttle / gear / selector |

Records are written alternately to the `_a`/`_b` file with a sequence number and checksum, so a power
cut during a write keeps the previous version. The drive log reaches the card every 10 s.
Without a card the level reference falls back to internal flash (screen blinks while saving) and
nothing else is kept. A level reference already in flash is copied to a new card automatically.

Without a card reader, over USB serial: `s` lists the files, `o` prints the end of `obd.txt`,
`b` the end of `probe.txt` (`b500` = its last 500 KB),
`c21` prints drive log 21, `v` prints the current screen, trip, fuel and comparison totals.

## How it works

- `tilt.cpp` — complementary tilt filter. The gyro turns the reading instantly when the car really
  tilts; the heavily smoothed accelerometer keeps it true, so gyro errors fade instead of building up
  (no trust gating, no integral term, no bias learning while driving). Median filter against spikes
  and bump rejection. Accelerating/braking briefly shows as pitch — by design. `TILT_GYRO_CARRY 0`
  in `config.h` switches to accelerometer-only angles. Readout is whole degrees with hysteresis.
- `ble.cpp` — NimBLE peripheral; the phone writes GPS packets to it. Initialised before the display
  (the BT stack touches NVS flash), no bonding, so nothing is written to flash afterwards.
- `gfx.cpp` — small antialiased vector renderer (polygons, arcs, gradients, dithering, smooth fonts).
- `board.cpp` — ST7701 RGB panel with two PSRAM frame buffers. Frames are drawn in 16-row strips in
  internal SRAM and moved to/from PSRAM by GDMA (the CPU path to PSRAM is too slow); strips whose
  content didn't change are skipped.
- Flash is never accessed while the display runs (the panel interrupt reads PSRAM, which is
  unavailable during flash operations) — without an SD card the panel is stopped for a moment when the
  level reference is saved. The SD card (`storage.cpp`, 1-bit SDMMC on the panel's init-SPI pins, which
  are released after display init) has no such restriction; it is written from a low-priority task.

## UI preview on a PC

```sh
tools/preview/build.sh     # renders all screens to preview/*.png
```

Fonts: Roboto / Roboto Condensed (Apache 2.0), regenerated with `swift tools/gen_fonts.swift > src/fonts_data.h`.
