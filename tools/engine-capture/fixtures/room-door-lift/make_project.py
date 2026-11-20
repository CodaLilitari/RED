"""Create the RED relay bay project from its original, redistributable assets."""

import argparse
import hashlib
import json
import math
from pathlib import Path
import struct


HERE = Path(__file__).resolve().parent


def material(colors):
    """Two-by-two indexed MAT with a single transparent-free texture."""
    return (b"MAT " + struct.pack("<6I", 0x32, 2, 1, 1, 0, 8) + bytes(48)
            + struct.pack("<10I", 8, 0, 0, 0, 0, 0, 0, 0, 0, 0)
            + struct.pack("<6I", 2, 2, 0, 0, 0, 1) + bytes(colors))


def grayscale():
    palette = bytes(channel for index in range(256) for channel in (index,) * 3)
    levels = bytes(round(index * light / 63) for light in range(64) for index in range(256))
    return b"CMP " + bytes(60) + palette + levels


def box(name, half_size, material_name):
    x, y, z = half_size
    points = [(-x, -y, -z), (x, -y, -z), (x, y, -z), (-x, y, -z),
              (-x, -y, z), (x, -y, z), (x, y, z), (-x, y, z)]
    faces = [(3, 2, 1, 0), (4, 5, 6, 7), (0, 1, 5, 4),
             (1, 2, 6, 5), (2, 3, 7, 6), (3, 0, 4, 7)]
    normals = [(0, 0, -1), (0, 0, 1), (0, -1, 0), (1, 0, 0), (0, 1, 0), (-1, 0, 0)]
    radius = math.sqrt(x*x + y*y + z*z)
    rows = ["SECTION: HEADER", "3DO 2.1", "SECTION: MODELRESOURCE", "MATERIALS 1",
            "0: " + material_name, "SECTION: GEOMETRYDEF", f"RADIUS {radius:.6f}",
            "INSERT OFFSET 0 0 0", "GEOSETS 1", "GEOSET 0", "MESHES 1", "MESH 0",
            "NAME " + name, f"RADIUS {radius:.6f}", "GEOMETRYMODE 4", "LIGHTINGMODE 3",
            "TEXTUREMODE 3", "VERTICES 8"]
    rows += [f"{i}: {px} {py} {pz} 1" for i, (px, py, pz) in enumerate(points)]
    rows += ["TEXTURE VERTICES 4", "0: 0 0", "1: 2 0", "2: 2 2", "3: 0 2", "VERTEX NORMALS 8"]
    rows += [f"{i}: 0 0 1" for i in range(8)]
    rows.append("FACES 6")
    for i, face in enumerate(faces):
        pairs = " ".join(f"{vertex},{uv}" for uv, vertex in enumerate(face))
        rows.append(f"{i}: 0 0 4 3 3 0 4 {pairs}")
    rows.append("FACE NORMALS 6")
    rows += [f"{i}: {nx} {ny} {nz}" for i, (nx, ny, nz) in enumerate(normals)]
    rows += ["SECTION: HIERARCHYDEF", "HIERARCHY NODES 1",
             "0: 0x0 0x10 0 -1 -1 -1 0 0 0 0 0 0 0 0 0 0 root"]
    return ("\n".join(rows) + "\n").encode("ascii")


def resource_archive():
    assets = {"mat/rlwall.mat": material((156, 145, 145, 156)),
              "mat/rlmark.mat": material((228, 72, 72, 228)),
              "misc/cmp/rlgray.cmp": grayscale(),
              "3do/rlgate.3do": box("gate", (.478, .022, .5), "rlmark.mat"),
              "3do/rlplatform.3do": box("platform", (.465, .413, .01), "rlmark.mat"),
              "3do/rlpilot.3do": box("pilot", (.02, .02, .03), "rlwall.mat"),
              "misc/snd/rlquiet.snd": b"# Silent original fixture sound class.\n",
              "cog/rlcontrol.cog": (HERE / "rlcontrol.cog").read_text(encoding="ascii").encode("ascii")}
    index = bytearray(b"GOB " + struct.pack("<III", 0x14, 12, len(assets)))
    offset = 16 + len(assets) * 136
    for name, data in assets.items():
        index.extend(struct.pack("<II128s", offset, len(data), name.replace("/", "\\").encode("ascii")))
        offset += len(data)
    return bytes(index) + b"".join(assets.values())


def create(destination):
    destination.mkdir(parents=True, exist_ok=False)
    for name in ("episode.jk", "redlift.jkl", "scenario.json"):
        (destination / name).write_bytes((HERE / name).read_text(encoding="ascii").encode("ascii"))
    archive = resource_archive()
    (destination / "relay-art.gob").write_bytes(archive)
    profile = {"schemaVersion": 1, "episode": "episode.jk", "levels": [{"export": "redlift.jkl"}],
               "target": {"game": "jk", "engine": "OpenJKDF2", "renderer": "OpenGL"},
               "mounts": [{"kind": "project", "path": "relay-art.gob",
                           "sha256": hashlib.sha256(archive).hexdigest()}],
               "scenarios": [{"name": "room-door-lift", "level": "redlift.jkl",
                              "definition": "scenario.json", "receipts": []}]}
    (destination / "red-project.json").write_bytes((json.dumps(profile, indent=2) + "\n").encode("utf-8"))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("destination", type=Path, help="new directory for the editable example project")
    create(parser.parse_args().destination)
