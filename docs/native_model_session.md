# Portable native model editing sessions

The [NativeModelSession API](../src/core/include/sed/editor/native_model_session.hpp) owns a mutable authoring session
around the immutable [all-LOD native model document](native_model_document.md). It provides source-addressed selection,
transactional previews, exact source history and a clean checkpoint for the existing UV-row and whole-mesh transform
operations. It is independent of the Level document and Qt. [Model Studio](native_model_studio.md) hosts the session
and handles reviewed publication. Model draft recovery remains on the [mesh roadmap](native_mesh_editing_proposal.md).

## Select, propose and apply

```cpp
using namespace sed::editor;
auto session = NativeModelSession::open(sourceText);
session.setSelection(NativeModelSelection{ 1, 1, std::nullopt });
session.beginMeshTransform();
session.previewMeshTransform(1.0, { 1.0, 0.0, 0.0 });
session.previewMeshTransform(1.0, { 2.0, 0.0, 0.0 });
auto displayed = session.displayed(); // Owning immutable snapshot: translation is +2, not +3.
const bool changed = session.apply();
```

Selection contains the geoset and authored source-mesh ordinals, with an optional texture-vertex ordinal. Mesh names
are descriptive; duplicate names do not merge addresses. Shared instances reference the same authored mesh, and an
orphan mesh remains selectable. `std::nullopt` clears selection outside an interaction. Invalid addresses refuse
without changing the selection. A mesh transform uses the selected mesh even when a UV row is also selected.
`beginTextureVertexEdit()` requires a selected UV row; `previewTextureVertex(u, v)` uses the document's native float
texture-pixel values and existing validation.

Begin freezes the operation family and target. Every proposal is computed from the committed document at Begin.
Selection changes, another Begin, Undo and Redo refuse until Apply or Cancel ends that interaction. A proposal never
changes the committed document, revision, clean checkpoint or history. `committed()` and `displayed()` return cheap
owning document handles, so a caller can pin a snapshot across later edits, Cancel or proposal failure.

Each new proposal first discards the session's previous proposal. Invalid values, a wrong operation family or a core
validation failure therefore leave no stale proposal that Apply could accidentally commit. The displayed session
falls back to its committed document. The interaction remains active so its inputs can be corrected. Apply with no
valid current proposal refuses. Cancel is idempotent and restores the exact committed display and frozen selection.

Effective Apply publishes one authored revision, regardless of the number of preview samples. The candidate has
already been reopened and validated by `NativeModelDocument`; all history allocations and retention preparation
precede mutation of the live document or Redo branch. A valid byte-identical proposal ends the interaction and returns
`false`, preserving both history and revision. An admission or allocation failure preserves committed state, selection,
history and checkpoint. An over-budget Apply leaves its valid proposal available for inspection or Cancel.

The existing document's preservation and refusal policies still apply: only admitted UV or XYZ token spans change;
all LODs, source ordinals, connectivity, hierarchy, normals, authored radii, lighting and unrelated bytes remain intact.
Successive UV and transform interactions retain their cumulative edits. There is no new mixed-operation CLI batch.

## Exact history and saved state

Undo and Redo store exact source strings rather than reconstructing an inverse numeric operation. Returning to a
revision restores comments, line endings, numeric spelling and signed zero. Each transition retains its before and
after selection. Changing or clearing selection after an edit does not rewrite that transition's selection.
An Undo/Redo destination is reopened before changing the live cursor, document or selection; a failed open leaves
those values and both history directions intact.

`canUndo()` and `canRedo()` are false while an interaction is active. `historyStats()` still reports the retained
depths during that interaction. After Cancel, refusal or no-op Apply, an existing Redo branch remains available.
A successful new edit discards Redo only when the prepared candidate and timeline are published.

Revision tokens carry a private session identity and a monotonically increasing authored ordinal. The ordinal is
useful for diagnostics but is not independently a revision token. Equal ordinals from separate sessions do not
compare equal. Returning through Undo/Redo restores the original token. Branching creates a fresh token and never
reuses the discarded revision's ordinal.

The initial revision is clean. `dirty()` compares the committed revision token with the clean checkpoint, independently
of cursor position or source-byte equality. A new edit that happens to reproduce an older saved source remains a new
revision. Evicting history cannot make a different cursor position clean. Pending previews and selection changes do
not themselves make the committed document dirty; a host must also consider `interaction()` before closing a session.

An external publisher may retain `committed()` and `revision()` together, publish the reviewed bytes, then call
`acknowledgeSaved(revision)`. Only the exact current session-bound token succeeds. A stale, foreign or empty token
returns `false` and preserves the checkpoint. Acknowledging the committed revision during a preview is allowed;
subsequently applying an effective proposal creates a dirty new revision. This API performs no file write, source
recheck, destination reservation or recovery serialization. A publisher remains responsible for those contracts.

## Retention and memory accounting

The default limits are **64 transitions** and **64 MiB of retained timeline source-string payload**, including the
current slot. At most 65 source slots can therefore be retained. A host can choose smaller limits at Open. Zero
transitions retains exactly the current source slot; a zero byte budget or limits above the supported ceilings refuse.
The source must also meet the document's independent 16 MiB input/output and parser-work ceilings.

Opening refuses if its initial source cannot fit the configured payload budget. Effective Apply first verifies that
the candidate alone fits, then retains the newest possible sequence ending at the new current revision. It discards
the old Redo branch and evicts oldest Undo states only at successful publication. A candidate that cannot fit as the
current slot refuses instead of evicting that slot. Count and payload limits constrain the same retained timeline.

The payload budget counts each retained source string's `size()`. It excludes string capacity, metadata, the parsed
committed and preview documents and their source copies, temporary parser allocations, and snapshots retained by
external callers. It is not an RSS limit or an allocation guarantee. `historyStats()` reports timeline payload plus
the committed and preview source sizes separately; identical document handles can share underlying storage.
The session retains parsed documents only for its current and optional preview state, never for every history slot.
Preparing Apply shares retained immutable history strings rather than duplicating all their bytes.

Open captures the original source's SHA-256 and byte count from the admitted immutable document. These remain fixed
through editing, branching and history eviction. It does not retain another permanent original source string or
parsed document after the original timeline slot is evicted. The hash view borrows from the session. An external
publisher that needs original file bytes or filesystem identity must capture and account for them separately.

The session has one mutation owner; it supplies no internal locking or asynchronous publication mechanism. Large-model
preview and Undo latency must be measured before exposing continuous pointer updates. This portable foundation does
not claim native GUI, recovery, retail rendering, animation or collision acceptance.

## Verification

`sed_native_model_session_tests` uses the owned relay-sentry fixture and synthetic source variants. It checks duplicate
names, shared and orphan mesh addresses, empty/invalid selections, fixed targets, baseline-relative proposals, stale
preview rejection, exact history and cumulative UV/mesh edits. Retention cases cover exact current-slot budgets, count
and byte eviction, zero transitions, and an admitted effective candidate that exceeds the payload budget while Redo
exists. That refused Apply must retain the exact Redo bytes, revision, selection and payload count. Checkpoint cases
include branching, eviction, stale acknowledgements and equal-ordinal foreign tokens.
