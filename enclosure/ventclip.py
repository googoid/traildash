"""
Air-vent clip for the round inclinometer enclosure (horizontal vent slats).

- A fork slides over one slat; one-way teeth inside the jaws make it easy to push
  on and hard to pull off.
- The back plate extends below the fork and rests against the lower slats, so
  the display can't pitch when the slat is moved (important for an inclinometer).
- Display bolts on with the same 1/4"-20 x 1/2" button-head bolt as the dash
  stand; head sits in a counterbore so the plate stays flat against the vent.
  Same two anti-rotation pins as the stand.

Measure your slat thickness and set SLAT_T. Printed lying on its side.

Run:  python ventclip.py      -> ventclip.stl / ventclip.step
"""
import os
import cadquery as cq
import enclosure as E

SLAT_T    = 2.0     # thickness of your vent slat (mm) -- measure it!
GRIP      = 0.3     # tooth height on each jaw (interference)
JAW_LEN   = 15.0    # how deep the fork goes into the vent
JAW_T     = 2.6     # jaw thickness
WIDTH     = 32.0    # clip width (fork is full width)
PLATE_T   = 8.0     # back plate thickness
TOP       = 12.0    # plate top above slat centre
BOTTOM    = -38.0   # plate bottom (brace) below slat centre
BOLT_Y    = -12.0   # bolt (display centre) below slat centre
HEAD_D, HEAD_H = 12.0, 4.0   # counterbore for the bolt head (vent side)
BOLT_HOLE = 6.8
PIN_D, PIN_H = 4.0, 1.8


def jaw(sign):
    """Upper (sign=+1) or lower (sign=-1) jaw, profile in x (out of vent = +) / y."""
    g = SLAT_T / 2
    pts = [(0.5, sign * (g + JAW_T)), (-JAW_LEN, sign * (g + JAW_T)),
           (-JAW_LEN, sign * (g + 1.0)), (-JAW_LEN + 1.0, sign * g)]
    xa, pitch = -JAW_LEN + 2.5, 3.5
    while xa + pitch < -1.0:                       # sawtooth: ramp in, hard step out
        xb = xa + pitch - 0.4
        pts += [(xa, sign * g), (xb, sign * (g - GRIP)), (xb, sign * g)]
        xa += pitch
    pts += [(0.5, sign * g)]
    return cq.Workplane("XY").polyline(pts).close().extrude(WIDTH)


def build():
    plate = (cq.Workplane("XY").center(PLATE_T / 2, (TOP + BOTTOM) / 2)
             .rect(PLATE_T, TOP - BOTTOM).extrude(WIDTH))
    try:
        plate = plate.edges("|Z").fillet(1.5)
    except Exception:
        pass
    c = plate.union(jaw(+1)).union(jaw(-1))
    # bolt hole + head counterbore from the vent side
    c = c.cut(cq.Solid.makeCylinder(BOLT_HOLE / 2, PLATE_T + 2,
                                    cq.Vector(-1, BOLT_Y, WIDTH / 2), cq.Vector(1, 0, 0)))
    c = c.cut(cq.Solid.makeCylinder(HEAD_D / 2, HEAD_H + 1,
                                    cq.Vector(-1, BOLT_Y, WIDTH / 2), cq.Vector(1, 0, 0)))
    # anti-rotation pins on the display side
    for dz in (-E.PIN_X, E.PIN_X):
        c = c.union(cq.Solid.makeCylinder(PIN_D / 2, PIN_H + 0.5,
                                          cq.Vector(PLATE_T - 0.5, BOLT_Y, WIDTH / 2 + dz),
                                          cq.Vector(1, 0, 0)))
    return c


def place_enclosure(shape):
    """Move a shape from enclosure (x, y, d) coords onto the clip."""
    from OCP.gp import gp_Trsf
    from OCP.BRepBuilderAPI import BRepBuilderAPI_Transform
    tr = gp_Trsf()
    tr.SetValues(0, 0, -1, PLATE_T + E.BACK,
                 0, 1,  0, BOLT_Y,
                 1, 0,  0, WIDTH / 2)
    return cq.Shape.cast(BRepBuilderAPI_Transform(shape.wrapped, tr, True).Shape())


if __name__ == "__main__":
    out = os.path.dirname(os.path.abspath(__file__))
    c = build()
    print(f"clip {WIDTH:.0f} wide x {TOP-BOTTOM:.0f} tall, fork {JAW_LEN:.0f} deep for {SLAT_T} mm slats, "
          f"volume {c.val().Volume()/1000:.1f} cm3")
    # under-head material + enclosure floor; the bolt must stay inside the nut
    tip = E.BACK - (12.7 - (PLATE_T - HEAD_H) - E.NUT_FLOOR)
    print(f"1/2in bolt tip depth {tip:.1f} mm (nut spans {E.BACK-E.NUT_FLOOR-E.NUT_H:.1f}..{E.BACK-E.NUT_FLOOR:.1f}, PCB parts end ~8.4)")
    v = c.val().intersect(place_enclosure(E.build().val())).Volume()
    print("clip/enclosure overlap:", f"{v:.3f} mm3", "OK" if v < 1e-3 else "COLLISION")
    cq.exporters.export(c, os.path.join(out, "ventclip.stl"), tolerance=0.02, angularTolerance=0.1)
    cq.exporters.export(c, os.path.join(out, "ventclip.step"))
    print("wrote ventclip.stl / ventclip.step")
