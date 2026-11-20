# Runtime validation and model packaging

> Copied into RED from [CodaLilitari/JK-MotS-Model-Workflow@ca011b8](https://github.com/CodaLilitari/JK-MotS-Model-Workflow/blob/ca011b84c4fa5e1f3755e1543be2c1c1d25063c9/skills/jk-mots-model-workflow/references/runtime-and-packaging.md) (MIT; see [LICENSE](LICENSE.md)). Regenerate with `tools/knowledge/import_knowledge.py` rather than editing this copy.

The release unit is a reproducible model package with a stated game/renderer/animation target. A Blender render, parsed 3DO, or successful install is useful evidence, but each answers a different question from actual gameplay.

For concrete Windows, WSL/Wine, virtual-display, source-port headless/capture, idle-camera and graphics-setting procedures, use [testing-methods.md](testing-methods.md). The [runtime test plan](https://github.com/CodaLilitari/JK-MotS-Model-Workflow/blob/ca011b84c4fa5e1f3755e1543be2c1c1d25063c9/skills/jk-mots-model-workflow/assets/runtime-test-plan.json) is a reusable starting record with every case initially marked `not_run`.

## Keep the evidence states explicit

| State | What it establishes | What remains open |
| --- | --- | --- |
| Source inspected | Geometry, materials, rig, and provenance have been reviewed | Native export and engine behavior |
| Native files validated | Export parses, references resolve, structural gates pass | Rendered appearance and animation |
| Package reproduced | Build yields the expected bytes and dependency closure | Installation and play |
| Installed and verified | Intended overlay exists and hashes match | Actual selected model and render path |
| Static runtime observed | Exact model rendered in an actual engine | Player integration, motion, networking |
| Sampled animation observed | Recorded keys or actions produced finite advancing poses | Complete blends, all weapons, every transition |
| One-player hosted MP observed | Selection and local host route work in that environment | A connected second client and remote appearance |
| Connected MP observed | Exact tested peers exchanged and displayed the intended model | Untested engines, maps, patches, or clients |
| Public delivery verified | Published archive downloads anonymously and matches | A recipient's installation or acceptance |

A previous build's pass transfers only to the fields proven unchanged. A byte-identical hand retains its source geometry evidence after a leg repair; a new final model hash still needs a scoped final runtime check. Do not copy an old all-weapons claim onto a new model without explaining that relationship.

## Build isolated test lanes

Use copies or overlays around legally owned game data. Preserve hashes of the executable, archives, source candidate, and installed overlay before and after the run. Do not make a graphics-driver change part of routine model diagnosis. Establish a stock control in the same lane first; if both control and candidate fail before loading, investigate the environment before blaming the model.

Record at least:

```json
{
  "game": "jk-or-mots",
  "engine_version_or_commit": "exact identity",
  "executable_sha256": "64 hex digits",
  "patches_and_wrappers": [],
  "os_and_runtime": "actual platform; include Wine if used",
  "renderer": "confirmed active renderer",
  "material_lane": "indexed8-or-rgb565-or-enhanced",
  "model_sha256": "64 hex digits",
  "animation_corpus_manifest": "project-relative manifest path",
  "entry_route": "normal-menu-or-diagnostic",
  "player_count": 1,
  "observed_actions": [],
  "capture_manifest": "project-relative manifest path",
  "exit_reason": "normal-quit-or-bounded-stop-or-failure",
  "limitations": []
}
```

This is a report template; example values are not a completed run. Use project-relative labels in a distributable report and keep installation paths or private profiles out of it.

Do not infer 3D acceleration from a saved preference alone. Confirm the game Display menu or equivalent renderer diagnostics. A captured window from a stale display is not a new observation; tie captures to live process identity, advancing frames, and the candidate hash.

## Test in increasing integration depth

1. **Stock control.** Start the exact target executable, render the unmodified reference model, and confirm renderer/material settings.
2. **Native model load.** Use a minimal existing test fixture to isolate file, material, bounds, and hierarchy problems. Diagnostic spawning can help, but label that route.
3. **Animation.** Exercise idle/fidget, walking and running in all supported directions, turning, aim up/down, crouch, jump/fall/land, swim where applicable, attacks, damage, death, and recovery. Check transitions and PUP priority mixtures, not only isolated KEY starts.
4. **Selection.** For MP, use normal Load Character and Edit Character preview, save/reload where supported, then select that profile into a hosted session. For SP, use the actual player or NPC replacement route intended for release.
5. **Weapons and cameras.** Check external body, separate external weapon hand, and first-person model. Confirm the held weapon by runtime state; inventory selection alone can leave the previous held object. Distinguish scoped, alternate-fire, mounted, and empty/trigger-only states.
6. **Lifecycle.** Test death/respawn, model changes, save/reload for SP, and relevant cutscene/gameplay transitions. Use supplied or owned content without turning this into a level-authoring project.
7. **Distance and scene cost.** Inspect every LOD, seams during transitions, mip shimmer, silhouettes at play scale, multiple visible characters, and representative dark/bright palette conditions.
8. **Connected multiplayer.** Use at least two clients when claiming MP interoperability. Check both host and remote model appearance, animation, weapon attachment, death/respawn, skin/team changes, join/rejoin, and missing-asset behavior. Record whether both peers have the package and the exact engine combinations.

An optional scripted pose probe can cover more animation samples than a manual pass. It should compare local or root-relative matrices so global movement is not mistaken for limb articulation. Finite advancing poses do not prove that a visible branch is moving: TDIR Broly supplied a concrete case where stock keys ran successfully while the visible large-character branch remained rigid.

For source-driven geometry repairs, isolate the acceptance measure. A bounding hull may be useful for clearance, but it cannot establish that actual open or concave surfaces hide another hand. Test the rendered objective separately.

## Multiplayer profile editing

The model field belongs to the multiplayer `.mpc` character file; the `.plr` settings file is not an interchangeable target. Prefer normal model registration and preview support. Direct `.mpc` selection is a historical fallback when appropriate and does not repair materials or a mismatched rig.

When using a helper, require the exact existing profile path, preview the byte change, close the game, make and verify a unique backup, change only `model:`, and preserve all other bytes. Detect an intervening save. State if the helper writes in place rather than atomically. Reload the exact character, then verify the actual in-game model without assigning one through a diagnostic script. The [source-only MPC helper on GitHub](https://github.com/CodaLilitari/OpenJKDF2-MotS-Fixes/tree/main/scripts/model-workarounds/mpc-selection), also available [on GitLab](https://gitlab.com/codalilitari/OpenJKDF2-MotS-Fixes/-/tree/main/scripts/model-workarounds/mpc-selection), documents these semantics and their limitations.

## Separate SP and MP integration

| Release intent | Integration responsibilities |
| --- | --- |
| Selectable MP skin | Model registry, display strings, sound class, exact profile selection, material dependencies, reachable player animation corpus, two-client evidence if claimed |
| Campaign player replacement | Exact intended player aliases/resources, cutscene and player puppet behavior, weapon/camera compatibility, save/load and campaign transitions |
| NPC or creature replacement | Exact model/template/sound/puppet relationships, AI-triggered animations, combat/hit/death behavior, attachment and collision effects |
| Custom animated model | Custom KEY/PUP package, runtime substitutions if used, dependency conflicts, all supported integration routes |

A selectable MP package does not automatically replace campaign aliases such as `ky.3do`, `kk.3do`, or `mj.3do`. Add only the route the user requested and test it directly. An NPC art benchmark is not evidence that the same file is registered as an MP skin.

## Local material editions with one character identity

A software-compatible indexed edition and an accelerated RGB565 edition can retain the same internal model names, selector, animation and script contracts while supplying different local MAT payloads. [The same-name edition study](same-name-material-editions.md) records an exact historical example and the required mixed-client checks. Install one overlapping edition per local installation; matching names alone do not certify compatibility, and installing both does not provide automatic renderer selection. A peer without either edition is a separate test case.

## Package original work and local derivation separately

A distributable workflow can include original scripts, synthetic tests, manifests, authoring templates, and original documentation. Include model/texture artwork only where the project has the necessary rights and intended release scope. Credit does not substitute for permission.

For dependencies from an owned installation:

- Ask the user or configuration for the exact installation path; do not ship stock KEY, DAT, UNI, CMP, GOB, GOO, executable, or DLL payloads as workflow conveniences.
- Read the required archive members locally, validate names and ranges, and reject missing or ambiguous inputs.
- Generate mod-local compatible KEY allocations and registration/string resources while preserving original archives and executables.
- Keep JK and MotS outputs distinct, especially their palettes, registry conventions, animation corpus, and material choices.
- Detect conflicts with other mods touching the same KEY, registry, or menu resources. A merge requires a new corpus and runtime check.

Keep source assets immutable and preserve named variants. A changed hand, shortened skirt, alternate texture lane, or experimental rig must not silently replace the accepted baseline. Keep source, build output, temporary private game files, and public staging separate.

## Installer and archive acceptance

Require a dedicated destination and a manifest of every installed file. A robust installer refuses existing unowned files, linked/reparse destinations outside the intended root, path traversal, invalid archives, changed required assets, and conflicting installs. Uninstall only files whose ownership and current hashes match its receipt; preserve unexpected user changes and report them.

Validate all offered build combinations. For example, two games × two poses × two material lanes means eight packages, while each runtime claim still applies only to the combinations actually run. Exercise install, verify, and uninstall in disposable test destinations. Include refusal cases where the tool might otherwise damage a real installation.

Before creating the release ZIP:

1. Freeze exact sources and record tool versions, build arguments, and license/attribution records.
2. Rebuild into a clean directory and compare output hashes. Retain intentional CRLF text and native bytes; do not normalize them casually.
3. Export an explicit allowlist or tracked-file tree. Exclude private references, profiles, downloaded community assets, owned game resources, caches, logs with personal paths, and build secrets.
4. Generate a checksum manifest, reopen the ZIP, check CRCs and exact membership, and verify each declared hash. Verify relative documentation links.
5. Publish the named release and source revision, then independently download its assets and compare their hashes. Test anonymous access separately from authenticated repository access.
6. Report delivery and runtime scope separately. A successful push, release asset upload, portfolio link, and GitLab mirror are distinct actions and should each have their own verification.

## Lessons from the retained case studies

Six earlier Starweird variants completed bounded original-executable rendering and sampled stock-key probes across five game/material configurations, but that did not certify ordinary connected multiplayer or later high-detail source assemblies. A subsequent model or revision does not inherit those results by sharing a character name.

The T-800 v1.0.1 release demonstrated a stronger normal-selection route: a selectable 24-node model with mod-local KEY allocation support, bounded original JK 1.01 and MotS one-player hosted tests, and independent installer combinations. Its RGB565 package checks did not become final-hash RGB565 runtime proof; a broad earlier weapon survey did not become an all-weapons test of the final leg-repaired hashes.

Later hand experiments also separated geometry containment, visible concealment, and artistic acceptance. A bulky cover could hide flesh in sampled frames and still fail the requested design. A more proportionate mechanical fist could be visually preferred while leaving some weapon-hand exposure. Preserve those distinctions in the release notes instead of declaring a single undifferentiated “pass.”

## Custom voice banks

A sound class is part of the character’s integration contract. [Generated character voice](generated-character-voice.md) covers dry Suno source performances, mono PCM delivery, actual trigger reachability, severity and weighted rows, repeated-hit gating and explicitly unfinished water placeholders. Test the selected executable: a reported engine correction or a newer WAV does not prove playback semantics have changed.
