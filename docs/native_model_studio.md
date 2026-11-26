# Model Studio

Choose **Tools > Model Studio…** to edit an admitted classic text 3DO 2.1 and save a separate variant. You can
scale and translate one authored mesh, change an existing texture vertex (UV row), and play a KEY against
the unsaved result. The UV inspector displays source corners over a captured MAT/CMP swatch. These workflows are
implemented in source. Release and target-game qualification have
separate gates in the [capability matrix](capability_matrix.md).

## Open a model and choose a target

Choose **Browse…** for a loose file, or enter a resource name and enable **Project resource** to use the project's
ordered mounts. Click **Open**. Check the displayed source identity and SHA-256, especially when several mounts
contain the same name. If a replacement fails to open, the current session, including pending text, remains available.

Select **LOD** and **Source mesh** by their zero-based ordinals. Names help identify a mesh and may repeat. Select
a **UV row** when editing texture coordinates. The static wireframe shows the selected source mesh once in its local coordinates.
An orphan mesh has zero joint instances and remains visible here. A shared mesh has one authored copy. Editing it
affects every joint that uses it at that LOD. The inspector gives the selected mesh's authored radius and XYZ
bounds. Bounds include loose vertices, while the wireframe draws face edges. The framing stays fixed across edits
to the same target so translation and scale remain visible. Use **Reframe selected mesh** to fit its displayed edges.

## Assemble or extract static levels of detail

Choose **Assemble static LODs…**. Enter two to four loose, single-geoset 3DO files in distance order. LOD 0
is nearest. Each input must have one authored mesh and the same static root, or all inputs can have an empty hierarchy. Use matching 3DO
versions (2.1 or 2.3), root names, flags, pivots, transforms and insertion offsets. Mesh names and
materials can differ. The tool identifies an incompatible LOD by its zero-based ordinal.

Choose a new output filename. The assembled model uses one shared MAT table in input order, remaps each
face's material by name, and keeps the largest input model radius. Each geoset retains its own mesh radius,
geometry, UVs, normals and lighting. Level-header settings control the distances at which the game changes LOD.

Choose **Extract static LOD…**, enter the assembled file and select its LOD number. Leave the source field
empty to use the currently open model, including committed edits. Save each desired LOD under a new filename.
The standalone model keeps the shared MAT table, radius and root hierarchy. Both commands reload the candidate
before publishing it. Existing destination files are protected.

## Preview an edit

| Operation | What to enter | What to inspect |
| --- | --- | --- |
| **Edit insertion offset** | The absolute X, Y and Z values for the 3DO's shared `INSERT OFFSET` header, in JK units. | Preview shows the selected mesh relative to an unrotated, zero-offset marker at the cross. The readout gives the proposed coordinates. **Apply** makes one undoable model edit across all LODs. |
| **Scale and translate mesh** | A strictly positive uniform scale and X, Y, Z translation. Scale acts about the mesh-local origin, then translation moves the result. Use scale 1 for translation alone. | The transform applies to every position on the source mesh, including loose vertices. Check the new bounds against the retained radius and review stored lighting and external ARCH bindings. |
| **Edit existing UV row** | The row's absolute U and V in native texture pixels. Negative and tiled values are allowed. | Check the stored float pair and the number of referencing face corners in this source mesh. All those corners use the edited row across its instances. |

Choose **Preview**, inspect the result, then **Apply** or **Cancel edit**. You can change the values and preview
again before applying. For example, translations of +1 and then +2 place the mesh at +2 from the committed baseline.
The addressed LOD, mesh, UV row and operation stay fixed throughout the edit. Changing any input revokes Apply
immediately. An incomplete exponent such as `1e` stays visible for correction, and an earlier valid preview cannot
be committed. **Cancel edit** restores the exact baseline, including when a calculation is in flight.

An effective Apply adds one local history entry. **Undo model edit** and **Redo model edit** restore exact source
bytes and the associated selection. Cancel and a byte-identical Apply preserve an existing Redo branch. While a text
field has focus, the platform's Undo/Redo keys edit the text. After leaving the field, the model commands act on an
idle session. The open Level retains its own selection, history and dirty state.

## Inspect UVs over a material

Open the **UVs** tab and choose a **Material slot**. The list shows each material used by the selected source mesh,
with its original slot ordinal and authored name. Select a **UV row** in the existing numeric controls. The canvas
outlines that material's source faces and marks the selected row. The table lists its exact source face/corner
addresses; the summary also counts uses across other materials. A separate UV row at the same coordinates stays
separate. An unused selected row is shown as an isolated point.

Choose **Load material** to capture the project resource. For a loose model without project mounts, use **Loose MAT**
to select its material explicitly. The override is for inspection and leaves the model's authored material name
unchanged. Indexed materials use the project's captured master colormap, or a **Loose CMP** of your choice. Check
the winning source and SHA-256 in the capture readout. Editing the MAT/CMP inputs clears old colors immediately;
choose **Load material** again to capture the new inputs.
Changing LOD, mesh or material clears the loose MAT override. Opening a new model also clears the loose CMP.

The background is one origin tile of cel 0, mip 0, at its real aspect ratio. U increases right and V down in native
texture pixels. Coordinates outside the tile remain unwrapped. The swatch is unlit: it does not apply CMP light
tables or simulate game rendering. A missing or invalid CMP leaves the indexed MAT's dimensions and UV outlines
available with colors explicitly unavailable. A solid-color MAT has a separate color chip and no invented texture
dimensions. Packed truecolor MATs use their stored alpha and do not need a CMP.

Preview, Cancel, Apply, Undo and Redo update this view from the same displayed model snapshot. The frame stays fixed
as coordinates change; **Reframe UV** fits the origin tile, face UVs and any selected unused row. Changing source,
LOD, mesh, material or material dimensions can reframe. Changing only the CMP preserves the frame. Material capture
runs separately from model edits, with no file reads during painting or each numerical preview. A changed project
context or a late result for an old selection cannot replace the current inspection.

Capture admits at most 16 MiB of MAT data, 1 MiB of CMP data and 4,194,304 decoded pixels. A mesh with more than
262,144 face corners shows an explicit unavailable overlay. Refusals leave numerical authoring and model history
available. See the [UV inspection contract](native_model_uv_inspection.md) for decoding and ownership details.

## Inspect an unsaved pose

Choose a KEY file, then **Preview current model with KEY**. The playback window owns the displayed model snapshot
and the captured KEY. For a prepared preview, this includes the unapplied geometry and UV rows. It compares LOD 0
with the selected LOD, draws each hierarchy instance at its sampled joint transform, and supports scrubbing, marker
jumps and playback. Both panes keep a shared camera during playback. Change the comparison LOD or use **Reframe**
to fit the current pose. Later Studio edits do not update an already open playback window. Open another preview to
inspect them. The wireframes establish geometry and pose only. UV appearance, in-game animation and collision
require separate inspection. See the [KEY playback guide](model_hierarchy.md#play-a-key-on-the-current-hierarchy).

## Save a reviewed variant

1. Apply or cancel any pending edit. Choose an unused destination with a name such as `relay_variant.3do`, then
   click **Review variant**. Studio reopens the cumulative candidate and shows the native destination, output SHA-256, affected mesh and UV ordinals,
   preserved LODs and hierarchy, and radius and lighting warnings. It covers all committed edits, including a mesh
   transform and UV edit from different interactions.
2. Choose **Cancel review** to return to editing, or **Save reviewed variant** to publish those exact bytes. Studio
   stages the candidate beside its destination, flushes it and closes the staging file. Before atomic creation it checks
   the original bytes and winning source, current project context, reviewed revision and destination. A source
   change, write failure or competing file leaves the current session and history available. For an occupied
   destination, choose another name and review again. Source checks are optimistic and do not lock out other programs.
3. Successful creation marks the exact published revision clean. Undoing a model edit afterwards leaves the variant
   on disk. The original and existing variants retain their bytes. Assign a variant to a Thing separately if required.

If the original bytes or winning resource change, another review of the existing session still refuses. To work
from the changed source, open it again and confirm **Discard** when prompted. The new session starts from that
source; re-enter any edits you want to keep. **Cancel** keeps the existing session open.

Closing or replacing a model with unsaved work gives a Save/Discard/Cancel choice. Resolve pending input before
saving. While a close decision is open, a completed staging job waits for the decision. **Cancel** returns to the
session and resumes the pending operation. **Discard** abandons unpublished work and either closes Studio or
replaces the session, depending on the requested action. A file already published
remains on disk. Model Studio has no draft recovery.

## Implementation and evidence

The [Tools action](../src/app/src/main_window/menus/menu_actions.cpp) uses the command ID `tools.model_studio`.
The [dialog](../src/app/src/ui/resources/model_studio_dialog.cpp) serializes changes to the
[portable session](native_model_session.md) on a worker. The GUI retains immutable displayed documents, allows only
the latest requested preview to follow an active calculation, and checks project context before publication.
The [variant writer](../src/app/src/ui/resources/native_model_variant.cpp) uses the cumulative source-preserving
document and a create-only publication operation. The [CLI/MCP workflow](native_model_authoring.md) continues to
review one operation family per request.

Focused checks cover [numeric edits and cumulative publication](../src/app/tests/model_studio_dialog_tests.cpp),
[keyboard and KEY interaction](../src/app/tests/model_studio_interaction_tests.cpp),
[visual UV history and two-variant publication](../src/app/tests/model_studio_uv_tests.cpp),
[bounded UV overlays](../src/app/tests/native_model_uv_preview_tests.cpp),
[material capture](../src/app/tests/model_uv_material_tests.cpp) and
[inspection lifecycle](../src/app/tests/model_uv_inspector_tests.cpp),
[close and replacement](../src/app/tests/model_studio_lifecycle_tests.cpp),
[Level isolation](../src/app/tests/model_studio_context_tests.cpp),
[selected-mesh wireframes](../src/app/tests/native_model_mesh_preview_tests.cpp) and
[publication refusals](../src/app/tests/native_model_variant_tests.cpp). The [qualification plan](native_model_studio_plan.md)
tracks the complete Release gate and native interaction, including the Windows test at 200% scale. The
[GUI readback procedure](../tests/fixtures/relay-sentry/README.md#model-studio-gui-readback) compares a Studio-published
mixed edit with the pinned independent Blender-Sith reader. The broader [mesh roadmap](native_mesh_editing_proposal.md)
keeps vertex/face picking, individual deformation, rotation, topology changes, textured 3D rendering, radius editing
and persistent model recovery open. **Tools > 3DO Hierarchy…** remains available for hierarchy and pivot edits.
