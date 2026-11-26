---
name: toyota-selector-decode
description: "4Runner lever/brake bits found by live probe 2026-11-26 — 21D9 C/D give R,D,4,3,2,L; P and N are indistinguishable; 21E6 A 0x04 is the brake"
metadata:
  node_type: memory
  type: project
  modified: 2026-11-26T07:09:30.923Z
---

Live serial probe on 2026-11-26 (lever P R N D 4 3 2 L, engine idling, parked), engine ECU 7E0:

- **21D9** (multi-frame, also holds the trans temp in frame 1) bytes C,D: P/N 00 00 · R 04 00 · D 00 04 · 4 00 06 · 3 00 01 · 2 02 00 · L 03 00.
- **21E6 byte A**: 0x10 = P or N (as before); **0x04 = brake pedal** (confirmed with brake presses in P and N); 0x20 toggles by itself (not lever). 21D3 byte D 00/07 follows the brake too.
- **P vs N: no bit found** in any of the 84 answering blocks — this ECU apparently only gets the neutral-start switch. Show "P/N" or "N" for both.
- 21D3 byte C 0x80 is set in D *and* 4, clear in 3/2/L (so the old decode showed 3/2/L as R).

**Why:** the old decode only knew P/N, R and D. **How to apply:** decode the selector from 21D9 when changing the gear display; don't spend time looking for a P bit again unless with a new idea (e.g. other ECU headers).
