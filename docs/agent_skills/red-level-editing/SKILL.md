---
name: red-level-editing
description: Edit Jedi Knight and Mysteries of the Sith levels with RED's red-agent or red-mcp - inspect, plan and apply patches, validate, snapshot and package a level. Use when building or changing a JKL level headlessly.
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

## Rules that save you

- Exit codes: 0 done, 2 the request is malformed, 3 an input can't be read, 4 a hash is stale (re-inspect and
  re-plan), 5 RED refused on purpose (read the error code; don't retry the same request).
- Pass stock archives as `resourceArchives` (`[{path, sha256}]`) whenever an operation needs templates, scripts or
  materials, and `templateSource` for the master template file. Without them, headless levels can declare too
  little room for what scripts load.
- Things that move along paths need a `move=path` template. Use `setThingFrames` or `buildThingPath`, or the
  assistants (see `red-cog-scripting`).
- RED is a stand-in for the game. A clean validate and a good snapshot don't prove the level plays. Test it in the
  game, in both the 8-bit and 16-bit renderers. Keep levels retail-compatible: no engine changes.
- Over MCP, the same work is `sed_inspect`, `sed_plan`, `sed_apply`, `sed_diagnose` (validation), `sed_snapshot`,
  `sed_package_plan` and the rest. Paths must be inside the workspace or a resource root, and write tools take an
  `operationId` so a retried call is not applied twice.

See [the agent workflow](../../agent_workflow.md) for every operation and contract.

## Geometry and playtesting

Save unsaved GUI work before generating a new file; open the generated output explicitly. When neighbouring rooms
are cut separately, a rounded intersection can become a zero-length edge. Prefer shared coordinates and inspect
each planned cut. Do not bypass a refusal. Before `deleteSectors`, remove Things from the rooms to be deleted;
the operation refuses once COGs are placed. Supply `materialIndex` for surviving faces that must become walls.

A clean validation report and snapshot are not gameplay evidence. Record the executable and version, launch a stock
control under the same settings, then enter the actual packaged level. Walk its routes, collect pickups and operate
doors and lifts. Check the intended 8-bit and accelerated 16-bit renderers separately. For multiplayer, use two
separate clients and profiles and check movement, firing, death and respawn from both. A loopback pass is not a
test over the internet.
