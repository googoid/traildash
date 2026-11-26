---
name: read-logs-from-sd
description: "For big logs (probe.txt, drive logs) ask the user to put the microSD card in the Mac instead of dumping over serial"
metadata:
  node_type: memory
  type: feedback
  modified: 2026-11-19T08:07:08.083Z
---

To read large SD files (probe.txt, many drive logs), ask the user to plug the microSD card into the Mac and read `/Volumes/<card>/incl/` directly. Serial dumps (`b`, `c<n>`, `o`) are only for small files.

**Why:** the user stopped a serial dump of probe.txt because it took too long (115200 baud ≈ 11 KB/s, so megabytes take minutes).
**How to apply:** check file sizes first (serial `s` is quick, or ask); if more than ~100 KB, suggest the card reader. Related: [[vehicle-4runner]].
