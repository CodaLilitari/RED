"""Reproduce the original two-LOD model, indexed MAT and short mono sound."""
import io
import math
from pathlib import Path
import struct
import wave

ROOT = Path(__file__).resolve().parent


def mesh(index, name, size, detailed):
    points = [(0, 0, 0), (size, 0, 0), (0, size, 0)]
    if detailed:
        points.append((size, size, 0))
    triangles = [(0, 1, 2)] + ([(1, 3, 2)] if detailed else [])
    rows = [f"MESH {index}", f"NAME {name}", "RADIUS 0.1", "GEOMETRYMODE 4",
            "LIGHTINGMODE 3", "TEXTUREMODE 3", f"VERTICES {len(points)}"]
    rows += [f"{i}: {x:.6f} {y:.6f} {z:.6f} 1.000000" for i, (x, y, z) in enumerate(points)]
    rows += [f"TEXTURE VERTICES {len(points)}"]
    rows += [f"{i}: {int(x != 0)} {int(y != 0)}" for i, (x, y, _) in enumerate(points)]
    rows += ["VERTEX NORMALS"] + [f"{i}: 0 0 1" for i in range(len(points))]
    rows += [f"FACES {len(triangles)}"]
    rows += [f"{i}: 0 0x0 4 3 3 0.000000 3 " + " ".join(f"{v},{v}" for v in triangle)
             for i, triangle in enumerate(triangles)]
    return rows + ["FACE NORMALS"] + [f"{i}: 0 0 1" for i in range(len(triangles))]


def model(count):
    rows = ["SECTION: HEADER", "3DO 2.1", "SECTION: MODELRESOURCE", "MATERIALS 1", "0: relay_skin.mat",
            "SECTION: GEOMETRYDEF", "RADIUS 0.15", "INSERT OFFSET 0 0 0", f"GEOSETS {count}"]
    for lod in range(count):
        rows += [f"GEOSET {lod}", "MESHES 2"]
        rows += mesh(0, "base", 0.06, lod == 0)
        rows += mesh(1, "boom", 0.03, lod == 0)
    rows += ["SECTION: HIERARCHYDEF", "HIERARCHY NODES 2",
             "0: 0x0000 0x00001 0 -1 1 -1 1 0 0 0 0 0 0 0 0 0 base",
             "1: 0x0000 0x00001 1 0 -1 -1 0 0.02 0 0.04 0 0 0 0.01 0 0 boom"]
    return ("\n".join(rows) + "\n").encode("ascii")


def write_generated():
    (ROOT / "relay_sentry.3do").write_bytes(model(2))
    (ROOT / "relay_single.3do").write_bytes(model(1))
    fields = [0x32, 2, 1, 1, 0, 8, *([0] * 12), 8, 25, *([0] * 8), 2, 2, 0, 0, 0, 1]
    (ROOT / "relay_skin.mat").write_bytes(b"MAT " + struct.pack(f"<{len(fields)}I", *fields) + bytes([25, 18, 18, 25]))
    audio = io.BytesIO()
    with wave.open(audio, "wb") as out:
        out.setparams((1, 1, 8000, 0, "NONE", "not compressed"))
        out.writeframes(bytes(round(128 + 32 * math.sin(2 * math.pi * n / 16)) for n in range(256)))
    (ROOT / "relay_ping.wav").write_bytes(audio.getvalue())


if __name__ == "__main__":
    write_generated()
