# Textures, image generation and mapped review

> Copied into RED from [CodaLilitari/JK-MotS-Model-Workflow@ca011b8](https://github.com/CodaLilitari/JK-MotS-Model-Workflow/blob/ca011b84c4fa5e1f3755e1543be2c1c1d25063c9/skills/jk-mots-model-workflow/references/textures-and-imagegen.md) (MIT; see [LICENSE](LICENSE.md)). Regenerate with `tools/knowledge/import_knowledge.py` rather than editing this copy.

Generate useful surface sources and then map them deliberately. A beautiful square image is not an accepted texture atlas. The useful evidence is the exact native source, a reproducible transform into UV space, inspection at final texture sizes, and actual appearance on the target mesh and runtime.

This document covers character and prop models. It does not prescribe level materials, map lighting or level design.

The [community surface research](community-surface-research.md) adds original modelers' MAT, palette, mipmap, UV and shading advice, with historical converter differences and unresolved explanations identified.

## Choose a texture method

| Method | Best use | Cost or limitation | Practical recommendation |
|---|---|---|---|
| Hand painting on the UV layout | Faces, exact seams, insignia and deliberately stylized shading | Requires painting time | Use for precision and final corrections, even when a generator supplied the starting image. |
| Procedural Blender materials | Broad cloth, bone, metal or repeated material color | Node graphs and generated coordinates need baking for portable delivery | Keep procedural detail restrained, then bake to the actual game UVs. |
| Built-in GPT/image generation | Concept development, palette/material pilots and regional diffuse sources | Placement and lighting are approximate; backend/version may be unexposed | One region per call, explicit reference roles, then deterministic mapping. |
| Local Comfy image generation or editing | Repeated private experiments when a suitable local image model is already installed | Requires model-specific workflows, VRAM and seed/version records | Preserve workflow JSON and conditioning; make one small pilot before a production set. |
| Donor or photo-based texture | A properly licensed source already contains the needed material | Perspective, baked lighting and licensing need attention | Rectify and de-light as recorded derivatives; inspect mapped seams. |
| High-to-low appearance bake | A good high-resolution or generated source mesh already exists | UV/cage/projection work; source PBR channels may exceed target capabilities | Bake supported color/detail onto a deliberate game mesh; retain silhouette in geometry. |

These methods combine well: a generated material source plus procedural color control and a final hand-painted seam pass is often more controllable than repeated whole-atlas generation. For a simple recolor, use a deterministic color edit. For a genuinely new design, offer a reference/concept stage before geometry or texture production.

For the Blender-Sith 1.1.0 integration, [native MAT export from baked
images](blender-export-and-baking.md#native-mat-export-from-baked-images) can encode
the inspected image or a Material's complete cel set directly. Select the output
encoding explicitly and provide the real CMP for indexed output; the API's
`RGBA32` default is not a JK/MotS compatibility recommendation. This saves a
conversion step after baking, not the UV/image bake or target-renderer review.
Keep existing converter recipes when the selected installation predates that API.

## Establish the texture contract first

Record material regions, anatomical side, UV orientation, runtime dimensions, working dimensions, intended pixel density, seam neighbors, palette/transparency policy and the reference role of each input. Agree on the face/identity and one representative clothing pilot before mass production.

A historical character workflow used 1024-square working tiles, a 4096 physical atlas, 256 base MATs, top-left image UV conventions and 12-pixel gutters at the 1024 working scale. These are an example contract, not engine-wide rules. Choose dimensions and packaging for the target format/profile and test them. Document where top-left image coordinates are converted to Blender/export UV conventions; validate with an asymmetric checker or labelled test map rather than guessing a vertical flip.

Divide production by region: face, head sides/ears, neck/chest, arms/hands, crown hair, hair lengths, garment front/back, sleeves, footwear and hardware. Keep repeated material colors consistent, but do not use symmetry where anatomy or the design differs. Individual tiles let a failed boot or ear source be replaced without regenerating an accepted face.

## Use image generation as a bounded stage

Use the active environment's image-generation skill and documented tool. Built-in image generation is the normal route when available. A separate API/CLI route can have different account, cost, model, fidelity and transparency behavior; choose it only within the user's authorization. This workflow does not require a paid API key.

For every call:

1. Inspect exact input images. Declare each as an edit target, identity/reference image, UV guide or material/palette reference.
2. Record a precise prompt and an ordered allowlist of the exact files supplied. Avoid implicit previous-image carryover between regions or characters.
3. Request one distinct asset per call. A many-region atlas request is harder to fit, repair and audit than separate sources.
4. State what changes and what must remain: pose, silhouette, feature placement, digit count, crop and material colors as applicable.
5. Preserve the returned native bytes and measured dimensions in the workspace; retain the original output reference. Write resizing, warping, color adjustment and atlas assembly to separate derivatives.
6. Inspect at original detail, then on the mesh. Stop after an accepted result unless a new defect requires a bounded correction.

Do not certify an image model version solely from a UI label, request wording or filename. The historical built-in generator did not expose a verifiable backend identifier. Likewise, requested dimensions were not output dimensions: several 1024-square requests returned native 1254 × 1254 RGB PNGs. Record what actually arrived.

Use a request/asset record such as:

```json
{
  "asset_id": "head-side-01",
  "status": "native-produced-unmapped",
  "tool_route": "built-in-image-generation",
  "backend_model": "not-exposed",
  "prompt_file": "requests/head-side-01.txt",
  "references": [
    {"path": "inputs/design.png", "role": "design-reference", "sha256": "RECORD_ACTUAL_HASH"},
    {"path": "inputs/uv-guide.png", "role": "layout-guide", "sha256": "RECORD_ACTUAL_HASH"}
  ],
  "native": {"path": "native/head-side-01.png", "sha256": "RECORD_ACTUAL_HASH", "width": 0, "height": 0},
  "derivatives": [],
  "mapped_review": "pending",
  "runtime_review": "pending"
}
```

Replace placeholder hashes and dimensions with measured values before calling the record complete. Preserve failure/request metadata when a call returns no image. On interruption or quota recovery, inspect existing manifests and any live calls before retrying. A reused or deterministically derived tile must be labelled as a derivative, never as a new generator native.

## Prompt for the actual job

Use short explicit requests. More aesthetic adjectives do not establish material correctness. Give each reference one role and repeat the invariants on each edit.

### Diffuse material source

```text
Asset: one square diffuse-color source for a low-poly game character's [region].
Use the supplied design only for [palette/material/identity]. Follow the separate
UV guide for feature placement and anatomical side.
Show intrinsic surface color under uniform neutral illumination. No directional
cast shadow, highlight, vignette, rim light, lens effect, baked ambient occlusion,
text, labels or watermark. Keep grain subtle enough to survive downsampling.
Preserve [specific color, seam and landmark constraints].
```

For face/ears, add explicit eyes/nose/mouth/ear landmark requirements and an unambiguous left/right convention. A generated UV placement is approximate until measured and mapped. Correct a strong identity source with deterministic landmark fitting where possible instead of repeatedly asking a generator to rediscover it.

For repeatable materials, ask for a tileable source only when useful, then actually test wrapping both axes. The word “seamless” is not validation. For isolated transparent elements, request genuine alpha if supported and verify the result's alpha channel. A checkerboard painted into RGB is not transparency.

### Geometry conditioning image

```text
Asset: one isolated [body region] reference for single-view image-to-3D.
Neutral pose; complete region visible with enough overlap for assembly.
Plain background; separate limbs/digits and clearly visible required openings.
Preserve [silhouette, counts, shape landmarks]. No labels, collage, perspective
turnaround sheet, floor shadow or extra object.
```

Geometry conditioning benefits from legible shape shading. Diffuse texture sources should avoid directional lighting. Do not use one prompt interchangeably for both purposes. A colored-to-clay edit may change relief, proportions and depth shading even when the prompt says “color only”; compare actual pixels and silhouettes before claiming a controlled color experiment.

## Look for painted lighting and false detail

Inspect native images for catchlights, heavy eyelid/ear shadows, directional highlights, shiny hair, broad metal reflections, excessive pores, coarse leather grain and dark halos. These can look good in the source while becoming fixed lighting artifacts on a moving model.

| Source symptom | Consequence on a model | Correction lane |
|---|---|---|
| Catchlight painted into an eye or face | Highlight stays fixed as lighting changes | Remove/subdue in a derivative or make it an explicit stylized choice. |
| Deep ear/neck shadow | Double shading and dark seams | Adjust diffuse source; check under neutral illumination. |
| Large pores or cracks | Skin/bone reads as stone at game pixel density | Reduce frequency/contrast; do not turn color noise into displacement. |
| Wide painted glow halo | Colored patch spreads across socket or skin | Isolate the eye/lens region and constrain UV coverage. |
| Uneven tile brightness | Neck/face or sleeve/body seam | Compare mapped material and seam samples; record color transforms. |
| Detailed hair sheen | Painted stripes conflict with engine light | Reduce fixed highlights and test hair orientation. |

A historical bone source read as porous limestone at full strength. A restrained base-color mix was useful as a material pilot; interpreting its cracks as bump/displacement would have altered the anatomy. A pearl-like eye source looked bright but did not establish emission, bloom or illumination of adjacent geometry. Keep image brightness, material flags and emitted world light as separate claims.

Do not reject a source only because a zoomed preview looks coarse. UV density may magnify the source too much. Check the UV-to-world mapping and intended viewing distance before regenerating texture detail.

## Deterministic integration

Freeze native sources, then perform recorded operations in a separate integration stage:

1. Fit landmarks and crop to the agreed UV region. Save control points and transformation parameters. Preserve facial identity and anatomical side.
2. Match adjacent region colors in the relevant color space. Record gains/curves instead of describing them as new recovered detail.
3. Resample to working dimensions; add edge dilation/padding appropriate to the actual island layout and mip chain. A 12-pixel gutter at 1024 becomes 3 pixels at 256 before subsequent mip reductions; inspect lower levels too.
4. Assemble atlas/material images reproducibly with a region manifest. Avoid regenerating the complete atlas as an image edit.
5. Inspect on the final target UVs with neutral materials, then at every shipped resolution and representative mip levels.
6. Convert using the target material/palette/transparency contract; reload the exported result to inspect the data actually delivered.

Do not silently assume an output-hint string is a clean filesystem path. Tool outputs can contain surrounding explanatory text; resolve the native path through the documented result and check file existence. A byte hash of the saved native verifies that copy. It does not verify inline response data that was not retained.

## Baking source appearance onto the game mesh

Settle the selected geometry and target UVs before the final bake. Keep source meshes, game meshes and cages visibly distinct. Use an intentional low-poly atlas rather than blindly unwrapping a million-triangle generated assembly.

The source material may use Blender-only generated coordinates, box projection, procedural masks, color mixing or node graphs. A successful Blender render does not make those features portable. In the local skull pilot, box-projected bone color required a bake into a UV texture before standard GLB or JK delivery; no equivalent portable GLB was claimed at that stage.

For a diffuse-only target, prepare a color bake that excludes studio lights, specular reflections, direct/indirect illumination and unwanted AO. In Blender this can be done with the appropriate color-only bake configuration or an explicitly prepared emission-color transfer; inspect the selected bake channels in the current Blender version. Keep genuine material color separate from optional hand-authored shading. Verify the bake with a neutral unlit inspection and a lit inspection.

Bake geometry detail only into channels that the chosen renderer actually consumes. A normal, roughness or metallic map in a modern source asset does not automatically affect the legacy runtime. Prioritize silhouette geometry and diffuse readability for a baseline target; retain richer source maps as optional source artifacts with clearly scoped renderer support.

Check ray cage/normal direction and projection distance on narrow gaps, teeth, fingers, layered cloth and overlapping hair. If adjacent parts contaminate one another, isolate the bake by region or use an exploded source/target arrangement with a recorded transform. Inspect seams, missed pixels, front/back projection, mirrored UVs and color bleed before packing.

Separate eyes, teeth or material regions in Blender need not consume separate animated hierarchy nodes. Follow the target mesh/node budget and exporter contract when consolidating materials.

## Select the runtime material path deliberately

There are three useful options: ordinary indexed MATs for a palette-aware baseline; RGB565 MATs for a supported full-color path; and an explicitly supported external replacement-image route for a particular port. The actual renderer and package determine compatibility. A JKE package requiring a patch does not prove every isolated 256-square RGB565 player skin requires that same patch.

**Do not scale 3DO UVs just because the texture image became larger.** In the inspected OpenJK native OpenGL path, `std3D_GetValidDimension` clamps the normalization dimension to 256 while upload can retain the full image size. Shred's inspected 2048-square Undead atlas uses a 256-unit UV period. Multiplying its coordinates by eight would change/repeat the mapping on that path. This is a renderer-specific convention, not a universal retail rule. The same clamp is visible in [the upstream OpenJKDF2 GL backend](https://github.com/shinyquagsire23/OpenJKDF2/blob/master/src/Platform/GL/std3D.c); pin the actual tested commit for a release.

Before a large-atlas conversion, use an asymmetric test map to verify one logical texture period on the intended renderer. Separate an image-resolution change from an atlas repack: repacking islands requires a mapping change regardless of image dimensions. Test external replacement registration separately from direct MAT upload, using a fresh process when caches can retain previous material choices.

For gray or diagonally sheared **JKGM PNG replacement** textures, inspect row layout before resizing or repainting. An RGB image 1254 pixels wide has 3762 bytes per row, which disagrees with a four-byte-aligned unpack stride of 3764; opaque RGBA at the same dimensions has 5016 bytes per row. The existing [maintained PNG alignment helper](https://github.com/CodaLilitari/OpenJKDF2-MotS-Fixes/tree/main/scripts/model-workarounds/png-alignment) reports this risk and can write a separate opaque RGBA copy preserving decoded RGB pixels and dimensions. Its [historical standalone repository](https://github.com/CodaLilitari/jkdf2-jkgm-png-alignment) includes the scoped observed case and synthetic tests. Inspect first; conversion is a conditional workaround for the affected upload path, not a general demand to convert every texture. That historical engine observation was under Wine/Mesa and did not establish retail or native Windows GPU behavior.

In the historical native GL comparison, native-size one-mip RGB565 MATs and external PNG replacements produced closely aligned fixed-view appearance. This was bounded native-port evidence; it did not establish pristine retail compatibility, motion, distance sampling, profile-picker behavior or connected-peer multiplayer.

Stored mipmaps remain relevant. The inspected GL path selects stored MAT mips and uploads each as a separate texture, rather than automatically supplying a modern GPU mip chain. For an oversized 1024 base, the audited denominator calculation predicted different logical periods at later stored mip levels; that specific distance defect was a source prediction, not a reproduced runtime result. Compare near/mid/far and moving views before shipping. A single mip avoids those particular transitions but can shimmer at distance; it is an option with a cost, not a universal quality setting.

Useful lessons from the fan-model comparison are intentional UV islands, readable anatomy, selected sharing of symmetric surfaces and appropriate texture sampling. Hundreds of overlaps across a broad noise tile cannot become unique rib, finger or hair detail merely by increasing resolution. Unknown MAT header words and a large triangle count are not demonstrated quality switches. Shred's tutorial was identified during the historical research but not watched in that audit; these conclusions come from parsed fan assets and renderer source, not an invented tutorial account.

## Acceptance gates

| Stage | Required evidence |
|---|---|
| Native production | Exact prompt, ordered references, actual native file/hash/dimensions, original-detail inspection. |
| UV integration | Recorded transforms, correct landmarks/side, seam continuity and density on the target mesh. |
| Bake | Target UV/image/material setup, projection parameters, clean pixels and portable appearance. |
| Export | Reimported texture/material data, dimensions, palette/alpha behavior and material assignment. |
| Runtime | Actual target renderer, settings and model tested; single-player and multiplayer evidence scoped separately. |

A generator worker should hand off accepted sources and candid defects without claiming final engine acceptance. An integration worker should preserve the natives and identify every derivative. An independent reviewer should see references and actual mapped renders before the builder's explanation, then report discrepancies that can be checked against the files.
