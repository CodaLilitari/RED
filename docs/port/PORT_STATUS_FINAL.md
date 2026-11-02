# Final port status — Coda features → C++/Qt rewrite (verification handoff)

> Verification date: 2026-11-01. Repo: `SED-Qt-Port`, branch
> `codalilitari/features-port` @ `b82255998fb733c8e8c87104eb7c17f27b48376f`.
> All slices are uncommitted in the working tree; nothing was committed by
> this verification. Coda repo `SED-MotS-Net` was read-only throughout.
> Contracts: [PORT_PLAN.md](PORT_PLAN.md) Phases 2/4/6,
> [CODA_FEATURES.md](CODA_FEATURES.md), [COG_SLICE_SPEC.md](COG_SLICE_SPEC.md).
> Source ledger: [PORT_LEDGER.md](PORT_LEDGER.md). Baseline:
> [BASELINE.md](BASELINE.md).

## 1. Git scope (whole port)

- Branch `codalilitari/features-port` @ `b822559`. Working tree: **28
  modified** tracked files, **71 untracked** files. No commits made.
- Raw `git diff --stat` is inflated by CRLF working copies; the true change
  is `git diff --ignore-cr-at-eol --stat`: **28 files, 2379 insertions,
  13 deletions**. `core.autocrlf` is `false`; fixtures stay LF.
- **New top-level directory:** `tools/` (absent at HEAD) with
  `tools/sed-agent/`, `tools/sed-mcp/`, `tools/sed-update/`,
  `tools/cog-registry/`. `build/` is ignored build output, not source.
- New source areas under existing roots: `docs/port/` (7 docs),
  `src/core/include/sed/level/` additions (`arch_authoring`,
  `arch_validation`, `cog_bindings`, `dynamic_vertex_colors`,
  `spatial_selection`, `workbench`), `src/core/include/sed/update/`
  (`updater.hpp`), `src/core/include/sed/assets/` additions
  (`cog_preflight`, `cog_registry_tables`, `cog_schema`, `source_lights`),
  `src/app/src/ui/cog/` dialogs, `src/app/src/ui/query/workbench_dialog.*`,
  `src/app/src/main_window/gui_wiring.cpp`.
- **Coda untouched:** `SED-MotS-Net` shows only pre-existing `.DS_Store`
  deletions (`## main...origin/main [behind 1]`, HEAD `f15639e`); this
  verification ran read-only `git status`/`git log` there and wrote nothing.

## 2. Build and test evidence (this verification, fresh run)

| Gate | Command | Result |
|---|---|---|
| Configure | `cmd /c "build\msvc-env.bat cmake --preset msvc-ninja-cpp23"` | exit 0 |
| Full build | `cmd /c "build\msvc-env.bat cmake --build --preset msvc-ninja-debug -j2"` (`-j2` only) | exit 0 (`ninja: no work to do` — tree already current) |
| Core tests | `build\ninja\src\core\Debug\sed_core_tests.exe` from repo root | exit 0 (silent on success; all `runGroup` suites incl. new F-00/F-03–F-08/F-10/F-12/F-13/F-14 groups registered in `src/core/tests/test_main.cpp`) |
| Agent/MCP/update | `ctest --test-dir build/ninja -R 'agent\|mcp\|update' -C Debug` (via `build\msvc-env.bat`) | **21/21 passed, 0 failed** (2.97 s) |

CTest detail (all `Passed`): `sed_agent_exit-codes`, `sed_agent_stale-hash`,
`sed_agent_existing-output`, `sed_agent_request-cap`,
`sed_agent_plan-writes-nothing`, `sed_agent_query`, `sed_agent_select`,
`sed_agent_validate-new-convert`, `sed_agent_archive-extract-launch`,
`sed_agent_snapshot-refusal`, `sed_agent_cog-bindings`,
`sed_agent_cog-add-rebind` (12 agent); `sed_agent_mcp_protocol`,
`sed_agent_mcp_writes`, `sed_agent_mcp_outcome`,
`sed_agent_mcp_snapshot-policy` (4 MCP); `sed_update_equal`,
`sed_update_downgrade`, `sed_update_corrupt-hash`,
`sed_update_interrupted-staging`, `sed_update_stage-activate` (5 update).

### Baseline exclusions (per [BASELINE.md](BASELINE.md), unchanged)

- Allocation-failure suite aborts on MSVC 19.51 (pre-existing, not a port
  regression). Not re-run as a gate here.
- GUI suites hang/crash in this sandbox. All GUI work below is
  **compile-verified only and runtime-unverified**. No GUI suite was run in
  this verification.

## 3. Final feature table (F-00…F-14)

Status vocabulary: `ported` / `partial` (exact remainder named) /
`adapted` / `not-started` / `engine-gated`. Every row was cross-checked by
grep against the implementing files named. **Flag** = ledger claim exceeds
code evidence; the corrected status is given.

| ID | Status | Implementing files (verified present) | Passing test suite that ran in this verification | Remainder / notes |
|---|---|---|---|---|
| F-00 | partial | `src/core/src/assets/jkl.cpp`, `src/core/include/sed/assets/jkl.hpp`; tests `jkl_preservation_parity_tests.cpp`, `jkl_geo_preservation_tests.cpp` | `sed_core_tests` exit 0 (`jkl-preservation-parity`, `jkl-geo-preservation` groups) | Unknown count-framed sub-blocks inside GEORESOURCE; record-internal extras inside THINGS/TEMPLATES/COGS rows; sector lines after the SURFACES record; byte-exact no-edit identity (modelled sections are regenerated). `.ned` companion round-trip proven at core level. |
| F-01 | ported | `tools/sed-agent/` (`main.cpp`, `agent_workspace.cpp`, `agent_json.*`); `runCommand`, `outputSha256`, 1 MiB cap, exit codes 0/2/3/4/5 in `agent_workspace.cpp` | 12 `sed_agent_*` ctests passed (real binary) | `snapshot` honestly refused (`snapshotRequiresRenderer`, exit 5, no image faked). Wider per-domain ops (geometry/lighting/workbench) not yet exposed — tracked under F-12. **Flag (minor):** ledger text "F-02 MCP exposure still missing" is stale — F-02 is present (see F-02). |
| F-02 | ported | `tools/sed-mcp/main.cpp` (thin adapter over `sed_agent::runCommand`; operation journal `<workspace>/.sed-mcp/journal.jsonl`; 11 tools `sed_discover/query/select/diagnose/plan/apply/new/convert/archive/snapshot/outcome`) | 4 `sed_agent_mcp_*` ctests passed (real binary over stdio) | `sed_snapshot` delegates to the agent refusal (`snapshotRequiresRenderer`, no PNG created). No domain logic in the adapter. |
| F-03 | partial | Core: `src/core/include/sed/assets/source_lights.hpp`, `src/core/src/assets/source_lights.cpp` (`sed-source-light-base-v1`, `SED_SOURCE_LIGHTS`), `SedLight::rgbIntensity`/`effectiveRgbIntensity()` in `src/core/include/sed/level.hpp`. Renderer: `sourceLightChannels` in `src/app/src/render/mesh/render_lighting_utils.*` | `sed_core_tests` exit 0 (`source-light-metadata` group) | `.sed` binary persistence of `rgbIntensity`/`legacyRgbRange` (only equality-compared in `sed.cpp` `sameLight`, not serialised); GUI/agent `setLight` surfaces (grep: none in `agent_workspace.cpp`). Renderer slice compile-verified, **runtime-unverified**. |
| F-04 | partial | `resolveMatTransparencyMode`/`matDiscardsPaletteIndex` in `src/core/include/sed/assets/mat.hpp`; `saveTo3DO` in `src/core/include/sed/assets/3do.hpp`; fixtures `f04_diamond.3do`, `f04_hierarchy.3do`, `f04_marker.3do` | `sed_core_tests` exit 0 (`mat-transparency`, `model-3do-writer` groups) | GUI draw ordering/blend/depth-write and offscreen index-zero probe — Phase 4 remainder, **runtime-unverified**. No transparency-table fidelity or pixel-identical clipping claimed. |
| F-05 | partial | `src/core/include/sed/level/arch_validation.hpp`, `src/core/src/level/arch_validation.cpp` (`readArchModelTopology`), `GobArchive::entrySha256Hex` | `sed_core_tests` exit 0 (`arch-validation` group) | **Flag:** ledger says "Present (core)" — shared GUI/agent validated-export entry point and explicit archive path+file-hash verification at call sites are absent (grep: no arch-validation use in `agent_workspace.cpp`). Core validator is ported; integration is the remainder. |
| F-06 | partial | `buildJklExport`/`commitJklExport`/`inspectJklExportBuffers`, `geoResourceRenumbered` in `src/core/include/sed/assets/jkl.hpp`; `Sector::colormapIndex` in `level.hpp` | `sed_core_tests` exit 0 (`jkl-export-transaction`, `jkl-geo-preservation` groups) | Preserved ordering refused (renumber reported) when COG surface values bind to editor order and imported order differs, when a shared vertex/texture slot diverges, or under nearby-vertex welding. Native (non-imported) levels regenerate in sector order by design. Adjoin distance byte-preservation not claimed in either codebase. |
| F-07 | ported | `src/core/include/sed/level/spatial_selection.hpp`, `src/core/src/level/spatial_selection.cpp` (`staleSelectionToken`, `cannotClassifySpatialSelection`, tolerances 1e-7 / 0.1°) | `sed_core_tests` exit 0 (`spatial-selection` group) **and** `sed_agent_select` ctest passed (mint + resolve, stale replay exit 4) | None. `none`/`ambiguous` are successful null-token results. |
| F-08 | partial | `src/core/include/sed/level/arch_authoring.hpp`, `src/core/src/level/arch_authoring.cpp` (`prepareBindArchSamples`/`prepareResetArchBinding`/`prepareBakeArchSamples`/`prepareUnbindArch`, `SED_ARCH_AUTHORING`) | `sed_core_tests` exit 0 (`arch-authoring` group) | **Flag:** ledger says "Present (core)" — `.sed` persistence of provenance (grep: none in `sed.cpp`), hierarchy-transform bake limits (callers supply rest-pose vertices), and a shared world-ray provider for the occluded profile remain. GUI ARCH Inspector compile-verified, **runtime-unverified**. Bake profiles are editor recipes, not a recovered retail baker. |
| F-09 | engine-gated | — (no `tools/engine-capture/`; verified absent) | — | Not started. Requires the capture-capable OpenMotS build (`sed-engine-capture-v1`); an ordinary retail executable is not a substitute. Engine acceptance stays **unverified**. Harness pattern (manifest + private staging + P6 RGB8 acceptance) is the adaptation when unblocked. |
| F-10 | partial | `src/core/include/sed/assets/cog_schema.hpp`, `cog_preflight.hpp`, `cog_registry_tables.hpp` (generated by `tools/cog-registry/generate_cog_registry.py`), `src/core/include/sed/level/cog_bindings.hpp` (`prepareAddCog`/`prepareRebindCogReference`/`validateThingAppend`); GUI `src/app/src/ui/cog/cog_preflight_dialog.*`, `placed_cog_bindings_dialog.*`, `cog_gui_wiring_utils.*` | `sed_core_tests` exit 0 (`cog-schema`, `cog-preflight`, `cog-binding`, `cog-mutation` groups); `sed_agent_cog-bindings`, `sed_agent_cog-add-rebind` ctests passed | Core complete (A-01…A-37 incl. `target-unmeasured` for IJIM, `lexical-preflight-v3-not-a-compiler`). Remainder is verification, not function: GUI wiring compile-verified only, **runtime-unverified** in this sandbox. Verb availability is new port behaviour (adapted part), not ported from Coda. |
| F-11 | engine-gated | — (`setThingFrames` grep: absent; no oracle in `tools/`) | — | Not started as a feature. `addCog` authoring exists via F-10; the acceptance oracle (causal witnesses, negative controls) needs the F-09 launch path and a real OpenMotS executable. Never weaken witnesses to make runs pass. |
| F-12 | partial | `src/core/include/sed/level/workbench.hpp`, `src/core/src/level/workbench.cpp` (`queryWorkbench`, `prepareProjectSurfaceUv`/`prepareTransformSurfaceUv`, `computePlacementPositions`, `prepareThingArray`/`prepareLightArray`); GUI `src/app/src/ui/query/workbench_dialog.*` | `sed_core_tests` exit 0 (`workbench` group) | **Flag:** ledger says "Present (core)" — CLI/MCP exposure absent (grep: no workbench use in `agent_workspace.cpp`/`sed-mcp/main.cpp`), declared thing-capacity refusal, grid/radial shapes in the dialog, and independent-RGB light fields remain. GUI dialog compile-verified, **runtime-unverified**. |
| F-13 | ported | `src/core/include/sed/level/dynamic_vertex_colors.hpp`, `src/core/src/level/dynamic_vertex_colors.cpp` (`evaluateDynamicVertexColors`, proposed-serialized-row assessment, `proposedOutputSha256`) | `sed_core_tests` exit 0 (`dynamic-vertex-colors` group); agent aggregate rides the passed `sed_agent_*` plan/apply ctests | **Flag (doc only):** ledger row ends with stale pre-port text "there is no retention evaluator" — contradicted by the shipped evaluator above; code evidence wins. Read-only; changes no values. |
| F-14 | adapted | `src/core/include/sed/update/updater.hpp`, `src/core/src/update/updater.cpp` (`IUpdateSource`/`FileUpdateSource`, `RELEASE_MANIFEST.json`, `<install>.staging-<version>` → `activate` rename, launch `verify`); `tools/sed-update/`; see [UPDATER.md](UPDATER.md) | 5 `sed_update_*` ctests passed (real binary) **and** `sed_core_tests` exit 0 (`updater` group) | Adapted to the Qt portable layout (stage beside, never overwrite, refuse equal/downgrade, per-file SHA-256 receipts rechecked at launch). Feed is **not independently signed** (HTTPS + host-control trust only). Help-menu entry compile-verified, **runtime-unverified**. |

## 4. Confirmation — every `ported` row has a passing suite from this run

| Row | Suite(s) executed in this verification | Observed |
|---|---|---|
| F-01 ported | 12 `sed_agent_*` ctests (real binary) | all Passed |
| F-02 ported | 4 `sed_agent_mcp_*` ctests (real binary over stdio) | all Passed |
| F-07 ported | `sed_core_tests` (`spatial-selection`) + `sed_agent_select` ctest | exit 0; Passed |
| F-13 ported | `sed_core_tests` (`dynamic-vertex-colors`) + agent plan/apply ctests | exit 0; Passed |
| F-14 adapted | 5 `sed_update_*` ctests (real binary) + `sed_core_tests` (`updater`) | all Passed; exit 0 |

No `ported`/`adapted` row rests on a GUI suite. All GUI-dependent remainders
(F-03 renderer, F-04 draw order, F-08 inspector, F-10 dialogs, F-12 dialog,
F-14 menu entry) are labelled **runtime-unverified** above and keep their
rows at `partial` (or, for F-14, `adapted` with the caveat stated).

## 5. Flags summary (claims exceeding evidence)

1. **F-05, F-08, F-12** — ledger "Present (core)" overstates: each has a
   named integration remainder (export entry point / `.sed` persistence /
   CLI-MCP exposure). Corrected to `partial` in §3.
2. **F-13** — ledger trailing sentence "there is no retention evaluator" is
   stale; the evaluator ships and passes. Status `ported` stands.
3. **F-01** — ledger "F-02 MCP exposure still missing" is stale; F-02 is
   present and its 4 ctests pass. F-01 status `ported` stands.
4. **F-09 / F-11** — engine acceptance remains **unverified**; no engine
   harness, oracle, or `setThingFrames` exists. Do not substitute
   screenshots or a retail executable.

This file is the handoff. Uncommitted only; fixtures LF; line endings
preserved.

## Addendum (parent, 2026-11-01)

- GUI runtime: `sed_app_tests.exe --group gui_wiring_utils` run by the parent with QT_PLUGIN_PATH set: 'DONE gui_wiring_utils failures=0' - that group's logic is now runtime-verified. Widget-level GUI suites remain unrun (cleave workspace test previously hung in this sandbox).
- Ledger reconciled: F-05/F-08 no longer read 'Present (core)' without their remainders; F-13 stale 'no retention evaluator' text corrected. F-01 row checked: no stale F-02 claim present.
- Remainder workflow (R1 .sed persistence, R2 agent exposure) hit provider rate limits three times with zero steps executed; those two items remain open and are tracked in PORT_LEDGER (F-03/F-08 .sed persistence; F-05/F-12 agent exposure).

## Addendum 2 (parent inline, 2026-11-01)

- R1 landed inline (workflow children were provider rate-limited): core optional-section codec `sed/assets/sed_extensions.*` (`lRgb` light RGB fields, `arcA` ARCH provenance) + app save/open wiring (file_menu/direct_sed_open_utils, compile-verified). Core tests: new `sed-extensions` group RUN, sed_core_tests exit 0; full build exit 0. Levels without RGB data emit no section (old bytes unchanged); count-mismatch/duplicate/truncated sections throw and apply nothing.
- R2 partial inline: sed-agent `setLightRgb` patch op (exit discipline preserved, null clears to inherited) + agent SED open/save now round-trips the extension sections. New ctest `sed_agent_light-rgb` passed; agent+MCP suite 17/17. Remaining: workbench/arch-validate command exposure in agent/MCP.
- Gates after R1+R2: full build exit 0, sed_core_tests exit 0, agent ctests 17/17.

## Addendum 3 (parent inline, 2026-11-02)

- Workbench/arch-validate exposure landed: agent commands `workbench-query` (exact queries, coded refusals at exit 5) and `arch-validate` (hash-stale model -> 4, unreadable topology -> 3); patch ops `batchUvProject`/`batchUvTransform`/`placeThingArray`/`placeLightArray` with core refusal codes (zero scale, count-300 array, empty filter all refuse; nothing truncates). MCP gained `sed_workbench_query`/`sed_arch_validate` by delegation only. New ctest `sed_agent_workbench` passed; agent+MCP 18/18, updater 5/5, sed_core_tests exit 0, full build exit 0.
- F-00 .ned verdict: JED-codec save/load/save byte-stable + identity preserved (jed_level_tests); byte-identity with an external NED writer is NOT claimed. Remaining F-00 gaps unchanged: no byte-exact no-edit JKL identity; unknown fields inside modeled sections still dropped.
- Session note: shell/disk stalls (C: full) were cleared by removing scratch build trees (alt-geo/mingw/ninja-*) and hung cmake processes; build/ninja is the only build tree.

## Addendum 4 (parent inline, 2026-11-02) — GUI per-entry ctest run

Scope note: since Addendum 3 the port was committed on
`codalilitari/features-port` (`2ad2535` docs, `d37f08b` core, `41aac93`
tools, `6dc9be1` app); §1's "no commits" statement is superseded.
Pushed 2026-11-02 to the new public repo
[CodaLilitari/SED-Qt-Port](https://github.com/CodaLilitari/SED-Qt-Port)
(branches `codalilitari/features-port` and base `develop` @ `b822559`).
A true second GitHub fork was impossible: `CodaLilitari/SED-MotS` is
already the account's fork of `smlu/SED`. The GitLab mirror
[codalilitari/SED-Qt-Port](https://gitlab.com/codalilitari/SED-Qt-Port)
was created by git push-to-create over HTTPS (same two branches, heads
verified equal) and later set **public** via the GitLab API once `glab`
auth was repaired (default branch `codalilitari/features-port`).

## Addendum 5 (provenance correction, 2026-11-02)

The F-00…F-14 inventory was built from Coda snapshot `f15639e`
(2026-09-30, local `SED-MotS-Net` `main`). That is **not** Coda's
current line. Measured from local refs (read-only, no fetch):

- `origin/claude` is 52 commits ahead of `f15639e` (tip 2026-11-01).
- `codex/linux-consolidated-20261101` is 70 ahead and contains `claude`.
- `codex/macos-consolidated-20261101` is 73 ahead and contains both
  (the +3 are macOS distribution/SDK commits).

So the port covers Coda's September state; roughly 70 commits of newer
work are **not yet inventoried, let alone ported**. Delta candidates
from commit subjects only (source not yet inspected; do not treat as
inventory): 16/32-bit MAT true-colour decode; master-palette texture
display; adjoined surfaces drawn only from in front of their plane;
retail mip fallback; Wren-style overview / orthographic / fullbright
snapshots (this may supersede the F-01/F-02 `snapshotRequiresRenderer`
refusal); a Caesar's Palace stray-parenthesis frame kept verbatim
(F-00-class preservation edge); non-GOB resource archives reported as
request errors instead of crashing; a retail JK capture tool under Wine
and a Linux engine profile for the capture runner (F-09-class); a
Blender JKL importer/preview renderer (external tool, likely
intentionally-different); native-inspection deadline work. The
Linux/macOS native-editor platform and packaging commits have no direct
Qt-rewrite counterpart (the rewrite is already cross-platform) and the
`jk-level-workshop` skill commits are documentation, not editor
features. A delta pass must re-derive this list from source diffs
against `codex/macos-consolidated-20261101` before any status changes.

## Addendum 6 (delta inventory, 2026-11-02)

The delta pass above is done: [DELTA_INVENTORY.md](DELTA_INVENTORY.md)
inventories all 73 commits (3 feature-new, 6 feature-extension, 12
platform, 5 tooling-external, 14 docs-skills, 33 replay-merge) as rows
D-01..D-17 with source-inspected contracts. Headline answers: the
snapshot refusal is **not** superseded (D-08/D-09 enlarge the F-01/F-02
gap; a headless render path is the missing piece); the F-09 engine gate
is intact (D-13/D-14 are runner contracts, no verified capture-v1
engine); two flags **invalidate ported assumptions** — D-10/D-11
(Caesar verbatim frame + writer guard) make F-00/F-06 incomplete, and
D-12 makes the port's non-GOB archive exit code diverge from Coda's
(3 vs 2). Proposed slice order: 1) D-10+D-11 preservation, 2) D-12 exit
codes, 3) D-04..D-07 MAT/renderer, 4) D-03 packages, 5) D-01+D-02
native-inspect, 6) D-08+D-09 headless snapshot/overview, 7) D-14
capture-runner (acceptance unverified). No port code changed yet.

GUI run: `ctest --test-dir build/ninja -R sed_app -E
sed_app_main_window_tests$ -C Debug --timeout 90` (per-entry flags into
`sed_app_main_window_tests.exe` plus `sed_app_tests` groups; log
`build/gui-ctest2.txt`, `Testing/Temporary/LastTest.log`).

- **Result: 48/79 passed, 31 failed, 3075 s.** Passing includes the
  port-adjacent `sed_app_cog_overwrite_lifetime`, `sed_app_native_save_edit_validation`,
  and the `sed_app_tests` groups run separately earlier (26/29 groups,
  `gui_wiring_utils failures=0`; `thing` group stack-overflows
  `0xc00000fd`, pre-existing upstream group).
- Failure clusters: orbit allocation x4 (known MSVC 19.51 baseline
  allocation family, see BASELINE.md), cleave x4 + 1 timeout,
  resource authoring x5, strict surface x2, navigation x2, plus single
  entries (toast timer, hold-modifier bindings, viewport mouse release,
  light-mode shortcuts, template collision, thing placement/collision,
  loose document watcher, UI decimal consumers) and the
  `sed_app_tests` monolith segfault.
- Port-adjacent failures examined individually:
  `sed_app_cog_shortcut_context`, `sed_app_cog_document_docking`,
  `sed_app_mots_arch_lighting`. Their implementation and test files are
  **untouched by the port** (`git diff b822559..HEAD` is empty for
  `src/app/src/main_window/cog/editor.cpp`,
  `src/app/src/main_window/events/event_filter.cpp` and the test files;
  the port's app changes are additive new dialogs/wiring plus four
  shortcut-less Tools-menu actions). `sed_app_cog_shortcut_context`
  reproduces alone (23.8 s) with focus/popup-dependent assertions
  (`focusWidget` requires a real active window in this non-interactive
  sandbox). Classification: **unresolved — suspected pre-existing /
  environment-bound, not attributable to port-changed code, and not
  port-verified.** A baseline per-entry A/B was not run: BASELINE.md
  records the baseline GUI run as partial in this sandbox (68/134
  passing, remainder not run, entry 69 hung >25 min), and C: free space
  (0.7 GB at the time) precluded a second build tree.
- No feature status changes result from this run: no `ported` row rests
  on a GUI suite (§4), and GUI-dependent remainders stay
  **runtime-unverified** pending a human interactive session.
