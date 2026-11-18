# CLAUDE.md

Car inclinometer firmware (ESP32-S3, PlatformIO), remote IMU firmware (`remote/`, XIAO ESP32-C6, own
PlatformIO project; protocol in `shared/imu_link.h`), iOS GPS link app (`ios/`), enclosure CAD (`enclosure/`).
See README.md for build, usage and architecture.

## Git — always use it

Follow the "Development — use git" section in README.md:

- Start by checking `git status` and `git log --oneline`.
- Commit a working state before risky changes; commit each finished change separately, message says *why*.
- No ad-hoc backup copies — revert with git (`git restore`, `git revert`).
- Commit only when the user asks; end commit messages with the attribution trailer from the session.

## Device

- Build: `~/.platformio/penv/bin/pio run`. Upload (`-t upload`) only when the user says the device is connected.
- Filter tuning lives in `src/config.h` (`TILT_*`); the filter is `src/tilt.cpp`.
