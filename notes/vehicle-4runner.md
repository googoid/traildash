---
name: vehicle-4runner
description: "The car is a 2005 Toyota 4Runner V8 Sport, automatic; OBD on CAN 11/500, mode-01 PIDs up to 0x40"
metadata:
  node_type: memory
  type: project
  modified: 2026-11-19T05:19:58.260Z
---

The car this device runs in: **2005 Toyota 4Runner V8 Sport (4.7 L 2UZ-FE), automatic** (A750E 5-speed), shift lever P R N D 4 3 2 L (user-confirmed 2026-11-26; online A750E manual pages showing P R N D S +/- are for later 4Runners).

OBD (from the SD card's obd.txt, 2026-11-19): ELM327 v2.3 BLE adapter, protocol 6 = ISO 15765-4 CAN 11-bit/500k.
Supported mode-01 PIDs: 01 03 04 05 06 07 08 09 0C 0D 0E 0F 10 11 12 13 15 19 1C 1F 20 21 24 28 2E 30 31 33 3C 3D 3E 3F 40
(0x40 set = more above 0x40 not yet queried by the firmware).

**Why:** decides which data is realistically available — e.g. standard gear PID 0xA4 is not expected on a 2005; gear/selector/ATF temp would need Toyota-specific mode 21/22 requests or an rpm/speed estimate.
**How to apply:** answer OBD feature questions for this car, not generically; verify Toyota-specific PIDs on the car (log + probe) rather than trusting lists.
