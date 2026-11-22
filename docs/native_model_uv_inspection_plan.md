# Model Studio UV Inspection Implementation Plan

> **For agentic workers:** Use superpowers:executing-plans for the coordinated implementation and independent
> review. Steps below track the tested deliverables.

**Goal:** Make existing native model UV edits visually inspectable without changing their preservation contract.

**Architecture:** Extend the strict portable MAT snapshot decoder, then add an immutable UV canvas and an
asynchronous material inspection panel. ModelStudioDialog supplies its displayed snapshot and existing selection.

**Tech Stack:** C++23, Qt 6.8.3, existing ResourceLocator and bounded preview worker pool.

**Spec:** [Model Studio UV inspection](native_model_uv_inspection.md).

## Global constraints

Keep classic 3DO 2.1 native texture-pixel units and existing document/session/writer APIs. Read at most 16 MiB MAT,
1 MiB CMP, and decode at most 4,194,304 selected pixels. Keep one material image. Bound overlay traversal to
262,144 corners. Use source ordinals and immutable documents. No geometry editing, material rebinding, recovery
schema, per-frame material I/O or engine-rendering claim. All failures retain the numerical authoring session.

## Review focus

1. A shared UV row and a coincident seam must not select the same source corners.
2. A nonzero cel selector or unselected key must not substitute another texture's pixels or alpha.
3. A large finite negative/tiled UV must not overflow Qt coordinates or create unbounded repeated tiles.
4. A target/source/CMP change during capture must not install an obsolete image under the new label.
5. A missing material or palette must not leave old colors or invented dimensions looking authoritative.

## Task 1: strict material inspection

**Files:** `src/core/include/sed/assets/mat.hpp`, `src/core/src/assets/mat.cpp`,
`src/core/tests/assets/mat_cmp_tests.cpp`.

**Interface:** `MatPreviewKind { SolidIndexed, Indexed, TrueColor }`; `MatPreviewCel` with `kind`, int32
`width`/`height`, `solidColorIndex`, `keyed`, `indices` and `Rgba8Image rgba`.
`MatFile::inspectPreviewCel(std::span<const std::byte>, std::size_t maxPixels = 4194304)` returns it.

- [ ] Add tests for asymmetric 3x2 pixels, selector 1, shared texture, selected/unselected alpha, solid zero with
  absent dimensions, packed RGB565, malformed later records and refusal before an over-budget allocation.
- [ ] Run the focused core fixture and record the expected failure before production changes.
- [ ] Extend the strict snapshot walk, preserving ordinary and indexed snapshot behavior.
- [ ] Run core MAT, indexed snapshot and allocation regressions; review and commit the bounded decoder.

## Task 2: source UV canvas

**Files:** new `src/app/src/ui/resources/native_model_uv_preview.hpp/.cpp`,
`src/app/tests/native_model_uv_preview_tests.cpp`; registrations in `src/app/CMakeLists.txt`.

**Interface:** `NativeModelUvPreview` accepts an owning `NativeModelDocument`, `NativeModelSelection`, material
slot, optional QImage and explicit texture dimensions. It exposes a bounded list of selected source face/corner
addresses and read-only framing/UV-to-widget observations for tests. Clear and Reframe never mutate the model.

- [ ] Add a synthetic shared-row/seam/multiple-material/unused-row fixture and pixel/identity tests, then observe
  the new contract fail before implementing it.
- [ ] Draw one origin tile, material-specific outlines, selected-row markers and labeled positive U/V axes.
- [ ] Keep frame stable across proposals; clip segments and refuse over-budget traversal explicitly.
- [ ] Verify nonsquare texture orientation, missing dimensions, negative/extreme coordinates, unchanged snapshot
  bytes and scale-independent logical coordinates.

## Task 3: captured material panel and Studio integration

**Files:** new `src/app/src/ui/resources/model_uv_inspector.hpp/.cpp` and focused tests;
`model_studio_dialog.hpp/.cpp`, `main_window/menus/menu_actions.cpp`, Studio interaction tests and CMake wiring.

**Interface:** `ModelUvInspector::Context` captures immutable ResourceLocator, project master CMP name and the
GUI-only current-context callback. `setModel(document, selection, sourceGeneration, fit)` updates the immutable
display; `clear()` revokes pending capture. The widget owns material/CMP pickers, source/hash readout and canvas.

- [ ] Test actual controls with captured indexed/RGB565 MATs, missing CMP, explicit loose override and selected
  material slots; establish failures before adding implementation.
- [ ] Capture and decode in the bounded worker pool, with one active result and generation checks. Clear stale
  images immediately on changed source/options. Invoke context callbacks only on the GUI thread.
- [ ] Embed the panel in Studio's visual area; pass the current displayed document after selection, Preview,
  Cancel, Apply, Undo and Redo. Preserve all existing edit controls and model/Level history boundaries.
- [ ] Test stale completions and destruction, source replacement, palette-only reload, failed capture after a
  successful one, and dirty Level isolation. Verify shared-row movement and seam stability through the actual UI.

## Task 4: qualify and document

- [ ] Publish negative/tiled UV edits to two fresh variants and extend the independent readback procedure.
- [ ] Inspect the native panel at ordinary and 200% scale with owned fixtures; record actual screenshots.
- [ ] Update Model Studio guide, README, capability/roadmap and agent discovery guidance with exact delivered scope.
- [ ] Review the complete diff independently, run complete Release Windows/Linux gates on the final candidate,
  and retain source-bound receipts. Merge only the qualified tree; no new binary release is implicit.
