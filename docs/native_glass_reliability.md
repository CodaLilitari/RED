# Native glass: coverage, draw order and verification

A pane can disappear because its texture has almost no covered texels. It can hide the city behind it because it
writes depth before that city is drawn. These failures need separate checks. Painting a stronger reflection does
not repair a submission-order problem, and changing a face flag does not reconstruct a source shader.

The source checkout includes hash-bound reconstruction recipes for [Nar Shaddaa JK16](../tools/engine-capture/recipes/nar_glass/README.md)
and [four Bespin editions](../tools/engine-capture/recipes/bespin_glass/README.md). Academy and Midgar below show
what changes when the window also belongs to a moving or colliding model. Related editor workflows are documented
in [material authoring](material_authoring.md), [native model authoring](native_model_authoring.md),
[JO/JA conversion](jo_ja_import.md) and [engine capture](game_testing.md#reproducible-engine-capture).

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

### Face-colliding models and translating doors

An original 3DO can keep supplying collision after its glass is drawn by another Thing. In the inspected engine,
`sithIntersect_CheckSphereMeshIntersection` tests each face with its original vertex indices and normal. The
model renderer clamps draw mode to the face's geometry mode, and the accelerated cache skips mode 0.
On the tested owners, changing just the glass face's draw mode to 0 preserved its index, topology, normal and
collision inputs. The visual companion used `collide=0` and retained each selected face's positions, UVs,
winding, vertex lighting and hierarchy transform. [Collision loop][mesh-collision], [face draw mode][face-mode],
[hardware draw-mode check][cache-mode]

For a translating door or lift, preserve the original path frame table, model collision, Thing ID and COG links.
Place the glass companion at its owner's initial transform. A startup COG can call
`AttachThingToThingEx(visual, owner, 0x8)`. This `0x8` is an **attachment flag**. The movement routine skips
the child's independent collision sweep, then updates its position from the parent's resolved transform and
stored offset. Without that bit, an attached child can shorten the parent's travel. References declared
`nolink` avoid adding control links. An invulnerable visual also avoids an attachment-generated `entered`
message to an already captured parent. [Attachment flag values][attach-flags], [attachment verb][attach-ex], [relative offset and message][attach-owner],
[child sweep][child-sweep], [resolved position][child-position], [COG symbol parser][nolink], [Thing link registration][cog-link]

Compare parent and child positions between path frames as well as at each endpoint. A successful translated
door or cabin does not qualify rotation, an obstructed route, or synchronization for a late-joining player.
Check the original control in play and preserve each donor's distance-lighting flags on its visual where relevant.

### Choosing the opaque pass

RED's [ordinary-Thing import option](jo_ja_import.md#choosing-the-static-world-model-render-pass-in-jk)
addresses measured JK depth-ordering cases. The parent conversions used it for software-renderer depth handling,
while retaining vertex shading at distance. An early opaque pass can fix a different occlusion on the accelerated
renderer. Verify the intended renderer, the actual contents of each owner and distant glass-free controls
before selecting a pass.

## Results on other converted levels

The hashes identify the archived candidates. Each case below names the edition used for its native checks.
Bespin parent and output JKL hashes are in its recipe.

| Edition | Parent GOB SHA-256 | Candidate GOB SHA-256 |
| --- | --- | --- |
| Academy V3, JK16 | `8bcc7dc1c8368c4f6637e8c2b46247cb6e3c7d6f44365cd95e7bce17f1299f9d` | `def6b0aff930a5993711c78cc8bae7128be49ed667f2a4b4404a8e0f1d453140` |
| Academy V3, JK indexed | `3eaad4777b0887f2498070579347a78339ac0c643df7592a6774e370c3543ccc` | `af4bef61d119cd15714c2fa0cab5c868b82fc644d71b6b3934c3289a2b8736f7` |
| Midgar FFA, JK16 | `3a204f859a8f56043ff1682036985bfd670c1cbb03848fa0ec90e225e907c3ae` | `783c3ce6d5eb4cadef4de8960db51d14ef0619382a49b1e1683f1cdc4e0b34da` |
| Midgar SP, JK16 | `e01764a3cde91a5e255bffcdf3befa4f58231c57d6db124f676b387a0754174b` | `6b4b40364b2ee7ccb544cb8d881adaa6b7e97a8736f62d73b4dd037dc0278b21` |

### Bespin

The two breakable window models contain 152 translucent faces. When the window-2 diagnostic hid just those
faces, the detailed room behind them appeared. Clearing the panes' inherited LEVELGEO flag alone left a JK
ordering failure. The JK candidate also moved 219 zero-frame `bp000`–`bp218` architecture Things to the early
pass. Their 16,690 visible faces all have flags `0x0`. The MotS architecture already used that pass. The
[Bespin recipe](../tools/engine-capture/recipes/bespin_glass/README.md) therefore changes 220 JKL bytes on JK
and one on MotS, retaining the original model, collision, MAT and COG resources.

Window 2 was checked on JK and MotS, each in indexed and 16-bit editions. Both window poses were checked on
JK16. In the central JK16 room crop, the recovered background correlated with the hidden-pane view at 0.9939,
with fitted gain 0.3525. Seven glass-free control regions across the platform and walkway were pixel-identical.

The JK16 gameplay probe used a fixture that changed only the authored player start in the JKL. The original
weapon, breakage COG and collision paths handled both no-fire and fire runs. Without gunfire both panes survived.
Two ordinary Bryar shots created native player-owned projectiles and took energy from 50 to 48. The first shot destroyed Thing 269 and its slot was freed.
The second pane (Thing 270) retained its signature and face collision. The player stayed grounded on the
same supporting Thing in both builds. This tests one breakable pane and its neighbouring control in JK16.

### Academy V3

The 637 glass faces already had full MAT coverage. Nine fixed scenery models contributed 551 faces and six
path doors contributed 86. All 15 owners also supplied face collision. The JK16 and indexed candidates retain
all 3,536 original donor faces in their original order and change one draw-mode byte per selected face.
Fifteen new noncolliding visual Things bring the total to 521 of 640. Six attach to the original doors.
The original 113 COG instances, path frames, traversal collision and foliage/grate materials are preserved.
Only the new startup COG establishes attachments. Both editions reproduce the same repair resources and
retain all 272 original differences between the packages (271 MATs and the review metadata). Mounted-resource
validation compiled all 83 distinct placed scripts in each edition without warnings, errors or unresolved resources.

In the JK16 door and skylight views, the intact candidates recover the nearby wall and overhead architecture
seen in the hidden-glass controls. The glass-free blue-pad control matched on all 1,024,000 pixels and the
courtyard matched on 1,023,998. In the indexed edition, the door likewise showed the nearby wall under the
original tint in place of the false outdoor view. Its courtyard matched on 1,023,993 of 1,024,000 pixels.

In JK16 gameplay, two normal switch presses exercised doors 376, 377, 383, 384 and 385 on
both the parent and the candidate. Each reached its original far endpoint and returned home. The native
observer measured zero positional error between every door and its visual on those runs. A separate normal
Use opened door 367 through its 0.38125-JKU stroke. Its original 20-second timer returned it on both parent
and candidate. All six companions reported the intended parent, collision mode 0 and attachment flags
`0xC` throughout the 520 candidate samples of that run.

### Midgar

The candidate enables native translucency on 5,742 source-mapped faces while retaining the packed material art.
Of those, 4,436 use ordinary window glass, 826 use the Shinra glass and 480 use an additive source material.
The native treatment cannot reconstruct that source shader's animated environment reflection. Separate choices
preserve the frosted opaque-base glass, invisible lift collision and existing shaft/spotlight treatment. Six new
visual models and six Things bring the FFA candidate to 611 of 640 slots. Only `episode.jk` differs between the
FFA and SP packages, as in their parents. Mounted-resource validation compiled 18 scripts in each edition
without warnings, errors or unresolved resources.

The original opaque station window concealed the train and platform. An initial treatment recovered the centre
but achieved only 0.493071 background correlation and 0.268569 fitted gain over the full station-window
comparison rectangle. Drawing the verified opaque backdrop first raised those full-region figures to 0.992346
and 0.353117 with the original art. The SP candidate matched the FFA candidate on all 1,024,000 pixels at that
station camera.

An ordinary FFA gameplay call moved lift 1 through the same 1.7125-JKU stroke on parent
and candidate. The attached visual matched its cabin position at every sampled tick. This qualifies one call and
stop in the tested FFA world. Other stops, blocked travel, SP gameplay and multiplayer require their own checks.

The FFA station was also loaded in single-player for a grounded firing test. In each positive run, one stock
Bryar projectile hit original window Thing 387, consumed one energy unit (50 to 49) and destroyed that pane.
Thing 388 survived. The paired no-fire runs kept both panes intact and energy at 50. Across all four runs the
player retained 100 health and, after gravity settling, support from Thing 12. An earlier firing lane hit the sill on both builds,
which is why the positive check required a recorded projectile impact on Thing 387 itself. This tests one
window from the sampled lane in parent and candidate.

The frosted-window control matched at all 921,600 pixels, and the highway differed at one pixel. The church
beam changed visibly, so it was checked against the archived, previously accepted BEAM02 native frame
(SHA-256 `3d4e383ad12ad0244c052b7bbd5b7dbaec87026a77289fe7697fe4a6493a1c3d`). Mean absolute RGB
error against that reference fell from 1.4232 to 0.0679 over the whole frame. Of the 106,383 pixels changed
by the candidate, 99,574 now match the accepted reference exactly, compared with three in the parent.
This identifies recovery of the reviewed beam at that camera.

### Helm's Deep V2

The reviewed hall and bright-wall frames retained the existing crystal treatment. The original 1,546 crystal
faces and 107 water faces already use native translucency, and the relevant model and MAT bytes match the
preceding appearance repair. Those observations support keeping that treatment on the inspected archive
`43e09c52c749301ae7da275825ebcd96b206a4141f65ff93db2f3d4383f4b870`.

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

The ordinary gameplay probes used separate, instrumented executables linked against the pinned release objects.
They initialized the documented player position and, where required, head pose once, then supplied bound player
controls and recorded native state. Their run receipts identify the probe binary and
fixture. The posed-frame executable and its image receipt establish a separate result.

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
[mesh-collision]: https://github.com/CodaLilitari/OpenJKDF2-MotS-Fixes/blob/74e4657c4f1751d09af9ad9e056acb8f41997acb/src/Engine/sithIntersect.c#L279-L306
[face-mode]: https://github.com/CodaLilitari/OpenJKDF2-MotS-Fixes/blob/74e4657c4f1751d09af9ad9e056acb8f41997acb/src/Primitives/rdModel3.c#L1709-L1716
[cache-mode]: https://github.com/CodaLilitari/OpenJKDF2-MotS-Fixes/blob/74e4657c4f1751d09af9ad9e056acb8f41997acb/src/Raster/rdCache.c#L655-L659
[attach-ex]: https://github.com/CodaLilitari/OpenJKDF2-MotS-Fixes/blob/74e4657c4f1751d09af9ad9e056acb8f41997acb/src/Cog/sithCogFunctionThing.c#L1023-L1037
[attach-owner]: https://github.com/CodaLilitari/OpenJKDF2-MotS-Fixes/blob/74e4657c4f1751d09af9ad9e056acb8f41997acb/src/World/sithThing.c#L1348-L1377
[child-sweep]: https://github.com/CodaLilitari/OpenJKDF2-MotS-Fixes/blob/74e4657c4f1751d09af9ad9e056acb8f41997acb/src/Engine/sithCollision.c#L681-L706
[child-position]: https://github.com/CodaLilitari/OpenJKDF2-MotS-Fixes/blob/74e4657c4f1751d09af9ad9e056acb8f41997acb/src/Engine/sithCollision.c#L889-L895
[nolink]: https://github.com/CodaLilitari/OpenJKDF2-MotS-Fixes/blob/74e4657c4f1751d09af9ad9e056acb8f41997acb/src/Cog/sithCogParse.c#L916-L924
[cog-link]: https://github.com/CodaLilitari/OpenJKDF2-MotS-Fixes/blob/74e4657c4f1751d09af9ad9e056acb8f41997acb/src/Cog/sithCog.c#L1918-L1934
[attach-flags]: https://github.com/CodaLilitari/OpenJKDF2-MotS-Fixes/blob/74e4657c4f1751d09af9ad9e056acb8f41997acb/src/types_enums.h#L1116-L1123
