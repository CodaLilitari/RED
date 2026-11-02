# Baseline (branch codalilitari/features-port @ b822559)

Toolchain: VS 2026 Professional (MSVC 19.51.36260, toolset 14.51), Qt 6.8.3
msvc2022_64 (F:\Qt), CMake 3.30 (F:\Qt\Tools), Ninja. Preset
`msvc-ninja-cpp23` (SED_CPP_STANDARD=23). Build via `build\msvc-env.bat`
(vcvars64 + QTDIR + pinned CMAKE_LINKER; the system PATH's FreePascal
`ld.exe` otherwise hijacks linking). Parallelism: keep `-j2` in this
sandbox — higher parallelism produced spurious Qt-header C1083s from
file-access contention; single-threaded and -j2 builds are clean.

## Portability fixes applied on this branch (uncommitted)

1. `src/app/src/gl_view.hpp`: add `#include <QOpenGLTexture>`
   (unique_ptr member; GCC/MSVC-transitive-include difference).
2. `src/app/tests/main_window_event_filter_tests.cpp`: capture
   `kSettingKeys` in the qScopeGuard lambda (GCC rejects, MSVC allowed).
3. `src/app/CMakeLists.txt`: `/bigobj` for `sed_app_tests`
   (C1128 section limit under VS2026).

## Results (2026-11-01)

- Configure: exit 0. Full build: exit 0, 367/367 targets.
- `sed_core_tests.exe` (repo-root working dir): **exit 0, all pass.**
  The initial 9 failures were a checkout artifact: fixture blobs are LF,
  this machine's `core.autocrlf` rewrote them to CRLF, and canonical export
  byte-compare failed. Fixed locally with `git config core.autocrlf false`
  + fixture restore (`i/lf w/lf`). Upstream has no `.gitattributes` guard;
  adding one is a candidate upstream patch.
- Allocation-failure suite (`sed_document_allocation_tests.exe` and the
  `sed_allocation_contract_*` / cleave / extrude / frame allocation ctest
  entries): **known-failing on this toolchain.** The exe aborts
  (-2147483645) and `--frame-parse-fault` exits 86 with "Allocation
  contract terminated instead of propagating failure". Allocation-failure
  injection does not propagate as the harness expects under MSVC 19.51.
  Not investigated further; treat as pre-existing baseline delta, not a
  port regression. Re-check if upstream CI (older MSVC) is the reference.
- GUI suites (`sed_app_main_window_tests`, `sed_app_tests`): individual
  entries passed under ctest's `windows` platform before test 69
  (`sed_app_cleave_workspace_persistence`) hung >25 min in this
  non-interactive sandbox; offscreen platform crashes it. GUI results are
  therefore partial/environment-bound here: 68/134 ctest entries observed
  passing, remainder not run to completion in this environment.

## Judgement rule for port slices

A slice is accepted when: build exit 0; `sed_core_tests` exit 0 from repo
root; the slice's new tests pass; no *new* failures versus this baseline
(allocation group and GUI-environment limits excluded by name).
