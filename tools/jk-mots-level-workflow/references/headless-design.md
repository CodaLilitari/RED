# Archived SED-MotS .NET workflow

SED-MotS is deprecated. This reference documents its .NET commands for historical reproduction and migration. For RED's Qt/C++ agent use [red-design.md](red-design.md). Detect the installed executable first: focused schemas, exit codes, native `.sed` grammar, renderer and conversion arguments differ.

For a legacy text `.jed`/`.sed`/`.ned` input, inspect through the .NET route and retain its `sourceRevision`, which includes template libraries and present/missing companion `master.tpl` or `mots.tpl`. Its apply/convert path produces a **new `.jkl`**; it does not write a native editor project. Do not apply RED's same-container binary-workspace rule to that migration.

The dynamic-color command documented here is implemented in the new development milestone, after the historical `0.4.0-mots.15-dev` package. Discover it before use. Its output is static candidate-JKL loader inference with source/output hashes and bounded surface addresses; it neither changes colors nor executes COGs. The corresponding plan aggregate is `dynamicVertexColors`, bound to the same proposed output hash. Ambiguous parsing/rounding remains unknown; no automatic epsilon preparation is implied.

All doc paths below are relative to the chosen SED distribution or source root. Discover that root through explicit `--sed-root` or private `local-config.json`; never guess an old package from a hard-coded developer path. Run the executable with an argument array, send UTF-8 JSON on stdin as `-`, check the exit code and parse stdout. The CLI requires no model account or GUI; snapshots require offscreen Vulkan.

| Task | Discovery and maintained guidance |
|---|---|
| Inventory and recover old projects | `schema commands`, `schema query`; `archive`, exact `extract`, `inspect`, bounded `query`; `docs/AGENT_PROTOCOL.md` native revision and preservation sections. |
| New rooms, portals and traversal | `new`, `schema select`, `schema extrudeSurface`, `schema adjoinSurfaces`; `docs/AGENT_WORKFLOWS.md`, `docs/AGENT_DESIGN_ROADMAP.md`. |
| Materials, UVs, repeated placement | `schema query`, focused `projectSurfaceUv`, `transformSurfaceUv`, thing/light array operations as discovered; `docs/TOOL_WORKBENCH.md`. Inspect candidate counts and returned indices. |
| Lights and architectural models | `schema lighting`, `schema setLight`, `schema bindThingArchitecture`, `schema bakeThingArchitecture`, `schema resources`; `docs/ARCHITECTURE_AUTHORING.md`. |
| Dynamic vertex-color readiness, when installed | `schema vertexColors`, `vertex-colors <level> <request.json|->`; MCP `sed_discover` topic `vertexColors`, `sed_diagnose` kind `vertexColors`; `docs/DYNAMIC_VERTEX_COLORS.md`. Request fields: `schemaVersion:1`, optional `sector`, `offset`, `limit` (1..200, default50). |
| Mission logic and path movers | `schema bindings`, `schema addCog`, `schema setThingFrames`; `docs/PLAYABLE_AUTHORING.md`, `docs/EDITING_FOUNDATIONS.md`. |
| Images | `schema snapshot`; diagnostic geometry versus resource-backed static lit mode. Keep camera, resource identity and render profile fixed for comparisons. |
| MCP | `docs/MCP_INTEGRATION.md`; discover focused tool schemas, plan/apply, operation outcomes and typed errors. CLI is the fallback when the adapter is not connected. |

A typical CLI edit is `query input.jkl request.json`, `plan input.jkl patch.json`, then `apply input.jkl patch.json new.jkl <reviewed-output-hash>`. Native patches also carry `sourceRevision`; resource-sensitive patches carry `resourceRevision`. Copy exact fields from the actual response, not from this abbreviated example. The canonical workflow includes runnable PowerShell examples and safe stdin handling.

Geometric selector tokens avoid guessed sector/surface indices. They bind the containing geometry and revision: editing geometry invalidates old selections. Bulk operations should expose the selected set and count before apply. Same-name materials, templates and models can resolve to different bytes; a registered name does not establish resource availability.

Snapshots are useful for composition, scale and lighting inspection. Inspect the image rather than treating file creation as visual QA. Ordinary model things can still use scalar lighting in an RGB world; ARCH bindings follow a different path. A correct screenshot is not evidence that a level loads in retail or plays correctly.

For MCP installation, copy the current project-scoped configuration example from the selected distribution's `docs/MCP_INTEGRATION.md`. Supply an explicit workspace and resource roots. Do not change global configuration, add another wrapper or install an ACP controller merely because the level task mentions Codex. Existing typed authoring operations already support serious background design.
