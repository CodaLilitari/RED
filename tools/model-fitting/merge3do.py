"""Merge every mesh of a 3DO's first geoset into one mesh, for testing whether a model's mesh count matters.

usage: python merge3do.py IN.3do OUT.3do

Faces keep their own material, geometry, light and texture modes, so a fully lit part (light mode 0) stays fully lit
in the merged mesh. Only models whose hierarchy nodes all have identity transforms (no offset, rotation or pivot) are
merged, since their mesh vertices are already in model space; anything else is refused. The merged mesh must still fit
JK's per-mesh limits (512 vertices, 768 texture vertices, 512 faces), or the script refuses."""
import math, re, sys

LIMITS = {'vertices': 512, 'texture vertices': 768, 'faces': 512}


def parse(text):
    lines = [l.split('#', 1)[0].rstrip() for l in text.splitlines()]
    materials, meshes, nodes, geoset = [], [], [], -1
    insert = '0.000000 0.000000 0.000000'
    i, section, mesh = 0, None, None

    def rows(n, start):
        out, j = [], start
        while len(out) < n:
            t = lines[j].strip(); j += 1
            if t: out.append(t)
        return out, j

    while i < len(lines):
        t = lines[i].strip(); i += 1
        if not t: continue
        w = t.split()
        if w[0] == 'SECTION:': section = w[1]; continue
        if section == 'MODELRESOURCE' and w[0] == 'MATERIALS':
            r, i = rows(int(w[1]), i); materials = [x.split()[1] for x in r]
        elif w[0] == 'INSERT' and w[1] == 'OFFSET': insert = ' '.join(w[2:5])
        elif w[0] == 'GEOSET': geoset = int(w[1])
        elif geoset == 0 and w[0] == 'MESH':
            mesh = {'name': '', 'geo': 4, 'light': 3, 'tex': 3}; meshes.append(mesh)
        elif mesh is not None and geoset == 0:
            if w[0] == 'NAME': mesh['name'] = w[1]
            elif w[0] == 'GEOMETRYMODE': mesh['geo'] = int(w[1])
            elif w[0] == 'LIGHTINGMODE': mesh['light'] = int(w[1])
            elif w[0] == 'TEXTUREMODE': mesh['tex'] = int(w[1])
            elif w[0] == 'VERTICES':
                r, i = rows(int(w[1]), i); mesh['v'] = [[float(x) for x in s.split()[1:5]] for s in r]
            elif w[0] == 'TEXTURE' and w[1] == 'VERTICES':
                r, i = rows(int(w[2]), i); mesh['tv'] = [[float(x) for x in s.split()[1:3]] for s in r]
            elif w[0] == 'VERTEX' and w[1] == 'NORMALS':
                r, i = rows(len(mesh['v']), i); mesh['vn'] = [[float(x) for x in s.split()[1:4]] for s in r]
            elif w[0] == 'FACES':
                r, i = rows(int(w[1]), i)
                faces = []
                for s in r:
                    head, _, rest = s.partition(':')
                    p = rest.split()
                    corners = [(int(a), int(b)) for a, b in re.findall(r'(\d+)\s*,\s*(\d+)', ' '.join(p[7:]))]
                    assert len(corners) == int(p[6]), s
                    faces.append({'mat': int(p[0]), 'type': p[1], 'geo': int(p[2]), 'light': int(p[3]),
                                  'tex': int(p[4]), 'extra': p[5], 'corners': corners})
                mesh['f'] = faces
            elif w[0] == 'FACE' and w[1] == 'NORMALS':
                r, i = rows(len(mesh['f']), i); mesh['fn'] = [[float(x) for x in s.split()[1:4]] for s in r]
        if section == 'HIERARCHYDEF' and w[0] == 'HIERARCHY' and w[1] == 'NODES':
            r, i = rows(int(w[2]), i); nodes = [s.split() for s in r]; mesh = None
    return materials, insert, meshes, nodes


def main():
    src, dst = sys.argv[1], sys.argv[2]
    materials, insert, meshes, nodes = parse(open(src, encoding='latin-1').read())
    for n in nodes:
        if any(abs(float(x)) > 1e-9 for x in n[8:17]):
            sys.exit(f'node {n[0]} ({n[17]}) has a transform; only identity hierarchies are merged')
    v, tv, vn, f, fn = [], [], [], [], []
    for m in meshes:
        vo, to = len(v), len(tv)
        v += m['v']; tv += m['tv']; vn += m['vn']; fn += m['fn']
        for face in m['f']:
            face = dict(face); face['corners'] = [(a + vo, b + to) for a, b in face['corners']]; f.append(face)
    counts = {'vertices': len(v), 'texture vertices': len(tv), 'faces': len(f)}
    over = {k: n for k, n in counts.items() if n > LIMITS[k]}
    if over: sys.exit(f'the merged mesh would be over JK\'s limits: {over}')
    radius = max(math.sqrt(x * x + y * y + z * z) for x, y, z, _ in v)
    name = meshes[0]['name']
    out = [f"# {dst.split('/')[-1]}: the meshes of {src.split('/')[-1]} merged into one by merge3do.py", '',
           'SECTION: HEADER', '', '3DO 2.1', '', 'SECTION: MODELRESOURCE', '', f'MATERIALS {len(materials)}', '']
    out += [f'{k:10d}:  {m}' for k, m in enumerate(materials)]
    out += ['', 'SECTION: GEOMETRYDEF', '', f'RADIUS {radius:10.6f}', '', f'INSERT OFFSET {insert}', '', 'GEOSETS 1', '',
            'GEOSET 0', '', 'MESHES 1', '', 'MESH 0', '', f'NAME {name}', '', f'RADIUS {radius:10.6f}', '',
            f"GEOMETRYMODE {max(m['geo'] for m in meshes)}", f"LIGHTINGMODE {max(m['light'] for m in meshes)}",
            f"TEXTUREMODE {max(m['tex'] for m in meshes)}", '', f'VERTICES {len(v)}', '']
    out += [f'{k:6d}: {x:10.6f} {y:10.6f} {z:10.6f} {c:9.6f}' for k, (x, y, z, c) in enumerate(v)]
    out += ['', f'TEXTURE VERTICES {len(tv)}', '']
    out += [f'{k:6d}: {a:10.6f} {b:10.6f}' for k, (a, b) in enumerate(tv)]
    out += ['', 'VERTEX NORMALS', '']
    out += [f'{k:6d}: {x:10.6f} {y:10.6f} {z:10.6f}' for k, (x, y, z) in enumerate(vn)]
    out += ['', f'FACES {len(f)}', '']
    for k, face in enumerate(f):
        corners = '  '.join(f'{a:3d},{b:3d}' for a, b in face['corners'])
        out.append(f"{k:6d}: {face['mat']:9d}  {face['type']}  {face['geo']:4d}  {face['light']:6d}  {face['tex']:5d}  "
                   f"{face['extra']}  {len(face['corners']):6d}  {corners}")
    out += ['', 'FACE NORMALS', '']
    out += [f'{k:6d}: {x:10.6f} {y:10.6f} {z:10.6f}' for k, (x, y, z) in enumerate(fn)]
    out += ['', 'SECTION: HIERARCHYDEF', '', 'HIERARCHY NODES 1', '',
            f'     0:  0x0000 0x00001        0       -1      -1        -1             0   0.000000   0.000000   0.000000'
            f'   0.000000   0.000000   0.000000   0.000000   0.000000   0.000000  {name}', '']
    open(dst, 'w', newline='\r\n').write('\n'.join(out))
    print(f'{src}: {len(meshes)} meshes -> 1 mesh: {counts}')


if __name__ == '__main__':
    main()
