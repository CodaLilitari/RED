# JK / MotS model workflow

> Copied into RED from [CodaLilitari/JK-MotS-Model-Workflow@ca011b8](https://github.com/CodaLilitari/JK-MotS-Model-Workflow/blob/ca011b84c4fa5e1f3755e1543be2c1c1d25063c9/skills/jk-mots-model-workflow/SKILL.md) (MIT; see [LICENSE](LICENSE.md)). Regenerate with `tools/knowledge/import_knowledge.py` rather than editing this copy.

> This is the standalone workshop's agent skill, kept for its rules and evidence. Its `scripts/` commands belong to that workshop and are not part of RED. In RED, use `red-agent` and the skills in [docs/agent_skills](../../agent_skills/README.md).

Help the user choose a method, then build the model. Offer a few useful alternatives with a recommendation, expected effort, visual tradeoffs, and target compatibility. Do not make them choose among every tool in this package or complete a long questionnaire before work can start.

On first use in a task, run `python scripts/research_updates.py` once to check the portfolio for newer research, unless offline work is requested or network access is unavailable. Continue with the bundled references if the check fails. Offer compatible updates when available; `--apply` preserves local edits and updates research only. See [updates-and-reporting.md](https://github.com/CodaLilitari/JK-MotS-Model-Workflow/blob/ca011b84c4fa5e1f3755e1543be2c1c1d25063c9/skills/jk-mots-model-workflow/references/updates-and-reporting.md).

## Pick the route

Infer the character, available source assets, desired appearance, and target game from the request and project. If a consequential choice is missing, ask briefly while preparing independent work. Keep an existing successful model and improve its weakest part instead of automatically regenerating it.

| User's priority | Recommend | Useful alternative |
| --- | --- | --- |
| Fast playable result, familiar humanoid proportions | Reskin or reshape a working stock-rig model | Licensed donor split into the same rigid body parts |
| A generated armored humanoid or supplied Tripo GLB | Preserve the generated source, slice rigid parts with UV continuity, fit a known stock hierarchy, and bake separate game atlases | Remodel the critical helmet/hand/joint regions manually |
| A statue, brazier or other environment prop | Measure the actual generated polygons and exported records, preserve the silhouette, and deliver a native prop with placement dimensions | Model a simple prop directly; hand sector, collision and moving-object work to the level workflow |
| Distinct creature with familiar movement | Stock skeleton plus authored silhouette, rigid attachments and deliberate texture space | Partial generated skull/torso plus manual limbs and cleanup |
| Unusual proportions or new movement | Custom hierarchy and a matched KEY/PUP set, following the *method* demonstrated by TDIR | Approximate the silhouette on stock animation for a smaller first release |
| Detailed mechanical/organic model | Keep a detailed source; retopologize or reduce a separate game mesh, then bake color and selected relief cues | Direct low-poly modeling with painted or procedural textures |
| Highest fidelity on a modern engine | Separate enhanced-engine asset profile | Maintain a conservative retail edition alongside it |
| New first-person hands or weapon | Author and test the actual view-model path as a separate asset | Third-person-only skin with its scope made clear |

Explain the recommendation in ordinary terms: for example, “For this character I would keep the stock movement and build a new head and torso. It gets you a playable version sooner; a custom rig would give better limb proportions but requires a new animation set.”

Read [methods-and-options.md](methods-and-options.md) for the fuller decision guide and example requests.

For hosted Tripo models, read [the conversion method and count ledger](tripo-to-jk.md); requested quads, downloaded polygon records, GLB triangles and emitted native counts are different quantities. Hosted Tripo and local TripoSG are distinct routes. For statues and other environment models, use [the prop guide and level handoff](environment-props-to-jk.md). For a custom voice, read [Suno takes to a game sound class](generated-character-voice.md). Existing supplied outputs can be reviewed without starting another generation.

## Inspect or render existing native models

For an existing JK/MotS 3DO or a model package, use our **Blender-Sith Agentic fork directly** for the Blender import. Read [the direct import recipe](blender-sith-fork.md#direct-native-import-for-inspection-and-screenshots). The [release/install guide](blender-sith-fork.md) distinguishes release evidence from the selected local installation. Load its verified source, register it in a disposable background process, and call `sith.model.model3doImporter.import3do(...)` with the actual MAT directory and CMP. A separately reconstructed mesh or independent parser can supplement diagnostics; it does not satisfy a requested fork import. Record the loaded module paths and source hashes, the Blender runtime, source assets, import parameters and any adaptations in the job receipt. If the direct import fails, preserve the failure and repair the task adapter or report the blocker before switching routes. Malformed-file recovery is a separate, explicit choice with retained diagnostics; it does not reconstruct missing source data.

For default Blender-Sith inspection, conversion, export or regression jobs, first run `python scripts/resolve_install.py` from this skill with Python 3.11 or newer. It verifies the shared private current-install manifest and returns the exact add-on, source root and runtime; use those paths in the job. It does not launch Blender, select a newer release or enable desktop preferences. A missing or invalid manifest is an installation blocker, not permission to use an old checkout, ambient `sith` or a runtime found on PATH. Explicitly requested historical/upstream comparisons may instead use a deliberately selected, separately verified source and runtime without changing the current install. See [shared installation resolution](blender-sith-fork.md#resolve-the-shared-current-installation).

Recover the exact requested package/version first and inspect its native records before rendering. The current fork imports **geoset 0 only**; examine other geosets separately and do not describe one Blender view as an all-LOD or runtime check. Preserve the native geometry and textures in the inspection scene. Put deliberate presentation changes in a separate scene or material copy and record them. Deliver inspected HD views with useful front/side/rear or detail coverage, an editable Blender scene when appropriate, and a concise account of verified issues and untested runtime behavior. See [Blender control and rendering methods](blender-export-and-baking.md).

For **Blender-Sith 1.2.0**, use the [native-pivot and model/KEY decisions](blender-sith-fork.md#120-native-pivots-and-binding-audit) and the retained [API migration rules](blender-sith-fork.md#110-integration) before adapting older scripts or saved scenes. Export through the explicit exporter modules; choose the [JKDF2 or MOTS KEY target](compatibility.md#select-the-key-export-target), and use the [baked-image MAT route](blender-export-and-baking.md#native-mat-export-from-baked-images) with an explicit encoding and real CMP for indexed output. Successful source/package validation does not change a local installation: resolve and verify its selected add-on/runtime before using these APIs.

## Work from source to game

1. **Reference and plan.** Identify the exact design/variant, lawful starting assets, and features that must survive at game size. Save editable source and a small set of matched reference views. For animated models, choose stock or custom animation; for a prop, establish units, footprint, height, facing, origin and intended placement before fine geometry.
2. **Build the parts.** Use Blender modeling, licensed donor geometry, or local image-to-3D for the parts each handles well. AI can supply coarse form; inspect and repair joints, fingers, teeth, sockets, thin sheets and hidden backs. Use [the Tripo conversion recipe](tripo-to-jk.md) for a supplied generated humanoid, or [partial-generation.md](partial-generation.md) for selective local parts.
3. **Make surfaces readable.** Choose hand painting, procedural color, generated source textures, projection, or a high-to-low bake. Use a neutral color pass as the main reusable texture; test any painted light or AO as a deliberate style choice. See [textures-and-imagegen.md](textures-and-imagegen.md).
4. **Prepare the game mesh.** Preserve useful silhouette and joints, reduce invisible/interior detail, allocate UV space intentionally, and bake onto a separate export candidate. See [blender-export-and-baking.md](blender-export-and-baking.md) for CLI/UI choices and export recipes.
5. **Bind the animation contract where needed.** In Blender-Sith 1.2.0, use the native model/KEY audit for index, label and target-limit diagnostics; it does not evaluate poses or retarget tracks. Inspect a troublesome seam with contrasting rigid-part colors in the exact failing KEY pose before repainting it. JK/MotS rigid mesh hierarchy and indexed KEY tracks need agreement on indices, parents, pivots and PUP references. Extra geometry nodes may also require larger KEY `JOINTS` allocations. A changed filename or player profile does not retarget animation. A static prop needs a deliberate native hierarchy and bounds, without an unrelated humanoid rig. See [compatibility.md](compatibility.md).
6. **Integrate the intended role.** Distinguish SP actor, SP player replacement, MP selectable skin, first-person view model, and third-person weapon. If a custom voice is requested, select clean performances, preserve float masters, bind the actual SND triggers and exercise interruptions/repetition using [the voice workflow](generated-character-voice.md). Package the resources each actually uses; derive needed stock resources locally from the user's installation. See [runtime-and-packaging.md](runtime-and-packaging.md).
7. **Review the result in its target.** Inspect the source, exported model, texture mapping, and relevant movement/weapon cases. Choose native Windows, WSL/Wine virtual-display retail, or OpenJKDF2/OpenMotS harness checks from [testing-methods.md](testing-methods.md). For connected multiplayer claims use actual separate clients. Record the exact version tested and the remaining limitations; offer a useful partial result when the requested runtime is unavailable.

The sequence is adaptable: an existing skin with a texture change does not need a new generation pipeline. A source-only modeling request does not require game deployment. Retain the user's requested scope and existing authorization.

## Use help where it changes the result

ChatGPT Pro is useful for reference comparison, difficult anatomy, alternative construction plans, source analysis and reviewing actual views. Local Codex workers are useful for independent parsing, focused Blender scripts and export checks. Give each a bounded question or owned component and verify consequential findings yourself. See [advisory-and-delegation.md](advisory-and-delegation.md) and the packet template in [assets/review-packet.md](https://github.com/CodaLilitari/JK-MotS-Model-Workflow/blob/ca011b84c4fa5e1f3755e1543be2c1c1d25063c9/skills/jk-mots-model-workflow/assets/review-packet.md). This skill works without any paid service or particular collaborator identity.

For local 3D generation, choose the direct model runner or a verified ComfyUI API workflow using [local-generation-automation.md](local-generation-automation.md). Check the installed node/model inventory, prepare the input actually reaching the encoder, preserve the workflow and intermediate outputs, and generate only the useful missing part.

Keep dependency environments, generated models, capture trees and builds physically outside Git checkouts without links back into them. Serialize Git work per checkout and give broad read-only queries actual process deadlines. Preserve exact source/output hashes so an older receipt is not mistaken for validation of a later model.

## Small tools

- `python scripts/inspect_contract.py --model candidate.3do --keys idle.key walk.key` reports hierarchy and KEY allocation consistency. Add `--reference-keys` and `--detail-start` when appending detail to a preserved rig. It reads files and does not fix them or certify playability.
- `scripts/probe_blender_sith.py` reports selected exporter behaviors using synthetic geometry, including deliberate modifier rejection versus silent base-mesh export. Read its help/reference before using it with the installed Blender and add-on; it is not the complete upstream core/application suite.
- `scripts/resolve_install.py --regression-output <absolute-new-external-directory>` verifies the current install and emits an exact `regression_argv` list without running it. Execute that list only for an authorized check; preserve its launcher receipt and measured runtime identity. The older probe above is a focused diagnostic, not the current release's full regression gate.
- [assets/model-plan.json](https://github.com/CodaLilitari/JK-MotS-Model-Workflow/blob/ca011b84c4fa5e1f3755e1543be2c1c1d25063c9/skills/jk-mots-model-workflow/assets/model-plan.json) is a compact planning/evidence template; adapt it to the task rather than requiring every field for simple edits.
- [assets/runtime-test-plan.json](https://github.com/CodaLilitari/JK-MotS-Model-Workflow/blob/ca011b84c4fa5e1f3755e1543be2c1c1d25063c9/skills/jk-mots-model-workflow/assets/runtime-test-plan.json) records exact engines, wrappers, graphics/material settings, camera cases and results. It starts with every case marked `not_run`.
- `scripts/prepare_finding.py` creates an optional local findings email draft. Reporting to `lilitarikiralv@gmail.com` requires explicit user permission for the displayed recipient, subject, body and each attachment before sending through an available email route. Never report automatically; follow [updates-and-reporting.md](https://github.com/CodaLilitari/JK-MotS-Model-Workflow/blob/ca011b84c4fa5e1f3755e1543be2c1c1d25063c9/skills/jk-mots-model-workflow/references/updates-and-reporting.md).

## Research to consult when needed

- [Downloaded corpus](downloaded-corpus.md): actual mod/skin/weapon packages, tool archives and native file evidence; includes a portable inventory with package hashes and explicit exclusions.
- [MotS native examples](downloaded-model-integration.md): cosmetic skins versus personality/weapon/body-state changes, registry checks, carried geometry and custom creature rigs.
- [Native tools and templates](downloaded-tools-geometry.md) and [material converters](downloaded-material-tools.md): exact template contracts, versioned authoring tools, indexed/16-bit choices and cells/mip preservation.
- [Additional animation](additional-geometry-animation.md), [surface methods](additional-surface-methods.md) and [model diagnostics](additional-discord-model-methods.md): attributed methods, controlled comparisons and unresolved hypotheses.

- [jke-tdir-shred-research.md](jke-tdir-shred-research.md): historical alternatives, measurements, sources and credit.
- [community-modeling-research.md](community-modeling-research.md): attributed character hierarchy, pivot, animation-event, skin and POV methods.
- [community-surface-research.md](community-surface-research.md): community MAT, palette, UV, shading and exporter choices.
- [blender-sith-audit.md](blender-sith-audit.md): the dated upstream/project-wrapper baseline; consult the fork guide for later changes.
- [blender-sith-fork.md](blender-sith-fork.md): version-specific evidence, shared installation, MAT/KEY APIs, strict/recovery import, precision, background jobs, optional MCP and tested limits.
- [tripo-to-jk.md](tripo-to-jk.md): Red’s separate Mando OC and Mandalorian Red examples, rigid slicing/fitting, baking, palette and weapon-hand lessons.
- [rigid-joint-and-face-diagnostics.md](rigid-joint-and-face-diagnostics.md): Red's later exact-pose ownership checks, centred caps, proportion adapters, measured native-face counts and matched clay/albedo face registration.
- [environment-props-to-jk.md](environment-props-to-jk.md): Wren/Lily prop practice, source versus native counts, silhouette and fit repairs, shared-atlas repaint checks, and Dot Lilitari's separate Kestrel level/resource handoff.
- [generated-character-voice.md](generated-character-voice.md): Lily/Red/Andrei’s Suno-to-SND method, whole-phrase editing, loudness, runtime corrections and incomplete water coverage.
- [research-index.md](research-index.md): reusable findings and their strength of evidence.
- [source-archive.md](source-archive.md): original links, verified archive alternatives and local licensed source snapshots for offline evidence.

Do not turn a measured successful example into a universal engine limit. OpenJKDF2, patched JKE, original JK, and original MotS are separate targets. An attractive Blender view, parseable 3DO, successful hosted start, and connected multiplayer session establish different things. Keep those distinctions concise and relevant to the user's choice.
