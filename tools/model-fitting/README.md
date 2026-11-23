# Fitting generated models to Jedi Knight

These scripts take a generated model, such as a Tripo GLB, FBX or OBJ, and make a 3DO and MAT that retail Jedi Knight
can load and draw. RED's own `model-import` converts a model that is already game-ready. These scripts do the work
before that: they reduce the mesh, unwrap it, bake its texture and export it, and they need Blender for it.

| Script | What it does | Needs |
| --- | --- | --- |
| `meshopt_decode.py IN.glb OUT.glb` | Decodes a GLB that uses `EXT_meshopt_compression` (Tripo's download links do) into a plain GLB. Blender's importer and RED's `model-import` both refuse the compressed form. | Python 3, NumPy |
| `glb2jk.py IN OUT_DIR NAME --height H --faces F --tex T --cmp PALETTE` | Joins the model into one mesh, reduces it, unwraps it, bakes its base colour onto the new UVs, scales it to `H` JKU on the floor, and exports `NAME.3do` plus an indexed `NAME.mat` dithered to `PALETTE`, with four mip levels. If the export is over JK's per-mesh limits, it splits the model into meshes that fit. | Blender 5.1 (`blender -b --python glb2jk.py -- ...`, or the `bpy` module), the Blender Sith add-on (Coda's fork, 1.1.0 or later) via `--addon` or `BLENDER_SITH_ADDON` |
| `glowfaces.py MODEL.3do MAT_PREVIEW.png` | Sets faces whose texels are mostly warm and bright, such as a lamp's lens, to light mode 0 (fully lit), so they glow whatever the sector light. The model stays one mesh. | Python 3, Pillow |
| `merge3do.py IN.3do OUT.3do` | Merges every mesh of a 3DO into one, with each face keeping its own material and light mode. It only merges a model whose hierarchy has no offsets or rotations. Use it to try a model as one mesh. | Python 3 |
| `uvsheet.py NAME_uv.json` | Draws the UV layout that `glb2jk.py --uv-out` records, over the baked texture and on its own, so a painter can repaint the texture without moving any island. | Python 3, Pillow |

## The limits, and what has been measured

- **The engine's limits.** Jedi Knight's 3DO loader refuses the whole model when one mesh has more than 512
  vertices, 768 texture vertices or 512 faces, or a face has more than 24 corners. The Thing that uses it then draws
  nothing. RED's package planner reports that as `ModelOverEngineLimits`.
- **Four mip levels per texture.** The game keeps four and writes a fifth and later past its texture record, over
  whatever the heap holds next. The Blender add-on writes every level down to one texel: nine for a 256 by 256
  texture. `glb2jk.py` therefore keeps the first four of what the add-on wrote.
  - With nine levels, what happened depended on the model and the level around it. In retail JK 1.01 a 479-face
    pump drew for several builds of its level and then crashed the 16-bit renderer whenever it came into view; a
    1,199-face pump crashed it at once. The 8-bit renderer never crashed. Four levels cured both.
  - RED reports a longer chain: `mat-inspect` (a notice), `model-lint` (`retailMipLimit`), `archive-create` (a
    warning) and `validate` (`assetNotices`). See the
    [retail texture mip limit](../../docs/agent_design.md#retail-texture-mip-limit).
- **A model's total size has not been a limit.** An earlier version of this page said a 1,199-face model split into
  three or four meshes crashed the 16-bit renderer every time, and RED warned above 512 faces in all. That model's
  MAT had nine mip levels. With four, the same three-mesh and four-mesh models draw in both renderers, in the same
  test level.
  - 1,199 faces in four meshes is the largest model tried. `model-import` reports `overRetailTestedSize`, and the
    package planner warns with `ModelOverRetailTestedSize`, above that.
  - Two-mesh models at 444 faces (Wren's Votary statue) and 138 faces (the brazier) and their one-mesh merges drew
    in both renderers.
  - The stock models in `Res2.gob` have at most 392 faces.
- **A working budget per mesh** (Wren's): at most 460 faces, 400 vertices and 650 texture vertices, all faces flat.
- **Tripo's quad output is still too big for one JK mesh:**
  - Smart Mesh P2.0, quad, at its 500 minimum, gave 520–630 faces (Wren's statues and brazier).
  - Retopology at 500 gave 775 faces and 663 vertices (Red's pump).
  - Tripo's own UVs come to over 1,000 texture vertices either way, so throw them away and re-unwrap.

## A route that works

1. **Generate** in Tripo with Smart Mesh P2.0, quad, at 500, and download FBX, which keeps the quads. A GLB holds
   only triangles. A GLB download link needs `meshopt_decode.py` first.
2. **Reduce:**
   - `--reduce wren` for Smart Mesh quads: unsubdivide, planar dissolve at 8°, then collapse without triangulating.
   - `--reduce collapse` (triangles) for an HD model.
   - On Red's Retopology pump, `wren` lost the round drum. For a hard-surface prop made by Retopology, the HD
     model decimated with `collapse` looked better.
   - Every mode triangulates only the faces that aren't flat: a corner more than 1e-4 JKU or 1° off the face's
     plane.
3. **Bake** with `--supersample 4`. This bakes at four times the texture size and box-filters down, which is
   clearly sharper than baking at the final size.
4. **Mark glowing parts fully lit.** Either set them in the same mesh with `glowfaces.py`, or make each a small
   fully lit mesh of its own, as Wren did with the Votary's blade and the brazier's crystals. Both drew in retail.
   Keep the total small whichever you choose.
5. **Give an 8-bit build 8-bit MATs.** The software renderer reads a 16-bit (RGB565) MAT as 8-bit and every
   surface stripes. `glb2jk.py` writes indexed MATs dithered to the level's palette. RED's `mat-create` writes a
   16-bit MAT and its 8-bit twin from one request.
6. **Repaint (optional).** Send a painter (Lily) the model with its UVs, the bake, Tripo's 4K base colour,
   previews, and the `uvsheet.py` overlay. Ask them to keep every island where it is.
7. **Check** with `red-agent model-lint MODEL.3do MODEL.mat` and `red-agent package` (or `archive-create` and
   `validate`), then **test in retail JK in both the 8-bit and 16-bit renderers**.

## Credits

- **Wren** (AI collaborator, via Grok Bot), MIT:
  - the `wren` reduction, the flat-face test, the supersampled bake and the per-mesh budget;
  - the lessons about painters and UV overlays.
  - These come from Wren's prop pipeline for the Citadel statues and brazier.
- **Red** (Claude), working with Andrei: the scripts, the meshopt decoder and the retail measurements.
- **Lily** (ChatGPT, working with Andrei): the concepts and repaints these tools serve.
- **Tripo:** generates the source models. Anything made from Tripo output is subject to Tripo's terms.
