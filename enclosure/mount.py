"""
Fixed-angle dashboard stand for the round inclinometer enclosure.

- Sticks to the dash with 3M VHB double-sided foam tape (base ~66 x 40 mm).
- Display bolts on with ONE 1/4"-20 x 1/2" (12.7 mm) bolt from behind into the
  captive nut in the enclosure. Do not use a longer bolt: it would reach the PCB.
- Two locating pins key into dimples on the enclosure back so it cannot rotate.
- No hinge on purpose: an inclinometer must not creep. Pick TILT, print, then
  "set level" once in the car (firmware accepts any mounting angle).

Printed lying on its side (profile on the bed) -> no supports, and layer lines
run along the bending direction, so the upright is strong.

Run:  python mount.py          -> mount.stl / mount.step (TILT below, 20 deg)
      python mount.py 0        -> mount_upright.stl / .step (display vertical)
      python mount.py 12       -> mount_tilt12.stl / .step
"""
import math, os
import cadquery as cq
import enclosure as E

TILT      = 20.0    # deg the display leans back from vertical (faces up toward the driver)
WIDTH     = 40.0    # stand width (left-right)
BASE_T    = 4.0     # base plate thickness
BASE_FWD  = 30.0    # base length in front of the upright
BASE_BACK = 30.0    # base length behind the upright
PLATE_T   = 7.0     # upright thickness
PAD_UP    = 22.0    # upright extends this far above the bolt
CLEAR     = 4.0     # gap between display bottom edge and base
GUSSET_Y  = 30.0    # height where the rear gusset meets the upright
BOLT_HOLE = 6.8
PIN_D, PIN_H = 4.0, 1.8


def set_tilt(deg):
    """(Re)compute the stand geometry for a given tilt."""
    global TILT, n, t, P
    TILT = deg
    th = math.radians(deg)
    n = (math.cos(th), math.sin(th))      # upright front-face normal (toward driver)
    t = (-math.sin(th), math.cos(th))     # "up" along the display
    P = (0.0, BASE_T + CLEAR + E.OUT_R * math.cos(th))   # bolt axis on upright front face


set_tilt(TILT)


def pt(base, s, v):
    return (base[0] + s * v[0], base[1] + s * v[1])


def profile_points():
    back = pt(P, -PLATE_T, n)                       # bolt point on upright back face
    s_bot_f = (BASE_T - P[1]) / t[1]
    s_bot_b = (BASE_T - back[1]) / t[1]
    f_top, f_bot = pt(P, PAD_UP, t), pt(P, s_bot_f, t)
    b_top, b_bot = pt(back, PAD_UP, t), pt(back, s_bot_b, t)
    g = pt(back, (GUSSET_Y - back[1]) / t[1], t)    # gusset attach point
    x_front = f_bot[0] + BASE_FWD
    x_back = b_bot[0] - BASE_BACK
    return [f_top, f_bot, (x_front, BASE_T), (x_front, 0), (x_back, 0),
            (x_back, BASE_T), g, b_top]


def build():
    pts = profile_points()
    m = cq.Workplane("XY").polyline(pts).close().extrude(WIDTH)
    try:
        m = m.edges("|Z").fillet(1.5)
    except Exception:
        pass
    # bolt hole through the upright, perpendicular to it
    axis = cq.Vector(-n[0], -n[1], 0)
    hole = cq.Solid.makeCylinder(BOLT_HOLE / 2, PLATE_T + 2,
                                 cq.Vector(P[0] + n[0], P[1] + n[1], WIDTH / 2), axis)
    m = m.cut(hole)
    # locating pins on the upright face, either side of the bolt
    for dz in (-E.PIN_X, E.PIN_X):
        pin = cq.Solid.makeCylinder(PIN_D / 2, PIN_H + 0.5,
                                    cq.Vector(P[0] - 0.5 * n[0], P[1] - 0.5 * n[1], WIDTH / 2 + dz),
                                    cq.Vector(n[0], n[1], 0))
        m = m.union(pin)
    return m


def place_enclosure(shape):
    """Move a shape from enclosure (x, y, d) coords onto the stand."""
    from OCP.gp import gp_Trsf
    from OCP.BRepBuilderAPI import BRepBuilderAPI_Transform
    T = (P[0] + E.BACK * n[0], P[1] + E.BACK * n[1], WIDTH / 2)
    tr = gp_Trsf()
    tr.SetValues(0, t[0], -n[0], T[0],
                 0, t[1], -n[1], T[1],
                 1, 0,     0,    T[2])
    return cq.Shape.cast(BRepBuilderAPI_Transform(shape.wrapped, tr, True).Shape())


if __name__ == "__main__":
    import sys
    out = os.path.dirname(os.path.abspath(__file__))
    name = "mount"
    if len(sys.argv) > 1:
        set_tilt(float(sys.argv[1]))
        name = "mount_upright" if TILT == 0 else f"mount_tilt{sys.argv[1]}"
    m = build()
    bb = m.val().BoundingBox()
    print(f"stand {bb.xlen:.0f} x {WIDTH:.0f} mm footprint, {bb.ylen:.0f} mm tall, "
          f"display centre {P[1] + E.BACK * n[1]:.0f} mm above dash, volume {m.val().Volume()/1000:.1f} cm3")
    # overlap check against the enclosure (pins sit in dimples -> should be 0)
    enc = place_enclosure(E.build().val())
    v = m.val().intersect(enc).Volume()
    print("stand/enclosure overlap:", f"{v:.3f} mm3", "OK" if v < 1e-3 else "COLLISION")
    cq.exporters.export(m, os.path.join(out, name + ".stl"), tolerance=0.02, angularTolerance=0.1)
    cq.exporters.export(m, os.path.join(out, name + ".step"))
    print(f"wrote {name}.stl / {name}.step  (tilt {TILT:g} deg)")
