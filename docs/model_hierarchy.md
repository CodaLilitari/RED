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

Parent-link validation walks the hierarchy iteratively in linear time. A cycle error identifies each node
whose parent chain returns to that node.

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

- a track for a joint the model lacks (several stock Kyle KEYs address a nineteenth joint on his 18-node model);
- the same joint animated twice;
- a track whose name differs from the model's node, which usually means a KEY made for another model;
- an entry after the KEY's last frame;
- fewer declared joints than the model has nodes;
- deltas that don't lead to the next entry.

## Small players with stock animations

A *ghost node* is a meshless joint. In SMLiberator’s unfinished JK1 NPC Pack, Max, Pic and the Ugnaught keep
Kyle’s numbered slots 0–17 and add sixteen pivots. In **Tools > 3DO Hierarchy…**, the Ugnaught’s left leg runs from
`K_LCALF` (3) → `PIVOT_LFOOT` (32, mesh −1) → `K_LFOOT` (4). The middle node carries a fixed offset.
When the stock animation bends the leg, the foot inherits that rotation at the Ugnaught’s ankle. The pack contains
43 models and supplies no KEY, PUP or COG files. [SMLiberator’s original pack and explanation on Massassi](https://discord.com/channels/881034839401328680/881749278811377695/1575533495005220946)

**Check the joint numbers a clip actually drives.** With your own copies of the Ugnaught and retail Jedi Knight
resources, run the three read-only checks below.

```sh
red-agent model-hierarchy kyug.3do
red-agent key-inspect kyrun1.key kyug.3do
red-agent key-inspect kyfall.key kyug.3do
```

On these inputs, RED reports 34 hierarchy nodes. The run clip declares 18 joints (`jointsShort`).
The fall clip’s track 18 names `Object01`, while the model uses slot 18 for
`PIVOT_TORSO` (`nameMismatch`). The stock `kyidle1b`, `kyidle2b` and `kysbsabr` KEYs also address slot 18.
A parent of the torso mesh in that slot can affect every visible part beneath it. A shorter declared allocation
and a track targeting a different joint are distinct findings. RED’s pose preview refuses the latter. Inspect
every KEY reachable from the target puppet and scripts, including any mod’s extra stances, before assigning
a helper index. A control with stock Kyle identifies diagnostics already present in the source motion.

**Check grounding separately from collision.** A fixed pivot can change limb length. A walking foot also has to
stay planted as the player moves. Compare a full stride, reverse crouch, a swim and a weapon grip at the same
frames as Kyle, then test the movement and transitions in the target executable.

Biddy and Lilivane’s R2 rigs supplement fixed offsets with character-specific animation tracks. A pose comparison
sampled eight grounded stock clips at half-frame intervals (1,072 poses per character). On the existing R2 rigs,
the best single fixed translation still left a worst-case difference from Kyle’s sole height of 0.018566 game
units for Biddy and 0.016695 for Lilivane. A differently fitted static rig needs its own measurements.

In original multiplayer, the released R2 characters retained Kyle’s hit radius of 0.065 game units and center
height of 0.120114 above level ground. SMLiberator’s Max, Pic and Ugnaught use different `INSERT OFFSET` heights
(0.072, 0.1045 and 0.101 respectively). Copying one of those heights into another skin calls for a fresh floor and
hit-center test. Compare the active player template’s `size` and `movesize` and test the actual sphere
with shots just inside and outside its edge in the same multiplayer level. A 3DO’s mesh radius describes drawing
bounds. RED’s [model face comparison](model_collision_compare.md) checks geometry for collision replicas.

**Original-game follow-up, November 26, 2026.** With raw stock `ky.pup`, Biddy Blas and Lilivane each matched Kyle on eleven standing projectile probes in Jedi Knight 1.01. All three reported collision type 1, hit and movement radius 0.065, standing Z 0.120114, six hits and five misses. The engine activated stock standing, hit, forward-walk and forward-crouch tracks on each avatar. Walking and crouching were exercised after scoring. Crouched collision, the slot-18 clips, per-joint motion and SBX require separate evidence. [Dated native measurements, model hashes and test limits](https://github.com/CodaLilitari/JK-MotS-Model-Workflow/blob/53dfbf06a3ece492691b56b3584191eb614ce1a0/docs/research/2026-11-26-ghost-node-retail.md)

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
