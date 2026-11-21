# Native 3DO mesh editing design

**Approved direction.** Direct editing in the native workspace remains planned. The implementation contracts and gates below define acceptance. See the [product roadmap](product_roadmap.md). This document defines a bounded native model workspace
and the Blender-Sith behavior worth reusing. Current functionality remains documented in [model hierarchy and KEY
authoring](model_hierarchy.md), [model import](model_import.md), [model lighting](model_lighting.md), and the
[portable all-LOD source document and UV-edit API](native_model_document.md). That API implements the first source
capture and UV-writer subset; it does not complete the native workspace milestone.

## Approved direction

Build a native **Model Studio** around a source-preserving model document. Reuse Blender-Sith's format semantics,
transform rules, diagnostic ideas, and independent synthetic fixtures. Keep deterministic operations in RED's portable
core and use the same operations from the GUI and reviewed automation.

The first useful milestone is **shape and surface editing with topology and skeleton identity held fixed**. A mapper
should be able to reshape a helmet, fit an armor piece, adjust a prop, repair UV alignment, or change a face's shading;
preview the result with a KEY; undo the interaction; and save a new validated model variant. Subsequent milestones add
topology changes, budget-aware partitioning, and broader integration with level resources.

This is a substantial authoring-document addition. Use the milestone scope and preservation contract below.
Do not remove current export refusals to make an early editor appear to support more source files.

## Audited foundations

| Project | Inspected source | What it contributes |
| --- | --- | --- |
| RED | [40efbd2][red-baseline] | Existing 3DO representation, hierarchy and KEY tools, normal-aware import, model lighting, and staged asset publication. |
| Blender-Sith upstream | [168dadc][upstream-baseline] | Portable resource codecs, scene adapters, Sith properties, and model/animation test vectors. |
| Blender-Sith Coda | [34d256c][fork-baseline], source for `v1.2.0-coda.1` | Current upstream integration plus native-pivot preservation/editing and an index-based model/KEY binding audit. |
| Holocron | [User manual, version 0.2.1][holocron] | Documented authoring workflows; see the separate [Holocron comparison](editor_comparison.md#holocron). |

These are source and documentation baselines, not claims that every application or target-game scenario was executed.
The older Blender-Sith audit at `ecc90131` is historical; its limitations must not be applied indiscriminately to the
current add-on. The imported [Model Workshop knowledge](knowledge/models/blender-sith-fork.md) has its own upstream
regeneration contract and must not be hand-edited as part of this feature.

## What a Blender-Sith port can provide

The add-on's scene importer/exporter calls Blender's `bpy`, `bmesh`, and `mathutils`. Those adapters do not constitute a
standalone mesh-editing kernel. Its portable resource model represents multiple geosets, while its Blender scene
import/export path represents geoset 0. Reusing the add-on therefore does not, by itself, provide native RED extrusion,
welding, viewport picking, history, or complete LOD preservation. See the [importer][fork-importer],
[exporter][fork-exporter], and [portable model][fork-model].

| Approach | Benefit | Cost and decision |
| --- | --- | --- |
| Keep modeling entirely in Blender and improve the round trip | Fast access to Blender's established tools. | Useful complementary route, but it does not meet the request for direct editing inside RED. |
| Embed Blender or broadly transplant its editing implementation | Access to a much larger modeling system. | Adds runtime, UI, dependency, and maintenance scope well beyond the requested Sith editing workflow. No such dependency is proposed. |
| Build RED-native operations and selectively port contracts and tests | Fits RED's document, review, resource, and target-engine workflows. | Recommended. Requires a real model document, editing controller, preservation writer, and interaction tests. |

### Reuse priorities

| Priority | Useful upstream material | RED application |
| --- | --- | --- |
| First | `model3do.py`, `model3doLoader.py`, `model3doWriter.py`, `docs/formats/3do.md` | Independent format checks, version distinctions, source identities, and multi-geoset fixtures. Extend RED's codec deliberately rather than replace it wholesale. |
| First | `model/utils.py`, exporter node-pose logic, native-pivot tests | Keep geometry, joint position, mesh pivot, and insertion offset distinct; preserve Empty joints and descendant frames. |
| First | Exporter vertex/normal/color handling and model fidelity vectors | Protect normal and color seams, independent UV indices, reflected winding, loose vertices, and face attributes. |
| Next | Importer/exporter UV-domain metadata and `test_uv_images.py` | Explicit pixel versus normalized UVs, trustworthy texture dimensions, and tiling without repeated scaling. Adapt Blender's coordinate convention to RED explicitly. |
| Next | `key/modelBinding.py`, `keyValidation.py`, KEY vectors | Diagnose index bindings, target limits, timing, and names separately; extend existing RED checks rather than replace working delta reconstruction. |
| Throughout | Hand-authored `tests/tv` fixtures and expected results | Independent cross-reader evidence, including malformed and adversarial input; preserve fixture provenance. |

The [fork's pipeline contract][fork-pipeline] distinguishes native mesh frames from visible surfaces. Its
[KEY binding audit][fork-binding] addresses hierarchy indices; names are diagnostic evidence. Its
[test-vector documentation][fork-vectors] identifies synthetic fixtures suitable for independent checks.

## Existing RED boundaries

`Model3doMesh` already stores positions, vertex colors, separate UVs, vertex/face normals, materials, render modes,
flags, and authored radii. A `Model3do` load exposes one requested geoset. Default rendering loads can apply hierarchy
transforms, expand shared mesh instances, and omit unreferenced source meshes. A model-editing document must use raw
authored geometry and stable source identities, not edit those derived render copies.
See the [current representation](../src/core/include/sed/assets/3do.hpp).

The [general writer](../src/core/src/assets/3do.cpp) refuses multiple geosets, unrepresented source sections,
hierarchy-transformed geometry, and other unsupported data. Its canonical output also normalizes numeric text and
omits comments. The [model-lighting implementation](../src/core/src/assets/model_lighting.cpp) offers a stronger
preservation pattern: it checks the source and edits selected lighting token spans while retaining unrelated bytes.
The native document now shares this bounded scanner and captures UV spans while applying stricter authoring
admission. Position, normal, face-property and radius span editing remain new infrastructure.

The current hierarchy dialog has no model-local Undo/Redo stack. Its `writeFresh` checks existence before using an
ordinary output stream; that is not atomic no-replace publication. New model editing should use the stronger staged
asset publication demonstrated by model import and lighting. The [Level document](../src/core/include/sed/editor/document.hpp)
does not automatically provide asset history: it owns a Level and Level selection.

## First milestone: native shape and surface editing

### User-visible scope

- Open an admitted classic text 3DO 2.1 and choose an explicit geoset and authored source mesh. Retain all other
  geosets without rewriting them. Read-only inspection remains available when an edit cannot be admitted safely.
- Select vertices or faces in a textured or wireframe model view, with numeric selection available for precise work.
- Move selected vertices; translate, rotate, and positively, uniformly scale a whole rigid mesh in mesh coordinates.
- Assign an existing material slot, edit existing UV coordinates, and change admitted face flags and render modes.
- Inspect normal changes and visual bounds separately from authored collision-related radii; change existing model or
  mesh radius values only through explicit reviewed controls, with warnings when retained values may underbound the shape.
- Preview an associated KEY using the existing animation evaluator, without changing the KEY or hierarchy.
- Undo/Redo one completed drag or numeric edit as one operation; Cancel restores the exact baseline.
- Save a new 3DO variant after source checks, validation, readback, and atomic create-only publication.

This milestone changes no vertex, UV, face, mesh, material-table, or hierarchy-node counts. It also leaves corner
connectivity and skeleton identity unchanged. It does not include split, extrude, bevel, weld, deletion, triangulation,
Make Unique, automatic LOD generation, or general UV unwrapping. Those operations need an explicit topology-rewrite
contract. Editing a shared UV row affects every referencing corner; a face-only change requiring a new UV row is a
later operation, not an invisible exception to fixed topology.

Existing hierarchy tools remain available as their current separate workflow. Combining hierarchy restructuring
with geometry edits is a later milestone because it changes animation and reference contracts.

### Model document and ownership

The proposed model document owns captured source bytes and their hash, admitted format/profile, all-geoset source
access, materials/hierarchy, authored element identities, current edits, selection, clean checkpoint, and bounded
history. A source-span map identifies the exact fields an admitted operation may replace. Render meshes and KEY poses
are derived views of this document.

| Owner | Responsibility |
| --- | --- |
| Portable core | Source parsing and admission, deterministic operations, preservation invariants, model history, validation, semantic change report, serialization. |
| Qt application | Viewport picking, selection overlays, gizmos, inspector, background orchestration, model workspace, and recovery interaction. |
| Publication service | Immutable inputs, revision/context checks, readback, staging, source rechecks, and atomic creation of a new asset. |
| Level document | Explicit later placement or rebinding of the saved resource; it does not silently become the owner of unsaved model edits. |

Use the existing [atomic asset writer](../src/core/src/assets/atomic_file_write.hpp) and the application patterns in
[model import](../src/app/src/ui/resources/model_import_dialog.cpp) and
[model lighting](../src/app/src/ui/resources/model_lighting_dialog.cpp). Derive rendering from source meshes loaded
with `applyHierarchyTransforms = false`; each hierarchy instance then gets its own derived transform.

### Preservation policies

| Concern | Required behavior |
| --- | --- |
| Multiple LODs | Edit the selected geoset; retain untouched geoset bytes. Never assume matching vertex indices imply correspondence across LODs. |
| Shared mesh instances | Show every node affected by a source-mesh edit. Refuse an instance-only request until explicit Make Unique exists. |
| UV identity | Preserve the separate geometric and texture-vertex index domains. Show all corners affected by a shared UV edit. |
| Normals | Transform existing normals correctly for whole-piece operations. For vertex deformation, require a stated normal policy and review affected normals; do not silently smooth the whole model. |
| Invalid deformation | Refuse degenerate, inverted, or nonplanar results outside the admitted face contract. Do not secretly triangulate or alter connectivity. |
| Face attributes | Preserve material, flags, geometry/light/texture modes, extra light, and stored colors unless named in the operation. Unknown bits stay intact. |
| Radii | Recompute derived display bounds separately. An authored model or mesh radius can affect engine behavior; changing it is an explicit reviewed edit. |
| Native frame | Keep insertion offset, node order/names, parents, transforms, and pivots unchanged in this milestone. Shape edits and joint edits are distinct actions. |
| KEY/PUP | Keep indexed bindings and associated files unchanged. Pose comparison is evidence about sampled poses, not full runtime blending or attachment behavior. |
| MotS ARCH and lighting | Report the edited candidate's known lighting dependencies as stale when shape changes. Original asset/Level bindings stay untouched; later rebinding must invalidate or revalidate affected ARCH data even with fixed counts. |
| Unknown source records | Retaining bytes does not prove semantic compatibility. Refuse edits when an unknown dependent section cannot be shown to remain valid. |
| Publication | Changed source/context or occupied destination requires a fresh review or different destination; no partial output and no silent overwrite. |

Require complete, finite face/vertex normal records for an affected mesh in this first milestone. Missing or incomplete
normal sections remain inspectable but cannot be edited through this writer; do not fall through to the canonical
writer's normal derivation. Inserting or rebuilding such sections belongs to the later section-rewrite contract.

Start with the classic 2.1 profile and preserve its original stored scalar lighting. Support for 2.3 or any other
write profile requires separate UV/color and target tests. Do not reinterpret a classic 3DO's vertex lighting as a
new RGB-lighting capability because the internal representation can store colors.

### Interaction and review

Open Model Studio from a selected model resource or placed Thing. The workspace identifies the resolved source and
shows a geoset/mesh list, the model viewport, an inspector, and optional KEY timeline. It can reuse the hierarchy tree,
pose evaluator, and isolated model rendering already present; editable textured vertex/face picking remains new work.

A completed edit shows its affected mesh/vertex/face count, changed attributes, affected shared instances, normal and
radius decisions, preserved LODs, known stale lighting, and target-limit warnings. Before publication, show both the
shape difference and the native frame/identity invariants. A model that looks unchanged can still have a changed
attachment frame, so those checks are separate.

Initial publication creates a resource variant. It does not automatically replace the original or rebind every Thing
using it. A later **Save variant and bind selection** operation needs an explicit combined asset/level transaction,
cache invalidation, Undo behavior, and generated-file recovery. Level Undo must not pretend it deletes a reusable
published asset.

### Automation and recovery

Expose model inspection, edit planning, and edit application through the same portable operations used by the GUI.
These are proposed responsibilities, not registered command or MCP names. Requests should identify source bytes,
profile, geoset, source-mesh/element identity, explicit operation, and expected revision. Planning returns semantic
changes, warnings, request identity, and the proposed output hash without writing files. Application rechecks the
source/request identity and accepted output hash, rejects stale inputs, and publishes only the reviewed result.

Unknown request fields and unsupported operations must be refused. Bound request size, allocations, history, and
output budgets. Background preparation uses immutable snapshots and context/generation checks; closing or cancelling
revokes publication without blocking the UI on an already-running bounded task.

Unsaved model work needs recovery before broad adoption. Specify a source-bound recovery representation with explicit
stale-source handling. Do not casually extend `.sed` or invent a second competing mutable model authority; any new
persistent schema needs its own reviewed format contract and failure tests.

## Following milestones

### Topology and rigid-piece tools

Add split, triangulate, extrude, delete, face construction, seam-aware weld, and Make Unique after section replacement
and identity remapping are defined. New faces need explicit material, UV, normal, and lighting inheritance. Welding
must account for normal/color seams; coincident position is insufficient. A reflected transform needs deliberate
winding and corner-attribute handling.

Mesh partitioning should preview engine counts after normal/color splits, preserve existing joint identities where
possible, and report any appended nodes, KEY allocation impact, attachment changes, and stale ARCH bindings. A split
is not automatically animation-preserving just because pieces follow the same visible parent.

RED already checks per-mesh loader limits and warns about larger total models. Passing those checks does not prove
that a particular model will draw safely in both retail renderers. Keep loader constraints, observed budgets, and
actual package/engine results separate. See [model import](model_import.md).

### Animation-aware fitting and lighting-aware geometry

Once native editing is trustworthy, add sampled pose envelopes and side-by-side before/after KEY playback for armor,
grips, feet, and attachment inspection. Report exactly which KEYs and frames were checked; do not infer all-animation
or runtime acceptance from a sparse sample.

A further useful experiment is to rank candidate geometry changes by the lighting improvement they produce within
an explicit geometry budget. Holocron already documents diagnosing interior lighting lost by corner interpolation
and cutting affected walls; that is direct Sith-editor precedent, not a new invention by RED. RED's opportunity is
measured error displays, reviewed cuts, stable ownership, reproducible bakes, and target evidence. World-surface
refinement and 3DO mesh refinement need their own output and reference contracts.

Keep the [indexed 8-bit snapshot profile](lighting.md#jk-indexed-world-snapshots) as a separately qualified renderer feature. Mesh editing, palette
conversion, and an indexed preview should cooperate, but none proves the correctness of the others.

## Implementation work packages

The identifiers below are handoff labels, not existing issue numbers or executable APIs. Follow the repository's
regression-first TDD requirement for every functional change.

| Item | Deliverable | Dependencies and completion evidence |
| --- | --- | --- |
| MESH-01 | Source-backed model document and explicit 2.1 admission | Independent fixtures first. No-op byte identity, raw source identities, shared meshes, all-geoset retention, and unknown-data refusal. |
| MESH-02 | Checked fixed-topology source-span writer | MESH-01. Position/UV/normal/face-property/radius spans; exact unrelated-byte preservation; strict RED and independent Blender-Sith readback. |
| MESH-03 | Portable edits and model history | MESH-01/02. Deterministic transforms and field edits; stated normal/radius policy; one commit per interaction; exact Undo/Redo/Cancel. |
| MESH-04 | Native Model Studio interaction | MESH-03. Perspective/orthographic selection, occlusion/X-Ray, numeric and gizmo editing, shared-instance scope, KEY preview, high-DPI interaction. |
| MESH-05 | Reviewed automation, staged publication, and recovery | MESH-02/03; integrate with MESH-04. Hash/context guards, create-only output, stale-source refusal, cancellation, recovery, and existing-output fault tests. |
| MESH-06 | First native editing acceptance package | MESH-01 through MESH-05. Own synthetic model/KEY/MAT fixtures, UI review, independent parsing, complete Release gate, named target-engine runs. |
| MESH-07 | Topology operations and budget-aware partitioning | MESH-06 plus approved rewrite/remap design. Separate vertex/UV/mesh/joint identity tests, new-face attribute inheritance, LOD preservation, and ARCH/KEY impact reporting. |

The first milestone is complete only when the mapper can open, edit, preview, undo, recover, and publish through the
native workspace. A core writer or a wireframe demonstration alone is not the advertised feature.

### Acceptance fixture matrix

| Fixture or action | Required result |
| --- | --- |
| Two geosets, edit one vertex in LOD 0 | Every untouched LOD and unrelated source region remains byte-identical. |
| Two nodes share one source mesh; nonzero pivots | Correct derived instances, explicit affected scope, unchanged joint frames, no double transform. |
| Independent UV seams and coincident vertices with different normals/colors | No accidental weld, scaling, smoothing, or material reassignment. |
| Non-square texture; negative/tiled UVs | Native units survive two save/reload cycles; absent texture dimensions are not guessed. |
| Vertex edit invalidates a face | Refusal leaves source, document, selection, and history unchanged. |
| Sparse KEY tracks, Empty intermediary joints, markers | Identical indexed bindings; matching sampled poses except for the intended geometric change. |
| Whole-piece transform and a local deformation | Correct stated normal policy; display bounds and authored radius decisions remain distinct. |
| Cancel, Undo/Redo, reopened recovery | Exact baseline restoration; stale recovered source requires review. |
| Existing target, concurrent creator, alias, write/flush/publish failure | No overwritten source or destination; no partial published asset. |
| Large or malformed source and request | Bounded work and explicit failure; no weakened loss guard. |
| New edited variant in a package | Actual target/game/renderer observations recorded separately from editor and structural evidence. |

Use the existing [relay-sentry cross-reader fixture](../tests/fixtures/relay-sentry/README.md) and independently authored
Blender-Sith vectors where their provenance permits. Extend the relevant core, hierarchy, lighting, cross-reader,
pose, and native dialog tests, and add a dedicated editing target. Follow the complete Release gate in
[AGENTS.md](../AGENTS.md); a subset or a different platform does not silently satisfy that gate.

## Provenance and attribution

Blender-Sith's [manifest][fork-manifest] declares GPL-3.0-or-later and credits Crt Vavros; the fork retains the
[GPL license][fork-license] and documents the division of work in [FORK.md][fork-notice]. RED already acknowledges
the Blender-Sith KEY delta rule in [THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md).

For each copied or translated implementation, retain applicable notices and record the original path, function,
source commit, and modifications. Record copied fixture provenance too. A Python-to-C++ translation still needs
attribution. The MIT license of imported Model Workshop documentation does not relicense the add-on's source code.
This proposal copies no implementation or fixture into RED.

The Holocron manual is evidence for workflow comparison. A public executable or manual does not establish a source
license for code reuse. Keep its documented feature audit distinct from any future source-port proposal.

[red-baseline]: https://github.com/CodaLilitari/RED/tree/40efbd291b856c6c82a93df76ffe8f1576a594de
[upstream-baseline]: https://github.com/smlu/blender-sith/tree/168dadcb1f744da833ec13f0c115fc08652b05f6
[fork-baseline]: https://github.com/CodaLilitari/blender-sith/tree/34d256c30f66fe9826e5a3318f2fd4397257f24c
[fork-importer]: https://github.com/CodaLilitari/blender-sith/blob/34d256c30f66fe9826e5a3318f2fd4397257f24c/sith/model/model3doImporter.py
[fork-exporter]: https://github.com/CodaLilitari/blender-sith/blob/34d256c30f66fe9826e5a3318f2fd4397257f24c/sith/model/model3doExporter.py
[fork-model]: https://github.com/CodaLilitari/blender-sith/blob/34d256c30f66fe9826e5a3318f2fd4397257f24c/sith/model/model3do.py
[fork-pipeline]: https://github.com/CodaLilitari/blender-sith/blob/34d256c30f66fe9826e5a3318f2fd4397257f24c/docs/blender-pipeline.md
[fork-binding]: https://github.com/CodaLilitari/blender-sith/blob/34d256c30f66fe9826e5a3318f2fd4397257f24c/sith/key/modelBinding.py
[fork-vectors]: https://github.com/CodaLilitari/blender-sith/blob/34d256c30f66fe9826e5a3318f2fd4397257f24c/tests/tv/README.md
[fork-manifest]: https://github.com/CodaLilitari/blender-sith/blob/34d256c30f66fe9826e5a3318f2fd4397257f24c/sith/blender_manifest.toml
[fork-license]: https://github.com/CodaLilitari/blender-sith/blob/34d256c30f66fe9826e5a3318f2fd4397257f24c/LICENSE
[fork-notice]: https://github.com/CodaLilitari/blender-sith/blob/34d256c30f66fe9826e5a3318f2fd4397257f24c/FORK.md
[holocron]: https://jkjunkie.com/holocron/manual.html
