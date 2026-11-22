"""Detach the visible glass faces of the seven pinned untransformed model owners."""
from decimal import Decimal
import re

from verify_episode_pair import require


CORNERS = re.compile(r"(\d+),\s*(\d+)")


def table(block, name):
    pattern = rf"^{name}(?: (\d+))?\n((?:\d+:[^\n]*\n)+)"
    match = re.search(pattern, block, re.M)
    require(match, f"Missing numbered model table {name}")
    rows = [line.split(": ", 1) for line in match[2].splitlines()]
    require([int(index) for index, _ in rows] == list(range(len(rows))), f"Nonsequential {name} rows")
    require(match[1] is None or int(match[1]) == len(rows), f"Invalid {name} row count")
    return match, [row for _, row in rows]


def numbered(name, rows, counted=True):
    heading = f"{name} {len(rows)}" if counted else name
    return heading + "\n" + "".join(f"{i}: {r}\n" for i, r in enumerate(rows))


def model_sections(s):
    geometry = s.index("SECTION: HIERARCHYDEF")
    starts = [m.start() for m in re.finditer(r"^MESH \d+\n", s[:geometry], re.M)]
    blocks = [s[a:b] for a, b in zip(starts, starts[1:] + [geometry])]
    require(blocks and len(blocks) == int(field(s, "MESHES")), "Invalid model mesh count")
    return s[:starts[0]], blocks, s[geometry:]


def field(block, name):
    return re.search(rf"^{name} (.+)$", block, re.M)[1]


def split_face(row):
    start = CORNERS.search(row)
    require(start, "Missing model face corners")
    meta = row[:start.start()].split()
    corners = [(int(v), int(uv)) for v, uv in CORNERS.findall(row[start.start():])]
    require(len(meta) == 7 and len(corners) == int(meta[-1]), "Invalid face corner count")
    return meta, corners


def retained_table(block, name, original, keep, counted):
    m, rows = table(block, name)
    require(rows == original, f"Unexpected donor {name}")
    return block[:m.start()] + numbered(name, [rows[i] for i in keep], counted) + block[m.end():]


def extract_mesh(block, slots, new_index):
    _, faces = table(block, "FACES")
    _, normals = table(block, "FACE NORMALS")
    targets = [i for i, row in enumerate(faces) if int(row.split()[0]) in slots and row.split()[2] != "0"]
    if not targets or field(block, "GEOMETRYMODE") == "0":
        return block, None
    selected = set(targets)
    keep = [i for i in range(len(faces)) if i not in selected]
    require(keep and len(normals) == len(faces), "Expected a mixed mesh and one normal per face")
    donor = retained_table(block, "FACES", faces, keep, True)
    donor = retained_table(donor, "FACE NORMALS", normals, keep, False)
    _, vertices = table(block, "VERTICES")
    _, vnormals = table(block, "VERTEX NORMALS")
    _, uvs = table(block, "TEXTURE VERTICES")
    require(len(vnormals) == len(vertices), "Invalid vertex normal count")
    glfaces = [split_face(faces[i]) for i in targets]
    vids = sorted({v for _, corners in glfaces for v, _ in corners})
    tids = sorted({t for _, corners in glfaces for _, t in corners})
    vm = {v: i for i, v in enumerate(vids)}
    tm = {t: i for i, t in enumerate(tids)}
    face_rows = []
    for meta, corners in glfaces:
        require(meta[1:3] == ["0x2", "4"] and len(corners) == 3, "Unexpected glass face flags or topology")
        face_rows.append(" ".join(["0", *meta[1:], *(f"{vm[v]}, {tm[t]}" for v, t in corners)]))
    # Each source mesh keeps its shading and radius, with only referenced source records carried.
    head = block[:table(block, "VERTICES")[0].start()]
    head = re.sub(r"^MESH \d+", f"MESH {new_index}", head)
    new = (head + numbered("VERTICES", [vertices[i] for i in vids])
           + numbered("TEXTURE VERTICES", [uvs[i] for i in tids])
           + numbered("VERTEX NORMALS", [vnormals[i] for i in vids], False)
           + numbered("FACES", face_rows) + numbered("FACE NORMALS", [normals[i] for i in targets], False) + "\n")
    return donor, new


def split_model(old):
    header, blocks, hierarchy = model_sections(old)
    _, materials = table(header, "MATERIALS")
    slots = {i for i, mat in enumerate(materials) if mat == "ns67.mat"}
    require(len(slots) == 1 and field(header, "INSERT OFFSET") == "0 0 0", "Unexpected glass material or insertion offset")
    retained, glass = [], []
    for block in blocks:
        donor, new = extract_mesh(block, slots, len(glass))
        retained.append(donor)
        if new:
            glass.append(new)
    _, nodes = table(hierarchy, "HIERARCHY NODES")
    require(len(nodes) == len(blocks), "Expected a hierarchy node for every mesh")
    for i, row in enumerate(nodes):
        fields = row.split()
        require(int(fields[2]) == i and all(Decimal(v) == 0 for v in fields[7:16]), "Expected identity node transforms")
    glass_header = (header[:table(header, "MATERIALS")[0].start()] + numbered("MATERIALS", ["ns67.mat"])
                    + header[table(header, "MATERIALS")[0].end():])
    glass_header = re.sub(r"^MESHES \d+", f"MESHES {len(glass)}", glass_header, flags=re.M)
    new_nodes = []
    for i, block in enumerate(glass):
        parent = -1 if i == 0 else 0
        child = 1 if i == 0 and len(glass) > 1 else -1
        sibling = i + 1 if 0 < i < len(glass) - 1 else -1
        children = len(glass) - 1 if i == 0 else 0
        new_nodes.append(f"0x0 0x0 {i} {parent} {child} {sibling} {children} 0 0 0 0 0 0 0 0 0 {field(block,'NAME')}")
    new_model = glass_header + "".join(glass) + "SECTION: HIERARCHYDEF\n" + numbered("HIERARCHY NODES", new_nodes)
    return header + "".join(retained) + hierarchy, new_model
