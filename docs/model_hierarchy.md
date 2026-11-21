# 3DO hierarchy and KEY authoring

A 3DO's hierarchy is a tree of nodes. Each node can show a mesh, and has a parent and a position, rotation and pivot
relative to that parent. A KEY animates the tree: each track names a hierarchy node by its index (the engine's joint
number) and gives its position and rotation over time.

RED edits the hierarchy, starts a KEY from a model, recomputes a KEY's deltas, and checks a KEY against the model it
animates. For existing UV rows and numeric whole-mesh transforms use [Model Studio](native_model_studio.md). Model import and
Blender with the Blender-Sith add-on support broader geometry and material workflows.

- In the editor: **Tools > 3DO Hierarchy…**. Open a 3DO, select a node in the tree, and change its name, parent,
  position, rotation or pivot with **Apply to node**. **Add child…** and **Remove node** change the tree, and **Save
  as…** writes a new 3DO. **New KEY…** starts a KEY for the model. **Check KEY…** checks one against it, and offers
  to save a copy with recomputed deltas when they are stale. Entries are added in the KEY editor.
- From the agent: `model-hierarchy`, `model-hierarchy-edit`, `key-from-model`, `key-plan` and `key-apply` with
  `computeDeltas`, and `key-inspect <key> <model>`. MCP: `sed_model_hierarchy`, `sed_model_hierarchy_edit`,
  `sed_key_from_model`, `sed_key_inspect`, `sed_key_plan`, `sed_key_apply`.

## The hierarchy

The model is read without applying the hierarchy, so it saves back as it was apart from the edit. The saver rebuilds
each node's child and sibling links from the parents.

| Edit | Refused when |
|---|---|
| rename | the name is empty, longer than 31 characters, or has spaces |
| reparent | the node is the root, the new parent doesn't exist, or the move would put a node under its own child |
| set position, rotation, pivot | a value isn't finite or is a million or more |
| add a node | the parent or mesh doesn't exist, or the name is unusable |
| remove a node | it is the root or has children; the nodes after it are renumbered |

`model-hierarchy` and the dialog report what the engine would trip over:

- **Errors:** a node whose parent isn't another node; a cycle; a mesh number the model doesn't have; a root other
  than node 0; an unusable name or transform.
- **Warnings:** two nodes with the same name, since a KEY that names one is ambiguous (`$$$dummy` helpers are
  exempt, as retail repeats them); a mesh no node shows, which the engine never draws.

A model with errors isn't saved.

## KEY deltas

Each KEY entry has a position and rotation, and deltas: the change per frame until the next entry. The engine
applies a delta only when the entry's flags allow it (1 for position, 2 for orientation). An entry added without
deltas holds still until the next one and then jumps. Recomputing fills them in.

- **Between entries:** the delta is the change to the next entry divided by the frames between them. This is the
  Blender-Sith exporter's rule.
- **Angles** turn the short way round: 343° to 3° is +20°, not −340°. Retail KEYs do this.
- **The last entry** of a track holds still when it is at the end of the animation. With frames left, it turns back
  toward the first entry over those frames, as a loop does.
- **Flags** are set for each kind of change that is not zero.

The check was made against Kyle's model and 60 of his stock KEYs. All 5,417 retail segments between entries match
the rule. Every last entry either holds or loops back. The check accepts both, so stock KEYs raise no stale-delta
warnings.

## Checking a KEY against its model

`key-inspect <key> <model>` (its `modelIssues`) and **Check KEY…** report:

- a track for a joint the model doesn't have (two stock Kyle KEYs have one, a 19th joint on an 18-node model);
- the same joint animated twice;
- a track whose name differs from the model's node, which usually means a KEY made for another model;
- an entry after the KEY's last frame;
- fewer declared joints than the model has nodes;
- deltas that don't lead to the next entry.

## Export scope

When opening a model, the hierarchy report lists geosets or unmodeled source sections that a general hierarchy export would omit. The writer refuses these inputs before opening its destination. Canonical export normalizes numeric text, rebuilds child/sibling links from the parent map, and omits comments. The [cross-reader fixture](../tests/fixtures/relay-sentry/README.md) includes the original files and reproducible canonical-output recipe. The source-preserving lighting editor has its own all-geoset byte-preservation checks.

## Play a KEY on the current hierarchy

Choose **Preview KEY…** after opening a 3DO. Scrub a fractional frame, jump to a numbered marker, or play once or in a loop at the KEY's declared FPS. Both wireframes share the frame, scale and view direction. The left pane uses LOD 0. Pick another geoset for the right pane to compare the geometry around a wrist, foot or seam. Choose the top or side view when the joint moves along the front view's depth axis.

Opening the preview, changing the comparison LOD or pressing **Reframe** fits the displayed geometry of both panes
at the current pose. Playback and scrubbing keep that shared frame fixed; an animated piece can leave it, and
**Reframe** brings the current pose back into view. Framing uses transformed face vertices from every displayed
joint instance, including multiple joints that reference the same source mesh. It does not depend on LOD 0's radius
being large enough for the comparison LOD. Changing the view direction retains the same model-space frame.

Select a joint to inspect its model-space origin (circle), mesh pivot (cross), local rotation, LOD 0's authored mesh radius and derived model radius. The authored radius is the source MESH RADIUS; the derived model radius can include reader expansion and is not the original source radius or the preview's framing radius. An untracked joint keeps its rest transform and follows its animated parent. The preview uses the selected hierarchy's current edits. A mismatched track name or unusable joint reference produces a refusal with the joint in question. Use **Inspect PUP association…** to see whether a specific puppet file lists this KEY. File references alone do not establish hierarchy compatibility.

Playback displays one KEY at full weight. In-game track blending, character physics, first-person camera placement and object attachment policy can change the rendered result. Mesh geometry, UV and material editing are outside the hierarchy tool.

### Unsaved model snapshot adapter

The application pose-preview factory also accepts an owning `NativeModelDocument` and a captured `KeyFile`.
[Model Studio](native_model_studio.md) uses this overload to play the displayed unsaved model. Both LOD panes read
the same immutable all-geoset document. Changing LOD cannot reload the original model
or discard unsaved geometry or UV rows. Caller document and KEY lifetimes do not limit the preview lifetime.
The optional KEY path identifies its title and PUP name matching only; the snapshot overload does not open it.

Geometry remains in mesh-local coordinates until each sampled joint's `meshToModel` is applied once for display.
The separate hierarchy overload continues to copy the supplied unsaved hierarchy and load comparison geometry
from its model path. Neither preview publishes a model or edits the open Level.

The renderer is a wireframe. Unsaved UV rows remain part of the captured document; these previews establish
geometry, pose and LOD behavior, not textured UV appearance or target-engine acceptance. Model Studio shows orphan source
meshes in its separate static view and can publish reviewed variants. Model recovery remains planned.
