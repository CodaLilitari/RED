"""Read the bounded native geometry subset used by the planar walkway check."""
import hashlib
import math
import re
from pathlib import Path


def require(condition, message):
    if not condition:
        raise ValueError(message)


def numbers(words):
    values = tuple(float(word) for word in words)
    require(all(math.isfinite(v) and abs(v) <= 1e6 for v in values), "Invalid world coordinate or angle")
    return values


def document(path, maximum):
    with path.open("rb") as stream:
        data = stream.read(maximum + 1)
    require(len(data) <= maximum, f"{path.name} exceeds {maximum} bytes")
    rows = [line.partition("#")[0].strip() for line in data.decode("latin-1").splitlines()]
    require(all(len(line) <= 65536 for line in rows), "Oversized source record")
    return [line for line in rows if line], hashlib.sha256(data).hexdigest()


def counted(lines, label, limit=150000):
    starts = [(i, m) for i, row in enumerate(lines)
              if (m := re.fullmatch(label + r"\s+(\d+)", row, re.I))]
    require(len(starts) == 1, f"Expected one {label} table")
    start, match = starts[0]
    count = int(match[1])
    require(count <= limit and start + count < len(lines), f"Invalid {label} count")
    values = []
    for index, row in enumerate(lines[start + 1:start + count + 1]):
        number, sep, body = row.partition(":")
        require(sep and int(number) == index, f"Unexpected {label} row {index}")
        values.append(body.strip())
    return values


def chunks(lines, pattern):
    starts = [(i, m[1].lower()) for i, line in enumerate(lines)
              if (m := re.fullmatch(pattern, line, re.I))]
    return [(key, lines[i+1:end]) for (i, key), (end, _) in zip(starts, starts[1:] + [(len(lines), "")])]


def corners(source, index, count, vertices):
    # Native JKL and 3DO rows use vertex,uv, vertex, uv or vertex uv.
    # Consume the UV even when the geometry check only needs the vertex.
    require(2 < count <= 256, "Unsupported face corner count")
    tokens = re.sub(r"\s*,\s*", ",", source).split()
    result = []
    for _ in range(count):
        require(index < len(tokens), "Truncated face record")
        vertex, separator, uv = tokens[index].partition(",")
        index += 1
        if not separator:
            require(index < len(tokens), "Missing face texture vertex")
            uv = tokens[index]
            index += 1
        require(re.fullmatch(r"-?[0-9]+", uv), "Invalid face texture vertex")
        vi = int(vertex)
        require(0 <= vi < len(vertices), "Invalid face vertex index")
        result.append(vertices[vi])
    return result


def jkl_flags(token):
    # Native surface and adjoin tables store hexadecimal masks even without 0x.
    require(re.fullmatch(r"(?:0[xX])?[0-9a-fA-F]+", token), "Invalid JKL flag mask")
    mask = int(token, 16)
    require(mask <= 0xffffffff, "JKL flag mask exceeds 32 bits")
    return mask


def options(tokens):
    return {name.lower(): value.lower() for token in tokens
            if "=" in token for name, value in [token.split("=", 1)]}


def load_level(path, sector, z):
    lines, digest = document(path, 16 << 20)
    sec = dict(chunks(lines, r"section:\s*(\w+)"))
    require(all(n in sec for n in ("georesource", "sectors", "things", "templates")),
            "JKL requires GEORESOURCE, SECTORS, THINGS and TEMPLATES sections")
    geo = sec["georesource"]
    vertices = [numbers(row.split()[:3]) for row in counted(geo, "world vertices")]
    adjoins = [jkl_flags(row.split()[0]) for row in counted(geo, "world adjoins")]
    surfaces = counted(geo, "world surfaces")
    owners = [body for key, body in chunks(sec["sectors"], r"sector\s+(\d+)") if int(key) == sector]
    require(len(owners) == 1, "Selected sector is missing or duplicated")
    ranges = [line.split() for line in owners[0] if line.lower().startswith("surfaces ")]
    require(len(ranges) == 1, "Missing or ambiguous sector surface range")
    first, count = map(int, ranges[0][1:])
    require(0 <= first <= first+count <= len(surfaces), "Invalid sector surface range")
    floors, walls = [], []
    for row in surfaces[first:first+count]:
        fields = row.split()
        flags, adjoin = jkl_flags(fields[1]), int(fields[6])
        require(-1 <= adjoin < len(adjoins), "Invalid adjoin index")
        face = corners(row, 9, int(fields[8]), vertices)
        require(all(len(v) == 3 for v in face), "Invalid level vertex")
        if adjoin < 0:
            blocked = bool(flags & 0x04)
        else:
            portal = adjoins[adjoin]
            blocked = not (portal & 0x02) or bool(portal & 0x10)
        if blocked and flags & 1 and all(abs(v[2]-z) <= 1e-5 for v in face):
            floors.append(face)
        elif blocked:
            walls.append(face)
    require(floors, "No flat floor-flagged surface at that Z in the selected sector")

    templates = {}
    for line in sec["templates"]:
        f = line.split()
        if len(f) >= 2 and f[0].lower() != "world":
            require(f[0].lower() not in templates, "Duplicate template definition")
            templates[f[0].lower()] = (f[1].lower(), options(f[2:]))
    things = []
    for row in sec["things"]:
        fields = row.split()
        if not re.fullmatch(r"\d+:", fields[0]):
            continue
        require(len(fields) >= 10, "Truncated Thing placement")
        if int(fields[9]) != sector:
            continue
        typ = fields[1].lower()
        props = options(fields[10:])
        seen = set()
        while typ != "none":
            require(typ in templates and typ not in seen, f"Missing or cyclic Thing template {typ}")
            seen.add(typ)
            typ, inherited = templates[typ]
            props = {**inherited, **props}
        things.append((fields[2], numbers(fields[3:9]), props))
    return floors, walls, things, digest


def native_mesh(path):
    lines, digest = document(path, 8 << 20)
    geo = dict(chunks(lines, r"section:\s*(\w+)"))
    require("geometrydef" in geo and "hierarchydef" in geo, f"Incomplete model {path.name}")
    part = geo["geometrydef"]
    require(any(row.lower() == "geosets 1" for row in part), "Model must have one GEOSET")
    offsets = [numbers(row.split()[2:]) for row in part if row.lower().startswith("insert offset ")]
    require(all(off == (0,0,0) for off in offsets), f"Unsupported insertion offset in {path.name}")
    nodes = counted(geo["hierarchydef"], "hierarchy nodes", 512)
    assigned = []
    for row in nodes:
        fields = row.split()
        require(len(fields) >= 17 and numbers(fields[7:16]) == (0,)*9,
                f"Transformed hierarchy in {path.name}; static identity bind transforms required")
        if int(fields[2]) >= 0:
            assigned.append(int(fields[2]))
    faces = []
    meshes = chunks(part, r"mesh\s+(\d+)")
    require(sorted(assigned) == sorted(int(k) for k, _ in meshes), f"Ambiguous hierarchy in {path.name}")
    for _, mesh in meshes:
        verts = [numbers(row.split()[:3]) for row in counted(mesh, "vertices", 32000)]
        for row in counted(mesh, "faces", 32000):
            face = corners(row, 7, int(row.split()[6]), verts)
            require(all(len(v) == 3 for v in face), "Invalid model vertex")
            faces.append(face)
    require(0 < len(faces) <= 32000, "Oversized or empty static model")
    return faces, digest


def slab(poly, low, high):
    for height, direction in [(low, 1), (high, -1)]:
        out = []
        for a, b in zip(poly, poly[1:]+poly[:1]):
            a_in = (a[2]-height)*direction >= 0
            b_in = (b[2]-height)*direction >= 0
            if a_in:
                out.append(a)
            if a_in != b_in:
                t = (height-a[2])/(b[2]-a[2])
                out.append((a[0]+t*(b[0]-a[0]), a[1]+t*(b[1]-a[1]), height))
        poly = out
    return poly
