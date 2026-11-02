# Rewrite map — what the C++/Qt SED already has

> Scope: inventory of the C++/Qt rewrite in this repository (branch
> `codalilitari/features-port`, HEAD `b822559` "Implement the new C++/Qt SED
> editor" on `e868c99` "Remove pascal project", itself the `smlu/SED` develop
> line). This is an original reimplementation informed by Sith/JED behaviour;
> nothing here copies C# code. Verdicts describe behaviour/contracts only.
> Every claim cites the file that implements it. Fixtures referenced are the
> synthetic/project-owned vectors under the test asset directories; no retail
> assets are used.

## Summary

| Area | Verdict |
| --- | --- |
| JKL / NDY (JKDF2, MotS, IJIM) read + write | Present |
| Native `.sed` project read + write (V1–V4) | Present |
| JED import + export, LEV / ASC import | Present (LEV/ASC import-only) |
| 3DO model load | Present (load-only) |
| MAT / CMP decode | Present (read/decode-only) |
| COG parsing + script editor | Present |
| Document model, undo, selection, layers | Present |
| Geometry editing (cleave, extrude, join, merge, transforms) | Present |
| UV editing + Smart UV Project | Present |
| Lighting bake + MotS ARCHLIGHTING + vertex colours | Present |
| OpenGL viewport, picking, Game Preview | Present |
| CLI / headless executable | Absent (core library is headless-testable) |
| MCP / agent interface | Absent |
| Auto-updater | Absent (manual release packages) |
| Pascal DLL plugin system | Absent (deliberate; see Known limitations) |
| Tests | Present (core + app CTest suites) |

## 1. File formats

### Level formats

| Format | Read | Write | Evidence |
| --- | --- | --- | --- |
| JKL (JKDF2/MotS text level) | Yes | Yes | [jkl.hpp](../../src/core/include/sed/assets/jkl.hpp) `loadFromJKL` / `saveToJKL`; impl [jkl.cpp](../../src/core/src/assets/jkl.cpp) |
| NDY (IJIM text level) | Yes | Yes (same JKL writer path) | Same `loadFromJKL`/`saveToJKL` with `ProjectType::IJIM`; tests [ndy_save_tests.cpp](../../src/core/tests/assets/ndy_save_tests.cpp) |
| `.sed` native project (binary V1–V4) | Yes | Yes | [sed.hpp](../../src/core/include/sed/assets/sed.hpp), [sed.cpp](../../src/core/src/assets/sed.cpp) (`saveToSED`/`loadFromSedBytes`); contract [sed_binary_format.md](../formats/sed_binary_format.md) |
| JED (legacy editor level) | Yes | Yes (JKDF2/MotS only) | [jed.cpp](../../src/core/src/assets/jed.cpp) `loadFromJED`/`saveToJED`; IJIM excluded — see [CHANGELOG.md](../../CHANGELOG.md) §0.4.0 Known limitations |
| LEV | Yes | No | [lev.cpp](../../src/core/src/assets/lev.cpp) `loadFromLEV` only |
| ASC (3D Studio ASCII sector import) | Yes | No | [asc.cpp](../../src/core/src/assets/asc.cpp) `loadFromASC` only |

JKL export options (include/exclude editor-only lights/layers sections,
compact text dialect, extra-light clamping, vertex welding, MotS ARCH
default omission) are declared in `JklSaveOptions` in
[jkl.hpp](../../src/core/include/sed/assets/jkl.hpp). Export capacity is
pre-computed without serialising via `inspectJklExportBuffers`.

### Model, material and resource formats (all under `src/core`)

| Format | Capability | Evidence |
| --- | --- | --- |
| 3DO (`.3do`, versions 2.1 pixel-UV / 2.3 normalised-UV) | Load only; hierarchy transforms, LOD/geoset selection, per-vertex `ColorF` | [3do.hpp](../../src/core/include/sed/assets/3do.hpp) `loadFrom3DO`, `Model3doVertex::color`; no `saveTo3DO` exists |
| MAT (`.mat` texture) | Parse + RGBA decode only (`MatFile::parse`, `decodeCelRgba8`); no writer | [mat.hpp](../../src/core/include/sed/assets/mat.hpp), [mat.cpp](../../src/core/src/assets/mat.cpp) |
| CMP (`.cmp` palette/colormap) | Parse | [cmp.hpp](../../src/core/include/sed/assets/cmp.hpp) |
| GOB/GOO archives | Read/mount via `GobArchive::read`; no archive creation | [gob.hpp](../../src/core/include/sed/assets/gob.hpp) |
| KEY, PAR, PUP, SPR, SND, WAV, TPL, JLH/JLL, episode `.jk`, UNI strings, `items.dat`, `models.dat`, DF2JK conversion | Parse (TPL/JLH/episode/UNI also serialise: `saveToEpisodeJk`, `saveToCogStringsUniText`, `saveToJLH`) | [key.cpp](../../src/core/src/assets/key.cpp), [par.cpp](../../src/core/src/assets/par.cpp), [pup.cpp](../../src/core/src/assets/pup.cpp), [spr.cpp](../../src/core/src/assets/spr.cpp), [snd.cpp](../../src/core/src/assets/snd.cpp), [wav.cpp](../../src/core/src/assets/wav.cpp), [tpl.cpp](../../src/core/src/assets/tpl.cpp), [jlh.cpp](../../src/core/src/assets/jlh.cpp), [episode.cpp](../../src/core/src/assets/episode.cpp), [items_dat.cpp](../../src/core/src/assets/items_dat.cpp), [models_dat.cpp](../../src/core/src/assets/models_dat.cpp), [df2jk.cpp](../../src/core/src/assets/df2jk.cpp) |
| Resource location (loose files + mounted archives, bounded reads) | Present | [resource_locator.hpp](../../src/core/include/sed/assets/resource_locator.hpp) |

A libFuzzer target for untrusted `.sed` bytes exists but is off by default:
[fuzz/sed_file_fuzzer.cpp](../../src/core/fuzz/sed_file_fuzzer.cpp), gated by
`SED_BUILD_FUZZERS` in [src/core/CMakeLists.txt](../../src/core/CMakeLists.txt).

### COG

- Core parses the `symbols` section only (schema/defaults for level COG
  instance values, locals/messages handling, vector-initialiser quirk):
  [cog_script.hpp](../../src/core/include/sed/assets/cog_script.hpp)
  `loadCogSymbols*`. It does not compile or execute COG.
- The app adds a full script editor on top: multi-file editing, diagnostics,
  completion, formatting, outline/symbol navigation:
  [cog_script_editor_widget.cpp](../../src/app/src/ui/cog/cog_script_editor_widget.cpp),
  [cog_script_diagnostics_utils.cpp](../../src/app/src/ui/cog/cog_script_diagnostics_utils.cpp),
  [cog/editor.cpp](../../src/app/src/main_window/cog/editor.cpp).

## 2. Document model

- Authoritative state is `sed::Level` (sectors/surfaces/vertices, things,
  templates, cogs, lights, layers, colormaps, materials):
  [level.hpp](../../src/core/include/sed/level.hpp) `struct Level`.
- Editor wrapper adds selection + dirty tracking + undo:
  [document.hpp](../../src/core/include/sed/editor/document.hpp)
  (`DocumentState`, `DocumentDirty`, `DocumentUndoHistory`, max 1024 live
  history entries), [undo.hpp](../../src/core/include/sed/editor/undo.hpp).
- Native `.sed` persists level + session (views, selections, layouts,
  open COG/resource drafts, optional undo history) with parity-based
  self-healing; autosave/crash recovery containers are specified in
  [recovery_format.md](../formats/recovery_format.md) and implemented in
  [document/recovery.cpp](../../src/app/src/main_window/document/recovery.cpp),
  [partial_level_salvage.cpp](../../src/app/src/ui/recovery/partial_level_salvage.cpp).
- App lifecycle/settings: [main.cpp](../../src/app/src/main.cpp) (GUI-only
  `QApplication` entry; no argument parsing), [app_settings.cpp](../../src/app/src/app_settings.cpp).

## 3. Editing operations (core algorithms in `src/core`, UI in `src/app`)

| Operation | Verdict | Evidence |
| --- | --- | --- |
| Cleave sectors/surfaces (incl. bounded multi-cut: rectangle, N-Gon, arch, star) | Present | [cleave.hpp](../../src/core/include/sed/level/cleave.hpp), [cleave.cpp](../../src/core/src/level/cleave.cpp); UI [cleave_options_overlay.cpp](../../src/app/src/main_window/viewport/cleave_options_overlay.cpp) |
| Extrude surfaces (wire/solid/inset/rounded modes) | Present | [extrude.cpp](../../src/app/src/main_window/surface/extrude.cpp), core tests [level_extrude_tests.cpp](../../src/core/tests/level/level_extrude_tests.cpp) |
| Join sectors / merge sectors & surfaces | Present | [join.cpp](../../src/app/src/main_window/sector/join.cpp), [merge.hpp](../../src/core/include/sed/level/merge.hpp) |
| Translate / rotate / scale / mirror / flip (sectors, surfaces, edges, vertices, things, frames) | Present | [transform.hpp](../../src/core/include/sed/level/transform.hpp); menus [transform_menu.cpp](../../src/app/src/main_window/menus/transform_menu.cpp) |
| Delete sector vertex / thing / light / cog (incl. force-delete recovery path) | Present | [edit.hpp](../../src/core/include/sed/level/edit.hpp) |
| Sector draw / primitive shapes | Present | [draw.cpp](../../src/app/src/main_window/sector/draw.cpp), [shapes.cpp](../../src/app/src/main_window/sector/shapes.cpp) |
| Clipboard copy/paste (sectors, things, baked lights) | Partial — no Paste-in-Place; no standalone surface/edge/vertex copy | [clipboard.hpp](../../src/core/include/sed/level/clipboard.hpp); limitation stated in [CHANGELOG.md](../../CHANGELOG.md) §0.4.0 |
| Undo/redo with labels, dirty-flag transaction | Present | [history.cpp](../../src/app/src/main_window/history.cpp), [undo_labels.hpp](../../src/app/src/main_window/detail/undo_labels.hpp) |
| Level consistency checks | Present | [checks.cpp](../../src/app/src/main_window/checks.cpp), [level_check_utils.cpp](../../src/app/src/ui/checks/level_check_utils.cpp) |

## 4. Selection

Present. Typed multi-selection (sectors, surfaces, surface-vertices, things,
frames, lights) with hash/equality refs:
[selection.hpp](../../src/core/include/sed/editor/selection.hpp);
picking/cycling/occlusion rules in
[gl_view_picking.cpp](../../src/app/src/render/gl_view_picking.cpp) and
[selection_geometry_utils.cpp](../../src/app/src/render/selection/selection_geometry_utils.cpp);
modifier semantics in
[selection_modifier_utils.cpp](../../src/app/src/ui/shortcuts/selection_modifier_utils.cpp).

## 5. UV

Present. Core owns coordinate-space conversion (texture-pixels ↔ normalised),
connected-surface UV and promotion:
[uv.hpp](../../src/core/include/sed/level/uv.hpp),
[connected_surface_uv.cpp](../../src/core/src/level/connected_surface_uv.cpp),
[uv_promotion.cpp](../../src/core/src/editor/uv_promotion.cpp).
App adds the docked multi-island UV editor, Smart UV Project, alignment,
stitching and packing: [uv/editor.cpp](../../src/app/src/main_window/uv/editor.cpp),
[smart_uv_project_utils.cpp](../../src/app/src/ui/uv/smart_uv_project_utils.cpp).

## 6. Lighting and rendering

- Baked lighting (sector ambients, vertex lights, regional rebake with
  influence spheres, async with progress callback):
  [lighting.hpp](../../src/core/include/sed/level/lighting.hpp)
  (`levelCalcLighting`, `levelCalcLightingNew`), app orchestration
  [light_bake.cpp](../../src/app/src/main_window/light_bake.cpp). Contract:
  [lighting.md](../lighting.md).
- Viewport: OpenGL 3.3 `QOpenGLWidget`-style view with textured/solid/
  wireframe/vertex modes, perspective + orthographic cameras and a separate
  read-only Game Preview: [gl_view.hpp](../../src/app/src/gl_view.hpp),
  [gl_view_render.cpp](../../src/app/src/render/gl_view_render.cpp),
  [viewport_camera.cpp](../../src/app/src/viewport_camera.cpp). System
  requirement (OpenGL 3.3) is stated in [README.md](../../README.md).
- Dynamic Thing lights/headlights and IJIM sector point lights render live;
  Game Preview does not simulate gameplay (stated limitation).

## 7. MotS-specific handling

| Feature | Verdict | Evidence |
| --- | --- | --- |
| `ProjectType::MOTS` project kind | Present | [level.hpp](../../src/core/include/sed/level.hpp) `enum class ProjectType { JKDF2, MOTS, IJIM }` |
| ARCHLIGHTING (per-3DO-vertex authored light) | Present — full authoring, bake, clipboard, staging, recovery | `MotsArchLightVertex{mono,red,green,blue}`, `MotsArchLightMesh`, `MotsArchLightObject`, `Level::motsArchLighting`, `Thing::archLightingIndex` in [level.hpp](../../src/core/include/sed/level.hpp); engine in [arch_lighting.hpp](../../src/core/include/sed/level/arch_lighting.hpp) / [arch_lighting.cpp](../../src/core/src/level/arch_lighting.cpp); UI [mots_arch_lighting_dialog.cpp](../../src/app/src/ui/lighting/mots_arch_lighting_dialog.cpp) |
| Vertex colours | Present in two senses: (a) 3DO mesh vertices carry `ColorF color` ([3do.hpp](../../src/core/include/sed/assets/3do.hpp)); (b) level surfaces/sectors carry `extraLight`/`ambient`/`tint` `ColorF` and tex-vertex baked light via `Surface::extraLight`, `Sector::ambient` in [level.hpp](../../src/core/include/sed/level.hpp). MotS ARCH vertices above are the dedicated vertex-colour path. |
| MotS 3DO hierarchy-node `0x2` special shading | Absent (documented gap) | [CHANGELOG.md](../../CHANGELOG.md) §0.4.0 Known limitations |
| `.sed` V3/V4 ARCH persistence | Present | version constants in [sed.hpp](../../src/core/include/sed/assets/sed.hpp) |

## 8. CLI / headless, MCP, updater, plugins

- **CLI/headless executable: absent.** `main()` always constructs a
  `QApplication` GUI ([main.cpp](../../src/app/src/main.cpp)); CMake defines
  only `sed_core` (library), `sed_app` (GUI, output name `sed`), test
  executables and the optional fuzzer ([CMakeLists.txt](../../CMakeLists.txt),
  [src/app/CMakeLists.txt](../../src/app/CMakeLists.txt)). The portable core
  is headless-*testable* (`sed_core_tests` needs no Qt) but ships no CLI tool.
- **MCP / agent interface: absent.** No MCP server, socket, or scripting API
  exists in `src/`; [AGENTS.md](../../AGENTS.md) is a contributor guide, not
  a runtime interface.
- **Auto-updater: absent.** Distribution is manual GitHub Releases plus a
  staged package target (`sed_release_package`, `RELEASE_MANIFEST.json` with
  SHA-256 entries) — see [docs/README.md](../README.md) §Source publication.
  No in-app update check was found in `src/app`.
- **Plugins: absent by design.** The Pascal DLL plugin API has no C++
  equivalent, Plugins menu, or developer mode
  ([CHANGELOG.md](../../CHANGELOG.md) §0.4.0 Known limitations).

## 9. Tests and how to run them

| Suite | Contents | Evidence |
| --- | --- | --- |
| `sed_core_tests` (+ `--pending-cog-import` CTest entry) | 69 test `.cpp` files: geometry, level ops, formats (JKL/JED/SED/3DO/MAT/COG/GOB…), synthetic vectors in `src/core/tests/assets/fixtures/sed_vectors/` | [src/core/CMakeLists.txt](../../src/core/CMakeLists.txt), [src/core/tests/README.md](../../src/core/tests/README.md) |
| Allocation-failure executables | `sed_document_allocation_tests`, frame/geometry-transform, cleave, MAT, extrude fault-injection CTest entries | [src/core/CMakeLists.txt](../../src/core/CMakeLists.txt) |
| `sed_app_tests` / MainWindow tests | 107 test `.cpp` files: cleave UI, COG lifecycle, MotS ARCH bake pipeline, recovery, UV precision, thing placement… | [src/app/tests/README.md](../../src/app/tests/README.md) |
| Package layout | `sed_package_layout` verifies built `sed.exe` + Qt runtime + 11-file `data/` set without launching the app | [verify_package_layout.cmake](../../src/app/tests/verify_package_layout.cmake) |

Run (from repo root, x64 Developer PowerShell; full gate in
[docs/README.md](../README.md) §Build and test):

```powershell
$env:QTDIR = "<Qt MSVC kit directory>"
cmake --preset msvc-ninja
cmake --build --preset msvc-ninja-release --parallel 1
ctest --preset msvc-ninja-release
```

Native GUI/input tests need an idle desktop; a partial run is not the
Release gate ([AGENTS.md](../../AGENTS.md) §Tests are mandatory).

## 10. Exact build configuration the repo expects

Source: [CMakeLists.txt](../../CMakeLists.txt),
[CMakePresets.json](../../CMakePresets.json),
[src/core/CMakeLists.txt](../../src/core/CMakeLists.txt),
[src/app/CMakeLists.txt](../../src/app/CMakeLists.txt),
[README.md](../../README.md) §Development.

| Item | Expected value |
| --- | --- |
| CMake minimum | 3.21 manual; 3.25+ for presets (preset schema v6) |
| Project | `SED 0.4.0`, `LANGUAGES CXX` |
| C++ standard | `SED_CPP_STANDARD` cache ∈ {26, 23, 20}; default 26 when CMake ≥ 3.28 else 23 (falls back to 23 if CMake < 3.28); `CMAKE_CXX_STANDARD_REQUIRED OFF`, extensions OFF; `sed_core` requires at least `cxx_std_20` |
| Qt | Qt 6 preferred (`Widgets OpenGL OpenGLWidgets Concurrent Multimedia`), Qt 5 fallback (`Widgets OpenGL Concurrent Multimedia`); located via `SED_QT_ROOT` cache path or `$env:QTDIR` (must be one exact kit containing `bin/qmake.exe`) |
| Generator/presets | `msvc-ninja` = Ninja Multi-Config, `binaryDir build/ninja`, `SED_CPP_STANDARD=26`, `SED_QT_ROOT=$env:QTDIR`; `msvc-ninja-cpp23` inherits with 23; build/test presets `msvc-ninja-debug` / `msvc-ninja-release` |
| Options | `SED_BUILD_APP=ON`, `SED_BUILD_FUZZERS=OFF` (Clang libFuzzer only), `SED_APP_CONSOLE=OFF`, `SED_SUPPRESS_REGENERATION` (ON under Visual Studio generators) |
| Targets | `sed_core` (Qt-free library), `sed_app` → output `sed.exe` at `build/ninja/src/app/Release/`, test executables, explicit `sed_release_package` staging target |
| Platform | 64-bit Windows 10/11, MSVC + Windows SDK, OpenGL 3.3; MSVC Debug uses Edit-and-Continue flags, Release links `/Brepro` |
| Runtime data | 11-file set from [data/](../../data) (`.tpl` databases, `.jlh/.jll` category pairs, `df2jk.lst`, `shapes.tpl`) staged beside the executable |
