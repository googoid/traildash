---
name: panel-odd-rows-damage
description: "Current inclinometer LCD shows nothing on odd rows (looks striped/pixelized) — cracked glass broke one side's gate driver; hardware, not firmware"
metadata:
  node_type: memory
  type: project
  modified: 2026-11-19T07:07:36.514Z
---

The user's Waveshare 2.8C panel does not drive odd pixel rows at all (dark stripes everywhere, text looks pixelized). Proven 2026-11-19 with the test pattern (serial `p`): the odd-row line block and the bottom edge ruler (odd rows) are invisible, while the even-row ones show. The frame-buffer readback (serial `f`) shows correct data. VCOM, C2 inversion, PCLK 14–20 MHz and HBP sweeps change nothing. The image is not shifted; only about 10 left columns are cut.

Cause: the glass is cracked at the right edge. The panel has dual-side interlaced gate-in-panel circuitry (init E5 = odd CLKs, E8 = even CLKs), so the break kills one row parity across the whole screen. The user said "ignore the damage" at first because the stripes are also in undamaged areas; this explains why they are there too.

**Why:** Several sessions went into firmware (BLE, strips, timing, VCOM) before this was pinned down.
**How to apply:** For display-quality complaints, ask whether the panel has been replaced. If not, it's this. On a new panel, verify with `p` (rulers on all 4 edges, EVEN/ODD blocks).
