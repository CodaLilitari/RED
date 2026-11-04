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

RED's ancestry has three stages. This README attributes each capability to the stage that introduced it:

| Stage | Source | Summary |
| --- | --- | --- |
| **Pascal SED 0.3** | [`smlu/SED` `main` at `c3b6c0e`](https://github.com/smlu/SED/commit/c3b6c0e) | The refurbished Delphi port of JED. It added Indiana Jones and the Infernal Machine support, 24/32-bit MATs, Paste in Place, `sed.ini` settings, a rewritten OpenGL 3D Preview, and plugin developer mode. |
| **Upstream C++ SED 0.4.0** | [`smlu/SED` `develop` at `b822559`](https://github.com/smlu/SED/commit/b822559) | A ground-up C++/Qt/OpenGL rewrite that replaced the Pascal project. Most of RED's editor comes from this stage unchanged. |
| **RED** | This repository | A fork of upstream C++ SED 0.4.0 that adds the DroidWorks profile, automation tools, preservation and validation hardening, Coda Lilitari's ported features, and RED branding. |

This README describes the current source tree. It is a capability-level inventory, not a claim of complete
legacy-editor parity or acceptance in every game. The [capability matrix](docs/capability_matrix.md) distinguishes
implementation from structural, visual, and actual-game evidence.

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

RED inherits JKDF2, MotS, and IJIM support from upstream SED. IJIM support was introduced in Pascal SED 0.1.
DroidWorks is the only game profile that RED adds.

### DroidWorks safety boundary

DroidWorks uses classic `VERSION 1` JKL syntax in this profile, with its own project identity, starter resources, player
Thing, and copyright identification. It does not offer JED or NDY export.

RED deliberately **refuses to re-export every imported DroidWorks JKL source** when the resource-table data needed for a
safe round trip is not preserved. This includes JKL files originally written by RED: copyright text identifies the
format, not safe export provenance. Keep a native `.sed` working copy instead. New RED-created maps can be exported,
and imported maps retain their identity when reopened, but neither observation establishes retail-game acceptance.
Unknown-field preservation, representative real-level round trips, resource-format coverage, packaging, launch, and
actual-game behavior remain open acceptance work.

## Feature comparison with upstream SED

Each table compares a capability in upstream C++ SED 0.4.0 with RED's changes to it. **Unchanged** means RED
inherits the upstream capability as is, apart from RED naming. **RED only** means upstream has no equivalent. A
capability listed here is implemented in this tree, but the acceptance limits in the linked documentation still apply.

### Editor, documents, and everyday workflow

| Capability | Upstream C++ SED 0.4.0 | RED change |
| --- | --- | --- |
| **Modern application** | C++20/23/26-capable Qt application with an OpenGL 3.3 viewport, a start screen, and searchable, pinnable recent files. | RED name and icon, the `red-editor` executable, `red.ini` settings with a one-time `sed.ini` import, and separate RED recovery directories. |
| **Editor and Game Preview** | One editing viewport and a separate read-only preview. Textured, solid, wireframe, and vertex rendering; perspective and orthographic cameras; fly, orbit, pan, and dolly navigation; axis controls; configurable LOD display. | Game Preview discards the back faces of adjoined world surfaces, as the engine does. Editor views still show both sides. |
| **Native workspace** | Native `.sed` Save/Open preserves the Level, selection, cameras, tools, layers, window and dock layout, open COG and resource drafts, pending Multi-cut work, and optional Undo/Redo history. Engine export is a separate operation. | DroidWorks is accepted as a native project kind. |
| **Recovery and history** | Atomic saves, debounced live checkpoints, autosave history, recovery after hard termination, compact saved history, checked metadata, bounded parity-assisted repair, forensic copies, and explicit degraded-recovery choices. Recovery is not a substitute for backups. | LP64 integer-type fixes in the recovery-history statistics, and the legacy `<file>.bak` of the previous file on Save and Export (Preferences > General > Files). |
| **Selection and layers** | Typed Sector, Surface, Edge, Vertex, Thing, Frame, and Light selection; multi-selection; marquee selection and repeated-click cycling; X-Ray selection; hidden and locked-layer policy; filtered Outliner; layer-aware placement. | Unchanged. |
| **Clipboard** | Dependency-aware Sector, Thing, baked-light, and MotS ARCH clipboard transactions with fresh identities and Undo/Redo. Legacy Insert-style duplication is absent. | **Paste in Place** (Ctrl+Shift+V) and legacy-style **Duplicate Entities** (Ctrl+D). |
| **Grids and snapping** | Transformed and adaptive grids; configurable grid appearance and visibility; grid, geometry, and Thing snapping; surface alignment; Grid Stick; project-aware units; rebindable held inverters. | Unchanged. |
| **Transforms** | Previewed Move, Rotate, and Scale with gizmos; local and world axes; numeric property scrubs; multi-selection pivots; UV preservation; commit or cancel as one transaction. | Legacy grid-relative numeric transforms: Rotate about and Mirror across the construction grid, and **Scale Along** X, Y, Z, or the grid normal. |
| **Accessibility and preferences** | Searchable keymap, mouse bindings, camera inversion, UI and code fonts, viewport themes and palettes, banner timing and colours, grid and gizmo controls, compact dark UI, high-DPI handling, and persistent per-project settings. | DroidWorks project rows, and a fix to a viewport-theme button callback that used a helper outside its lifetime. |

### Geometry, UVs, Things, scripts, and resources

| Capability | Upstream C++ SED 0.4.0 | RED change |
| --- | --- | --- |
| **Core geometry** | Sector and Surface cleave, Join, Merge, Adjoin/Unadjoin, Connect Surfaces, Extrude with Expand, sector/face/edge/vertex editing, and checked reciprocal-adjoin updates. | Unchanged. |
| **Reusable shapes** | Interactive cube, cone, cylinder, UV sphere, and ico sphere creation; classic `shapes.tpl` shapes; saving a selected Sector as a shape. | Unchanged. |
| **Multi-cut** | Persistent editable cut lines, bounded and unbounded cuts, shared endpoints, local history, execution ranks, transforms, snapping, and rectangle, N-gon, filled-arch, and star plans. | Unchanged. |
| **Sector interchange** | Selected-sector OBJ/3DO import and export, contiguous closed-shell recognition, coordinate-system choices, hierarchy-transformed 3DO import, normalized and pixel UV handling, and atomic export. Arbitrary meshes are not automatically valid convex sectors. | Unchanged. |
| **UV editor** | Docked multi-island Face, Edge, and Vertex editing; transforms; geometry, grid, and sub-grid snapping; Smart UV Project, Follow Active Quads, Auto Align, Stitch Ref/Chain, Pack, Fit, and Repeat; tile grid; optional view-aligned canvas orientation. | Unchanged. |
| **Things and templates** | Surface and brush placement, resolved template inheritance, typed effective parameters, retention of unknown and mod parameters, searchable flags, model and sprite previews, category editing, and project-specific Thing behaviour. | Unchanged. |
| **Thing frames** | Visible path overlays; Add, Duplicate, Delete, and Move; surface and Thing snapping; visual-contact alignment; frame-property editing; exact Undo/Redo selection remapping. | Unchanged. |
| **COG authoring** | Multi-file docked editor with drafts and recovery, syntax highlighting, symbols and code outlines, diagnostics, completion, go-to-definition, a formatter, snippets, schema-derived placed values, typed resource pickers, and safe atomic saves. It is not a COG runtime or compiler. | Generated COG registry tables used by the new COG preflight. |
| **Placed COG editing** | Create placed COGs from scripts or values, and edit their values. | **RED only:** COG Preflight and COG Bindings tools with typed bindings, guarded appends and reference edits, schema refreshes, hash-bound resource resolution, and diagnostics that keep unknown states instead of guessing defaults. |
| **Episodes and objectives** | JKDF2 and MotS episode sequence graphs, goals, COG strings, UNI handling, and JKDF2-only startup-COG generation and placement. | Unchanged. |
| **Resource pickers** | Searchable COG, MAT, 3DO, KEY, WAV, AI, TPL, MSG, CMP, SND, PUP, SPR, and PAR pickers with loose and archive source identity, editable categories, thumbnails, model previews, and audio metadata and playback. | Unchanged. |
| **Resource authoring** | Detachable SPR, SND, and UNI editors with project-aware fields, previews, local history, raw-draft preservation, checked engine-name limits, and Save All. | Unchanged. |
| **Audio** | WAV and IJIM INDYWV preview; replay, loop, and volume controls; camera-following Sector ambience; fades and crossfades; separate Editor and Game Preview mute state. | Unchanged. |
| **Asset handling** | Bounded readers for MAT/CMP, 3DO, KEY, SPR, SND, UNI, templates, categories, episodes, GOB/GOO, and related formats. | A checked 3DO text writer that refuses unrepresentable data. Stricter JKL and JED legacy-value checks. Retained unknown JKL sections and imported geometry for preservation-checked export. Checked MAT true-colour channel layouts. SHA-256 hashes for GOB entries. General 3DO hierarchy and KEY timeline authoring remain external-tool territory. |

### Lighting, rendering, validation, and packaging

| Capability | Upstream C++ SED 0.4.0 | RED change |
| --- | --- | --- |
| **Baked lighting** | Baked-light placement and editing, asynchronous dirty-region recalculation, live drag previews, optional Sector-ambient derivation, cancellable progress, and exact removal of stale vertex light. | Unchanged. |
| **Runtime light preview** | Thing lights, actor and player headlights with rotated offsets, IJIM Sector point lights, range gizmos, and Gouraud or per-pixel diffuse options. These previews do not rewrite baked colours. | Unchanged. |
| **MotS lighting** | Architectural-lighting (ARCH) baking and model-vertex editing. | Separate RGB source-light intensity, preserved through the `SED_SOURCE_LIGHTS` JKL extension. ARCH authoring profiles and resource validation across LODs, with an ARCH Inspector. Dynamic vertex-colour readiness diagnostics. Full retail-baker equivalence is not claimed. |
| **Material and render compatibility** | Indexed and true-colour MAT decoding, cutout and translucency handling, hierarchy-preserving 3DO handling, and master-palette resolution. | True-colour texels cut out below 0.5 alpha, notices when a later mip falls back to a shorter one, and the Game Preview adjoin back-face discard. Visual and runtime equivalence remain separately gated. |
| **Consistency checker** | Geometry, topology, adjoin, resource, template, COG, Thing containment, project-limit, and optional Sector-overlap checks, with searchable per-project settings and direct issue navigation. | Typed COG reference checks and DroidWorks precision settings. |
| **Sealed-seam validation** | None. | **RED only:** `red-agent validate` reports cross-sector solid T-junctions as `sealed-seam` and names both surfaces. Exactly matching sealed walls remain valid. |
| **Query and workbench** | Exact filtered level queries. | **RED only:** a Workbench with spatial selection tokens, batch UV projection and transforms, and bounded linear, grid, and radial arrays of new Things or lights through shared core operations. The GUI exposes a narrower subset than the agent. |
| **Archives and packages** | GOB/GOO mounting and resource discovery. | **RED only:** GOB/GOO listing and exact-entry extraction through `red-agent`, plus a deterministic reviewed package plan/apply with manifest and output hashes and explicit unresolved dependency scopes. This is not a complete GUI archive, dependency, and play-test pipeline. |
| **Release staging** | Explicit Windows package target with Qt deployment, the selected MSVC runtime, editor data, third-party notices, and a SHA-256 manifest. | Stages `red-editor.exe`. A staged layout still needs a clean-directory interactive launch before publication. |
| **Portable updater** | None. | **RED only:** `sed-update` with a no-overwrite staged update design, version and hash checks, recovery handoff, and rollback-preserving behaviour. The update feed is not independently signed. |

### RED-only automation, generation, and platform additions

| Capability | RED addition |
| --- | --- |
| **`red-agent`** | Local JSON inspect/query/select/validate/plan/apply/new/convert/archive/extract workflows, typed COG and lighting operations, package planning, native inspection, software snapshots, and overviews. Plans write nothing. Apply uses fresh revision- and hash-bound outputs and never blindly overwrites. |
| **`red-mcp`** | A workspace-confined local stdio adapter over the same agent operations, with bounded requests, operation IDs, journaled outcome lookup, and no second domain-edit path. |
| **Native inspection bridge** | Hash- and deadline-bound worker inspection of JKL and native `.sed` documents, typed outcomes, bounded input and output, and explicit project-kind checks. The current worker host is Windows-oriented. |
| **Software snapshots** | Qt-free `software-v1` PNG rendering with perspective and orthographic cameras and fullbright and world-backface options. It renders **world surfaces only**: Things, models, sprites, and dynamic lights are excluded. |
| **Overviews** | Deterministic overhead and three-quarter framing, Wren and contain fits, bounded margins, and an embedded snapshot request for replay. Empty adjoin portals are skipped rather than drawn as grey walls. |
| **Engine-capture harness** | Hash-bound private staging, finite process deadlines, platform profiles, output manifests, and honest receipts. Synthetic fake-engine tests do not establish real engine capture or gameplay acceptance. |
| **Declarative `red-build` prototype** | `cellgen.py` builds convex cells, layered air, water, and bridge geometry, adjoins, risers, sky and water surfaces, UVs and decals, strata, and baked sun, sky, and point lighting from a plan, with geometry and seam checks. Integration as a native `red-agent build` command is future work. |
| **DroidWorks profile** | New-project creation, classic-JKL identification, native persistence and recovery, agent, MCP, and native-inspection identity, resource discovery, and the conservative loss-prevention refusal described above. |
| **Platform fixes** | LP64 integer fixes in recovery statistics and POSIX stdin and exit-status handling in the agent CLI tests. These fixes do not certify every Linux configuration. |

## Deliberate gaps and non-claims

RED does **not** currently provide complete parity with JED, JOKE, NJED, ZED, JKEdit, or the historical plugin ecosystem.
In particular, the following remain absent or incomplete:

- one-operation elevator, switch-wired door, switch, water, breakable-glass, objective, and cutscene assistants;
- parameterized stairs, domes, swept/bent structures, heightmap terrain, and dependency-complete reusable prefabs;
- a safe general plugin API or loading of historical native DLL plugins;
- a complete in-GUI archive browser, dependency closure, GOB/GOO authoring, and launch-and-collect play-test workflow;
- material-emission/bounce/sun/sky baking, shadow-driven subdivision, and automatic source-light placement;
- general 3DO hierarchy and KEY animation editors, CND support, and LEV/ASC writers;
- the proposed gameplay overlays, AI sight-route preview, named saved-camera report set, and engine-matched horizon/ceiling sky preview described in the RED roadmap;
- automatic proof that an editor screenshot, parsed file, or successful export will run correctly in a retail or open-source engine.

Several of these gaps were features of Pascal SED 0.3 that upstream C++ SED 0.4.0 does not provide, so RED inherits
the gap rather than introducing it. They include the Plugins menu and plugin SDK, GOB project packaging, Save JKL and
Test, the 3DO Hierarchy editor, and the Template Creator. RED restores these Pascal SED 0.3 commands: Paste in Place,
Revert to Saved, Make a Backup Copy, the automatic `.bak` on save, selection duplication, Planarize, Calculate Angle,
grid-relative Rotate, Mirror, and Scale, keyboard Nudge along the grid normal, Fix Solid Surfaces, Fix Passable Adjoins, Build Surface or Sector, Delete Saved Shape, Make Door, and the
Move Things to Camera, Insert Thing at Camera, and Copy Camera as Frame helpers.

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
