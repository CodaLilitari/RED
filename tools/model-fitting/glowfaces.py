"""Mark a 3DO's glowing faces fully lit (light mode 0), so a lamp's lens shines whatever the sector light.

usage: python glowfaces.py MODEL.3do MAT_PREVIEW.png [--min-red R] [--max-blue B]
A face glows when at least --share of its texels (sampled over the face in the MAT's PNG preview) are a warm,
bright colour: red at least R and blue at most B. The 3DO is rewritten in place; other faces keep their mode."""
import argparse, re
from PIL import Image
ap = argparse.ArgumentParser()
ap.add_argument('model'); ap.add_argument('png')
ap.add_argument('--min-red', type=int, default=150); ap.add_argument('--max-blue', type=int, default=120)
ap.add_argument('--share', type=float, default=0.4, help='share of a face\'s texels that must be warm')
a = ap.parse_args()
img = Image.open(a.png).convert('RGB'); W, H = img.size
lines = open(a.model).read().split('\n')
def texel(u, v):
    # 3DO texture vertices are texel coordinates that wrap; Blender Sith writes v negative (rows from the top)
    return img.getpixel((int(u) % W, int(v) % H))
glow = 0
face_re = re.compile(r'^(\s*\d+:\s+\d+\s+0x[0-9a-fA-F]+\s+\d+\s+)(\d+)(\s+\d+\s+\S+\s+(\d+)\s+)(.*)$')
tv = []
state = None
for idx, l in enumerate(lines):
    t = l.strip()
    if t.startswith('TEXTURE VERTICES'):
        tv = []; state = 'tv'; continue
    if t.startswith('VERTEX NORMALS') or t.startswith('FACE NORMALS'):
        state = None; continue
    if t.startswith('FACES'):
        state = 'faces'; continue
    if state == 'tv':
        p = t.split()
        if p and p[0].endswith(':') and len(p) >= 3:
            tv.append((float(p[1]), float(p[2])))
        continue
    m = face_re.match(l) if state == 'faces' else None
    if not m:
        continue
    pairs = re.findall(r'(\d+),\s*(\d+)', m.group(5))
    pts = [tv[int(t)] for _, t in pairs]
    # sample the face's texels on a barycentric grid over its fan of triangles
    samples = []
    for t in range(1, len(pts) - 1):
        p0, p1, p2 = pts[0], pts[t], pts[t + 1]
        for i in range(7):
            for j in range(7 - i):
                w1, w2 = (i + 0.5) / 7.5, (j + 0.5) / 7.5
                w0 = 1.0 - w1 - w2
                samples.append(texel(w0 * p0[0] + w1 * p1[0] + w2 * p2[0], w0 * p0[1] + w1 * p1[1] + w2 * p2[1]))
    warm = sum(1 for r, g, b in samples if r >= a.min_red and b <= a.max_blue and g >= 0.3 * r)
    if warm >= a.share * len(samples):
        lines[idx] = m.group(1) + '0' + m.group(3) + m.group(5)
        glow += 1
open(a.model, 'w').write('\n'.join(lines))
print('glowing faces', glow)
