---
name: jk-mots-level-workflow
description: Design, recover, edit and validate Jedi Knight Dark Forces II and Mysteries of the Sith levels in RED with its editor, CLI or MCP. Recover historical SED-MotS projects when requested; use the maintained research library for compatibility and editor comparisons. For character rigging or standalone COG language work, use the specialized model or COG workflow.
---

# JK / MotS level workflow

Build the requested level or improve its weakest part. Use RED for new levels, edits, packaging and playtests. Its editor, `red-agent`, and `red-mcp` share the level model. SED-MotS is deprecated. Keep its .NET tools for explicitly requested historical reproduction or migration. Infer the game, target engine and existing project from the request before asking consequential questions. Prefer OpenJKDF2/OpenMotS when the user has not chosen another target; report retail evidence separately.

This pack includes original research findings in [research-index.md](references/research-index.md) and [findings.json](references/findings.json). On first use in a task, `python scripts/research_updates.py` can check for newer reference material unless offline work is requested. The check is optional and failure does not block work. Applying references is a separate explicit command; it preserves local changes. Read [updates.md](references/updates.md) when updating or packaging.

## Choose a practical route

| Starting point | Route |
|---|---|
| Existing JKL or native JED/SED/NED project | Inspect source and companion templates; query exact targets; plan supported edits into new outputs. |
| GOB/GOO level package | List archive entries, extract the chosen exact JKL, preserve the source package and asset provenance. |
| Jedi Outcast or Jedi Academy map | Inspect the extracted Raven BSP and its source profile, map MATs and CMP, then prepare a new archive. Use the [playable conversion checks](references/red-design.md#converting-a-raven-multiplayer-map) for collision, texture editions and multiplayer. |
| New layout | Create a scaffold, use geometric selectors and connected extrusion/adjoins, then add useful traversal, materials and lighting. Boxes placed next to each other are not automatically connected. |
| Mission or moving architecture | Author ordered frames and typed COG bindings, validate identity and portal topology, then test the actual interaction in a declared runtime. |
| Compatibility or historical-tool question | Search the research index first, retrieve exact context where privately available, then check current source or a discriminating fixture. A demonstration, source release and tested capability are distinct. |

For original-model creation and native model inspection, import, conversion or export use `jk-mots-model-workflow`; for COG language/parser/synchronization work use `jk-mots-cog-workflow`. Default Blender-Sith work resolves the model skill's `scripts/resolve_install.py` and uses its verified current add-on and runtime; do not maintain another Blender-Sith path/version default in the level skill or silently use an old checkout. An explicit historical/development comparison keeps a separate source/runtime receipt. Continue the level task while those specialized parts are handled. External reviewers are optional; use the external-reviewers skill when a bounded source or visual review would change the result. Only share project material authorized for that destination.

For a model/material/animation handoff, carry the level's exact game, renderer,
CMP, resource names, placement units and intended interaction into the model
workflow. Use its explicit exporter API for the resolved version and select
`JKDF2` or `MOTS` for native KEY validation; a generic export is not evidence
for the intended engine. Native MAT export needs inspected image pixels (bake
procedural shaders first), the real CMP for indexed output, and the complete
cel/mip set. Require the returned resource hashes, actual encoding and any
missing-texture or recovery diagnostics. A recovered or partially textured model
is not a completed resource handoff. Preserve an authored model collision radius
separately from mesh culling bounds and independently verify level placement,
collision and actual gameplay events; two material cels or a synthetic event
does not establish physical Use behavior.

## Discover the actual installation

Run `python scripts/probe_tools.py` to locate RED from this installation's private `local-config.json` or `red-agent` on PATH. Use `--red-root <extracted-RED-directory>` or `--agent <exact-executable>` to select another build. The probe records the executable hash and reads its capability inventory without opening an editor or modifying a level. If RED is missing, obtain a matching package from the [RED releases](https://github.com/CodaLilitari/RED/releases) or build the tools from its [source](https://github.com/CodaLilitari/RED). A missing operation belongs in the handoff as a capability limit.

Start with CLI `schema`, then use advertised topics such as `schema commands`, `schema patch` and `schema <operation>`. For MCP inspect `tools/list`. Recent RED builds advertise `sed_schema` for capability discovery and `sed_inspect` for a level. Older builds may require a path for `sed_discover`. The historical `sed_` tool names and binary workspace suffix `.sed` are part of RED's interface.

Read [RED design](references/red-design.md), then the selected build's `docs/agent_workflow.md`, `docs/capability_matrix.md` and `docs/gap_register.md`. A private configuration may contain `redRoot`, `agentPath`, `repositoryRoot` and `corpusHome`. For a requested historical comparison use `--legacy-sed --sed-root <old-distribution>`; [the archived .NET design notes](references/headless-design.md) cover its distinct formats and command contracts.

## Design, inspect, apply, inspect again

1. Establish input hash, native source revision and any resource identities. Query bounded pages and retain exact indices/selector tokens. For spatial selections, accept only unique results; a boundary may match more than one room.
2. Make the next change concrete with discovery, `plan`, and before/after readback. Use the actual build's focused schema when advertised; otherwise consult its matching source-owned workflow for exact fields. RED and .NET schemas remain distinct even where command names match. Preserve unselected geometry, UVs, references and independent white/RGB lighting. Discover arrays, batch UVs, bindings and light diagnostics in the installed build rather than reimplementing them with text substitution.
3. For RED receipts with `semanticPreview`, compare both authored and persisted views, completeness flags and exclusions; an empty truncated result does not establish no change. Apply the reviewed output hash into a new file. Bind resources separately with the plan's resource revision where required. If source, resources or intended operations changed, plan again. Re-select after geometry changes; old tokens bind old geometry.
4. Query and validate the actual output. For visual work take an offscreen snapshot with an explicit camera and inspect the image. Lit snapshots require exact resource archives/hashes and a declared render profile; an incomplete asset preview must be labelled accordingly.
5. Test the behavior the user asked for. Structural validation, preview images, engine capture and gameplay establish different things. Use the existing original fixtures where appropriate, then adapt acceptance to this level. Preserve exact request/result receipts for durable handoff.

Planning is a machine check, not a reason to ask for human permission at every reversible edit. For a journaled RED MCP edit, retain the operation ID and consult `sed_outcome` after an interruption. Check the chosen tool’s exact arguments and the resulting file before retrying. A timeout does not prove the write failed, and reusing an ID does not resume an edit. File edits do not synchronize a GUI's unsaved document automatically.

For RED builds that expose **File > Save JKL & Test**, follow their `docs/game_testing.md`. Save modified COG and resource-editor drafts first, then export the current document to an isolated test stage outside the game installation and Git checkout. Preserve the native project's path, dirty state, undo history and normal export destination. Select Retail or OpenJKDF2 explicitly and retain separate JK/MotS runtime evidence. The OpenJKDF2 profile requests direct map startup; retail uses the developer resource path and requires selecting **New Game > RED Test**. `launch-plan` only checks and describes a prepared stage; it neither exports nor starts the engine. Retain the exact executable/profile, resource identities, staged map and actual observed outcome. Unknown dependency scopes require an explicit review; a missing named resource must not silently launch stale content.

For resources not discoverable from level references, such as custom UNI strings, add exact logical resources explicitly in the test/package setup. Review copied and omitted stock resources; do not include a whole stock archive to fill an unexplained gap. RED's engine-facing outputs canonicalize physical archive names while preserving authored logical references. See [RED's human controls and packaging boundaries](references/red-design.md) before choosing a GUI counterpart or interpreting a package report.

Read [RED design](references/red-design.md) for the end-to-end editor route and [compatibility and testing](references/compatibility-and-testing.md) for MotS lighting, missions and playtests.

## Research that survives a restart

Use [research-index.md](references/research-index.md) to choose a topic. If this installation has an authorized private corpus, read [private-corpus.md](references/private-corpus.md) and run the `corpus.py` helper with its explicit home directory. The index references existing captures in place; it does not fetch Discord or upload evidence. It distinguishes acquisition, reviewed text, source inspection, runtime verification and reuse. Search hits alone establish none of the latter states.

Record useful findings with exact source/version and the next acceptance case. Keep raw community logs, copied posts, third-party/game payloads and private locators outside public repositories and release packs. Shareable packets contain separately written original conclusions and public source links. Do not present source-informed work as formal clean-room work. Keep Urgon's announced Qt/C++ editor, Delphi SED, Mudhorn, NED/Smith and separate lighting utilities distinct during piecemeal comparisons.

