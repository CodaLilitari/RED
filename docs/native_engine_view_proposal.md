# Native Engine View proposal

**Status: design proposal for review.** This document adds no editor UI, engine protocol or live-update
implementation. It develops a possible extension of [LOOP-01](product_roadmap.md#review-validation-and-target-testing)
and does not change the qualification of existing capture tools.

## Goal and recommendation

Edit a level in RED while a native OpenJKDF2 world remains running and renders the result. A designer should be
able to follow the editor camera, change a supported surface material, undo it, adjust authored lighting and
inspect the accepted bake in the engine. Capturing that view should produce an image associated with the exact
document revision, runtime inputs and rendering configuration that produced it.

Keep authoring, selection, gizmos and Undo in RED. Use a separate, explicitly owned engine process for native
rendering and simulation. Start with its own preview window; an embedded image dock can follow after the update
protocol works. This is an editor preview workflow, not an in-game level editor or a GeoSith integration.
Embedding or copying the renderer into RED is not the recommended first implementation.

Expose a small workflow: **Start Engine View**, **Refresh**, **Follow editor camera**, **Capture** and **Stop**.
Show whether the view is **Current**, **Updating**, **Needs bake**, **Needs reload** or **Stopped**. Keep detailed
revision, input and protocol identities in an inspectable diagnostic report. Unsupported edits must visibly
require a refresh; an old image must not appear current.

## Existing foundations and the missing connection

| Foundation | Current behavior | What this proposal adds |
| --- | --- | --- |
| [Save JKL & Test](game_testing.md) | Validates and exports an immutable snapshot, stages an owned test directory, checks for intervening edits and launches a configured engine. | Reuse the export and launch guards for a preview session. |
| [Tools > Engine Capture](../tools/engine-capture/README.md) | Accepts an explicit posed-capture request or a supported ordinary-control gameplay recipe, then inspects receipts and hash-checked frames. It does not implicitly export the open level. | A current-document/current-view adapter, followed by a separate live-preview protocol. |
| [Project Cockpit playtest](../src/app/src/ui/project/project_playtest.cpp) | Stages an open level as `live.jkl`, packages it, records identities and checks the editor revision before launch and receipt review. | Reuse its ownership and stale-result checks instead of inventing a second unguarded exporter. |
| [Saved viewpoints](saved_cameras.md) | Stores RED camera and workspace state. | Tested conversion to a native inspection camera, including sector and horizontal field of view. |
| [Accepted lighting publication](lighting.md#worker-coalescing-and-publication-contract) | Authored light edits can produce persisted bake output after the original edit. | Publish the accepted exported lighting values to an eligible running world. |

The existing posed camera hook and the persistent ordinary-input hook are different interfaces:

- [`sed-engine-capture-v1`](https://github.com/CodaLilitari/OpenJKDF2-MotS-Fixes/blob/74e4657c4f1751d09af9ad9e056acb8f41997acb/src/Main/stdCapture.c)
  applies a specified inspection camera, captures a composed frame and exits. It supplies the camera/capture
  mechanism, not a long-lived edit session.
- [`openjkdf2-agent-v1`](https://github.com/CodaLilitari/OpenJKDF2-MotS-Fixes/blob/766d6def4b275499c1941fc97b5dd487cae7725d/docs/AGENT_WORKSPACE.md)
  runs an opted-in Linux single-player engine with a private request/response mailbox. Its
  [command table](https://github.com/CodaLilitari/OpenJKDF2-MotS-Fixes/blob/766d6def4b275499c1941fc97b5dd487cae7725d/src/Main/stdAgentProtocol.c)
  supports ordinary input, observed state, current-view capture and shutdown. It has no camera-placement or
  world-edit command. Existing posed and agent modes are mutually exclusive.

These sources show reusable mechanisms, not a single released binary with the proposed combined capabilities.
RED and its engine companion need paired changes and explicit capability discovery. Existing gameplay recipes
must retain their ordinary-input meaning; do not quietly add authoring commands to the testing allowlist.

A previous [revision-preview-check proposal](https://github.com/CodaLilitari/RED/pull/11) checks whether a packaged
capture still matches exported input hashes. Freshness of that package does not synchronize unsaved editor
changes into a running world.

## Architecture and alternatives

Repeated posed captures are a useful first delivery: they reuse the current runner, isolate failures and can
show the current exported document. Each run reloads the world and ends. Label this **capture/refresh**, without
claiming continuous simulation or hot reload.

A persistent companion adds a new local protocol around native world ownership. RED remains authoritative for
the authored document and history. The engine owns its loaded world, scripts, simulation and graphics context.
Portable export identities and typed change values belong below the Qt boundary; process lifecycle, camera
conversion and view presentation belong in the app. Engine mutations belong in the engine, never in a Qt worker
or a shared pointer into engine memory.

Embedding only an OpenJKDF2 renderer would require ongoing integration of its resource, material, palette,
lighting and renderer behavior. It would not by itself run the world or COGs. A separate process reuses those
native systems and confines an engine crash. Review the upstream and third-party licensing and packaging before
distributing a companion. Keep user-supplied stock assets and private maps outside the repository and binaries.

### Milestone A: capture the current editor view

1. Capture an immutable document, resource configuration and selected viewpoint. Use ordinary export preflight,
   resource staging and the Project Cockpit revision checks. Do not save over the source or alter its dirty state,
   Undo history, selection or recovery state.
2. Export into a new owned stage. Retain document revision, export and package hashes, ordered resource identities,
   target mode, engine identity and capture settings. Preserve existing resource-closure limits: successful
   packaging is not proof that every runtime COG reference resolves.
3. Convert the camera against that exported snapshot and run the existing posed-capture route. Check the same
   revision again before launch. Track a separate ephemeral view/request generation for pose, projection, image
   size and device-pixel ratio: moving the editor camera does not change the Level Undo history. A late result can
   be retained as evidence, but must be marked stale if the document or followed view has advanced. An explicit
   fixed-pose capture can retain that pose without pretending it shows the newly moved editor camera.
4. Display only the frame whose receipt, path, dimensions and hash pass the existing runner checks. Keep the
   original receipt and qualification limits. Failed or refused capture must not reuse a previous successful frame
   as if it came from the new revision.

An optional automatic refresh can coalesce committed edits and accepted bake publications, with one run in flight
and one latest pending revision. Do not re-export or hash the complete resource set on every mouse-move event.
An intermediate uncommitted gizmo preview is not an exported document revision.

#### Camera fidelity

RED's orbit angles are not native engine angles. Convert the actual view basis, including roll, to the engine's
convention and verify handedness. Use the real image aspect ratio, horizontal field of view and device-pixel
dimensions. Resolve the containing sector from the exported geometry and its mapping; never substitute sector
zero. Refuse or request a valid pose when a point is outside or ambiguously on the world boundary. An orthographic
editor view needs an explicit perspective inspection choice. Editor overlays and viewport clipping controls are
not world edits.

Test with a synthetic asymmetric room: visibly distinct axes, non-collinear markers at different depths and a
known sector boundary. Exercise yaw, pitch, roll, aspect ratio, field of view and high-DPI sizing. A matching request
file alone does not prove the native camera was observed. Keep requested and observed camera evidence distinct.

#### Capture compatibility

For A, select an explicitly configured compatible engine and known profile, then validate the run and its receipt;
legacy posed-capture v1 has no capability handshake. B introduces negotiation with the owned companion after launch
and before accepting live commands. Reject unsupported requests before expensive work where the interface allows.
The current
[SDL offscreen posed-capture contract](engine_capture_offscreen_plan.md) requires an observed graphics report;
older capture builds can write an image without supplying that report. Do not synthesize missing observations
from environment variables or silently weaken the gate. Use a qualified companion or retain the failed/unverified
status. A new frontend does not turn existing `effectiveCameraObserved: false`,
`runtimeResourceResolutionObserved: false` or unverified acceptance into positive observations.

### Milestone B: one persistent edit, end to end

The first live proof should change the material assigned to one static, non-adjoined world surface with valid UVs.
Use two opaque, same-sized, single-cel textured synthetic materials already resident in the staged world and visible
elsewhere in the fixture. Require a compatible default-or-zero surface cel and no active surface or material
animation. This isolates the assignment from new asset loading, transparency, UV dimension changes, cel selection
and material-cache replacement. Check eligibility before mutation: the native renderer can use the old surface
`wallCel` to index the replacement material, so equal image dimensions alone do not make a swap safe.
The admission rule is stricter than equal image dimensions: compare the candidate export and require identical
wire UVs, layout and other exported fields apart from the mapped material assignment. A different-size resident
material can change exported pixel UVs from the same normalized RED coordinates. Include a nonzero-UV fixture
where that change refuses the single-field update and requires refresh; compare it with a fresh export.

Start one engine, establish the preview camera and record its world identity. Commit the material change in RED,
then Undo and Redo. Capture each acknowledged state. The same process and loaded world must remain active; an
independent timer or moving fixture should demonstrate advancing simulation across the updates. Compare the target
surface with clean launches of the corresponding exported revisions. Merely receiving an acknowledgement is not
visual acceptance.

#### Proposed protocol, not an existing interface

Use a distinct preview protocol with explicit capabilities. Negotiate session nonce, executable identity, target
mode, protocol version, update families and resource budgets. A filename, engine version string or successful
process start is insufficient to establish support.

Every update is bound to:

- The owned session, world generation and export epoch.
- The loaded base package and ordered resource identities.
- A unique request ID, last acknowledged preview revision and desired authored revision.
- Export-mapped target identities and expected old values for every changed field.

Keep the base package hash separate from the in-memory revision and acknowledged delta history. Do not claim the
original archive hash identifies an edited runtime world. Record enough applied changes to reproduce a captured
revision from its base; otherwise describe the record as incomplete.

Surface and sector numbers are local to an export. The JKL writer also resolves material slots. Retain an exact
mapping from the exported document to the loaded world and resolve resources by their staged identities, not by
assuming editor and runtime indices match. An exporter change, topology change, ambiguous mapping or changed
resource order invalidates the epoch. Full persistent editor IDs are useful future work, but a bounded preview
session must work safely without pretending ID-01 is already implemented.

Parse and validate commands away from live world mutation. Queue a complete typed batch for the engine-owned
boundary after simulation work and before rendering. Validate session, generation, identity, ranges, expected old
values and the entire operation before mutation. An invalid batch changes nothing. World unload invalidates the
queue and all previous mappings.

Distinguish **received**, **applied** and **rendered** acknowledgements. A rendered result identifies the first
composed frame containing that revision, its camera and rendering configuration. Native-window swap submission
and later RED-dock display are separate observations. The existing pre-swap capture hook proves neither one;
offscreen readback is not a user-visible presentation. Record successful swap submission where observed, without
claiming physical screen display. For D, acknowledge the received frame actually displayed by the RED UI separately.
An accepted command is not yet an image showing its result. Engine-owned observation is required for new claims of
camera or applied-state verification. Correlate camera updates with their own view generation so an older camera
frame cannot satisfy a newer Follow editor camera request.

On timeout, application state is unknown until queried. Use idempotent absolute-state updates and request IDs;
never blindly replay relative edits. Keep one batch in flight and one latest desired state. Coalescing must diff
against the last acknowledged state, including rapid Edit/Undo/Redo, rather than discard intermediate operations
that affect the final value. If a full refresh becomes necessary, supersede queued changes and start a new epoch.

Proposed initial admission limits are 1 MiB per change request/response (excluding separately bounded frames),
at most 8,192 scalar or surface-corner
records per atomic batch, and preview frames no larger than 2,048 pixels on either side or 2,097,152 pixels in total.
These are implementation starting points, not measured engine limits. Negotiate smaller supported limits. If a
lighting change exceeds a whole-batch limit, fall back to a full refresh; do not publish partial lighting by
splitting it into unrelated visible updates. Bound logs, diagnostics and frame queues separately. Keep only the
newest display frame; retain explicit captures independently.

The existing private file mailbox is a reasonable Linux prototype. A named pipe or local socket may later reduce
latency. Use an owned endpoint and session binding; do not add a general TCP listener, script evaluation or raw
memory-write interface. RED may terminate only its owned child processes. Keep multiplayer and unrelated game
sessions outside this authoring mode.

### Milestone C: native lighting feedback

Authored RED lights feed a bake. Native world rendering consumes exported surface-corner intensities, sector
lighting and surface extra light, while runtime Thing lights use a different path. Moving an editor lamp cannot
be represented faithfully by silently moving a runtime dynamic light instead. See the pinned native
[surface loader](https://github.com/CodaLilitari/OpenJKDF2-MotS-Fixes/blob/74e4657c4f1751d09af9ad9e056acb8f41997acb/src/World/sithSurface.c)
and [renderer](https://github.com/CodaLilitari/OpenJKDF2-MotS-Fixes/blob/74e4657c4f1751d09af9ad9e056acb8f41997acb/src/Engine/sithRender.c).

Send bake output only after RED accepts its worker generation and input fingerprint. A bake can complete after a
light edit and be folded into that edit's Undo interaction. Watching only Undo depth would miss this publication.
The preview needs a notification for each accepted persisted output revision, including an automatic bake that
does not add an Undo entry. The recovery serial has recovery-specific activation conditions and is not an assumed
universal change bus.

For an unchanged topology and compatible channel layout, a later engine capability can accept the exact exported
corner and sector values atomically. Preserve JK scalar and MotS color semantics and native allocation layouts.
Validate the full batch and invalidate any affected render caches before reporting the rendered revision.
Layout changes, unsupported caches and over-budget batches use a full refresh. Compare with a clean load of the
same exported bake before claiming equivalence.

Show **Needs bake** while the authored lighting and accepted bake differ, and **Updating** after a valid publication
is sent. Continuous feedback can debounce committed drag interactions; it must report the bake and transfer delay.
Never label an old native frame as the result of a pending light edit. Cover accepted automatic publication,
cancelled/stale workers, Undo, Redo and a second edit arriving while the first bake is in flight.

#### Initial update boundary

| Change | Proposed handling |
| --- | --- |
| Detached inspection camera | Live only after a new camera capability is qualified; existing ordinary-input agent mode cannot place it. |
| Static surface assignment to a resident material under the first fixture's constraints | First live-edit proof. Broader material/UV behavior needs its own coverage. |
| Sector scalar light or surface extra light | Candidate next capability, with native range and runtime-ownership checks. |
| Accepted baked corners, unchanged topology and channel layout | Later atomic lighting capability; compare against a full reload. |
| Runtime Thing-light parameters | Separate later capability; distinguish from authored bake lights. |
| UV changes, static Thing transforms, model/architecture edits | Require separate identity, bounds, attachment and cache qualification; refresh until supported. |
| Vertex/topology edits, adjoins, collision, Thing creation/deletion | Full staged refresh initially. |
| COG/script changes, new or replaced MAT/3DO/KEY/sound resources | Full staged refresh initially. |

COGs can change light or material-related state during simulation. Do not overwrite an independently changed
runtime field without detecting the conflict. Expected-value checks should reject a conflicting batch and report
the runtime override; a deterministic inspection fixture or explicit restart can resolve it. An applied/rendered
acknowledgement describes a frame, not an indefinite pin against future script changes. Any future override mode
needs its own visible ownership and release rules.

## Simulation, refresh and presentation

A detached inspection camera must not teleport the player or implicitly trigger gameplay. Keep authoring camera
control separate from optional explicit **Play** input focus. This requires engine work beyond reusing the posed
camera override: native rendering marks sectors seen, sends sector/Thing `SIGHTED` messages, wakes sleeping AI and
stamps Thing visibility used by simulation in applicable builds. Isolate the inspection pass from those side effects
while preserving the ordinary player camera's visibility behavior. Test a remote scripted sector, a sleeping actor and visibility
timestamps against a matched player-camera control run; reject detached-camera qualification until that holds.
Global noclip is not a substitute for isolation. This requirement does not retroactively qualify existing posed
captures as free of simulation side effects.

Let simulation continue by default. Pause or step
requires a negotiated engine capability that handles its clocks and scripts; freezing the displayed image is not
pausing the world.

For unsupported changes, prepare and validate a new owned stage. Keep the old view visibly stale until the new
world and requested camera are acknowledged. The initial implementation may restart the engine; this resets game
state and must be shown as a refresh, not seamless hot reload. Restore a camera only if it remains valid in the
new world. If it no longer fits, request a valid pose instead of silently teleporting to an arbitrary sector.

The first persistent delivery can use a dedicated native window. A later RED dock should consume a bounded newest
frame stream. Measure GPU readback, transport, image conversion and Qt presentation separately before choosing
shared memory or another transport. Keep graphics-context ownership in the engine and UI updates on the Qt thread.
Do not reparent an unrelated game window or steal keyboard focus while the user edits. Closing a document,
cancelling a launch, an engine crash and editor shutdown must clean up owned resources and leave the document and
its recovery state intact.

Measure latency from committed RED revision to the correlated rendered frame, native swap submission and RED-dock
display as separate available endpoints, with bake, export, staging, apply and image-delivery time reported
separately. Label the endpoint and do not report render/readback timing as edit-to-present latency. Report median
and p95 with the fixture and executable identity. Engine FPS alone does not measure preview responsiveness. This
proposal makes no achieved latency claim.

## Delivery slices and acceptance

| Slice | Deliverable | Required evidence |
| --- | --- | --- |
| A | Capture current editor view through the existing runner | Camera fixture, exact staged inputs, stale-result rejection, preserved editor state and existing receipt validation. |
| B | Persistent native window, detached camera and one resident surface-material edit | Same PID/world, advancing independent simulation, isolated inspection visibility, material/cel eligibility, Edit/Undo/Redo images, clean-reload comparison and protocol failure tests. |
| C | Accepted bake publication and qualified lighting fields | Native channel/layout coverage, atomic publication, stale-bake rejection, runtime conflicts and clean-reload comparisons. |
| D | Embedded Engine View and measured responsiveness | Bounded frame transport, correct focus/lifecycle behavior, crash recovery and separately measured render/submission/display latency. |

Start with one explicitly qualified Linux companion/profile to prove the protocol, then implement and test the
Windows endpoint, process ownership, capture and GUI integration. Existing Windows posed capture does not imply
that the Linux ordinary-input agent protocol or the new preview works there. Do not advertise Windows live preview
before those gates pass. macOS remains unsupported until it has a qualified engine profile. Retail JK/MotS keep
their existing launch and acceptance paths; an OpenJKDF2 preview is evidence for its exact build and configuration.

Use synthetic owned fixtures in the repository. The test suite needs pure export/camera mapping coverage, app
publication/lifecycle tests and protocol tests for stale sessions, reordered replies, dropped acknowledgements,
duplicate IDs, rejected old values, size limits, engine exit and document close. Exercise independent unsaved
edits and recovery with preview disabled, active, failed and stopped, and camera-only movement while a capture
is in flight. A small engine stub can test failures, but
every claimed native update family also needs a real-engine visual fixture and clean-load reference. Production
app changes retain the repository's complete Release and Windows/Qt qualification requirements.

The first implementation review should settle the export-mapping representation, local endpoint and exact first
capability. This proposal recommends the constrained resident-material proof before broader lighting or topology
updates. Complete A independently if the paired engine work is not yet available.

## Source and implementation map

- [Capture menu integration](../src/app/src/main_window/menus/menu_actions.cpp) and
  [capture runner](../tools/engine-capture/invoke_engine_capture.py).
- [Ordinary-input controller](../tools/engine-capture/gameplay_capture.py) and
  [current capture contracts](../tools/engine-capture/README.md).
- [Project playtest staging](../src/app/src/ui/project/project_playtest.cpp) and
  [editor revision adapter](../src/app/src/main_window/resources/project_family.cpp).
- [JKL export and material-slot resolution](../src/core/src/assets/jkl.cpp).
- [Lighting publication](../src/app/src/main_window/light_bake.cpp) and
  [recovery change handling](../src/app/src/main_window/document/recovery.cpp).
- [Saved editor camera contract](saved_cameras.md), [lighting contract](lighting.md) and
  [document history](../src/core/include/sed/editor/document.hpp).

These source links explain current mechanisms. The protocol, UI states, update limits and delivery slices above
remain proposed until implemented and qualified in paired changes.
