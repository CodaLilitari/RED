"""Generate the continuous RGB565 pane for the four existing inset UV charts."""
import math
import struct


BASE = (26, 33, 43)
SHOULDER = (38, 47, 59)
RIM = (50, 61, 74)
STRENGTH = (1, 0.87, 0.72, 0.58)
OFFSETS = (0.125, 0.375, 0.625, 0.875)


def smoothstep(start, end, value):
    t = min(1, max(0, (value - start) / (end - start)))
    return t * t * (3 - 2 * t)


def reflection(across, along):
    depth = max(across - 8, 0)
    length = max(along - 8, 0)
    shoulder = ((1 - smoothstep(0, 20, depth)) * (1 - smoothstep(56, 82, length)) *
                (0.58 + 0.42 * math.exp(-((length - 39) / 25) ** 2)))
    rim = (1 - smoothstep(0.8, 4.5, depth)) * (1 - smoothstep(76, 98, length))
    return tuple(shoulder * (s - b) + rim * (r - s) for b, s, r in zip(BASE, SHOULDER, RIM))


def rgb565(values):
    r, g, b = [round(c * levels / 255) for c, levels in zip(values, (31, 63, 31))]
    return (r << 11) | (g << 5) | b


def mip(level, strength):
    scale = 1 << level
    side = 128 // scale
    width = 2 * side
    base = rgb565(BASE)
    plane = [base] * (width * width)
    for y in range(side):
        for x in range(side):
            if min(x, y) * scale >= 28:
                continue
            top, both = [0., 0., 0.], [0., 0., 0.]
            for dx in OFFSETS:
                for dy in OFFSETS:
                    u, v = (x + dx) * scale, (y + dy) * scale
                    a, b = reflection(v, u), reflection(u, v)
                    for channel in range(3):
                        top[channel] += a[channel]
                        both[channel] += max(a[channel], b[channel])
            def color(total):
                return rgb565([c + d * (strength / 16) for c, d in zip(BASE, total)])
            edge = color(top)
            plane[y * width + side + x] = edge
            plane[(side + x) * width + y] = edge
            plane[(side + y) * width + side + x] = color(both)
    return struct.pack(f"<{len(plane)}H", *plane)


def glass_material():
    # JK MAT v50, one textured cel, no color key and four authored mips.
    header = struct.pack("<4s18I", b"MAT ", 50, 2, 1, 1, 1, 16, 5, 6, 5, 11, 5, 0, 3, 2, 3, 0, 0, 0)
    header += struct.pack("<10I", 8, 0, *([0x3f800000] * 4), 0, 0, 0xbff78482, 0)
    header += struct.pack("<6I", 256, 256, 0, 0, 0, 4)
    return header + b"".join(mip(level, strength) for level, strength in enumerate(STRENGTH))
