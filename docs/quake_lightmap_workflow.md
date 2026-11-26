# Bake Quake lightmaps into native JK and MotS materials

Use this workflow when a Quake III or Quake Live room loses a colored pool of light, a shadow edge, or detail between vertices after a RED model-scene import. The baker samples the source diffuse texture and RGB lightmap using their separate UVs, then writes the product into an opaque MAT atlas. The remapped 3DO face displays this lighting once. Player models, moving objects and translucent artwork can keep their original lighting.

## Prepare the inputs

Start with a working [RED Quake import](jo_ja_import.md#preparing-a-quake-iii-or-quake-live-map). Retain the original IBSP v46/v47 or PK3, the exact `jo-ja-plan` JSON, the two like-named episode archives for the target game, and the source artwork you have permission to use. Keep the parent indexed and hardware editions in separate directories. Run the workflow separately for JK and MotS.

Use Python 3 with NumPy, Pillow and SciPy in your authoring environment. Install the optional packages with `python -m pip install numpy Pillow scipy`. A player only needs the resulting level archive. Commands below use the helper names from `tools/engine-capture/`. Use an existing work directory outside the RED source checkout and a fresh output directory for each step.

Make an explicit shader binding for each opaque material you want to bake. In `materials.json`, give the original shader name, resolved base artwork, native MAT basename and native MAT size in pixels. For example:

```json
{
  "shaders": {
    "textures/arena/brick": {
      "chosenStage": "textures/arena/brick.jpg",
      "mat": "stone.mat",
      "dimensions": [256, 256]
    }
  }
}
```

Supply source artwork relative to the bindings file or with repeated `--pk3` arguments. Later archives override the same member in earlier ones. Use an explicit image extension when several source formats exist. A reviewed additive stage can be described with `"additiveStages": [{"image":"textures/arena/glow.tga","gain":0.5,"scale":1,"wrap":true}]`. This captures a chosen phase of the effect. Omit shaders whose animation or blending must stay live. The baker excludes bindings marked cutout, translucent or fullbright.

## Bake and inspect the charts

```sh
python quake_lightmap_atlas.py arena.pk3 jk-plan.json hardware/arena.gob materials.json jk-atlas --member maps/arena.bsp --pk3 base.pk3 --pk3 arena.pk3 --patch-subdivisions 3 --scale 0.003125
```

Use the same scale and patch subdivision count as the import. The baker matches source surfaces to native model faces using the plan, positions, material and base UVs. It supports opaque lightmapped planar and patch surfaces in RED's single-mesh, single-geoset static 3DOs. The `rgb/` directory contains the 256 × 256 pages. The `models/` directory contains the corresponding native models. Inspect mapped and residual face counts, referenced page and model hashes, and source image hashes in `receipt.json`. The default refuses unmatched faces of eligible materials. Use `--allow-unmatched` only after reviewing why they should retain the native material.

Match a fixed source camera before choosing exposure. `--lightmap-shift` takes 0, 1 or 2 stops on the original BSP lighting, and `--display-gain` acts on the composed texture. For Fly on Rocket, comparing a torch wall, a shadow and neutral stone supported a shift of 1 and display gain of 2. Baking a shifted intermediate a second time would brighten the room again. `--density` sets the desired pixels per lightmap texel, subject to the chart fitting on a page. Compare detail up close and at a distance in the target game.

## Compile the two editions

```sh
python quake_lightmap_package.py jk-atlas indexed8/arena.gob hardware/arena.gob jk-result --red-agent /path/to/red-agent --level jkl/arena.jkl --palette-member misc/cmp/arena.cmp
```

The packager accepts one level with one archive-supplied master CMP. RED's `cmp-master-create` protects palette entries used by the parent indexed episode and adds colors sampled from the charts. Add `--protect path/to/borrowed.gob` for other indexed artwork using that same palette. RED writes opaque one-mip indexed MATs without dithering and RGB565 MATs from the same pages. These settings preserved stone and distant detail in the reference conversion. The packager verifies parent model hashes, page and model output hashes, new material references, and the finished pair. It adds the new MATs to the JKL while preserving the original number of free runtime slots.

The fresh output contains `indexed8/arena.gob`, `hardware/arena.gob` and a receipt. Existing collision, scripts, episode metadata and artwork outside the selected resources are copied from the parent. Preserve original authorship and distribution terms when preparing delivery ZIPs.

## MotS software draw order

Bake from the matching MotS plan and GOO. When the accelerated view looks right but hidden walls appear over the architecture in software, compare the normal result with the guarded ordinary-Thing pass:

```sh
python quake_lightmap_package.py mots-atlas indexed8/arena.goo hardware/arena.goo mots-result --red-agent /path/to/red-agent --level jkl/arena.jkl --palette-member misc/cmp/arena.cmp --mots-parent _jjworld
```

Use the exact static parent template from the imported level. Before changing its selected model templates, the helper checks every placement and compares each remaining Gouraud vertex's 3DO scalar with the corresponding ARCH mono, red, green and blue values. A disagreement stops the conversion. Keep distinct per-placement color in ARCH or bake it deliberately. To inspect this operation independently, run:

```sh
python mots_baked_draw_pass.py --jkl parent.jkl --plan mots-plan.json --models mots-atlas/models --parent-template _jjworld --output ordinary.jkl
```

Use the final baked models for this check. After packaging, compare software/indexed and accelerated/RGB565 captures from the same camera. Test the level in motion and connect a second machine or game instance using the other texture edition. The package receipt establishes shared gameplay bytes and material layouts. The game session establishes renderer behavior and multiplayer acceptance. See the [conversion checks](conversion_reliability.md#recover-colored-light-pools-on-imported-architecture) and [paired-edition verifier](../tools/engine-capture/README.md#paired-retail-texture-editions).
