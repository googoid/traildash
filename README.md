# Car Inclinometer — Waveshare ESP32-S3-Touch-LCD-2.8C

Round 480×480 off-road inclinometer: tilting vehicle silhouettes,
bubble level and peak-angle memory, with amber/red warning zones and a buzzer alarm,
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
| Tap | Next screen: dual gauge → GPS speed → bubble → peaks |
| Hold 2 s | **Set level** — park on flat ground first. The screen blinks off briefly while it is saved. |
| Hold 2 s on the speed screen | Reset max speed and trip distance |
| Double-tap | Reset peak angles |
| BOOT button | Short = next screen, long = same as hold |

Mount it anywhere, at any tilt (upright on the dash facing you, or flat on the console with the
top edge pointing forward), then **set level once** on flat ground.

## GPS speed — iPhone app (`ios/`)

Open `ios/InclinoLink.xcodeproj` in Xcode, pick your team under *Signing & Capabilities*
(change the bundle ID if Xcode asks), and run it on the iPhone. Allow Location and Bluetooth.

- The app finds the inclinometer (advertising as `Inclinometer`), remembers it, and from then on
  iOS reconnects automatically whenever the car powers up — also with the phone locked.
- It sends speed + course about once per second; the device shows `--` after 3 s without data.
- Max speed and trip distance are computed on the device (RAM only — they reset at power-up).
  Distance is speed × time, so it isn't counted while the phone is disconnected.
- GPS runs only while the inclinometer is connected. Set Location to **Always** so GPS can start
  when the device connects while the phone is locked.
- iOS doesn't reopen a swiped-away app for Bluetooth. The in-app option *Restart after swipe-away*
  covers that with low-power location monitoring (the location arrow then stays on).
- With a free Apple ID the app must be re-installed from Xcode every 7 days.

Protocol (service/characteristic UUIDs, 10-byte packet) is documented in `src/ble.h`.

## Settings — `src/config.h`

Warning/danger thresholds, buzzer on/off, backlight, filter tuning.

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
  unavailable during flash operations) — the panel is stopped for a moment when the level reference is saved.

## UI preview on a PC

```sh
tools/preview/build.sh     # renders all screens to preview/*.png
```

Fonts: Roboto / Roboto Condensed (Apache 2.0), regenerated with `swift tools/gen_fonts.swift > src/fonts_data.h`.
