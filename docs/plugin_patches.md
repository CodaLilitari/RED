# Legacy plug-in conversion patches

Behavioral conversions of the 45 known JED/SED plug-in and companion
families (grouped as E-01..E-19 in
[capability_matrix.md](capability_matrix.md)). A family counts as
"converted" when its behavior works natively in RED when selected —
as a menu command, recipe, or agent patch operation — with one Undo
step and tests. Hosting the original 32-bit DLLs is a separate lane
(P-01 bridge; synthetic plug-ins only so far).

| Row | Families | Native conversion | Status |
| --- | --- | --- | --- |
| E-01 | Statter, Statman, Layer Everything, calculators | Queries, summary/sector/thing collections | Partial (named aggregation/layer gaps) |
| E-02 | Flagman, SkyFlagger, Vinkel, MiniPlugins, Copy SF | Masked surface recipe (`levelApplyMaskedSurfaceRecipe`, Surface > Masked Recipe dialog, agent `applyMaskedSurfaceRecipe`) | Partial (changed-level acceptance pending) |
| E-03 | Awesome Arrays, Mirror Madness, ThingPack, MoveSC2VX | `placeThingArray` / `placeLightArray` patch ops plus geometry arrays (`levelBuildSectorArray` prefab copies over a placement layout, Sector > Build Sector Array, agent `buildSectorArray{sectors,layout}`) | Partial (reflection/anchoring correctness pending) |
| E-04 | Beam Me in JED, JKTree, fragments | Versioned, SHA-256-bound sector prefab (`makePrefab`/`insertPrefab`, boundary-face list) on the clipboard payload, portable text format (`savePrefabText`/`loadPrefabFromText`), Sector > Save/Insert Prefab, agent `prefab-capture` + `insertPrefab{prefabFile,target}` | Partial (dependency-complete resource packaging and attachment-face joining missing) |
| E-05 | Stair Crazy, FreeSector, MrSector, Go Rotate, Get Bent, TransSectoral | Build Stairs + Build Switchback Stairs + Build Spiral Stairs + Build Dome + Build Swept Corridor recipes (`levelBuildSpiralStairs`/`levelBuildDome`/`levelBuildSweptCorridor`, Sector dialogs, agent ops) | Working (in-game acceptance pending) |
| E-06 | Surface Tools, Adjoin Jiffy, Point Extruder, Cleave2VX, SecCleave | Fit and Adjoin + Inset Surface (single/paired) recipes | Partial (extrusion pairing missing) |
| E-07 | Vertex Assassin, Ripper, CreateVertex, EWOK | Remove/Report Redundant Vertices + Subdivide Surfaces recipes | Partial (repaired-level acceptance pending) |
| E-08 | ForceRemap, UV companions | Six-basis UV workbench + GUI editor; cylindrical (`batchUvCylindrical`) and spherical (`batchUvSpherical`) batch UV wired in core, agent and Workbench dialog | Partial (selection-wide confirmation missing) |
| E-09 | Goal!, Pathfinder, CogWriter, Cogginator, Coggit, CutMaster2 | Pathfinder: `levelBuildThingPath` (Thing > Build Thing Path, agent `buildThingPath`). Goal!: the objectives assistant (`red_goals.cog`: reach, use and kill goals and an exit). CutMaster2: the cutscene assistant (`red_cutscene.cog`). Thing > Assistants and agent `objectivesAssistant`/`cutsceneAssistant`, checked in the COG runtime; see [gameplay assistants](gameplay_assistants.md) | Partial (COG-fragment recipes of CogWriter/Cogginator/Coggit missing; objectives and cutscenes need in-game acceptance) |
| E-10 | Water Works, Shatter | Make Underwater / Make Breakable Glass recipes | Working (in-game acceptance pending) |
| E-11 | Parsec, Gorc, DataMaster | COG schema/preflight/editor, the COG compiler (`cog-compile`, retail grammar and engine limits) and `cog-check` over the measured registries | Partial (compiler and runtime landed; the runtime is a stand-in world) |
| E-12 | KeyMaster, Puppet Jedi, Fun3DO, JOKE | KEY reader plus `saveToKEYText` writer and ordered timeline insert/erase/sort; File > KEY Timeline dialog (own `UndoHistory<KeyFile>`) and agent `key-plan`/`key-apply`/`key-inspect` | Partial (preview and 3DO timeline editing missing) |
| E-13 | DXF Import, JED2PRJ, OBJ exporters, Radiant/MilkShape, OnPar | OBJ/3DO sector import/export plus DXF 3DFACE export/face import profile and scoped DXF-to-sector compilation (horizontal footprints extruded, shared walls adjoined); File > Export Level as DXF / Import DXF (Inspect) / Import DXF as Sectors and agent `export-dxf`/`dxf-inspect`/`dxf-import` | Partial (Radiant/MilkShape/PAR scopes missing) |
| E-14 | Hellcat Terraining, 3DO Heightmap Generator | `levelBuildHeightmapTerrain` box-cell terrain with neighbour adjoins plus `levelBuildHeightmapModel` single-mesh 3DO model output (node heights average touching cells); Sector > Build Heightmap Terrain and Export Heightmap Terrain Model (3DO), agent `buildHeightmapTerrain{...}` and `terrain-3do` command | Partial (UV/partition profiles missing) |
| E-15 | Light Fantastic | Place Sector Lights recipe | Working (concave-room policy pending) |
| E-16 | CleaveLight, Sige Lighter | Shadow cuts (`levelShadowCuts`, Edit > Sector > Shadow Cuts, agent `shadowCuts`): cut along the edges of each light's view through the openings, or split surfaces near lights to a size; Remove Shadow Cuts merges them back. Openings are never cut, and no cut takes a surface past 24 corners | Partial (a density field the author paints, shadows cast by models, and in-game acceptance pending) |
| E-17 | JED Light Baker (LadyEeb) | Point-light bake (`levelCalcLightingNew`), sector ambients, ARCH bake, FULLYLIT material emission | Partial (emission resolved from actual MAT data via `sed/level/emissive_resolution.hpp`: color MAT = cel solid color, texture MAT = mip-0 mean excluding discarded texels, sector/master CMP palette; GUI bake + transform preview and agent `bakeLighting{resourceRoots,gobs,intensity,range}` pass the resolved span; profile via QSettings `lighting/emissive_intensity` / `lighting/emissive_range`, defaults 1.0/10.0; missing/unparseable MATs omit only that surface; empty resolution is byte-identical to the point-light bake; one-bounce diffuse profile wired (`levelCalcLightingNewWithBounce`: direct pass, then every surface re-emits its average baked colour through the same gating/portals; QSettings `lighting/bounce_strength`/`lighting/bounce_range`, default off; agent `bakeLighting` gains `bounceStrength`/`bounceRange`); sun/sky profile wired (`resolveSkyEmissiveSurfaces`: FULLYLIT SF_SkyCeiling/SF_SkyHorizon surfaces emit a uniform sky colour; QSettings `lighting/sky_intensity`/`sky_range`/`sky_color`, default off; agent `bakeLighting` gains `skyIntensity`/`skyRange`/`skyColor`); Light Baker conventions wired: directional suns (`LF_Sun` 0x02 with `LF_SunAnchor` 0x08) through sky surfaces, multiply lights (`LF_Multiply` 0x20, non-MotS), and opt-in angle shading and water absorption (QSettings `lighting/angle_shading`/`lighting/water_absorption`, agent `bakeLighting`). Fixes for four bugs found while reading her baker were sent to LadyEebs as pull requests 23-26 on LadyEebs/JED-Light-Baker) |
| E-18 | Matt, GIMP IJIM, blender-sith, JKL2OBJ, Urgon CLI, Smith | Asset readers/writers, OBJ bridge, agent `mat-inspect`/`mat-export-png` MAT bridge | Partial |
| E-19 | Hotkey Enforcer, SDKs, OLE helpers | Rebindable shortcuts; P-01 DLL bridge | Partial (safe typed extension API design-gated; see gap register) |

## Conversion order (proposed)

1. E-17 Light Baker profiles — emission source decided (MAT solid
   color / mip-0 mean, above) and wired; bounce and sun/sky build on
   the same profile struct.
2. E-02 masked batch recipes (exact masks already specified).
3. E-05/E-06/E-07 recipe completions (contracts already in the matrix).
4. E-14 terrain, E-04 prefabs, E-09 game recipes, E-16 subdivision,
   E-12 timeline, E-13 interchange scopes, E-11 compiler diagnostics.
   (Largely landed since: E-13 DXF-to-sector, E-11 `cog-check`,
   E-12 `key-inspect`, E-18 MAT bridge, E-08 spherical UV; E-16 shadow cuts
   on Andrei's decision of 19 November 2026.)
5. E-19 typed extension API (design-gated with P-01 sandboxing).

Each conversion lands as: core function + GUI command and/or agent
patch operation + maintained tests + capability-matrix receipt.

## The JKTree archive review (19 November 2026)

The 55 plug-in pages in the jkdf2.net JED archive were checked against this register. Every plug-in on them already
had a family here except the two vector calculators (Veccalc, Unit Vector Calculator), which belong with E-01. CleaveLight 0.1b
and Lighter v.7 were read for the shadow cuts: both cut surfaces where light stops, Lighter passes light through
adjoins unless they block light (`0x80000000`), and neither could be undone in JED. RED's cuts run on a copy, are one
undo step, and can be merged back later. Still open from the archive: Mirror Madness's reflection correctness (E-03),
Adjoin Jiffy's pairing of two different surfaces with the same corner count (E-06), OnPar's `.par` export of
selected vertices (E-13), and the vector calculators.
