# Launch lifetime and selected-wire review

This review records two bounded repairs in the RED integration work. Coda's `red_audit` Codex agent investigated and
implemented the changes; the coordinating agent executed builds and tests. The sibling `muse_route` Codex agent
independently reviewed the relevant source and ownership math. That review is distinct from external Muse Code review.
Receipts named below are retained with the coordinator's external validation artifacts; no game assets or private
machine paths are included here.

## Selected-wire framebuffer alpha

The original ARCH regression failed for both cached Face and rebuilt Edge selection over an opaque adjoin. The sample
was inside the existing required inward band (1.15212 physical pixels within 0.5–1.6), and supporting-face depth was
correct. Its Qt color readback was `(84,84,0,192)`. Disabling wire AA reproduced the failure without shader warnings;
moving the sample or weakening the yellow predicate would not have repaired the defect.

Temporary raw GPU readback established the transition at the same pixel:

| Pass | Raw RGBA |
|---|---|
| Before selected overlay | `(0,255,255,255)` |
| Visible selection | `(255,255,0,255)` |
| Occluded selection | `(255,255,0,192)` |

The straight-alpha occluded shader used alpha 0.55. Applying `SRC_ALPHA, ONE_MINUS_SRC_ALPHA` to the alpha channel
computed `0.55² + 1×0.45 = 0.7525`, corrupting the opaque destination alpha. RGB then exceeded alpha in Qt's premultiplied
framebuffer image. The GPU transition was measured directly; Qt's particular integer conversion was not separately traced.

[gl_view_render.cpp](../../src/app/src/render/gl_view_render.cpp) now uses separate source-over alpha factors
`ONE, ONE_MINUS_SRC_ALPHA` in the common wire setup and both occluded wire paths while preserving the RGB factors and
existing separate-factor state restoration. The [focused regression](../../src/app/tests/mots_arch_lighting_render_tests.cpp)
retains all color, three-point sampling, inward-distance, supporting-depth and alpha-adjoin controls, additionally requires
alpha 255, and repeats opaque Face coverage with AA disabled. Temporary per-pass and row-scan instrumentation was removed.

Executed red receipts include `integration-regressions-red.log`, `wire-aa-diagnostic.log`, and `integration-next-red.log`.
Native DPR 3 green is recorded by `wire-clipboard-integration-green.json/.log`: exit 0, five tests passed in 110.69s:
`sed_app_mots_arch_lighting`, `sed_app_wireframe_support_depth`, `sed_app_paste_in_place`,
`sed_app_clipboard_publication`, and `sed_app_entity_clipboard_routing`.

## Game-test owner lifetime

The new [workflow](../../src/app/src/ui/project/game_test_workflow.cpp) originally created parent-owned stack dialogs.
The [isolated regression](../../src/app/tests/game_test_lifetime_tests.cpp) deletes a valid heap QWidget caller during
setup, export, and staging. It does not delete the application's ordinary stack MainWindow and never executes a game.
Its only runtime files are synthetic, non-executable markers.

`launch-lifetime-red.json/.log` recorded successful compilation followed by heap corruption `0xc0000374` in 0.32s.
Guarded heap dialogs corrected setup ownership, but a same-binary repeated run exposed an additional failure:
`launch-lifetime-stability.log` passed once, then crashed on repetition 2 immediately after owner deletion inside the
progress dialog's synchronous Show event. The cancellation, setup-deletion and export-deletion cases had completed.
Deleting the owner still deleted its child while Qt was executing `show()`; an after-return pointer check alone was insufficient.

The staging progress dialog now has independent QObject ownership, application modality during staging, and scope cleanup
that closes it before deferred deletion. Owner checks cancel after UI and callback boundaries. The setup and dependency
review dialogs use guarded heap ownership, and the File action guards callbacks and post-return member access. On early
return, the worker future is destroyed before its owned temporary stage, preserving worker inputs until completion.

The regression requires no launch, no later snapshot callback, exact preservation of synthetic source bytes, removal of
the unlaunched stage, and no visible orphaned progress window. Flushed case diagnostics locate any process-level failure.
`launch-lifetime-final-stability.json/.log` records exit 0 and ten successive passes in 1.02s, using:

```text
ctest --test-dir <Release-build> -C Release -R ^sed_app_game_test_lifetime$ --repeat until-fail:10 --output-on-failure
```

`launch-final-focused.json/.log` additionally records exit 0, four tests passed in 8.05s: `sed_core_tests`,
`sed_app_game_test_workflow`, `sed_agent_archive-extract-launch`, and `sed_agent_launch_plan`.

## Acceptance boundary

These receipts establish the exercised native GL and synthetic Qt/core/agent contracts. The legacy GL fallback received
the same source correction but was not deliberately forced; a separate lower-DPI wire run is not recorded here. The
lifetime repair does not certify destruction inside the pre-existing MainWindow export implementation generally.

Actual RED-driven retail/OpenJKDF2 map loading and gameplay, and the complete integrated Release CTest gate, remain
separate acceptance work. A runtime baseline outside this workflow or operating-system acceptance of a process launch
does not establish those results. See the [game-testing contract](../game_testing.md),
[rendering contract](../algorithms.md), and [application test guide](../../src/app/tests/README.md).
