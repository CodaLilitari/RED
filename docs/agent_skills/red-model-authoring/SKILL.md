---
name: red-model-authoring
description: Build, edit and check Jedi Knight and MotS 3DO models and KEY animations with RED. Use for native UV rows, mesh scale/translation, Model Studio inspection, import, lighting, hierarchy edits, KEY playback, delta recompute and target-game checks.
---

# 3DO models and KEY animations with RED

Use RED for model import, lighting, native source-row edits, hierarchy and KEY authoring/playback. Use Blender with
Blender-Sith for broader deformation, topology, unwrapping and animation-curve authoring. Check the installed
add-on's Blender compatibility instead of assuming a version. For the whole pipeline
(generation, Blender, installation checks), use Coda Lilitari's JK/MotS Model Workflow and its
`jk-mots-model-workflow` skill when you have it:
https://richard-pemberton-research.darkahn.chatgpt.site/projects/jk-mots-model-workflow/index.html. A copy of the
workflow and its references is in RED's repository under `docs/knowledge/models/`. Bring the result to RED to fit it
to the engine and check it.

## Models

- **Native edits:** inspect with `model-native-inspect`, then review `model-native-plan` and publish with
  `model-native-apply` using both request and output hashes. These commands support admitted classic 3DO 2.1
  existing UV rows and positive uniform whole-mesh scale/translation. Use source geoset/mesh/UV ordinals; position
  and UV indices are independent. A shared row changes every referencing corner. UVs are absolute texture pixels,
  including negative/tiled values. Preserve all other LODs, hierarchy, normals and authored radii; review radius and
  lighting consequences after a transform. Read [native authoring](../../native_model_authoring.md) before issuing a request.
- **GUI inspection:** use **Tools > Model Studio…** for the same edit families, immutable previews, local Undo/Redo,
  unsaved KEY comparison and cumulative reviewed variants. In **UVs**, select a material, load its MAT/CMP and inspect
  source face/corner addresses. The bounded unlit cel-zero/mip-zero swatch is not a runtime renderer. These are current
  source capabilities; inspect an installed binary's schema/help before assuming they exist in an older release.
- **Import:** `red-agent model-import <request.json> <new.3do>` turns OBJ or GLB into a 3DO. It refuses a mesh over
  the loader's limits (512 vertices, 768 texture vertices, 512 faces), and flags any model over 512 faces in all.
  `red-agent schema model-import` gives the request.
- **Lighting:** `model-lighting-inspect`, then `model-lighting-plan` and `model-lighting-apply` with the planned
  hash.
- **Materials:** `mat-create` from PNG or JPEG cels, `mat-inspect`, `mat-export-png`; `cmp-create` and
  `cmp-master-create` for colormaps. The 8-bit software renderer uses indexed MATs and the 16-bit Direct3D path
  uses RGB565 MATs: check the one you made in the renderer it's for.
- **Before reducing a mesh:** split triangles at rigid-part boundaries, keep the geometry that caps joints, and bake
  UV atlases with complete mip chains.
- **Carried weapons:** with a stock weapon, the right-hand mesh must be empty or match the animation contract.

## The hierarchy

`red-agent model-hierarchy <model.3do>` lists the node tree (name, parent, mesh, position, rotation, pivot) and any
problems: bad parents, cycles, missing meshes, a root other than node 0, duplicate names. To change it:

```json
{"schemaVersion":1, "inputSha256":"…", "operations":[
  {"op":"rename", "node":3, "name":"k_rhand"},
  {"op":"reparent", "node":4, "parent":3},
  {"op":"setTransform", "node":4, "position":[0,0.02,0], "rotation":[0,0,0], "pivot":[0,0,0]},
  {"op":"addNode", "parent":3, "mesh":-1, "name":"saberpoint"},
  {"op":"removeNode", "node":7}]}
```

`model-hierarchy-edit <model.3do> <request.json> <new.3do>` applies the operations in order, or refuses and writes
nothing. Node 0 stays the root; removing a node renumbers the ones after it.

## KEY animations

1. `red-agent key-from-model <model.3do> <new.key> [frames] [fps]` starts a KEY with a track per node at its rest
   pose. Track numbers are joint numbers, so they follow the model's node order.
2. Add entries with `key-plan`/`key-apply` and `{"op":"insertEntry", "node":n, "entry":{"frame":…, "position":[…],
   "rotation":[…]}}`.
3. Finish with `{"op":"computeDeltas"}`. Entries hold still until their deltas are set; recomputing gives each entry
   the change per frame to the next one. Angles turn the short way round, and the last entry holds or loops back, as
   retail KEYs do.
4. Inspect `modelIssues` with `red-agent key-inspect <key> <model.3do>`. Compare a stock control when an existing
   clip reports a warning. Distinguish an untracked meshless helper from a numbered track now targeting a different joint.

When adding detail to an existing rig, keep the whole node set its KEYs were made for: every KEY the game can reach
must fit the hierarchy, not only the ones you tested.

For short multiplayer skins, trace one complete limb through the numbered animation joints and its meshless
pivots. Check stock run and fall, every reachable KEY, planted-foot motion and the active player sphere separately.
The [small-player example](../../model_hierarchy.md#small-players-with-stock-animations) includes reproducible
RED checks on SMLiberator’s Ugnaught and the slot-18 conflict to look for.

Over MCP: `sed_model_hierarchy`, `sed_model_hierarchy_edit`, `sed_key_from_model`, `sed_key_inspect`,
`sed_key_plan`, `sed_key_apply`; native authoring uses `sed_model_native_inspect`, `sed_model_native_plan` and
`sed_model_native_apply`.

## Check it in the game

Use **3DO Hierarchy > Preview KEY…** or **Model Studio > Preview current model with KEY** to inspect sampled poses,
markers and LOD differences. The wireframes establish editor geometry and pose. A parser or editor preview alone
does not prove the asset renders, animates or collides correctly in the target game.

- Record the executable and its hash; JK 1.00, 1.01 and patched builds are different inputs.
- Test a stock control first (stock Kyle or a known creature) in the same executable and settings. If it fails, fix
  the setup before blaming the model.
- Start with indexed MATs in the software renderer, then RGB565 in 16-bit with real 3D acceleration.
- Reach the model the normal way. `SetThingModel` skips the route the game uses to choose models; a read-only
  `GetThingModel` can confirm the normal choice.
- Watch the animations in game: idle, walk, run, attacks, death.
