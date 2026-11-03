#!/usr/bin/env python3
"""cellgen: build Jedi Knight level geometry from a cell plan.

This is the prototype of the declarative builder RED's agent interface needs ("red-agent build").
A plan names 2D points and lays convex cells over them. A cell is a stack of layers (an open-air
sector, a water layer, a bridge deck over a ravine). Shared cell edges become adjoins automatically.
Height differences across an edge become riser and header faces. Sky ceilings, water surfaces, UVs
and baked vertex light are generated. The output is JKL section text (MATERIALS, GEORESOURCE,
SECTORS) plus lookups for tagged surfaces and for the sector at a point.

Standard library only. Red, November 2026.

Conventions: x east, y north, z up, JKU. Cell polygons are counter-clockwise seen from above (they are
reversed automatically with a warning). A wall polygon on edge a→b runs (a,lo) (a,hi) (b,hi) (b,lo), so
its normal points into the sector, as in retail levels.
"""
from __future__ import annotations

import math

EPS = 1e-6
SKY_FLAGS = 0x404          # ceiling sky + collision (Lily's sky ceilings); L.sky_flags overrides it.
                           # A ceiling sky needs a tileable cloud MAT (13cnsky2); a gradient MAT like
                           # 14sky belongs on a horizon sky (0x204), which maps it by view yaw and pitch.
FLOOR_FLAGS = 0x80005      # dirt + collision + floor
ROCKFLOOR_FLAGS = 0x5      # collision + floor
WALL_FLAGS = 0x4
WATER_FACE = 0x2           # translucent


def vsub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def vdot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def vcross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def vnorm(a):
    n = math.sqrt(vdot(a, a))
    return (a[0] / n, a[1] / n, a[2] / n) if n > 0 else (0.0, 0.0, 0.0)


def newell(poly):
    nx = ny = nz = 0.0
    for i, p in enumerate(poly):
        q = poly[(i + 1) % len(poly)]
        nx += (p[1] - q[1]) * (p[2] + q[2])
        ny += (p[2] - q[2]) * (p[0] + q[0])
        nz += (p[0] - q[0]) * (p[1] + q[1])
    return vnorm((nx, ny, nz))


def area2(pts):
    return sum(pts[i][0] * pts[(i + 1) % len(pts)][1] - pts[(i + 1) % len(pts)][0] * pts[i][1]
               for i in range(len(pts)))


def plane_from(points3):
    """z = a*x + b*y + c through the first non-collinear triple; None if all collinear."""
    n = len(points3)
    for i in range(n):
        for j in range(i + 1, n):
            for k in range(j + 1, n):
                p, q, r = points3[i], points3[j], points3[k]
                det = (q[0] - p[0]) * (r[1] - p[1]) - (r[0] - p[0]) * (q[1] - p[1])
                if abs(det) > 1e-9:
                    a = ((q[2] - p[2]) * (r[1] - p[1]) - (r[2] - p[2]) * (q[1] - p[1])) / det
                    b = ((q[0] - p[0]) * (r[2] - p[2]) - (r[0] - p[0]) * (q[2] - p[2])) / det
                    return (a, b, p[2] - a * p[0] - b * p[1])
    return None


def zat(plane, x, y):
    return plane[0] * x + plane[1] * y + plane[2]


class PlanError(Exception):
    pass


class Level:
    def __init__(self, title="level"):
        self.title = title
        self.points = {}
        self.mats = {}           # alias -> [file, px_per_jku]
        self.cells = []
        self.things = []
        self.lights = []         # (x, y, z, intensity, radius)
        self.sun = dict(azimuth=240.0, elevation=35.0, strength=0.5, sky=0.5, base=0.07, underwater=0.75)
        self.warnings = []
        self.built = False

    # ---------- plan API ----------
    def mat(self, alias, file, scale=256.0):
        self.mats[alias] = [file, float(scale)]

    def pt(self, name, x, y):
        if name in self.points and self.points[name] != (x, y):
            raise PlanError(f"point {name} redefined")
        self.points[name] = (float(x), float(y))
        return name

    def cell(self, name, pts, layers=None, **kw):
        """Add a cell. Either give layers=[{floor, ceil, ...}, ...] (bottom first) or floor/ceil/... directly.

        floor/ceil: number, {point: z} (every corner), callable f(x, y), or ("rel", h) for ceil = floor + h.
        Layer keys: floor, ceil, fmat, cmat, sky (bool), water (bool: this layer is under water), fflags,
        tint, sound, flags, light (multiplier), extra.
        Cell keys: wmat (default wall material), edges {(a, b): {mat, tag, flags}}, tag, light, layer defaults."""
        if layers is None:
            layers = [dict((k, kw.pop(k)) for k in list(kw) if k in (
                "floor", "ceil", "fmat", "cmat", "sky", "fflags", "tint", "sound", "flags", "extra"))]
            if kw.get("water") is not None:
                wz = kw.pop("water")
                lay = layers[0]
                layers = [dict(floor=lay["floor"], ceil=wz, fmat=lay.get("fmat"), water=True,
                               tint=kw.pop("wtint", (0.0, 0.12, 0.18)), sound=kw.pop("wsound", ("00underwater.wav", 0.35)),
                               fflags=ROCKFLOOR_FLAGS),
                          dict(lay, floor=wz)]
        for p in pts:
            if p not in self.points:
                raise PlanError(f"cell {name}: unknown point {p}")
        c = dict(name=name, pts=list(pts), layers=layers, **kw)
        self.cells.append(c)
        return c

    def light(self, x, y, z, intensity, radius):
        self.lights.append((x, y, z, intensity, radius))

    def thing(self, index, template, name, x, y, z=None, above=0.0, yaw=0.0, pitch=0.0, roll=0.0, extra="", layer=None):
        self.things.append(dict(index=index, template=template, name=name, x=x, y=y, z=z, above=above,
                                yaw=yaw, pitch=pitch, roll=roll, extra=extra, layer=layer))

    # ---------- build ----------
    def _corner_z(self, spec, pts3, floor_z=None):
        out = []
        for i, (x, y) in enumerate(pts3):
            name = self._names[i]
            if isinstance(spec, (int, float)):
                out.append(float(spec))
            elif isinstance(spec, dict):
                if name not in spec:
                    raise PlanError(f"height for {name} missing")
                out.append(float(spec[name]))
            elif isinstance(spec, tuple) and spec[0] == "rel":
                out.append(floor_z[i] + float(spec[1]))
            elif callable(spec):
                out.append(float(spec(x, y)))
            else:
                raise PlanError(f"bad height spec {spec!r}")
        return out

    def _expand(self):
        """Resolve layer heights per corner, reverse clockwise cells, triangulate non-planar cells."""
        cells = []
        for c in self.cells:
            pts = c["pts"]
            xy = [self.points[p] for p in pts]
            if area2(xy) < 0:
                self.warnings.append(f"cell {c['name']}: clockwise, reversed")
                pts = pts[::-1]
                xy = xy[::-1]
            n = len(pts)
            for i in range(n):
                a, b, d = xy[i - 1], xy[i], xy[(i + 1) % n]
                cr = (b[0] - a[0]) * (d[1] - b[1]) - (b[1] - a[1]) * (d[0] - b[0])
                if cr < -1e-9:
                    raise PlanError(f"cell {c['name']}: not convex at {pts[i]}")
            self._names = pts
            lays = []
            for lay in c["layers"]:
                fz = self._corner_z(lay["floor"], xy)
                cz = self._corner_z(lay["ceil"], xy, fz)
                for i in range(n):
                    if cz[i] < fz[i] - EPS:
                        raise PlanError(f"cell {c['name']}: ceiling below floor at {pts[i]}")
                lays.append((fz, cz))
            planar = True
            for fz, cz in lays:
                for zs in (fz, cz):
                    p3 = [(x, y, z) for (x, y), z in zip(xy, zs)]
                    pl = plane_from(p3)
                    if pl is None or any(abs(zat(pl, x, y) - z) > 1e-4 for x, y, z in p3):
                        planar = False
            if planar:
                cells.append(dict(c, pts=pts, corner=lays))
            else:
                # fan from the corner with the widest angle; corners only (collinear points dropped)
                keep = [i for i in range(n) if abs((xy[i][0] - xy[i - 1][0]) * (xy[(i + 1) % n][1] - xy[i][1])
                                                   - (xy[i][1] - xy[i - 1][1]) * (xy[(i + 1) % n][0] - xy[i][0])) > 1e-9]
                for t in range(1, len(keep) - 1):
                    tri = [keep[0], keep[t], keep[t + 1]]
                    sub = dict(c, name=f"{c['name']}#{t}", pts=[pts[i] for i in tri],
                               corner=[([fz[i] for i in tri], [cz[i] for i in tri]) for fz, cz in lays])
                    cells.append(sub)
        # planes per layer
        for c in cells:
            xy = [self.points[p] for p in c["pts"]]
            c["planes"] = []
            for fz, cz in c["corner"]:
                fp = plane_from([(x, y, z) for (x, y), z in zip(xy, fz)])
                cp = plane_from([(x, y, z) for (x, y), z in zip(xy, cz)])
                c["planes"].append((fp, cp))
        return cells

    def _refine(self, cells):
        names = list(self.points)
        for c in cells:
            out = []
            pts = c["pts"]
            c["parent"] = {}
            for i, a in enumerate(pts):
                b = pts[(i + 1) % len(pts)]
                start = len(out)
                out.append(a)
                ax, ay = self.points[a]
                bx, by = self.points[b]
                dx, dy = bx - ax, by - ay
                L2 = dx * dx + dy * dy
                mids = []
                for p in names:
                    if p in (a, b):
                        continue
                    px, py = self.points[p]
                    t = ((px - ax) * dx + (py - ay) * dy) / L2
                    if 1e-7 < t < 1 - 1e-7:
                        ex, ey = ax + t * dx - px, ay + t * dy - py
                        if ex * ex + ey * ey < 1e-8:
                            mids.append((t, p))
                out.extend(p for _, p in sorted(mids))
                chain = out[start:] + [b]
                for k in range(len(chain) - 1):
                    c["parent"][(chain[k], chain[k + 1])] = (a, b)
            c["poly"] = out

    def build(self):
        cells = self._expand()
        self._refine(cells)
        self.cellsx = cells
        edge_owner = {}
        for ci, c in enumerate(cells):
            poly = c["poly"]
            for i, a in enumerate(poly):
                b = poly[(i + 1) % len(poly)]
                if (a, b) in edge_owner:
                    raise PlanError(f"edge {a}-{b} used twice in the same direction ({cells[edge_owner[(a, b)]]['name']}, {c['name']})")
                edge_owner[(a, b)] = ci
        # sectors
        self.sectors = []
        for ci, c in enumerate(cells):
            c["sectors"] = []
            for li, lay in enumerate(c["layers"]):
                fp, cp = c["planes"][li]
                s = dict(cell=ci, layer=li, lay=lay, fplane=fp, cplane=cp, name=f"{c['name']}/{li}",
                         water=bool(lay.get("water")), sky=bool(lay.get("sky")), surfaces=[], edges=[])
                c["sectors"].append(len(self.sectors))
                self.sectors.append(s)
        for c in cells:
            secs = c["sectors"]
            for k in range(len(secs) - 1):
                lo, hi = self.sectors[secs[k]], self.sectors[secs[k + 1]]
                # stacked layers: water surface (adjoin) when the lower layer is water and planes coincide
                same = all(abs(zat(lo["cplane"], *self.points[p]) - zat(hi["fplane"], *self.points[p])) < 1e-5 for p in c["pts"])
                lo["above"] = (secs[k + 1], "water" if (lo["water"] and same) else "solid")
                hi["below"] = (secs[k], "water" if (lo["water"] and same) else "solid")
        self._verts = {}
        self.vertices = []
        self.texverts = []
        self.surfaces = []
        self.adjoins = []
        pending = {}
        for si, s in enumerate(self.sectors):
            c = cells[s["cell"]]
            poly = c["poly"]
            xy = [self.points[p] for p in poly]
            lay = s["lay"]
            # floor
            fpoly = [(x, y, zat(s["fplane"], x, y)) for x, y in xy]
            below = s.get("below")
            if below and below[1] == "water":
                self._surface(si, fpoly, "water", adjoin_to=below[0], key=("h", min(si, below[0]), max(si, below[0])), pending=pending)
            else:
                self._surface(si, fpoly, lay.get("fmat"), flags=lay.get("fflags", FLOOR_FLAGS), kind="floor", tag=("floor", c["name"], s["layer"]))
            # ceiling
            cpoly = [(x, y, zat(s["cplane"], x, y)) for x, y in reversed(xy)]
            above = s.get("above")
            if above and above[1] == "water":
                self._surface(si, cpoly, "water", adjoin_to=above[0], key=("h", min(si, above[0]), max(si, above[0])), pending=pending)
            elif s["sky"] and not above:
                self._surface(si, cpoly, "sky", flags=SKY_FLAGS, kind="sky")
            else:
                self._surface(si, cpoly, lay.get("cmat") or c.get("wmat"), flags=WALL_FLAGS, kind="ceiling")
            # walls
            for i, a in enumerate(poly):
                b = poly[(i + 1) % len(poly)]
                A, B = self.points[a], self.points[b]
                fa, fb = zat(s["fplane"], *A), zat(s["fplane"], *B)
                ca, cb = zat(s["cplane"], *A), zat(s["cplane"], *B)
                opens = []
                nci = edge_owner.get((b, a))
                if nci is not None:
                    for ti in cells[nci]["sectors"]:
                        t = self.sectors[ti]
                        lo_a = max(fa, zat(t["fplane"], *A)); lo_b = max(fb, zat(t["fplane"], *B))
                        hi_a = min(ca, zat(t["cplane"], *A)); hi_b = min(cb, zat(t["cplane"], *B))
                        da, db = hi_a - lo_a, hi_b - lo_b
                        if da > 1e-4 or db > 1e-4:
                            if da < -1e-4 or db < -1e-4:
                                raise PlanError(f"partial opening {c['name']} {a}-{b} to {cells[nci]['name']}: adjust heights")
                            opens.append((ti, lo_a, lo_b, max(lo_a, hi_a), max(lo_b, hi_b)))
                opens.sort(key=lambda o: o[1] + o[2])
                s["edges"].append((A, B, opens))
                pa, pb = c["parent"].get((a, b), (a, b))
                em = c.get("edges") or {}
                emeta = em.get((pa, pb)) or em.get((pb, pa)) or {}
                cur = (fa, fb)
                pieces = []
                for o in opens:
                    if o[1] < cur[0] - 1e-5 or o[2] < cur[1] - 1e-5:
                        raise PlanError(f"overlapping openings on {c['name']} {a}-{b}")
                    pieces.append(("solid", cur, (o[1], o[2]), None))
                    pieces.append(("open", (o[1], o[2]), (o[3], o[4]), o[0]))
                    cur = (o[3], o[4])
                skytop = bool(s["sky"] and opens and self.sectors[opens[-1][0]]["sky"] and not s.get("above"))
                bands = c.get("bands") if (not emeta.get("mat") and not skytop) else None
                decal = emeta.get("decal") if s["layer"] == len(c["sectors"]) - 1 else None
                if decal:
                    # one painted panel, fitted exactly once across this edge at a fixed height above the floor
                    elen = math.hypot(B[0] - A[0], B[1] - A[1])
                    hgt = elen * decal["px"][1] / decal["px"][0]
                    lo = (fa + decal["lo"], fb + decal["lo"])
                    hi = (lo[0] + hgt, lo[1] + hgt)
                    if not (cur[0] < lo[0] and hi[0] < ca and cur[1] < lo[1] and hi[1] < cb):
                        raise PlanError(f"decal on {c['name']} {a}-{b} does not fit the wall")
                    base = decal.get("base") or c.get("wmat")
                    pieces.append(("solid", cur, lo, "band", base))
                    pieces.append(("solid", lo, hi, "decal", decal["mat"]))
                    pieces.append(("solid", hi, (ca, cb), "top", base))
                    bands = None
                elif bands:
                    # strata bands: split the main wall at fixed heights above the local floor
                    zs = [cur]
                    for off, _m in sorted(bands, key=lambda b: b[0]):
                        za, zb = fa + off, fb + off
                        if zs[-1][0] + 1e-3 < za < ca - 1e-3 and zs[-1][1] + 1e-3 < zb < cb - 1e-3:
                            zs.append((za, zb))
                    rim = getattr(self, "rim", None)      # (depth below the sky, material): a weathered cap-rock band
                    if rim and s["sky"] and not s.get("above"):
                        ra, rb = ca - rim[0], cb - rim[0]
                        if zs[-1][0] + 0.3 < ra and zs[-1][1] + 0.3 < rb:
                            zs.append((ra, rb))
                    zs.append((ca, cb))
                    base = c.get("bandbase") or c.get("wmat")
                    for k in range(len(zs) - 1):
                        off_here = zs[k][0] - fa
                        mat_k = base
                        for off, m in sorted(bands, key=lambda b: b[0]):
                            if off <= off_here + 1e-3:
                                mat_k = m or c.get("wmat")
                        if rim and k == len(zs) - 2 and len(zs) > 2 and abs(zs[k][0] - (ca - rim[0])) < 1e-6:
                            mat_k = rim[1]
                        pieces.append(("solid", zs[k], zs[k + 1], "top" if k == len(zs) - 2 else "band", mat_k))
                elif not decal:
                    pieces.append(("solid", cur, (ca, cb), "top"))
                for piece in pieces:
                    kind, z0, z1, ref = piece[:4]
                    override = piece[4] if len(piece) > 4 else None
                    if z1[0] - z0[0] < 1e-5 and z1[1] - z0[1] < 1e-5:
                        continue
                    wpoly = [(A[0], A[1], z0[0]), (A[0], A[1], z1[0]), (B[0], B[1], z1[1]), (B[0], B[1], z0[1])]
                    wpoly = [p for k, p in enumerate(wpoly) if abs(p[2] - wpoly[k - 1][2]) > 1e-6 or p[:2] != wpoly[k - 1][:2]]
                    if len(wpoly) < 3:
                        continue
                    if kind == "open":
                        key = ("v", min(si, ref), max(si, ref), frozenset((a, b)), round(z0[0] + z0[1], 4))
                        self._surface(si, wpoly, None, adjoin_to=ref, key=key, pending=pending, wall=(A, B))
                    else:
                        skyhead = (ref == "top" and s["sky"] and opens and self.sectors[opens[-1][0]]["sky"]
                                   and not s.get("above"))
                        if skyhead:
                            self._surface(si, wpoly, "sky", flags=SKY_FLAGS, kind="sky", wall=(A, B))
                        else:
                            mat = override or emeta.get("mat") or c.get("wmat")
                            if ref not in ("top", "band", "decal") and kind == "solid" and z0 == (fa, fb) and emeta.get("riser"):
                                mat = emeta["riser"]
                            tagged = emeta.get("tag") and (ref == "decal" or not decal)
                            sf = self._surface(si, wpoly, mat, flags=emeta.get("flags", WALL_FLAGS), kind="wall", wall=(A, B),
                                               tag=("edge", emeta.get("tag"), s["layer"]) if tagged else None)
                            if ref == "decal":
                                sf["uvfit"] = (A, B, z0, z1, decal["px"])
        if pending:
            raise PlanError(f"{len(pending)} adjoins without a mirror: {list(pending)[:3]}")
        self._group_by_sector()
        self._light()
        self.built = True
        return self

    def _vertex(self, p):
        key = (round(p[0], 5), round(p[1], 5), round(p[2], 5))
        if key not in self._verts:
            self._verts[key] = len(self.vertices)
            self.vertices.append(key)
        return self._verts[key]

    def _surface(self, si, poly, mat, flags=0, kind="", adjoin_to=None, key=None, pending=None, wall=None, tag=None):
        s = dict(sector=si, poly=poly, mat=mat, flags=flags, kind=kind, adjoin=None, tag=tag, wall=wall,
                 normal=newell(poly), vidx=[self._vertex(p) for p in poly])
        if adjoin_to is not None:
            s["kind"] = "water" if mat == "water" else "adjoin"
            s["flags"] = 0
            other = pending.pop(key, None) if pending is not None else None
            s["adj"] = len(self.adjoins)
            self.adjoins.append(dict(surface=None, mirror=None, sector=si, to=adjoin_to))
            if other is not None:
                self.adjoins[s["adj"]]["mirror"] = other["adj"]
                self.adjoins[other["adj"]]["mirror"] = s["adj"]
            else:
                pending[key] = s
        self.sectors[si]["surfaces"].append(s)
        return s

    def _group_by_sector(self):
        self.surfaces = []
        for si, s in enumerate(self.sectors):
            s["first"] = len(self.surfaces)
            for surf in s["surfaces"]:
                surf["index"] = len(self.surfaces)
                if "adj" in surf:
                    self.adjoins[surf["adj"]]["surface"] = surf["index"]
                self.surfaces.append(surf)
            pts = [self.vertices[v] for surf in s["surfaces"] for v in surf["vidx"]]
            lo = [min(p[k] for p in pts) for k in range(3)]
            hi = [max(p[k] for p in pts) for k in range(3)]
            s["bbox"] = (lo, hi)
            s["center"] = tuple((lo[k] + hi[k]) / 2 for k in range(3))
            s["radius"] = max(math.dist(s["center"], p) for p in pts)
            s["vset"] = sorted({v for surf in s["surfaces"] for v in surf["vidx"]})

    # ---------- light ----------
    def _trace(self, si, P, D, maxd=None):
        """March a ray through sectors. True if it reaches the sky (maxd None) or the target distance."""
        h = math.hypot(D[0], D[1])
        travelled = 0.0
        for _ in range(400):
            s = self.sectors[si]
            if h < 1e-9:
                if D[2] > 0:
                    cz = zat(s["cplane"], P[0], P[1])
                    if maxd is not None and P[2] + (maxd - travelled) <= cz:
                        return True
                    above = s.get("above")
                    if above:
                        if above[1] == "water":
                            si, travelled = above[0], travelled + (cz - P[2]); P = (P[0], P[1], cz + 1e-6); continue
                        return False
                    return s["sky"] and maxd is None
                return False
            ux, uy, k = D[0] / h, D[1] / h, D[2] / h
            s_exit, e_exit = math.inf, None
            for ei, (A, B, opens) in enumerate(s["edges"]):
                dx, dy = B[0] - A[0], B[1] - A[1]
                nx, ny = dy, -dx
                den = nx * ux + ny * uy
                if den > 1e-12:
                    t = (nx * (A[0] - P[0]) + ny * (A[1] - P[1])) / den
                    if t < s_exit:
                        s_exit, e_exit = t, ei
            s_exit = max(0.0, s_exit)
            remaining = None if maxd is None else (maxd - travelled)
            # ceiling and floor along the ray
            cp, fp = s["cplane"], s["fplane"]
            cz0 = zat(cp, P[0], P[1]); slope_c = cp[0] * ux + cp[1] * uy
            fz0 = zat(fp, P[0], P[1]); slope_f = fp[0] * ux + fp[1] * uy
            hit_c = (cz0 - P[2]) / (k - slope_c) if k - slope_c > 1e-12 else math.inf
            hit_f = (fz0 - P[2]) / (k - slope_f) if k - slope_f < -1e-12 else math.inf
            if remaining is not None and remaining <= min(s_exit, hit_c, hit_f):
                return True
            if hit_c <= s_exit and hit_c <= hit_f:
                above = s.get("above")
                q = (P[0] + ux * hit_c, P[1] + uy * hit_c, P[2] + k * hit_c)
                if above and above[1] == "water":
                    si, P, travelled = above[0], (q[0], q[1], q[2] + 1e-6), travelled + hit_c
                    continue
                return bool(s["sky"] and not above and maxd is None)
            if hit_f <= s_exit:
                below = s.get("below")
                if below and below[1] == "water":
                    q = (P[0] + ux * hit_f, P[1] + uy * hit_f, P[2] + k * hit_f)
                    si, P, travelled = below[0], (q[0], q[1], q[2] - 1e-6), travelled + hit_f
                    continue
                return False
            if e_exit is None:
                return False
            A, B, opens = s["edges"][e_exit]
            q = (P[0] + ux * s_exit, P[1] + uy * s_exit, P[2] + k * s_exit)
            L = math.hypot(B[0] - A[0], B[1] - A[1])
            t = min(1.0, max(0.0, math.hypot(q[0] - A[0], q[1] - A[1]) / L))
            nxt = None
            for ti, la, lb, ha, hb in opens:
                lo = la + (lb - la) * t
                hi = ha + (hb - ha) * t
                if lo - 1e-6 <= q[2] <= hi + 1e-6:
                    nxt = ti
                    break
            if nxt is None:
                # above a lower sky neighbour: the header there is drawn as sky, so the ray is out
                if maxd is None and s["sky"] and opens and self.sectors[opens[-1][0]]["sky"]:
                    top = opens[-1]
                    if q[2] >= top[3] + (top[4] - top[3]) * t - 1e-6:
                        return True
                return False
            si = nxt
            P = (q[0] + ux * 1e-5, q[1] + uy * 1e-5, q[2] + k * 1e-5)
            travelled += s_exit
        return False

    def _light(self):
        sun = self.sun
        az, el = math.radians(sun["azimuth"]), math.radians(sun["elevation"])
        L = (math.sin(az) * math.cos(el), math.cos(az) * math.cos(el), math.sin(el))
        dirs = [(0.0, 0.0, 1.0)]
        for e in (18.0, 42.0, 68.0):
            for i in range(10):
                a = math.radians(i * 36.0 + (18.0 if e == 42.0 else 0.0))
                dirs.append((math.sin(a) * math.cos(math.radians(e)), math.cos(a) * math.cos(math.radians(e)), math.sin(math.radians(e))))
        norm_up = sum(d[2] for d in dirs)
        cache = {}
        for surf in self.surfaces:
            if surf["kind"] in ("adjoin", "water", "sky"):
                surf["light"] = [1.0 if surf["kind"] in ("water", "sky") else 0.15] * len(surf["poly"])
                continue
            s = self.sectors[surf["sector"]]
            c = self.cellsx[s["cell"]]
            mult = s["lay"].get("light", c.get("light", 1.0))
            n = surf["normal"]
            cx = sum(p[0] for p in surf["poly"]) / len(surf["poly"])
            cy = sum(p[1] for p in surf["poly"]) / len(surf["poly"])
            cz = sum(p[2] for p in surf["poly"]) / len(surf["poly"])
            out = []
            for p in surf["poly"]:
                key = (surf["sector"], round(p[0], 4), round(p[1], 4), round(p[2], 4), round(n[0], 3), round(n[1], 3), round(n[2], 3))
                if key in cache:
                    out.append(cache[key]); continue
                # nudge inside: along the normal and toward the polygon centre
                P = (p[0] + n[0] * 0.012 + (cx - p[0]) * 0.02, p[1] + n[1] * 0.012 + (cy - p[1]) * 0.02,
                     p[2] + n[2] * 0.012 + (cz - p[2]) * 0.02)
                sky = 0.0
                for d in dirs:
                    w = vdot(n, d)
                    if w > 0 and self._trace(surf["sector"], P, d):
                        sky += w
                val = sun["base"] + sun["sky"] * sky / norm_up
                w = vdot(n, L)
                if w > 0 and self._trace(surf["sector"], P, L):
                    val += sun["strength"] * w
                for lx, ly, lz, li, lr in self.lights:
                    v = (lx - P[0], ly - P[1], lz - P[2])
                    dist = math.sqrt(vdot(v, v))
                    if dist < lr:
                        dd = vnorm(v)
                        w = max(0.0, vdot(n, dd))
                        if w > 0 and self._trace(surf["sector"], P, dd, maxd=math.hypot(v[0], v[1]) if abs(dd[0]) + abs(dd[1]) > 1e-9 else abs(v[2])):
                            val += li * w * (1 - dist / lr) ** 2
                if s["water"]:
                    val *= sun["underwater"]
                val = max(0.0, min(1.0, val * mult))
                cache[key] = val
                out.append(val)
            surf["light"] = out
        for s in self.sectors:
            vals = [v for surf in s["surfaces"] if surf["kind"] == "floor" for v in surf["light"]] or [0.5]
            s["ambient"] = max(0.15, min(0.95, sum(vals) / len(vals)))

    # ---------- queries ----------
    def sector_at(self, x, y, z=None, layer=None):
        for c in self.cellsx:
            xy = [self.points[p] for p in c["poly"]]
            inside = all((xy[(i + 1) % len(xy)][0] - xy[i][0]) * (y - xy[i][1]) - (xy[(i + 1) % len(xy)][1] - xy[i][1]) * (x - xy[i][0]) >= -1e-9
                         for i in range(len(xy)))
            if not inside:
                continue
            secs = c["sectors"]
            if layer is not None:
                return secs[layer]
            if z is None:
                return secs[-1] if len(secs) > 1 and self.sectors[secs[0]]["water"] else secs[0]
            for si in secs:
                s = self.sectors[si]
                if zat(s["fplane"], x, y) - 1e-4 <= z <= zat(s["cplane"], x, y) + 1e-4:
                    return si
        return None

    def floor_z(self, si, x, y):
        return zat(self.sectors[si]["fplane"], x, y)

    def tagged(self, tag, layer=0):
        hits = [s for s in self.surfaces if s.get("tag") and s["tag"][1] == tag and s["tag"][2] == layer]
        if not hits:
            raise PlanError(f"no surface tagged {tag}")
        def area(s):
            n = newell(s["poly"])
            tot = (0.0, 0.0, 0.0)
            for i, p in enumerate(s["poly"]):
                q = s["poly"][(i + 1) % len(s["poly"])]
                c = vcross(p, q)
                tot = (tot[0] + c[0], tot[1] + c[1], tot[2] + c[2])
            return abs(vdot(n, tot)) / 2
        return max(hits, key=area)["index"]

    # ---------- emit ----------
    def mat_index(self):
        used = []
        for s in self.surfaces:
            if s["mat"] and s["mat"] not in used:
                used.append(s["mat"])
        for a in used:
            if a not in self.mats:
                raise PlanError(f"material alias {a} not declared")
        return used

    def section_materials(self, extra_mats=()):
        used = self.mat_index()
        for m in extra_mats:
            if m not in used:
                used.append(m)
        files, self._mat_slot = [], {}
        for a in used:          # aliases that name the same file share one MATERIALS entry
            f = self.mats[a][0]
            if f not in files:
                files.append(f)
            self._mat_slot[a] = files.index(f)
        self._mat_order = used
        lines = ["SECTION: MATERIALS", "World materials 640"]
        lines += [f"{i}: {f} 1.000000 1.000000" for i, f in enumerate(files)]
        lines += ["end", ""]
        return "\n".join(lines) + "\n"

    def _uv(self, surf, p):
        mat = surf["mat"]
        scale = self.mats[mat][1] if mat in self.mats else 256.0
        n = surf["normal"]
        if surf.get("uvfit"):
            (ax, ay), (bx, by), z0, z1, (wpx, hpx) = surf["uvfit"]
            L2 = (bx - ax) ** 2 + (by - ay) ** 2
            t = max(0.0, min(1.0, ((p[0] - ax) * (bx - ax) + (p[1] - ay) * (by - ay)) / L2))
            zlo, zhi = z0[0] + (z0[1] - z0[0]) * t, z1[0] + (z1[1] - z1[0]) * t
            return ((1.0 - t) * wpx, (zhi - p[2]) / (zhi - zlo) * hpx)
        if abs(n[2]) > 0.7:
            return (p[0] * scale, p[1] * scale)
        A, B = surf["wall"] if surf.get("wall") else ((0, 0), (1, 0))
        dx, dy = B[0] - A[0], B[1] - A[1]
        L = math.hypot(dx, dy) or 1.0
        return ((p[0] * dx + p[1] * dy) / L * scale, -p[2] * scale)

    def section_georesource(self, colormap):
        order = self._mat_slot
        lines = ["SECTION: GEORESOURCE", "World Colormaps 1", f"0: {colormap}", f"World vertices {len(self.vertices)}"]
        ox, oy = getattr(self, "offset", (0.0, 0.0))
        lines += [f"{i}: {x + ox:.6f} {y + oy:.6f} {z:.6f}" for i, (x, y, z) in enumerate(self.vertices)]
        tv = []
        surf_lines = []
        for s in self.surfaces:
            refs = []
            for k, p in enumerate(s["poly"]):
                if s["kind"] == "adjoin":
                    refs.append(f"{s['vidx'][k]},-1")
                else:
                    u, v = self._uv(s, p)
                    refs.append(f"{s['vidx'][k]},{len(tv)}")
                    tv.append((u, v))
            light = " ".join(f"{v:.6f}" for v in s["light"])
            adj = s.get("adj", -1)
            if s["kind"] == "adjoin":
                surf_lines.append(f"{s['index']}: -1 0x0 0x0 0 3 1 {adj} 0.000000 {len(s['poly'])} {' '.join(refs)} {light}")
            elif s["kind"] == "water":
                surf_lines.append(f"{s['index']}: {order['water']} 0x0 0x{WATER_FACE:x} 4 0 1 {adj} 0.000000 {len(s['poly'])} {' '.join(refs)} {light}")
            elif s["kind"] == "sky":
                sky_flags = getattr(self, "sky_flags", SKY_FLAGS)     # 0x404 ceiling sky, 0x204 horizon sky
                surf_lines.append(f"{s['index']}: {order['sky']} 0x{sky_flags:x} 0x0 4 0 1 -1 0.000000 {len(s['poly'])} {' '.join(refs)} {light}")
            else:
                surf_lines.append(f"{s['index']}: {order[s['mat']]} 0x{s['flags']:x} 0x0 4 3 1 -1 0.000000 {len(s['poly'])} {' '.join(refs)} {light}")
        lines.append(f"World texture vertices {len(tv)}")
        lines += [f"{i}: {u:.6f} {v:.6f}" for i, (u, v) in enumerate(tv)]
        lines.append(f"World adjoins {len(self.adjoins)}")
        for i, a in enumerate(self.adjoins):
            d = math.dist(self.sectors[a["sector"]]["center"], self.sectors[a["to"]]["center"])
            lines.append(f"{i}: 0x7 {a['mirror']} {d:.6f}")
        lines.append(f"World surfaces {len(self.surfaces)}")
        lines += surf_lines
        lines.append("# Surface normals")
        lines += [f"{s['index']}: {s['normal'][0]:.9f} {s['normal'][1]:.9f} {s['normal'][2]:.9f}" for s in self.surfaces]
        lines.append("")
        return "\n".join(lines) + "\n"

    def section_sectors(self):
        lines = ["SECTION: SECTORS", f"World sectors {len(self.sectors)}"]
        for i, s in enumerate(self.sectors):
            lay = s["lay"]
            flags = (0x2 if s["water"] else 0) | lay.get("flags", 0)
            tint = lay.get("tint", (0, 0, 0))
            lo, hi = s["bbox"]
            ox, oy = getattr(self, "offset", (0.0, 0.0))
            lo, hi = [lo[0] + ox, lo[1] + oy, lo[2]], [hi[0] + ox, hi[1] + oy, hi[2]]
            lines += [f"SECTOR {i}", f"FLAGS 0x{flags:x}", f"AMBIENT LIGHT {s['ambient']:.2f}",
                      f"EXTRA LIGHT {lay.get('extra', 0.0):.2f}", "COLORMAP 0",
                      "TINT " + " ".join(f"{t:g}" for t in tint),
                      f"BOUNDBOX {lo[0]:.6f} {lo[1]:.6f} {lo[2]:.6f} {hi[0]:.6f} {hi[1]:.6f} {hi[2]:.6f}"]
            if lay.get("sound"):
                lines.append(f"SOUND {lay['sound'][0]} {lay['sound'][1]:.2f}")
            lines += [f"CENTER {s['center'][0] + ox:.6f} {s['center'][1] + oy:.6f} {s['center'][2]:.6f}",
                      f"RADIUS {s['radius']:.6f}", f"VERTICES {len(s['vset'])}"]
            lines += [f"{k}: {v}" for k, v in enumerate(s["vset"])]
            lines.append(f"SURFACES {s['first']} {len(s['surfaces'])}")
        lines.append("")
        return "\n".join(lines) + "\n"

    def section_things_lines(self):
        out = []
        for t in sorted(self.things, key=lambda t: t["index"]):
            si = self.sector_at(t["x"], t["y"], layer=t["layer"])
            if si is None:
                raise PlanError(f"thing {t['index']} {t['name']} at ({t['x']}, {t['y']}) is outside the level")
            z = t["z"] if t["z"] is not None else self.floor_z(si, t["x"], t["y"]) + t["above"]
            ox, oy = getattr(self, "offset", (0.0, 0.0))
            line = (f"{t['index']}: {t['template']} {t['name']} {t['x'] + ox:.6f} {t['y'] + oy:.6f} {z:.6f} "
                    f"{t['pitch']:.6f} {t['yaw']:.6f} {t['roll']:.6f} {si}")
            if t["extra"]:
                line += " " + t["extra"]
            out.append(line)
        return out

    # ---------- checks ----------
    def check(self):
        problems = []
        # sealed seams: a wall edge that lies along another cell's edge without sharing its points
        segs = []
        for ci, c in enumerate(self.cellsx):
            poly = c["poly"]
            for i, a in enumerate(poly):
                segs.append((ci, self.points[a], self.points[poly[(i + 1) % len(poly)]]))
        solid, opened = set(), set()
        for s in self.sectors:
            for A, B, opens in s["edges"]:
                (opened if opens else solid).add((s["cell"], A, B))
        solid -= opened            # stacked cells: one open layer is enough
        for ci, A, B in segs:
            if (ci, A, B) not in solid:
                continue
            dx, dy = B[0] - A[0], B[1] - A[1]
            L2 = dx * dx + dy * dy
            for cj, C, D in segs:
                if cj == ci:
                    continue
                # C and D close to the line AB and overlapping it
                def off(P):
                    t = ((P[0] - A[0]) * dx + (P[1] - A[1]) * dy) / L2
                    return t, abs((P[0] - A[0]) * dy - (P[1] - A[1]) * dx) / math.sqrt(L2)
                (tc, ec), (td, ed) = off(C), off(D)
                if ec < 2e-3 and ed < 2e-3 and min(max(tc, td), 1) - max(min(tc, td), 0) > 1e-3:
                    problems.append(f"sealed seam: {self.cellsx[ci]['name']} {A}->{B} overlaps {self.cellsx[cj]['name']}")
                    break
        # overlapping cells: an edge of one cell properly crossing an edge of another (shared points excepted)
        def cross(o, a, b):
            return (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0])
        boxes = []
        for c in self.cellsx:
            xy = [self.points[q] for q in c["poly"]]
            boxes.append((min(x for x, _ in xy), min(y for _, y in xy), max(x for x, _ in xy), max(y for _, y in xy), xy))
        for i in range(len(boxes)):
            for j in range(i + 1, len(boxes)):
                bi, bj = boxes[i], boxes[j]
                if bi[2] < bj[0] or bj[2] < bi[0] or bi[3] < bj[1] or bj[3] < bi[1]:
                    continue
                hit = False
                for k in range(len(bi[4])):
                    p1, p2 = bi[4][k], bi[4][(k + 1) % len(bi[4])]
                    for m in range(len(bj[4])):
                        q1, q2 = bj[4][m], bj[4][(m + 1) % len(bj[4])]
                        if len({p1, p2, q1, q2}) < 4:
                            continue
                        d1, d2 = cross(q1, q2, p1), cross(q1, q2, p2)
                        d3, d4 = cross(p1, p2, q1), cross(p1, p2, q2)
                        if ((d1 > 1e-6 and d2 < -1e-6) or (d1 < -1e-6 and d2 > 1e-6)) and ((d3 > 1e-6 and d4 < -1e-6) or (d3 < -1e-6 and d4 > 1e-6)):
                            hit = True
                            break
                    if hit:
                        break
                if hit:
                    problems.append(f"overlap: {self.cellsx[i]['name']} crosses {self.cellsx[j]['name']}")
        for i, a in enumerate(self.adjoins):
            m = a["mirror"]
            if m is None or self.adjoins[m]["mirror"] != i:
                problems.append(f"adjoin {i} mirror broken")
                continue
            sa, sb = self.surfaces[a["surface"]], self.surfaces[self.adjoins[m]["surface"]]
            if sorted(sa["vidx"]) != sorted(sb["vidx"]):
                problems.append(f"adjoin {i}: mirror polygon differs")
            if vdot(sa["normal"], sb["normal"]) > -0.999:
                problems.append(f"adjoin {i}: normals not opposite")
        for s in self.surfaces:
            n = s["normal"]
            p0 = s["poly"][0]
            for p in s["poly"]:
                if abs(vdot(n, vsub(p, p0))) > 1e-4:
                    problems.append(f"surface {s['index']} not planar")
                    break
        return problems

    def stats(self):
        kinds = {}
        for s in self.surfaces:
            kinds[s["kind"]] = kinds.get(s["kind"], 0) + 1
        return dict(cells=len(self.cellsx), sectors=len(self.sectors), surfaces=len(self.surfaces),
                    vertices=len(self.vertices), adjoins=len(self.adjoins), kinds=kinds)


def render_plan(level, path, scale=14, things=True, title=None, mode="height"):
    """Top-down plan: floors coloured by height (or by baked light), walls black, adjoins grey, water blue."""
    from PIL import Image, ImageDraw
    xs = [p[0] for p in level.points.values()]
    ys = [p[1] for p in level.points.values()]
    x0, x1, y0, y1 = min(xs) - 2, max(xs) + 2, min(ys) - 2, max(ys) + 2
    W, H = int((x1 - x0) * scale), int((y1 - y0) * scale)
    img = Image.new("RGB", (W, H + 30), (18, 18, 22))
    d = ImageDraw.Draw(img)
    tx = lambda x, y: ((x - x0) * scale, (y1 - y) * scale)
    zs = [zat(s["fplane"], *level.points[level.cellsx[s["cell"]]["poly"][0]]) for s in level.sectors]
    zlo, zhi = min(zs), max(zs)
    for s in level.sectors:
        c = level.cellsx[s["cell"]]
        if s["layer"] != len(c["sectors"]) - 1 and not s["water"]:
            continue            # draw the top layer of stacks (bridge deck), but water layers show blue
        poly = [tx(*level.points[p]) for p in c["poly"]]
        fl = [v for surf in s["surfaces"] if surf["kind"] == "floor" for v in surf["light"]]
        lit = sum(fl) / len(fl) if fl else 0.5
        if s["water"]:
            col = (40, 90, 170)
        elif mode == "light":
            g = int(255 * lit)
            col = (g, int(g * 0.85), int(g * 0.7))
        else:
            zc = sum(zat(s["fplane"], *level.points[p]) for p in c["poly"]) / len(c["poly"])
            t = (zc - zlo) / ((zhi - zlo) or 1)
            col = (int(90 + 150 * t), int(150 - 60 * t), int(80 - 40 * t))
        if len(c["sectors"]) > 1 and not s["water"] and not level.sectors[c["sectors"][0]]["water"]:
            col = tuple(min(255, v + 40) for v in col)
        d.polygon(poly, fill=col)
    for s in level.sectors:
        c = level.cellsx[s["cell"]]
        for A, B, opens in s["edges"]:
            a, b = tx(*A), tx(*B)
            if opens:
                d.line([a, b], fill=(70, 70, 70), width=1)
            else:
                d.line([a, b], fill=(0, 0, 0), width=3)
    if things:
        for t in level.things:
            x, y = tx(t["x"], t["y"])
            col = (255, 60, 60) if t["template"].startswith("kr_pair") else (255, 255, 255) if t["template"] in ("walkplayer", "kr_kell") else (250, 210, 60)
            d.ellipse([x - 3, y - 3, x + 3, y + 3], fill=col)
            if not t["template"].startswith("+"):
                d.text((x + 4, y - 6), t["name"][:10], fill=col)
    d.text((6, H + 8), title or level.title, fill=(230, 230, 230))
    for gx in range(int(x0) // 5 * 5, int(x1) + 1, 5):
        d.text(tx(gx, y0 + 0.6), str(gx), fill=(120, 120, 120))
    for gy in range(int(y0) // 5 * 5, int(y1) + 1, 5):
        d.text(tx(x0 + 0.2, gy), str(gy), fill=(120, 120, 120))
    img.save(path)
