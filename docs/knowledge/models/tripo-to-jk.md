# Hosted Tripo sources to rigid JK character models

> Copied into RED from [CodaLilitari/JK-MotS-Model-Workflow@ca011b8](https://github.com/CodaLilitari/JK-MotS-Model-Workflow/blob/ca011b84c4fa5e1f3755e1543be2c1c1d25063c9/skills/jk-mots-model-workflow/references/tripo-to-jk.md) (MIT; see [LICENSE](LICENSE.md)). Regenerate with `tools/knowledge/import_knowledge.py` rather than editing this copy.

Use a hosted Tripo result as an editable geometry and texture source when the design already has coherent views and familiar humanoid movement is acceptable. The useful work after generation is segmentation, geometry reduction, fitting, texture transfer and native-game integration. Keep a successful source and repair its weakest region before commissioning another whole character.

This reference derives from Red/Claude's saved Mando OC conversion scripts, native packages and November 2026 validation records, plus the distinct Mandalorian Red 0.2.1 package. It shares methods and measured examples, not private artwork, generated meshes, stock game resources or account access. The saved packages do not establish a self-contained one-command build. The historical hosted settings are not a statement of Tripo's current models, controls, pricing or license.

## Choose and preserve the source

1. Establish the intended character variant, permitted source images and target role: selectable MP skin, SP player replacement, NPC, or separate weapon/view model. Choose the stock hierarchy or a custom animation contract before polishing geometry.
2. For a hosted run, use the available service or browser controls to verify the actual account, input mode, selected generator, output settings and any charge. Supply only the authorized reference files. Separately labelled front/back inputs are useful when supported; do not assume a collage is equivalent to multiview conditioning.
3. Save the original downloaded mesh, embedded or separate textures, generation receipt/task reference, input images, view labels, selected settings and known license terms. Hash the mesh and textures before editing. Record unknown settings as unknown instead of inferring them from the output's appearance.
4. Inspect the actual downloaded geometry from front, side and rear. Check hidden surfaces, disconnected islands, armour thickness, open limb gaps, fingers, neck, helmet rim and joints. Record whether the source carries albedo only or additional material maps. Retain the original UVs and an untouched source copy for baking.
5. Work on a separate game candidate. Remove genuinely unwanted geometry deliberately and retain a record of the selection. A total triangle count near the desired target does not imply that every rigid part fits the native per-mesh budgets.

Mando OC's README records a Tripo HD multiview source made from a concept sheet's front and back, with **4,875 source triangles** and a **2K albedo**. Its delivered native model has **4,969 triangles**, including new joint caps. The source GLB was not in the reviewed package, so its generation details remain package provenance rather than a fresh inspection of the hosted task or original source.

For local generation alternatives, see [partial-generation.md](partial-generation.md) and [local-generation-automation.md](local-generation-automation.md). Use [the direct Blender-Sith import recipe](blender-sith-fork.md#direct-native-import-for-inspection-and-screenshots) when importing native 3DOs into Blender; this case's custom readback renderer is not a substitute for that requested import.

## Count the download, not the quad label

A requested topology or polygon target is an input preference. Record it separately from the downloaded geometry. If the generation receipt is unavailable, keep the requested value unknown; a filename such as `quad_5336` cannot establish it. Preserve an OBJ/FBX containing useful polygons alongside the GLB, which may already be triangulated. Inspect both rather than assuming their face totals should match.

For each source and export candidate, keep a small count ledger:

| Quantity | What to record |
| --- | --- |
| Generation request | Saved topology/target and receipt, or explicitly participant-reported/unknown settings |
| Downloaded topology | Format, polygon records, triangle/quad/n-gon histogram, and triangle equivalent `sum(corners - 2)` |
| Vertex counts | Raw position and UV records; separately labelled welded analysis with its weld tolerance |
| Native export | Vertices, texture vertices and face records for every mesh/geoset, after normal splits, cuts, caps and baking |
| Acceptance | Polygon shape/seams, bounds and mapped appearance; exact target-engine tests still needed |

For ordinary simple polygons, triangle equivalent describes triangulation cost; it is not proof that a folded, concave or self-intersecting face is valid. A valid native quad consumes **one face record**, while its triangle equivalent is two. Keep a conservative triangle budget when that is the project's policy, but do not rename the format's face-record bound a universal triangle bound. See [the qualified native budgets](compatibility.md#budget-each-mesh-and-each-animation-domain).

Fresh read-only parsing of Red/Claude's November 2026 source archive found:

| Source label | OBJ polygons | Triangles | Quads | Triangle equivalent | Paired GLB triangles |
| --- | ---: | ---: | ---: | ---: | ---: |
| Body v3, `5336` textured | 5,336 | 915 | 4,421 | 9,757 | 9,757 |
| Body v2, `2851` textured | 2,851 | 520 | 2,331 | 5,182 | Not paired in this check |
| Body v2, `607` | 607 | 179 | 428 | 1,035 | Not paired in this check |

These are measured files, not inferred UI targets. The textured and untextured `2851` OBJs have the same polygon totals but 6,732 and 3,586 raw position records respectively. Welding coincident positions for analysis is a separate operation with a stated tolerance; it must not silently erase normal or UV distinctions in the export. Red's retained plan selected the detailed source rather than reducing every model, so these alternatives do not establish a universal reduction percentage or a high-poly-to-low-poly requirement.

Wren's separate prop study illustrates the requested-versus-measured distinction. Wren reported aiming for roughly 400–450 quads, then receiving 593 faces at the available low setting. The saved source inspection records **450 quads + 143 triangles = 593 FBX polygons**, equivalent to the paired report's **1,043 GLB triangles**. Those original source files were absent from the reviewed prop ZIPs, so the input counts remain saved-report evidence. Fresh parsing of the included final Votary gives **444 native faces / 460 triangle equivalent / 245 vertices**, across two meshes. See [the prop guide](environment-props-to-jk.md) for the measured outputs and evidence boundaries.

### Preserve only usable polygons

Inspect planarity, convexity, winding, repeated vertices and material/UV continuity before retaining a generated quad. Split a folded or concave face deliberately and inspect the resulting diagonal and silhouette. When merging adjacent triangles, require compatible face/material modes, a continuous shared UV edge and a convex, acceptably planar result. Preserve the intended triangulation where the target renderer makes the diagonal visible; do not join triangle pairs solely to lower a counter.

Red's saved triangle-merge experiment enforced shared material/flags, UV continuity, a fold tolerance and convexity while preserving triangle equivalent. A separate later HD candidate's saved integrity report listed folded/nonconvex faces despite satisfying numeric per-mesh ceilings. These are distinct candidates: the latter supports independent shape review, not a verdict on the earlier merge experiment. Neither was freshly rerun for this publication. Wren's build reports describe selective triangulation of residual nonplanar faces after reduction. Keep the method that meets the actual shape and target requirements.

## Slice the rigid parts before fitting

Use the target rig's actual mesh order, node names, pivots and rest transforms. A skinned generated humanoid does not become a JK rigid model by renaming its bones.

The executed Mando OC pipeline used 16 body meshes on Kyle's 18-node hierarchy. Its second segmentation implementation made classification boundaries agree with explicit cut planes: shoulders, elbows, wrists, crotch and outer hips, knees, ankles, waist, neck and a tilted helmet rim. It split triangles crossing a plane, interpolating both position and source UV at each new edge point, and only then assigned the resulting triangles to rigid parts. Recompute regional masks after each cut where earlier splits can change centroids.

This gives the cap builder and the part classifier a shared geometric boundary. Merely assigning an entire crossing triangle by its centroid can make a joint edge follow source triangulation instead of the intended cut. The package retains an older centroid-classification implementation, but no preserved A/B record establishes every failure attributed to that earlier approach. Treat plane consistency as the implemented method, not as proof that all other segmentation methods fail.

Keep source-space positions and UV correspondence alongside fitted positions. They serve different purposes: the fitted mesh determines game appearance and atlas density; the source coordinates identify the albedo to transfer.

## Budget surfaces and caps together

Allocate detail by silhouette, joint movement and visual importance. Count vertices, texture vertices and faces independently after splitting, reduction, atlas seams and cap construction. Do not treat a global triangle target as sufficient.

Mando OC's build script used a **480-face working budget per part** and an approximate **4,900-face total target**. It reserved the proposed cap count first, then reduced the remaining surface with `pyfqmr`, `aggressiveness=5` and `preserve_border=True`. Importance weights included head 1.25, neck 0.45 and thighs/calves 0.85. These are case-specific tuning values. The delivered result was 4,969 faces, so verify the final result instead of assuming the simplifier reached its requested count exactly.

The cap method grouped cut-boundary edges by plane and connected component, projected them into that plane and used a convex hull with at most ten boundary points. It shrank the hull by 7% and inset it by 0.0025 source units toward the part interior before triangulating it. The result contributed **439 dark undersuit triangles** to **4,530 surface triangles**.

Those caps reduced exposed cuts but did not eliminate a thin rear hip/thigh opening in the deepest crouch. Convex hulls, shrink factors and planar caps are approximations: inspect them in demanding poses, author overlap or joint volume where needed, and avoid presenting a closed rest pose as proof that the moving joint stays closed.

The package reports parser ceilings of 512 faces, 512 vertices and 768 texture vertices per mesh; its delivered maxima were **479 / 380 / 550**. Attribute those ceilings to the recorded toolchain and check the intended engine/tool versions independently. They are not a general guarantee that every asset below them is compatible.

## Fit geometry to the animation contract

Preserve stock hierarchy indices, parentage, node types, positions, rotations and pivots when reusing stock KEYs. Transform each source part to the stock rest skeleton, then express its fitted vertices in the corresponding node's draw frame. Keeping an unchanged skeleton also keeps its proportions and motion constraints.

The Mando OC method used a global source-to-JK scale of **0.205**, rigid rotations and axial stretch. A piecewise upper-arm transform preserved the pauldron and stretched the sleeve below it to reach Kyle's elbow. Reported compromises were roughly **1.6× sleeve stretch**, thighs **3% longer**, shins **3% shorter**, torso **5% shorter**, and total height **7% above Kyle** because the helmet retained its size. These are observations of that fit, not recommended proportions for another design.

Measure bounds across the intended animation corpus, not only the rest pose. Mando OC's native radius is **0.193**; its build-script comment reports a maximum posed radius of **0.1921** in `kyswdeth`, compared with stock Kyle's 0.173779. That historical all-key sweep was not rerun in the artifact review. A new model needs its own sampled bounds and visible clipping check.

If fitting to stock limbs damages the design, choose deliberately between a revised silhouette and a custom hierarchy with matched KEY/PUP work. See [compatibility.md](compatibility.md). A filename alias or profile registration does not retarget animations.

## Re-atlas and bake without losing the source surfaces

Mando OC grouped its fitted geometry into five materials: helmet/neck, torso/hip, both arms, right leg and left leg. The implementation used xatlas at **1024×1024 working resolution**, **six-pixel packing padding**, chart rotation and iterative density reduction until each group fit one atlas. It then produced **256×256 game textures**.

Its bake followed two paths:

- **Unchanged topology:** retain exact source face/corner correspondence and barycentrically interpolate the original UVs. Assert that atlas generation preserved the expected face/corner relationship before relying on it.
- **Reduced topology:** densely sample the original surface, including position, normal and UV; find a nearby source sample using both position and normal. The recorded implementation used roughly 250,000 samples per part and a normal weight of 0.02 in its source units. Including orientation helps distinguish opposite sides of thin armour plates; inspect the result rather than treating that weight as universal.

The baker covered a small edge fringe and dilated nearby covered colours into atlas gutters before downsampling. Caps sampled a dark interior texel chosen with coverage depth and an 8×8 neighbourhood check so the lower mips stayed dark. Inspect UV seams and cap colour in the exported MAT readback, including the smallest mip, not only in the 1024 working image.

This route transfers albedo. It does not demonstrate a native JK normal-map or PBR workflow. Retain richer source maps for authoring, but bake any selected relief/light cues intentionally into the supported target surface. See [textures-and-imagegen.md](textures-and-imagegen.md) and [blender-export-and-baking.md](blender-export-and-baking.md).

## Build and compare the material lanes

Keep indexed and RGB565 outputs separate and identify the active renderer. For Mando OC:

- Five MATs per lane had four mip levels: **256, 128, 64 and 32** pixels square.
- Indexed conversion used the recorded `dflt.cmp` palette, SHA-256 `38f60876067f015d27d0298fadfdc67f24dd3cf75ba0a97492e90ed887fe7adc`, with Floyd–Steinberg dithering.
- RGB565 conversion packed 5/6/5 colour channels. Both lanes generated each mip from the working image and were strictly decoded after writing.
- The first 140 header bytes came from the corresponding lane's previously tested T-800 r4 MAT template. Reuse of a known header is a compatibility-oriented technique, not a substitute for testing the new texture or model.

The indexed red trim showed coloured speckle at point-blank camera distance; RGB565 was smoother. A no-dither or selectively dithered trim quantization is a candidate repair, but the reviewed package did not contain a verified fixed variant. Compare the alternatives at actual play distance and in close-up, with the target level's colormap. Very dark albedo also became mostly silhouette in dim rooms; a brightness adjustment was proposed, not validated.

OpenJKDF2 rendered both saved material lanes through OpenGL. Loading an indexed MAT there did **not** exercise original JK's 8-bit software rasterizer.

## Resolve the weapon-hand contract

Stock Kyle's right-hand mesh is empty because third-person weapon models carry that hand. Choose the contract deliberately:

- Use an empty body right-hand mesh when retaining ordinary stock carried weapons, then verify all intended visible cases.
- Keep a custom body glove only with a compatible weapon/attachment approach and explicit tests for overlap or missing hands.
- Treat first-person arms/hands as separate assets from the external body and external weapon.

Mando OC retained its right glove. Saved character-editor views show a tan patch from the stock pistol's own hand, and the README records the overlap compromise. The later **Mandalorian Red 0.2.1** models all have zero vertices, texture vertices and faces in `k_rhand`. This is a concrete native-file difference between the examples; it does not prove that every weapon case was independently tested.

## Validate native output and the intended role

1. Re-read the exported 3DO and MATs independently. Check indices, material resolution, face modes, finite values, hierarchy and bounds. Compare the saved model's actual hash with the validation receipt.
2. Run `inspect_contract.py` against the intended PUP's referenced KEY corpus. Preserve declared joint allocations and any stock tracks beyond visible nodes instead of deleting them automatically. Structural success is not runtime certification.
3. Render multiple front/side/rear views from the native readback. Include idle, run, fire, deepest crouch, jump and other design-sensitive actions. Inspect transitions and combined poses in the game afterward.
4. For MP, register a unique model and sound class, preserve all intended base registry entries, show the normal character-editor preview, and save/reload the selected MPC. For SP, test the actual player/NPC route; a stock-compatible `ky.3do` alias is a separate integration choice.
5. Test the original target executable and renderer where required, then any source-port edition separately. Use a second client for connected-MP claims. Record exact model, materials, executable, patches, maps, actions and capture hashes.
6. Inspect every geoset and mip. The two Mando examples below have only one geoset, so full geometry is drawn at every distance. They establish no successful LOD transition or multi-character performance budget. Later Red experiments used both a single-geoset policy and four duplicated full-detail geosets in different candidates; neither demonstrates reduced distant geometry. Read the actual target level's LOD thresholds and cross them with recorded camera cases. A near-only room or a Blender import of geoset 0 cannot validate the distance behavior.

Mando OC's registries added one model to the 28-row stock list and a human-readable display string, with assertions that existing rows and strings were unchanged. Such registries are owned game data; derive them locally or distribute only material for which distribution is permitted. See [runtime-and-packaging.md](runtime-and-packaging.md) for the full test ladder.

## Evidence ledger: keep the two cases distinct

### Mando OC

Artifact identifiers: `mandooc.3do`, `reports/build_report.json`, `reports/kyle_rig_contract.json`, `reports/inspect_contract_mandooc.json`, `authoring/build_parts.py`, `authoring/bake.py`, `authoring/build_native.py`, and `validation/runtime-openjkdf2-20261103/{ACCEPTANCE.md,runtime.json}`.

The shipped model in both lanes has SHA-256 **`854e103601f3c99ede97a9ba2643307bb8cc61545a8ef00a60b5dbd4638d0359`**. Fresh artifact review found all package-manifest hashes matched, and all 18 current hierarchy rows matched the saved Kyle contract for indices, names, types, mesh references, parentage and transforms.

The saved contract report says 62 KEYs passed but identifies an earlier model hash, `561007086555d8b1507a1ba2548debbb78adf40e57ff39135c7147d8801823c8`. Its topology matches the shipped one; this is still a stale final-byte receipt. Do not silently transfer the report to the current hash. Four recorded keys have node-18 tracks as in the documented stock case: `kyfall`, `kyidle1b`, `kyidle2b`, `kysbsabr`.

The November 3 runtime record identifies OpenJKDF2 commit **`a857b6320af71c0e7a526dfc9d8441a43bac0723`**, executable SHA-256 `c9b403134d65bf27fc3398b6e30d94e4ad24cecfd63fb1aa5cd4f6671381799e`, Wine 9.0, Xvfb and Mesa llvmpipe with dummy audio. Installed-input hashes tie its overlays to the current shipped model. Red's record reports character-editor preview/save, one local-host Canyon Oasis match and campaign level 1 via the alias for each lane, sampled movement/crouch/fire and clean quits. Saved captures were inspected and corroborate appearance and the documented visual defects; that review was not a new runtime execution.

Untested in this case: original JK.EXE and its renderers, native Windows, physical GPU, a second networked client, audio, save/load, saber, LOD and other levels. The Steam executable probe failed before model loading, so it is an environment blocker rather than evidence of a model failure.

The archived scripts require external Python support, source arrays/albedo, stock references and preparation/intermediate files not included in the reviewed package. Preserve these dependencies in a new job; do not advertise the historical bundle as fully reproducible without them.

### Mandalorian Red 0.2.1

This is a later, separate three-variant character package, not a renamed Mando OC release. The `Red_JK_Pack_2026-11-04.zip` manifest and native GOB entries were inspected. Its README says 0.2.1 added MP lists while retaining 0.2.0 geometry/textures. Each 3DO header attributes a Tripo source and Red/Claude conversion onto Kyle's hierarchy.

| Native model | Design provenance recorded in package | Faces | Maximum faces / vertices / UVs per mesh |
| --- | --- | ---: | --- |
| `redmandot.3do` | T-visor, Andrei's sheet | 5,045 | 480 / 248 / 718 |
| `redmando.3do` | Red visor, Lily's sheet | 4,976 | 480 / 243 / 669 |
| `redmandoh.3do` | Helmetless crown braid, Andrei's sheet | 5,022 | 480 / 244 / 721 |

All have one geoset, 18 hierarchy nodes, 15 materials and the empty right-hand mesh described above. Their hashes are respectively `6f0db0850c00bbacbcbe21b1d4aa2fc7c9250176520ff7d81fdf1a8b49c17a4c`, `ff5d373bcedbec5102ced190d6c349788de52fc1f8ed99f15869a819beaba85d`, and `61782e1b796f54eea5f49c1685eb483cb62c637d8c99323d7897d2af687a2eeb`.

The character GOBs contain identical model bytes for retail and SBX, but different registries: retail rows 28–30 within 31 entries, SBX rows 151–153 within 154 entries, all using `kyfemale.snd`. Do not install the optional stock-18-node SP aliases over SBX's documented 20-node Kyle/puppet without resolving that contract. The sulfur cross on the pauldrons remains listed as unfinished.

This archive contains no conversion scripts, source GLBs, views or runtime logs. Its attribution supports the Tripo provenance, but not an assumption that it used Mando OC's exact five-atlas recipe. Included lightwhip documentation describes separate retail 1.3.0 and SBX 1.1.0 assets; its runtime claims must not be generalized to every character variant. Later participant reports of character-editor or connected-MP testing need their own versioned evidence record.

Credit the contribution that supplied each part: Andrei's design/direction and authorized inputs; Tripo's generated source; Lily's named design contribution; Red/Claude's conversion and tests; and the Andrei/Coda/Lily native tooling explicitly credited by Mando OC. Preserve these boundaries when publishing methods without redistributing the underlying character assets.

## Later rigid-joint and face diagnostics

Red's November 16–18 Darth Lilitari R9–R9.3 work is a later, separate private
candidate. Read [rigid-joint and face diagnostics](rigid-joint-and-face-diagnostics.md)
for solid-color ownership checks in the exact failing KEY pose, cut-centred caps,
shallow shoulder lids, proportion adapters and matched clay/albedo face review.
Its freshly recounted native model has 13,554 faces per geoset, including 9,863
quads, equivalent to 23,692 triangles. Do not transfer the earlier Mando budgets,
rig layout or runtime claims to that model. The study separates newly inspected
records from Red's authored tests and does not distribute the candidate.
