---
name: red-level-design
description: Design and validate JKDF2, Mysteries of the Sith and supported IJIM levels with this RED checkout's red-agent or red-mcp. Use the installed executable's contracts, preserve source/resource identities, and distinguish structural, visual and game acceptance.
---

# RED level design

Read [the authoring workflow](../../docs/agent_workflow.md) and [operation contracts](../../docs/agent_design.md) before making a patch. Check [the capability matrix](../../docs/capability_matrix.md) and [gap register](../../docs/gap_register.md) for missing adapters and unqualified workflows.

1. Select an exact `red-agent` executable built from this source. Start with `schema`; if advertised, use focused `schema commands`, `schema patch`, `schema <operation>` and domain schemas such as `schema dependencies`, including their runtime constraints. Older installed RED builds may expose only the inventory. Do not assume the older .NET SED-MotS schemas or exit codes apply. `scripts/probe_tools.py --agent <exact-path>` records the executable hash and distinguishes the two implementations without editing a level.
2. Inspect the document and exact resource archives. Retain input/source hashes, archive hashes and selector tokens. RED native `.sed` is binary; do not infer compatibility with historical text `.sed` from its extension.
3. Construct a small meaningful change through shared operations. A starter is a closed room with placeholder resource names. Touching boxes need explicit reciprocal adjoins. Keep authored white/RGB corner channels, UVs, Thing parameters and references intact.
4. Plan against the current source. Review both authored and persisted `semanticPreview` views when returned, including completeness, reasons and exclusions; storage-slot paths are not stable entity IDs. Then apply the expected output hash into a new file. Supply hash-bound COG resources when adding Things to a COG-bearing level. Refusals for opaque native state or retained unknown records protect data; do not remove those guards to complete a plan.
5. Reopen, query and validate the actual output. Re-select after geometry edits. Inspect a snapshot with an explicit camera. The default `software-v1` renders world surfaces. Where supported, `thingMode: "static"` adds bounded rest-pose models and first-cel sprites. Check missing resources and use the game for dynamic lighting and animation.
6. Test the requested interaction in the chosen engine before claiming gameplay acceptance. For a current editor document, use **File > Save JKL & Test** and [the launch contract](../../docs/game_testing.md). Save modified COG/resource-editor drafts first. Keep the native project as the editable source; the test export must preserve its path, dirty state, undo history and regular export destination. Select Retail or OpenJKDF2 explicitly, with separate JK/MotS evidence. OpenJKDF2 requests a direct map start; retail requires choosing **New Game > RED Test**. Neither process creation nor an error-free launch plan proves the map loaded.
7. Retain the exact executable hash, engine profile, game-resource hashes, staged map and launch plan with the observed result. Stage outside the installation and Git checkout. Respect unresolved dependency warnings and inspect any explicit review before launching; missing named resources remain a refusal. Test actual traversal, collision clearance, COG interaction and mission completion as required. A clean structural report or editor image does not establish those behaviors.

## Convert a Raven multiplayer map

1. Capture the extracted RBSP v1 source and run `red-agent jo-ja-inspect <map.bsp>`. Choose `jo-mp`
   or `ja-mp` explicitly for footsteps. Bind exact MATs and the intended CMP. Inspect brush contents and sides
   separately from draw shaders. A player-only boundary can have no visible face. When the JK model-scene schema
   exposes `sourcePlayerClip`, choose walkable support or movement barriers explicitly. Use
   `movementBarrierBrushes` with `standable` to choose the selective walls by source brush index.
2. Use **Tools > Import JO/JA Map…**, or review `jo-ja-plan` and pass both its output and manifest hashes to
   `jo-ja-apply`. Start with a model scene for faithful source detail. A hybrid needs checked reciprocal openings,
   model ownership and opaque materials. Compare both sides of a proposed portal and retain a playable reference.
   Use the importer's reimport overrides to protect authored starts, lighting, sky, controllers and ambience.
3. For original JK software, prepare indexed MATs with their CMP. A mixed hardware edition can reuse the identical
   indexed cutout MAT while using RGB565 for opaque art. Check distant lighting and foreground occlusion with
   the JK ordinary-Thing draw pass. Compare editions at a fixed camera in software and accelerated rendering.
   For glass, compare the same target with the pane visible and with just its drawing disabled. Check source
   alpha strength and overlapping faces before changing the material or collision.
4. Run `python tools/engine-capture/verify_episode_pair.py indexed.zip hardware.zip` from the source tree to
   verify installed episode names, level identities, material capacity and packaged differences. Test a movement
   barrier with a player and a stock shot from the same position. Verify a breakable pane in the game as well as
   its script bindings. Run the final editions on two peers for multiplayer claims and identify any separate
   start-adjusted or instrumented test maps. The [converter guide](../../docs/jo_ja_import.md) records the
   Bespin, Council and RJA examples and the limits of each observation.

## Human workflow counterparts

Verify these actions in the selected executable and matching docs before directing a user to them. A newer source implementation does not update an installed release. Agent file edits do not synchronize an unsaved editor document.

- **Tools > Workbench** exposes Query, Batch UV and Arrays through shared operations. For MotS source lights, the Baked popup/Properties expose **RGB follows white** and **RGB intensity**: absent inherits white, explicit zero remains zero. Preserve independent ARCH channels and retained metadata; see [lighting](../../docs/lighting.md).
- **View > Camera Pose and Saved Viewpoints** retains editor views as workspace metadata. It does not author an engine camera or player start; see [saved cameras](../../docs/saved_cameras.md).
- **Tools > Dependencies…** corresponds to `dependencies` / `sed_dependencies`, using explicit exported JKL and ordered archives. Scan completion, detail completion, reference resolution and runtime closure are different claims; see [dependency inspection](../../docs/dependency_inspection_design.md).
- **Tools > Material Authoring** and **Tools > Import Model…** expose `mat-create` and `model-import`. Use explicit palette/material mappings, prepare and inspect before create-only output. Model image export is a separate publication. See [materials](../../docs/material_authoring.md) and [model import](../../docs/model_import.md).
- **Tools > Archive and Package Authoring…**, where exposed, provides Resource Archive and Exported Level tabs for `archive-create` and `package-plan`/`package-apply`. Explicit additional resources such as custom UNI strings also belong in **Save JKL & Test** setup. Preserve stock omissions, review unknown scopes and inspect final physical entries/hashes. Runtime output canonicalizes physical archive names; authored logical names and raw archival inputs remain distinct. See [game testing](../../docs/game_testing.md).
- **Tools > Engine Capture…** provides ordinary controls for the explicit capture runner. Prepare/Validate, runtime capture and actual gameplay are separate results; the [game testing contract](../../docs/game_testing.md) records supported profiles and limits.

Generic plan/apply [semantic previews](../../docs/semantic_preview_design.md) require their own receipt review. Do not assume a Workbench/dependency report is that preview or that an empty incomplete report proves no change.

**Help > Check Installation…** and `red-editor --check-installation --report-directory <absolute-new-directory>` test native installation rendering with isolated storage, not ordinary editing or gameplay. Keep source/full Release CTest, [package CLI/MCP acceptance](../release/package_cli_acceptance.md), [package native installation acceptance](../release/package_native_acceptance.md), ordinary GUI workflows and game acceptance as separate platform-specific evidence. See [installation checks](../../docs/installation_check.md); record exact package and executable identities rather than borrowing another platform's result.

Keep build trees, generated levels, resource copies, screenshots and receipts physically outside Git. Do not link external dependency trees into this checkout. Use one Git/build coordinator. Native GUI tests need an idle desktop and recorded effective display scale/work area; a process-local test-scale pass is a separate lane from the user's default scale.

Use the older .NET editor only through its own verified workflow when a required RED adapter is absent. KEY animation tracks and Thing path frames are distinct contracts. Preserve the installed general `jk-mots-level-workflow` skill as the multi-editor entry point rather than replacing its .NET fallback silently.
