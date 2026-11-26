"""
Speedometer-opening insert for the TrailDash enclosure
(2005 Toyota 4Runner V8 instrument cluster, centre pod).

The printed enclosure (enclosure.py) snaps into this insert from the back, and
the insert pushes into the speedometer opening of the cluster bezel:

- the opening is a circle with a FLAT BOTTOM; the insert has the same flat
  bottom, which rests on it (and keeps the insert from turning -- it would
  rotate the roll reading). Its flat edge is only ~47 mm below the circle
  centre, less than the display radius, so the display sits SHIFT mm above
  the centre of the opening
- front flange sits on the silver bezel ring (outer edge a circle about the display
  centre, so the display is centred in it; flat at the bottom) and
  hides the gap; its lip covers the enclosure rim, so only the glass shows
- six spiral leaf springs on the round part push the insert down onto the flat
  and centre it sideways. They sit deep in the opening (12-29 mm behind the
  flange, the top ones deepest), on the straight part of the bezel wall, so they
  don't press on its rounded front edge (that pushed the prototypes back out).
  The sleeve runs on past the snap hooks to carry them (they take openings ~123 to ~133 mm, so the photo estimate
  of the opening does not have to be exact)
- three snap hooks hold the enclosure; their catches sit in pockets in its back
  edge, flush with the back (that also keys its rotation). Push it in from the back until it clicks,
  spread the hooks to take it out (needed for BOOT/RST and the battery switch).
  The hooks bend outward while the enclosure goes in, so no spring may run over
  a hook (on the first prototype that snapped the two lower springs off);
  check() verifies this
- windows in the sleeve at both USB ports, open to the back: use a right-angle
  USB-C cable whose plug points BACKWARD (a straight plug sticks out too far
  for the opening); plug it in before pushing the insert into the dash

Opening measured from a photo with a tape measure across the bezel:
silver-ring inner edge ~128 mm across, flat bottom ~47 mm below its centre,
ring outer ~136 mm, >= 40 mm deep to the cluster lens. The insert uses 35 mm:
the display sits as deep as that allows (RECESS), seen through a tunnel.

Coordinates are the enclosure's: x, y viewed from the BACK, d (= z) depth from
the glass front, positive toward the back. The display centre is the origin;
the centre of the opening is at (0, -SHIFT).

Print front face down (flange on the bed), no supports, PETG or ASA -- not PLA,
a dashboard in the sun softens PLA.

Run:  python cluster_insert.py        -> cluster_insert.stl / .step
"""
import math, os
import cadquery as cq
import enclosure as E

# ---------------- car (from the photo, see docstring) ----------------
OPEN_D     = 128.0     # bezel opening (round part), estimated
FLAT_Y     = 47.2      # opening centre -> flat bottom edge, estimated
FLANGE_OVER = 3.0      # flange bottom runs this far below the opening's flat

# ---------------- insert ----------------
FIT        = 0.20                      # radial clearance around the enclosure
BORE_R     = E.OUT_R + FIT             # 50.53
TUBE_T     = 1.8
TUBE_R     = BORE_R + TUBE_T
FLAT_CLEAR = 0.3                       # insert bottom sits this far above the flat
SHIFT      = TUBE_R + FLAT_CLEAR - FLAT_Y   # display centre above opening centre
OY         = -SHIFT                    # opening centre (y) in display coords
SHOE_Y     = OY - FLAT_Y + FLAT_CLEAR  # insert bottom face (y)
# The display sits SHIFT above the opening centre (the flat stops it going lower),
# so the flange's outer edge is a circle about the DISPLAY centre: the display is
# exactly centred in it. Kept small enough to stay on the silver bezel at the top;
# that leaves a thin crescent of the opening (up to ~2 mm) uncovered at the lower
# corners, which is accepted.
RING_R     = 66.0
SHOE_W     = 30.0                      # width of the flat bottom pad (between the lower hooks)
LIP_IN_R   = E.POCKET_R + 0.3          # front lip covers the enclosure rim only
LIP_T      = 1.2                       # lip thickness in front of the enclosure
FLANGE_T   = 2.6
LENS_DEPTH = 40.0                      # bezel face -> cluster lens, shallowest (measured)
LENS_GAP   = 5.0                       # keep the insert this far off the lens
HOOK_H     = E.SNAP_CATCH_H            # catch height (ramp length)
HOOK_END   = E.BACK                    # catches sit in the enclosure's back pockets, flush
# The display sits as deep as the lens allows: the hook ends (flush with the
# enclosure back) come to LENS_GAP in front of the lens; the lip becomes a
# tunnel from the flange to the glass.
RECESS     = LENS_DEPTH - LENS_GAP - (HOOK_END - (E.FRONT - LIP_T + FLANGE_T))
FRONT      = E.FRONT - LIP_T - RECESS  # insert front face (d)
FLANGE_BACK = FRONT + FLANGE_T

# springs (root angle about the display centre, sweep; + = counter-clockwise)
SPR_T      = 2.0                       # leaf thickness (1.6 was too weak)
SPR_FULL   = 5.0                       # full-height part (presses on the wall) ...
SPR_TAPER  = 6.0                       # ... then the lead-in taper toward the back
SPR_H      = SPR_FULL + SPR_TAPER
SPR_FREE_R = OPEN_D / 2 + 2.5          # free tip radius about the opening centre
# (root, sweep, start depth behind the flange) -- mirror pairs, the top ones deepest
SPRINGS    = [(-29, 31, 12.0), (6, 32, 15.0), (42, 36, 18.0),
              (209, -31, 12.0), (174, -32, 15.0), (138, -36, 18.0)]
LEAD       = 1.0                       # lead-in: tip radius drops 1 mm per mm depth

# snap hooks
HOOK_ANG   = E.SNAP_ANG                # lower two beside the flat shoe, away from springs
HOOK_W     = E.SNAP_W                  # arm width (mm along the tube)
HOOK_SLOT  = 1.2
HOOK_FROM  = HOOK_END - 16.65           # arm free length starts here
HOOK_CATCH = HOOK_END - HOOK_H          # catch face just behind the pocket floor
HOOK_IN    = E.SNAP_CATCH_IN           # catch protrudes this far inward
# the sleeve runs on past the hooks to carry the deep springs
TUBE_END   = max(HOOK_END, FLANGE_BACK + max(s[2] for s in SPRINGS) + SPR_H + 0.5)

# USB windows (open toward the back so the cable can leave that way)
USB_ANG    = {s: math.degrees(math.atan2(y, x)) for x, y, s in E.USB_MOUTH}
WIN_HALF   = 10.0      # deg
WIN_D0     = E.USB_MOUTH_D - 5.5


def polar(r, deg, cy=0.0):
    a = math.radians(deg)
    return (r * math.cos(a), cy + r * math.sin(a))


def d_shape(r, flat_y, d0, d1):
    """Circle of radius r about the opening centre, cut flat at y = flat_y."""
    c = (cq.Workplane("XY").workplane(offset=d0).center(0, OY)
         .circle(r).extrude(d1 - d0))
    keep = (cq.Workplane("XY").workplane(offset=d0 - 1)
            .center(0, flat_y + 200).rect(400, 400).extrude(d1 - d0 + 2))
    return c.intersect(keep)


def sector(r0, r1, a0, a1, d0, d1):
    """Annular sector solid (about the display centre) between angles a0..a1 (deg)."""
    n = max(4, int(abs(a1 - a0) / 2))
    pts = [polar(r1, a0 + (a1 - a0) * i / n) for i in range(n + 1)]
    pts += [polar(r0, a1 - (a1 - a0) * i / n) for i in range(n + 1)]
    return (cq.Workplane("XY").workplane(offset=d0)
            .polyline(pts).close().extrude(d1 - d0))


def spring(root, sweep, dz):
    """Spiral leaf from the tube wall out to a rounded tip pad on the opening circle,
    dz..dz+SPR_H behind the flange, tip trimmed by the lead-in cone."""
    d0, d1 = FLANGE_BACK + dz, FLANGE_BACK + dz + SPR_H
    tip_ang = root + sweep
    tx, ty = polar(SPR_FREE_R - SPR_T / 2 - 0.8, tip_ang, OY)
    r0, r1 = TUBE_R - 0.6, math.hypot(tx, ty)
    a1 = math.degrees(math.atan2(ty, tx))
    while a1 - root > 180: a1 -= 360
    while a1 - root < -180: a1 += 360
    n = 30
    cl = []
    for i in range(n + 1):
        u = i / n
        cl.append((root + (a1 - root) * u, r0 + (r1 - r0) * (u ** 1.3)))
    outer = [polar(r + SPR_T / 2, a) for a, r in cl]
    inner = [polar(r - SPR_T / 2, a) for a, r in reversed(cl)]
    leaf = (cq.Workplane("XY").workplane(offset=d0)
            .polyline(outer + inner).close().extrude(d1 - d0))
    px, py = polar(r1, a1)
    pad = (cq.Workplane("XY").workplane(offset=d0)
           .center(px, py).circle(SPR_T / 2 + 0.8).extrude(d1 - d0))
    return leaf.union(pad).cut(lead_in_cut(d0 + SPR_FULL, d1))


def lead_in_cut(d_start, d_end):
    """Cone about the opening centre that trims a spring tip so it slides in."""
    big = SPR_FREE_R + 20
    prof = [(SPR_FREE_R, d_start), (SPR_FREE_R - LEAD * (d_end + 1 - d_start), d_end + 1),
            (big, d_end + 1), (big, d_start)]
    cone = cq.Workplane("XZ").polyline(prof).close().revolve(360, (0, 0, 0), (0, 1, 0))
    return cone.translate((0, OY, 0))


def hook(ang):
    """Slots that free a strip of the tube, plus an inward catch with a ramp."""
    half = math.degrees(HOOK_W / 2 / TUBE_R)
    slot = math.degrees(HOOK_SLOT / TUBE_R)
    cut = (sector(BORE_R - 1, TUBE_R + 1, ang - half - slot, ang - half, HOOK_FROM, TUBE_END + 1)
           .union(sector(BORE_R - 1, TUBE_R + 1, ang + half, ang + half + slot, HOOK_FROM, TUBE_END + 1)))
    # catch: flat face toward the front, ramp toward the back
    a0, a1 = ang - half + 0.5, ang + half - 0.5
    prof = [(BORE_R + 0.01, HOOK_CATCH), (BORE_R - HOOK_IN, HOOK_CATCH),
            (BORE_R - 0.3, HOOK_END), (BORE_R + 0.01, HOOK_END)]
    catch = (cq.Workplane("XZ").polyline(prof).close()
             .revolve(a1 - a0, (0, 0, 0), (0, 1, 0)))
    bb = catch.val().BoundingBox()          # revolve sense depends on the plane normal
    start = a0 if bb.ymax > 1e-6 else a1
    catch = catch.rotate((0, 0, 0), (0, 0, 1), start)
    return cut, catch


def flange():
    """Circle RING_R about the display centre, flat at the bottom."""
    flat = OY - FLAT_Y - FLANGE_OVER
    return (cq.Workplane("XY").workplane(offset=FRONT).circle(RING_R).extrude(FLANGE_T)
            .intersect(cq.Workplane("XY").workplane(offset=FRONT - 1)
                       .center(0, flat + 200).rect(400, 400).extrude(FLANGE_T + 2)))


def build():
    body = flange()
    body = body.faces("<Z").edges().chamfer(1.0)    # lip hole is cut after this
    body = body.cut(cq.Workplane("XY").workplane(offset=FRONT - 1).circle(LIP_IN_R).extrude(FLANGE_T + 2))

    body = body.union(cq.Workplane("XY").workplane(offset=FRONT)
                      .circle(TUBE_R).circle(LIP_IN_R).extrude(TUBE_END - FRONT))
    # flat bottom shoe that rests on the bezel's flat edge
    body = body.union(cq.Workplane("XY").workplane(offset=FLANGE_BACK - 0.01)
                      .center(0, SHOE_Y + 4).rect(SHOE_W, 8).extrude(HOOK_END - FLANGE_BACK)
                      .edges("|Z").fillet(1.5))
    # bore (lip stays only in front of the enclosure)
    body = body.cut(cq.Workplane("XY").workplane(offset=E.FRONT)
                    .circle(BORE_R).extrude(TUBE_END - E.FRONT + 1))
    # tunnel from the flange to the glass (the shoe runs past it)
    body = body.cut(cq.Workplane("XY").workplane(offset=FRONT - 1)
                    .circle(LIP_IN_R).extrude(E.FRONT - FRONT + 2))

    for sp in SPRINGS:
        body = body.union(spring(*sp))

    for a in HOOK_ANG:
        cut, catch = hook(a)
        body = body.cut(cut).union(catch)

    # USB windows in the tube (plug enters radially through them)
    for a in USB_ANG.values():
        body = body.cut(sector(BORE_R - 1, TUBE_R + 5, a - WIN_HALF, a + WIN_HALF, WIN_D0, TUBE_END + 1))

    # nothing may stick out below the flat or past the opening circle (behind the flange)
    body = body.cut(cq.Workplane("XY").workplane(offset=FLANGE_BACK)
                    .center(0, SHOE_Y - 100).rect(400, 200).extrude(50))
    return body


def check(body):
    enc = E.build().val()
    v = body.val().intersect(enc).Volume()
    print("insert/enclosure overlap:", f"{v:.3f} mm3", "OK" if v < 1e-3 else "COLLISION")
    # behind the flange, everything but the spring tips must be inside the opening
    hole = d_shape(OPEN_D / 2, OY - FLAT_Y, FLANGE_BACK + 0.01, 60).val()
    rear = body.val().intersect(cq.Solid.makeBox(400, 400, 60, cq.Vector(-200, -200, FLANGE_BACK + 0.01)))
    out = rear.cut(hole).Volume()
    # no spring may sit where a hook arm bends out while the enclosure goes in
    springs = spring(*SPRINGS[0])
    for sp in SPRINGS[1:]:
        springs = springs.union(spring(*sp))
    hit = 0.0
    for ang in HOOK_ANG:
        half = math.degrees((HOOK_W / 2 + HOOK_SLOT + 1.0) / TUBE_R)
        zone = sector(TUBE_R + 0.05, TUBE_R + HOOK_IN + 1.5, ang - half, ang + half, HOOK_FROM, TUBE_END)
        hit += springs.val().intersect(zone.val()).Volume()
    gap = LENS_DEPTH - (TUBE_END - FLANGE_BACK)
    print(f"clearance to the cluster lens: {gap:.1f} mm", "OK" if gap >= LENS_GAP - 1e-6 else "TOO DEEP")
    if gap < LENS_GAP - 1e-6:
        raise SystemExit("insert reaches too close to the lens")
    print("springs clear of hook flex zones:", "OK" if hit < 1e-3 else f"NO ({hit:.1f} mm3)")
    if hit >= 1e-3:
        raise SystemExit("spring over a hook")
    # how much of the opening the flange leaves open
    hole = d_shape(OPEN_D / 2, OY - FLAT_Y, FRONT, FLANGE_BACK).val()
    bare = hole.cut(flange().val()).Volume()
    print(f"opening left uncovered by the flange (lower corners, accepted): {bare / FLANGE_T:.0f} mm2")
    # the window for the glass must be open all the way through
    win = cq.Solid.makeCylinder(LIP_IN_R - 0.05, TUBE_END - FRONT + 2, cq.Vector(0, 0, FRONT - 1))
    blk = body.val().intersect(win).Volume()
    print("glass window:", "OPEN" if blk < 1e-3 else f"BLOCKED ({blk:.1f} mm3)")
    if blk >= 1e-3:
        raise SystemExit("glass window blocked")
    print(f"outside the opening behind the flange: {out:.1f} mm3 (spring tips only, they flex)")
    print(f"display centre {SHIFT:.1f} mm above the opening centre; insert bottom on the flat; "
          f"springs free {2*SPR_FREE_R:.1f} mm; depth behind flange {TUBE_END - FLANGE_BACK:.1f} mm "
          f"(bezel to lens >= {LENS_DEPTH:.0f}); front face {-FRONT:.1f} mm ahead of the glass; "
          f"volume {body.val().Volume()/1000:.1f} cm3")
    return enc


if __name__ == "__main__":
    out = os.path.dirname(os.path.abspath(__file__))
    body = build()
    check(body)
    name = "cluster_insert"
    # print orientation: front face down
    printable = body.translate((0, 0, -FRONT))
    cq.exporters.export(printable, os.path.join(out, name + ".stl"), tolerance=0.02, angularTolerance=0.1)
    cq.exporters.export(body, os.path.join(out, name + ".step"))
    print(f"wrote {name}.stl / .step")
