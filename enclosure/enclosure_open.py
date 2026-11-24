"""
Open-back enclosure for the Waveshare ESP32-S3-Touch-LCD-2.8C (round 2.8", 95.86 mm glass).

Same outside as enclosure.py (diameter, depth, back face, snap pockets), so it
still snaps into cluster_insert.py, but with no mount: no 1/4"-20 nut, no back plate. The display
drops in face-up, the glass sits in the front pocket on a thin ledge, and four
arms reach in from the side wall to the board's M2 standoffs; 4x M2x6 screws
from the back pull the board (and so the glass) onto the ledge. Behind the
ledge everything is open.

Board positions (measured screw holes, BOARD_SHIFT for the rest) come from
enclosure.py, see its docstring.

Coordinates as in enclosure.py: x, y = glass centre viewed from the BACK (USB
ports lower-left / lower-right), d = depth from the glass front face, + toward
the back.

Print back face down (front up), no supports: the ledge is carried by a 45 deg
cone, the screw arms stand on the bed. PETG or ASA.

Run:  python enclosure_open.py [path/to/ESP32-S3-Touch-LCD-2.8C.step]
      -> writes enclosure_open.stl / .step (and checks fit if STEP given)
"""
import math, sys, os
import cadquery as cq
import enclosure as E

STANDOFFS, SX, SY = E.STANDOFFS, E.SX, E.SY

# ---------------- enclosure ----------------
OUT_R      = E.OUT_R                 # same outside as enclosure.py -> fits the cluster insert
FRONT, BACK = E.FRONT, E.BACK
POCKET_R   = E.POCKET_R
LEDGE_T    = 2.4                     # glass ledge thickness
LEDGE_IN_R = 39.5                    # ledge inner edge (clears the LCD body)
REAR_WALL  = 2.6                     # side wall behind the ledge
WALL_IN_R  = OUT_R - REAR_WALL
LEDGE_BACK = E.GLASS_T + LEDGE_T
ARM_W      = 7.0
ARM_FRONT  = E.STANDOFF_TOP + 0.10   # screws pull the board back onto the arms
ARM_T      = 2.6                     # material under the screw head (M2x6 -> ~3 mm thread)
BOSS_R     = 3.4
SCREW_D    = E.SCREW_D
HEAD_D     = E.SCREW_HEAD_D
SNAP_PAD_T = 1.6                     # wall left inside each snap pocket
SNAP_PAD_SIDE = 1.5                  # pad reaches this far past the pocket's sides
SNAP_PAD_BELOW = 2.0                 # ... and in front of its floor
# arm direction from each standoff to the wall (deg), clear of USB / buttons
ARM_DIR    = [160, 20, -110, -70]


def body_ring():
    b = (cq.Workplane("XY").workplane(offset=FRONT).circle(OUT_R).extrude(BACK - FRONT)
         .faces("<Z").edges().chamfer(0.6))
    # glass pocket + lead-in chamfer
    b = b.cut(cq.Workplane("XY").workplane(offset=FRONT - 1)
              .circle(POCKET_R).extrude(1 + E.LIP + E.GLASS_T))
    b = b.cut(cq.Workplane("XY").workplane(offset=FRONT - 0.01)
              .circle(POCKET_R + 0.4).workplane(offset=0.41).circle(POCKET_R).loft())
    # open behind the glass: ledge hole, then a 45 deg cone out to the side wall
    cone_end = LEDGE_BACK + (WALL_IN_R - LEDGE_IN_R)
    prof = [(0, E.GLASS_T - 0.01), (LEDGE_IN_R, E.GLASS_T - 0.01), (LEDGE_IN_R, LEDGE_BACK),
            (WALL_IN_R, cone_end), (WALL_IN_R, BACK + 1), (0, BACK + 1)]
    cav = cq.Workplane("XZ").polyline(prof).close().revolve(360, (0, 0, 0), (0, 1, 0))
    b = b.cut(cav)
    # small outer chamfer on the back rim (keeps the flat for the insert's hooks)
    return b.faces(">Z").edges(cq.selectors.RadiusNthSelector(1)).chamfer(0.4)


def arm(sx, sy, ang):
    """Rib from the side wall to a standoff, front face just behind the standoff."""
    a = math.radians(ang)
    L = 60.0
    rib = (cq.Workplane("XY").workplane(offset=ARM_FRONT)
           .center(sx, sy).rect(L, ARM_W, centered=(False, True)).extrude(BACK - ARM_FRONT)
           .rotate((sx, sy, 0), (sx, sy, 1), ang))
    rib = rib.union(cq.Workplane("XY").workplane(offset=ARM_FRONT)
                    .center(sx, sy).circle(BOSS_R).extrude(BACK - ARM_FRONT))
    # keep it inside the outer wall
    return rib.intersect(cq.Workplane("XY").workplane(offset=ARM_FRONT)
                         .circle(OUT_R - 0.5).extrude(BACK - ARM_FRONT))


def build():
    body = body_ring()

    for (sx, sy), ang in zip(STANDOFFS, ARM_DIR):
        body = body.union(arm(sx, sy, ang))
        body = body.cut(cq.Workplane("XY").workplane(offset=ARM_FRONT - 1)
                        .center(sx, sy).circle(SCREW_D / 2).extrude(BACK - ARM_FRONT + 2))
        body = body.cut(cq.Workplane("XY").workplane(offset=ARM_FRONT + ARM_T)
                        .center(sx, sy).circle(HEAD_D / 2).extrude(BACK))

    # snap-catch pockets for the cluster insert (as in enclosure.py); the rear wall
    # is thinner than the pocket is deep, so a pad inside the wall carries each one
    for a in E.SNAP_ANG:
        half = math.degrees((E.SNAP_W / 2 + E.SNAP_GAP + SNAP_PAD_SIDE) / OUT_R)
        body = body.union(E.sector(E.SNAP_POCKET_R - SNAP_PAD_T, OUT_R - 0.5, a - half, a + half,
                                   BACK - E.SNAP_DEPTH - SNAP_PAD_BELOW, BACK))
        body = body.cut(E.snap_pocket(a))

    # LCD flex tail wraps around the bottom edge of the LCD: notch in the ledge
    body = body.cut(E.flex_notch(LEDGE_T + 6))

    # USB-C openings, 45 deg toward lower-left / lower-right
    for mx, my, side in E.USB_MOUTH:
        mx, my = mx + SX, my + SY
        ang = math.degrees(math.atan2(my, mx))
        r0 = math.hypot(mx, my) - 2
        c = (cq.Workplane("XY")
             .box(OUT_R - r0 + 3, E.USB_W, E.USB_H, centered=(False, True, True))
             .edges("|X").fillet(2.0)
             .translate((r0, 0, E.USB_MOUTH_D))
             .rotate((0, 0, 0), (0, 0, 1), ang))
        c = c.intersect(cq.Workplane("XY").workplane(offset=E.GLASS_T + 0.05)
                        .circle(OUT_R + 10).extrude(30))
        body = body.cut(c)

    # BOOT / RST paper-clip holes and battery-switch slot (left edge)
    for y in E.BTN_Y.values():
        body = body.cut(cq.Workplane("YZ").workplane(offset=-OUT_R - 2)
                        .center(y + SY, E.BTN_D).circle(E.PIN_D / 2).extrude(OUT_R - LEDGE_IN_R + 4))
    body = body.cut(cq.Workplane("YZ").workplane(offset=-OUT_R - 2)
                    .center(sum(E.SW_Y) / 2 + SY, E.SW_D)
                    .slot2D(E.SW_Y[1] - E.SW_Y[0], 2.2)
                    .extrude(OUT_R - LEDGE_IN_R + 4))
    return body


if __name__ == "__main__":
    out = os.path.dirname(os.path.abspath(__file__))
    body = build()
    print(f"outer diameter {2*OUT_R:.1f} mm, depth {BACK-FRONT:.1f} mm, "
          f"back opening {2*WALL_IN_R:.1f} mm, volume {body.val().Volume()/1000:.1f} cm3")
    if len(sys.argv) > 1:
        if not E.check_fit(body, E.load_board(sys.argv[1])):
            raise SystemExit(1)
    printable = body.rotate((0, 0, 0), (1, 0, 0), 180).translate((0, 0, BACK))
    cq.exporters.export(printable, os.path.join(out, "enclosure_open.stl"),
                        tolerance=0.02, angularTolerance=0.1)
    cq.exporters.export(body, os.path.join(out, "enclosure_open.step"))
    print("wrote enclosure_open.stl / enclosure_open.step")
