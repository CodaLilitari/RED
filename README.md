<p align="center">
  <img src="src/app/resources/red_app_128.png" width="96" height="96" alt="RED application icon">
</p>

<h1 align="center">RED — Rebel Editor</h1>

<p align="center">
  Build, texture, light, script and package Sith-engine levels.<br>
  Work in the editor, automate through CLI and MCP, and test the resulting package in the game.
</p>

<p align="center">
  <a href="https://github.com/CodaLilitari/RED/releases/tag/v0.8.2">Download</a> ·
  <a href="docs/user_guide.md">Designer guide</a> ·
  <a href="docs/agent_workflow.md">Agent workflow</a> ·
  <a href="docs/editor_comparison.md">Editor comparisons</a> ·
  <a href="docs/product_roadmap.md">Product roadmap</a> ·
  <a href="CHANGELOG.md">Changelog</a> ·
  <a href="NOTICE-RED.md">Credits</a>
</p>

RED is a free, open-source level editor for **Jedi Knight: Dark Forces II**, **Mysteries of the Sith** and
**Indiana Jones and the Infernal Machine**, with experimental DroidWorks support. It combines sector and UV editing,
asset creation, COG checking and simulation, campaign packaging, and target-specific playtesting in one workspace.
It can also convert supported Jedi Outcast and Jedi Academy scenes into JK/MotS content.

The Qt editor, `red-agent` command-line tools and `red-mcp` server share a portable level model and core editing
operations. RED builds on Crt Vavros (smlu) and contributors' C++/Qt rewrite of SED, itself descended from JED.
The [comparison below](#red-and-seds-develop-branch) distinguishes that foundation from RED's additions.

**This page describes the current source branch.** The published **0.8.2 previews** are an earlier build.
The [source-only changes](#source-and-release-status) are identified below; release notes record each download's
actual platform qualification.

## Download and start

| Platform | 0.8.2 preview | Run after extracting the whole archive |
| --- | --- | --- |
| Windows 10/11, x64 | [ZIP](https://github.com/CodaLilitari/RED/releases/download/v0.8.2/RED-0.8.2-windows-x64.zip) · [SHA-256](https://github.com/CodaLilitari/RED/releases/download/v0.8.2/RED-0.8.2-windows-x64.zip.sha256) | `red-editor.exe` |
| Linux x86_64, glibc 2.39+ | [Archive](https://github.com/CodaLilitari/RED/releases/download/v0.8.2/RED-0.8.2-linux-x86_64.tar.gz) · [SHA-256](https://github.com/CodaLilitari/RED/releases/download/v0.8.2/SHA256SUMS.txt) | `./red-editor` |

Both include the agent and MCP server. Use a writable installation directory, OpenGL 3.3 graphics and your own game
assets. On first run, open a level or create a project, then select the game installation or resource archives.
RED can import an existing `sed.ini` without modifying it.
[First project](docs/user_guide.md#1-start-a-project) · [Release qualification](https://github.com/CodaLilitari/RED/releases/tag/v0.8.2)

There is no published macOS package for 0.8.2. Build instructions and platform requirements are in the
[developer guide](docs/README.md).

## What RED does

Capabilities are organized by production task. **Editor**, **CLI/MCP** and **external helpers** have different
entry points; the linked guides describe their exact scope.

| Task | Tools and workflow | Start here |
| --- | --- | --- |
| **Build and repair geometry** | Cleave, extrude, join, merge and adjoin sectors; persistent Multi-cut; stairs, domes, corridors, arrays and heightmap recipes; sealed-seam conformance and reviewed adjoin repair. | [Designer guide](docs/user_guide.md) |
| **Work quickly in the viewport** | JED/NJED workspace preset, searchable commands and keymap, named selections and viewpoints, numerical repeat, layers, snapping and transform gizmos. | [Guide](docs/user_guide.md) · [Saved cameras](docs/saved_cameras.md) |
| **Texture surfaces** | Docked UV editor with multi-island transforms, Smart UV Project, Follow Active Quads, stitch, pack and fit; scoped batch UV operations. | [Guide](docs/user_guide.md) · [Workbench](docs/workbench.md) |
| **Light the level** | Compatibility bake with Light Baker sun/multiply conventions, emission, sky, angle shading and water absorption; manual ray bake with up to four diffuse bounces and optional editor GPU compute; MotS RGB/ARCH tools; saved ray recipes and owned Shadow Cuts. | [Lighting](docs/lighting.md) |
| **Create materials and palettes** | Indexed, keyed-cutout and RGB565 MATs, mipmaps and painted glows; tinted CMPs and master palettes; reusable paint recipes for separate 8-bit/16-bit editions; mirror-floor and lamp-cut recipes. | [Materials](docs/material_authoring.md) · [Lighting](docs/lighting.md) |
| **Prepare models and animation** | OBJ/GLB-to-3DO import, model lighting and loader-limit checks; hierarchy/pivot editing; KEY creation, timeline editing, delta repair, playback, marker navigation and LOD comparison; explicit export-loss refusal. | [Model import](docs/model_import.md) · [Hierarchy and KEYs](docs/model_hierarchy.md) |
| **Write and check COGs** | Docked editor, completion, typed resource pickers and binding diagnostics; JK/MotS compiler with target grammar, verb signatures and table-limit checks; scoped script simulation through CLI/MCP. | [Compiler](docs/cog_compiler.md) · [Runtime](docs/cog_runtime.md) |
| **Build gameplay systems** | Door, lift, switch, objective, cutscene and pursuit assistants; assistant Try it and message-trace inspection; reusable COG sets for vehicles, creatures and mission systems. | [Assistants](docs/gameplay_assistants.md) · [COG library](docs/cog_templates.md) |
| **Manage a campaign and its assets** | Project Cockpit, portable profiles, episode progression, ordered mounts, winning/shadowed resources, dependency inspection, typed previews and reviewed resource-family insertion; SPR/SND/UNI authoring. | [Projects and families](docs/portable_projects.md) |
| **Convert JO/JA scenes** | Raven BSP to JK GOB/MotS GOO; model scenes or eligible native-sector/portal geometry; source light, explicit material mappings and stock footstep classes; collision/PVS inspection, clip choices, attribute-aware optimization and reviewed reimport. | [Conversion guide](docs/jo_ja_import.md) |
| **Validate and package** | Geometry, reference, seam and corner checks; dependency-aware archive/package creation; per-mesh loader limits and separately labeled retail draw-size warnings; multiplayer material-headroom advisory. | [Validation](docs/headless_validation.md) · [Packaging](docs/package_authoring.md) |
| **Test the exact result** | Save JKL & Test; named-scenario Test from the open project; captured package/resource identities and engine receipts; external route-performance, two-peer and paired-edition checks. | [Game testing](docs/game_testing.md) · [Capture helpers](tools/engine-capture/README.md) |
| **Inspect static appearance** | CLI/MCP snapshots and overviews; default RGB preview and an opt-in JK indexed 8-bit world profile using original MAT indices, sector CMP light tables and resource-hash receipts. | [Snapshot workflow](docs/agent_workflow.md) · [Indexed profile](docs/lighting.md#jk-indexed-world-snapshots) |
| **Automate and review changes** | Schema-described CLI/MCP operations, bounded queries, semantic previews, reviewed plan/apply and create-only outputs; static snapshots/overviews; one live native material-edit review with Undo and draft preservation. | [Agent workflow](docs/agent_workflow.md) · [Semantic review](docs/semantic_preview_design.md) |
| **Recover work and reuse tools** | Native workspace, Undo/Redo and autosave recovery; journaled generated gameplay/campaign files; native IJED-compatible plug-ins and Windows architecture bridge; installable agent skills and a JK/MotS knowledge library. | [Native format](docs/sed_format.md) · [Skills](docs/agent_skills/README.md) · [Research](docs/knowledge/README.md) |

General level patches use the reviewed **inspect → plan → apply** workflow; specialized creation commands have their
own contracts. Agent coverage is broad, with [specific remaining gaps](docs/agent_parity.md).
The live **Review Agent Edit** dialog currently accepts a surface-material assignment, not arbitrary agent changes
to an open document.

**Lilivane authored the initial Bespin and Sith Council conversion tools.** Their production lessons helped shape
RED's JO/JA workflow. The [conversion credits](docs/jo_ja_import.md#engineering-sources) record the subsequent
map authoring, optimization and integration contributions.

## RED and SED's `develop` branch

The relevant upstream baseline is **C++/Qt SED 0.4.0 on `develop`**, inspected at
[`b822559`](https://github.com/smlu/SED/tree/b82255998fb733c8e8c87104eb7c17f27b48376f).
The latest published [Pascal SED 0.3.0](https://github.com/smlu/SED/releases/tag/v0.3.0) is a different implementation.
Most of RED's core editing interface comes from the C++ rewrite.

| Area | Already in SED `develop` | Added or extended in RED |
| --- | --- | --- |
| **Geometry and UVs** | Sector construction, OBJ/3DO sector interchange, shaped Multi-cut, docked multi-island UV editing and Smart UV tools. | Procedural recipes, seam conformance, restored classic commands, reviewed repairs and typed COG reference mapping for supported merges. |
| **Lighting** | Baked vertex lights, live Thing/headlight preview and MotS architectural lighting. | Light Baker conventions, emission/sky/bounce profiles, ray transport, saved recipes and owned Shadow Cut families. |
| **Scripts and gameplay** | COG editing, completion/diagnostics, templates and Thing frame paths. | JK/MotS compiler, scoped simulator, gameplay assistants, COG library and assistant trace review. |
| **Assets and animation** | Resource pickers, SPR/SND/UNI editors, audio preview and KEY reading. | MAT/CMP authoring, OBJ/GLB model import, hierarchy/KEY authoring and playback, LOD comparison and export-loss reporting. |
| **Projects and resources** | Native workspace, Undo/recovery, episode editor and mounted GOB/GOO resources. | Portable campaign profiles, resource families, reviewed dependency packaging and recoverable generated-file publication. |
| **Testing and automation** | Consistency checker and editor Game Preview. | Actual-engine launch/capture workflows, named-scenario receipts, CLI/MCP authoring and semantic review. |
| **Legacy plug-ins** | The C++ port's documented gaps include the old DLL host. Pascal SED already had a plug-in host. | IJED-compatible hosting and a Windows architecture bridge; ISED-only plug-ins remain unsupported. |

[Detailed comparison and upstream sources](docs/editor_comparison.md#sed-develop-and-pascal-sed)

## Other level editors

These are different workflows within the Sith ecosystem. The comparison credits their documented strengths;
**RED does not claim to have invented packaging, gameplay helpers, scripting, or engine testing**.

| Editor | Existing strength | How RED differs |
| --- | --- | --- |
| **JED** | Established JK/MotS editing, plug-ins, OLE automation, episode/package tools and Save JKL & Test. | Adds the C++/Qt workspace, integrated asset tools, structured CLI/MCP review and package-bound test evidence. |
| **ZED** | JED-derived editing and distributed fixes. | RED develops the broader editor/asset/agent workflow; this comparison does not assume every ZED feature was reproduced. |
| **NJED** | JED familiarity with mouse-look/WASD navigation and MAT thumbnail browsing. | RED adds project/resource provenance, content authoring and reviewed automation; it provides a JED/NJED workspace preset. |
| **JKEdit** | Textured construction, automatic doors/lifts, Test Level and multi-level episode tools. | RED adds its shared editor/agent core and reviewed resource/package workflow. Native JKEdit project migration is unverified. |
| **NED / Smith** | NED's JED-derived editing; Smith's JED/ZED camera synchronization, engine timing and COG/physics diagnostics. | RED binds source and package revisions to named target-engine scenarios. Smith observations are not retail-game acceptance. |
| **GeoSith** | Editing inside OpenJKDF2, host-approved multiplayer coediting and bounded direct 3DO mesh authoring. | RED offers a standalone document/CLI/MCP workflow. Live coediting and direct mesh editing are meaningful GeoSith capabilities that RED does not currently provide. |
| **Holocron** | Its [JK/MotS manual](https://jkjunkie.com/holocron/manual.html) documents camera-position playtesting and detecting and cutting walls where corner lighting loses a shadow or light pool. | RED offers reviewed engine runs and owned Shadow Cuts. [Workflow comparison](docs/editor_comparison.md#holocron) |

The [full comparison](docs/editor_comparison.md) gives sources and scope for every row, covers historical
**LEIA/ModelX**, and separates complementary **Urgon, Blender-Sith and specialist tools**.
It also compares RED's target workflow with **TrenchBroom, Radiant, DarkRadiant, Hammer, Unreal, Unity and Godot**:
those tools serve other engines; RED directly interprets Sith sectors, resources, COGs and episode packages.
There is no measured claim that RED is faster than every alternative.

## Games and interchange

| Target | Level workflow | Scope |
| --- | --- | --- |
| **Jedi Knight** | JKL, JED and native RED `.sed` | Editing, assets, COG tools and packaging. Named RED-authored/conversion fixtures have retail JK 1.01 observations in both renderers. |
| **Mysteries of the Sith** | JKL, JED and native RED `.sed` | Editing and packaging with RGB source lights and ARCH tools; target-specific qualification remains separate from JK. |
| **Indiana Jones and the Infernal Machine** | NDY and native RED `.sed`; JED import | IJIM geometry, fog, sound, sector lighting and thrust. No JED export or JK/MotS compiler guarantee for IJIM scripts. |
| **DroidWorks** | Experimental classic JKL and native RED `.sed` | Newly authored maps can export. Re-export of an imported DroidWorks JKL is refused until its resource tables can be preserved safely. |

ASC and Dark Forces LEV provide additional import paths, not export support for those games.
JO/JA conversion is a supported-scene import into JK/MotS, not a Raven BSP round trip or automatic mission-logic port.
[Format and acceptance details](docs/capability_matrix.md)

## Source and release status

Download published builds and their qualification notes from [RED releases](https://github.com/CodaLilitari/RED/releases).

Current source adds capabilities beyond those downloads:

- Import-dialog mesh optimization and JK world-model draw-pass controls; source PVS and player-clip tools.
- Keyed-material authoring refinements and paired texture-edition checks.
- Multiplayer material-headroom warnings, sampled whole-route comparisons and ray-bake handling across imported
  static-world render passes.
- The integrated JK/MotS research library.
- An opt-in indexed JK world snapshot profile through CLI/MCP, with explicit supported modes and resource provenance.
- A [portable native 3DO document and UV-edit API](docs/native_model_document.md) that retains all LODs and unrelated source bytes.

Release chronology belongs in the [changelog](CHANGELOG.md). The
[acceptance matrix](docs/capability_matrix.md) and each release's notes distinguish implementation, structural tests,
editor previews and actual-game observations.

## Boundaries that affect a project

- **Actual game behavior needs an actual game test.** Default snapshots approximate the 16-bit renderer. The
  [indexed JK world profile](docs/lighting.md#jk-indexed-world-snapshots) samples original MAT indices through sector CMP tables.
  Runtime dynamics, visibility and target-specific behavior still need game qualification. Ordinary-control Project Test currently uses
  Linux OpenJKDF2/OpenGL; retail launch and other capture profiles have separate [platform limits](docs/game_testing.md).
- **The simulator has a defined scope.** It models messages, timers and selected world behavior, not complete
  physics, AI, weapons or multiplayer. The GUI trace viewer is assistant-scoped.
- **Native Model Studio remains in development.** The portable source document supports checked UV edits;
  in-editor mesh/UV interaction, model history and publication remain [planned](docs/native_mesh_editing_proposal.md).
  Existing hierarchy/KEY tools and [Blender-side fitting helpers](tools/model-fitting/README.md) remain available.
- **Automation preserves unsupported state by refusing unsafe edits.** Some scripted topology operations and
  native-file edits remain restricted. Campaign packaging uses exported sibling levels; save COG/resource drafts
  before testing. Generated reusable asset files survive document Undo.
- **Plug-ins are native, unsandboxed code.** A helper can isolate a crash; it does not sandbox a plug-in.
  Platform and interface compatibility are described in the [guide](docs/user_guide.md).

## Build and contribute

Use CMake 3.25+, Ninja, a C++20 compiler (C++23/26 when available), Qt 6 and OpenGL 3.3.
From an x64 Visual Studio developer shell with `QTDIR` pointing to one Qt MSVC kit:

```powershell
$env:QTDIR = "<Qt MSVC kit directory>"
cmake --preset msvc-ninja
cmake --build --preset msvc-ninja-release --parallel 1
ctest --preset msvc-ninja-release
```

[Linux/macOS and core-only builds](docs/README.md) · [Architecture](docs/architecture.md) ·
[Contribution guide](CONTRIBUTING.md) · [Agent engineering rules](AGENTS.md) ·
[Report an issue](https://github.com/CodaLilitari/RED/issues)

RED credits Crt Vavros (smlu), Alexei Novikov and the Code Alliance, upstream contributors, Coda Lilitari,
Lilitari, Red, Lilivane, Muse Code, Kimi Code, LadyEebs, Wren, and the reverse-engineering and mapping communities.
See [NOTICE-RED.md](NOTICE-RED.md) for the full attribution record.

RED is distributed under the **GNU General Public License version 3**, without warranty.
[License](LICENSE) · [Third-party notices](THIRD_PARTY_NOTICES.md).
It is not affiliated with Lucasfilm, LucasArts, Disney or the upstream authors.
