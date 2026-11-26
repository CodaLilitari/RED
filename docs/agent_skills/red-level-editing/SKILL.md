---
name: red-level-editing
description: Edit, package and test Jedi Knight and MotS levels with RED's red-agent or red-mcp. Use when building or changing a JKL level headlessly or testing one in the game.
---

# Editing JK and MotS levels with RED

RED never overwrites. Every write goes to a new file, and every input that matters is bound by its SHA-256, so a
stale file is refused instead of being edited blind. Work in this loop:

1. **Look.** `red-agent inspect <level>` gives counts, the input hash and the source revision. `query` and `select`
   find sectors, surfaces, Things and COGs by what they are. `statistics` gives totals against the engine's limits.
2. **Plan.** Write a patch: `{"schemaVersion":1, "inputSha256":…, "sourceRevision":…, "operations":[…]}`. List
   the operations with `red-agent schema patch`, and get one operation's exact fields with
   `red-agent schema <operation>`. `red-agent plan <level> <patch.json>` checks it, writes nothing, and returns the
   output hash and what would change.
3. **Apply.** `red-agent apply <level> <patch.json> <new.jkl> <planned-sha256>` writes exactly what was planned, or
   refuses if anything changed since.
4. **Check.** `validate <new.jkl> --strict` (geometry, adjoins, templates, COGs, engine limits) and
   `snapshot <new.jkl> <request.json> <new.png>` to look at it. Look at the picture before calling the work done.
5. **Package.** `dependencies` lists what the level needs from which archives. `package-plan`, then `package-apply`
   with the planned hash, writes the level's GOB. `launch-plan` prepares a test run.
6. **Test in the game** (below). RED is a stand-in for the game.

## Rules that save you

- Exit codes: 0 done, 2 the request is malformed, 3 an input can't be read, 4 a hash is stale (re-inspect and
  re-plan), 5 RED refused on purpose (read the error code; don't retry the same request).
- Pass stock archives as `resourceArchives` (`[{path, sha256}]`) whenever an operation needs templates, scripts or
  materials, and `templateSource` for the master template file. Without them, headless levels can declare too
  little room for what scripts load.
- Things that move along paths need a `move=path` template. Use `setThingFrames` or `buildThingPath`, or the
  assistants (see `red-cog-scripting`).
- Agent edits go to files. They don't reach a level that is open and unsaved in the editor: save it there first, or
  reopen it after.
- Before shipping, review what the package holds: the level, its colormaps, scripts and custom strings (`.uni`).
  Stock resources are referenced, not packed.
- Over MCP, the same work is `sed_inspect`, `sed_plan`, `sed_apply`, `sed_diagnose` (validation), `sed_snapshot`,
  `sed_package_plan` and the rest. Paths must be inside the workspace or a resource root, and write tools take an
  `operationId` so a retried call is not applied twice.

## Geometry RED refuses, and how to build around it

- Levels RED wrote before 0.7.0 rounded every corner to eight decimals, so a corner a cut computed moved on save and a
  later cut through it, in a new patch, was refused as a zero-length edge. RED now writes such corners exactly. With
  an older level, or if a cut is still refused that way, build neighbouring rooms whose faces come from the same
  formula, adjoin them exactly, and prefer axis-aligned cuts.
- A batch of `cleaveSurface` cuts on one plane has been refused where the same cuts pass one patch each. Split the
  batch.
- A cut hands its new corner to every face whose edge it crosses, and the engine refuses a face of more than 24
  corners (`validate` reports `surface-corner-limit`). Split a long wall before cutting many rooms beside it.
- `deleteSectors` removes rooms; their portals become solid walls in the material you name. Use it to cut bridges
  and platforms out of a volume.
- If scripts generate the level, they are its source of truth. Once anyone edits the JKL by hand, the scripts no
  longer reproduce it: write that down.

## Shadows

JK blends light between a surface's corners, so a shadow can only fall where there are corners. Before
`bakeLighting`, run `shadowCuts` (`shadowEdges` cuts where each light's view through the openings ends; `sizeLimit`
splits surfaces near lights to `maxEdge`). It refuses over `maxNewSurfaces` and never cuts openings. After moving a
light, `removeShadowCuts`, cut again and rebake. Cuts are refused once COGs are placed, so cut before adding COGs.

## Multiplayer layout

A study of 2,400 JK multiplayer levels (Andrei's, for Lumen Cistern) found that the admired levels:

- give each player start about 17 JKU² of floor (about 10 elsewhere) and keep starts about 1.6 JKU apart;
- place half as many weapons per area, with more kinds.

For 32 players that means 32 starts, roughly 500–550 JKU² of floor, about 5 weapons per 100 JKU², and 12 or more
floor heights. These describe well-liked levels; they aren't engine rules.

Leave spare world-material slots. A joining player's model and saber load materials at runtime, and the game refuses
one once the level's declared table is full: Sith Council's host exited when a second player joined with none spare.
LucasArts' FFA maps keep 32, and `validate` warns (`multiplayer-material-headroom`) below that. Test a real
second-player join.

## Frame rate

In one big sector the engine culls nothing: it transforms, lights and clips every mesh in view, even behind walls.
Before shipping a large level, especially a conversion, read `docs/knowledge/levels/performance.md` in RED's
repository. It covers COGs that loop over every Thing, duplicate vertices, which face merges are safe, and how to
measure the frame time.

## Review a visual or architectural change

Use the [capture comparison](../../visual_capture_comparison.md) for repeatable before/after images and the
[transparency example](../../transparency_review.md) for keyed edges, overlapping faces and draw order. For a
crowded walkway, check reachability at player radius with the [clearance tool](../../walkway_clearance.md) and
walk the narrowest part in the target game.

## Test in the game

A clean `validate` and a good snapshot don't prove the level plays.

1. **Name the target** before testing: retail JK 1.01, retail MotS, or OpenJKDF2. Record the executable and its
   hash: JK 1.00, JK 1.01, a store build and a patched build are different inputs. A pass in OpenJKDF2 is not a
   pass in retail, so test the one you ship for. Keep levels retail-compatible: no engine changes.
2. **Test both renderers.** 8-bit is the software renderer (8bpp game mode, 3D acceleration off), which uses indexed
   MATs. 16-bit is the Direct3D path (16bpp, real 3D acceleration), which uses RGB565 MATs. Look at the lighting
   and colours in each.
3. **A launched game isn't a loaded level.** Get into the level, walk its routes, take the pickups, ride the lifts,
   look at the lighting. Process creation proves nothing about the mission.
4. **Check a stock control first**, such as a stock level, in the same executable and settings. If it fails, fix the
   setup before blaming the level.
5. **Multiplayer needs two clients**, in separate processes and profiles. Each must see the other move, fire, die
   and respawn. One hosted player proves nothing about sync, and a loopback test says nothing about the internet.

See RED's `docs/agent_workflow.md` for every operation and contract, its `docs/knowledge/` library for the COG,
model and level research, and Coda Lilitari's JK/MotS level workflow
(https://richard-pemberton-research.darkahn.chatgpt.site/projects/jk-mots-level-workflow/) and testing methods
(https://richard-pemberton-research.darkahn.chatgpt.site/projects/jk-mots-model-workflow/references/testing-methods.html).
