# Native glass: coverage, draw order and verification

A pane can disappear because its texture has almost no covered texels. It can hide the city behind it because it
writes depth before that city is drawn. These failures need separate checks. Painting a stronger reflection does
not repair a submission-order problem, and changing a face flag does not reconstruct a source shader.

This guide records the Nar Shaddaa glass repair and the constraints learned from related converted levels. The
[Nar recipe](../tools/engine-capture/recipes/nar_glass/README.md) is a standalone, input-specific reconstruction.
It adds no editor command, importer policy or automatic glass repair. Existing [material authoring](material_authoring.md),
[native model authoring](native_model_authoring.md), [JO/JA conversion](jo_ja_import.md) and
[engine capture](game_testing.md#reproducible-engine-capture) retain their own contracts.

## Diagnose four independent properties

| Property | Inspect | What the evidence means |
| --- | --- | --- |
| Texture coverage | MAT cel/key declarations, mapped UV footprint and every stored mip | A keyed hole has no painted glass to blend. A nearly empty footprint can look like an open frame. |
| Appearance | Stored RGB, palette, source shader stages and vertex lighting | Tint and painted highlights affect visible colour. A static image does not reproduce a moving environment reflection. |
| Native blending | Actual face flags, material encoding, intersected faces and target renderer | A source shader name or an alpha channel alone does not establish target transparency. |
| Submission and depth | Effective placement flags, render pass, mixed model contents and cache flushes | Geometry behind a translucent depth write can disappear even when the pane's material is correct. |

Read the effective template and per-instance properties, including inheritance. Distinguish a face's translucent
bit from an indexed MAT's color key. Cutout leaves and grilles need their holes; a continuous smoked pane needs
coverage across its interior. Preserve indexed cutouts in both texture editions as described in
[paired-edition qualification](jo_ja_import.md#qualifying-a-playable-conversion).

Measure coverage where the model actually samples the material. A whole-image histogram can miss an empty UV
island or a collapsed feature in a lower mip. Also count the front-facing or double-sided surfaces that actually
survive culling along the camera ray. An intersection with two geometric sheets does not by itself prove two
blended layers. Compare close, distant, oblique and opposite-side views at matched poses.

### RGB is not an opacity control

In the inspected OpenJKDF2 OpenGL path, `rdCache_SendFaceListToHardware` supplies alpha 90 for a face with
`RD_FF_TEX_TRANSLUCENT`. `std3D_DrawRenderList` selects inverse-source-alpha mode, and the supplied fragment shader
premultiplies RGB by the resulting alpha. For unit texture and material alpha, one surviving layer transmits
`90 / 255` of the already drawn background before postprocessing. Changing RGB changes its tint, not that factor.
See the pinned [cache alpha selection][cache-alpha], [GL blend/depth state][gl-state] and [fragment shader][shader].

That calculation describes this path and these assumptions. It is not a retail Direct3D/software, MotS, alternate
shader or multiple-layer acceptance claim. RED's RGB565 writer creates an opaque texture; the model face can still
request native blending. Do not describe a darker RGB565 MAT as having a newly authored alpha channel.

## The Nar repair's exact scope

The recipe accepts only this parent archive and targets the following approved JK 16-bit candidate:

| Identity | SHA-256 |
| --- | --- |
| Parent `nsstreets.gob` | `bba754108bc0986e2aa4aa97f534aca5ca9a7c5d2597120e73a410c2cc5328f8` |
| Accepted `nsstreets.gob` | `0ed7c70a169a928cb3b6f9dd0dd5cf35c851e95f89f0ff362393e22274ab9dff` |
| Generated `mat/ns67.mat` | `1bb08aa2f9e790c6e8064806402facbf2ffa512a11c0f0e7b1d3bd87cb9c88c6` |

The parent has one sector and zero adjoins. Its background buildings are model Things. Deferred alpha-adjoin
rendering therefore cannot explain this particular failure. Count the actual world tables before attributing a
symptom to a familiar engine feature.

The replacement MAT is one unkeyed RGB565 cel with four stored mip sizes: 256, 128, 64 and 32. It keeps the existing
four-chart UV layout, covers the pane interior with a dark blue-gray base, and gives authored outer edges soft
highlights. The internal triangle diagonal stays at the base colour. Highlights weaken in coarser mips. These are
painted reflections, not environment mapping. Reusing this atlas with unrelated UVs would create unrelated stripes.

| Authored change | Preservation boundary |
| --- | --- |
| Move 36 existing breakable placement rows earlier in the JKL file | Keep explicit Thing IDs and each row's original payload; retain the other original rows' relative order. |
| Separate 120 visible fixed-glass triangles from seven static mixed models | Add seven visual-only noncolliding Things and seven models; retain world positions, winding, UVs, scalar lighting, normals and draw modes. |
| Put 131 verified stationary opaque model placements in the `LEVELGEO` pass | Add `0x8` to their existing flags, producing `0x04000048`; exclude the glass, movers and colliding original models. |
| Replace the shared glass MAT | Keep the 216 breakable glass triangles, their models, collision, controllers and sounds unchanged. |

The candidate has 381 of 494 Thing slots, 336 of 568 model slots and the original 399 templates. It contains 597
archive members: 581 original members remain byte-identical, nine original members change and seven are added.
The recipe generates source-derived resources only from the supplied parent; no original level or game assets
are distributed here. Its hash check prevents the known addresses from being applied to another revision.

## Why the first fixes were incomplete

The parent's mapped keyed rim covered about 1.77% of a one-edge triangle and 3.52% of a two-edge triangle at the
tested base-mip pixel centres. The three lower mips had no non-key pixel centres in those footprints. That supported
a coverage repair. It did not establish the order in which the surrounding buildings would render.

| Attempt | Observed limitation | Reusable lesson |
| --- | --- | --- |
| Broaden the painted edge | Some views acquired conspicuous pale bars and obscured background geometry. | Stronger artwork exposes ordering errors; colour alone is not the diagnostic. |
| Use continuous tinted glass and reorder breakable rows | Front, reverse and bridge views improved, but the roof view still lost buildings. | Several convincing views do not cover a different cache/batch composition. |
| Separate fixed glass and retain row ordering | The roof failure remained. | Separate ownership enables a pass choice; it does not itself force a flush. |
| Draw the verified opaque subset first, flush, then draw ordinary glass | The reviewed roof views retained the expected background, with continuous smoked glass. | Establish the required pass boundary, then evaluate appearance and controls. |

JKL row order and Thing identity are separate. The inspected loader uses the row's explicit ID and
`sithThing_EnterSector` prepends a Thing to the sector list. Earlier file rows can therefore be visited later in
that list. They are not a general back-to-front face ordering mechanism. In `RD_ZBUFFER_READ_WRITE`,
`rdCache_DrawRenderList` sorts normal triangles within a batch by texture format, then texture pointer, through
`rdCache_TriCompare`. The relative order of two Things does not guarantee the final order of their polygons.
See [row loading][thing-load], [sector insertion][thing-list] and [cache sorting][cache-sort].

The inspected `sithRender_Draw` calls `sithRender_RenderSectors` before `sithRender_RenderThings`.
The former renders eligible `LEVELGEO` Things and flushes its cache. The latter selects read/write depth and
skips those Things. This supplies the boundary used by the Nar recipe. Putting a mixed opaque/glass model in
the early pass would put its glass there too. See [pass sequence][passes], [sector pass and flush][sector-pass]
and [ordinary pass selection][ordinary-pass].

The `0x80000000` deferred-Thing path is not a replacement transparent queue: this inspected implementation
retains one `lastDrawn` pointer. Applying it to several panes can omit earlier candidates. Its source also marks
the path as a MotS addition. Do not use it as a general retail JK glass solution. [Deferred-Thing source][last-drawn]

## Preserve model behavior when choosing a pass

The Nar fixed donors already had `collide=0`, one geoset, static placements and identity mesh transforms. The
recipe relies on those facts. A split of a face-colliding model can change collision even if the visible result
looks identical. A detached visual from a moving door or lift can remain behind when its owner moves. Check
the source geometry, hierarchy, model instances, movement frames, COG bindings and collision basis first.

The 131 selected Nar models contain no translucent faces after separation. They include existing keyed cutouts;
those masks and UVs remain intact. The sector and ordinary passes have different lighting and distance/LOD
behavior, so compare unaffected architecture as well as the glass. A pass change must not be accepted solely
because the window looks stronger.

This does not reverse RED's existing [ordinary-Thing import option](jo_ja_import.md#choosing-the-static-world-model-render-pass-in-jk).
That option addresses other measured JK depth-ordering cases, preserves earlier recipes when omitted, and has its
own target restrictions. Select a pass for the actual contents and runtime evidence. Do not globally set or clear
`LEVELGEO`, change all alpha materials, or detach every glass face in a converted level.

## Applicability to other converted levels

The following combine archive/source audits with the native comparisons explicitly identified below. They do not
represent completed repairs or general runtime acceptance. Counts refer to the inspected authored versions.
Match each release's archive hash before reusing addresses or conclusions.

| Level | Relevant evidence | Repair decision supported by that evidence |
| --- | --- | --- |
| Academy V3 | 637 translucent faces in 15 mixed owners: 551 in nine fixed owners and 86 in six movers. The glass MAT covers all mips. Separate cutout materials account for 2,419 foliage and 20 grate faces. Matched door/skylight diagnostics reveal background discrepancies when the glass is hidden. | A mover-aware draw-order repair needs investigation. Preserve collision, motion and existing traversal repairs; establish the door state as well as the camera before accepting a fix. Nar's rim atlas is not applicable. |
| Bespin | 152 translucent faces in two isolated breakable models. Both inherit `LEVELGEO`, while the modeled city uses ordinary Things. A hidden-pane diagnostic reveals a detailed room absent through the intact pane. Clearing only the panes' early-pass flag remains insufficient; an explicit opaque-before-glass pass restores the room at the tested view. | A native ordering defect and a promising pass treatment are confirmed at one pose. Extend the comparison to lighting, additional views and gameplay before accepting a map repair. The original glass material needs no tint change for this observed improvement. |
| Helm's Deep V2 | Its 1,546 crystal faces and 107 water faces already use native translucency. Crystal and fixture materials have received their own appearance repair. | Keep those materials and the traversal work. Crystal, water and window glass require different source comparisons; this audit does not justify Nar's tint or geometry split. |
| Midgar | In each inspected FFA/SP archive, 5,742 render-enabled faces from three glass shaders are opaque in the native models. An invisible lift collision duplicate and 22 frosted faces with a source opaque base are excluded. Hiding eight station-window faces reveals a train and platform otherwise concealed by the opaque pane. | The source mismatch is visible in the native FFA view. A source-stage and ownership-specific glass repair is warranted. Review ordinary, Shinra and additive environment glass separately; preserve moving/breakable owners and lift collision. |

Academy and Bespin use the same 16-bit glass MAT bytes, but their ownership and render-pass arrangements differ.
Midgar's previously repaired shaft/spotlight faces do not establish that its windows were repaired. A material
name match is a lead; the packed faces, source stages and actual placements determine the work.

Fresh hall and bright-wall views of Helm's Deep archive
`43e09c52c749301ae7da275825ebcd96b206a4141f65ff93db2f3d4383f4b870` retained the reviewed crystal appearance.
Its translucent-owner models and preexisting MATs are byte-identical to the preceding material repair.
The images were visually consistent rather than pixel-identical; they do not qualify every crystal or water view.

The Bespin comparison used current archive `707e3b32ae6030f93cc27e3528ad4b1df7ddf06e0f788bf40eaee9ce93ea26a3`
and matching 1280-by-800 views. One diagnostic hid only the 152 pane faces. A second changed inherited `_structure`
flags from `0x8` to `0x0`, affecting only Things 269 and 270. The stronger pass trial,
`d39e607a98e75c7e2729d406b9c78d36a71c8e7c7ce9fdc2f950dc7b828fbb8f`, also changed the 219 `bp000`–`bp218`
placements from `0x04000040` to `0x04000048`. They have path-type movement but zero frames, and their 16,690 visible
faces all have flags `0x0`. Only JKL bytes changed; models, MATs and COGs remained identical. The room returned
behind the original smoked glass at the same window-2 camera. This demonstrates the pass boundary at that pose;
the added RGB signal in the central-room crop correlated with the hidden-pane background at 0.9939, with a
fitted gain of 0.3525, consistent with `90 / 255`. Three glass-free control regions remained pixel-identical.
These observations do not establish global lighting, gameplay, retail rendering or multiplayer acceptance.

Academy's hidden diagnostic hid all 637 glass faces. The Midgar FFA comparison used archive
`3a204f859a8f56043ff1682036985bfd670c1cbb03848fa0ec90e225e907c3ae` and hid only eight `mg045.mat` faces in mesh 0
of `mi387.3do`, preserving collision, scripts and all other resources. The train and platform became visible at
the same station camera. These diagnostic archives establish differences for inspection; none replaces its
playable release. Their older-engine offscreen receipts retain the graphics-report failure described below.

## Verification and evidence boundaries

Use a control with the pane hidden to inspect the backdrop, then compare the intended intact material. Keep the
camera position, pitch/yaw/roll, sector, FOV, dimensions, frame, engine, configuration and resources fixed. Include
both sides, a distant/oblique view, a problematic roof or enclosed view, and a region without glass. Verify the
archive actually staged for each view. A screenshot from a preceding release is not the current baseline.

| Evidence | Establishes | Does not establish |
| --- | --- | --- |
| Synthetic recipe tests | Defined transformations, invariant checks and refusal behavior without game assets | Rendering or gameplay |
| Exact archive/member readback | Which bytes changed and whether the accepted resources were reconstructed | Correct lighting, draw order or resource resolution in a game |
| Strict RED JKL validation | The checked framing, counts and supported structural invariants | All native render/collision/gameplay behavior |
| Matched native engine frames | The appearance visible in those frames on the identified engine and inputs | Every camera, retail renderer, multiplayer behavior or moving-object state |
| Ordinary gameplay checks | The specific movement, breakage, collision or replicated action observed | Untested routes, targets or state transitions |
| Complete Windows Qt Release gate | The configured editor/core/package test result for that exact source | Native game acceptance |

The reviewed Nar images used engine binary SHA-256
`16944b47e2ceecf3f09cc2820af26372e2af8642057c2b0a963e353ff090c745`. Front, reverse, bridge, roof, above-roof
and city-control frames were examined. Four glass-free control regions in the city comparison remained
pixel-identical. This is bounded appearance evidence, not a retail or full gameplay qualification.

That older capture engine produced valid image/camera sidecars and exited successfully, but omitted the required
`graphics` object. The unchanged RED offscreen runner therefore reported
`CaptureError: Engine receipt must include graphics.` Keep that failure visible. Independently inspected images
do not turn it into a completed offscreen receipt. Use a compatible engine for the full
[graphics-report contract](../tools/engine-capture/README.md#posed-mode-engine-graphics-report).

Recipe tests and engine pictures do not replace the [complete Release gate](README.md#build-and-test).
The full Windows/MSVC/Qt gate must be reported separately; it is not established by this Linux repair work.
An 8-bit or MotS edition also needs its own material/pass choices, paired-package checks and target observations.

## Pinned engine source

The mechanism references below identify OpenJKDF2-MotS-Fixes commit
`74e4657c4f1751d09af9ad9e056acb8f41997acb`. They support source analysis of this implementation. The native image
evidence above has its own binary identity; a source citation is not an attestation of an arbitrary executable.

[cache-alpha]: https://github.com/CodaLilitari/OpenJKDF2-MotS-Fixes/blob/74e4657c4f1751d09af9ad9e056acb8f41997acb/src/Raster/rdCache.c#L407-L422
[gl-state]: https://github.com/CodaLilitari/OpenJKDF2-MotS-Fixes/blob/74e4657c4f1751d09af9ad9e056acb8f41997acb/src/Platform/GL/std3D.c#L2724-L2750
[shader]: https://github.com/CodaLilitari/OpenJKDF2-MotS-Fixes/blob/74e4657c4f1751d09af9ad9e056acb8f41997acb/resource/shaders/default_f.glsl#L335-L357
[thing-load]: https://github.com/CodaLilitari/OpenJKDF2-MotS-Fixes/blob/74e4657c4f1751d09af9ad9e056acb8f41997acb/src/World/sithThing.c#L1554-L1597
[thing-list]: https://github.com/CodaLilitari/OpenJKDF2-MotS-Fixes/blob/74e4657c4f1751d09af9ad9e056acb8f41997acb/src/World/sithThing.c#L777-L794
[cache-sort]: https://github.com/CodaLilitari/OpenJKDF2-MotS-Fixes/blob/74e4657c4f1751d09af9ad9e056acb8f41997acb/src/Raster/rdCache.c#L1385-L1454
[passes]: https://github.com/CodaLilitari/OpenJKDF2-MotS-Fixes/blob/74e4657c4f1751d09af9ad9e056acb8f41997acb/src/Engine/sithRender.c#L575-L602
[sector-pass]: https://github.com/CodaLilitari/OpenJKDF2-MotS-Fixes/blob/74e4657c4f1751d09af9ad9e056acb8f41997acb/src/Engine/sithRender.c#L2475-L2539
[ordinary-pass]: https://github.com/CodaLilitari/OpenJKDF2-MotS-Fixes/blob/74e4657c4f1751d09af9ad9e056acb8f41997acb/src/Engine/sithRender.c#L2742-L2777
[last-drawn]: https://github.com/CodaLilitari/OpenJKDF2-MotS-Fixes/blob/74e4657c4f1751d09af9ad9e056acb8f41997acb/src/Engine/sithRender.c#L2969-L2998
