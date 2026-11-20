# Import a Jedi Outcast or Jedi Academy scene

**Tools > Import JO/JA Map…** turns a compiled Raven BSP into a new JK GOB or MotS GOO. The same importer is available
through `red-agent` and MCP. It creates an authoring scaffold: world geometry, source texture coordinates, sampled
lighting, static model Things and player starts. Gameplay conversion and acceptance in the target game remain separate
work.

The input is an extracted `RBSP` version 1 `.bsp`, used by JO and JA. PK3 extraction, Radiant `.map` files and other BSP
dialects are not part of this command. Supply source files you have permission to use. Generated output never replaces
the source and contains no bundled stock game assets.

## Editor workflow

1. Open **Tools > Import JO/JA Map…**, choose the source BSP and select **Jedi Knight (GOB)** or
   **Mysteries of the Sith (GOO)**. Set a new output filename and a lowercase level name.
2. Choose **Inspect shaders**. Each source shader has a row with a target MAT file, its portable MAT basename and a
   **Skip** checkbox. Choose a MAT with **Browse**. Use [Material Authoring](material_authoring.md) to convert source
   images first. The importer reads the explicitly chosen files; it does not search a game installation or interpret
   shader scripts.
3. Map every visible shader, explicitly skip a source effect, or enable the option for checker materials. Checkers are
   conspicuous placeholders and are listed in the report. Sky, nodraw surfaces and billboards are omitted automatically.
   Skip additive flares when their source shader requires blending that the opaque MAT cannot reproduce. A shader
   cannot be both skipped and mapped.
4. Keep scale at **1 / 320** for the initial review. Native BSP coordinates are already Z-up: an OBJ axis rotation does
   not belong here. Patch subdivisions control fixed quadratic sampling; increasing the value increases geometry.
5. Choose **Model scene** for static 3DOs inside one enclosing sector, or **Hybrid sectors** for native rooms,
   portals and models that hold the remaining detail. The dialog initially suggests 400 faces per 3DO and a
   200,000 source-triangle budget for hybrid geometry. Adjust sector and fragment limits to the source.
   **Make rendered model triangles collidable** applies to model geometry. In hybrid mode, native room boundaries
   and portals exist independently of this checkbox. Clip brushes, water volumes and player/weapon filters need
   separate authoring.
6. Choose **Prepare**, then read the counts, material mappings, skipped shaders, audio flags, limitations and hashes.
   Preparation writes no archive. Editing an input invalidates the prepared result; canceling work or closing the dialog
   prevents publication.
7. Choose **Create new archive**. Inputs are rechecked before publication. An existing destination is refused. The new
   archive is independent of the open document; creation does not replace the document or mark it dirty.

Open the generated JKL with the new archive available as a resource source. For a hybrid import, inspect reciprocal
adjoins in the sector editor and the sector assignments of the remaining model Things. Test travel, rendering and
frame times in the target game with the original model-scene conversion available for comparison.

## Materials and lighting

Each mapped MAT must have one texture cel, power-of-two dimensions from 8 through 256, and a complete stored mip chain.
Indexed 8-bit MATs require an explicit matching CMP. The importer can also capture opaque RGB565 MATs with explicit
5:6:5 channels. RGB565 renderer support must be qualified in the intended game; it is not an automatic fallback to an
indexed edition. The selected CMP also affects stock assets, so use a palette compatible with the stock resources the
scene references.

Hybrid sectors require opaque mapped MATs. During preparation, an alpha declaration in the MAT header or a color-key
flag on its texture cel causes the importer to name the material and stop before writing an archive. A masked grille
can have visible openings even when its source brush is solid, so treating the entire face as an opaque portal
boundary would hide geometry behind the grille. To keep a supported indexed cutout, choose **Model scene** and supply
its matching CMP. That mode stores the original MAT bytes. To use hybrid sectors, supply an opaque replacement or
explicitly skip the shader if its geometry should be omitted. The successful preparation report lists the material
mapping or the skipped shader for review.

Source base UVs are converted to MAT pixel coordinates. The first 128-by-128 lightmap atlas/style supplies bilinearly
sampled corner luminance; surfaces without one use source vertex colors. Models use scalar 3DO 2.1 vertex intensities.
MotS receives explicit ARCH lighting records bound to each architecture Thing. Dynamic light styles, source renderer
gamma/overbright, colored lighting and shader-stage modulation are not reproduced.

Source shaders can contain animations, alpha tests, transparency, additive stages and multiple textures. A base image
alone does not preserve those behaviors. Exact shader names remain in the report and source entity text is retained in
`misc/joja-source.ent` for later authoring.

## Footsteps and other audio

The import dialog's **Source game** selects Jedi Outcast multiplayer or Jedi Academy multiplayer. Both use RBSP v1,
so the header alone cannot distinguish them. Through the CLI or MCP, set `sourceProfile` to `jo-mp` or `ja-mp`.
An omitted field, or `legacy`, preserves the combined rules, archive bytes and manifest identity of existing recipes.
The GUI also offers those legacy rules for reproducing an earlier import.

The stock audio policy uses available JK/MotS cues. Architecture chunks are separated by their target footstep class
because the target engine chooses a contact sound from the attached **Thing's flags**. A model face's MAT name or face
flags do not select an independent footstep. In hybrid mode, a promoted world face carries the corresponding native
surface flags. A player can make contact with a native solid floor even with model collision disabled.

| Source evidence | Target model cue | Report |
| --- | --- | --- |
| JO MP `METALSTEPS` | Metal | JO ignores the low material bits for this cue |
| JO MP ordinary surface | Hard | Generic JO footstep |
| JO MP `NOSTEPS`, including alongside `METALSTEPS` | Hard fallback | JO suppresses the source step. Stock target architecture flags cannot guarantee silence |
| JA MP solid metal / hollow metal (pipe) | Metal | Pipe is marked for review. JA's traced animation steps use the low five material bits and ignore `METALSTEPS` |
| JA MP dirt / sand, gravel, snow or mud | Earth | Sand, gravel, snow and mud are marked as approximations |
| JA MP grass / wood / rug, fabric, canvas, rubber or plastic | Hard fallback | Needs audio review |
| Other JA MP values, including leaves and unspecified | Hard | The traced JA animation selects its stone family |
| JA MP `NOSTEPS` | Material-based cue, flagged for review | The movement event uses zero for this flag; the traced animation uses the underlying material. The import makes no guarantee of silence |
| Water contents, or JA water material | Existing contact cue, flagged for review | Liquid movement and splash events still require authoring |

Model flags matter when the player is attached to a colliding architecture model. Sound classes and samples come
from the target game's stock player references. The selected flags and a structural readback do not verify playback.
No source WAVs, ambience, music, triggers or sound routing are imported.

The **Original JO/JA sounds (unavailable)** choice is disabled. Agent requests using `audioPolicy: "source-import"`
receive `joJaSourceAudioUnavailable`. Supporting that choice requires verified source assets and an actual playback
mapping; copying WAVs into an archive alone does not establish per-surface routing. The current report preserves the
source material classification and exposes the cases that need this follow-up.

## Agent workflow

Inspect the extracted source:

```text
red-agent jo-ja-inspect scene.bsp
red-agent schema jo-ja-plan
```

The inspection supplies `sourceSha256`, shader names and flags, entity classes, model counts and brush counts. A minimal
request with explicit placeholder permission is:

```json
{
  "schemaVersion": 1,
  "source": { "path": "scene.bsp", "sha256": "<SHA-256 from inspect>" },
  "target": "jk",
  "levelName": "joja_scene",
  "allowPlaceholders": true,
  "audioPolicy": "stock-equivalents",
  "sourceProfile": "jo-mp"
}
```

For real artwork, set `allowPlaceholders` to false and add `materials` entries:

```json
{
  "shader": "textures/example/floor",
  "name": "floor.mat",
  "source": { "path": "materials/floor.mat", "sha256": "<raw MAT SHA-256>" }
}
```

Add `palette: {"path": "level.cmp", "sha256": "<raw CMP SHA-256>"}` for indexed materials. Use exact source names in
`skipShaders`, for example `"skipShaders": ["textures/example/additive_flare"]`. Unknown request fields, duplicate
mappings, conflicting target basenames, and absent shader names are refused.

```text
red-agent jo-ja-plan import.json
red-agent jo-ja-apply import.json scene.gob <outputSha256> <manifestSha256>
```

Set `geometryMode` to `model-scene` (default) or `hybrid-sectors`. The hybrid receipt reports the sector and portal
counts, native face count, remaining model faces (`residualFaces`), their triangle equivalents (`residualTriangles`),
each model's owning sector, and area coverage for the clipped source triangles.

`nativePromotionFallbacks` counts source surfaces retained as models when target precision prevents a valid native face.
`maximumRelativeCoverageError` measures the area difference after partitioning. Retail frame times require a separate run.

Plan returns the report, model/material inventory and both review hashes. Apply recaptures the inputs, regenerates the
archive and checks the reviewed output and manifest before creating one new file. Use `target: "mots"` with a `.goo`
output. Request fields, including explicit skipped shaders even when unused by world faces, are bound by the manifest.

MCP exposes `sed_jo_ja_inspect`, `sed_jo_ja_plan` and `sed_jo_ja_apply`. Plan/apply use a nested `request`. Apply also takes
`output`, `expectedOutputSha256`, `expectedManifestSha256` and an optional journal `operationId`. Existing MCP path
policy, resource roots and journal conflict/replay rules apply. See [Agent workflow](agent_workflow.md).

## Reimporting an authored episode

The agent accepts an optional `reimport` object in the usual `jo-ja-plan` and `jo-ja-apply` request.
Use the same target game and level name. Supply the original generator request as `baselineRecipe`
with the exact SHA-256 of its published archive as `baselineOutputSha256`. Supply the edited episode
as `authoredArchive`, a `{path, sha256}` file reference. The outer request contains the new BSP and
conversion settings. The importer replays both recipes from their declared inputs.

Each entry in `overrides` declares an authored archive member by portable relative name and role.
For example, `{"entry":"mat/night_sky.mat","role":"sky"}` declares an edited sky material. Supported
roles are `sky`, `ambience`, `palette`, `starts`, `throwables`, `script`, `authoredLighting`, `collision`,
`gameplay` and `visual`. The importer compares the original generator output, authored episode, and
new generator output. Every manual addition, replacement and deletion requires a declaration. All
other members come from the new generator. The result uses the same create-only publishing contract.

The review records old, authored and regenerated entry digests. Its source tables are indexed within
the recorded BSP digest, with separate counts for entity, brush, surface and shader indices. Generated
models list their source surfaces. A changed BSP therefore cannot establish cross-import identity
by reusing a surface number or generated model filename. A declared MAT replacement also requires
the generator to retain its shader association. Independent MAT, CMP and WAV edits can survive a
change in tessellation or partitioning when the generator still supplies the same resource baseline.

A `joJaReimportConflict` response contains one row per unresolved archive member, its role when
declared, and three content hashes (null for an absent member). Examples include `bothChanged`,
`sourceIdentityChanged`, `topologyChanged`, `shaderIdentityChanged` and `undeclaredManualChange`.
For overlapping level edits up to 4 MiB per version, `authoredFields` and `regeneratedFields` show
bounded model differences from the baseline. These views use snapshot coordinates. A changed JKL,
COG or model that may reference moving geometry requires a designer decision; no archive is
published while a conflict exists. The engine must still verify movement, scripts, audio and collision.

## Budgets and remaining authoring

| Setting | Default | Admitted range or bound |
| --- | --- | --- |
| Scale | 1 / 320 | Greater than 0, at most 1; geometry collapsing at target precision is refused |
| Patch subdivisions per quadratic block | 4 | 1–16 |
| `meshOptimization` | `"off"` | `"off"`, `"strict"` or `"loose"`; see [Mesh optimization](#mesh-optimization) |
| Faces per model (`trianglesPerModel`) | 140 | Model scene emits 1–170 triangular faces. Hybrid admits 1–479 convex faces, subject to position and UV limits |
| Source triangles | 100,000 | Model scene up to 100,000, hybrid up to 250,000 |
| Hybrid sectors | 64 | 1–96 |
| Hybrid fragments | 500,000 | 1–1,000,000 |
| Model chunks | 512 | Configurable budget up to 512 |
| Tessellation work | Twice the triangle budget | Counts degenerate candidate triangles too |
| Material mappings or explicit skipped shaders | 256 each | Exact source shader names |
| Captured BSP | — | 128 MiB |
| Captured MAT data | — | 16 MiB per file, 128 MiB aggregate |
| Captured CMP | — | 1 MiB |
| Generated resource data | — | 256 MiB |

World model 0 is converted. Doors, lifts and other submodels remain omitted and counted. Deathmatch starts take
precedence over the single-player marker, with at most 32 starts. If none exist, a clearly reported untested review
start is placed above the geometry. The archive includes target-specific stock player references and conservative
runtime resource capacity allowances; stock dependency closure and runtime allocations still require game testing.

Hybrid uses source BSP partitions to build bounded convex cells. Matching planar boundary geometry can become
native textured faces. A planar surface can supply an additional partition candidate when every triangle corner
lies on the same side of a finite world brush and inside its other sides. The draw shader must match that side.
Both the brush material and draw material must declare solid, opaque contents with the translucent bit clear.
These compiled flags describe brush contents. Source shader stages can still change how the artwork looks.
Uncertified triangles and curves remain available as model detail. Exact source visibility, trigger behavior,
pickups, enemies, water physics, mission logic and multiplayer state still require qualification or authoring.
Fixed patch sampling is not an error-bounded or crack-free
tessellation guarantee. Large one-sector scenes can exceed a renderer's practical limits even when each model fits
the loader's per-mesh limits. RED's software snapshot also has its own model/resource budgets.

The import report is stored in `misc/joja-import.txt`. `runtimeVerified` remains false. Qualify the exact archive in its
target engine, including collision, lighting, starts, sounds and multiplayer travel, before distributing a playable map.
Fullbright editor comparisons can check geometry and UVs. The current software snapshot's baked-light path shades
native world surfaces but uses sector ambient for model Things, so it cannot establish matching authored model/world
lighting. Check serialized corner and ARCH records separately, then compare the intended target renderer.

## Mesh optimization

The model-scene importer writes every triangle with three vertices of its own, so a model has three vertices per
triangle even where neighbours share corners. Set `meshOptimization` in an agent or MCP request to clean up each
generated 3DO mesh before it is written. The import dialog doesn't offer it yet.

- `"off"` (the default) writes models exactly as before and keeps existing recipe hashes.
- `"strict"` welds vertices that share a position and light when their normals differ by at most 0.5 degrees,
  keeping the first normal, and shares identical texture vertices. It then merges coplanar neighbouring faces that
  share an edge, material and modes into convex polygons of up to 8 corners, but only where the corners' light stays
  within half an 8-bit step and their texture coordinates within 1/32 texel of a linear fit across the merged face.
  The engine draws a polygon as a fan of triangles, so this keeps every merged face drawing as its triangles did.
- `"loose"` welds the same way and allows 4/255 of light and half a texel, for more merges.

If the first three usable corners of a polygon nearly line up, their UV or light gradient can be numerically unstable.
After the original merge pass finishes, the optimizer retries rejected fits using the widest projected pair from
the same origin. This extra pass requires each corner to fit within half the selected tolerance, so two triangle
fans interpolating those residuals stay within the full tolerance. It applies to both opt-in levels.

Positions, light values, texture coordinates and winding never change, and the source triangle count, model chunks
and hybrid coverage stay the same. Only dynamic light reads vertex normals. The plan reports `optimizedVertices`,
`optimizedFaces` and `worstNormalTurnDegrees`, and the import report and manifest name the level. For JA's
`ffa_bespin` at four patch subdivisions, strict took the model scene from 70,095 vertices and 23,365 faces to 26,321
vertices and 17,017 faces, and the JK archive from 9.4 MB to 4.1 MB. Loose reached 13,393 faces.

Red brought these rules into the importer from the Sith Council conversion. There, the weld and merge together with a COG fix made the
level about a fifth faster in OpenJKDF2 without visible change. A merge that ignored the linear-attribute check moved
textures by up to 149 texels inside merged faces, which is why the check is not optional. Patch sampling is still
fixed per quadratic block: adaptive sampling would need adjacent patches to agree on their shared edges to avoid
cracks, and isn't done here.

## Choosing the static world model render pass in JK

An agent or MCP import request can set `"worldModelRenderPass": "ordinaryThing"` with
`"target": "jk"`. This puts generated static world 3DOs in JK's ordinary Thing pass and retains
Gouraud shading at distance. The importer clears `LEVELGEO` (`0x8`) and sets
`IGNOREGOURAUDDISTANCE` (`0x04000000`) on the generated world template and its model overrides.
This covers model scenes and residual 3DOs in hybrid sectors. Standability, footstep flags, collision
settings, sector geometry, player starts, 3DO light samples and textures keep their original values.

Use the option when checking depth ordering in the original JK renderer. In one indexed
Sith Council hall comparison, the stock software renderer recovered the central ramp and doorway.
Other scenes and viewpoints need their own comparison. For the original software renderer, supply
indexed MATs and their matching CMP. Changing the pass does not convert RGB565 textures.
When baking lighting again in RED, its LEVELGEO-only static-model ray-bake path can omit these
ordinary Things. The per-vertex light already stored in the generated 3DOs is preserved.

Omitting the option, or setting it to `"levelGeometry"`, reproduces the original package and
recipe hash. The ordinary pass gets a distinct recipe hash. MotS accepts only `"levelGeometry"`
and keeps its architecture lighting records.

## Reviewing source visibility

Use `red-agent jo-ja-inspect gallery.bsp --visibility 7` to examine the potentially visible set (PVS) for
Raven cluster 7. The source-surface page lists the clusters of reachable world leaves that reference each draw
surface. The corresponding MCP call is `sed_jo_ja_inspect` with `{"path":"gallery.bsp","sourceVisibility":{"viewCluster":7}}`.
The reported IDs are specific to the BSP identified by `sourceSha256`.

Add `"sourceVisibility":{"viewCluster":7}` to a normal JO/JA plan or apply request to see what that source row
says about the generated geometry. A native target names its sector and sector-local surface. A residual or
model-scene target names its placed Thing, 3DO and mesh. Its `sourceSurfaces` list comes from the emitted packet,
including pieces split by hybrid clipping. This is deliberately mesh-level ownership when optimization joins faces.
Generated enclosure surfaces and portals have no source draw-surface owner.

- `potentially-visible` means the source row marks at least one contributing owner cluster.
- `outside-pvs` requires complete known ownership with every owner outside the row.
- `unknown` stays visible. Examples include a missing PVS, a negative or unrepresented viewer cluster,
  a row missing its own viewer, or a surface whose only evidence is missing or partly unclustered.

Both calls return up to 64 rows. For inspection, `offset` is a world draw-surface offset (the optional final CLI
argument). For import it is a generated-target offset; pass `nextOffset` back within `sourceVisibility`.
Source inspection lists up to 32 distinct nonnegative owners per surface and sets `clustersTruncated` when more
exist. All owners participate in classification. The import result also counts `outsidePvs` and `unknown` over
all targets, independently of the selected page.

The diagnostic leaves generated bytes and recipe hashes unchanged. Its `generatedOutputSha256` identifies the
generator archive before any authored reimport overrides. The source PVS omits the target engine’s camera, dynamic
doors and visibility traversal. The diagnostic never hides JK/MotS geometry or changes collision.

## Inspecting source and target collision

Run `red-agent jo-ja-inspect gallery.bsp` to find collision brush shaders, including shaders
absent from the draw surfaces. Select one brush with `red-agent jo-ja-inspect gallery.bsp --brush 0`.
Each side records its own plane, outward normal, distance, side shader and optional draw surface.
Source solid, water, player clip, shot clip and no-shot contents are independent. A referenced
material or visible surface does not establish the corresponding JK movement filter.

Inspect captured target geometry with
`red-agent jo-ja-inspect gallery.gob --target jkl/gallery.jkl 0 1`. The sector layer includes
native polygons and reciprocal portal references. The Thing layer resolves the placed instance's
template inheritance and model in the selected archive. Use `@templateName` in place of a Thing
index to inspect a projectile or player template. Missing stock models are reported explicitly.
Optional final arguments select the surface and model-face page offsets. Both commands report
artifact hashes. The MCP tool exposes the same selectors through its `brush`, `sideOffset` or
`target` argument.

Create a switchable collision section from the source and the target with the bundled inspector.

```sh
python tools/engine-capture/collision_overlay.py --agent ./red-agent \
  --source gallery.bsp --archive gallery.gob --level jkl/gallery.jkl \
  --brush 0 --sector 0 --thing 0 --scale 0.005 --axis z --depth 0.1 \
  --player 2 --projectile @+windstone --projectile-origin 0.18 0.2 0.1 \
  --decision "Keep this source-only filter under review." --output gallery-section.html
```

The section clips the selected source brush's actual planes at the indicated depth. The scale must
match the conversion recipe. Exterior and portal polygons remain separate from captured model
faces. Player and projectile markers use the target's declared sphere movement radius when
available. The player's attached floor-search reference comes from the loaded model and effective
template. Actor flags, position, animation, sweep direction and game state affect actual contact.
The report records the mapper's decision and the exact inputs without changing either input.
Test the route in the named target engine to establish traversal or impact behavior.

## Engineering sources

The workflow is informed by Lilivane's conversion tools. The portable implementation uses RED's existing target writers
and original synthetic fixtures, without redistributing extracted game assets or relying on map-specific scripts.

- [Raven renderer selects a cluster row and tests each leaf’s PVS bit](https://github.com/JACoders/OpenJK/blob/260c59c2907187af555a676fe0cc798893bf7757/codemp/rd-vanilla/tr_world.cpp#L1632-L1652)
- [Raven brush contents, including SOLID, OPAQUE and TRANSLUCENT](https://github.com/JACoders/OpenJK/blob/260c59c2907187af555a676fe0cc798893bf7757/codemp/game/surfaceflags.h#L34-L65)
- [Raven collision loader obtains contents from the brush shader and bounds from axial sides](https://github.com/JACoders/OpenJK/blob/260c59c2907187af555a676fe0cc798893bf7757/codemp/qcommon/cm_load.cpp#L227-L275)
- [Raven collision loader preserves the brush side plane and shader](https://github.com/JACoders/OpenJK/blob/260c59c2907187af555a676fe0cc798893bf7757/codemp/qcommon/cm_load.cpp#L420-L448)
- [JO multiplayer traced footstep selection](https://github.com/mvdevs/mvsdk/blob/4c7c463638192aead1ff3de8a88cc5de266bf3a2/code/cgame/cg_players.c#L1807-L1852)
- [JA multiplayer traced footstep selection](https://github.com/JACoders/OpenJK/blob/260c59c2907187af555a676fe0cc798893bf7757/codemp/cgame/cg_players.c#L2039-L2180)
- [JA multiplayer movement event parameters](https://github.com/JACoders/OpenJK/blob/260c59c2907187af555a676fe0cc798893bf7757/codemp/game/bg_pmove.c)
- [Target surface versus Thing contact sounds](https://github.com/CodaLilitari/OpenJKDF2-MotS-Fixes/blob/3c6814b724871f7e1fe33d12d2e20ec93499c0b3/src/Engine/sithPuppet.c)
- [Design and adapter ownership](jo_ja_import_design.md)
