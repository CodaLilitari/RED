# Import a Jedi Outcast or Jedi Academy scene

**Tools > Import JO/JA Map…** turns a compiled Raven BSP into a new JK GOB or MotS GOO. The same importer is available
through `red-agent` and MCP. It creates an authoring scaffold with world geometry, source texture coordinates,
sampled lighting, static model Things and player starts. For a playable conversion, keep the authored episode as
a reference and check the [gameplay and renderer cases](#qualifying-a-playable-conversion) on the final package.
The [conversion reliability guide](conversion_reliability.md) covers interior lightmap loss, decorative collision,
stair support and overflowing native tables, with repeatable checks and explicit evidence limits.

The input is an extracted `RBSP` version 1 `.bsp`, used by JO and JA. Extract the BSP and any artwork you are
authorized to use from the source PK3 before opening the importer. RED packages the MATs you select and references
the target game's stock resources. Keep source art, music and original game archives out of shared test fixtures.

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
   and portals exist independently of this checkbox. For a JK model scene, review the separate
   [source player clip choices](#choosing-a-player-clip-treatment). Water and other collision filters still need authoring.
   **Mesh optimization** welds and merges the generated models; strict keeps how they draw, and loose also removes
   straight corners. See [Mesh optimization](#mesh-optimization).
6. Choose **Prepare**, then read the counts, material mappings, skipped shaders, audio flags, limitations and hashes.
   Preparation writes no archive. Editing an input invalidates the prepared result; canceling work or closing the dialog
   prevents publication.
7. Choose **Create new archive**. Inputs are rechecked before publication. An existing destination is refused. The new
   archive is independent of the open document; creation does not replace the document or mark it dirty.

Open the generated JKL with the new archive available as a resource source. For a hybrid import, inspect reciprocal
adjoins in the sector editor and the sector assignments of the remaining model Things. Test travel, rendering and
frame times in the target game with the original model-scene conversion available for comparison.

## Stock multiplayer pickups and projectile checks

Enable **Stock pickups** or set `"stockPickups":true` in a plan/apply request, with an explicit `jo-mp` or
`ja-mp` source profile. Existing recipes that omit the option retain the original geometry-only behavior.
In this mode RED recognizes these unconditional source entities (classname and origin only):

| JO/JA multiplayer classname | JK / MotS target template |
| --- | --- |
| `weapon_blaster` (E-11) | `strifle` |
| `weapon_bowcaster` | `crossbow` |
| `weapon_thermal` | `detonator` |
| `ammo_blaster` | `energycell` |
| `item_medpak_instant` (on contact) | `healthpack` |
| `item_medpac` (holdable) | `bactatank` |
| `item_shield_sm_instant` | `shieldrecharge` |

Mappings use the [JO MP item table](https://github.com/mvdevs/mvsdk/blob/4c7c463638192aead1ff3de8a88cc5de266bf3a2/code/game/bg_misc.c)
and [JA MP item table](https://github.com/JACoders/OpenJK/blob/260c59c2907187af555a676fe0cc798893bf7757/codemp/game/bg_misc.c).
Stock counterparts are not balance equivalence: amount, capacity, fire modes, spawn heights, collection and respawn
need a target game test. Target template rows and their parents come from RED's `data/master.tpl` for JK
or `data/mots.tpl` for MotS. A stock MotS healthpack carries the architecture flag but has no source-authored
per-instance lighting; it uses the stock 3DO lighting fallback. Generated world models keep their explicit ARCH records.
No original COG, model or texture is redistributed by this option.

Each supported pickup is placed at its scaled source origin in a containing generated sector. RED reports, but
does not move or substitute, other weapon, item, ammo and team entities, malformed/out-of-sector origins and
entities with additional fields (including spawn flags, target activation or mode filters). Map-specific scripts
and dynamically spawned items are not inferred. A source with more than 512 pickup candidates is refused.

The preparation report also checks the **generated JKL after serialization** for the default stock Bryar/fists
starting inventory and recognized placed stock weapons. A pickup can be present even when the target cannot fire it.
`missing-template` names an absent projectile root; `unresolved-dependency` names a missing parent or referenced
template. Duplicate declarations in an audited root or dependency leave the binding `unverified`.
`bound-static` is not a firing test. Resolve findings in the authored level and verify each firing mode
before release. To check an already authored level, extract its final JKL and run
`red-agent projectile-audit final.jkl jk` (or `mots`). This check does **not** inject projectiles, inspect custom inventory COGs, or certify asset loading.

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

For masked leaves or grilles, author an indexed MAT with **Material Authoring** and keep those exact bytes when
assembling the accelerated edition. In a `mat-create` or `sed_mat_create` request, `"format":"indexed8"` and
`"colorKey":true` use palette index zero for transparent pixels. If a card shows a straight seam, inspect its
source alpha, UVs and stored mips first. For an image whose right edge bleeds when sampled, `"guardEdges":["right"]`
makes one column transparent at every stored mip. Apply the guard only to the affected edge and compare the
result at the same distance. See [Image-to-MAT authoring](material_authoring.md) for the request and palette rules.

Source base UVs are converted to MAT pixel coordinates. The first 128-by-128 lightmap atlas/style supplies bilinearly
sampled corner luminance; surfaces without one use source vertex colors. Models use scalar 3DO 2.1 vertex intensities.
MotS receives explicit ARCH lighting records bound to each architecture Thing. Dynamic light styles, source renderer
gamma/overbright, colored lighting and shader-stage modulation are not reproduced.

Source shaders can contain animations, alpha tests, transparency, additive stages and multiple textures. A base image
alone does not preserve those behaviors. Exact shader names remain in the report and source entity text is retained in
`misc/joja-source.ent` for later authoring.

### Choosing between PK3 map variants

When packages install different versions of the same map, compare their BSPs before choosing one.
From the release directory or source tree, run

```sh
python3 -B tools/engine-capture/compare_pk3_maps.py older.pk3 redux.pk3 --agent ./red-agent
```

`paths` lists distinct BSP hashes at each case-insensitive installed path. Under `variants`, each
hash has its package/member names, world surface count, source player starts, triggers and complete
entity-class counts. Starts include source duel and team starts. The FFA importer applies its own
spawn rules. Identical bytes are inspected once across packages.

Repeat with `--select <full BSP SHA-256> --output chosen.bsp` to save that version under a new
filename. Open it in **Import JO/JA Map**, or copy the returned `selectedSource` object into the
`source` field of a `jo-ja-plan` request.

### Inspecting first-lightmap fidelity

In **Import JO/JA Map**, enable **Rank first-atlas lighting loss across this map** and click **Inspect shaders**.
The review lists the twelve strongest source surfaces for each metric, with the source triangle, sample coordinates
and corresponding JK coordinates at the selected scale. Click **Prepare** to see the first generated sector/surface
or placed Thing and model mesh for each entry, alongside the count of other targets if the face was split.
A skipped shader or omitted submodel can have a source measurement and zero generated targets.

For a scriptable ranking use `red-agent jo-ja-inspect scene.bsp --lightmap-rank 12 4`, or call
`sed_jo_ja_inspect` with `{"path":"scene.bsp","sourceLightmapRanking":{"top":12,"patchSubdivisions":4}}`.
The list length accepts 1–64. Equal scores retain BSP surface order. The report counts sampled triangles,
grid probes, and surfaces lacking an atlas or drawable triangles. The map-wide limit is 250,000 sampled triangles.
If a scan exceeds it, reduce patch subdivisions or inspect individual surfaces. The command returns an error for
an over-budget scan so a partial list cannot be mistaken for the worst faces across the whole map.

When a pool of light or a color cast disappears during conversion, inspect its source draw surface by absolute
RBSP index. Use the patch subdivision count from the import recipe. The default is 4 and the range is 1–16.

```sh
red-agent jo-ja-inspect scene.bsp --lightmap 12 4
```

The matching `sed_jo_ja_inspect` arguments are `{"path":"scene.bsp","sourceLightmap":{"surface":12,"patchSubdivisions":4}}`.
The top-level `sourceSha256` identifies the exact BSP. `sourceLightmap` reports the first atlas index, patch
subdivisions and two independent comparisons of source lighting, in normalized RGB units from 0 to 1.

| Witness | What the error measures |
| --- | --- |
| `rgbInterpolation` | Largest sampled absolute channel difference between the bilinear atlas color and the RGB obtained by interpolating the triangle's three atlas corner colors. A light pool at the centroid can produce an error of 1 even with three black corners. |
| `cornerScalar` | Largest absolute channel difference at a corner between its atlas RGB and the grayscale luminance used by the importer. A constant red triangle has essentially zero interpolation error and a corner scalar error of about 0.7874. |

Each witness includes both RGB triples, the zero-based triangle ordinal within the tessellated, nondegenerate
surface, and barycentric weights in that triangle's corner order. Ties retain the first witness visited.
The fixed grid uses steps of 1/6 along triangle edges and evaluates 28 positions per triangle, including
corners, edges and the centroid. `sampleCount` gives the number of grid positions evaluated. The existing
65,536-triangle surface limit bounds a request to 1,835,008 positions. Finer features can fall between probes.
A negative first atlas index yields `no-first-atlas`; an atlas with no triangles yields `no-triangles`.
Both report zero grid positions and null witnesses.

Use these measurements to locate source lighting that warrants review. The read-only comparison runs before
material modulation, palette quantization and target-engine shading. Shader stages, additional light styles,
the appearance of a particular exported package and frame time require separate evidence.

## Decorative faces and solid supports

CLI/MCP recipes can add `nonCollidingShaders`, an array of up to 256 unique exact source shader names of 1–63 UTF-8 bytes each. Embedded NUL is refused. With
`geometryMode: "model-scene"` and `renderMeshCollision: true`, selected triangles remain visible but occupy
separate `collide=0` Things. Unselected structural chunks keep face collision and their contact-sound flags.
The split occurs before mesh optimization and counts against `maxModels`. It applies to JK and MotS.

Review source brushes and intended movement before selecting shaders. Transparency or a filename alone does not
establish collision intent. Unknown, skipped and non-emitting selections refuse; empty and omitted lists preserve
existing recipe bytes and hashes. A nonempty list with hybrid geometry or globally disabled render-mesh collision
also refuses. The GUI's existing whole-model checkbox is unchanged; use the agent recipe for selective treatment.

The optional `generatedCollisionSelection` report names selected generated Things, models, source surfaces and
shaders, and binds them to `generatedOutputSha256` before any authored reimport override. Its `nonCollidingModels`
and `nonCollidingTriangles` summarize that selection. The top-level `outputSha256` identifies the final archive.
An authored collision override can change the final native Thing, so inspect the packaged JKL after reimport.
See the [worked collision workflow](conversion_reliability.md#reviewed-decorative-collision).

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
| `nonCollidingShaders` | Empty | Up to 256 unique exact names with emitted world geometry; model scene with render-mesh collision enabled |
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
triangle even where neighbours share corners. Choose **Mesh optimization** in **Tools > Import JO/JA Map…**, or set
`meshOptimization` in an agent or MCP request, to clean up each generated 3DO mesh before it is written.

- `"off"` (the default) writes models exactly as before and keeps existing recipe hashes.
- `"strict"` welds vertices that share a position and light when their normals differ by at most 0.5 degrees,
  keeping the first normal, and shares identical texture vertices. It then merges coplanar neighbouring faces that
  share an edge, material and modes into convex polygons of up to 8 corners. A merge is made only where every
  corner's light lies within half an 8-bit step of a linear fit across the merged face, and its texture coordinates
  within 1/32 texel. The engine draws a polygon as a fan of triangles, and both the old triangles and the new fan
  interpolate corners that close to the fit, so a pixel's light moves by at most one 8-bit step.
- `"loose"` welds the same way and allows 4/255 of light and half a texel, for more merges. It then removes
  straight corners, described below.

If the first three usable corners of a polygon nearly line up, their UV or light gradient can be numerically unstable.
After the original merge pass finishes, the optimizer retries rejected fits using the widest projected pair from
the same origin. This extra pass requires each corner to fit within half the selected tolerance, so two triangle
fans interpolating those residuals stay within the full tolerance. It applies to both opt-in levels.

Strict never moves a corner: positions, light values, texture coordinates, winding, the source triangle count,
model chunks and hybrid coverage stay the same, and so does the number of triangles the engine draws. Only dynamic
light reads vertex normals.

### Straight corners

Merging a grid of triangles leaves corners in the middle of straight edges, where the face on the other side turns.
Each one costs the engine a fan triangle. Loose removes such a corner only when every face of every generated
model with a corner at that position removes it too, and no sector vertex is there. Removing it from one face while
a neighbour keeps it would leave a T-junction, which can open single-pixel cracks. The light, texture coordinates and
normal of each removed corner must lie within half the loose tolerances of the straight edge that replaces it. A
face keeps its first corner, its fan origin, and at least three corners. Removed vertices take their dynamic-light
samples with them, so moving lights are sampled more coarsely on large flat faces.

### Measurements

The plan reports `optimizedVertices`, `optimizedFaces` and `worstNormalTurnDegrees`; loose adds
`straightCornersRemoved` and `fanTriangles`. The import report and manifest name the level. On the
`ffa_bespin` model scene at four patch subdivisions:

| Setting | Vertices | Faces | Triangles drawn | JK archive |
|---|---:|---:|---:|---:|
| Off | 70,095 | 23,365 | 23,365 | 9.4 MB |
| Strict | 26,321 | 16,988 | 23,365 | 4.1 MB |
| Loose | 24,379 | 13,336 | 20,463 | 3.7 MB |

Loose removed 2,902 straight corners there, 12% of the drawn triangles. These are mesh counts; frame time needs its
own measurement in the target engine.

Red brought these rules into the importer from the Sith Council conversion. There, the weld and merge together with a
COG fix made the level about a fifth faster in OpenJKDF2 without visible change. A merge that ignored the
linear-attribute check moved textures by up to 149 texels inside merged faces, which is why the check is not
optional. Coda later removed 18,509 straight corners from that level, face by face, for about 4% more FPS in
retail under Wine. About half of those corners sat where a face in the same model kept a corner, which leaves a
T-junction. Run on the same level, RED's rule removes 8,481 corners, 4.6% of the drawn triangles, and adds no
T-junction. [Frame rate in large one-sector levels](knowledge/levels/performance.md)

Patch sampling is still fixed per quadratic block: adaptive sampling would need adjacent patches to agree on their
shared edges to avoid cracks, and isn't done here.

## Choosing the static world model render pass in JK

In **Tools > Import JO/JA Map…**, select **Jedi Knight (GOB)** and check **Draw static world models
as ordinary Things (JK only)**. Agent and MCP recipes select the same setting with
`"target": "jk"` and `"worldModelRenderPass": "ordinaryThing"`.
This puts generated static world 3DOs in JK's ordinary Thing pass and retains
Gouraud shading at distance. The importer clears `LEVELGEO` (`0x8`) and sets
`IGNOREGOURAUDDISTANCE` (`0x04000000`) on the generated world template and its model overrides.
This covers model scenes and residual 3DOs in hybrid sectors. Standability, footstep flags, collision
settings, sector geometry, player starts, 3DO light samples and textures keep their original values.

Use the option when checking depth ordering in the original JK renderer. In one indexed
Sith Council hall comparison, the stock software renderer recovered the central ramp and doorway.
Other scenes and viewpoints need their own comparison. For the original software renderer, supply
indexed MATs and their matching CMP. Changing the pass does not convert RGB565 textures.
A later ray bake in RED still treats these models as static architecture. It recognises the ordinary-pass flags, so
the same buildings block the same rays in either pass, and it treats the world template's `move=path` without frames
as static. The per-vertex light already stored in the generated 3DOs is preserved.

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
index to inspect a projectile or player template. A missing, cyclic or ambiguous selected template
chain yields `templateUnresolved`. The native sector layer remains available. Missing stock models
in an otherwise resolved profile are reported explicitly.
Optional final arguments select the surface and model-face page offsets. Both commands report
artifact hashes. The MCP tool exposes the same selectors through its `brush`, `sideOffset` or
`target` argument.

For `collide=3`, `modelGeometry` is `capturedRawFaces` and `geometryBasis` is `rawGeoset0`.
The polygons use every authored mesh in the first physical geoset, including meshes with no draw-hierarchy node,
and the placed Thing's position and pitch/yaw/roll. The draw hierarchy and model insert offset do
not move these collision faces. Repeated draw nodes do not duplicate them. The native engine ignores
the geoset's numeric label and reads vertex positions in physical row order. A face that refers to
vertex 2 uses the third position row even when that row carries a different label. Before reporting
raw faces, RED checks the captured 3DO 2.1 or 2.3 source tables. Each native mesh requires
both vertex-normal and face-normal tables, including their headers when the row count is zero.
A missing table, missing declared mesh or unreadable coordinate returns an input error.
Complete decimal mode spellings such as `03` and `+3` retain their
authored text in `profile` and select the same face basis. For other collision
shapes, `capturedRestPose` with `hierarchyRestPose` describes visual reference polygons; those
polygons do not establish sphere or sphere-tree collision. Unavailable model resources retain
their explicit missing-model status. The captured geometry does not establish live contacts.

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
Use the overlay script with its matching RED agent executable. A populated native model-face layer
requires the explicit `rawGeoset0` receipt; older or visual-only receipts are refused.

### Choosing a player clip treatment

In a **Jedi Knight** **Model scene**, use **Source player clips** to choose how eligible world brushes become
invisible collision models. The default **Omit source player clips** keeps earlier recipes. Use **Standable hulls**
when the player needs to walk on a top face. A standable hull can also intercept bolts, saber traces and line-of-sight
queries. **Movement barriers** block explicit player movement and allow ordinary blaster bolts through. They can
intercept saber traces and do not support ground attachment. Compare each important traversal with the source.

To combine treatments, choose **Standable hulls** and enter the inspected source brush indices in **Movement
barrier brush IDs**. The corresponding `jo-ja-plan` or MCP request fields are:

```json
{
  "target": "jk",
  "geometryMode": "model-scene",
  "sourcePlayerClip": "standable",
  "movementBarrierBrushes": [17, 42]
}
```

The numbers illustrate the syntax. Select eligible indices from the exact source BSP. With this request,
brushes 17 and 42 receive movement barriers and the other eligible brushes receive standable hulls.
Use `"movementBarrier"` for the whole eligible set or `"omit"` to leave it out. An eligible static world brush
has `PLAYERCLIP` and may also have `MONSTERCLIP` or `TRANSLUCENT` contents. Mixed solids, shot clips, water
and moving submodels use separate collision or authoring paths. The override list accepts up to 512 unique,
eligible source brush IDs under the standable policy. On preparation, each `playerClips` row identifies the source
`brush`, chosen `mode`, emitted `model`, placed `thing` and number of `faces`. Inspect these actual targets
before testing rooftops, rails, projectile routes and intentional open drops. Brush height alone cannot tell what
should support a player.

For a standable hull imported with `sourceProfile: "jo-mp"` or `"ja-mp"`, RED reads the shader on
each emitted brush side whose outward unit normal has Z at least 0.7. That is the source movement
slope threshold. A walkable cap can have a different shader from its walls or the brush's contents
shader. The attached player takes the hull owner's stock cue. Where walkable sides map to different
cues, the cue covering the greatest polygon area wins. Exact ties prefer hard when present, then the
first tied cue alphabetically. A hull with no qualifying facet keeps the hard fallback.

Each such `playerClips` row includes a `footsteps` object with `cue`, `status` (`uniform`, `mixed` or
`noWalkableFaces`), `areaByCueJKU2` and `needsReview`. Areas use the emitted target polygons and measure square Jedi Knight
units, including the slope of each facet. Mixed owners, missing support and approximate source cues
require review. Layered collision and player routes affect the floor that actually receives a step.
Movement barriers have no floor attachment. Legacy recipes retain their original flags and hashes.

Two source readbacks illustrate what the report measures. A fresh Bespin model scene emitted all 58 source
hulls using 34 standable and 24 selected movement barriers. Of 399 original nonzero boundary facets, 385
survived at target precision. Sub-float-width bevels can coalesce while the emitted hull remains closed.
A one-subdivision RJA scene emitted 267 clip hulls with 1,602 faces. These are geometry readbacks of the
generic importer. The retail observations below used the separately authored playable episodes and fixtures.

## Qualifying a playable conversion

Build two texture editions against the same authored world when supporting both original JK renderers. Give their
installed GOBs the same basename and their JKLs and episode records the same identity. With a common palette,
use indexed 8-bit MATs for software and opaque RGB565 MATs for accelerated rendering. Keep indexed color-key
MATs for cutouts in **both** editions. Inspect the final archive members to establish exactly which resources differ.
Stock multiplayer characters can load more materials after the level opens. Allow spare world-material slots and
exercise a real second-player join.

From the RED source tree, run the read-only verifier on the two archives you intend to distribute:

```sh
python tools/engine-capture/verify_episode_pair.py path/to/indexed.zip path/to/hardware.zip
```

It also accepts bare GOB or GOO files. Review the installed episode path, shared gameplay and resource
members, MAT differences, and the spare material slots in each JKL. Each opaque replacement must match
the indexed cel's dimensions and mip count. Every stored texture must fit the retail four-level mip limit.
To enforce a project-specific floor, add
`--min-free-materials N` with the required number of spare slots. Passing this check establishes package
agreement and declared capacity. Qualify dynamic loads and mixed-edition play in the target engine.

These three conversions supply repeatable checks for the choices above. They include gameplay, resources and
scripts authored after the initial import.

**Bespin Streets** (Raven Software, Jedi Outcast). The original BSP has 58 player/bot clip brushes. The playable
conversion uses 34 walkable hulls and 24 tall movement barriers. In a relocated-start retail fixture, a player
stopped and slid along a roof edge while a stock Bryar bolt reached a marked target beyond it. An ordinary solid
wall stopped the same shot. A second roof and a deliberately open pit supplied separate movement checks. The
DirectPlay session used the actual final 16-start episodes. The accelerated 16-bit host and software 8-bit guest
each received the other player's chat.

**Sith Council V2** (Nick Aikenhead / Kahn, final BSP by BabySpinach). The retail host could play alone with 519
named materials and 519 slots, then exited when a second player joined. Keeping those material rows and raising
only the declared capacity to 1,031 sustained a mixed-edition session. The 16-bit host and 8-bit guest exchanged
chat and the guest saw the host's saber. RED's importer already reserves space for model and stock-player
materials. Inspect the authored final JKL after repackaging, then test the chosen reserve with the intended
players and resources.

**RJA Sparring** (=RJA=Bahamut / Anthony, Jedi Academy). The two editions shared the same indexed leaf MAT.
A faint square edge appeared under acceleration in both. Clearing one texture edge across its four stored mips
removed the measured streak in matched retail captures. On Windows, a player approached a wooden door,
watched it open and walked through to the stairs. Mixed-edition joining and remote door synchronization
remain unverified for this map.

Choose the source BSP by hash when a collection includes several maps. The 2005 Sparring arena and the copy in
the author's later collection match byte for byte. Its 110 static world surfaces use the stock security-glass
shader, with a reflection and a translucent chain pattern. The collection's three Training Redux BSPs instead
use a different pane shader and add four cyan lights linked to a trigger. All three use the same installed BSP
path. A glass material name by itself establishes neither a damage threshold nor breakage.
[Original Sparring release and credits](https://mrwonko.de/jk3files/Jedi%20Academy/Maps/Free%20For%20All/42237/)
and [author's collection](https://jkhub.org/files/file/2688-rja-sparring/).

For a see-through pane, inspect the authored blend stages and the number of glass faces along the sightline.
Original Sparring's security-chain image has black RGB with alpha from 37 to 56 out of 255. Its reflection
is a separate stage. The JK port approximates the glass with one keyed, indexed MAT shared by both editions.
At the north arena window, the previous approximation doubled the source alpha and the sightline crossed
two glass surfaces. In a fixed-camera original JK software test, a white target measured 157/255 through
that pane and 255/255 with only the pane hidden. Using source-strength coverage raised the target to 206/255
with the original pane geometry, collision and face flags. The 16-bit edition on retail JK's Direct3D path
under WineD3D and Mesa llvmpipe measured 198/255 before the correction, 227/255 after, and 249/255 with
the pane hidden. Those figures measure the port at that window. Physical Windows GPU reproduction of
the corrected pane remains open.

For a renderer comparison, keep the camera, geometry, light and display mode fixed. Capture each texture edition
in both renderers. In Bespin's matched courtyard the all-indexed edition worked in both, with some floor stippling
in software. RGB565 opaque textures produced corrupted colors in software. The JK ordinary-Thing draw pass
also recovered the near facade when distant geometry showed through it. The [Council measurements](knowledge/levels/performance.md)
isolate the same depth-ordering issue from texture encoding.

For moving or breakable objects, check the actual placed Thing or adjoined surface, its controller and bindings.
Bespin's authored skylights are two model Things with separate instances of a damage controller. Each accumulates
20 damage before removal. RED's [COG simulator](cog_runtime.md) can deliver synthetic damage messages to test
that threshold. Retail shattering remains unverified for the two panes. Strike a playable pane and observe both
its removal and collision change before reporting that behavior. RED's [Make Breakable Glass](user_guide.md)
command uses a stock script on two sides of an adjoin.

Record which checks used a stock control, a modified-start fixture or the final editions. Capture the executable,
renderer, episode hashes, starts, actions and game-observed result. A two-peer test needs two running clients and
an action visible remotely. The Bespin and Council sessions above used separate retail JK 1.01 processes under
Wine on one computer. Their original Windows renderer captures were a separate test.

## Engineering sources

Lilivane authored the initial Bespin and Council conversion tools. Lilitari refined the playable maps, source
collision, audio and model geometry. Red measured and optimized Council, and Coda integrated and compared the
retail conversions. RED's portable implementation uses its own target writers and project-owned synthetic fixtures.

- [Raven renderer selects a cluster row and tests each leaf’s PVS bit](https://github.com/JACoders/OpenJK/blob/260c59c2907187af555a676fe0cc798893bf7757/codemp/rd-vanilla/tr_world.cpp#L1632-L1652)
- [Raven brush contents, including SOLID, OPAQUE and TRANSLUCENT](https://github.com/JACoders/OpenJK/blob/260c59c2907187af555a676fe0cc798893bf7757/codemp/game/surfaceflags.h#L34-L65)
- [Raven collision loader obtains contents from the brush shader and bounds from axial sides](https://github.com/JACoders/OpenJK/blob/260c59c2907187af555a676fe0cc798893bf7757/codemp/qcommon/cm_load.cpp#L227-L275)
- [Raven collision loader preserves the brush side plane and shader](https://github.com/JACoders/OpenJK/blob/260c59c2907187af555a676fe0cc798893bf7757/codemp/qcommon/cm_load.cpp#L420-L448)
- [Raven collision trace takes surface flags from the hit brush side](https://github.com/JACoders/OpenJK/blob/260c59c2907187af555a676fe0cc798893bf7757/codemp/qcommon/cm_trace.cpp#L611-L615)
- [Raven multiplayer walkable-slope threshold](https://github.com/JACoders/OpenJK/blob/260c59c2907187af555a676fe0cc798893bf7757/codemp/game/bg_local.h#L28)
- [JO multiplayer traced footstep selection](https://github.com/mvdevs/mvsdk/blob/4c7c463638192aead1ff3de8a88cc5de266bf3a2/code/cgame/cg_players.c#L1807-L1852)
- [JA multiplayer traced footstep selection](https://github.com/JACoders/OpenJK/blob/260c59c2907187af555a676fe0cc798893bf7757/codemp/cgame/cg_players.c#L2039-L2180)
- [JA multiplayer movement event parameters](https://github.com/JACoders/OpenJK/blob/260c59c2907187af555a676fe0cc798893bf7757/codemp/game/bg_pmove.c)
- [Target surface versus Thing contact sounds](https://github.com/CodaLilitari/OpenJKDF2-MotS-Fixes/blob/3c6814b724871f7e1fe33d12d2e20ec93499c0b3/src/Engine/sithPuppet.c)
- [Target face collision uses Thing pose and every raw mesh in geoset zero](https://github.com/shinyquagsire23/OpenJKDF2/blob/545eace7ec5b0b35e18768adc6cd22653fc440ca/src/Engine/sithIntersect.c#L188-L205)
- [Target model loader consumes geosets in physical order](https://github.com/shinyquagsire23/OpenJKDF2/blob/545eace7ec5b0b35e18768adc6cd22653fc440ca/src/Primitives/rdModel3.c#L216-L236)
- [Target model loader reads vertex positions in physical row order](https://github.com/shinyquagsire23/OpenJKDF2/blob/545eace7ec5b0b35e18768adc6cd22653fc440ca/src/Primitives/rdModel3.c#L306-L327)
- [Design and adapter ownership](jo_ja_import_design.md)
