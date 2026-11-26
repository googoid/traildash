---
name: show-cad-renders
description: "When generating/changing 3D-printable CAD models, open the render images for the user every time"
metadata:
  node_type: memory
  type: feedback
  modified: 2026-11-09T16:55:03.149Z
---

After every CAD model build or change (enclosure, mount, etc.), render preview images and open them for the user (`open file.png` on macOS) — not just inspect them myself.

**Why:** User asked "show me the renders each time" while I was designing the display enclosure (2026-11-09).
**How to apply:** Each iteration of a printable part → regenerate renders (3D views + 2D cross-sections with the board model) and `open` them; keep copies in enclosure/renders/.
