# Developer documentation

This directory contains the maintained technical and level-designer contracts for SED. The repository is C++26-first
with C++23/C++20 fallback; `src/core` is portable C++, while `src/app` owns Qt and OpenGL integration.

Start with [README.md](../README.md) and the document for the subsystem you are changing. AI- and LLM-assisted work
must also follow [AGENTS.md](../AGENTS.md).

For CLI/MCP level design, start with [RED agent workflow](agent_workflow.md). It distinguishes this C++ agent from the older .NET SED-MotS protocol and links the supported authoring contracts.

[Semantic change previews](semantic_preview_design.md) defines the bounded authored/persisted comparisons returned by
generic plan/apply, including snapshot addresses, exact scalar values, declared exclusions and incomplete results.

[Package node budgets](package_node_budget_design.md) defines mandatory episode/level accounting, distinct resource
admission, stock omissions and callback behavior at the limit.

[Dependency inspection](dependency_inspection_design.md) defines read-only CLI/MCP graphs from raw JKL and ordered,
hash-bound archives, physical-row provenance, supported reference families, independent scan/detail completeness and
the twelve operational limits. Its shared package readers retain separate traversal and publication policies; a
completed inspection does not prove runtime closure.

See [headless validation](headless_validation.md) for strict raw JKL framing and bounded seam diagnostics,
[bake output limits](bake_output_limits.md) for optional world-lighting policies and post-bake reports, and
[model lighting](model_lighting.md) for source-preserving 3DO lighting and import-normal controls, and
[material authoring](material_authoring.md) for ordinary image-to-MAT controls and publication limits.

[Camera poses and saved viewpoints](saved_cameras.md) covers numeric viewport controls, named editor viewpoints,
bounded pose admission, orthographic DPR scaling, and the shared native Save/recovery workspace codec.

[Model import](model_import.md) describes the ordinary OBJ/GLB conversion dialog, explicit material mappings,
captured input checks, and independent create-only model/image publication.
[Archive and package authoring](package_authoring.md) covers explicit resource archives and exported-level packages,
shared portable admission, ordered resource/stock inputs, full review reports and create-only publication.
[Installation checks](installation_check.md) describes the shared Help/CLI native deployment diagnostic,
isolated synthetic document, bounded child process, and source-bound frame/report evidence.

Path notation: inline repository paths are relative to this C++ project root (`cpp/` in the combined checkout); Markdown
links are relative to the document that contains them.

## Build and test

The build produces `sed_core`, `sed_app`, and their test targets. If Qt is unavailable, CMake still builds the core;
pass `-DSED_BUILD_APP=OFF` to request that explicitly.

The Qt-free gate includes the Python [capture-runner suite](../tools/engine-capture/README.md#tests).
Its synthetic engine/display fixtures need no Xvfb, GPU or game installation. Actual Linux capture defaults to
private Xvfb/X11 isolation and can explicitly select SDL offscreen with a required observed graphics report.
Game-acceptance requirements remain separate; see the [offscreen contract](engine_capture_offscreen_plan.md)
and the earlier [headless capture testing design](engine_capture_headless_testing.md).

For a Windows Qt Release build, use Ninja Multi-Config and set `QTDIR` to a compatible Qt kit. The included presets
use schema version 6 and require CMake 3.25 or newer; manual configuration requires at least CMake 3.21.

```powershell
$env:QTDIR = "<Qt MSVC kit directory>"
$env:QT_QPA_PLATFORM_PLUGIN_PATH = Join-Path $env:QTDIR 'plugins/platforms'
cmake --preset msvc-ninja
cmake --build --preset msvc-ninja-release --parallel 1
ctest --preset msvc-ninja-release
```

Replace `<Qt MSVC kit directory>` with one exact Qt kit directory on your system, which contains `bin/qmake.exe`.
Do not point `QTDIR` or `SED_QT_ROOT` at a parent directory containing multiple Qt kits; pass the intended kit root so
CMake cannot select a different version or ABI during discovery. The platform-plugin path lets deployed offscreen tests
load the selected kit's `qoffscreen.dll` when the test directory contains only `qwindows.dll`; each test retains its
configured Qt platform.

MSVC Release builds apply `/Brepro` to C++ executable and shared-library linking for the core and application targets.
Debug builds retain the existing Edit-and-Continue settings. Reproducible output still depends on identical source
inputs, build configuration, dependencies, and compiler/linker toolchain.

Do not run concurrent Ninja writers against the same `build/ninja` tree; use a different build directory for genuinely
parallel builds. The default Release executable is `build/ninja/src/app/Release/red-editor.exe`.

Without presets:

```powershell
cmake -S . -B build/ninja -G "Ninja Multi-Config" -DSED_QT_ROOT="$env:QTDIR"
cmake --build build/ninja --config Release --parallel 1
ctest --test-dir build/ninja -C Release --output-on-failure
```

Useful options:

- `SED_CPP_STANDARD=26|23|20` selects the preferred language mode.
- `SED_BUILD_APP=OFF` creates a Qt-free core build.
- `SED_BUILD_FUZZERS=ON` exposes the supported Clang/libFuzzer file-format target.
- `SED_APP_CONSOLE=ON` keeps a Windows console on the application target.

### Linux and macOS builds

On Linux and macOS, configure a single-configuration Release build against one Qt kit:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DSED_QT_ROOT=<Qt kit directory>
cmake --build build --parallel
```

The `Release builds` GitHub Actions workflow ([release-builds.yml](../.github/workflows/release-builds.yml)) builds
`red-editor`, `red-agent` and `red-mcp` with Qt 6.8.3 for every push to `red/main` and every version tag:

- `RED-<version>-linux-x86_64.tar.gz`, built on Ubuntu 24.04 and deployed with linuxdeploy, needs glibc 2.39 or
  newer. Run `./red-editor` from a writable location: settings are stored beside the executable.
- `RED-<version>-macos-arm64.zip` contains an ad-hoc signed, unnotarized `RED.app` deployed with macdeployqt. It needs
  macOS 13.3 or newer. Bundle launches store `red.ini`, logs and recovery outside the signed bundle in Qt's per-user
  application configuration directory. Existing bundled RED/SED settings can be imported once without modifying them.

Both jobs build the complete default target set and run the configured Release CTest suite before packaging.
Linux Qt tests use an isolated Xvfb/Openbox desktop with software OpenGL; macOS tests use the native Cocoa platform.
Those environments qualify their observed paths only: software OpenGL does not establish hardware-driver behavior,
and an editor/package test does not establish gameplay. Published acceptance remains tied to the exact source and
platform evidence in the release notes and capability matrix.

After packaging, both hosted jobs verify the actual extracted archive through the
[CLI/MCP acceptance gate](../tools/release/package_cli_acceptance.md) and the
[native installation gate](../tools/release/package_native_acceptance.md). The latter runs the same
installation check exposed by **Help > Check Installation**, independently decodes its two synthetic frames,
checks installed runtime data, and verifies that package bytes and the declared storage sentinels remain unchanged.
Receipts and native frame evidence are retained even when acceptance fails. These checks remain separate from the
ordinary interactive editing checkpoint and complete Release CTest suite; none alone establishes all GUI workflows.

Packages include the engine-capture Python runner and its instructions in `tools/engine-capture` beside the executables.
The macOS copy lives inside `RED.app/Contents/MacOS`; the bundle is signed after this content is staged. Python and a
capture-capable engine remain explicit external prerequisites. See [Engine Capture](game_testing.md#reproducible-engine-capture)
for the GUI and platform limits. Windows package staging includes the agent and MCP companions when those targets are enabled.

Parse floating-point text with `sed::text::fromChars` ([float_chars.hpp](../src/core/include/sed/float_chars.hpp))
rather than `std::from_chars` with `float` or `double`. libc++ before LLVM 20, which Apple toolchains ship, has no
floating-point overloads; there the helper converts exactly the standard pattern with a C-locale `strtod_l` or
`strtof_l`. Its tests compare that fallback with the standard overloads wherever both exist.

The complete Release gate includes `sed_core_tests`, `sed_app_tests`, `sed_app_main_window_tests`, isolated allocation
tests for document/resource operations, Frame transforms, geometry recalculation/transforms/duplication,
Cleave/Join/Merge and orbit resolution, plus
`sed_package_layout` and `sed_package_layout_tests`. Build all default targets before running the complete gate;
building only the ordinary test executables omits the isolated allocation executables. The real package-layout test verifies
the built `red-editor.exe`, required Qt runtime/plugins, and distributed editor data without launching the application. Its
companion module/system test exercises Windows Release/Debug, multimedia fallback, non-Windows, and missing, empty,
or directory-in-place-of-file branches with synthetic layouts. Run focused targets during iteration and the full CTest suite
before handoff. Tests must remain independent from personal settings, game installations, loose files, and previous
build output.

Foreground-dependent native GUI/input tests activate their own test windows and require an idle test desktop. Test
runners must not minimize, restore, reposition, or close unrelated applications. When the desktop is in use, run
background-safe checks and report native checks as pending; do not weaken guards or count a partial run as the full
Release gate. Release validation remains incomplete until the complete Release CTest gate passes.

Dedicated allocation executables inject failures only around the operation under test, keeping allocator
instrumentation out of ordinary core and application processes. They verify exact state, selection, timeline,
record IDs, output parameters, runtime geometry fields and retained recovery snapshots after rejected allocations.
Each sweep tracks whether the scheduled fault actually fired and reaches a successful run without an injected fault.

CTest labels describe the coverage layers rather than creating duplicate executables. Core tests carry `unit`, `module`,
`domain`, and `integration`; focused application tests add `cross_layer`, `cross_app_core`, and Qt coverage; MainWindow
tests cover module, domain, integration, cross-layer, and UI behavior; package checks carry module/system/package labels. See the
[core test guide](../src/core/tests/README.md) and [application test guide](../src/app/tests/README.md) for the ownership
matrix, checked-in vectors, focused groups, and rules for adding coverage.

## Runtime and icon assets

The Windows build and release package include the default 11-file runtime-data set from `data/`, including `shapes.tpl`
and each supported game's non-empty `.tpl` template database. The `ijimtpls.jlh`/`ijimtpls.jll` template-category
pair and `ijimwavs.jlh`/`ijimwavs.jll` WAV-category pair are optional user-authored or project-local data and are
omitted from the default package. Without an IJIM template category pair,
templates remain available through All and Thing placement uses type-based filters; the Template Editor hides its
category pane. Without an IJIM WAV category pair, discovered sounds appear under the generated `Sounds > Uncategorized`
category. JKDF2/MotS WAV categories are generated from discovered sounds when no project-local or mounted pair exists.
When a Thing category file is missing, empty, or invalid, the picker falls back to generated type-based categories (or
`Other`) so entries remain available. Tests reject a Release package that omits a supported project's non-empty
`.tpl` database.

Application icons and the embedded `eye.cur` fly cursor are owned under `src/app/resources`. Windows embeds
`app_flat_red_circle.ico` for the native shell/taskbar icon. Qt embeds the maintained PNG sizes and cursor through
`app_icons.qrc`, so normal startup has no filesystem fallback. `app_flat_red_circle.svg` and `app_window.svg` are the
editable masters for the two active icon families. Icon experiments and superseded variants are outside the maintained
source tree and are not part of the SED solution or Release gate.

Do not commit generated icon experiments, deployed Qt plugins, binaries, or build-tree copies of `data/`.

## Source publication and binary packaging

Publish the source files selected by Git, including tests, their synthetic fixtures, runtime `data/`, documentation,
and license notices. Local build trees, IDE state, logs, recovery files, test scratch files, and banner concepts are
ignored. Ignore rules do not remove files already tracked by Git; review `git status --short` and `git ls-files`
before publishing. Archiving the entire working directory bypasses these exclusions.

Keep living documentation free of dated status notes and developer-machine paths. Fixed synthetic paths and timestamp
values in tests are intentional contract vectors; public project/source links and required copyright/license text
are also intentional. Do not remove these to satisfy a blanket text search.

Review documentation images visually, including every animation frame, and inspect embedded metadata before
publication. Use neutral synthetic project and resource paths for captures; remove accidental private data and
developer checkout or installation paths from pixels and metadata while preserving required public credits.

The build output directory is a development workspace, not a ready-made release archive. Stage the application,
required Qt runtime/plugins, official app-local MSVC runtime DLLs selected for the build toolchain, editor data, and the
notices described in [THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md) into a clean package. Exclude test
executables, extra development tools/copies, local settings, logs, recovery files, and debug symbols from the public
runtime package. Validate the staged layout and launch the extracted package on Windows before distribution; passing
file-layout tests alone does not establish interactive acceptance.

For a Windows Qt 6 MSVC package, configure with the selected Qt kit and invoke the explicit release target from this
C++ project root:

```powershell
cmake --build build/ninja --config Release --target sed_release_package
```

`sed_release_package` is intentionally excluded from ordinary builds. It stages `build/package`, copies the explicit
11-file editor-data set plus `LICENSE` and `THIRD_PARTY_NOTICES.md`, runs the selected imported Qt kit's
`windeployqt --release`, copies the selected app-local MSVC CRT set, and writes `RELEASE_MANIFEST.json` with relative
paths and SHA-256 hashes. It discovers the selected Qt 6 version's license, module attribution HTML, and SBOM files
from the selected kit/root and records that exact version in the manifest; it also requires the selected Visual Studio
`ThirdPartyNotices.txt` and `Redist.txt` when MSVC packaging is enabled. Missing or empty notice files fail the explicit
target before staging. On Windows, the staging pass queries the selected `windeployqt` dry-run mapping and excludes
outputs whose source is under the current `SystemRoot`; ICU files sourced from the selected Qt kit remain package-local.
The target does not infer redistribution permissions; source-delivery and complete license review remain a
maintainer/distribution gate.

For a release, set `SED_RELEASE_SOURCE_COMMIT` to the exact 40-character lowercase commit of the built candidate.
The manifest records it as `sourceCommit`; invalid values fail before replacing an existing stage. The target never
infers identity from Git or a dirty working tree. Older direct staging callers may omit `SED_PACKAGE_SOURCE_COMMIT`,
in which case the manifest leaves the field absent and does not claim source identity.

The low-level [`cmake/stage_release_package.cmake`](../cmake/stage_release_package.cmake) script remains available for
maintainers who need an explicitly supplied file set. Package discovery can be adjusted through the target's
`SED_RELEASE_QT_VERSION_ROOT`, `SED_RELEASE_QT_KIT_ROOT`, `SED_RELEASE_QT_DEPLOY_TOOL`,
`SED_RELEASE_QT_VERSION`, `SED_RELEASE_QT_REQUIRED_VERSION`, `SED_RELEASE_QT_LICENSE_FILES`,
`SED_RELEASE_QT_SBOM_FILES`, `SED_RELEASE_MSVC_NOTICE_FILES`, and `SED_RELEASE_MSVC_RUNTIME_FILES` CMake cache
variables. `SED_RELEASE_MSVC_VS_ROOT` can override the selected Visual Studio root used for the standard notice paths.
The target writes its resolved settings to a configuration-specific CMake script in the build directory;
complete notice lists are not passed through the Windows shell's command-length limit. The generated-target
regression checks long lists, literal path characters, spaces, and resolved target/configuration expressions.
`SED_RELEASE_QT_REQUIRED_VERSION` is an optional policy check; the selected imported Qt version remains the default and
is recorded in the manifest. Do not copy an unrelated system or old build-tree DLL to make the package pass. After
staging, inspect the manifest, extract the package into a clean Windows directory, and launch `red-editor.exe` there.

### Release preparation checklist

- Keep the source and binary deliveries separate. Publish the Git-selected source, tests and fixtures, active runtime
  data, maintained documentation, and license notices; stage the executable and its runtime files separately for a
  binary release.
- Preserve active resource masters and required companions, including the editable icon sources under
  `src/app/resources` and the checked-in README banner JPEG
  `docs/images/sed-readme-banner-no-logo-v5.jpg`. Keep experiments and superseded assets outside the maintained source
  tree and out of documentation links.
- Exclude from the source publication compiler artifacts such as object files and executables, build directories, IDE
  state, logs, recovery files, local settings, and personal or proprietary level samples. Archive obsolete experiments
  outside the maintained source tree and keep normal build and IDE workspaces ignored.
- Review the actual publication set with `git status --short` and `git ls-files`; ignored files that are already tracked
  still belong to the publication set until they are deliberately removed from tracking.
- Build a clean binary package with `cmake --build build/ninja --config Release --target sed_release_package`. The
  registered CTest package-layout checks and synthetic staging fixture cover layout and rejection contracts; inspect
  the actual staged package separately, then extract it to a clean location and launch it on Windows. A layout check
  alone does not prove interactive startup.

## Documentation map

- [RED level-design skill](../tools/level-design-skill/SKILL.md) - portable, source-aligned CLI/MCP authoring instructions and read-only installation probe.
- [user_guide.md](user_guide.md) - level-designer manual for projects, editing, selection, geometry, UVs, Things,
  lighting, COGs, top resource-group tabs/file tabs and Save All, layers, preferences, saving, checking, autosave, and recovery.
  Cleave covers Bounded workflows, Add Shape, shared endpoints, group transforms, editable cut ranks, and saved draft
  Undo/Redo with exact pending-cut restoration when undoing Apply. It also explains independent Sector/Surface draft
  retention, corner-angle guides, local keymap contexts, shape rotation and scaling by wheel, project-aware units,
  centered transformation readouts, and optional Sector Camera plane placement.
  Number-row edit modes and Shift-number submodes are listed beside their workflows. Synthetic UI sheets are
  linked beside the relevant Sprite, Sound Class, UNI, COG, and viewport instructions; COG resource pickers and
  symbol/script flag workflows are described separately.
- [project_context.md](project_context.md) - project context, implemented systems, deliberate behavior, and
  current limitations.
- [architecture.md](architecture.md) - runtime ownership, data flow, thread boundaries, and subsystem map.
- [algorithms.md](algorithms.md) - picking, transforms, snapping, grid, geometry tools, rendering, resource, UV, COG,
  and performance contracts.
- [lighting.md](lighting.md) - canonical authored, baked, runtime, GPU, and source-parity lighting reference.
- [model_lighting.md](model_lighting.md) - portable 3DO inspection/baking, CLI/MCP requests, import normals, and GUI integration contract.
- [porting_guide.md](porting_guide.md) - original-editor parity checklist and modernization roadmap.
- [porting.md](porting.md) - concise compatibility principles used when replacing legacy behavior.
- [sed_format.md](sed_format.md) - accessible overview of the native `.sed` format.
- [editor_state.md](editor_state.md) - authored Level and shared EDIT persistence inventory, application preferences,
  transient/presentation exclusions, and native/autosave/recovery degradation boundaries.
- [resource_authoring.md](resource_authoring.md) - detachable SPR/SND/UNI editors, project-backed controls, engine mode
  semantics, authored sprite geometry, source metadata, sound-column sizing, engine filename limits,
  exact-byte UNI keys/live external row counts, raw-draft preservation, and
  loose-file authoring contracts.
- [formats/sed_binary_format.md](formats/sed_binary_format.md) - normative `.sed` V1 through V4 binary contract.
- [formats/recovery_format.md](formats/recovery_format.md) - normative recovery/autosave container contract.
- [formats/key_format.md](formats/key_format.md) - engine-compatible KEY animation fields, sparse node identity,
  fractional timing, and refusal of lossy edits.

Living documentation must remain date-free and describe the current repository. Put durable architectural decisions
in the appropriate document rather than adding temporary session notes.
