---
name: red-model-authoring
description: Build and check Jedi Knight and MotS 3DO models and KEY animations with RED - import OBJ/GLB to 3DO within engine limits, light models, edit the 3DO hierarchy, start KEYs from a model, recompute KEY deltas and check KEYs against their model. Use for any 3DO or KEY work done headlessly.
---

# 3DO models and KEY animations with RED

RED handles model import, lighting, the hierarchy and KEY timelines. Shaping meshes, UVs and animation curves is
Blender work, with the Blender-Sith add-on; for the whole pipeline (generation, Blender, installation checks), use
Coda Lilitari's JK/MotS Model Workflow and its `jk-mots-model-workflow` skill when you have it. Bring the result to
RED to fit it to the engine and check it.

## Models

- **Import:** `red-agent model-import <request.json> <new.3do>` turns OBJ or GLB into a 3DO. It refuses a mesh over
  the loader's limits (512 vertices, 768 texture vertices, 512 faces), and flags any model over 512 faces in all.
  `red-agent schema model-import` gives the request.
- **Lighting:** `model-lighting-inspect`, then `model-lighting-plan` and `model-lighting-apply` with the planned
  hash.
- **Materials:** `mat-create` from PNG or JPEG cels, `mat-inspect`, `mat-export-png`; `cmp-create` and
  `cmp-master-create` for colormaps.

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

Over MCP: `sed_model_hierarchy`, `sed_model_hierarchy_edit`, `sed_key_from_model`, `sed_key_inspect`,
`sed_key_plan`, `sed_key_apply`. See [3DO hierarchy and KEY authoring](../../model_hierarchy.md). Look at the model
in the game before calling it done; RED doesn't play animations back.
