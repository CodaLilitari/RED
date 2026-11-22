# Runtime light receivers

A saber can light a nearby crate while a large floor under it stays dark. In the inspected JKWeb-linked
OpenJKDF2 fork, the renderer evaluates a moving light at the floor's world vertices and blends between those
samples. If every corner is outside the light's range, the interior receives zero contribution from that light.
RED's `shadowCuts` mode `selectedReceivers` adds geometry samples to chosen JKL world faces. The engine supplies
the moving light when the level runs.

## What the fork computes

The full-length saber supplies intensity 0.6. Its light sits at the **blade tip**, computed from the grip transform
and blade length. Let `d` be the tip-to-vertex distance in JK units. With the fork's normal camera attenuation,
the following single-light contributions apply before tint, other lighting and saturation.
[Saber intensity](https://github.com/tristamus/OpenJKDF2/blob/cdcb8c0bc9297a5cc2ed328251674bbdbc9afbbd/src/Gameplay/jkSaber.c#L365-L398) ·
[Blade tip](https://github.com/tristamus/OpenJKDF2/blob/cdcb8c0bc9297a5cc2ed328251674bbdbc9afbbd/src/Gameplay/jkSaber.c#L597-L604) ·
[Camera constants](https://github.com/tristamus/OpenJKDF2/blob/cdcb8c0bc9297a5cc2ed328251674bbdbc9afbbd/src/engine_config.h#L258-L259)

| Receiver | Single-light contribution | Source |
| --- | --- | --- |
| JKL world vertex | `0.6 − 0.8d` for `d < 0.75`, zero farther away | [World light loop](https://github.com/tristamus/OpenJKDF2/blob/cdcb8c0bc9297a5cc2ed328251674bbdbc9afbbd/src/Engine/sithRender.c#L2877-L2919) |
| Lit Gouraud 3DO vertex | `(0.6 − 0.4d) × max(N·L, 0)` for `d < 1.5`, zero farther away. The vertex normal must face the light. The inspected colored model branch takes mono vertex-color input. | [Model sampling](https://github.com/tristamus/OpenJKDF2/blob/cdcb8c0bc9297a5cc2ed328251674bbdbc9afbbd/src/Engine/rdLight.c#L283-L316) · [Model branch](https://github.com/tristamus/OpenJKDF2/blob/cdcb8c0bc9297a5cc2ed328251674bbdbc9afbbd/src/Primitives/rdModel3.c#L1826-L1849) |

A CPU harness using the fork's lighting code placed the tip 0.2 JKU above the center of a 2 × 2 JKU floor.
The four outer corners sampled zero. A vertex at the center sampled 0.44. At `d = 0.8` the world contribution
was zero while a model vertex facing the tip still received light. This measures vertex lighting only. GPU output,
frame time and the result on a particular map require a game run.

An invisible 3DO cannot transfer its vertex lighting to a JKL face. A visible translucent overlay contributes
its own textured pixels. Ordinary translucent faces use vertex alpha 90/255, about 35%, and blend with the scene,
which can leave a visible patch even with the saber off. The fork's tint correction preserves luminance when
coloring the light and leaves the sampling radii unchanged.
[Face alpha](https://github.com/tristamus/OpenJKDF2/blob/cdcb8c0bc9297a5cc2ed328251674bbdbc9afbbd/src/Raster/rdCache.c#L414-L424) ·
[Shader blend](https://github.com/tristamus/OpenJKDF2/blob/cdcb8c0bc9297a5cc2ed328251674bbdbc9afbbd/resource/shaders/default_f.glsl#L292-L311) ·
[Tint correction](https://github.com/tristamus/OpenJKDF2/commit/06d26f69edd7c54cc494bc091fa9529f0873646a)

## Refine a floor patch

Choose a level at its geometry-authoring stage, before placing COGs. All agent Shadow Cuts modes retain the
placed-COG guard because cutting can change surface references. A level with placed COGs refuses with
`parityCogBindings`. For a scripted map, a small unscripted test room can establish whether the spacing helps.
The native Shadow Cuts dialog offers the two light-guided modes.
Selected receivers are available through CLI and MCP.

1. Run `red-agent schema shadowCuts` and `red-agent inspect arena.jkl`. Save the inspection receipt. Use a
   `surfaces` query on the desired sector to inspect face addresses, flags, material, geometry and light modes.
2. Select 1–256 distinct `[sector, surface]` pairs from that exact revision. Each must be a valid simple planar convex
   world face with geometry mode 3 (solid shading) or 4 (textured), Gouraud light mode 3, and a material name other than blank or
   `none`. Adjoins, horizon or ceiling sky, and faces with explicit translucency are ineligible. This check reads
   stored face data. It does not resolve MAT resources or examine texture pixels for alpha.
3. Set `maxEdge` to a value in 0.001–1000 JKU. It bounds the **largest distance between any two vertices** of
   every resulting receiver polygon. Short perimeter edges can still enclose a large unsampled interior.
   The default 0.5 JKU is an experimental starting point. `maxNewSurfaces` is an integer budget from 0 to 100,000,
   default 2000. Cuts can introduce interior samples, although a narrow face can meet the bound using only
   points on its original perimeter. Start with a small patch. Omit `lights`, `minPiece` and `maxDepth` in this mode.

For example, save this as `receiver-patch.json` after replacing the two binding strings and the example address
with values for the inspected file.

```json
{
  "schemaVersion": 1,
  "inputSha256": "REPLACE_WITH_INSPECT_INPUT_SHA256",
  "sourceRevision": "REPLACE_WITH_INSPECT_SOURCE_REVISION",
  "operations": [
    {"op":"shadowCuts","mode":"selectedReceivers","surfaces":[[0,2]],"maxEdge":0.5,"maxNewSurfaces":64}
  ]
}
```

```sh
red-agent plan arena.jkl receiver-patch.json > receiver-plan.json
```

In `editReports`, check `receiverSurfaces`, `newSurfaces`, `pieces` and `maxReceiverSpan`. Review
`semanticPreview.authored` and `semanticPreview.persisted`, including their completeness fields. Shared-edge
neighbors and matching portal boundaries may acquire vertices. Newly inserted corners interpolate UV and
stored lighting channels. A different subdivision can change texture interpolation or shading with the saber off.
The operation adds no light source or 3DO and performs no rebake. An over-budget candidate, a required cut that
would exceed the 24-corner limit, or a required cut that fails geometry checks refuses the whole operation.

A patch has a separate fixed budget of 250,000 geometry records across its selected-receiver operations.
The candidate copies sector geometry, leaving the retained document and its resource tables in place. It charges
one unit per sector, vertex, surface, corner index and texture corner, plus one per started 64-byte block of
material, colormap or sound names.
Before each required cleave it charges the current records in the owner and its adjoined sectors again, including
geometry added by earlier cuts. `shadowCutsReceiverWorkLimit` refuses the patch before an over-budget copy or
cleave. Increasing `maxNewSurfaces` does not raise this limit.

Plan and apply reopen the exact proposed bytes to measure `maxReceiverSpan` across all selected descendants,
including faces that already met the bound. `shadowCutsReceiverOutput` refuses publication if serialization makes
a receiver ineligible or increases its diameter past `maxEdge`. A later operation that replaces the owner or
changes a tracked contour or address also refuses. If legacy sectors share an owner ID, the number of sectors
with that ID must also stay the same. Put refinement after those edits so the report describes the final geometry.

After review, substitute the plan's exact `outputSha256` for the last argument and publish a new file:

```sh
red-agent apply arena.jkl receiver-patch.json receiver.jkl OUTPUT_SHA256_FROM_PLAN
red-agent inspect receiver.jkl
red-agent validate receiver.jkl
```

MCP uses `sed_plan` and `sed_apply` with the same patch, a fresh output path and the reviewed hash.
[Plan and apply workflow](agent_workflow.md#inspect-plan-apply-reopen)

## Compare in game

Capture the original and refined versions of the **same floor patch** with the same engine build, renderer,
textures, ambient light, blade color, tip position, camera and resolution. Keep **Colored Saber Glow enabled**
throughout four captures: original with the blade retracted/extended, then refined with the blade retracted/extended.
Compare both retracted views for UV or static-light seams, then compare each extended view against its own
retracted view for light following the tip. As a separate tint control, disable Colored Saber Glow with the blade
extended: the fork retains ordinary white saber light, so this does not measure an unlit scene. Include a moving
pass across the cut boundary.
Record the surface-count increase and frame times on the same repeated route. Choose a smaller `maxEdge` only
when the capture shows an improvement worth the added surfaces. Ordinary native level geometry can carry these
cuts; the colored saber response depends on the engine build and settings. Retail colored glow and the appearance
of any particular map remain separate runtime qualifications.
