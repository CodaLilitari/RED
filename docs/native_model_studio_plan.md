# Native Model Studio: numeric editing implementation plan

**Numeric editing and variant publication are implemented in source.** The [Model Studio guide](native_model_studio.md)
describes the Tools command, controls and save procedure. The host uses the [portable session](native_model_session.md)
and advances MESH-04 and the publication part of MESH-05. The requirements below define the implementation contract
and qualification gates. The complete MESH-01 through MESH-06 milestone remains partial under the
[approved model design](native_mesh_editing_proposal.md).

## First visible result

A mapper opens an admitted classic 3DO 2.1, selects a source LOD and mesh, previews a positive uniform scale and
translation or edits an existing UV row, accepts or cancels the change, uses local Undo/Redo, previews an associated
KEY from the unsaved model, and saves a new reviewed variant. The original model and open Level remain untouched.

The entry is **Tools > Model Studio…**. The separate hierarchy editor remains available. Viewport picking, gizmos,
per-vertex deformation, rotation, topology changes, radius edits and model recovery remain outside this implementation.

## Reuse and ownership

| Existing owner | Reuse | Required extension or boundary |
| --- | --- | --- |
| [NativeModelSession](../src/core/include/sed/editor/native_model_session.hpp) | Selection, baseline-relative proposals, exact history, clean revision and bounded timeline. | Keep one mutation owner. Take owning document handles for every reader; never retain a reference into replaceable preview storage. |
| [NativeModelDocument](native_model_document.md) | Source ordinals, admitted edits, exact candidate bytes and raw all-LOD geometry. | Render from the selected source snapshot. Preserve existing operation and source admission limits. |
| [KEY preview](../src/app/src/ui/resources/model_pose_preview.cpp) | Playback controls, marker navigation, joint transforms and wireframe comparison. | Use the owning snapshot overload for native model edits. Its LOD changes stay within the captured document. The separate hierarchy-editing overload retains its existing path-based geometry loading. |
| [Model Lighting dialog](../src/app/src/ui/resources/model_lighting_dialog.cpp) | Captured resource context, result generations, cancellation and reviewed variant publication order. | Use the session's cumulative committed source. Do not turn its history into a mixed native CLI batch. |
| [StagedAssetFile](../src/core/src/assets/atomic_file_write.hpp) | Closed sibling staging and atomic create-only publication. | Use `publishNew`; ordinary `publish` replaces an existing entry and has a different contract. |
| [Tools actions](../src/app/src/main_window/menus/menu_actions.cpp) | Discoverable menu entry and command wiring. | The new dialog owns its local document. Its commands must not enter the open Level's history. |

The [dialog and serial session owner](../src/app/src/ui/resources/model_studio_dialog.cpp) drive the
[selected-mesh view](../src/app/src/ui/resources/native_model_mesh_preview.cpp) and the
[variant writer](../src/app/src/ui/resources/native_model_variant.cpp). The
[app CMake file](../src/app/CMakeLists.txt) registers the focused tests and MainWindow context check.

## Task 1: render an owned unsaved snapshot

The bounded pose-preview adapter is implemented through the owning document/KEY overload. It retains all raw
geosets and UV rows, renders edited geometry and shared joint instances, fits both panes, and keeps framing fixed
during playback. **Reframe** and an explicit comparison-LOD change fit the current pose. The existing hierarchy
overload remains available. The Studio's separate static view draws a selected source mesh once, including an orphan
with no hierarchy references. It holds a fixed camera across updates to the same target and can explicitly refit.

The snapshot entry point accepts an owning `NativeModelDocument` and a captured KEY. Both panes select their
LODs from that snapshot's `geosets()` without reopening a source path. Geometry remains raw and mesh-local until
each sampled joint's `meshToModel` is applied exactly once. Multiple nodes referencing one source mesh render as
multiple instances without creating multiple authored meshes.

The displayed face vertices of both panes establish a shared comparison frame. A larger edited second LOD does
not depend on LOD 0's display radius. The camera remains fixed during playback until the mapper explicitly
reframes or changes the comparison LOD. The Studio inspector displays the selected source mesh's preserved
`authoredRadius` and its mesh-local bounds. The KEY readout identifies LOD 0's authored mesh radius separately from
the derived model radius. `Model3do::radius` can include the reader's expansion.

The [pose regressions](../src/app/tests/model_pose_preview_tests.cpp) measure wireframe pixels for an edited
LOD 1 after caller destruction and temporary source removal. They also check both shared-mesh instances and their
origins/pivots, fixed framing during playback, explicit Reframe and unsaved hierarchy edits in the older overload.
UV appearance requires separate textured evidence.

## Task 2: host the session without losing UI input or state

Capture source kind, source identity/hash, native path or resolved resource, resource context and project identity
when opening. Use Qt/native path conversion that preserves Unicode. A failed Open must retain the existing session
and its unsaved work. Before replacing or closing, inspect both committed dirty state and active interactions or
pending input; a preview alone does not make `NativeModelSession::dirty()` true. Resolve the interaction through
Apply or Cancel before the idle-only Save path. Replacing unsaved work requires an explicit save/discard/cancel choice.

The host dispatches one worker operation at a time while the GUI retains immutable displayed snapshots. At most
one newer preview waits to follow the active calculation. The large-UV fixture exercises the last ordinal in a
100,000-row table and measures admission, row selection and GUI timer delivery on the executing host.

Keep the following conditions when modifying queue ownership. Bind results to the session identity,
committed revision, interaction and input generation. Cancellation or newer input immediately disables Apply and
revokes obsolete results. A completed old calculation must not re-enable Apply or replace the displayed snapshot.
Closing invalidates callbacks without waiting in the GUI thread for a bounded calculation to finish. Preserve
committed history when cancelling an in-flight preview; dropping a worker result must not discard the editing session.

The minimum controls are source selection, explicit LOD/source-mesh ordinals, optional UV-row ordinal, scale,
translation XYZ, UV values, Preview, Apply, Cancel, Undo, Redo, KEY Preview and Save Variant. Display names supplement
ordinals; duplicate names must not select a different mesh. Reuse established numeric controls and preserve their
accepted precision. Invalid intermediate text must not fall back to an earlier valid proposal.

Begin freezes the operation family and target. Repeated previews are relative to the same committed baseline.
Keep target controls disabled until Apply or Cancel. Apply creates one history transition; Cancel and no-op Apply
preserve Redo. Show shared-instance/corner scope, changed bounds, retained radii and lighting-revalidation warnings.
UV review shows the actual stored pixel coordinates and referencing-corner count even before textured UV preview is
available. Do not present that numerical review as a demonstrated material appearance.

Test real control interactions, including invalid-after-valid input, selection changes, cancellation during work,
stale completions, no-op/failed Apply after Undo, exact dirty state and repeated close/open. Keep keyboard text Undo
inside the active numeric/text editor; model Undo acts on the model session. Test with an independently dirty Level
and verify that its data, selection, history and dirty state remain unchanged.

## Task 3: review and publish the cumulative model

Saving requires an idle interaction and a reviewed committed snapshot. Capture its opaque session revision, exact
output bytes/hash, original input identity, current resource context and explicit destination. The review identifies
affected source meshes/UV rows, preserved LODs and hierarchy, retained radius/lighting warnings, and the absence of
target-game acceptance. A sequence containing both UV and transform commits publishes those combined bytes; the
existing homogeneous CLI batch contract remains unchanged.

Use this order:

1. Revalidate the original captured source and live context. Refuse an occupied or invalid destination without
   changing the session. A changed source requires reopening it after an explicit replacement decision; existing
   edits are not automatically rebased onto it.
2. Reopen the exact candidate through the native document reader and prepare all data needed for the success report
   before writing. The selected output path and reported path must identify the same native entry.
3. Stage, write, flush and close the complete candidate. Recheck source/context and the accepted session revision at
   the publication boundary, using the application's established generation and lifetime guards.
4. Publish atomically with `publishNew`. Do not fall back to replacement or non-atomic copying when a filesystem
   cannot provide this operation. Preserve the original and every previously published variant.
5. Acknowledge the exact saved revision. A delayed acknowledgement must not mark newer edits clean. Once publication
   succeeds, later UI/reporting trouble must not be described as proof that no output exists.

Source rechecks are optimistic checks, not a lock against other processes. The serial worker stages the reviewed
bytes. The GUI keeps that revision sealed and checks `ModelStudioDialog::Context::isCurrent` immediately before
create-only publication. The callback is GUI-thread-only. The final source/context check and publication decision
do not pump events; acknowledgement marks only the published revision clean. A stale-save acknowledgement protects
the clean checkpoint but does not itself serialize publication.

Exercise Unicode loose/resource sources and destinations, source replacement during review/staging, occupied and
concurrently created destinations, path aliases, injected write/flush/close/publish failures, and stale save
acknowledgements. Verify actual native directory entries and hashes. Failed publication retains the session and
history. Successful variant creation must not automatically rebind Things or change Level history.

## Task 4: qualify the numeric slice and preserve the remaining gates

Run the focused controller/dialog/pose tests, the portable session and native-document tests, and the complete
configured Release suite. Exercise the native dialog on an idle supported desktop, including high-DPI controls,
keyboard-only operation, window close and reopening. Retain source hashes, exact commands and failed-case evidence.

For the owned two-LOD fixture, perform a UV edit and a mesh transform in one session; cancel another proposal;
Undo/Redo; compare the edited LOD under a named KEY; and publish a new variant. Compare every unedited parsed field
using the pinned independent [Blender-Sith readback](../tests/fixtures/relay-sentry/README.md#native-authoring-readback).
Record actual target-engine rendering/animation/collision separately when executed.

Update the designer guide, command discovery, capability matrix and roadmap with the exact delivered numerical
workflow. Keep vertex/face picking, gizmos, broader shape/face/radius edits, persistent model recovery and the complete
MESH-06 acceptance package open until their own tests and observations exist. This implementation plan does not
authorize a new recovery schema or claim that the full content studio is finished.
