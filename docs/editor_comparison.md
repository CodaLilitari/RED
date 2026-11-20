# RED and other level editors

[RED overview](../README.md) · [Designer guide](user_guide.md) · [Capability and acceptance matrix](capability_matrix.md)

RED's purpose is to take Sith-engine content through editing, asset preparation, scripting, reviewed packaging
and target-specific testing. This comparison identifies the concrete workflows it adds and the reasons another
editor or specialist tool can still be useful.

## Scope and evidence

The direct comparison covers the identified Sith-editor families: **SED, JED, ZED, NJED, JKEdit, NED/Smith,
GeoSith and the Holocron reference in RED's audit**. Historical internal tools and adjacent asset utilities are
listed separately, followed by editors for other engines. This is a named comparison set, not a claim to have
executed every historical build or catalogued every plug-in.

- **Source inspected** means the implementation or entry point was read. It does not establish a fresh test pass.
- **Documented** means an author manual, official tutorial or distribution records the capability.
- **Unverified** means the relevant build, format or behavior has not been established. It does not mean absent.
- **Runtime evidence** applies to the named game, build, package and scenario. Editor views, static checks,
  source inspection and other engines' behavior are separate evidence.

RED entries describe the current source checkout. Its [release status](../README.md#source-and-release-status)
identifies what is in the downloads. Pinned revisions identify the inspected source baselines; other links point
to the cited project documentation. This comparison did not execute those editors or conduct a timed usability contest.

## SED develop and Pascal SED

SED has two important baselines:

| Baseline | Source | Relationship to RED |
| --- | --- | --- |
| **Pascal SED 0.3.0** | [Release][sed-release]; main at [c3b6c0e][sed-pascal] | JED's Delphi lineage, including IJIM support and a native plug-in host. Its published release is not the C++ rewrite. |
| **C++/Qt SED 0.4.0, develop** | [b822559][sed-tree] | RED's principal editor foundation. Compare RED with this branch when assessing modern geometry, UV, resource and workspace functionality. |
| **RED** | This source tree | Extends the shared editor with additional authoring, preservation, automation, packaging and evidence workflows. |

### Capability comparison

| Workflow | SED develop baseline | RED extension and boundary |
| --- | --- | --- |
| **Sector construction** | Cleave, join, merge, extrude, adjoin/connect, primitive shapes, shaped Multi-cut and OBJ/3DO sector interchange. | Recipes for stairs, domes, corridors, arrays and terrain; sealed-seam conformance; restored classic operations and reviewed repairs. See [designer guide](user_guide.md). |
| **UV editing** | Docked multi-island editing, transforms, Smart UV Project, stitching and packing. | Batch planar, cylindrical and spherical projection at an explicit texel density in the [Workbench](workbench.md), with validation and one Undo step; shared core operations are available to agents. |
| **Lighting** | Baked lights, dirty-region recalculation, live Thing/headlight preview and MotS ARCH authoring/model-vertex display. | Light Baker conventions, emission/sky/bounce profiles, manual ray transport, contribution inspection, saved native recipes and owned Shadow Cuts. [Lighting scope](lighting.md). |
| **COG editing and behavior** | COG text editing, completion and diagnostics; placed values, templates and frame paths. | JK/MotS [compiler](cog_compiler.md), scoped [simulator](cog_runtime.md), gameplay assistants, template library and assistant message inspection. The simulator is not a complete game runtime. |
| **Model and KEY handling** | Model/resource readers, KEY track reading and Thing frame-path tools. | Hierarchy/pivot and KEY authoring, delta reconstruction, marker/LOD pose playback and explicit export-loss refusal. Direct mesh editing remains outside RED. [Model studio](model_hierarchy.md). |
| **Material and resource authoring** | Typed resource pickers, audio preview, SPR/SND/UNI and template editing. | [MAT/CMP and palette creation](material_authoring.md), [OBJ/GLB-to-3DO model import](model_import.md), model lighting and source-qualified [resource-family insertion](portable_projects.md). |
| **Native document** | Native workspace, Undo/Redo, editor state, autosave and recovery. | Journaled generated-file recovery, guarded native-file automation and a live reviewed material edit that preserves unsaved drafts. External reusable files intentionally survive document Undo. |
| **Campaigns** | An episode/campaign editor already exists. | Portable project/target profiles, mount identities, multi-level package preparation and named scenarios. Packaging reads exported sibling JKLs; Test exports the current open level. [Project contract](portable_projects.md). |
| **Archives and dependencies** | GOB/GOO mounting and archive readers. | Archive creation, reviewed dependency/package planning, source precedence inspection and source rechecks before publication. Static dependency inspection does not prove runtime closure. [Packaging](package_authoring.md). |
| **Preview and game testing** | Consistency checking and an editor Game Preview. | Separate actual-engine launch/capture, current-level scenario testing and package/resource receipts. Ordinary-control gameplay currently targets Linux OpenJKDF2/OpenGL. [Testing](game_testing.md). |
| **Agent interface** | No equivalent general CLI/MCP authoring entry point was located in this inspected C++ baseline. | Shared-core CLI/MCP operations, schema/query discovery, semantic plan/apply previews and static snapshots. The GUI live-review bridge currently covers one material operation. [Agent workflow](agent_workflow.md). |
| **Legacy plug-ins** | The [C++ porting guide][sed-porting] identifies the absent legacy DLL host; Pascal SED had one. | Native IJED-compatible host, restored built-in commands and a Windows architecture bridge. ISED-only plug-ins are unsupported, and native plug-ins are unsandboxed. |
| **JO/JA conversion** | No equivalent Raven BSP-to-Sith production workflow was located in this baseline. | Supported static scenes become JK/MotS packages, with explicit mappings, diagnostics, optimization and reviewed reimport. Source gameplay, shaders and audio need additional authoring. [Conversion](jo_ja_import.md). |

Upstream evidence: [README][sed-readme], [user guide][sed-guide], [lighting contract][sed-lighting],
[resource authoring][sed-resources], [KEY reader interface][sed-key], [episode editor][sed-episode],
[GOB reader][sed-gob], [native format][sed-native] and [build targets][sed-cmake].
These sources establish the shared foundation; they should remain credited when the RED feature list changes.

## Other Sith-editor families

| Editor | Documented or inspected strengths | RED comparison and remaining distinction |
| --- | --- | --- |
| **JED** | Classic JK/MotS sector/surface/Thing workflow; preview, Undo, consistency and resource checks, episode editor, GOB creation, Save JKL & Test, hierarchy tools, native plug-ins and OLE automation. | RED extends this lineage with the C++/Qt workspace, content tools, structured CLI/MCP review and exact-package evidence. Packaging, automation, hierarchy tools and game launch are not RED inventions. |
| **ZED** | A JED-derived distribution with fixes and additions that retains the familiar editing model. | RED's broader content/agent workflow is documented above. The exact ZED distribution has not received a complete feature or runtime audit here. |
| **NJED 0.973** | JED-derived editing with mouse-look/fly controls, WASD movement, grid/sensitivity controls and MAT thumbnails. | RED adds resource provenance, broader integrated content tools, reviewed automation and a JED/NJED workspace preset. The preset is not a measured guarantee of identical muscle memory. |
| **JKEdit** | Direct textured construction, split/extrude/carve tools, automatic doors and elevators, Test Level, episode and multiple-level editing. | RED combines these familiar production goals with its shared editor/agent core, resource review and preservation contracts. Native JKEdit-project migration and its complete schema remain unverified. |
| **NED / Smith** | The author identifies NED as a C# transcription of JED. Smith documents JED/ZED integration with bidirectional camera synchronization, engine timings, COG VM diagnostics, physics-frame recordings and portal/BVH inspection. | RED provides a reviewed source/package/scenario/receipt workflow and its own editor/agent tools. Smith's author describes incomplete gameplay compatibility; its diagnostic observations do not qualify retail JK/MotS. |
| **GeoSith** | In-game OpenJKDF2 editing, F8 edit/play, host-approved multiplayer coediting, Undo/Redo, browsers, spline/terrain and gameplay generators, bounded direct 3DO mesh editing, template/episode tools and GOB saves. | RED supplies a standalone document and CLI/MCP workflow with preservation and target-evidence contracts. RED's two-peer gameplay checks are distinct from GeoSith's live collaborative editing. Direct mesh authoring and live coediting are real reasons to consider GeoSith. |
| **Holocron, the Sith-editor reference in RED's audit** | RED's [X-04 record](capability_matrix.md) describes prior manual/binary inspection and diagnostic ideas. | An authenticated runnable baseline, extension grammar and interoperability remain unverified. It is not scored as lacking features. This reference is not the KotOR Holocron Toolset. |

### Source and implementation notes

**JED:** the [source archive][jed-source], [menu definitions][jed-menu] and [OLE interface][jed-ole] substantiate
the listed entry points. This comparison does not claim a fresh JED runtime pass.

**ZED and NJED:** [ZED's preserved distribution][zed] identifies the archived build; [NJED's author release][njed]
documents its navigation and resource-browser changes. The comparison does not extrapolate one fork's changes
to every other JED derivative.

**JKEdit:** the [preserved official tutorials][jkedit] explicitly document door/elevator tools, game testing
and multiple-level editing. RED should describe the scope of its own assistants, not advertise gameplay
generators or campaign support as new to the entire ecosystem.

**NED/Smith:** the [author's project page][smith] links the NED source and describes Smith's compatibility;
the [advanced documentation][smith-advanced] describes the integration and diagnostic tools.

**GeoSith:** source is pinned to [0dbad22][geosith]. Selected implementation checks include
[accepted/speculative documents, host approval, rejection and rollback][geosith-net], and
[model editing/import/library operations][geosith-models]. Its README requires matching builds among editing peers.
Its model-import workflow uses static rest-pose geometry without imported animation hierarchy or alternate LODs.
Source presence and declared tests do not establish executed compatibility or a comparative speed result.

## Historical and complementary tools

These are not interchangeable categories: an internal editor, an animation utility and a format converter do
different jobs.

| Tool or family | Role and evidence | Relationship to RED |
| --- | --- | --- |
| **LEIA** | Internal-editor history and technical-design references; an authenticated runnable baseline is not qualified here. [JK technical-design archive][jk-design]. | Useful design history, with no assertion that RED reproduces every undocumented internal workflow. |
| **ModelX** | The [DroidWorks developer postmortem][droidworks-postmortem] identifies a separate animation preview/editor alongside LEIA. | Historical asset tooling, not another name for the level editor. RED's DroidWorks support remains experimental. |
| **Urgon / libim** | [Author's tooling][urgon] includes IJIM CND/NDY conversion, archive/resource manipulation, geometry export and material tools. | Complementary asset/format utilities. RED's NDY editing does not establish CND support or replace the full toolset. |
| **Blender-Sith** | [Blender add-on][blender-sith] for 3DO/KEY and Sith materials, hierarchy, UVs and model attributes. | A full modeling/animation environment remains useful. RED integrates level placement, checks, hierarchy and KEY workflows; it does not replace general mesh authoring. |
| **Puppet Jedi, TrajJedi, MAT/CMP and archive utilities** | Specialist programs survive in the [historical distributions][programs]. | RED incorporates common tasks. Match each replacement to the actual supported operation rather than promising complete behavior parity. |
| **Parsec, Gorc, DataMaster and COG references** | Compiler, runtime/reimplementation and reference work have different contracts; RED's [compiler guide](cog_compiler.md) and [source library](knowledge/cog/sources.md) describe its evidence. | RED adds target-aware diagnostics and integrated scoped simulation. A reference manual is not a rival level editor, and one corrected reference entry does not establish universal superiority. |

## Editors for other engines

Their native game and content models are the relevant comparison. A tool's ability to import a mesh is different
from preserving Sith sectors, adjoins, templates, COG semantics and episode/resource resolution.

| Editor/family | Native workflow documented by its authors | RED's specialization |
| --- | --- | --- |
| **TrenchBroom** | Cross-platform Quake-family brush/CSG editing, vertex and texture tools, entity links, custom game configurations and map compilation. [Project][trenchbroom] · [Manual][trenchbroom-manual]. | Direct Sith level/resource semantics, JK/MotS scripting and package creation. TrenchBroom remains a useful benchmark for geometry and texture usability. |
| **GtkRadiant / Radiant family** | Brush, patch and entity workflows for supported id Tech games; GtkRadiant lists Jedi Academy and older-branch Jedi Outcast support. [Game support][radiant]. | RED converts supported JO/JA scenes into JK/MotS. It does not replace authoring and compiling the original Raven game format. |
| **DarkRadiant** | Doom 3/The Dark Mod-related map editing, brushes/patches, prefabs, layers and game/camera integration. [User guide][darkradiant]. | Sith-native documents and target workflows. Prefabs and live connections are established capabilities elsewhere. |
| **Hammer / Source 2 tools** | Valve level authoring and its material/model/animation/particle toolchain. [Valve's tools announcement][valve-tools]. | The JK/MotS/IJIM content model and compatibility constraints, rather than a general claim of a more capable renderer or asset suite. |
| **Unreal Editor** | Actor-based levels, terrain/foliage, modeling, content management and in-editor play. [Epic documentation][unreal]. | Direct authoring for existing Sith games and their standard packages. Unreal serves a different runtime and production system. |
| **Unity + ProBuilder** | In-scene geometry, UVs, vertex colors, parametric shapes, collision meshes and scripting. [Unity documentation][unity]. | Native Sith geometry, scripts, resources and packaging, without translating a Unity scene into those game contracts. |
| **Godot and level-building add-ons** | Scene/node authoring, built-in CSG prototyping and documented dedicated add-on workflows. [Godot documentation][godot]. | Existing Sith game content and engine acceptance. Godot's scene/runtime model has different authoring goals. |

This table makes no claim that extensions or custom exporters are impossible in those tools. It identifies the
documented native workflows and the target-specific work RED supplies.

## What the comparison establishes

Choose RED when the required workflow is Sith editing plus integrated assets/scripts, reviewed automation,
dependency-aware packaging and evidence tied to the resulting game content.

Choose a specialist or another editor when its particular workflow matters: a familiar legacy setup,
GeoSith's live in-engine coediting, full Blender modeling, Urgon's IJIM format utilities, or authoring for a
different game engine. Those are substantive distinctions, not failures to discover a menu.

Human task speed, usability, preservation across unrelated projects and target-game behavior need matched tests.
The [acceptance matrix](capability_matrix.md) is the place for those results. Keep this comparison current by
updating a changed baseline, its owning source and the actual workflow boundary together; an unknown cell must
not silently become a negative claim.

[sed-release]: https://github.com/smlu/SED/releases/tag/v0.3.0
[sed-pascal]: https://github.com/smlu/SED/tree/c3b6c0e59f192e96dc6df2daf698fb179e63603a
[sed-tree]: https://github.com/smlu/SED/tree/b82255998fb733c8e8c87104eb7c17f27b48376f
[sed-readme]: https://github.com/smlu/SED/blob/b82255998fb733c8e8c87104eb7c17f27b48376f/README.md
[sed-guide]: https://github.com/smlu/SED/blob/b82255998fb733c8e8c87104eb7c17f27b48376f/docs/user_guide.md
[sed-lighting]: https://github.com/smlu/SED/blob/b82255998fb733c8e8c87104eb7c17f27b48376f/docs/lighting.md
[sed-resources]: https://github.com/smlu/SED/blob/b82255998fb733c8e8c87104eb7c17f27b48376f/docs/resource_authoring.md
[sed-key]: https://github.com/smlu/SED/blob/b82255998fb733c8e8c87104eb7c17f27b48376f/src/core/include/sed/assets/key.hpp
[sed-episode]: https://github.com/smlu/SED/blob/b82255998fb733c8e8c87104eb7c17f27b48376f/src/app/src/ui/episode/episode_editor.cpp
[sed-gob]: https://github.com/smlu/SED/blob/b82255998fb733c8e8c87104eb7c17f27b48376f/src/core/include/sed/assets/gob.hpp
[sed-native]: https://github.com/smlu/SED/blob/b82255998fb733c8e8c87104eb7c17f27b48376f/docs/sed_format.md
[sed-porting]: https://github.com/smlu/SED/blob/b82255998fb733c8e8c87104eb7c17f27b48376f/docs/porting_guide.md
[sed-cmake]: https://github.com/smlu/SED/blob/b82255998fb733c8e8c87104eb7c17f27b48376f/CMakeLists.txt
[jed-source]: https://github.com/df21-net/editor/tree/8a08e019a21efc113b23ef62f716baac180563f4/jed
[jed-menu]: https://github.com/df21-net/editor/blob/8a08e019a21efc113b23ef62f716baac180563f4/jed/Jed_Main.lfm
[jed-ole]: https://github.com/df21-net/editor/blob/8a08e019a21efc113b23ef62f716baac180563f4/jed/JED_OLE.pas
[zed]: https://www.massassi.net/programs/#zed-level-editor-based-on-jed
[njed]: https://github.com/JKBoBoFett/NJED/releases/tag/V0.973
[jkedit]: https://www.moddb.com/tutorials/jkedit-official-tutorials
[smith]: https://smith.bah.wtf/
[smith-advanced]: https://smith.bah.wtf/advanced.html
[geosith]: https://github.com/SuperDonRobb/GeoSith/blob/0dbad22c1b6b0c63f89b5c7ae6634552daded8f2/README.md
[geosith-net]: https://github.com/SuperDonRobb/GeoSith/blob/0dbad22c1b6b0c63f89b5c7ae6634552daded8f2/src/Main/jkEditorNet.h
[geosith-models]: https://github.com/SuperDonRobb/GeoSith/blob/0dbad22c1b6b0c63f89b5c7ae6634552daded8f2/src/Main/jkEditorModels.h
[jk-design]: https://jkdf2.net/JKBot/JKArchive/JKDF2/JKDF2/Documents%20And%20Development%20Information/Text/96-11_Jedi_Knight_Technical_Design_Document_compressed-1.pdf
[droidworks-postmortem]: https://www.gamedeveloper.com/design/postmortem-lucaslearning-s-star-wars-droidworks
[urgon]: https://github.com/smlu/Urgon
[blender-sith]: https://github.com/smlu/blender-sith
[programs]: https://www.massassi.net/programs/
[trenchbroom]: https://github.com/TrenchBroom/TrenchBroom
[trenchbroom-manual]: https://trenchbroom.github.io/manual/latest/
[radiant]: https://icculus.org/gtkradiant/developers.html
[darkradiant]: https://www.darkradiant.net/userguide/
[valve-tools]: https://store.steampowered.com/oldnews/?appgroupname=Half-Life%3A+Alyx&appids=546560&feed=steam_community_announcements
[unreal]: https://dev.epicgames.com/documentation/en-us/unreal-engine/level-editor-in-unreal-engine
[unity]: https://docs.unity3d.com/Packages/com.unity.probuilder@6.0/manual/index.html
[godot]: https://docs.godotengine.org/en/stable/tutorials/3d/csg_tools.html
