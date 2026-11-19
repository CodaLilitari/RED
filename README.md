<p align="center">
  <img src="src/app/resources/red_app_128.png" width="96" height="96" alt="RED application icon">
</p>

<h1 align="center">RED — Rebel Editor</h1>

<p align="center">
  A level editor for Jedi Knight, Mysteries of the Sith and Indiana Jones and the Infernal Machine,<br>
  with a command-line and MCP interface that scripts and AI agents can build levels with.
</p>

<p align="center">
  <a href="https://github.com/CodaLilitari/RED/releases">Download</a> ·
  <a href="docs/user_guide.md">Level designer guide</a> ·
  <a href="docs/agent_workflow.md">Agent workflow</a> ·
  <a href="CHANGELOG.md">What's new</a> ·
  <a href="NOTICE-RED.md">Credits</a>
</p>

RED edits the levels of LucasArts' Sith-engine games. You can build and texture rooms, light them, place Things and
scripts, and export a level the game loads. It reads and writes JKL, JED and NDY, and the native `.sed` project file.
The same core runs headless as `red-agent` and `red-mcp`, so a script or an AI agent can plan an edit, review it and
apply it without opening the editor.

RED is a GPLv3 fork of SED, the editor by Crt Vavros (smlu) and contributors, by way of its C++/Qt rewrite. It is not
affiliated with or endorsed by Lucasfilm, LucasArts, Disney or the upstream authors.

## What you can do with it

**Build rooms.**

- Draw sectors, cleave, join, merge and extrude them, and adjoin rooms into one level.
- Cut walls with persistent multi-cut lines.
- Generate stairs, domes, corridors, arrays and heightmap terrain from recipes.
- Close sealed-seam gaps with **Map > Conform Sealed Seams**.
- Use the native replacements for the classic JED plug-ins: Build Stairs, Fit and Adjoin, Make Underwater, Make
  Breakable Glass, Place Sector Lights and Remove Redundant Vertices.

**Texture and light them.**

- Edit UVs in a docked editor (Smart UV Project, Follow Active Quads, stitch, pack, fit).
- Bake light into corners, with live previews and undo.
- Bake fully lit materials as light sources, with one bounce and sky light.
- Follow the conventions of LadyEebs' JED Light Baker: suns that reach a room only through its sky, multiply
  lights, and optional angle shading and water absorption.
- Use Mysteries of the Sith's RGB source lights and architectural (Arch) lighting.

**Place Things and scripts.**

- Place Things from templates and edit their parameters and paths.
- Write COGs in a docked editor with completion, diagnostics and typed resource pickers.
- Check placed COGs with COG Preflight and COG Bindings.

**Make resources.**

- Create MATs from images, including a 16-bit MAT and its 8-bit twin from one request, with painted lamp glows.
- Build tinted colormaps and level master palettes.
- Import OBJ and GLB models as 3DOs.
- Import JO/JA Raven BSP scenes with explicit MAT mappings, source light and stock footstep classes through
  **Tools > Import JO/JA Map…** or the agent. See the [scene import guide](docs/jo_ja_import.md) for remaining authoring work.
- Edit SPR, SND and UNI files.
- `tools/model-fitting` takes a generated model, such as one from Tripo, through Blender to a 3DO small enough for
  JK.

**Test and ship.**

- Use **Save JKL & Test** to run a level in retail JK or OpenJKDF2.
- The consistency checker covers geometry, adjoins, templates, COGs and engine limits.
- Packaging builds a GOB with everything the level uses beyond the stock game. It refuses a model the game's loader
  would refuse, and warns about a model larger than retail has been seen to draw.

**Automate.**

- `red-agent` has 100 patch operations: geometry, surfaces, Things, lights, COGs and bakes.
- It also does validation, queries, snapshots and overhead overviews, packaging and asset tools.
- Every edit is planned first, writes nothing until applied, and publishes to a new file bound to the hash of what
  was reviewed.
- `red-mcp` exposes the same work to AI agents as 52 MCP tools.
- Snapshots can show the baked light and the placed models, so an agent can look at what it built.

## What's new

Highlights of the 0.7.0 source tree (full [changelog](CHANGELOG.md)):

- **Shadow cuts.** JK blends light between a surface's corners, so a shadow needs corners to land on. **Edit >
  Sector > Shadow Cuts** adds them where each light's view through the openings ends, or splits surfaces near lights
  to a size, before the bake; **Remove Shadow Cuts** merges them back. The agent's `shadowCuts` does the same.
- **JO/JA scene import.** Prepare a new JK GOB or MotS GOO from a Raven BSP, with the same conversion core in the
  editor, CLI and MCP. Review skipped effects, source audio gaps and collision limitations before creating the archive.
- **Saving preserves computed corners.** Coordinates that eight decimals would materially move use round-trip
  spelling. Values within the serializer's calculation-noise tolerance retain JED-style formatting.
- **Light Baker conventions in the bake.** Suns (light flag `0x02`, anchor `0x08`), multiply lights (`0x20` in JK)
  and two opt-in options: angle shading and light absorbed by water. **Options…** on the Light bake strip sets them,
  together with emission, bounce and sky light.
- **Engine-limit checks for models.** Packages refuse a 3DO with a mesh over the loader's limits (512 vertices,
  768 texture vertices, 512 faces). Model import and packaging flag any model over 512 faces in all.
- **Mirror floors, lamp cuts and painted glows.** A floor that reflects its room in retail JK; a rectangle cut into
  a wall for a fully lit lens; MATs with soft lamp glows painted in.
- **Colormaps.** Tinted colormaps, and level master colormaps that add a level's own colours to the 8-bit palette.
- **Better snapshots.** Baked light, colormap tints, placed models (with fully lit faces) and translucent faces.
- **A COG compiler.** RED reads a script the way retail JK or MotS loads it. It says whether the game will load
  the script, and finds what the game accepts silently: unknown verbs, wrong argument counts, missing labels, and
  full symbol or label tables. It is in the COG check dialog and the agent's `cog-compile`. See
  [`docs/cog_compiler.md`](docs/cog_compiler.md).
- **A COG runtime.** RED runs scripts on a simulated clock against a stand-in world built from the level: doors and
  lifts move along their frames, timers and messages fire, and the log shows what each script did. It runs the
  stock two-piece door as the game does. It is in the agent's `cog-run`. See
  [`docs/cog_runtime.md`](docs/cog_runtime.md).
- **Gameplay assistants.** Doors, elevators, switches, objectives and cutscenes from the selection, in one undo step,
  with a **Try it** that runs the result in the COG runtime first. Doors, lifts and switches use the stock scripts;
  objectives and cutscenes use scripts RED writes. **Thing > Assistants**, and the agent's assistant operations. See
  [`docs/gameplay_assistants.md`](docs/gameplay_assistants.md).
- **3DO hierarchy and KEY authoring.** Edit a model's node tree (names, parents, positions, rotations, pivots), start
  a KEY from a model, recompute a KEY's deltas the way retail KEYs have them, and check a KEY against its model.
  **Tools > 3DO Hierarchy…** and the agent's `model-hierarchy` commands. See
  [`docs/model_hierarchy.md`](docs/model_hierarchy.md).
- **A COG template library.** Lily's controllers for a player swoop, a rideable Kell, enemy swoops that wake, follow
  an exit route and chase, Tuskens riding Kells, and the Demolition vehicles and arena hazards; SOTE's space
  fighter, space gunner, jetpack and enemy classes. Each set lists the templates, models and sounds you supply. A Pursuit assistant places an enemy's controller from the selection. See
  [`docs/cog_templates.md`](docs/cog_templates.md).
- **More of the editor in the agent.** JED output, ASC and LEV input, room deletion, and the editor's transform,
  surface and sector commands. [`docs/agent_parity.md`](docs/agent_parity.md) tracks what is still editor-only.
- **`tools/model-fitting`.** Scripts that reduce, re-texture and export generated models within JK's budgets.

## Download and run

On the [Releases page](https://github.com/CodaLilitari/RED/releases), open the version you want and check its **Assets** for your platform. Not every version has a binary for every platform. If your platform is missing, use the [source build guide](docs/README.md).

When a package is available, use these requirements and launch instructions:

- **Windows:** 64-bit Windows 10 or 11, staged by the maintainer. Tools package: `red-agent.exe` and `red-mcp.exe`.
- **Linux:** x86_64, glibc 2.39 or newer (`RED-<version>-linux-x86_64.tar.gz`). Run `./red-editor` from a folder
  you can write to, since settings are kept beside it.
- **macOS:** arm64, macOS 13.3 or newer (`RED-<version>-macos-arm64.zip`). `RED.app` is ad-hoc signed but not
  notarized, so allow the first launch in System Settings > Privacy & Security (on older macOS, use **Open** from
  the app's context menu).

Extract the whole package and keep its files together. The Linux and macOS packages include `red-agent` and
`red-mcp`. You need OpenGL 3.3 graphics and your own copy of the game: RED ships no game assets.

On first run, create a project or open a level from the start screen, then point the project at the game's install
folder or its GOB files ([Start a project](docs/user_guide.md#1-start-a-project)). Settings live in `red.ini` beside
the editor; RED offers to import an existing `sed.ini` once, without changing it.

## Games

| Game | Levels | Resources | Status |
| --- | --- | --- | --- |
| **Jedi Knight: Dark Forces II** | JKL, JED, `.sed`; LEV and ASC import | loose files, GOB, GOO | Full editing. RED's own levels have run in retail JK 1.01 in both renderers. |
| **Mysteries of the Sith** | JKL, JED, `.sed`; LEV and ASC import | loose files, GOB, GOO | Full editing, plus RGB source lights and Arch lighting. |
| **Indiana Jones and the Infernal Machine** | NDY, `.sed`; JED import | loose files, GOB | Full editing, with IJIM's fog, sound, sector lights and thrust. No JED export. |
| **Star Wars DroidWorks** | classic JKL, `.sed` | `dwMin.gob`, `dwHD.gob`, `dwCD.gob` | **Experimental.** New maps can be made and exported. RED refuses to re-export a DroidWorks JKL it imported, because that format's resource tables are not yet kept safely; work in `.sed`. |

## Known limits

- **A level still needs a test in the game.** RED checks geometry, adjoins, references and engine limits, but a
  clean check is not proof that a level plays. Test in retail, in both the 8-bit and 16-bit renderers, or in
  OpenJKDF2. The [capability matrix](docs/capability_matrix.md) records what has been verified where.
- **Snapshots approximate the 16-bit renderer.** The 8-bit software renderer's light tables can look different.
- **Plug-ins run unsandboxed.** The Plugins menu runs JED `IJED` plug-ins as native code. The 32-bit bridge can keep
  a crashing plug-in from closing RED, but it doesn't contain one.
- **Not built yet:**
  - editing 3DO geometry and playing KEY animations back in RED (use Blender with the Blender-Sith add-on);
  - LEV and ASC export;
  - a COG runtime in the app. The agent runs scripts against a stand-in world, without physics, AI or weapons.
- **Not planned:** Pascal SED's plug-in developer mode and its Template Creator. Nobody is known to have used either.

## Where RED came from

| Stage | Source | What it brought |
| --- | --- | --- |
| **Pascal SED 0.3** | [`smlu/SED` `main` at `c3b6c0e`](https://github.com/smlu/SED/commit/c3b6c0e) | The refurbished Delphi port of JED: IJIM support, 24/32-bit MATs, Paste in Place, a rewritten 3D Preview, plug-in developer mode. |
| **C++ SED 0.4.0** | [`smlu/SED` `develop` at `b822559`](https://github.com/smlu/SED/commit/b822559) | The C++/Qt/OpenGL rewrite. Most of RED's editor is this stage. |
| **RED** | this repository | Coda Lilitari's feature ports, the automation tools, preservation and validation hardening, the DroidWorks profile, and the work below. |

| Area | From C++ SED 0.4.0 | Added or changed in RED |
| --- | --- | --- |
| **Editor and workflow** | Qt editor with an OpenGL 3.3 viewport and a separate Game Preview; native `.sed` workspace; recovery and autosave history; typed selection, layers, grids, snapping and transform gizmos; searchable keymap. | RED name and settings; Paste in Place and Duplicate (Ctrl+D); grid-relative Rotate, Mirror and Scale Along; `.bak` on save; Game Preview discards adjoin back faces as the engine does. |
| **Geometry and UVs** | Cleave, join, merge, extrude, adjoin, connect; shapes; multi-cut; OBJ/3DO sector interchange; the UV editor. | Conform Sealed Seams; recipes (stairs, domes, corridors, arrays, terrain); Fit and Adjoin, Make Underwater, Make Breakable Glass, Planarize, Fix Solid Surfaces, Fix Passable Adjoins and the other restored JED commands. |
| **Things, COGs and resources** | Templates, frames, the COG editor, episodes, resource pickers, SPR/SND/UNI editors, audio preview. | COG Preflight and Bindings; MAT, colormap and master-palette authoring; OBJ/GLB model import and model lighting; KEY timeline editing; the Plugins menu and 32-bit bridge. |
| **Lighting and rendering** | Baked lights with dirty-region recalculation, runtime light preview, MotS Arch lighting. | Emission, bounce and sky profiles; Light Baker suns, multiply lights, angle shading and water absorption; MotS RGB source lights preserved in JKL; mirror floors. |
| **Validation and shipping** | Consistency checker; GOB/GOO mounting. | Sealed-seam and corner-limit checks; GOB creation; reviewed package plans with dependency graphs and model limit checks; Save JKL & Test; a staged updater. |
| **Automation** | — | `red-agent`, `red-mcp`, software snapshots and overviews, engine-capture harness, `red-build` prototype, `tools/model-fitting`. |

## Building from source

You need CMake 3.25+, Ninja, a C++20 compiler (C++23/26 is used when available), Qt 6 (Widgets, OpenGL,
OpenGLWidgets, Concurrent, Multimedia; the release workflow pins 6.8.3) and OpenGL 3.3 for the editor and its
renderer tests.

On Windows, from an x64 Visual Studio developer shell, with `QTDIR` naming one Qt MSVC kit:

```powershell
$env:QTDIR = "<Qt MSVC kit directory>"
cmake --preset msvc-ninja
cmake --build --preset msvc-ninja-release --parallel 1
ctest --preset msvc-ninja-release
```

The editor is `build/ninja/src/app/Release/red-editor.exe`; the `sed_release_package` target stages a Windows
package. Linux and macOS builds, core-only builds and the test lanes are in the
[developer documentation](docs/README.md). The internal `sed_*` CMake names are kept for compatibility.

| Folder | Holds |
| --- | --- |
| `src/core/` | the portable level model, formats, geometry, lighting, validation and software rendering |
| `src/app/` | the Qt editor, OpenGL renderer, tools and their tests |
| `tools/sed-agent/`, `tools/sed-mcp/` | `red-agent` and `red-mcp` |
| `tools/model-fitting/` | Blender-side scripts that fit generated models to JK |
| `tools/red-build/` | the declarative level-builder prototype |
| `tools/engine-capture/`, `tools/sed-cpp-bridge/` | isolated engine capture and the native-inspection worker |
| `data/` | editor data, templates and categories |
| `docs/` | the designer guide, format, architecture and developer documents |

## Documentation

- [Level designer guide](docs/user_guide.md): the editor, task by task
- [Agent workflow](docs/agent_workflow.md), [agent parity](docs/agent_parity.md): scripting and AI agents
- [Lighting](docs/lighting.md), [model import](docs/model_import.md), [material authoring](docs/material_authoring.md),
  [package authoring](docs/package_authoring.md)
- [Capability matrix](docs/capability_matrix.md): what is verified, and where
- [Architecture](docs/architecture.md), [native format](docs/sed_format.md), [developer index](docs/README.md)

## Contributing, credits and license

Contributions, tests, documentation fixes and reproducible bug reports are welcome. Read
[CONTRIBUTING.md](CONTRIBUTING.md) and [AGENTS.md](AGENTS.md) first, and report issues on
[RED's tracker](https://github.com/CodaLilitari/RED/issues), not upstream's.

RED builds on work by:

- Crt Vavros (smlu);
- Alexei Novikov and the Code Alliance;
- the upstream contributors;
- Coda Lilitari, Muse Code, Red and Kimi Code;
- LadyEebs, whose JED Light Baker conventions RED's bake follows;
- Wren, whose prop pipeline shaped `tools/model-fitting`;
- the wider reverse-engineering and level-design communities.

The RED icon was designed with Lily (Lilitari). [NOTICE-RED.md](NOTICE-RED.md) is the full attribution record.

RED is free software under the **GNU General Public License version 3**, with no warranty. See [LICENSE](LICENSE) and
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md); Qt and other components keep their own licenses.
