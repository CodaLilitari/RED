# Surface Planarize and D-06 Renderer Repair Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Expose legacy Surface Face Planarize as an atomic, undoable editor command and restore the D-06 Game Preview adjoin-backface contract without affecting Editor or Thing rendering.

**Architecture:** Add one canonical `surface.planarize` QAction to the existing Surface Edit menu, selection/action gates, and keymap. Its trigger handler revalidates the selection, applies the existing singular `levelFlattenSurface` operation to an undo candidate, rejects any failure without committing, and suppresses an unchanged Level so an all-flat selection is a true no-op. Add one shared render predicate for the existing fragment shader's negative-facing discard, including correction of the sorted Thing draw lambda's invalid batch variable.

**Tech Stack:** C++26, Qt 6 Widgets, OpenGL, CMake/Ninja Multi-Config, MSVC Release, existing `sed_app_tests` offscreen integration harness and CTest.

**Spec:** `docs/capability_matrix.md` rows P-06 and D-06; `docs/porting_guide.md` Planarize (`Ctrl+P`, Pascal `FlattenSurface`); `docs/port/DELTA_INVENTORY.md` D-06; `docs/algorithms.md` geometry and preview contracts; `AGENTS.md` transaction, test, and preservation requirements.

## Global Constraints

- Work in `C:/Users/Andrei/Documents/ChatGPT/SED-Qt-Port` on `codalilitari/features-port`; preserve the existing modified `src/app/tests/render_preview_utility_tests.cpp` and untracked `docs/capability_matrix.md`.
- Do not commit, push, publish, delete source material, add dependencies, or copy proprietary game assets/community levels into Git or build outputs.
- Use the existing `bool sed::level::levelFlattenSurface(Level&, SurfaceRef) noexcept`; do not introduce UV reprojection, rebaking, a plane-fitting alternative, a policy dialog, or a second viewport shortcut path.
- Every selected face succeeds or none is committed. A rejected operation leaves Level, selection, dirty state, undo, and redo unchanged. An unchanged result is a quiet no-op and must not truncate redo.
- Use `editor::DocumentDirty::LevelGeometry`, retain selected `SurfaceRef`s and active Surface, and preserve exact texture UV/scales, baked RGBA, flags, materials, adjoins, and ordered vertex indices.
- D-06 applies only to single-sided adjoined world surfaces in Game Preview. Zero normals and non-adjoined surfaces remain visible; Editor inspection and Things do not inherit this discard. Do not change sprite culling, translucent-adjoin behavior, or shader zero-normal handling.
- Use one Ninja writer per build tree. Set `QTDIR` to the confirmed `F:/Qt/6.8.3/msvc2022_64` kit and use the documented `msvc-ninja` presets. Do not treat offscreen checks as native GUI or game-runtime validation.
- No commits are part of this plan; Andrei explicitly prohibited committing/pushing in the containing goal.

## Review Focus

- A later face fails after an earlier face moved vertices: rollback the complete candidate and preserve history; Task 2's atomic-rejection test.
- An all-flat selection has a live redo branch: leave Level, selection, dirty state, and redo intact; Task 2's no-op test.
- Disabled QAction state is bypassed by direct signal emission: enforce every edit/mode/selection/lock condition in the handler; Task 2's rejection tests.
- The sorted Thing lambda has a different batch variable: use `currentBatch`, never copy the Level draw site's `batch`; Task 1's build and helper cases.
- Geometry changes must not silently reproject or relight authored face data: compare exact texture/light/topology fields after commit and Undo/Redo; Task 2's metadata tests.
- The goal requires eventual real-level and game-runtime evidence, which this UI/helper slice cannot provide: report those boundaries explicitly and record only evidence actually collected in Task 3.

---

### Task 1: Restore the D-06 Preview Predicate and Build

**Files:**
- Modify: `src/app/tests/render_preview_utility_tests.cpp` (retain the existing D-06 regression around lines 733-737)
- Modify: `src/app/src/render/mesh/render_lighting_utils.hpp`
- Modify: `src/app/src/render/mesh/render_lighting_utils.cpp`
- Modify: `src/app/src/render/gl_view_render.cpp` (three `u_single_sided` assignments, including the sorted Thing lambda around lines 5582-5586)

**Interfaces:**
- Consumes: `FF_RD_Adjoin`, the existing `gamePreview`/Level/Thing render context, and `const TriBatch* currentBatch` in the sorted Thing lambda.
- Produces: `bool sed::render::shouldDiscardAdjoinBackface(bool gamePreview, bool levelSurface, std::uint32_t faceFlags) noexcept`, returning `gamePreview && levelSurface && (faceFlags & FF_RD_Adjoin) != 0u`.

- [ ] **Step 1: Preserve and run the failing regression.**

  Assert the four existing cases: `(true, true, FF_RD_Adjoin)` is true; `(true, false, FF_RD_Adjoin)`, `(true, true, 0u)`, and `(false, true, FF_RD_Adjoin)` are false. Preserve the original red logs: missing `sed::render::shouldDiscardAdjoinBackface` in `render_preview_utility_tests.cpp:733-736`, and C2065/C2661 at `gl_view_render.cpp:5582-5584`.

- [ ] **Step 2: Add the predicate and route all three uniform assignments through it.**

  Declare it beside the other classic render helpers in `render_lighting_utils.hpp`, define it in the matching `.cpp`, and replace each boolean `u_single_sided` expression with the helper. At the sorted Thing call site use `currentBatch` (with its existing null guard), not undeclared `batch`. Leave the fragment shader's `< 0.0` facing test and zero-normal exception unchanged.

- [ ] **Step 3: Build the affected application and test targets.**

  Run `cmake --build --preset msvc-ninja-release --target sed sed_app_tests --parallel 1` with the confirmed Qt kit and MSVC environment.

  Expected: both targets compile and link; the former C2065/C2661 errors are gone.

- [ ] **Step 4: Run the focused renderer tests.**

  Run the `sed_app_tests` executable with `--group render_preview` using `QT_QPA_PLATFORM=offscreen` and the active kit's Qt plugin path.

  Expected: PASS, including all four D-06 cases and existing preview utility regressions. This establishes the boolean routing contract, not pixel/game-runtime equivalence.

### Task 2: Add the Atomic Planarize Command Regression-First

**Files:**
- Create: `src/app/tests/surface_planarize_action_tests.cpp`
- Create: `src/app/src/main_window/menus/surface_planarize_action.cpp`
- Modify: `src/app/tests/main_window_event_filter_test_main.cpp` (test declaration, normal sequence, `--release-precision`, and `--surface-planarize` dispatch)
- Modify: `src/app/CMakeLists.txt` (new source/test registration and focused CTest entry)
- Modify: `src/app/src/main_window.hpp` (`setupPlanarizeSurfaceAction(QMenu*)`, `QAction* planarizeSurfaceAction_`)
- Modify: `src/app/src/main_window/menus/edit_entity_menus.cpp` (`setupEditEntityMenus` Surface menu wiring)
- Modify: `src/app/src/main_window/edit/state.cpp` (`updateEditActions` availability)
- Test: `src/app/tests/editing_rejection_ui_tests.cpp` (reuse its offscreen/modal/selection/history patterns; modify only if a shared fixture genuinely belongs there)

**Interfaces:**
- Consumes: `document_.applyUndoable(...)`, `level::levelFlattenSurface(Level&, SurfaceRef)`, `assets::sedLevelsEqual`, `documentEditingAllowed()`, `selectionTouchesNonEditableLayer(...)`, `glView_->selectedSurfaceRef()`, `glView_->notifyLevelGeometryChanged()`, and the existing generic keymap.
- Produces: canonical `QAction` objectName `surface.planarize`, text `Planarize`, default `Ctrl+P`, in Edit > Surface; exactly one `Planarize Surface` history row for a changed multi-face selection.

- [ ] **Step 1: Register a failing UI regression that requires `surface.planarize`.**

  In `runSurfacePlanarizeActionTests()`, create the isolated `QApplication`/MainWindow fixture with `Qt::WA_DontShowOnScreen`, find `findChild<QAction*>("surface.planarize")`, and require it before exercising edits. Register the normal full sequence, `--release-precision`, and a focused `--surface-planarize` option plus matching CTest properties.

  Run the focused test before implementing the action.

  Expected: FAIL specifically because `surface.planarize` is absent; do not proceed with production code on any unrelated error.

- [ ] **Step 2: Implement the minimal canonical action and independently guarded trigger.**

  Add the action through `setupPlanarizeSurfaceAction(surfaceMenu)` and register the source/header changes. Require `documentEditingAllowed()`, Surface edit mode, Face submode (verify the existing enum member before use), a nonempty face-only selection, and no read-only/locked-layer target both in `updateEditActions()` and at trigger time. Set `setAutoRepeat(false)` and use the normal action/keymap mechanism, not a `GlView` key handler.

  Rerun the focused test. Expected: the action exists and the initial action lookup passes; subsequent behavioral assertions may expose the next expected red contract.

- [ ] **Step 3: Add success, preservation, and single-history tests.**

  Build synthetic nonplanar quad fixtures with authored, distinguishable `texVertices`, UV scales, baked vertex RGBA, face flags, materials, adjoins, and ordered vertex indices. Trigger the action for one and multiple selected Surface Faces; assert the expected first-vertex/authored-normal plane positions within the core operation's documented tolerance, exactly one history row for the batch, unchanged unselected geometry and metadata, preserved complete selection and active Surface, and a published geometry update through an existing notification/revision seam. Do not add a renderer counter just for this test.

  Exercise Undo and Redo with exact `assets::sedLevelsEqual` comparisons to the pre/post Levels, plus exact selection and active-Surface restoration. Assert default `Ctrl+P` and a changed binding through the existing keymap path.

- [ ] **Step 4: Implement the all-or-nothing candidate mutation and quiet no-op.**

  Snapshot the original Level and invoke singular `levelFlattenSurface` calls only inside `document_.applyUndoable(QStringLiteral("Planarize Surface"), editor::DocumentDirty::LevelGeometry, ...)`. Return `false` immediately when any selected face fails so `UndoHistory::apply` restores its saved candidate and does not clear redo. When every call succeeds, return `!assets::sedLevelsEqual(candidateBeforeMutation, mutatedCandidate)` to suppress an unchanged history commit. Commit only with `LevelGeometry`; preserve/republish selection after `glView_->notifyLevelGeometryChanged()`.

  On rejection show a warning titled `Cannot Planarize Surface` with text `Cannot planarize Sector <sector> / Surface <surface>: the face cannot be flattened safely. Repair its geometry or move its vertices closer to the face plane.` For an invalid trigger selection use `Select one or more editable Surface Faces first.` An all-flat no-op displays no warning.

- [ ] **Step 5: Add rejection, no-op, and no-history-corruption tests.**

  Use a valid face followed by an invalid/excessive-displacement face to prove the entire Level, selection, Redo branch, and dirty state are unchanged, and capture the exact actionable warning with a modal `QTimer`. Exercise empty, Edge, Vertex, mixed-family, locked-layer/read-only, and Game Preview rejection via direct `QAction::triggered` signal invocation as well as checking disabled state; none may mutate the document.

  Start an all-flat fixture with a live Redo branch, trigger Planarize, and assert exact Level/selection/dirty/history equality with no modal warning and no lost Redo. Assert successful edits mark only the intended Level-geometry dirty contract rather than Structure.

- [ ] **Step 6: Run focused tests and inspect the complete patch.**

  Run `--surface-planarize`, `--group render_preview`, existing core geometry tests, and default MainWindow tests. Rebuild `sed_app_tests` after each test/source registration change.

  Expected: all focused tests PASS; new behavior is reached through the actual QAction and document/history path. Review every changed line for unrelated edits and stale comments.

### Task 3: Verify the Deliverable and Update Living Contracts

**Files:**
- Modify: `docs/user_guide.md` (Surface command, default/remappable shortcut, plane and failure behavior)
- Modify: `docs/algorithms.md` (atomic batch/no-op semantics and preserved authored fields)
- Modify: `docs/porting.md`, `docs/porting_guide.md` (current legacy mapping and evidence boundary)
- Modify: `src/app/tests/README.md` (focused test invocation)
- Modify: `docs/capability_matrix.md` (P-06/D-06 evidence only)
- Modify only if a documented contract actually changed: `AGENTS.md`

**Interfaces:**
- Consumes: Tasks 1-2 behavior and actual command/test output.
- Produces: user/developer documentation that distinguishes unit/UI/build evidence from native visual and game-runtime evidence; no broader parity claim.

- [ ] **Step 1: Reconfigure and build the complete Release configuration.**

  Run, in the confirmed Qt/MSVC environment:

  ```text
  cmake --preset msvc-ninja
  cmake --build --preset msvc-ninja-release --parallel 1
  ```

  Expected: configuration succeeds and all default Release targets build without the D-06 compile errors. If the preset/cache differs, correct the documented kit environment rather than silently substituting another build.

- [ ] **Step 2: Run the complete Release CTest gate.**

  Run `ctest --preset msvc-ninja-release`.

  Expected: every registered required test passes, including the new focused Planarize test and its normal full-sequence coverage. Do not skip desktop-dependent tests or relabel offscreen results as native GUI/game-runtime checks. If a test fails, preserve and diagnose its output rather than weakening or deleting it.

- [ ] **Step 3: Update the documentation and capability evidence.**

  Document Planarize as Surface Face projection to the face's authored normal/first-vertex plane, its batch rollback and no-op behavior, retained UV/baked light data, warning, and remappable `Ctrl+P`. Record D-06's helper and render-routing coverage; leave pixel/runtime validation explicitly unverified unless actually run. Update only P-06/D-06 rows supported by the fresh evidence and preserve all other matrix investigations.

- [ ] **Step 4: Audit the delivered tree and report the bounded result.**

  Run `git status --short --branch` and `git diff --check`; inspect new/modified files and remove only artifacts created by these test/build runs that are not deliverables. Report exact commands/results, the files changed, and remaining native GUI, real-level, DroidWorks, and actual-game verification gaps. Do not mark the larger parity goal complete or commit the changes.

## Assumptions

- Native execution in this session is already authorized by Andrei's `drive the rest` / `start it`; no subagents will be launched.
- `docs/capability_matrix.md` and the D-06 test edit are prior work to build upon, not disposable changes.
- `levelFlattenSurface` is the intended compatibility algorithm; its existing tolerances and return-value safety limits remain authoritative.
- Selected Surface references stay stable under this operation, so retaining the selection is preferable to the selection clearing used by winding flip.
- The existing test seams can observe geometry publication without introducing renderer-only test instrumentation.
- This slice does not claim overall editor parity, runnable DroidWorks support, representative real-level round trips, or acceptance in an actual game.
