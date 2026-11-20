"""Construct the original Raven RBSP used by the wind gallery conversion fixture."""

import struct


def scene():
    lumps = [b"" for _ in range(18)]
    lumps[0] = (
        b'{\n"classname" "worldspawn"\n}\n'
        b'{\n"classname" "info_player_deathmatch"\n"origin" "0 -100 20"\n"angle" "90"\n}\n\0'
    )
    shaders = [
        ("gallery/floor", 0, 1),
        ("gallery/sky", 0x2000, 0),
        ("gallery/grate", 0, 0),
        ("gallery/curve", 0, 1),
        ("gallery/air_clip", 0x200000, 0x90),
    ]
    lumps[1] = b"".join(
        struct.pack("<64sII", name.encode(), flags, contents) for name, flags, contents in shaders
    )
    bounds = [(-96, 96), (-240, 240), (0, 240)]
    planes = []
    for axis, (low, high) in enumerate(bounds):
        for sign, distance in ((-1, -low), (1, high)):
            normal = [0, 0, 0]
            normal[axis] = sign
            planes.append((*normal, distance))
    planes += [(0, 1, 0, 0)]
    # An independently compiled volume with no draw surface.
    clip_first = len(planes)
    for axis, (low, high) in enumerate(((25, 50), (30, 60), (0, 100))):
        for sign, distance in ((-1, -low), (1, high)):
            normal = [0, 0, 0]
            normal[axis] = sign
            planes.append((*normal, distance))
    lumps[2] = b"".join(struct.pack("<4f", *p) for p in planes)
    low, high = (-112, -256, -16), (112, 256, 256)
    for i in range(7):
        front, back = (-8, -9) if i == 6 else (-2 - i, i + 1)
        lumps[3] += struct.pack("<9i", i, front, back, *low, *high)

    verts, indices, surfaces = [], [], []

    def surface(shader, points, normal, patch=False):
        first, first_index = len(verts), len(indices)
        for i, position in enumerate(points):
            v = bytearray(80)
            struct.pack_into("<3f2f", v, 0, *position, i % 2, (i // 2) % 2)
            struct.pack_into("<3f", v, 52, *normal)
            v[64:68] = bytes([220, 212, 195, 255])
            verts.append(v)
        if not patch:
            indices.extend((0, 1, 2, 0, 2, 3))
        face = bytearray(148)
        struct.pack_into(
            "<7i",
            face,
            0,
            shader,
            -1,
            2 if patch else 1,
            first,
            len(points),
            first_index,
            0 if patch else 6,
        )
        struct.pack_into("<4i", face, 36, -1, -1, -1, -1)
        if patch:
            struct.pack_into("<2i", face, 140, 3, 3)
        surfaces.append(face)

    surface(0, [(-96, -240, 0), (96, -240, 0), (96, 240, 0), (-96, 240, 0)], (0, 0, 1))
    surface(1, [(-96, -240, 240), (-96, 240, 240), (96, 240, 240), (96, -240, 240)], (0, 0, -1))
    surface(2, [(-90, 170, 5), (90, 170, 5), (90, 170, 120), (-90, 170, 120)], (0, -1, 0))
    surface(
        3,
        [
            (-75 + 14 * u, 30 + 36 * v, 12 + (60 if u == 1 and v == 1 else 0))
            for v in range(3)
            for u in range(3)
        ],
        (0, 0, 1),
        True,
    )
    n = len(surfaces)
    for leaf in range(9):
        open_leaf = leaf >= 7
        lumps[4] += struct.pack(
            "<12i",
            leaf - 7 if open_leaf else -1,
            0,
            *low,
            *high,
            (leaf - 7) * n if open_leaf else 0,
            n if open_leaf else 0,
            0,
            1 if leaf == 7 else 0,
        )
    lumps[5] = b"".join(struct.pack("<i", i) for _ in range(2) for i in range(n))
    lumps[6] = struct.pack("<i", 0)
    lumps[7] = struct.pack("<6f4i", *low, *high, 0, n, 0, 1)
    lumps[8] = struct.pack("<3i", 0, 6, 4)
    lumps[9] = b"".join(struct.pack("<3i", clip_first + i, 4, -1) for i in range(6))
    lumps[10] = b"".join(verts)
    lumps[11] = b"".join(struct.pack("<i", i) for i in indices)
    lumps[13] = b"".join(surfaces)
    header = bytearray(b"RBSP" + struct.pack("<i", 1) + bytes(18 * 8))
    for i, lump in enumerate(lumps):
        struct.pack_into("<II", header, 8 + 8 * i, len(header), len(lump))
        header.extend(lump)
        header.extend(bytes((-len(header)) % 4))
    return bytes(header)
