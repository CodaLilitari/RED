# Agent parity with the RED app

The goal is full feature parity: anything a level designer can do from RED's menus, panels and dialogs, `red-agent`
and `red-mcp` can do too. They do it as reviewed, transactional patches: `plan` writes nothing, `apply` publishes a new
file.

This page is the inventory of the app's commands against the agent. It's kept current as gaps close. Each row is one of
three things:

- **covered:** a patch operation or command does the job.
- **partial:** something covers part of the job; the row says which part is missing.
- **gap:** nothing covers it yet.

Commands that only make sense with a window (undo, tabs, camera, view toggles) are listed at the end as not applicable,
with the agent's equivalent where there is one.

## Files

| App | Agent | Status |
|---|---|---|
| Open (.sed, .jkl, .ndy, .jed, .asc, .lev) | every command opens all six; `.ned` too | covered |
| Save / Save As (.sed, .jkl, .ndy, .jed) | `apply` (format kept, or `outputFormat: "jed"`), `convert`, `new` | covered |
| Export Level as DXF / Import DXF | `export-dxf`, `dxf-inspect`, `dxf-import` | covered |
| Export Heightmap Terrain Model (3DO) | `terrain-3do` | covered |
| Mount Archive / Add Resource Root | per request: `resourceArchives`, `resourceRoots`, `gobs` | covered |
| Level Statistics | `statistics` | covered |
| Save JKL Test / Game Preview | `launch-plan` plans the launch; `snapshot`/`overview` preview the level | partial: the agent does not start the game |
| Episode / Campaign Editor | `package-plan` / `package-apply` build a one-level episode GOB | partial: multi-level episodes and episode.jk editing |

## Edit

| App | Agent | Status |
|---|---|---|
| Find / Find Next / Find Previous | `query`, `select`, `workbench-query` | covered |
| Delete sectors | `deleteSectors` | covered |
| Delete Things / lights / COGs | `deleteThings`, `deleteLights`, `deleteCogs` | covered |
| Delete surface / vertex, Force Delete Vertex | none | gap |
| Copy / Paste Entities, Paste in Place | `prefab-capture` + `insertPrefab` (sectors); `addThing`/`addLight` | partial: no Thing/light clipboard with relative placement |
| Duplicate Entities | `duplicateSector` (sectors); `addThing`/`addLight` | partial: one sector per operation; no Thing/light duplicate |

## Sectors and surfaces

| App | Agent | Status |
|---|---|---|
| Add Sector (cube) | `addBox` | covered |
| Extrude / Build Surface or Sector | `extrudeSurface` | partial: building a surface or sector from picked vertices is a gap |
| Cleave | `cleaveSurface`, `cleaveSector`, `carveSurfaceRect` | covered |
| Adjoin / Connect Surfaces | `adjoinSurfaces`, `connectSurfaces` | covered |
| Unadjoin | `unadjoinSurfaces` | covered |
| Fit and Adjoin | `fitAndAdjoin` | covered |
| Join / Merge sectors | `mergeSectors` | covered (refused once COGs are placed) |
| Merge surfaces, Planarize, Subdivide Surfaces, Inset Surface | `mergeSurfaces`, `planarizeSurfaces`, `subdivideSurfaces`, `insetSurface` (`adjoinPair` for a portal) | covered (merge and subdivide refused once COGs are placed) |
| Flip Surface Winding | `flipSurfaces` | covered |
| Conform Sealed Seams | `conformSeams` | covered |
| Remove / Report Redundant Vertices | `removeRedundantVertices` (`plan` reports without writing) | covered |
| Fix Passable Adjoins / Fix Solid Surfaces | `repairSurfaces {solid?, passable?}` | covered |
| Sector properties (flags, ambient, colormap) | `setSectorFlags`, `setSectorAmbient`, `setSectorColormap` | covered |
| Sector properties (extra light, tint, sound) | `setSectorProperties` | covered |
| Surface material, render modes, UVs, vertex light | `setSurfaceMaterial`, `setSurfaceRenderModes`, `setSurfaceUv`, `setVertexLight` | covered |
| Surface extra light, UV scale | `setSurfaceProperties` | covered |
| Surface, face and adjoin flags (Masked Recipe) | `applyMaskedSurfaceRecipe` | covered |
| Vertex position | `setVertexPosition` | covered |
| Save Selected Sector as Shape, saved shapes | `prefab-capture` / `insertPrefab` | partial: the app's shape library is not shared |
| Mirror floor | `mirrorFloor` (agent first) | covered |

## Transform

| App | Agent | Status |
|---|---|---|
| Translate | `translateSectors`, `translateThings`, `translateLights` | covered |
| Rotate / Scale | `rotateSectors`, `scaleSectors`, `rotateThings`, `rotateLights` | partial: scaling Things and lights; a Thing with path frames is moved with `translateThings` only |
| Mirror, Scale Along | `mirrorSectors`, `mirrorThings`, `mirrorLights` | partial: Scale Along |

## Texture mapping

| App | Agent | Status |
|---|---|---|
| UV Editor, Move / Rotate / Scale Surface UV | `setSurfaceUv`, `batchUvTransform` | covered |
| Auto Align, Copy Texture UV From Reference, Stitch From Reference | `alignSurfaceTexture`, `fitSurfaceTexture`, `batchUvProject`/`Cylindrical`/`Spherical` | covered |
| Stitch UV Across Connected Selection | `stitchSurfaces` pair by pair, `alignSurfaceTexture` | partial: a whole connected selection in one operation |

## Building and recipes

| App | Agent | Status |
|---|---|---|
| Build Stairs / Switchback / Spiral, Dome, Swept Corridor, Sector Array, Heightmap Terrain, Thing Path | the matching `build*` operations | covered |
| Insert Prefab / Save Sectors as Prefab, Import / Export Sectors | `insertPrefab`, `prefab-capture` | covered |
| Make Door | `makeDoor` | covered |
| Make Underwater | `makeUnderwater` | covered |
| Place Sector Lights | `placeSectorLights` | covered |
| Make Breakable Glass | none | gap |

## Lighting

| App | Agent | Status |
|---|---|---|
| Recalculate Lighting | `bakeLighting` (with emissive, sky, bounce, limits, mirror floors, `angleShading`, `waterAbsorption`; suns and multiply lights by their flags) | covered |
| Add / edit light, MotS RGB, light flags (sun, sun anchor, multiply) | `addLight`, `setLight` (`flags`), `setLightRgb`, `placeLightArray` | covered |
| MotS architectural lighting | `bindThingArchitecture`, `rebindThingArchitecture`, `bakeThingArchitecture`, `arch-validate` | covered |
| Model lighting | `model-lighting-inspect` / `-plan` / `-apply` | covered |

## Things, templates and COGs

| App | Agent | Status |
|---|---|---|
| Add Thing, Thing properties, frames | `addThing`, `setThing*`, `setThingFrames`, `placeThingArray` | covered |
| Template Editor | `defineTemplate`, `importTemplates` | partial: rename and delete |
| Add COG, bindings | `addCog`, `rebindCogReference`, `setCogValue`, `bindings`, `cog-check`, `cog-compile` | covered |
| Delete COG | `deleteCogs` | covered |
| Duplicate COG, Edit COG flags | none | gap |
| New COG Script | none: scripts are written as files and checked with `cog-compile` | partial |
| KEY Timeline | `key-inspect`, `key-plan`, `key-apply` | covered |

## Layers, assets and plug-ins

| App | Agent | Status |
|---|---|---|
| Manage Layers | none | gap |
| MAT / CMP tools | `mat-inspect`, `mat-export-png`, `mat-create` (with `paint`), `cmp-create`, `cmp-master-create` | covered (the agent has more) |
| Model import | `model-import` | covered |
| Plug-ins (JED IJED / SED ISED DLLs) | none | gap: native Windows DLLs; not reachable from a headless Linux agent |

## Not applicable

Undo and Redo, Autosave History, Make a Backup Copy, Revert, Reload, tabs, Preferences, view modes and overlays, and the
camera commands (Insert Thing at Camera, Move Things to Camera, Copy Camera as Frame) need a window. The agent's
equivalents:

- **Undo:** transactional patches. A plan is reviewed by its hash, and nothing is ever overwritten.
- **Camera:** the `snapshot` camera.
