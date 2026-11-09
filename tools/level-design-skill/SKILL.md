---
name: red-level-design
description: Design and validate JKDF2, Mysteries of the Sith and supported IJIM levels with this RED checkout's red-agent or red-mcp. Use the installed executable's contracts, preserve source/resource identities, and distinguish structural, visual and game acceptance.
---

# RED level design

Read [the authoring workflow](../../docs/agent_workflow.md) and [operation contracts](../../docs/agent_design.md) before making a patch. Check [the capability matrix](../../docs/capability_matrix.md) and [gap register](../../docs/gap_register.md) for missing adapters and unqualified workflows.

1. Select an exact `red-agent` executable built from this source. Use `schema` to discover its inventory; do not assume the older .NET SED-MotS schemas or exit codes apply. `scripts/probe_tools.py --agent <exact-path>` records the executable hash and distinguishes the two implementations without editing a level.
2. Inspect the document and exact resource archives. Retain input/source hashes, archive hashes and selector tokens. RED native `.sed` is binary; do not infer compatibility with historical text `.sed` from its extension.
3. Construct a small meaningful change through shared operations. A starter is a closed room with placeholder resource names. Touching boxes need explicit reciprocal adjoins. Keep authored white/RGB corner channels, UVs, Thing parameters and references intact.
4. Plan against the current source, then apply the expected output hash into a new file. Supply hash-bound COG resources when adding Things to a COG-bearing level. Refusals for opaque native state or retained unknown records protect data; do not remove those guards to complete a plan.
5. Reopen, query and validate the actual output. Re-select after geometry edits. For visual work produce and inspect a snapshot with an explicit camera. `software-v1` covers world surfaces; it does not show model Things, sprites or dynamic lighting, and missing resources must be reported.
6. Test the requested interaction in the chosen engine before claiming gameplay acceptance. For a current editor document, use **File > Save JKL & Test** and [the launch contract](../../docs/game_testing.md). Save modified COG/resource-editor drafts first. Keep the native project as the editable source; the test export must preserve its path, dirty state, undo history and regular export destination. Select Retail or OpenJKDF2 explicitly, with separate JK/MotS evidence. OpenJKDF2 requests a direct map start; retail requires choosing **New Game > RED Test**. Neither process creation nor an error-free launch plan proves the map loaded.
7. Retain the exact executable hash, engine profile, game-resource hashes, staged map and launch plan with the observed result. Stage outside the installation and Git checkout. Respect unresolved dependency warnings and inspect any explicit review before launching; missing named resources remain a refusal. Test actual traversal, collision clearance, COG interaction and mission completion as required. A clean structural report or editor image does not establish those behaviors.

Keep build trees, generated levels, resource copies, screenshots and receipts physically outside Git. Do not link external dependency trees into this checkout. Use one Git/build coordinator. Native GUI tests need an idle desktop and recorded effective display scale/work area; a process-local test-scale pass is a separate lane from the user's default scale.

Use the older .NET editor only through its own verified workflow when a required RED adapter is absent. KEY animation tracks and Thing path frames are distinct contracts. Preserve the installed general `jk-mots-level-workflow` skill as the multi-editor entry point rather than replacing its .NET fallback silently.
