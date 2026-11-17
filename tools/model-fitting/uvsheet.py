"""Draw a model's UV layout for a painter, from the NAME_uv.json and NAME_tex.png that glb2jk.py --uv-out writes.

usage: python uvsheet.py NAME_uv.json [--scale 4]
Writes NAME_overlay.png (the baked texture with every face outlined) and NAME_layout.png (the outlines alone, white
on black), both at scale x the texture size so thin islands stay legible."""
import argparse, json
from pathlib import Path
from PIL import Image, ImageDraw
ap = argparse.ArgumentParser(); ap.add_argument('uv'); ap.add_argument('--scale', type=int, default=4)
a = ap.parse_args()
src = Path(a.uv); data = json.loads(src.read_text()); n = data['size'] * a.scale
stem = src.name[:-len('_uv.json')] if src.name.endswith('_uv.json') else src.stem
tex = Image.open(src.with_name(data['texture'])).convert('RGB').resize((n, n), Image.NEAREST)
lay = Image.new('RGB', (n, n), (0, 0, 0))
for im, colour in ((tex, (0, 255, 255)), (lay, (255, 255, 255))):
    d = ImageDraw.Draw(im)
    for face in data['faces']:
        pts = [(u * n, (1 - v) * n) for u, v in face]
        d.line(pts + [pts[0]], fill=colour, width=1)
tex.save(src.with_name(stem + '_overlay.png')); lay.save(src.with_name(stem + '_layout.png'))
print('wrote', stem + '_overlay.png', stem + '_layout.png')
