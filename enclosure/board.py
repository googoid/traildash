"""
Shared board model and outside dimensions for the TrailDash enclosure
(Waveshare ESP32-S3-Touch-LCD-2.8C, round 2.8", 95.86 mm glass).
Used by enclosure_open.py (the enclosure) and cluster_insert.py (the dash insert
it snaps into); not a part by itself.

Screw holes: Waveshare's STEP model has the board ~2 mm too high in the glass
(the first print's holes missed). STANDOFFS were measured on a photo of the
real board, back up on a 10 mm cutting mat (15.4 px/mm, grid uniform to <1 %,
so no perspective error), relative to the glass edge (fitted circle 95.9 mm).
The other board features below are STEP values; they are moved by BOARD_SHIFT,
the mean offset of the measured holes from the STEP's.

Coordinates (same as the Waveshare STEP after centring):
  x, y : centre of the glass, viewed from the BACK (USB ports at lower-left/right)
  d    : depth from the glass front face, positive toward the back
"""
import math
import cadquery as cq

# ---------------- board (from Waveshare STEP 2024-12-26) ----------------
GLASS_R      = 95.86 / 2
GLASS_T      = 1.26          # cover glass + touch, front at d=0
LCD_R, LCD_CY = 36.54, 1.77  # round LCD body behind the glass
STANDOFF_TOP = 9.71          # back face of the board's M2 standoffs
STANDOFFS    = [(-29.0, 12.95), (29.9, 12.95), (-17.6, -27.7), (18.5, -27.7)]  # measured
BOARD_SHIFT  = (0.45, -2.15)  # real board vs. STEP (STEP standoffs: (+-29.5, 15.14), (+-18, -25.56))
SX, SY       = BOARD_SHIFT
USB_MOUTH_D  = 6.88          # USB-C centre depth
USB_MOUTH    = [(-26.33, -23.68, -1), (26.35, -23.71, +1)]  # mouth x, y, side
BTN_D        = 6.41
BTN_Y        = {"BOOT": 9.6, "RST": 1.0}
SW_Y, SW_D   = (-11.0, -4.7), 6.71   # battery switch slot

# ---------------- enclosure outside (the cluster insert fits this) ----------------
FIT          = 0.20          # radial clearance around the glass
WALL         = 2.2           # side wall outside the glass
LIP          = 0.5           # rim stands this far proud of the glass
BACK_SKIN    = 3.0           # BACK = CAV_BACK + BACK_SKIN: the back face's depth
CAV_BACK     = 13.6
SCREW_D, SCREW_HEAD_D, SCREW_HEAD_H = 2.6, 4.6, 4.4   # 2.6: +-0.3 mm for photo error
USB_W, USB_H = 13.5, 8.0     # plug overmold opening
PIN_D        = 2.0           # paper-clip holes for BOOT / RST
# pockets in the back edge for the cluster insert's snap catches (cluster_insert.py):
# the catches sit in them flush with the back, and they key the enclosure's rotation
SNAP_ANG     = [90, -63, -117]   # deg, about the display centre (as seen from the back)
SNAP_W       = 11.0              # hook arm width (mm along the wall)
SNAP_CATCH_H = 1.8               # catch height along the depth
SNAP_CATCH_IN = 2.0              # catch reaches this far in from the bore
SNAP_GAP     = 0.5               # pocket clearance around the catch (sides, inside)
SNAP_DEPTH   = SNAP_CATCH_H + 0.15   # pocket depth from the back face

POCKET_R = GLASS_R + FIT
OUT_R    = POCKET_R + WALL
FRONT    = -LIP
BACK     = CAV_BACK + BACK_SKIN
DEPTH    = BACK - FRONT


def sector(r0, r1, a0, a1, d0, d1):
    """Annular sector solid between angles a0..a1 (deg), depths d0..d1."""
    n = 12
    pts = [(r * math.cos(math.radians(a0 + (a1 - a0) * i / n)),
            r * math.sin(math.radians(a0 + (a1 - a0) * i / n)))
           for r, rng in ((r1, range(n + 1)), (r0, range(n, -1, -1))) for i in rng]
    return cq.Workplane("XY").workplane(offset=d0).polyline(pts).close().extrude(d1 - d0)


SNAP_POCKET_R = OUT_R - SNAP_CATCH_IN + 0.2 - SNAP_GAP   # 0.2 = insert bore clearance


def snap_pocket(a):
    """Pocket in the back edge for the insert's snap catch at angle a (deg)."""
    half = math.degrees((SNAP_W / 2 + SNAP_GAP) / OUT_R)
    return sector(SNAP_POCKET_R, OUT_R + 2, a - half, a + half, BACK - SNAP_DEPTH, BACK + 1)


def flex_notch(depth):
    """Room for the LCD flex where it wraps around the bottom edge of the LCD,
    behind the glass, kept inside the glass pocket radius."""
    n = (cq.Workplane("XY").workplane(offset=GLASS_T)
         .center(SX, -32.5 + SY).rect(2 * 27.5, 14).extrude(depth)
         .union(cq.Workplane("XY").workplane(offset=GLASS_T)
                .center(SX, -41.5 + SY).rect(2 * 16.0, 7).extrude(depth)))
    return n.intersect(cq.Workplane("XY").workplane(offset=GLASS_T)
                       .circle(POCKET_R - 0.6).extrude(depth))


def load_board(step_path):
    """Waveshare model centred on the glass, board parts moved by BOARD_SHIFT
    (the glass stays put)."""
    CX, CY, Z0 = 18.0, 25.5642, -5.7105
    out = []
    for s in cq.importers.importStep(step_path).solids().vals():
        s = s.translate(cq.Vector(-CX, -CY, -Z0))
        b = s.BoundingBox()
        if not (b.xlen > 90 and b.zmax < GLASS_T + 0.05):
            s = s.translate(cq.Vector(SX, SY, 0))
        out.append(s)
    return out


def check_fit(body, parts):
    """Intersect the shell with every solid of the (shifted) Waveshare model."""
    shell = body.val()
    worst = 0.0
    for s in parts:
        v = shell.intersect(s).Volume()
        if v > 1e-3:
            b = s.BoundingBox()
            worst = max(worst, v)
            print(f"  COLLISION {v:.3f} mm3 with part at "
                  f"({b.center.x:.1f},{b.center.y:.1f}) d{b.zmin:.2f}..{b.zmax:.2f}")
    print("fit check:", "OK, no collisions" if worst == 0 else "collisions found")
    return worst == 0
