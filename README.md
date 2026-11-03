<p align="center">
  <img src="src/app/resources/red_app_128.png" width="96" height="96" alt="RED application icon">
</p>

<h1 align="center">RED — Rebel Editor</h1>

<p align="center">
  A modern, visual level editor for LucasArts’ Sith-engine games,<br>
  with reviewed automation, headless rendering, and preservation-first file handling.
</p>

<p align="center">
  <a href="https://github.com/CodaLilitari/RED/releases">Releases</a> ·
  <a href="docs/user_guide.md">User guide</a> ·
  <a href="docs/capability_matrix.md">Capability and acceptance matrix</a> ·
  <a href="NOTICE-RED.md">Modification notice and credits</a>
</p>

RED is a GPLv3 fork of the editor maintained by Crt Vavros (smlu) and contributors. It incorporates the later
C++/Qt/OpenGL rewrite, Coda Lilitari’s feature-port line, and RED-specific fixes and tools. RED is not affiliated with
or endorsed by Lucasfilm, LucasArts, Disney, or the upstream authors.

This README compares RED with the upstream ancestry at `c3b6c0` and describes the current source tree. It is a
capability-level inventory, not a claim of complete legacy-editor parity or acceptance in every game. The
[capability matrix](docs/capability_matrix.md) distinguishes implementation from structural, visual, and actual-game
evidence.

## Download and run

Windows Release packages, when published, are on the [RED Releases page](https://github.com/CodaLilitari/RED/releases).
Extract the complete package and keep its files together, then run:

- **`red-editor.exe`** — the visual editor;
- **`red-agent.exe`** — the reviewed JSON command-line workflow, when included in the tools package;
- **`red-mcp.exe`** — the local stdio MCP adapter, when included in the tools package.

Requirements for the Windows editor:

- 64-bit Windows 10 or Windows 11;
- OpenGL 3.3-capable graphics hardware and a current driver;
- your own game or project data for real game resources.

RED does not include retail game assets. Settings live in `red.ini` beside the editor. On first run, RED can import an
existing adjacent `sed.ini` without modifying it. Native projects retain the `.sed` extension for compatibility.

On first launch, create a project or open a level from the start screen. Configure the intended game installation,
resource roots, or archives when the project needs external assets. See [Start a project](docs/user_guide.md#1-start-a-project).

## Game and format support

| Project | Level formats | Resource discovery | Current boundary |
| --- | --- | --- | --- |
| **Jedi Knight: Dark Forces II (JKDF2)** | JKL, JED, native `.sed`; LEV/ASC import | Loose files, GOB, GOO | Full visual editing substrate; representative edited levels still need separate actual-game acceptance. |
| **Mysteries of the Sith (MotS)** | JKL, JED, native `.sed`; LEV/ASC import | Loose files, GOB, GOO | Adds mono/RGB source-light handling and architectural-lighting workflows; retail/OpenMotS behavior is not interchangeable. |
| **Indiana Jones and the Infernal Machine (IJIM)** | NDY, native `.sed`; JED import only | Loose files, GOB | Project-specific NDY, fog, sound, template, UV, Sector-light, and thrust handling; no IJIM JED export. |
| **Star Wars DroidWorks** | Experimental classic JKL and native `.sed` | GOB only: `dwMin.gob`, `dwHD.gob`, `dwCD.gob`, in that order | **Partial.** Distinct new projects, classic-JKL identification, inspection, editing infrastructure, recovery, RED-created map export, and imported-map reopen exist. Actual-game acceptance is unproven. |

### DroidWorks safety boundary

DroidWorks uses classic `VERSION 1` JKL syntax in this profile, with its own project identity, starter resources, player
Thing, and copyright identification. It does not offer JED or NDY export.

RED deliberately **refuses to re-export every imported DroidWorks JKL source** when the resource-table data needed for a
safe round trip is not preserved. This includes JKL files originally written by RED: copyright text identifies the
format, not safe export provenance. Keep a native `.sed` working copy instead. New RED-created maps can be exported,
and imported maps retain their identity when reopened, but neither observation establishes retail-game acceptance.
Unknown-field preservation, representative real-level round trips, resource-format coverage, packaging, launch, and
actual-game behavior remain open acceptance work.

## Feature comparison with upstream

The upstream baseline supplied much of the classic Sith/JED editing model. The C++/Qt rewrite and feature-port lineage
added the modern application and automation foundations. RED integrates those layers and adds its own product,
compatibility, validation, and builder work. “Available” below means implemented in this tree; it does not erase the
acceptance limits in the linked documentation.

### Editor, documents, and everyday workflow

| Capability | RED relative to the upstream baseline |
| --- | --- |
| **Modern application** | C++20/23/26-capable Qt application with an OpenGL 3.3 viewport, start screen, searchable/pinned recent files, project cards, and RED branding. |
| **Editor and Game Preview** | One editing viewport plus a separate read-only preview, textured/solid/wireframe/vertex rendering, perspective/orthographic cameras, fly/orbit/pan/dolly navigation, axis controls, and configurable LOD display. |
| **Native workspace** | Native `.sed` Save/Open preserves the Level, selection, cameras, tools, layers, window/dock layout, open COG/resource drafts, pending Multi-cut work, and optional Undo/Redo history. Engine export remains a separate operation. |
| **Recovery and history** | Atomic saves, debounced live checkpoints, autosave history, recovery after hard termination, compact saved history, checked metadata, bounded parity-assisted repair, forensic copies, and explicit degraded-recovery choices. Recovery is not a substitute for backups. |
| **Selection and layers** | Typed Sector/Surface/Edge/Vertex/Thing/Frame/Light selection, multi-selection, marquee and repeated-click cycling, X-Ray selection, hidden/locked-layer policy, filtered Outliner, and layer-aware placement. |
| **Clipboard** | Dependency-aware Sector, Thing, baked-light, and MotS ARCH clipboard transactions with fresh identities and Undo/Redo. Paste in Place and legacy Insert-style duplication remain gaps. |
| **Grids and snapping** | Transformed/adaptive grids, configurable appearance and visibility, grid/geometry/Thing snapping, surface alignment, Grid Stick, project-aware units, and rebindable held inverters. |
| **Transforms** | Previewed Move/Rotate/Scale with gizmos, local/world axes, numeric property scrubs, multi-selection pivots, UV preservation, and single-transaction commit/cancel. |
| **Accessibility and preferences** | Searchable keymap, mouse bindings, camera inversion, UI/code fonts, viewport themes and palettes, banner timing/colors, grid/gizmo controls, compact dark UI, high-DPI handling, and persistent per-project settings. |

### Geometry, UVs, Things, scripts, and resources

| Capability | RED relative to the upstream baseline |
| --- | --- |
| **Core geometry** | Sector/Surface cleave, Join, Merge, Adjoin/Unadjoin, Connect Surfaces, Extrude with Expand, sector/face/edge/vertex editing, and checked reciprocal-adjoin updates. |
| **Reusable shapes** | Interactive cube, cone, cylinder, UV sphere, and ico sphere creation, plus classic `shapes.tpl` shapes and saving a selected Sector as a shape. |
| **Multi-cut** | Persistent editable cut lines, bounded/unbounded cuts, shared endpoints, local history, execution ranks, transforms, snapping, and rectangle, N-gon, filled-arch, and star plans. |
| **Sector interchange** | Selected-sector OBJ/3DO import/export, contiguous closed-shell recognition, coordinate-system choices, hierarchy-transformed 3DO import, normalized/pixel UV handling, and atomic export. Arbitrary meshes are not automatically valid convex sectors. |
| **UV editor** | Docked multi-island Face/Edge/Vertex editing, transforms, geometry/grid/sub-grid snapping, Smart UV Project, Follow Active Quads, Auto Align, Stitch Ref/Chain, Pack, Fit, Repeat, tile grid, and optional view-aligned canvas orientation. |
| **Things and templates** | Surface/brush placement, resolved template inheritance, typed effective parameters, unknown/mod parameter retention, searchable flags, model/sprite previews, category editing, and project-specific Thing behavior. |
| **Thing frames** | Visible path overlays, Add/Duplicate/Delete/Move, surface and Thing snapping, visual-contact alignment, frame-property editing, and exact Undo/Redo selection remapping. |
| **COG authoring** | Multi-file docked editor, drafts and recovery, syntax highlighting, symbols/code outlines, diagnostics, completion, go-to-definition, formatter, snippets, schema-derived placed values, typed resource pickers, and safe atomic saves. It is not a COG runtime or compiler. |
| **Placed COG editing** | Typed bindings, guarded appends/reference edits, schema refreshes, hash-bound resource resolution, and diagnostics that preserve unknown states rather than guessing defaults. |
| **Episodes and objectives** | JKDF2/MotS episode sequence graphs, goals, COG strings, UNI handling, and JKDF2-only startup-COG generation/placement. |
| **Resource pickers** | Searchable COG, MAT, 3DO, KEY, WAV, AI, TPL, MSG, CMP, SND, PUP, SPR, and PAR pickers with loose/archive source identity, editable categories, thumbnails, model previews, and audio metadata/playback. |
| **Resource authoring** | Detachable SPR, SND, and UNI editors with project-aware fields, previews, local history, raw-draft preservation, checked engine-name limits, and Save All. |
| **Audio** | WAV and IJIM INDYWV preview, replay/loop/volume controls, camera-following Sector ambience, fades/crossfades, and independent Editor/Game Preview mute state. |
| **Asset handling** | Bounded readers for MAT/CMP, 3DO, KEY, SPR, SND, UNI, templates, categories, episodes, GOB/GOO, and related formats; bounded writers only where documented. General 3DO hierarchy and KEY timeline authoring remain external-tool territory. |

### Lighting, rendering, validation, and packaging

| Capability | RED relative to the upstream baseline |
| --- | --- |
| **Baked lighting** | Baked-light placement/editing, asynchronous dirty-region recalculation, live drag previews, optional Sector-ambient derivation, cancellable progress, and exact removal of stale vertex light. |
| **Runtime light preview** | Thing lights, actor/player headlights with rotated offsets, IJIM Sector point lights, range gizmos, Gouraud/per-pixel diffuse options, and deterministic scene-light limits. These previews do not rewrite baked colors. |
| **MotS lighting** | Independent mono/RGB source-light metadata, ARCH resource validation across LODs, ARCH authoring/baking profiles, model-vertex editing, checksum-bound provenance, and dynamic vertex-color readiness diagnostics. Full retail-baker equivalence is not claimed. |
| **Material/render compatibility** | Indexed and true-color MAT decode paths, cutout/translucency handling, hierarchy-preserving 3DO handling, master-palette resolution, short-later-mip fallback notices, and Game Preview adjoin-facing discard. Visual/runtime equivalence remains separately gated. |
| **Consistency checker** | Geometry, topology, adjoin, resource, template, COG, Thing containment, project-limit, and optional Sector-overlap checks, with searchable per-project settings and direct issue navigation. |
| **Sealed-seam validation** | `red-agent validate` reports cross-sector solid T-junctions as `sealed-seam`, naming both surfaces; exactly matching sealed walls remain valid. |
| **Query and workbench** | Exact filtered queries, spatial selection tokens, batch UV projection/transform, and bounded linear/grid/radial arrays of new Things or lights through shared core operations. The GUI exposes a narrower subset than the agent. |
| **Archives and packages** | GOB/GOO mounting, listing and exact-entry extraction, deterministic reviewed package plan/apply with manifest and output hashes, and explicit unresolved dependency scopes. This is not yet a complete GUI archive/dependency/play-test pipeline. |
| **Release staging** | Explicit Windows package target with Qt deployment, selected MSVC runtime, editor data, third-party notices, and a SHA-256 manifest. A staged layout still requires a clean-directory interactive launch before publication. |
| **Portable updater** | No-overwrite staged update design, version/hash checks, recovery handoff, and rollback-preserving behavior. The update feed is not independently signed. |

### RED automation, generation, and platform additions

| Capability | RED relative to the upstream baseline |
| --- | --- |
| **`red-agent`** | Local JSON inspect/query/select/validate/plan/apply/new/convert/archive/extract workflows, typed COG and lighting operations, package planning, native inspection, software snapshots, and overviews. Plans write nothing; apply uses fresh revision/hash-bound outputs and does not blindly overwrite. |
| **`red-mcp`** | A workspace-confined local stdio adapter over the same agent operations, with bounded requests, operation IDs, journaled outcome lookup, and no second domain-edit path. |
| **Native inspection bridge** | Hash/deadline-bound worker inspection of JKL and native `.sed` documents, typed outcomes, bounded input/output, and explicit project-kind checks. The current worker host is Windows-oriented. |
| **Software snapshots** | Qt-free `software-v1` PNG rendering with perspective/orthographic cameras, fullbright and world-backface options. Scope is **world surfaces only**: Things, models, sprites, and dynamic lights are excluded. |
| **Overviews** | Deterministic overhead and three-quarter framing, Wren/contain fits, bounded margins, and an embedded snapshot request for replay. Empty adjoin portals are skipped rather than drawn as grey walls. |
| **Engine-capture harness** | Hash-bound private staging, finite process deadlines, platform profiles, output manifests, and honest receipts. Synthetic fake-engine tests do not establish real engine capture or gameplay acceptance. |
| **Declarative `red-build` prototype** | `cellgen.py` builds convex cells, layered air/water/bridge geometry, adjoins, risers, sky/water surfaces, UVs/decals, strata, and baked sun/sky/point lighting from a plan, with geometry and seam checks. Integration as a native `red-agent build` command remains future work. |
| **DroidWorks profile** | New-project creation, classic-JKL identification, native persistence/recovery, agent/MCP/native-inspection identity, resource discovery, and conservative loss-prevention refusal described above. |
| **Cross-platform fixes** | Qt 6.4 compatibility, LP64 integer fixes, Mesa GLSL qualifier ordering, and POSIX CLI test exit-status/stdin handling. These fixes do not by themselves certify every Linux configuration. |
| **RED product and stability fixes** | RED executables/icon/settings migration, recovery path migration, Game Preview adjoin-backface fix, and a Preferences viewport-theme button lifetime fix. |

## Deliberate gaps and non-claims

RED does **not** currently provide complete parity with JED, JOKE, NJED, ZED, JKEdit, or the historical plugin ecosystem.
In particular, the following remain absent or incomplete:

- Paste in Place, legacy selection duplication, keyboard grid nudges, Build Sector/Build Surface, and some classic recovery commands;
- one-operation door, elevator, switch, water, breakable-glass, objective, and cutscene assistants;
- parameterized stairs, domes, swept/bent structures, heightmap terrain, and dependency-complete reusable prefabs;
- a safe general plugin API or loading of historical native DLL plugins;
- a complete in-GUI archive browser, dependency closure, GOB/GOO authoring, and launch-and-collect play-test workflow;
- material-emission/bounce/sun/sky baking, shadow-driven subdivision, and automatic source-light placement;
- general 3DO hierarchy and KEY animation editors, CND support, and LEV/ASC writers;
- the proposed gameplay overlays, AI sight-route preview, named saved-camera report set, and engine-matched horizon/ceiling sky preview described in the RED roadmap;
- automatic proof that an editor screenshot, parsed file, or successful export will run correctly in a retail or open-source engine.

The [capability matrix](docs/capability_matrix.md), [porting guide](docs/porting_guide.md), and
[project context](docs/project_context.md) track these boundaries and the remaining acceptance work.

## Development and source builds

### Requirements

- CMake 3.25 or newer for the included presets;
- a C++20-capable compiler, with C++23/C++26 used when available;
- Visual Studio C++ Build Tools and the Windows SDK on Windows;
- Ninja;
- a matching Qt 6 kit with Widgets, OpenGL, OpenGLWidgets, Concurrent, and Multimedia;
- OpenGL 3.3 support for the application and renderer tests.

Run these commands from the repository root in an x64 Visual Studio developer shell. `QTDIR` must name one exact Qt kit
containing `bin/qmake.exe`.

```powershell
$env:QTDIR = "<Qt MSVC kit directory>"
cmake --preset msvc-ninja
cmake --build --preset msvc-ninja-release --parallel 1
ctest --preset msvc-ninja-release
```

The default Windows application is `build/ninja/src/app/Release/red-editor.exe`. Run the complete Release suite on an
idle desktop: native GUI/input tests activate their own windows and must not disturb unrelated applications. Do not run
concurrent Ninja writers in one build tree.

To stage the Windows Release package explicitly:

```powershell
cmake --build build/ninja --config Release --target sed_release_package
```

The internal `sed_*` CMake and protocol identifiers remain for compatibility; they do not change the RED executable
names. See the [developer documentation](docs/README.md) for core-only builds, test ownership, package contents, and
release preparation.

### Repository map

- `src/core/` — portable Level model, formats, resources, geometry, validation, and software rendering;
- `src/app/` — Qt editor, OpenGL renderer, tools, preferences, and application tests;
- `tools/sed-agent/` — implementation of the `red-agent` executable;
- `tools/sed-mcp/` — implementation of the `red-mcp` executable;
- `tools/sed-cpp-bridge/` — bounded native-inspection worker;
- `tools/red-build/` — declarative level-builder prototype and canyon findings;
- `tools/engine-capture/` — isolated engine-capture runner;
- `data/` — redistributed editor data and template/category files;
- `docs/` — designer, format, architecture, acceptance, and developer documentation.

## Documentation and contributing

- [Level Designer Guide](docs/user_guide.md)
- [Architecture](docs/architecture.md)
- [Lighting contracts](docs/lighting.md)
- [Native format overview](docs/sed_format.md) and [normative binary format](docs/formats/sed_binary_format.md)
- [Capability and acceptance matrix](docs/capability_matrix.md)
- [Resource authoring](docs/resource_authoring.md)
- [Changelog](CHANGELOG.md)
- [RED modification notice](NOTICE-RED.md)

Contributions, tests, documentation improvements, and reproducible issue reports are welcome. Read
[CONTRIBUTING.md](CONTRIBUTING.md) and [AGENTS.md](AGENTS.md) before submitting changes. Report issues through
[RED’s GitHub Issues](https://github.com/CodaLilitari/RED/issues), not the upstream tracker.

## Credits and license

RED builds on work by Crt Vavros (smlu), Alexei Novikov and the Code Alliance, the upstream contributors, Coda
Lilitari, Muse Code, Red, Kimi Code, and the wider reverse-engineering and level-design communities. The RED icon was
designed with Lily (Lilitari). See [NOTICE-RED.md](NOTICE-RED.md) for the modification and attribution record.

RED is free software under the **GNU General Public License version 3**, with absolutely no warranty. See
[LICENSE](LICENSE) and [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). Qt and other third-party components retain
their own licenses and notices.
