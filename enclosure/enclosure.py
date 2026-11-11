"""
Enclosure for the Waveshare ESP32-S3-Touch-LCD-2.8C (round 2.8", 95.86 mm glass).

One-piece shell: the display drops in face-up, the glass sits in a pocket on a
ledge, and 4x M2x6 screws from the back go into the board's own M2 standoffs.
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
STANDOFFS    = [(-29.5, 15.14), (29.5, 15.14), (-18.0, -25.56), (18.0, -25.56)]
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
SCREW_D, SCREW_HEAD_D, SCREW_HEAD_H = 2.4, 4.4, 4.4
NUT_AF, NUT_H, NUT_HOLE = 11.30, 5.80, 6.8   # 1/4"-20 hex nut (7/16" AF)
NUT_FLOOR    = 2.0
USB_W, USB_H = 13.5, 8.0     # plug overmold opening
PIN_D        = 2.0           # paper-clip holes for BOOT / RST
PIN_X, DIMPLE_D, DIMPLE_H = 13.0, 4.5, 2.2   # mount locating-pin dimples (back)

POCKET_R = GLASS_R + FIT
OUT_R    = POCKET_R + WALL
FRONT    = -LIP
BACK     = CAV_BACK + BACK_SKIN
DEPTH    = BACK - FRONT


def ring_body():
    return (cq.Workplane("XY").workplane(offset=FRONT)
            .circle(OUT_R).extrude(DEPTH)
            .faces(">Z").edges().fillet(1.5)     # back edge
            .faces("<Z").edges().chamfer(0.6))   # front edge


def cut_at(wp_solid, cutter):
    return wp_solid.cut(cutter)


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
    # notch for the LCD flex tail (wraps down to y=-43.6)
    cav = cav.union(cq.Workplane("XY").workplane(offset=GLASS_T)
                    .center(0, -36).rect(2 * 21.6, 7).extrude(CAV_BACK - GLASS_T))
    cav = cav.union(cq.Workplane("XY").workplane(offset=GLASS_T)
                    .center(0, -41.5).rect(2 * 16.0, 6).extrude(CAV_BACK - GLASS_T))
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
        ang = -135 if side < 0 else -45
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
                        .center(y, BTN_D).circle(PIN_D / 2).extrude(OUT_R - CAV_R + 4))
    body = body.cut(cq.Workplane("YZ").workplane(offset=-OUT_R - 2)
                    .center(sum(SW_Y) / 2, SW_D).slot2D(SW_Y[1] - SW_Y[0], 2.2)
                    .extrude(OUT_R - CAV_R + 4))

    # vent / buzzer slots in the back, lower-right
    # (top-centre, above the dash mount's contact pad)
    for i in range(5):
        body = body.cut(cq.Workplane("XY").workplane(offset=CAV_BACK - 1)
                        .center(i * 3.2 - 6.4, 30).slot2D(10, 1.6, 90)
                        .extrude(BACK_SKIN + 2))

    # anti-rotation dimples for the dash mount's locating pins
    for x in (-PIN_X, PIN_X):
        body = body.cut(cq.Workplane("XY").workplane(offset=BACK - DIMPLE_H)
                        .center(x, 0).circle(DIMPLE_D / 2).extrude(DIMPLE_H + 1))
    return body


def check_fit(body, step_path):
    """Intersect the shell with every solid of the Waveshare model."""
    CX, CY, Z0 = 18.0, 25.5642, -5.7105
    board = cq.importers.importStep(step_path)
    shell = body.val()
    sb = shell.BoundingBox()
    worst = 0.0
    for s in board.solids().vals():
        s = s.translate(cq.Vector(-CX, -CY, -Z0))
        b = s.BoundingBox()
        if b.xmax < sb.xmin or b.xmin > sb.xmax or b.ymax < sb.ymin or b.ymin > sb.ymax:
            continue
        v = shell.intersect(s).Volume()
        if v > 1e-3:
            worst = max(worst, v)
            print(f"  COLLISION {v:.3f} mm3 with part at "
                  f"({(b.xmin+b.xmax)/2:.1f},{(b.ymin+b.ymax)/2:.1f}) d{b.zmin:.2f}..{b.zmax:.2f}")
    print("fit check:", "OK, no collisions" if worst == 0 else "collisions found")
    return board


if __name__ == "__main__":
    out = os.path.dirname(os.path.abspath(__file__))
    body = build()
    print(f"outer diameter {2*OUT_R:.1f} mm, depth {DEPTH:.1f} mm, volume {body.val().Volume()/1000:.1f} cm3")
    if len(sys.argv) > 1:
        check_fit(body, sys.argv[1])
    # print orientation: back face down on the bed, front up
    printable = body.rotate((0, 0, 0), (1, 0, 0), 180).translate((0, 0, BACK))
    cq.exporters.export(printable, os.path.join(out, "enclosure.stl"), tolerance=0.02, angularTolerance=0.1)
    cq.exporters.export(body, os.path.join(out, "enclosure.step"))
    print("wrote enclosure.stl / enclosure.step")
