"""Bake the phase-zero RGB of an id Tech 3 cloud dome to six JK-oriented faces."""

import argparse
import math
from pathlib import Path
import tempfile


def _sample(layer, u, v):
    width, height, pixels, scale_s, scale_t = layer
    u = u * scale_s * width - 0.5
    v = v * scale_t * height - 0.5
    x, y = math.floor(u), math.floor(v)
    fx, fy = u - x, v - y
    a = 3 * ((y % height) * width + x % width)
    b = 3 * ((y % height) * width + (x + 1) % width)
    c = 3 * (((y + 1) % height) * width + x % width)
    d = 3 * (((y + 1) % height) * width + (x + 1) % width)
    return [(1 - fy) * ((1 - fx) * pixels[a+i] + fx * pixels[b+i])
            + fy * ((1 - fx) * pixels[c+i] + fx * pixels[d+i]) for i in range(3)]


def bake_faces(layers, height, size=256):
    """Return RGB byte buffers indexed by rt/lf/bk/ft/up/dn. First stage is opaque, others additive."""
    if not math.isfinite(height) or not 0 < height <= 65536 or not 2 <= size <= 1024:
        raise ValueError('Cloud height must be positive and finite; face size must be 2 to 1024')
    if not 1 <= len(layers) <= 8:
        raise ValueError('Expected one to eight cloud stages')
    for width, height_px, rgb, sx, sy in layers:
        if not (0 < width <= 8192 and 0 < height_px <= 8192 and len(rgb) == width * height_px * 3):
            raise ValueError('Invalid RGB stage dimensions or data')
        if any(not math.isfinite(scale) or abs(scale) > 1024 for scale in (sx, sy)):
            raise ValueError('Invalid cloud stage scale')
    directions = {
        'rt': lambda s,t: (1,-s,t), 'lf': lambda s,t: (-1,s,t),
        'bk': lambda s,t: (s,1,t), 'ft': lambda s,t: (-s,-1,t),
        'up': lambda s,t: (-t,-s,1)
    }
    # The renderer omits the lower cloud face. JK needs an opaque background there.
    result = {'dn': bytes(3 * size * size)}
    radius = 4096.
    shell = radius + height
    for name, direction in directions.items():
        image = bytearray()
        for row in range(size):
            t = 1 - 2 * row / (size - 1)
            for col in range(size):
                s = 2 * col / (size - 1) - 1
                x, y, z = direction(s, t)
                length_sq = x*x + y*y + z*z
                along = (math.sqrt((radius*z)**2 + length_sq * height * (radius+shell)) - radius*z) / length_sq
                u = math.acos(max(-1., min(1., x*along / shell)))
                v = math.acos(max(-1., min(1., y*along / shell)))
                rgb = [0., 0., 0.]
                for layer in layers:
                    sample = _sample(layer, u, v)
                    for channel in range(3):
                        rgb[channel] += sample[channel]
                image.extend(min(255, round(channel)) for channel in rgb)
        result[name] = bytes(image)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=Path, help='New output directory for six PNGs')
    parser.add_argument('--height', required=True, type=float, help='Cloud height from skyparms')
    parser.add_argument('--size', type=int, default=256)
    parser.add_argument('--layer', metavar=('IMAGE', 'S', 'T'), action='append', nargs=3, required=True,
                        help='Image and tcMod scale, in source shader order; default scale is 1 1')
    args = parser.parse_args()
    try:
        from PIL import Image
    except ImportError:
        parser.error('The image CLI requires Pillow')
    layers = []
    if args.output.exists():
        parser.error('Choose a new output directory')
    for path, sx, sy in args.layer:
        with Image.open(path) as image:
            if max(image.size) > 8192 or image.width * image.height > 16777216:
                parser.error('Input stage exceeds image limits')
            layers.append((*image.size, image.convert('RGB').tobytes(), float(sx), float(sy)))
    faces = bake_faces(layers, args.height, args.size)
    with tempfile.TemporaryDirectory(dir=args.output.parent) as temporary:
        directory = Path(temporary) / 'sky'
        directory.mkdir()
        for name, rgb in faces.items():
            Image.frombytes('RGB', (args.size, args.size), rgb).save(directory / (name + '.png'))
        if args.output.exists():
            parser.error('Output appeared while preparing the sky')
        directory.rename(args.output)


if __name__ == '__main__':
    main()
