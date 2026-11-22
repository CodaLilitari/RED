"""Write an offline section through captured Raven and Jedi Knight collision geometry."""

import argparse
from html import escape
import json
import math
from pathlib import Path
import subprocess


def capture(executable, *args):
    answer = subprocess.run(
        [str(executable), "jo-ja-inspect", *map(str, args)],
        check=True,
        capture_output=True,
        timeout=45,
    )
    return json.loads(answer.stdout)


def intersect_face(points, axis, depth):
    """Intersect a polygon with a plane; return a coplanar outline or physical segments."""
    crossings = []
    for left, right in zip(points, points[1:] + points[:1]):
        a, b = left[axis] - depth, right[axis] - depth
        if abs(a) < 1e-8:
            crossings.append(left)
        elif a * b < 0:
            ratio = a / (a - b)
            crossings.append([x + (y - x) * ratio for x, y in zip(left, right)])
    if len(crossings) >= 3 and all(abs(v[axis] - depth) < 1e-8 for v in points):
        return [points + [points[0]]]
    unique = []
    for v in crossings:
        if all(math.dist(v, p) > 1e-8 for p in unique):
            unique.append(v)
    return [unique] if len(unique) == 2 else []


def clip_section(polygon, normal, distance):
    result = []
    for left, right in zip(polygon, polygon[1:] + polygon[:1]):
        a = sum(x * y for x, y in zip(normal, left)) - distance
        b = sum(x * y for x, y in zip(normal, right)) - distance
        if a <= 0:
            result.append(left)
        if (a < 0 < b) or (b < 0 < a):
            t = a / (a - b)
            result.append([x + t * (y - x) for x, y in zip(left, right)])
    return result


def require_native_faces(model):
    if model.get("faces") and model.get("geometryBasis") != "rawGeoset0":
        raise ValueError(
            "Native model faces require a rawGeoset0 receipt; use the matching RED agent executable"
        )


def render(args):
    src = args.source.resolve()
    target = args.archive.resolve()
    probe = lambda *cmd: capture(args.agent.resolve(), *cmd)
    target_cmd = (target, "--target", args.level, args.sector, args.thing)
    first = probe(*target_cmd)
    native = first["native"]
    model = first["model"]
    worlds = list(native["surfaces"])
    faces = list(model.get("faces", []))
    if model["shape"] == "faces":
        require_native_faces(model)
    fingerprints = (first["archiveSha256"], first["levelSha256"])

    def more(surface, face):
        page = probe(*target_cmd, surface, face)
        if (page["archiveSha256"], page["levelSha256"]) != fingerprints:
            raise ValueError("The target changed during inspection")
        return page

    def append_pages(which, rows, first_page, field):
        page = first_page[which]
        while page.get("nextOffset") is not None:
            if len(rows) >= 8192:
                raise ValueError("Selected section exceeds 8192 polygons")
            key = page["nextOffset"]
            page = more(key if which == "native" else 0, key if which == "model" else 0)[which]
            if which == "model" and model["shape"] == "faces":
                require_native_faces(page)
            rows.extend(page[field])

    append_pages("native", worlds, first, "surfaces")
    append_pages("model", faces, first, "faces")
    if not args.scale > 0 or not math.isfinite(args.scale) or not math.isfinite(args.depth):
        raise ValueError("Finite depth and positive source scale required")
    axis = {"x": 0, "y": 1, "z": 2}[args.axis]
    u, v = [i for i in range(3) if i != axis]
    points = [pt for face in worlds for pt in face["polygon"]]
    if not points:
        raise ValueError("The chosen sector is empty")
    xmin, xmax = min(q[u] for q in points), max(q[u] for q in points)
    ymin, ymax = min(q[v] for q in points), max(q[v] for q in points)
    pad = max(xmax - xmin, ymax - ymin, 0.01) * 0.05
    xmin -= pad
    xmax += pad
    ymin -= pad
    ymax += pad
    w, h = xmax - xmin, ymax - ymin
    width, height = 850, 540
    scale = min(width / w, height / h)

    def project(p):
        return (
            (width - w * scale) / 2 + (p[u] - xmin) * scale,
            (height - h * scale) / 2 + (ymax - p[v]) * scale,
        )

    def polyline(points, closed=False):
        coords = " ".join(f"{x:.3f},{y:.3f}" for x, y in map(project, points))
        tag = "polygon" if closed else "polyline"
        return f'<{tag} points="{coords}"/>'

    pieces = {k: [] for k in ("source", "wall", "portal", "model", "player", "projectile")}

    def world(x, y):
        out = [0.0, 0.0, 0.0]
        out[axis] = args.depth
        out[u] = x
        out[v] = y
        return out

    clip = [world(xmin, ymin), world(xmax, ymin), world(xmax, ymax), world(xmin, ymax)]
    read = probe(src, "--brush", args.brush)
    identity = read["sourceSha256"]
    brush = read["collisionBrush"]
    while True:
        for side in brush["sides"]:
            clip = clip_section(clip, side["normal"], side["distance"] * args.scale)
        if brush["nextOffset"] is None:
            break
        read = probe(src, "--brush", args.brush, brush["nextOffset"])
        if read["sourceSha256"] != identity:
            raise ValueError("The source changed during inspection")
        brush = read["collisionBrush"]
    if clip:
        pieces["source"].append(polyline(clip, True))
    for face in worlds:
        group = "portal" if face["boundary"] == "portal" else "wall"
        for seg in intersect_face(face["polygon"], axis, args.depth):
            pieces[group].append(polyline(seg))
    if model["shape"] == "faces":
        for face in faces:
            for seg in intersect_face(face["polygon"], axis, args.depth):
                pieces["model"].append(polyline(seg))
    explanations = []
    for key, selector, anchor in (
        ("player", args.player, None),
        ("projectile", args.projectile, args.projectile_origin),
    ):
        if selector is None:
            continue
        row = probe(target, "--target", args.level, args.sector, selector)
        if (row["archiveSha256"], row["levelSha256"]) != fingerprints:
            raise ValueError("Profile archive changed")
        obj = row["model"]
        profile = obj["profile"]
        if profile.get("type") != ("player" if key == "player" else "weapon"):
            raise ValueError(f"Selected {key} profile has the wrong engine type")
        if row["templateOnly"] and anchor is None:
            raise ValueError(f"An explicit placement is required for {key}")
        center = anchor if anchor is not None else obj["position"]
        explanations.append(
            f"{key}: {obj['template']}, {obj['shape']}, movesize {profile.get('movesize', 'unresolved')}, "
            f"height {profile.get('height', 'inherited/default')}, model {obj['modelGeometry']}, "
            f"floor search reference {obj.get('attachedFloorSearchHeight', 'unresolved')}"
        )
        if obj["shape"] != "sphere" or "movesize" not in profile:
            continue
        radius = float(profile["movesize"])
        delta = center[axis] - args.depth
        if abs(delta) <= radius:
            x, y = project(center)
            pieces[key].append(
                f'<circle cx="{x:.3f}" cy="{y:.3f}" r="{scale * math.sqrt(max(0, radius * radius - delta * delta)):.3f}"/>'
            )
    labels = {
        "source": "Source brush",
        "wall": "Native exterior",
        "portal": "Native portal",
        "model": "Native model faces (geoset 0)",
        "player": "Player movement sphere",
        "projectile": "Projectile movement sphere",
    }
    colors = {
        "source": "#ea9b2e",
        "wall": "#9ed0df",
        "portal": "#60d7a3",
        "model": "#d094ff",
        "player": "#f3e584",
        "projectile": "#ff6767",
    }
    css = []
    switches = []
    layers = []
    for k, shapes in pieces.items():
        css.append(
            f"#{k}:not(:checked)~svg .{k}{{display:none}} .{k}{{stroke:{colors[k]};stroke-width:2;fill:none}}"
        )
        switches.append(
            f'<input id="{k}" type="checkbox" checked><label for="{k}">{labels[k]}</label> '
        )
        layers.append(f'<g class="{k}">' + "".join(shapes) + "</g>")
    css.append(
        ".source polygon{fill:#ea9b2e33}.portal{stroke-dasharray:7 4} body{background:#171b1d;color:#e2e9e9;font:16px system-ui;max-width:920px;margin:2em auto}svg{width:100%;background:#101416;margin-top:1em}label{margin-right:.6em}code{overflow-wrap:anywhere}"
    )
    flags = (
        ", ".join(k for k in ("solid", "water", "playerClip", "shotClip", "noShot") if brush[k])
        or "none of the listed filters"
    )
    details = [
        f"Source brush {args.brush}: {brush['shaderName']} ({flags}); linked draw surface: {brush['hasLinkedDrawSurface']}.",
        f"Target {args.level}, sector {args.sector}, Thing {args.thing}: {model['shape']}.",
        f"Model polygon basis: {model.get('geometryBasis', 'unavailable')}. "
        "Only a face-collision shape supplies the native model-face layer.",
        *explanations,
        f"Mapper decision: {args.decision}" if args.decision else "Mapper decision: pending.",
        "Captured Thing-pose section. Live contacts and traversal require an engine scenario.",
        f"Source BSP SHA-256 {identity}",
        f"Target archive SHA-256 {fingerprints[0]}",
    ]
    return (
        '<!doctype html><meta charset="utf-8"><title>Collision section</title><style>'
        + "".join(css)
        + "</style><h1>Collision section</h1><p>"
        + escape(f"{args.axis.upper()} = {args.depth:g} JK units; source scale {args.scale:g}.")
        + "</p>"
        + "".join(switches)
        + '<svg role="img" aria-label="Collision geometry section" viewBox="0 0 850 540">'
        + "".join(layers)
        + "</svg>"
        + "".join("<p>" + escape(s) + "</p>" for s in details)
    )


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ("agent", "source", "archive", "output"):
        p.add_argument("--" + name, required=True, type=Path)
    p.add_argument("--level", required=True)
    p.add_argument("--brush", type=int, required=True)
    p.add_argument("--sector", type=int, required=True)
    p.add_argument("--thing", required=True)
    p.add_argument("--scale", required=True, type=float)
    p.add_argument("--axis", choices=["x", "y", "z"], default="z")
    p.add_argument("--depth", required=True, type=float)
    p.add_argument("--player")
    p.add_argument("--projectile")
    p.add_argument("--projectile-origin", type=float, nargs=3)
    p.add_argument("--decision")
    args = p.parse_args()
    report = render(args)
    with args.output.open("x", encoding="utf-8") as dest:
        dest.write(report)


if __name__ == "__main__":
    main()
