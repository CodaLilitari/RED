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
5. Choose collision deliberately. **Visual-only** is the default. Rendered-mesh collision makes the static architecture
   collidable, but does not reproduce clip brushes, water volumes or the source engine's player/weapon filters.
6. Choose **Prepare**, then read the counts, material mappings, skipped shaders, audio flags, limitations and hashes.
   Preparation writes no archive. Editing an input invalidates the prepared result; canceling work or closing the dialog
   prevents publication.
7. Choose **Create new archive**. Inputs are rechecked before publication. An existing destination is refused. The new
   archive is independent of the open document; creation does not replace the document or mark it dirty.

Open the generated JKL with the new archive available as a resource source before editing its model Things. The
geometry is in 3DOs inside one enclosing sector, rather than reconstructed native rooms and portals. An editor preview
or archive readback does not establish that a retail game can load, render or play the map correctly.

## Materials and lighting

Each mapped MAT must have one texture cel, power-of-two dimensions from 8 through 256, and a complete stored mip chain.
Indexed 8-bit MATs require an explicit matching CMP. The importer can also capture opaque RGB565 MATs with explicit
5:6:5 channels. RGB565 renderer support must be qualified in the intended game; it is not an automatic fallback to an
indexed edition. The selected CMP also affects stock assets, so use a palette compatible with the stock resources the
scene references.

Source base UVs are converted to MAT pixel coordinates. The first 128-by-128 lightmap atlas/style supplies bilinearly
sampled corner luminance; surfaces without one use source vertex colors. Models use scalar 3DO 2.1 vertex intensities.
MotS receives explicit ARCH lighting records bound to each architecture Thing. Dynamic light styles, source renderer
gamma/overbright, colored lighting and shader-stage modulation are not reproduced.

Source shaders can contain animations, alpha tests, transparency, additive stages and multiple textures. A base image
alone does not preserve those behaviors. Exact shader names remain in the report and source entity text is retained in
`misc/joja-source.ent` for later authoring.

## Footsteps and other audio

The default policy uses available JK/MotS stock cues. Architecture chunks are separated by their target footstep class
because the target engine chooses a contact sound from the attached **Thing's flags**. A model face's MAT name or face
flags do not select an independent footstep.

| Source evidence | Target model cue | Report |
| --- | --- | --- |
| Unspecified or ordinary hard surface | Hard | Stock default; no texture-name guess |
| Raven metal-step flag or solid/hollow metal material | Metal | Stock metal cue |
| Dirt, sand, gravel, snow or mud material | Earth | Approximation with the available stock category |
| Grass, wood, rug/fabric and related soft families | Hard fallback | Needs audio review; no exact stock model cue |
| Foliage or source `NOSTEPS` | Hard fallback | Needs review; silence is not claimed |
| Water contents or water material | Existing fallback cue | Needs review; liquid movement and splash events remain unconverted |

These flags matter when the player is attached to a colliding architecture model. The visual-only default does not
establish contact or audible playback. Sound classes and samples come from the target game's stock player references.
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
  "audioPolicy": "stock-equivalents"
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

Plan returns the report, model/material inventory and both review hashes. Apply recaptures the inputs, regenerates the
archive and checks the reviewed output and manifest before creating one new file. Use `target: "mots"` with a `.goo`
output. Request fields, including explicit skipped shaders even when unused by world faces, are bound by the manifest.

MCP exposes `sed_jo_ja_inspect`, `sed_jo_ja_plan` and `sed_jo_ja_apply`. Plan/apply use a nested `request`. Apply also takes
`output`, `expectedOutputSha256`, `expectedManifestSha256` and an optional journal `operationId`. Existing MCP path
policy, resource roots and journal conflict/replay rules apply. See [Agent workflow](agent_workflow.md).

## Budgets and remaining authoring

| Setting | Default | Admitted range or bound |
| --- | --- | --- |
| Scale | 1 / 320 | Greater than 0, at most 1; geometry collapsing at target precision is refused |
| Patch subdivisions per quadratic block | 4 | 1–16 |
| Triangles per model | 140 | 1–170, at most 510 flat corners |
| Surviving triangles | 100,000 | Configurable budget up to 100,000 |
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

The importer does not reconstruct source visibility, convex-sector connectivity, trigger behavior, pickups, enemies,
water physics, mission logic or multiplayer state. Fixed patch sampling is not an error-bounded or crack-free
tessellation guarantee. Large one-sector scenes can exceed a renderer's practical limits even when each model fits
the loader's per-mesh limits. RED's software snapshot also has its own model/resource budgets.

The import report is stored in `misc/joja-import.txt`. `runtimeVerified` remains false. Qualify the exact archive in its
target engine, including collision, lighting, starts, sounds and multiplayer travel, before distributing a playable map.

## Engineering sources

The workflow is informed by Lilivane's conversion tools. The portable implementation uses RED's existing target writers
and original synthetic fixtures, without redistributing extracted game assets or relying on map-specific scripts.

- [Raven surface flags and materials](https://github.com/JACoders/OpenJK/blob/260c59c2907187af555a676fe0cc798893bf7757/codemp/game/surfaceflags.h)
- [JA multiplayer footstep event selection](https://github.com/JACoders/OpenJK/blob/260c59c2907187af555a676fe0cc798893bf7757/codemp/cgame/cg_event.c)
- [JO movement and footstep behavior](https://github.com/JACoders/OpenJK/blob/260c59c2907187af555a676fe0cc798893bf7757/codeJK2/game/bg_pmove.cpp)
- [Target surface versus Thing contact sounds](https://github.com/CodaLilitari/OpenJKDF2-MotS-Fixes/blob/3c6814b724871f7e1fe33d12d2e20ec93499c0b3/src/Engine/sithPuppet.c)
- [Design and adapter ownership](jo_ja_import_design.md)
