---
name: red-model-authoring
description: Build and check Jedi Knight and MotS 3DO models and KEY animations with RED: import, lighting, hierarchy edits, KEYs from a model, delta recompute and in-game checks.
---

# 3DO models and KEY animations with RED

RED handles model import, lighting, the hierarchy and KEY timelines. Shaping meshes, UVs and animation curves is
Blender work, with the Blender-Sith add-on (1.0.3 is for Blender 5.0 and later). For the whole pipeline
(generation, Blender, installation checks), use Coda Lilitari's JK/MotS Model Workflow and its
`jk-mots-model-workflow` skill when you have it:
https://richard-pemberton-research.darkahn.chatgpt.site/projects/jk-mots-model-workflow/index.html. Bring the result
to RED to fit it to the engine and check it.

## Models

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
4. `red-agent key-inspect <key> <model.3do>` checks it: its `modelIssues` should be empty. It reports joints the model
   lacks, renamed parts, entries past the end and stale deltas.

When adding detail to an existing rig, keep the whole node set its KEYs were made for: every KEY the game can reach
must fit the hierarchy, not only the ones you tested.

Over MCP: `sed_model_hierarchy`, `sed_model_hierarchy_edit`, `sed_key_from_model`, `sed_key_inspect`,
`sed_key_plan`, `sed_key_apply`.

## Check it in the game

RED doesn't play animations back, and a parser accepting a model doesn't prove it renders or plays.

- Record the executable and its hash; JK 1.00, 1.01 and patched builds are different inputs.
- Test a stock control first (stock Kyle or a known creature) in the same executable and settings. If it fails, fix
  the setup before blaming the model.
- Start with indexed MATs in the software renderer, then RGB565 in 16-bit with real 3D acceleration.
- Reach the model the normal way. `SetThingModel` skips the route the game uses to choose models; a read-only
  `GetThingModel` can confirm the normal choice.
- Watch the animations in game: idle, walk, run, attacks, death.
