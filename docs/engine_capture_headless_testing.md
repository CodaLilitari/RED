# Engine capture headless testing implementation plan

> **For agentic workers:** Use superpowers:executing-plans task by task. Preserve verification evidence for continuation.

**Goal:** Run synthetic capture-runner checks without Xvfb while retaining real display, process and identity contracts.

**Architecture:** Keep private X11 isolation for actual Linux engine captures. Substitute external display/engine
boundaries only in orchestration fixtures, exercise display ownership separately, and execute a real synthetic child
for Linux staging/process coverage. No renderer, public request field or acceptance flag is replaced.

**Tech Stack:** Existing standard-library Python runner, unittest/mock, CMake/CTest and existing CI jobs.

**Spec:** [Runner contract](../tools/engine-capture/README.md) and the design below.

## Design and approaches

The synthetic full-capture fixture replaces engine execution but inadvertently starts a real Xvfb server. A fake
engine that writes a PPM has no display requirement. The selected approach isolates both external boundaries in that
fixture and separately tests the real display-management implementation against controlled process/pipe inputs.
This preserves staging, hashes, commands, environment, image validation, receipts and failure behavior.

Installing Xvfb in every headless job would retain an unnecessary machine dependency and still fail where X-server
sockets are unavailable. Skipping the fixture would remove useful coverage. Implementing an EGL renderer is a separate
engine integration task: this runner has no verified offscreen capture-v1 engine. Neither is part of this correction.

The same boundary audit found three Linux lifecycle defects: a staged engine loses its execute bit, display setup
errors after server startup can escape cleanup, and process-creation failure can leak the readiness pipe's read end.
Fix those with regressions before claiming the headless runner is usable. Only the owned staged Linux engine receives
owner execute permission; source modes and ordinary data remain untouched. Display stderr handles close with the
owned process.

## Global constraints

- No Xvfb, display socket, GPU, game installation or retail asset is needed by the synthetic suite.
- Actual Linux captures retain private Xvfb, X11 SDL, dummy/null audio and isolated session paths.
- Do not use another application's display, add automatic backend fallback or skip failing tests.
- Preserve hashes, exact PPM dimensions/bytes, unchanged-input checks and existing request/receipt compatibility.
- Camera/resource observation flags remain false and acceptance remains unverified.
- Full configured headless CTest evidence is separate from Windows/Qt/native/game acceptance.
- Preserve prior checkpoints; publish only an authorized draft, never merge or release.

## Review focus

- Startup, setup and execution failures must release every owned display/process/pipe resource (Task 2).
- Fake display isolation must not hide incorrect real environment or manifest construction (Task 1).
- A real synthetic child must run from the staged Linux engine while data files remain nonexecutable (Task 2).
- Missing, malformed or wrong-size PPM and changed inputs must still fail (Task 1).
- Removing CI's Xvfb package must apply only to the Qt-free synthetic jobs (Task 3).

## Task 1: Isolate synthetic orchestration

**Files:** `tools/engine-capture/tests/test_invoke_engine_capture.py`.
**Interfaces:** Existing `run_capture`, `execute_engine` and `PrivateDisplay` remain unchanged.

- [x] Reproduce the original 9/10 synthetic test result and save its exact Xvfb failure.
- [x] Replace both external boundaries in the full-capture fixture; forbid accidental live server/process calls.
- [x] Assert command/environment, staged request/session, manifests, exact PPM, and unchanged acceptance fields.
- [x] Cover display-start refusal, engine exit/timeout, invalid images and input mutation without Xvfb.
- [x] Run the focused unittest suite and commit the test isolation.

## Task 2: Repair Linux resource ownership

**Files:** `tools/engine-capture/invoke_engine_capture.py`, its existing tests, and `tests/test_private_display.py`.
**Interfaces:** `stage_files` publishes hash-matching files; `PrivateDisplay` owns readiness descriptors/process/stderr;
`run_capture` owns that display through all Linux setup and execution paths.

- [x] Add executable-staging, post-start setup-failure and Popen-failure descriptor regressions; observe their failures.
- [x] Grant only owner execute on the owned Linux engine and extend cleanup to all acquired resources.
- [x] Cover valid/malformed/closed readiness, timeout/early exit, and terminate/kill cleanup without a live server.
- [x] Run focused tests with no Xvfb available; commit only after they pass.

## Task 3: Document, verify, review and preserve

**Files:** Runner README/CMake, `docs/README.md`, capability/gap ledgers, GitHub/GitLab Qt-free CI jobs.

- [x] Remove unnecessary Xvfb installation from only the Qt-free CI jobs and document the evidence layers.
- [x] Build all configured Release targets and run all CTest entries without exclusions.
- [x] Obtain a fresh whole-change review; resolve material findings with regression evidence.
- [x] Publish a separate draft on the verified dependency-graph parent and verify its exact tree.
- [x] Preserve execution/review evidence and update the continuation Page without replacing older checkpoints.

## Verification scope

All configured Linux GCC 14.2 / C++23 / Release targets built with Qt disabled and agent/MCP enabled. The complete
CTest run passed all 143 entries without exclusions. The focused capture suite passed all 23 tests with no skips on
Linux and no Xvfb on PATH. Independent whole-change review found no material issue and separately repeated the
23-test suite with DISPLAY and WAYLAND_DISPLAY unset. Production code and test bodies were unchanged afterward.

These results establish the headless contract gate. Native Windows/Qt execution, live Xvfb rendering, a compatible
capture-v1 engine and actual-game camera/resource/gameplay acceptance remain separate, unverified requirements.
