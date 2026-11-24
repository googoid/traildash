"""
Enclosure for the Waveshare ESP32-S3-Touch-LCD-2.8C (round 2.8", 95.86 mm glass).

One-piece shell: the display drops in face-up, the glass sits in a pocket on a
ledge, and 4x M2x6 screws from the back go into the board's own M2 standoffs.

Screw holes: Waveshare's STEP model has the board ~2 mm too high in the glass
(the first print's holes missed). STANDOFFS were measured on a photo of the
real board, back up on a 10 mm cutting mat (15.4 px/mm, grid uniform to <1 %,
so no perspective error), relative to the glass edge (fitted circle 95.9 mm).
The other board features below are STEP values; they are moved by BOARD_SHIFT,
the mean offset of the measured holes from the STEP's.
The back has a captive 1/4"-20 nut (standard camera thread) for car mounts.

Coordinates (same as the Waveshare STEP after centring):
  x, y : centre of the glass, viewed from the BACK (USB ports at lower-left/right)
  d    : depth from the glass front face, positive toward the back

Run:  python enclosure.py [path/to/ESP32-S3-Touch-LCD-2.8C.step]
      -> writes enclosure.stl / enclosure.step (and checks fit if STEP given)
"""
import math, sys, os
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

# ---------------- enclosure parameters ----------------
FIT          = 0.20          # radial clearance around the glass
WALL         = 2.2           # side wall outside the glass
LIP          = 0.5           # rim stands this far proud of the glass
CAV_R        = 39.5          # open cavity behind glass (clears LCD + parts)
BACK_SKIN    = 3.0           # thickness of the back plate
CAV_BACK     = 13.6          # inner face of the back plate (depth)
SCREW_D, SCREW_HEAD_D, SCREW_HEAD_H = 2.6, 4.6, 4.4   # 2.6: +-0.3 mm for photo error
NUT_AF, NUT_H, NUT_HOLE = 11.30, 5.80, 6.8   # 1/4"-20 hex nut (7/16" AF)
NUT_FLOOR    = 2.0
USB_W, USB_H = 13.5, 8.0     # plug overmold opening
PIN_D        = 2.0           # paper-clip holes for BOOT / RST
PIN_X, DIMPLE_D, DIMPLE_H = 13.0, 4.5, 2.2   # mount locating-pin dimples (back)
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


def ring_body():
    return (cq.Workplane("XY").workplane(offset=FRONT)
            .circle(OUT_R).extrude(DEPTH)
            .faces(">Z").edges().fillet(1.5)     # back edge
            .faces("<Z").edges().chamfer(0.6))   # front edge


def flex_notch(depth):
    """Room for the LCD flex where it wraps around the bottom edge of the LCD,
    behind the glass, kept inside the glass pocket radius."""
    n = (cq.Workplane("XY").workplane(offset=GLASS_T)
         .center(SX, -32.5 + SY).rect(2 * 27.5, 14).extrude(depth)
         .union(cq.Workplane("XY").workplane(offset=GLASS_T)
                .center(SX, -41.5 + SY).rect(2 * 16.0, 7).extrude(depth)))
    return n.intersect(cq.Workplane("XY").workplane(offset=GLASS_T)
                       .circle(POCKET_R - 0.6).extrude(depth))


def build():
    body = ring_body()

    # glass pocket (front, open)
    body = body.cut(cq.Workplane("XY").workplane(offset=FRONT - 1)
                    .circle(POCKET_R).extrude(1 + LIP + GLASS_T))
    # small chamfer at pocket mouth so the glass drops in easily
    body = body.cut(cq.Workplane("XY").workplane(offset=FRONT - 0.01)
                    .circle(POCKET_R + 0.4).workplane(offset=0.41)
                    .circle(POCKET_R).loft())

    # main cavity behind the glass
    cav = (cq.Workplane("XY").workplane(offset=GLASS_T)
           .circle(CAV_R).extrude(CAV_BACK - GLASS_T))
    # notch for the LCD flex tail (wraps down to y=-43.6 in the STEP)
    cav = cav.union(flex_notch(CAV_BACK - GLASS_T))
    body = body.cut(cav)

    # screw bosses from the back plate down to the standoffs
    for x, y in STANDOFFS:
        boss = (cq.Workplane("XY").workplane(offset=STANDOFF_TOP + 0.10)
                .center(x, y).circle(3.4).extrude(CAV_BACK - STANDOFF_TOP))
        body = body.union(boss)
        body = body.cut(cq.Workplane("XY").workplane(offset=STANDOFF_TOP)
                        .center(x, y).circle(SCREW_D / 2).extrude(DEPTH))
        body = body.cut(cq.Workplane("XY").workplane(offset=BACK - SCREW_HEAD_H)
                        .center(x, y).circle(SCREW_HEAD_D / 2).extrude(5))

    # 1/4"-20 captive nut boss in the middle of the back
    nut_face = BACK - NUT_FLOOR - NUT_H
    body = body.union(cq.Workplane("XY").workplane(offset=nut_face)
                      .circle(NUT_AF / math.sqrt(3) + 2.2).extrude(CAV_BACK - nut_face + 0.01))
    body = body.cut(cq.Workplane("XY").workplane(offset=nut_face - 1)
                    .polygon(6, 2 * NUT_AF / math.sqrt(3) + 0.1).extrude(NUT_H + 1))
    body = body.cut(cq.Workplane("XY").workplane(offset=nut_face)
                    .circle(NUT_HOLE / 2).extrude(DEPTH))

    # USB-C openings, 45 deg toward lower-left / lower-right
    for mx, my, side in USB_MOUTH:
        mx, my = mx + SX, my + SY
        ang = math.degrees(math.atan2(my, mx))
        r0 = math.hypot(mx, my) - 2
        length = OUT_R - r0 + 3
        c = (cq.Workplane("XY")
             .box(length, USB_W, USB_H, centered=(False, True, True))
             .edges("|X").fillet(2.0)
             .translate((r0, 0, USB_MOUTH_D))
             .rotate((0, 0, 0), (0, 0, 1), ang))
        # keep the opening below the glass (roof = glass back face)
        c = c.intersect(cq.Workplane("XY").workplane(offset=GLASS_T + 0.05)
                        .circle(OUT_R + 10).extrude(30))
        body = body.cut(c)

    # BOOT / RST paper-clip holes and battery-switch slot (left edge)
    for y in BTN_Y.values():
        body = body.cut(cq.Workplane("YZ").workplane(offset=-OUT_R - 2)
                        .center(y + SY, BTN_D).circle(PIN_D / 2).extrude(OUT_R - CAV_R + 4))
    body = body.cut(cq.Workplane("YZ").workplane(offset=-OUT_R - 2)
                    .center(sum(SW_Y) / 2 + SY, SW_D).slot2D(SW_Y[1] - SW_Y[0], 2.2)
                    .extrude(OUT_R - CAV_R + 4))

    # vent / buzzer slots in the back, lower-right
    # (top-centre, above the dash mount's contact pad)
    for i in range(5):
        body = body.cut(cq.Workplane("XY").workplane(offset=CAV_BACK - 1)
                        .center(i * 3.2 - 6.4, 30).slot2D(10, 1.6, 90)
                        .extrude(BACK_SKIN + 2))

    # snap-catch pockets in the back edge (cluster insert)
    for a in SNAP_ANG:
        body = body.cut(snap_pocket(a))

    # anti-rotation dimples for the dash mount's locating pins
    for x in (-PIN_X, PIN_X):
        body = body.cut(cq.Workplane("XY").workplane(offset=BACK - DIMPLE_H)
                        .center(x, 0).circle(DIMPLE_D / 2).extrude(DIMPLE_H + 1))
    return body


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


if __name__ == "__main__":
    out = os.path.dirname(os.path.abspath(__file__))
    body = build()
    print(f"outer diameter {2*OUT_R:.1f} mm, depth {DEPTH:.1f} mm, volume {body.val().Volume()/1000:.1f} cm3")
    if len(sys.argv) > 1:
        if not check_fit(body, load_board(sys.argv[1])):
            raise SystemExit(1)
    # print orientation: back face down on the bed, front up
    printable = body.rotate((0, 0, 0), (1, 0, 0), 180).translate((0, 0, BACK))
    cq.exporters.export(printable, os.path.join(out, "enclosure.stl"), tolerance=0.02, angularTolerance=0.1)
    cq.exporters.export(body, os.path.join(out, "enclosure.step"))
    print("wrote enclosure.stl / enclosure.step")
