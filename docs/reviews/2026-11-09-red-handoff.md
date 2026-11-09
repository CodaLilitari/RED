# RED review checkpoint for Red (Claude)

Coda: I am collaborating with Andrei. This handoff is for **Red, Andrei's Claude interlocutor**, to continue work on the **RED editor**. Thank you for picking it up. Andrei requested a checkpoint on GitHub and GitLab and a stop when Codex account usage reaches 10% remaining.

This is a development checkpoint, **not a release or a complete parity claim**. The default Qt Release build succeeds, but the complete native suite has unresolved failures. Do not discard tests, relax preservation guards, or reuse an earlier passing subset as final qualification.

## Start here

- Branch: `codex/red-parity-review`, based on `red/main` at `cc0b5b8193fe2d14e818056260534bb7fe7078c1` (RED 0.5.0).
- A fresh origin fetch confirmed that `red/main` still had that head. Upstream `smlu/SED` `develop` at `b82255998fb733c8e8c87104eb7c17f27b48376f` is fully incorporated. This does not qualify every preserved fork work branch.
- Preserved fork WIP commits remain separately unintegrated: `39130a7` sky, `92cc2a0` ops, `50bb36d` clip, `87471f2` cam and `216aa85` abi. Common changes overlap current main; do not blindly cherry-pick their entire diffs. The camera and verified-clipboard helper files deserve separate dependency and regression review.
- Read [AGENTS.md](../../AGENTS.md), [the repair report](2026-11-09-red-parity.md), [agent workflow](../agent_workflow.md), [capability matrix](../capability_matrix.md), and [gap register](../gap_register.md).
- The repository now carries a portable [RED level-design skill](../../tools/level-design-skill/SKILL.md). The installed general `jk-mots-level-workflow` was also updated to distinguish RED from the older .NET editor, retaining the latter's fallback.

## Repairs to preserve

The patch contains native optional-section/timestamp preservation and refusal of unsupported opaque edits; no-replace atomic new outputs; bounded source capture and final hash checks; immutable archive reads, cumulative resource budgets and duplicate-alias accounting; engine-compatible KEY fields/syntax/fractional timing with staged saves; portal-recipe and corner-light preservation; useful closed-room and geometry/material/entity/query adapters; strict numeric/JSON admission; and native append-ID/future-reference/unknown-record guards.

Software snapshots now skip geometry-mode-zero faces, use proper orthographic depth, and clamp projected bounds before integer conversion, refusing non-finite raster arithmetic. Six geometry recipes now share the unknown-retained-record admission used by simpler geometry edits.

The reported `COG Script Changed on Disk` dialog was a real production protection triggered by an invalid ordinary-save test fixture. Its fixture now supplies distinct baseline bytes and an accepted hash, with bounded unexpected-dialog handling and exact Save/Cancel checks. Do not remove the production conflict guard.

Native test repairs align expectations with documented current behavior: sole default placement shortcut `A`, the eight Tools commands, explicit re-entry into Vertex after topology reset, and structural history returning to Face while geometry-only movement preserves compatible component selection. Cleave fixtures normalize their camera scale by DPR while preserving all gesture thresholds. The decimal-policy suite restores its injected locale/settings; the tiny-number assertion permits the locale's exponent-marker case and verifies the nonzero numeric value.

The grouped resource tabs had a separate production icon defect: all four native tab icons were null despite valid dock icons. A workspace-owned public-Qt synchronizer and focused regrouping/ambiguity/lifetime coverage are included. It must not decode private Qt tab data or guess among ambiguous title matches.

## Evidence at the checkpoint

| Check | Observed result and limit |
| --- | --- |
| Complete Release suite, before the last native repairs | **184/199 passed, 15 failed**, exit 8, 1589.91 seconds. The MainWindow test completed in 852.73 seconds; it did not time out. |
| Rebuilt core/agent/MCP plus six focused native cases | **54/56 passed**. All 50 core/agent/MCP checks passed, including late recipe/raster guards. Cleave chain/connection, COG bulk-close and surface-history checks passed at native DPR3. Icons and the decimal sequence failed with discriminating diagnostics before their final repairs. |
| Eleven previously failing native groups at process-local `QT_SCALE_FACTOR=0.5` | **8/11 passed**. Remaining failures: ARCH paste, Thing Paste in Place, and four null grouped-tab icons. This is a DPR1.5 lane, not default-DPR3 acceptance. |
| Standalone Linux atomic publication | 14 assertions passed under existing WSL2 on tmpfs using the actual shared headers. Not a Linux editor or macOS gate. |
| Actual headless level exercise | All 19 CLI invocations succeeded; output reopened as three sectors with four directed reciprocal portal records and no structural validation issues. The produced world-only image was inspected. Placeholder resources, no player/gameplay claim. |
| Final handoff build and focused selectors | See the checkpoint result block below; do not infer success merely from implemented changes. |

<!-- HANDOFF_FOCUSED_RESULTS_START -->
The final default Release build passed (74.56 seconds). The final focused run passed **4/5 tests** in 20.52 seconds: grouped native resource icons, same-process decimal suite, surface selection history and the Python capture runner passed at native DPR3. The entity-clipboard routing test failed its two existing Thing-copy/paste assertions. Its exact-state portable codec control passed; mode, selected owner and binding preconditions passed; Copy and Paste events were consumed. Immediate MIME was empty; settled MIME advertised entity/text formats but returned zero payload bytes. This isolates the observed failure beyond basic codec and mode/selection setup; it does not yet prove a specific platform or handler cause. The full Release gate remains unqualified.
<!-- HANDOFF_FOCUSED_RESULTS_END -->

The default display was measured with Qt 6.8.3 `qtdiag`: native 4096×2160, logical 1365×720, DPR3. The process-only scale override produced logical 2731×1440, DPR1.5. No Windows display setting was changed. Several native fixtures request windows larger than the default logical work area; keep screen-clamping failures distinct from product behavior and from scaled-lane evidence.

## Next work, in order

1. **Classify the remaining clipboard failures.** Run `sed_app_entity_clipboard_routing`, `sed_app_paste_in_place` and `sed_app_mots_arch_lighting`. The first retains the original Sector/cross-document/Thing-light/Baked-light sequence, adds a portable exact-state codec control, and reports mode, focus, event consumption, selected owners and MIME/decode state. Do not blame a missing Thing light parameter: copying owning Things does not require one. ARCH paste fails promotion/Undo assertions; Thing Paste in Place fails exact-pose/one-Undo assertions. No cause has yet been established for those two failures.
2. **Verify the native icon and locale repairs.** Run `sed_app_resource_native_group_icons`, `sed_app_ui_decimal_suite_sequence` and `sed_app_surface_selection_history`. Preserve the explicit ambiguity/foreign-tab nonmutation checks and the Move-versus-Structure distinction.
3. **Re-run the complete Release gate after fixes.** The recorded full default-scale failure list was MainWindow, Cleave chain origin/connection/input routing/fly custom shortcuts/UV custom owner, MotS ARCH lighting, Paste in Place, Template collision preview, COG document docking, resource workspace/lifecycle/Sprite geometry, Thing collision cache and decimal consumers. Some subsequently passed focused checks as documented above; the aggregate gate has not been rerun successfully.
4. **Stage and launch an isolated package.** The package output must remain outside Git. Exact Qt 6.8.3 notice material was recovered from official versioned SDK/license archives into the external `qt-notices` evidence folder: 65 files, including all 56 required module attribution pages. Its `qt-notices-cache.cmake` supplies the existing `SED_RELEASE_QT_LICENSE_FILES` override. Archive checksums and extracted-file hashes are retained. Actual staging, deployed-runtime notice coverage, package audit and isolated launch remain unexecuted.
5. **Continue explicit parity work.** Legacy text-SED/companion migration, exact .NET envelopes/JSON Schemas and array fields, free vertex/fit-align adapters, ARCH bind/rebind/bake, independent RGB GUI controls, model-inclusive snapshots and the other ledger rows remain work. Platform/game acceptance is separate. Do not mark these complete because an adapter or source file exists.

Use the documented Qt/MSVC kit with an external build directory. Typical gates are:

```text
cmake --build <external-build> --config Release --parallel 2
ctest --test-dir <external-build> -C Release -R "^(sed_core_tests|sed_agent_)" --output-on-failure -j 1
ctest --test-dir <external-build> -C Release --output-on-failure -j 1
```

Keep one writer per source/build tree and serialize Git. Do not run native-input suites concurrently or interfere with unrelated windows. A focused failure is useful evidence; do not skip it to manufacture a green full gate.

## Reviewers and companion delivery

Antigravity reviewed bounded KEY source/engine context. Muse Code completed a bounded atomic-output-wrapper review through the repaired subscription-backed route; it did not review the full editor or execute its tests. Late competitor/throwing-callback regressions incorporate its useful suggestions. Review agreement is not runtime evidence.

Muse support in Review Hub is committed as `dcb2908` on `codex/muse-code-reviewer`, with [GitHub draft PR #1](https://github.com/CodaLilitari/review-hub/pull/1) and the [GitLab branch](https://gitlab.com/codalilitari/review-hub/-/tree/codex/muse-code-reviewer). Source-bound suites passed: 449 private tests and 445 public tests, one skip each. Private installed configuration is not part of the public source commit.

The coordinator's external evidence directory contains command receipts, source hashes, native failure logs, the actual three-room output, reviewer provenance and Qt notice receipts. Personal paths, credentials, game assets, installed runtime copies and generated outputs are intentionally excluded from this repository. A local companion handoff provides their exact locations to Andrei.
