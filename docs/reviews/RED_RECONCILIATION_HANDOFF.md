# RED reconciliation handoff for Red and Lilitari

This is an in-progress source handoff, prepared at Andrei's request to conserve usage. It is **not a release approval**.
Continue from `codex/red-unified-release` on either [GitHub](https://github.com/CodaLilitari/RED/tree/codex/red-unified-release)
or [GitLab](https://gitlab.com/codalilitari/RED/-/tree/codex/red-unified-release). Do not replace the original dirty checkout.
No main merge, release tag, or new Windows/Linux/macOS binary publication was performed for this handoff.

## Reconciled source

The branch already incorporated Lilitari's exact lighting tree and twelve replayed integration commits, with provenance
in the prior branch history. Its preceding committed head was `c85b5566c65ece793741e11146277f53005856e4`.
This checkpoint adds the accumulated reviewed source work:

- Workbench query/array/UV controls, independent MotS source RGB editing, resource-aware export reconciliation,
  and their portable operations and focused schemas.
- Material Authoring, Model Lighting, Dependencies, Import Model, Engine Capture, and Archive and Package Authoring
  dialogs, with explicit captured inputs, review boundaries and create-only output. Model import and package authoring
  now have actual Tools-menu wiring and integration tests.
- Camera poses and saved editor viewpoints, native workspace persistence and recovery, and numeric/DPR controls.
- Owner-aware multisample depth handling for wireframes, with portable numeric and native renderer regressions.
- An isolated native installation diagnostic, shared Help/CLI contracts, bounded process ownership, Unicode paths,
  explicit runtime identities, and package acceptance helpers.
- Windows source-commit package metadata and exact Qt notice inputs; Linux/macOS CI package CLI/native acceptance
  steps and retained diagnostic evidence. CI configuration is not a completed platform run.
- Updated designer/developer contracts, capability/gap ledgers, and the portable level-design skill. The local general
  skill was also updated; it retains the older .NET editor as a separate route and checks the selected executable.

The archive helper extraction preserves the agent's original bytes, warning order and refusal precedence. The package
dialog's Qt5 API review findings were repaired. Actual Qt5 linking remains unverified; signature-model compilation is
not a substitute. All generated builds, notices, runtime copies and test evidence remain outside this source tree.

## Latest executed checks

The coordinator's Windows kit was Qt 6.8.3 MSVC, with Ninja Multi-Config Release and one build writer.

| Check | Observed result |
|---|---|
| Final integrated Windows build, including package dialog and shared adapter | Passed, 64 actions, 85.681 seconds |
| Focused RGB/Workbench/camera/model/installation checks with the correct Qt plugin path | 17 of 19 passed; controller and dialog fixtures failed |
| Native clipboard publication, entity clipboard routing, Paste in Place | All three passed; 7.24 seconds total, with Red's passive observer |
| Package dialog, actual Tools-menu integration, agent archive equivalence, source-commit manifest | All four passed |
| Newly registered core runtime-archive test | Timed out at 31.40 seconds; external focused version had passed; unresolved |
| Native ARCH lighting group | Four MainWindow vertex-selection assertions failed; no clipboard assertion failed |
| Dedicated wireframe support-depth test | Passed, 1.05 seconds |
| Windows package staging/target/source-commit focused regressions | Passed |
| Complete Release CTest, extracted final package and ordinary GUI acceptance | Not completed |
| Final Linux/macOS jobs and native packaged editor acceptance | Not completed |

An earlier direct test invocation omitted `QT_QPA_PLATFORM_PLUGIN_PATH`. Its offscreen tests stopped at a Qt startup
error dialog and timed out. The coordinator observed that dialog, stopped only the owned test tree, restored the exact
kit environment and reran. Those earlier timeouts are not application regressions or passing evidence.

Use the documented complete gate, with an explicit compatible kit and external build tree. On Windows ensure the
test environment includes the kit's `plugins/platforms` directory: the deployed ordinary editor only includes the
Windows plugin, whereas offscreen unit tests also require the kit's offscreen plugin. Preserve native tests as native.

## Highest-priority unresolved work

1. **Installation controller/dialog fixtures.** Current failures are matching native/rendered child evidence, child
   ownership admission, descendant spawn, destruction during progress and honest cancellation; the dialog's outcome,
   frame and saved-report checks also fail. The Unicode fixture follow-up and Qt compatibility patch were integrated
   just before this run. Diagnose rather than changing assertions or timeouts. Contract/storage/runtime/owner and
   main-window integration tests passed. The process fixture and controller have separate ownership responsibilities.
2. **Core archive test timeout.** Compare the freshly integrated CMake target with its passing external focused harness.
   The shared adapter and ordinary package GUI passed in the same integrated build. Do not raise an arbitrary timeout.
3. **ARCH fixture selection.** The failed MainWindow test targets first-owner mesh 0 vertex 0 at yaw/pitch 32/42. The
   immediately preceding equivalent multi-owner test explicitly uses visible vertex 5 because vertex 0 is occluded.
   The failure diagnostic's `owners=1` is a true boolean meaning both owners remained selected. A separate frozen
   diagnostic harness preserves all four assertions and switches only the fixture target and matching mono setup.
   It compiled successfully but its native vertex-0/vertex-5 comparison was not run. This is a source-grounded lead,
   not a verified fix; no repository ARCH picking source was changed for it.
4. **Lilitari's semantic-review GUI.** The dedicated saved chat is
   [Resume RED lighting work](https://chatgpt.com/c/6af524fe-66ac-832d-a3f9-dd713fe8b68f). Her isolated lane reported a
   typed shared comparator, 133 baseline responses, 32 exact CLI/MCP equivalence cases, offscreen GUI tests and a
   headless Release build. Its final handoff was not obtained: the response was interrupted, and reconnecting returned
   "Could not load this ChatGPT conversation." Recover the exact patch and receipts; do not reconstruct it from this
   summary or claim it integrated. It is pinned to `c85b5566`; root retains menu/build integration ownership.
5. **Updater parity.** The existing Help action is informational; check/stage/verify/activate remain local CLI operations.
   The Windows package omits that binary, Unix builds disable it, and the updater rejects the Unix size/type/symlink
   manifests. A bounded synthetic verifier probe reproduced the schema mismatch. There is no implemented network
   release-feed/download path. Do not imply automatic cross-platform updates work. A local GUI adapter and truthful
   platform refusals can be reviewed separately; remote downloading and Unix symlink support require their own design.
6. Recheck live remote heads before integration, run the complete Release suite, inspect ordinary GUI workflows, stage
   packages from the exact committed source, run CLI and native acceptance on each real platform, then merge/tag/publish
   only qualified artifacts. Package installation rendering is distinct from ordinary editing and game acceptance.

## TightVNC and Red's clipboard repair

Read [Red's original diagnosis and the later race corrections](2026-11-09-red-clipboard.md). TightVNC can hold the
clipboard while waiting for delayed rendering. The integrated publication helper services sent messages, verifies
private bytes and refuses uncertain intervening generations. It does not promise an atomic check-and-write.

The new passive-observer run again saw `tvnserver` opening the clipboard; all three selected native clipboard tests
passed. The observer never opens or reads clipboard contents. TightVNC configuration was not changed. Do not disable
the contention fixture or treat unrelated renderer/selection failures as clipboard failures without evidence.

## Moonwell and local continuation material

The current curated Moonwell working copy includes the accepted mission sources, GOB/JKL/SED, later controller/proof/
save-inspector tools, provenance manifests and a narrow Windows LF-output correction with regression evidence.
All 100 synthetic tests passed in the existing Linux environment; Windows had one original symlink-fixture privilege
error, preserved as a limitation. Canonical artifacts were independently audited and unchanged. No new engine
execution or rebuild with this final RED candidate occurred. Existing bounded Linux gameplay evidence is not retail,
Windows/macOS gameplay, multiplayer, audio or complete resource-closure qualification.

The coordinator retained exact stock/template/v2 input pins and a create-only final-agent rebuild command. Public
Moonwell distribution is not part of this source push: inherited asset attribution does not establish a blanket
redistribution license. Keep proprietary game inputs and private runtime receipts out of both forges.

The private local evidence directory is named `RED-reconciliation-20261112` under the user's Documents directory.
Useful entries: `moonwell-local-update-handoff.md`, `moonwell-rebuild-command-handoff.md`, `arch-owner-handoff.md`,
`package-authoring-handoff.md`, `release-source-review-handoff.md`, `updater-release-handoff.md`,
`level-workflow-skill-proposal/applied.json`, and the JSON/log receipts prefixed `handoff-` or `integrated-`.
The worktree is the existing `red-unified-release` Codex worktree. Do not create a conflicting writer or reuse stale
objects with changed MainWindow/GlView headers. The original working checkout remains preserved.

Red and Lilitari: thank you for the clipboard, lighting and mission work. This checkpoint preserves both the useful
integration and the failed checks so the next continuation starts from concrete evidence.
