"""Adapt Quake III / Quake Live IBSP geometry to RED's Raven BSP import path."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys


LIMIT = 128 * 1024 * 1024  # RED's RBSP reader limit.
SLOTS = bytes((0, 255, 255, 255))
IBSP_STRIDES = (1, 72, 16, 36, 48, 4, 4, 40, 12, 8, 44, 4, 72, 104, 49152, 8, 1)
# Same semantics, different source and Raven flags. Unmapped bits are reported.
SURFACES = {1: 0x40000, 2: 0x4000, 4: 0x2000, 0x10: 0x80000,
            0x20: 0x100000, 0x80: 0x200000, 0x1000: 0x8000,
            0x2000: 0x400000, 0x20000: 0x800000}
CONTENTS = {1: 1, 8: 2, 0x10: 0x20000, 0x20: 4, 0x40: 8, 0x10000: 0x10,
            0x20000: 0x20, 0x40000: 0x80000, 0x400000: 0x40,
            0x2000000: 0x100, 0x4000000: 0x200, 0x8000000: 0x8000000,
            0x20000000: 0x80000000, 0x40000000: 0x400, 0x80000000: 0x800}


def source_lumps(data):
    if len(data) < 8 or len(data) > LIMIT:
        raise ValueError("IBSP outside supported size")
    magic, version = struct.unpack_from("<4si", data)
    if magic != b"IBSP" or version not in (46, 47):
        raise ValueError("expected Quake III IBSP 46 or 47 (Quake Live)")
    count = 18 if version == 47 else 17
    header_size = 8 + count * 8
    if len(data) < header_size:
        raise ValueError("truncated lump directory")
    lumps = []
    spans = []
    view = memoryview(data)
    for index in range(count):
        start, length = struct.unpack_from("<ii", data, 8 + index * 8)
        end = start + length
        if min(start, length) < 0 or end > len(data) or (length and start < header_size):
            raise ValueError(f"lump {index} outside file")
        if length:
            spans.append((start, end))
        if index < len(IBSP_STRIDES) and length % IBSP_STRIDES[index]:
            raise ValueError(f"partial record in lump {index}")
        lumps.append(view[start:end])
    spans.sort()
    if any(a[1] > b[0] for a, b in zip(spans, spans[1:])):
        raise ValueError("overlapping IBSP lumps")
    return version, lumps


def remap(bits, mapping):
    return sum(out_bit for in_bit, out_bit in mapping.items() if bits & in_bit)


def shifted_rgb(red, green, blue, shift):
    red, green, blue = red << shift, green << shift, blue << shift
    peak = max(red, green, blue)
    if peak <= 255:
        return bytes((red, green, blue))
    return bytes((red * 255 // peak, green * 255 // peak, blue * 255 // peak))


def normalize(data, lightmap_shift=0):
    if lightmap_shift not in (0, 1, 2):
        raise ValueError("lightmap shift must be 0, 1, or 2")
    version, source = source_lumps(data)
    target = list(source[:17]) + [b""]
    # RED samples lightmaps and vertex colors; Raven's optional compressed
    # light-grid/index pair differs from Quake's positional light grid.
    target[15] = b""
    report = {
        "sourceVersion": version,
        "lightmapShift": lightmap_shift,
        "sourceSha256": hashlib.sha256(data).hexdigest(),
        "lightGridSamplesOmitted": len(source[15]) // 8,
        "advertisementBytesOmitted": len(source[17]) if version == 47 else 0,
        "unmappedShaderBits": [],
    }
    expected_size = 152 + sum((len(l) + 3) & ~3 for l in target)
    expected_size += len(source[9]) // 8 * 4 + len(source[10]) // 44 * 36 + len(source[13]) // 104 * 44
    if expected_size > LIMIT:
        raise ValueError("normalized scene exceeds RED's input limit")
    shaders = bytearray()
    for index, (name, surface, contents) in enumerate(struct.iter_unpack("<64sII", source[1])):
        raven_contents = remap(contents, CONTENTS)
        if contents & 1 and not contents & 0x20000000:
            raven_contents |= 0x8000  # Opaque solid, used by RED's visibility pass.
        shaders.extend(struct.pack("<64sII", name, remap(surface, SURFACES), raven_contents))
        unhandled_surface = surface & ~sum(SURFACES)
        unhandled_contents = contents & ~sum(CONTENTS)
        if unhandled_surface or unhandled_contents:
            report["unmappedShaderBits"].append({
                "index": index, "shader": name.split(b"\0", 1)[0].decode("ascii", "replace"),
                "surface": hex(unhandled_surface), "contents": hex(unhandled_contents),
            })
    target[1] = shaders
    sides = bytearray()
    for plane, shader in struct.iter_unpack("<ii", source[9]):
        sides.extend(struct.pack("<iii", plane, shader, -1))
    target[9] = sides
    verts = bytearray()
    for first, normal_color in struct.iter_unpack("<28s16s", source[10]):
        if lightmap_shift:
            color = shifted_rgb(*normal_color[12:15], lightmap_shift)
            normal_color = normal_color[:12] + color + normal_color[15:]
        verts.extend(first + bytes(24) + normal_color + bytes(12))
    target[10] = verts
    surfaces = bytearray()
    for first, lm, x, y, remaining in struct.iter_unpack("<28siii64s", source[13]):
        surfaces.extend(first + SLOTS * 2)
        surfaces.extend(struct.pack("<12i", lm, -3, -3, -3, x, 0, 0, 0, y, 0, 0, 0))
        surfaces.extend(remaining)
    target[13] = surfaces
    if lightmap_shift:
        pixels = bytearray()
        for rgb in struct.iter_unpack("3B", source[14]):
            pixels.extend(shifted_rgb(*rgb, lightmap_shift))
        target[14] = pixels
    result = bytearray(8 + len(target) * 8)
    struct.pack_into("<4si", result, 0, b"RBSP", 1)
    for i, lump in enumerate(target):
        struct.pack_into("<ii", result, 8 + i * 8, len(result), len(lump))
        result.extend(lump)
        result.extend(bytes(-len(result) % 4))
    report["normalizedSha256"] = hashlib.sha256(result).hexdigest()
    return result, report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path, help="extracted Quake BSP")
    parser.add_argument("output", type=Path, help="new RBSP file")
    parser.add_argument("--lightmap-shift", type=int, choices=(0, 1, 2), default=0,
                        help="whole stops of overbright correction for lightmaps and vertex RGB")
    args = parser.parse_args()
    if args.source.stat().st_size > LIMIT:
        raise ValueError("IBSP exceeds RED's input limit")
    normalized, report = normalize(args.source.read_bytes(), args.lightmap_shift)
    stream = args.output.open("xb")
    try:
        with stream:
            stream.write(normalized)
    except OSError:
        args.output.unlink(missing_ok=True)
        raise
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError) as exc:
        sys.exit(str(exc))
