"""Tripo GLB or OBJ -> JK 3DO 2.1 with one indexed MAT, through Blender and the Blender Sith add-on (Coda's fork,
1.1.0 or later), run without a window.

usage: python glb2jk.py IN.glb|IN.obj OUT_DIR NAME --height H --faces F --tex T --cmp PALETTE [--reduce quads] \
           --addon PATH/TO/sith
   or: blender -b --python glb2jk.py -- IN.glb OUT_DIR NAME ...
The first form needs the bpy module (pip install bpy, matching your Blender); --addon, or the BLENDER_SITH_ADDON
environment variable, names the add-on's `sith` folder. A meshopt-compressed GLB (Tripo's download links) must go
through meshopt_decode.py first.
The model is joined into one mesh, its base-colour texture baked down to a TxT image, reduced to about F faces,
scaled so its height is H JKU, set on the floor (z = 0 at its base, centred in x and y), exported as NAME.3do with
NAME.mat dithered to PALETTE (the level's master colormap).

--reduce collapse (the default) decimates to triangles. The others are for a quad mesh, such as Tripo's Retopology
output exported as OBJ (glTF stores only triangles); JK draws a quad as one face, so the same face budget carries
about twice the detail:
  wren      Wren's recipe (Wren, AI collaborator, via Grok Bot; MIT): unsubdivide, planar dissolve at 8 degrees,
            then collapse without triangulating until the mesh is within --faces and --verts;
  dissolve  merges nearly flat neighbours into polygons and splits any that would be concave or bent;
  quads     decimates to --verts vertices, then joins the triangles back into quads;
  none      keeps the mesh as it is (and splits it into parts if it is over JK's per-mesh limits).
All of them clear custom normals and shade smooth, so each vertex is exported once, and the quad modes triangulate
only the faces that are not flat (a corner more than 1e-4 JKU or 1 degree off the face's plane) or have more than 24
corners, as JK needs flat faces."""
import sys, argparse, os
from pathlib import Path
ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
ap.add_argument('glb'); ap.add_argument('out'); ap.add_argument('name')
ap.add_argument('--height', type=float, required=True); ap.add_argument('--faces', type=int, default=400)
ap.add_argument('--tex', type=int, default=128); ap.add_argument('--cmp', required=True)
ap.add_argument('--addon', default=os.environ.get('BLENDER_SITH_ADDON', ''), help="the Blender Sith add-on's sith folder")
ap.add_argument('--yaw', type=float, default=0.0, help='degrees about z applied before export')
ap.add_argument('--reduce', choices=('collapse', 'wren', 'dissolve', 'quads', 'none'), default='collapse')
ap.add_argument('--unsubdivide', type=int, default=2, help='wren: unsubdivide iterations')
ap.add_argument('--supersample', type=int, default=1, help='bake at this many times --tex, then box-filter down (Wren uses 4)')
ap.add_argument('--verts', type=int, default=500, help='dissolve, quads: the most vertices to keep')
ap.add_argument('--ray', type=float, default=0.05, help='bake: how far, as a share of the model size, a texel may be from the source surface')
ap.add_argument('--uv-out', default='', help='also write, beside this path, the baked texture (_tex.png) and the UV layout (_uv.json) for repainting; uvsheet.py draws them')
a = ap.parse_args(sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else None)
if not a.addon: ap.error('name the Blender Sith add-on with --addon or BLENDER_SITH_ADDON')
addon = Path(a.addon).resolve()
sys.path.insert(0, str(addon.parent)); sys.dont_write_bytecode = True
import bpy, bmesh, addon_utils, math, time
T0 = time.time()
def tick(m): print(f'[{time.time()-T0:7.1f}s] {m}', flush=True)
import sith
from sith.model import model3doExporter, model3doLoader
assert Path(sith.__file__).resolve() == addon / '__init__.py'
sith.register()
addon_utils.enable('io_scene_gltf2', default_set=True)
bpy.ops.wm.read_factory_settings(use_empty=True)
tick('importing')
if a.glb.lower().endswith('.obj'):
    bpy.ops.wm.obj_import(filepath=str(Path(a.glb).resolve()))   # Y up in the file, Z up in Blender, as glTF
else:
    bpy.ops.import_scene.gltf(filepath=str(Path(a.glb).resolve()))
tick('imported')
meshes = [o for o in bpy.context.scene.objects if o.type == 'MESH']
assert meshes, 'no mesh in the GLB'
for o in bpy.context.scene.objects: o.select_set(o in meshes)
bpy.context.view_layer.objects.active = meshes[0]
if len(meshes) > 1: bpy.ops.object.join()
obj = bpy.context.view_layer.objects.active
bpy.ops.object.select_all(action='DESELECT'); obj.select_set(True)
obj.parent = None
bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
src_faces = len(obj.data.polygons)
# the base-colour image Tripo baked
img = None
for slot in obj.material_slots:
    if slot.material and slot.material.use_nodes:
        for n in slot.material.node_tree.nodes:
            # the image that feeds Base Color, not the normal or roughness map an OBJ also brings
            if n.type == 'TEX_IMAGE' and n.image and any(l.to_socket.name == 'Base Color' for l in n.outputs['Color'].links):
                img = n.image; break
    if img: break
assert img is not None, 'no base-colour texture'
# glTF splits vertices along UV seams; weld them (UVs live on the corners, so the seams stay) or collapse
# decimation treats every island edge as a border it may not cross
bm = bmesh.new(); bm.from_mesh(obj.data)
ext = max(max(v.co[i] for v in bm.verts) - min(v.co[i] for v in bm.verts) for i in range(3))
bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=ext * 1e-5)
bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
bm.to_mesh(obj.data); bm.free(); tick(f'welded: {len(obj.data.vertices)} vertices')
# the textured original stays as the bake source; a copy is decimated
high = obj
obj = high.copy(); obj.data = high.data.copy(); bpy.context.collection.objects.link(obj)
bpy.ops.object.select_all(action='DESELECT'); obj.select_set(True); bpy.context.view_layer.objects.active = obj
def reduce_dissolve():
    """Merge faces meeting at less than an angle that grows until the mesh fits, keeping every face JK can draw:
    convex, flat to within a degree, at most 24 corners."""
    base = obj.data.copy()
    for deg in (1, 2, 3, 4, 5, 6, 7, 8, 10, 12, 14, 16, 18, 20):
        bm = bmesh.new(); bm.from_mesh(base)
        bmesh.ops.dissolve_limit(bm, angle_limit=math.radians(deg), use_dissolve_boundaries=False,
                                 verts=bm.verts[:], edges=bm.edges[:], delimit=set())
        for _ in range(3):
            bmesh.ops.connect_verts_nonplanar(bm, angle_limit=math.radians(1.0), faces=bm.faces[:])
            bmesh.ops.connect_verts_concave(bm, faces=bm.faces[:])
        big = [f for f in bm.faces if len(f.verts) > 24]
        if big: bmesh.ops.triangulate(bm, faces=big)
        bmesh.ops.dissolve_degenerate(bm, dist=ext * 1e-4, edges=bm.edges[:])
        nf, nv = len(bm.faces), len(bm.verts)
        sizes = {}
        for f in bm.faces: sizes[min(len(f.verts), 9)] = sizes.get(min(len(f.verts), 9), 0) + 1
        tick(f'dissolve {deg} deg: {nf} faces, {nv} vertices, corners {dict(sorted(sizes.items()))}')
        if nf <= a.faces and nv <= a.verts:
            bm.to_mesh(obj.data); bm.free(); return deg
        bm.free()
    raise SystemExit(f'dissolving to 20 degrees left more than {a.faces} faces or {a.verts} vertices')
def reduce_quads():
    """Collapse to about --verts vertices, then pair the triangles back into quads (JK draws a quad as one face)."""
    nv = len(obj.data.vertices)
    for _ in range(8):
        if nv <= a.verts: break
        mod = obj.modifiers.new('dec', 'DECIMATE'); mod.ratio = a.verts / nv * 0.98; mod.use_collapse_triangulate = True
        bpy.ops.object.modifier_apply(modifier='dec'); nv = len(obj.data.vertices)
        tick(f'collapsed to {nv} vertices, {len(obj.data.polygons)} faces')
    bpy.ops.object.mode_set(mode='EDIT'); bpy.ops.mesh.select_all(action='SELECT')
    bpy.ops.mesh.tris_convert_to_quads(face_threshold=math.radians(40), shape_threshold=math.radians(40))
    bpy.ops.object.mode_set(mode='OBJECT')
    sizes = {}
    for f in obj.data.polygons: sizes[len(f.vertices)] = sizes.get(len(f.vertices), 0) + 1
    tick(f'quads: {len(obj.data.polygons)} faces, {len(obj.data.vertices)} vertices, corners {dict(sorted(sizes.items()))}')
def reduce_wren():
    """Wren's reduction for Tripo's Smart Mesh quads: unsubdivide, planar dissolve at 8 degrees, then collapse
    without triangulating. From Wren's prop pipeline (Wren, AI collaborator, via Grok Bot; MIT)."""
    bm = bmesh.new(); bm.from_mesh(obj.data)
    bmesh.ops.unsubdivide(bm, verts=list(bm.verts), iterations=a.unsubdivide)
    bm.to_mesh(obj.data); bm.free(); obj.data.update()
    tick(f'unsubdivided: {len(obj.data.polygons)} faces, {len(obj.data.vertices)} vertices')
    mod = obj.modifiers.new('planar', 'DECIMATE'); mod.decimate_type = 'DISSOLVE'; mod.angle_limit = math.radians(8)
    bpy.ops.object.modifier_apply(modifier='planar')
    tick(f'planar dissolve: {len(obj.data.polygons)} faces, {len(obj.data.vertices)} vertices')
    for _ in range(12):
        triangulate_bent()
        nf, nv = len(obj.data.polygons), len(obj.data.vertices)
        if nf <= a.faces and nv <= a.verts: break
        mod = obj.modifiers.new('collapse', 'DECIMATE'); mod.use_collapse_triangulate = False
        mod.ratio = max(0.5, min(0.97, min(a.faces / nf, a.verts / nv) * 0.97))
        bpy.ops.object.modifier_apply(modifier='collapse')
        tick(f'collapsed: {len(obj.data.polygons)} faces, {len(obj.data.vertices)} vertices')
def triangulate_bent():
    """Triangulate the faces JK cannot draw flat: a corner more than 1e-4 JKU or 1 degree off the face's plane, or
    more than 24 corners (Wren's test)."""
    eps = 1e-4 * src_height / a.height   # the model is scaled to --height only after baking
    bm = bmesh.new(); bm.from_mesh(obj.data); bm.normal_update()
    def bent(f):
        if len(f.verts) > 24: return True
        if len(f.verts) <= 3: return False
        n = f.normal
        if n.length < 1e-12: return True
        n = n.normalized(); c = f.calc_center_median()
        for v in f.verts:
            d = abs((v.co - c).dot(n)); r = (v.co - c).length
            if d > eps or (r > 1e-12 and math.degrees(math.asin(min(1.0, d / r))) > 1.0): return True
        return False
    faces = [f for f in bm.faces if bent(f)]
    if faces: bmesh.ops.triangulate(bm, faces=faces)
    bm.to_mesh(obj.data); bm.free(); obj.data.update()
    if faces: tick(f'triangulated {len(faces)} bent faces: {len(obj.data.polygons)} faces')
src_height = max(v.co[2] for v in obj.data.vertices) - min(v.co[2] for v in obj.data.vertices)
if a.reduce == 'wren':
    reduce_wren()
elif a.reduce == 'dissolve':
    tick(f'dissolving {src_faces}'); reduce_dissolve(); tick(f'reduced to {len(obj.data.polygons)}')
elif a.reduce == 'quads':
    reduce_quads(); triangulate_bent()
elif a.reduce == 'none':
    tick(f'keeping {src_faces} faces, {len(obj.data.vertices)} vertices')
else:
    mod = obj.modifiers.new('dec', 'DECIMATE'); mod.ratio = min(1.0, a.faces / max(1, src_faces)); mod.use_collapse_triangulate = True
    tick(f'decimating {src_faces}'); bpy.ops.object.modifier_apply(modifier='dec'); tick(f'decimated to {len(obj.data.polygons)}')
for _ in range(6 if a.reduce == 'collapse' else 0):
    n = len(obj.data.polygons)
    if n <= a.faces * 1.15: break
    mod = obj.modifiers.new('dec', 'DECIMATE'); mod.ratio = a.faces / n; mod.use_collapse_triangulate = True
    bpy.ops.object.modifier_apply(modifier='dec'); tick(f'decimated to {len(obj.data.polygons)}')
    if len(obj.data.polygons) > 0.9 * n: break
assert len(obj.data.polygons) <= max(4 * a.faces, 2000), f'decimation stalled at {len(obj.data.polygons)} faces'
# fresh UVs on the low mesh, then bake the original's base colour onto them (Cycles, selected to active)
while obj.data.uv_layers: obj.data.uv_layers.remove(obj.data.uv_layers[0])
obj.data.uv_layers.new(name='bake')
bpy.ops.object.mode_set(mode='EDIT'); bpy.ops.mesh.select_all(action='SELECT')
# JK allows 768 texture vertices a mesh, and every UV island's border vertices count once per island. A hard-surface
# low-poly model (Tripo's Retopology) falls apart into nearly one island a face under Smart UV Project, so when that
# is over, cut seams only at sharp edges and unwrap: fewer, larger islands, at the cost of some stretch.
uv_methods = [('smart', 60)] if a.reduce == 'none' else [('smart', 60), ('smart', 66), ('seams', 100), ('seams', 110), ('seams', 120)]
for method, uv_angle in uv_methods:
    if method == 'smart':
        bpy.ops.uv.smart_project(angle_limit=math.radians(uv_angle), island_margin=2.0 / a.tex)
    else:
        bpy.ops.mesh.mark_seam(clear=True); bpy.ops.mesh.select_all(action='DESELECT')
        bpy.ops.mesh.edges_select_sharp(sharpness=math.radians(uv_angle)); bpy.ops.mesh.mark_seam(clear=False)
        bpy.ops.mesh.select_all(action='SELECT'); bpy.ops.uv.unwrap(method='CONFORMAL', margin=2.0 / a.tex)
    bpy.ops.object.mode_set(mode='OBJECT')
    texverts = len({(round(l.uv[0], 6), round(l.uv[1], 6)) for l in obj.data.uv_layers.active.data})
    tick(f'UVs ({method} {uv_angle} deg): {texverts} texture vertices')
    if texverts <= 740: break
    bpy.ops.object.mode_set(mode='EDIT'); bpy.ops.mesh.select_all(action='SELECT')
if obj.mode != 'OBJECT': bpy.ops.object.mode_set(mode='OBJECT')
# one normal per vertex: custom (split) normals would export a vertex once per normal
if obj.data.has_custom_normals:
    with bpy.context.temp_override(object=obj, active_object=obj):
        bpy.ops.mesh.customdata_custom_splitnormals_clear()
for e in obj.data.edges: e.use_edge_sharp = False
for p in obj.data.polygons: p.use_smooth = True
small = bpy.data.images.new(a.name + '_bake', a.tex * a.supersample, a.tex * a.supersample, alpha=False)
mat = bpy.data.materials.new(a.name); mat.use_nodes = True
nt = mat.node_tree; bsdf = nt.nodes.get('Principled BSDF')
tex = nt.nodes.new('ShaderNodeTexImage'); tex.image = small
nt.links.new(tex.outputs['Color'], bsdf.inputs['Base Color']); nt.nodes.active = tex
obj.data.materials.clear(); obj.data.materials.append(mat)
scene = bpy.context.scene; scene.render.engine = 'CYCLES'; scene.cycles.device = 'CPU'; scene.cycles.samples = 4
bake = scene.render.bake; bake.use_selected_to_active = True; bake.use_pass_direct = False; bake.use_pass_indirect = False
bake.use_pass_color = True; bake.margin = 4 * a.supersample; bake.cage_extrusion = ext * 0.02; bake.max_ray_distance = ext * a.ray
bpy.ops.object.select_all(action='DESELECT'); high.select_set(True); obj.select_set(True); bpy.context.view_layer.objects.active = obj
tick('baking'); bpy.ops.object.bake(type='DIFFUSE'); tick('baked')
if a.supersample > 1:
    # box-filter the oversized bake down to --tex: smoother than baking at --tex directly (Wren's practice)
    import numpy as np
    k, n = a.supersample, a.tex
    px = np.array(small.pixels[:], dtype=np.float32).reshape(n * k, n * k, 4)
    down = px.reshape(n, k, n, k, 4).mean(axis=(1, 3))
    final = bpy.data.images.new(a.name, n, n, alpha=False); final.pixels[:] = down.ravel().tolist()
    tex.image = final; bpy.data.images.remove(small); small = final
else:
    small.name = a.name
if a.uv_out:
    # what a painter needs to repaint the texture on the same layout: the baked texture (NAME_tex.png) and every
    # face's outline in UV space (NAME_uv.json). uvsheet.py, run with any Python that has Pillow, draws the sheets.
    import json
    base = Path(a.uv_out).resolve(); base.parent.mkdir(parents=True, exist_ok=True)
    tex_png = base.with_name(base.stem + '_tex.png')
    small.filepath_raw = str(tex_png); small.file_format = 'PNG'; small.save()
    uvl = obj.data.uv_layers.active.data
    polys = [[[round(uvl[i].uv[0], 6), round(uvl[i].uv[1], 6)] for i in p.loop_indices] for p in obj.data.polygons]
    base.with_name(base.stem + '_uv.json').write_text(json.dumps({'size': a.tex, 'texture': tex_png.name, 'faces': polys}))
    tick(f'UV layout written beside {base.name}')
bpy.data.objects.remove(high, do_unlink=True)
for o in list(bpy.context.scene.objects):
    if o is not obj: bpy.data.objects.remove(o, do_unlink=True)
bpy.ops.object.select_all(action='DESELECT'); obj.select_set(True); bpy.context.view_layer.objects.active = obj
# size, place, orient
bpy.context.view_layer.update()
xs = [ (obj.matrix_world @ v.co) for v in obj.data.vertices ]
lo = [min(p[i] for p in xs) for i in range(3)]; hi = [max(p[i] for p in xs) for i in range(3)]
# glTF is Y-up; Blender's importer converts to Z-up already
s = a.height / (hi[2] - lo[2])
obj.location = (-(lo[0] + hi[0]) / 2 * s, -(lo[1] + hi[1]) / 2 * s, -lo[2] * s)
obj.scale = (s, s, s)
obj.rotation_euler = (0, 0, math.radians(a.yaw))
bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
obj.name = a.name; obj.data.name = a.name
for m in obj.modifiers: obj.modifiers.remove(m)
for k in list(obj.data.shape_keys.key_blocks) if obj.data.shape_keys else []: pass
tick('exporting')
# export. Jedi Knight's model loader (rdModel3_Load) refuses a mesh with more than 512 vertices, 768 texture
# vertices or 512 faces, so when the export is over, the model is split into parts along its longest axis, each part
# a mesh of its own under the first, until every mesh fits.
LIMITS = {'VERTICES': 512, 'TEXTURE VERTICES': 768, 'FACES': 512}
out = Path(a.out).resolve(); out.mkdir(parents=True, exist_ok=True)
target = out / f'{a.name}.3do'
def mesh_counts(path):
    counts = []
    for line in open(path):
        t = line.strip().split()
        if len(t) >= 2 and t[0] == 'VERTICES' and t[1].isdigit(): counts.append({'VERTICES': int(t[1])})
        elif len(t) >= 3 and t[0] == 'TEXTURE' and t[1] == 'VERTICES': counts[-1]['TEXTURE VERTICES'] = int(t[2])
        elif len(t) >= 2 and t[0] == 'FACES' and t[1].isdigit(): counts[-1]['FACES'] = int(t[1])
    return counts
def split(root, parts):
    """Split `root` into `parts` meshes by face centroid along its longest axis; the rest are parented to the first."""
    me = root.data
    axis = max(range(3), key=lambda i: max(v.co[i] for v in me.vertices) - min(v.co[i] for v in me.vertices))
    order = sorted(range(len(me.polygons)), key=lambda f: me.polygons[f].center[axis])
    group = {f: (k * parts) // len(order) for k, f in enumerate(order)}
    made = [root]
    for g in range(parts - 1, 0, -1):
        bpy.ops.object.select_all(action='DESELECT'); root.select_set(True); bpy.context.view_layer.objects.active = root
        bpy.ops.object.mode_set(mode='EDIT'); bpy.ops.mesh.select_all(action='DESELECT'); bpy.ops.object.mode_set(mode='OBJECT')
        for poly in root.data.polygons: poly.select = group.get(poly.index, 0) == g
        bpy.ops.object.mode_set(mode='EDIT'); bpy.ops.mesh.separate(type='SELECTED'); bpy.ops.object.mode_set(mode='OBJECT')
        piece = [o for o in bpy.context.selected_objects if o is not root][0]
        piece.name = f'{a.name}_{g}'[:31]; piece.data.name = piece.name
        made.append(piece)
        # face indices shift after a separate: regroup what is left by centroid again
        order = sorted(range(len(root.data.polygons)), key=lambda f: root.data.polygons[f].center[axis])
        group = {f: (k * g) // len(order) for k, f in enumerate(order)}
    for piece in made[1:]:
        piece.parent = root
    return made
parts = 1
while True:
    if target.exists(): target.unlink()
    warnings = []
    model3doExporter.export3do(obj, str(target), model3doLoader.Model3doFileVersion.Version2_1,
        uvAbsolute=True, exportVertexColors=False, sync_mesh_list=False, warnings=warnings,
        exportTextures=True, textureEncoding='INDEXED', texturePalette=str(Path(a.cmp).resolve()),
        overwriteTextures=True, generateMipmaps=True, textureDirectory=str(out))
    counts = mesh_counts(target)
    over = [c for c in counts if any(c.get(k, 0) > v for k, v in LIMITS.items())]
    tick(f'{len(counts)} mesh(es): {counts}')
    if not over: break
    worst = max(max(c.get(k, 0) / v for k, v in LIMITS.items()) for c in counts) * len(counts)
    parts = max(parts + 1, math.ceil(worst * 1.15))
    assert parts <= 16, f'the model needs more than 16 parts to fit: {counts}'
    # start again from one piece: join any earlier split
    kids = [o for o in bpy.context.scene.objects if o.parent is obj]
    if kids:
        for o in kids: o.parent = None
        bpy.ops.object.select_all(action='DESELECT')
        for o in kids: o.select_set(True)
        obj.select_set(True); bpy.context.view_layer.objects.active = obj
        bpy.ops.object.join()
    tick(f'splitting into {parts} parts')
    split(obj, parts)
dims = [ (max((obj.matrix_world @ v.co)[i] for v in obj.data.vertices) - min((obj.matrix_world @ v.co)[i] for v in obj.data.vertices)) for i in range(3)]
print('SRC_FACES', src_faces, 'FACES', len(obj.data.polygons), 'VERTS', len(obj.data.vertices), 'DIMS', [round(d, 4) for d in dims])
print('WARNINGS', warnings[:8])
print('OUT', target, sorted(p.name for p in out.iterdir()))
