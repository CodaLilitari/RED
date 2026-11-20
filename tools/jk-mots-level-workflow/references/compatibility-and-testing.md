# Compatibility and useful verification

Use the target engine profile requested by the user. Retail JK, retail MotS, OpenJKDF2/OpenMotS and patches with extra COG verbs have different contracts. A registered verb does not establish the corresponding runtime event or replication behavior.

MotS white lighting and RGB lighting are independent. Source lights distinguish nullable RGB overrides from explicit equal/zero values; omission preserves a field, while an explicit null can relink it. Historical RGB range metadata is not automatically a second falloff. Never impose a universal 0..1 clamp on preserved historical values.

Dynamic RGB readiness must be assessed from the **actual proposed serialized output and final float32 values**. A source-preserving writer can retain scalar or mixed rows while regenerated MotS rows use four values per corner. Inspection of one retail MotS executable found a compaction predicate comparing white/red/green across the whole surface, with blue absent from that branch. This is source/original-binary evidence, not a completed retail load/mutate/render experiment or a universal rule for scalar rows. Preserve the uncertainty; do not globally add epsilon offsets or set an RGB flag without storage. See the project's current dynamic-readiness documentation/schema when available rather than assuming an operation exists in an older package.

Current ARCH recipes distinguish source-point baking from source-point baking occluded by world polygons. World occlusion already exists in supported builds. It does not imply model/self/alpha-aware shadow tracing, animated/LOD correspondence or dynamic COG execution. Preserve source/resource revision, freshness, manual overrides and unknown legacy provenance. Binding retained compatible samples, resetting samples and calculating new light are different actions.

## Match verification to the requested change

| Evidence | Useful acceptance | Does not establish |
|---|---|---|
| Query/plan/apply/save/reopen | Exact target set, reviewed bytes, references, preserved channels, topology, undo where applicable | Runtime resource loading or gameplay |
| Offscreen diagnostic/lit snapshot | Camera-specific visual geometry/material/lighting result and captured resources | Retail parity, running COGs, moving lights |
| Engine capture | Exact captured binary/resources/camera and measured scenario output | Arbitrary engine builds or untested transitions |
| Gameplay fixture | Observed controls, gate blockage, movement, portal crossing, objectives with negative controls | Campaign transition, multiplayer synchronization, deterministic simulation unless separately measured |

Canonical original-fixture routes include `tools/Test-AgentDesign.ps1`, `tools/Test-AgentSpatial.ps1`, `tools/Test-AgentEditing.ps1` and the current TOOL/ARCH/PLAY workflow helpers named by the project docs. Pass an explicit executable and a fresh output directory; inspect their current help before use. Use selected relevant checks rather than replaying the entire project suite for a simple level edit.

For real runtime work read `docs/ENGINE_CAPTURE.md`, `docs/PLAYABLE_AUTHORING.md`, and the current checkpoint. Capture protocol revisions and package versions change; do not assume the old capture-v1 limits or test counts describe the newest instrumented companion. Use exact manifests and source/binary receipts. `launch-plan` prepares arguments without starting a game.

COG frames are ordered absolute poses; repeated frame parameters cannot be represented by a dictionary that keeps only the last value. MotS MoveToFrame speed semantics differ from casual editor intuition. Typed static bindings do not prove thing identity survives difficulty filtering or runtime destruction/reuse. Any source-level COG editing must preserve labels, ordering and message fallthrough. Byte-preserving reads are preferable to unsolicited encoding normalization.
