"""Build one editable gallery and its matching single-player JK episode."""
import argparse
import importlib.util
import math
from pathlib import Path
import struct

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location(
    "relay_art", HERE.parent / "room-door-lift" / "make_project.py")
relay = importlib.util.module_from_spec(spec)
spec.loader.exec_module(relay)


def gray_colormap():
    # The original Relay Bay light table plus a 256 x 256 software blend table.
    header = struct.pack("<4sII3f", b"CMP ", 0x1E, 1, 1, 1, 1) + bytes(40)
    blend = bytes((a + b) // 2 for a in range(256) for b in range(256))
    return header + relay.grayscale()[64:] + blend


def opaque_material(colors, edition):
    if edition == 8:
        return relay.material(colors)
    color_info = (1, 16, 5, 6, 5, 11, 5, 0, 3, 2, 3, 0, 0, 0)
    header = b"MAT " + struct.pack("<18I", 0x32, 2, 1, 1, *color_info)
    # Cel and texture metadata have the same layout as their indexed counterpart.
    layout = relay.material(colors)[76:140]
    vals = []
    for value in colors:
        red_blue = round(value * 31 / 255)
        green = round(value * 63 / 255)
        vals.append((red_blue << 11) | (green << 5) | red_blue)
    return header + layout + struct.pack("<4H", *vals)


def shaft_material():
    """Indexed feathered cutout with four mips; shared by both editions."""
    header = relay.material((1, 1, 1, 1))[:116]
    pixels = bytearray()
    order = (0, 2, 3, 1)
    for size in (32, 16, 8, 4):
        for y in range(size):
            height = 1 - (y + 0.5) / size
            for x in range(size):
                radius = abs(2 * (x + 0.5) / size - 1)
                coverage = min(1, (1 - radius) * 4) * min(1, height * 2.5)
                visible = coverage > (order[(x % 2) + 2 * (y % 2)] + 0.5) / 4
                pixels.append(round(95 + 115 * height * (1 - radius)) if visible else 0)
    return header + struct.pack("<6I", 32, 32, 1, 0, 0, 4) + pixels


def flat_model(name, materials, faces):
    """One mesh. Quad corners are (x,z) in a vertical plane facing the player."""
    rows = ["SECTION: HEADER", "3DO 2.1", "SECTION: MODELRESOURCE",
            f"MATERIALS {len(materials)}"]
    rows += [f"{i}: {mat}" for i, mat in enumerate(materials)]
    points = [point for _, _, _, corners in faces for point in corners]
    radius = max(math.hypot(x, z) for x, z in points) + .0001
    rows += ["SECTION: GEOMETRYDEF", f"RADIUS {radius:.6f}", "INSERT OFFSET 0 0 0",
             "GEOSETS 1", "GEOSET 0", "MESHES 1", "MESH 0", f"NAME {name}",
             f"RADIUS {radius:.6f}", "GEOMETRYMODE 4", "LIGHTINGMODE 0", "TEXTUREMODE 1",
             f"VERTICES {len(points)}"]
    rows += [f"{i}: {x:.5f} 0 {z:.5f} 1" for i, (x, z) in enumerate(points)]
    rows += [f"TEXTURE VERTICES {len(points)}"]
    for i, (_, _, length, _) in enumerate(faces):
        for j, (u, v) in enumerate(((0, length), (length, length), (length, 0), (0, 0))):
            rows.append(f"{4*i+j}: {u} {v}")
    rows += [f"VERTEX NORMALS {len(points)}"] + [f"{i}: 0 -1 0" for i in range(len(points))]
    rows += [f"FACES {len(faces)}"]
    for i, (material, flags, _, _) in enumerate(faces):
        indices = " ".join(f"{j},{j}" for j in range(4*i, 4*i+4))
        rows.append(f"{i}: {material} {flags} 4 0 1 0 4 {indices}")
    rows += [f"FACE NORMALS {len(faces)}"] + [f"{i}: 0 -1 0" for i in range(len(faces))]
    rows += ["SECTION: HIERARCHYDEF", "HIERARCHY NODES 1",
             "0: 0x0 0x10 0 -1 -1 -1 0 0 0 0 0 0 0 0 0 0 root"]
    return ("\n".join(rows) + "\n").encode("ascii")


def rectangle(left, right, bottom, top, material=0):
    return material, "0x1", 2, ((left, bottom), (right, bottom), (right, top), (left, top))


def signage(overlap):
    # The variants share the same badge surface and depth. Only the backing changes.
    if overlap:
        panel = [rectangle(-.1, .1, -.08, .08)]
    else:
        panel = [rectangle(-.1, -.045, -.08, .08), rectangle(.045, .1, -.08, .08),
                 rectangle(-.045, .045, -.08, -.045), rectangle(-.045, .045, .045, .08)]
    panel.append(rectangle(-.045, .045, -.045, .045, 1))
    return flat_model("plaque", ("vgrib.mat", "vgbadge.mat"), panel)


def write_gob(path, entries):
    directory = bytearray(b"GOB " + struct.pack("<III", 0x14, 12, len(entries)))
    offset = 16 + 136 * len(entries)
    for name, data in entries.items():
        directory.extend(struct.pack("<II128s", offset, len(data), name.replace("/", "\\").encode("ascii")))
        offset += len(data)
    with path.open("xb") as stream:
        stream.write(directory)
        for data in entries.values():
            stream.write(data)


def create(destination, edition, control):
    jkl = (HERE / "viewlab.jkl").read_text(encoding="ascii")
    if control == "ordinary-rib":
        jkl = jkl.replace("thingflags=0x8 model3d=vgrib", "thingflags=0x0 model3d=vgrib")
    elif control == "no-shaft":
        jkl = "\n".join(line for line in jkl.split("\n") if not line.startswith("2: vgshaft Shaft "))
        jkl = jkl.replace("3: vgsign Plaque", "2: vgsign Plaque")
    shaft = [(0, "0x23", 32, ((-.01, -.20), (.13, -.20), (.01, .20), (-.13, .20)))]
    entries = {
        "episode.jk": b'"Transparency gallery"\r\nTYPE 1\r\nSEQ 1\r\n0: 1 1 LEVEL viewlab.jkl 0 0 -1 -1\r\nEND\r\n',
        "jkl/viewlab.jkl": jkl.replace("\n", "\r\n").encode("ascii"),
        "misc/cmp/vggray.cmp": gray_colormap(),
        "misc/snd/rlquiet.snd": b"# Silent original fixture sound class.\r\n",
        "mat/vgwall.mat": opaque_material((76, 84, 84, 76), edition),
        "mat/vgrib.mat": opaque_material((30, 34, 34, 30), edition),
        "mat/vgbadge.mat": opaque_material((232, 180, 180, 232), edition),
        "mat/vgshaft.mat": shaft_material(),
        "3do/rlpilot.3do": relay.box("pilot", (.02, .02, .03), "vgwall.mat"),
        "3do/vgrib.3do": relay.box("rib", (.35, .018, .026), "vgrib.mat"),
        "3do/vgshaft.3do": flat_model("shaft", ("vgshaft.mat",), shaft),
        "3do/vgsign.3do": signage(control == "overlap-crest"),
    }
    destination.mkdir(parents=True, exist_ok=False)
    for name, data in entries.items():
        path = destination / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
    write_gob(destination / "viewlab.gob", entries)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("destination", type=Path, help="new output directory outside the checkout")
    parser.add_argument("--edition", type=int, choices=(8, 16), default=8)
    parser.add_argument("--control", choices=("revised", "ordinary-rib", "overlap-crest", "no-shaft"),
                        default="revised", help="change one visual condition at a time")
    args = parser.parse_args()
    create(args.destination, args.edition, args.control)
