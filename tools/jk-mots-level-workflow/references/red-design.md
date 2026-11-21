# RED level design route

Use this for CodaLilitari/RED's Qt/C++ editor and `red-agent` / `red-mcp`. Use the matching RED GUI, CLI or MCP operation. If an operation is unavailable, identify the build and record the gap. The deprecated SED-MotS workflow remains available for reproducing older work on request.

## Select the build and scope

Probe an explicit executable with `python scripts/probe_tools.py --agent <path>`. Start with RED's no-topic `schema` inventory. Builds advertising `focusedSchema` also support `schema commands`, `schema patch`, exact patch-operation topics and declared domain schemas such as `schema dependencies`. Read returned `x-runtimeConstraints`: schema validation alone cannot certify geometry or resource validity. An older RED executable may only expose the inventory; its contract is not the .NET schema contract. Retain the executable hash and source revision. A source checkout's Release executable may be outside the checkout under `tools/sed-agent/Release/red-agent.exe` or `tools/sed-agent/red-agent`; do not copy build/dependency trees into Git.

For MCP, inspect `tools/list` and exact argument schemas. Current RED builds expose capability discovery via `sed_schema` or an empty `sed_discover` request. `sed_inspect` reads a supplied level. Older versions may support only the path-taking `sed_discover`; in that case use CLI `schema` for capability discovery. RED snapshot camera vectors are nested in `camera: {position: [...], target: [...]}`. `software-v1` is the default. Discover `renderProfile` support before requesting `indexed8bit` for JK world faces and verify `appliedProfile` on the returned render receipt.

Read the matching source's `docs/agent_workflow.md` for runnable authoring, `docs/capability_matrix.md` and `docs/gap_register.md` for boundaries, and `docs/sed_format.md` for native persistence. Historical port ledgers record past milestones, not current acceptance.

## Ordinary design loop

Use `new`, `inspect`, `statistics`, `select`, `query`, `plan`, `apply`, `validate`, and `snapshot` as discovered. Store requests and receipts outside source checkouts. Bind a patch to returned input/source hashes; require the reviewed output hash on apply; use a new output path. Re-query selectors after geometric edits. A touching box is not connected until an explicit successful adjoin; inspect both portal ends and traverse the result in the declared game runtime.

Current source includes closed-room scaffolds and shared-core adapters for boxes, extrusion/adjoins, material/colormap registration, surface render modes, UVs, white/RGB corner lighting, source lights and fresh Thing placement by exact name. Verify those operation names in the actual binary before using them: unreleased source changes do not update an installed release. Exact fields and unit/refusal rules live in the source-owned workflow. For `addThing` or `placeThingArray` in a COG-bearing document, supply the exact hash-bound `resourceArchives` required by the placed-reference guard. A missing or incomplete schema is an explicit refusal, not permission to append and repair references afterward.

## Converting a Raven multiplayer map

Select **Tools > Import JO/JA Map…** or the matching `jo-ja-inspect`, `jo-ja-plan` and `jo-ja-apply` commands.
Read `docs/jo_ja_import.md` in the selected RED source. Use an extracted RBSP v1 and specify `jo-mp` or `ja-mp`
for the original game. Capture the exact BSP, opaque and keyed MATs and CMP. Review the generated static scene,
then author or preserve the moving architecture, scripts, sky, starts and audio that make an episode playable.
Use the plan's output and manifest hashes when applying.

- **Movement.** Inspect the original collision brushes and identify the routes a player should reach or be
  blocked from. A standable model can intercept shots. A movement-only wall may have no walkable top.
  Where the JK model-scene schema exposes `sourcePlayerClip`, select `standable` or `movementBarrier`. To
  combine them, give the standable recipe a `movementBarrierBrushes` list of verified source brush indices.
  Compare the player's path and a stock projectile independently, with an otherwise matching solid control.
- **Rendering.** For original JK software use all-indexed MATs with the matching CMP. For the accelerated
  edition, retain the same indexed cutouts alongside opaque RGB565. A fringe on foliage can follow the edge of
  one keyed mip chain. Where `mat-create` supports `colorKey` and `guardEdges`, guard only the affected edge.
  Compare the same camera in each renderer and retain authored lighting and UVs.
  For glass, read the source blend stages and count the overlapping surfaces at the chosen window. Place a
  high-contrast target behind it, then capture the same view with just the pane drawing disabled. This
  distinguishes an overly dark approximation from opaque geometry behind the pane.
- **Multiplayer.** Install the same episode basename on each peer. Inspect the actual JKL, COGs, models and
  differing MATs. In the matching RED source, `tools/engine-capture/verify_episode_pair.py` compares ZIPs or
  bare episodes and reports each JKL's free material slots. Test the final editions in separate
  processes with a remote-visible action. Record the hash of any relocated-start or damage-target fixture
  separately. Script simulation and editor inspection cannot establish that a player broke a pane in retail.

## Human controls and shared operations

Use ordinary controls when the user wants to work in the editor, and CLI/MCP for repeatable bounded operations. Confirm each action in the selected build and its matching documentation; a source dialog or newer source manual does not establish that an installed release exposes it. File-based agent changes do not update an unsaved GUI document.

| Work | Ordinary editor route | Agent or evidence contract |
|---|---|---|
| Bounded queries, UV projection and placement arrays | **Tools > Workbench**: **Query**, **Batch UV**, **Arrays** | Shared query/patch operations; discover exact names, selectors and limits. |
| Repeatable editor views | **View > Camera Pose and Saved Viewpoints** | See `docs/saved_cameras.md`. Saved editor views are workspace metadata, not engine cameras or player starts; export/snapshot requests still need their own explicit camera contract. |
| Source-light white and RGB authoring | Baked-light popup and Properties: **RGB follows white**, **RGB intensity** | In MotS, absent RGB inherits white; explicit zero is distinct. World RGB uses effective RGB while preserving existing alpha/white; ARCH mono and RGB remain independent. `legacyRgbRange` is retained metadata, not a second falloff range. See `docs/lighting.md`. |
| Explain resource selection | **Tools > Dependencies…** | `dependencies` / `sed_dependencies`: captured exported JKL and ordered archives, physical winning row/payload hashes, separate scan/detail/resolution results. Report export does not revalidate source paths or certify package byte identity/runtime closure. See `docs/dependency_inspection_design.md`. |
| Create indexed MATs | **Tools > Material Authoring** | `mat-create`: explicit CMP and image cels; preview/read back opaque indexed output and create a new MAT. Resource registration and assignment remain separate. See `docs/material_authoring.md`. |
| Convert OBJ/GLB into 3DO | **Tools > Import Model…** | `model-import`: explicit MAT mappings/dimensions, origin/scale/up-axis, normal policy and mesh splitting. Separate embedded base-color image export is an independent publication, not a two-file transaction or automatic MAT conversion. **Edit > Import Sector** is a different workflow. See `docs/model_import.md`. |
| Assemble archives or level packages | **Tools > Archive and Package Authoring…**, where exposed: **Resource Archive** / **Exported Level** | `archive-create` / `package-plan` / `package-apply`; review captured inputs, emitted contents, copied/omitted resources and unknown scopes before new-file publication. See the matching `docs/agent_workflow.md`. |
| Launch or capture a level | **File > Save JKL & Test**; **Tools > Engine Capture…** | Launch staging and the explicit engine-capture runner have different contracts. Capture Prepare/Validate does not launch the engine or prove all declared source hashes. Retain runtime profile and observed evidence; see `docs/game_testing.md`. |
| Diagnose the installed editor | **Help > Check Installation…** | `red-editor --check-installation --report-directory <absolute-new-directory>` exercises the native installation/render helper, with isolated storage. It does not qualify ordinary editing, undo, docking or game behavior. See `docs/installation_check.md`. |

Generic `plan`/`apply` semantic receipts are a separate review surface; do not assume Workbench or a dependency report is a GUI for that receipt. Read `semanticPreview.authored` (isolated candidate) and `.persisted` (exact proposed bytes reloaded), their input/output hashes, `comparisonComplete`, `detailsComplete`, reasons and exclusions. Paths identify storage slots in each snapshot, not stable entity IDs. Truncated details, optional native sections outside the model and an unknown result cannot certify preservation. Follow `docs/semantic_preview_design.md`; independently validate and reopen the output.

## Resource packaging and test staging

Package explicit project resources with their ordered archive identities. Names in a material/Thing table do not supply payload bytes. Custom runtime-only resources, including UNI messages, require explicit `extraResources`; the test GUI exposes **Folder** and **Resource name**. A `misc` resource named `cogStrings.uni` is emitted physically as `misc\cogstrings.uni`. Keep stock omissions and unresolved scopes visible instead of silently copying stock assets.

Engine-facing package/archive authoring uses validated lowercase ASCII physical names. Preserve authored logical references and raw archival fidelity; do not lowercase all project files or rewrite arbitrary input GOBs. Reject incompatible aliases/collisions with the reported diagnostic. Retain the package's own final payload hashes and inspect/read back its physical entries. Dependency inspection identifies its selected archive rows but does not substitute for package readback, particularly for legacy aliases. See `docs/game_testing.md` and the packaging section of `docs/agent_workflow.md`.

Save modified resource-editor drafts before staging. Keep the native editable project, exported JKL and runtime stage distinct. Separate JK/MotS and Retail/OpenJKDF2 profiles; process start does not prove map load, working HUD strings, collision, COG behavior or mission completion. Copy only the intended deliverable and its attributable sources; owned game inputs and private runtime proofs do not become redistributable merely because the package works.

## Preservation and evidence boundaries

- RED native `.sed` is a binary workspace format. The .NET route also accepts historical text `.sed`; do not route merely by that suffix. RED's `.jed`/`.ned` importer and companion-resource behavior need fixture-specific evidence.
- Native copy/no-op may retain complete opaque workspace sections. An edited native workspace carrying unsupported buffers/history must refuse rather than discard them or retain stale state. Treat a refusal as a concrete preservation constraint; use a supported explicit migration only when it meets the task.
- Registering `scaffold.mat` or another material name does not supply texture bytes. A starter room is a design scaffold, not a packaged playable mission. Resolve actual resources and validate typed COG/template/Thing dependencies before gameplay claims.
- RED's default `software-v1` snapshots cover world surfaces. Where supported, `thingMode: "static"` adds bounded rest-pose models and first-cel sprites. Keep cameras and resource inputs explicit, inspect the image, and check animation and dynamic lighting in the intended runtime.
- KEY animation tracks and Thing path-mover frames are different contracts. KEY commands use positional track selectors while `key-inspect` reports retained engine `nodeIndex`; fractional frames and sparse engine joint IDs must survive a save. Use `docs/formats/key_format.md` and the specialized model skill for animation work.
- Preserve separate evidence for source tests/full Release CTest, exact extracted-package CLI/MCP acceptance, native installation rendering, ordinary native editing workflows, engine load/capture and actual gameplay. `tools/release/accept_package_cli.py` and `accept_package_native.py` have separate owning docs; a successful native installation check still leaves ordinary editing acceptance unproven. Record each platform's exact package/executable hashes and actual result. Windows checks or reviewer advice do not qualify Linux/macOS UI or retail gameplay.

When requesting independent review, use the installed external-reviewers skill and exact authorized source/fixture attachments. Muse Code has its own native subscription route; a timeout or successful adapter test does not establish a completed source review.
