# Change Log

## 0.8.2

- Open **Tools > Project Cockpit** to review the target, level list, ordered resource mounts and
  shadowed resources. Preview and package a multi-level campaign using its portable project profile.
- F8 gameplay and campaign operations journal their generated files. Reopening a project can recover
  an interrupted operation. RED preserves independently edited files and reports the affected path.
- Scripted surface merges carry typed COG references through the edit. The agent reports the
  particular script and symbol when a proposed split leaves the target ambiguous. Native sector
  identities follow topology changes and Undo/Redo.
- Test a named scenario from the open editor and return to the same document and Undo history.
  The receipt records the export, package, resource and result identities. Native compact JKL exports now
  include custom palettes when packaging.
- Find an actor's model, animation, script, material and sound dependencies in Project Cockpit. Inspect
  all model LODs and review supplied files and stock requirements before inserting the family.
- Save the accepted ray-lighting recipe with the workspace, inspect its contributions and detect stale
  inputs. Regenerate or remove owned Shadow Cuts while protecting manual work.
- Review the baseline, authored and regenerated versions of JO/JA episode members. The reimport lists
  specific conflicts before it writes output.
- Use the JED / NJED workspace, command search, selection sets and numerical repeat. Preview adjoin
  repairs before applying them.
- Compare recorded engine frame times on stationary and ordinary walking routes. The report shows
  repeated measurements for each geometry and engine configuration, and flags mismatched routes.
  [Measured two-room example](tools/engine-capture/examples/route-profile/observed-two-room.md)

## 0.8.1

- **Tools > Review Agent Edit…** previews one proposed surface material change in the open native
  workspace. Acceptance records one Undo step. RED checks the reviewed document, drafts and material
  again before applying the change and remembers the result when the same proposal is submitted again.

## 0.8.0

- Parameter dialogs reserve enough title-bar space using the current display and style. Archive mounting
  now preserves the existing resource configuration if a filesystem identity check runs out of memory.
- Gameplay assistants verify generated and supplied project scripts before committing level references. Conflicting
  files and failed publication refuse the edit. Undo preserves scripts that were edited separately.
- Source profiles for Jedi Outcast and Jedi Academy multiplayer use their respective footstep rules. Existing import
  recipes retain their previous conversion when the profile is omitted.
- 3DO dependency scanning and packaging now resolve compact indexed material rows such as `0:wall.mat`.
- Shadow Cuts reports geometry refusals separately from the engine corner limit. Windows ray baking prepares its
  compute context on the GUI thread before transferring it to a worker. The macOS backend uses CPU ray transport.

- **Agent skills:** The level, COG and model skills include Red’s retail authoring and playtest guidance.
- **JO/JA hybrid geometry:** Lilitari added an optional native-sector conversion to the shared GUI, CLI and MCP
  importer. It chooses bounded source-derived partitions with a crossing-cost heuristic, closes apertures against
  verified static opaque brush sections, promotes suitable walls and floors, and preserves remaining detail as
  indexed convex model faces. Native shells, reciprocal portals, source coverage, target precision and loader budgets
  are checked before output; unrepresentable native artwork stays in models when that placement can be certified.
  The existing model-scene recipe remains the default. Masked MATs, complete gameplay migration and retail frame-rate
  qualification remain outside this mode's current contract. See [hybrid geometry](docs/jo_ja_geometry_design.md).
- **JO/JA patch precision:** Non-power-of-two subdivision counts now evaluate the same authored patch control net
  without creating phantom edge strips on mathematically collapsed rows. Default four-step tessellation keeps its
  existing output. Tests retain genuinely narrow patches and verify interpolated UVs and light.
- **Ray-traced world lighting:** The Light bake strip's **Ray bake…** command gathers sky and material emission
  over each corner's visible hemisphere, with up to four diffuse bounces. It uses an optional OpenGL compute
  intersector and a portable CPU reference, also available to agents through `bakeLighting` with `transport: "ray"`.
  Opaque static architecture can block the gathered light. The manual operation preserves sector ambient and
  model lighting, validates resource revisions, supports cancellation and publishes one undo step. The existing
  compatibility bake remains the default. See [the lighting guide](docs/lighting.md#manual-ray-traced-world-lighting)
  for supported materials, limits and the difference between world corners and model lighting.

- **Full-map static snapshots:** Lilivane fixed a preview limit that prevented RED from rendering
  the complete Bespin conversions for Jedi Knight and Mysteries of the Sith. Indexed archive previews
  now accept up to 512 distinct model and sprite references without relaxing memory or raster limits.

- **JO/JA scene import:** **Tools > Import JO/JA Map…**, CLI `jo-ja-inspect` / `jo-ja-plan` / `jo-ja-apply` and three
  matching MCP tools share a bounded Raven RBSP v1 importer. It tessellates world patches, preserves source UVs,
  samples first-style lightmap luminance, creates loader-sized 3DO chunks, assigns available stock footstep categories,
  and packages a new JK GOB or MotS GOO with a report and source entity text. Materials require explicit mappings or
  opted-in checkers; additive effects can be explicitly skipped. MotS architecture receives ARCH lighting records.
  Prepare writes nothing; create rechecks captured inputs and refuses existing outputs. Brush collision, moving
  submodels, shader effects, source sounds and gameplay remain authoring work. Source-audio import explicitly refuses
  until playback mapping exists. See [JO/JA scene import](docs/jo_ja_import.md).
- **Shadow-cut review fixes:** Size-limited cuts now require finite surface reach and visibility through openings;
  rejected cuts leave COG surface references unchanged. Removal preserves authored UV and lighting seams, and the
  editor clears or remaps face selection after renumbering. The dialog reports traversal truncation. Removal is
  structural merging, not a recorded inverse: it can also merge compatible authored splits and leave perimeter corners.
  Undo remains the exact restoration of the previous edit state.

- **Shadow cuts (E-16):** JK stores light at a surface's corners and blends it across the face, so a shadow edge can
  only fall where there are corners. **Edit > Sector > Shadow Cuts** and the agent's `shadowCuts` add them before the
  bake, in two modes: along the planes where each light's view through the openings ends (as CleaveLight and Lighter
  did in JED), or by splitting every surface a light reaches until no edge is longer than a limit. Blocks Light
  openings stop the light and No Block lights cast none. The cuts are estimated and run on a copy, refused past a
  budget, and are one undo step; **Remove Shadow Cuts** and `removeShadowCuts` merge them back for a recut. Openings
  are never cut, and a cut that would give a surface more than the engine's 24 corners is undone (the first agent
  test found a wall at 32). In a doorway test the bake then lights the new corner where the light's edge meets the
  floor. Shadows cast by models, and a painted density field, are still to come (100 operations).

- **Saving no longer moves corners:** JKL files kept every world vertex to eight decimals, so a corner a cut had
  computed moved slightly on save. A later cut through that corner then missed it by a hair and was refused as a
  zero-length edge, which made Lumen's multi-patch builds fragile. A corner that eight decimals hold is still written
  as JED writes it; a coordinate beyond the serializer's calculation-noise tolerance is written with the shortest
  spelling that reads back to the same value, in fixed notation. Values within that tolerance can still normalize to
  eight decimals; this is not a bit-exact guarantee for every double.

## 0.7.0

- **Sector thrust clipboard:** The IJIM sector Properties panel now reports **Copied** only after RED verifies the vector on the system clipboard. A failed copy shows an error instead.

- **SOTE sets in the COG template library:** four sets from the Shadows of the Empire port by Coda, Lily, Andrei
  and Red: a space fighter (`sg30_flight`, `sfp_view`, `sfi_state`, `sg30_missile`, `sf_weapon`), a space gunner
  (`sg_flight`, `sg_missile`), a jetpack (`jp_item`, `jp_level`, `jp_pickup`, `jp_body`) and enemy classes
  (`sote_atst`, `sote_troop`, `sote_wampa`, `sote_droid`, `om_ig88`). All 16 compile for JK with no errors or
  warnings. A set without template rows now lists the templates its scripts name, in its notes and in
  `cog-templates` (`namedTemplates`). The Hoth snowspeeder is left out: its mission and wingman COGs need 622 and 570
  label slots, past JK's 512.
- **COG template library and the Pursuit assistant:** `data/cog_templates` ships Lily's original vehicle and enemy
  controllers. Two sets come from her Star Wars Demolition port: the vehicle controllers (snowspeeder, speeders,
  walkers) and the arena hazards. The rest come from the KnightCop kit, with their 26 template rows: the player swoop (`swp_fleet`, `swp_ride`,
  `swp_aux`, `swp_ram`), enemy swoop pursuit (`kcswoop`), the bowcaster Tusken on a Kell (`kc_bow`, `kckbody`) and
  the rideable Kell (`kc_mount`, `kc_geom`). No models, animations or sounds; each set lists the files the user
  supplies. Tools > COG Templates and agent `cog-templates`/`cog-template-write` copy a set into a project without
  overwriting anything. Thing > Assistants > Pursuit (enemy AI) and agent `pursuitAssistant` place an enemy's
  controller: its route markers in order, an optional wake sector, wake distance, speed, ram damage, fire interval
  and the rider's survival chance (98 operations, 53 commands). All nine scripts compile for JK with no errors or
  warnings; `swp_aux` uses 506 of JK's 512 label slots. See `docs/cog_templates.md`.
- **3DO hierarchy and KEY authoring:** **Tools > 3DO Hierarchy…** opens a 3DO, shows its node tree, and renames,
  reparents, moves, adds and removes nodes, saving a new 3DO with the geometry unchanged. It refuses cycles, a moved
  or removed root, unknown parents and meshes, and unusable names. It starts a KEY from a model (a rest-pose track per
  node) and checks a KEY against its model: missing or doubled joints, renamed parts, entries past the end, too few
  joints and stale deltas. It saves a copy with the deltas recomputed: change per frame to the next entry
  (Blender-Sith's rule), angles the short way round, and a last entry that holds at the end or loops back. That
  matches all 5,417 segments of 60 stock Kyle KEYs. Agent `model-hierarchy`, `model-hierarchy-edit`,
  `key-from-model` (51 commands), `computeDeltas` in `key-plan`/`key-apply`, `modelIssues` in `key-inspect`; MCP
  `sed_model_hierarchy`, `sed_model_hierarchy_edit`, `sed_key_from_model` (49 tools). See
  `docs/model_hierarchy.md`.
- **Gameplay assistants:** **Thing > Assistants** builds doors, elevators, switches, objectives and cutscenes from
  the selection in one undo step: path frames for the moving Things and one placed COG. Doors (`00_door.cog`), lifts
  (`00_elev_switch.cog`) and locked doors with switches (`00_doorswitch.cog`) use the stock scripts; objectives
  (`red_goals.cog`: reach, use or kill goals, an optional exit) and cutscenes (`red_cutscene.cog`: the view follows
  a camera Thing's frames) use scripts RED writes into the project. **Try it** runs the result in the COG runtime
  and checks it: the door opens and closes, the lift comes back, the switch unlocks the door, every goal completes
  before the exit ends the level, the view returns to the player. The assistants refuse targets they can't use and
  warn about door sectors that hold other Things. Agent operations `doorAssistant`, `elevatorAssistant`,
  `switchAssistant`, `objectivesAssistant` and `cutsceneAssistant` (97 operations), commands `assistant-check` and
  `assistant-script` (48 commands), MCP `sed_assistant_check` and `sed_assistant_script` (46 tools). The COG runtime
  now simulates the camera and actor flags. See `docs/gameplay_assistants.md`.
- **COG runtime:** RED runs compiled COG scripts on a simulated clock against a stand-in world (`CogRuntime`; agent
  `cog-run`, MCP `sed_cog_run`). The virtual machine keeps the retail engine's quirks: a 64-value stack, arithmetic
  that always gives a flex, `!=` on ints, an empty stack that reads -1, four levels of `call`, handlers found by
  message id, and messages that run during a sleep. Things move along their path frames at the speed a script asks
  and send `arrived`; scripts sleep, set timers and pulses, wait for things to stop and message each other. Over a
  hundred common verbs are simulated, and the rest are logged and return 0. A request takes an optional hash-bound
  level, scripts with a placed COG's values or explicit bindings, and timed activate, enter, cross, damage and message
  events; the result is a timed log and the things, surfaces and sectors that changed. The stock two-piece door runs as
  in the game. See `docs/cog_runtime.md`.
- **COG compiler:** RED compiles COG scripts the way retail JK 1.01 and MotS load them (`compileCog`; agent
  `cog-compile`, MCP `sed_cog_compile`, and the app's COG check dialog). It reads the header like the engine's config
  reader, parses with the retail grammar, and emits the engine's opcodes. It says whether the game loads the script,
  giving the line the engine's console would print for a syntax error. It also reports what the game accepts
  silently: unknown verbs and messages, verb argument counts (from signatures generated from OpenJKDF2's verb code),
  verbs that return nothing used as values, missing labels, assignments that overwrite global verbs, and exhausted
  symbol, link, handler and label tables. In both retail games `ModifyColorEffect` runs `FreeColorEffect`. All 543
  stock JK scripts load, and 784 of 790 stock MotS scripts; the six that don't are really broken. See
  `docs/cog_compiler.md`.
- **Bake Options:** **Options…** on the Light bake strip opens a dialog for the bake profile settings that until now
  lived only in `red.ini`: fully lit material emission (intensity, range), one diffuse bounce, uniform sky light
  (intensity, range, color), and Light Baker's angle shading and water absorption. It reads and writes the same
  `[lighting]` keys the bake reads, with **Restore Defaults**; the next bake uses them. Both bake paths now read
  these settings through the same function as the dialog.
- **Stair measurements:** the agent workflow guide records which stair treads and risers retail JK's player climbed
  (0.10–0.12 JKU treads did; 0.06 JKU treads stalled), as a starting point for the optional `maxStepRise` advisory.
- **Not planned:** Pascal SED's plug-in developer mode and Template Creator are dropped from the README's list of
  things not built yet, since nobody is known to have used them.
- **Models larger than retail JK has been seen to draw are flagged:** `model-import` reports `overRetailTestedSize`
  (with `retailTestedFaces`, 512), and the app's import report adds a warning, when a model has more than 512 faces
  in all. Package planning warns with `ModelOverRetailTestedSize`; the package is still built. In retail JK 1.01 a
  1,199-face model split into meshes that each fit crashed the 16-bit renderer every time, while a 479-face single
  mesh drew in both; the stock models have at most 392 faces. `model-import`'s default split keeps each mesh within
  the loader's limits but cannot make a large model safe.
- **Documentation:** the README is rewritten for level makers: what RED does, what's new, download and first run on
  each platform, the games, known limits in one place, and a condensed comparison with upstream SED. The level
  designer guide now calls the editor RED, documents the Sun, Sun anchor and Multiply light flags and the bake options
  that live only in `red.ini`, and the capability matrix, gap register and plug-in notes record the Light Baker work,
  agent JED output, the snapshot's model and baked-light drawing, the package model checks and RED-built levels run
  in retail.
- **`tools/model-fitting`:** scripts that take a generated model (Tripo GLB, FBX or OBJ) to a JK 3DO and MAT through
  Blender and the Blender Sith add-on: a meshopt decoder for Tripo's compressed GLBs, reduction (collapse, or Wren's
  unsubdivide and planar dissolve for quads), flat-face triangulation, supersampled baking onto fresh UVs, fully lit
  glow faces, UV sheets for a painter, and a mesh merger for testing a model as one mesh. With Wren's per-mesh budget and lessons (Wren, AI collaborator, via Grok
  Bot; MIT).
- **Agent parity, second batch:** `flipSurfaces`, `mirrorSectors`, `mirrorThings`, `mirrorLights`, `rotateThings`,
  `rotateLights`, `duplicateSector`, `fitAndAdjoin`, `stitchSurfaces`, `mergeSurfaces`, `mergeSectors`,
  `subdivideSurfaces`, `insetSurface`, `planarizeSurfaces` and `repairSurfaces` (92 operations) give the agent the app's
  Transform commands and the remaining Surface and Sector commands over the same core functions. A rotated or mirrored
  Thing takes the one sector it lands strictly inside, or is refused. Operations that renumber faces or rooms are
  refused once COGs are placed, as the cleaves are. `docs/agent_parity.md` is updated.
- **Packages refuse models Jedi Knight cannot load:** `package-plan`, `package-apply` and the app's packaging read
  every 3DO they include and report `ModelOverEngineLimits` (an error) for each mesh over the engine loader's limits:
  512 vertices, 768 texture vertices, 512 faces, 24 corners on a face. JK refuses such a model whole, so its Thing
  draws nothing; found when Lumen's 1,199-face pump never appeared in retail. Core `model3doEngineLimitIssues`; the
  importer's limit constants now come from the same place.
- **Fully lit model faces in snapshots:** a 3DO face in light mode 0 (fully lit), such as a lamp's lens, now draws at
  full brightness in `snapshot`, as in the engine, instead of taking its sector's ambient light.
- **Light Baker conventions in the bake:** RED's bake follows some of LadyEebs' JED Light Baker, so a level lit for her
  plug-in bakes much the same in RED; everything is baked into corner light, so levels stay retail compatible. A light
  flagged `0x02` is a sun: the direction from the anchor (`0x08`, or the origin) to the light points at it, and it
  reaches a corner only when a ray walked through the adjoins leaves through a sky surface, at `intensity x cos`, fading
  through a ceiling sky near the horizon. A light flagged `0x20` multiplies the light within its range by its color,
  fading to no change at the range (in MotS `0x20` stays Affect Arch Things). Two opt-in options: `angleShading`
  (N·L for point lights and emissives) and `waterAbsorption` (light that crosses underwater sectors takes on their
  tint; a body of water absorbs `pow(tint, min(depth, 1 m))` however many sectors it spans). The agent's `bakeLighting`
  takes both and reports `angleShading`, `waterAbsorption`, `sunLights` and `multiplyLights`; the app reads them from
  `lighting/angle_shading` and `lighting/water_absorption`, and its light Flags editor offers Sun, Sun anchor and
  Multiply. Core `LightingBakeOptions`, `LF_Sun`, `LF_SunAnchor`, `LF_Multiply`. Separately, fixes for four bugs found
  while reading her baker (refraction that never changed a ray's path, water absorption that counted the air and
  repeated per sector, N·L applied twice with physical falloff, and multiply lights going black at their edge) are
  prepared as pull requests for her repository.
- **Agent parity, first batch:** `deleteThings`, `deleteLights`, `deleteCogs`, `setSectorProperties` (extra light,
  tint, sound), `setSurfaceProperties` (extra light, UV scale), `unadjoinSurfaces`, `makeUnderwater`,
  `placeSectorLights` and `removeRedundantVertices` (77 operations) give the agent those app commands over the same
  core functions; each reports in `editReports`. `docs/agent_parity.md` inventories every app command against the agent
  and lists the gaps still open.
- **The agent writes JED, and opens ASC and LEV:** `convert` and `new` write `.jed` (JK and MotS), and `plan`/`apply`
  take `outputFormat: "jed"` to write an edited level as JED; without it a JED input still plans JKL. Each JED is
  read back first and refused (`jedRoundTripLoss`) if the project, sectors, faces, corners, Things, their templates,
  lights or COGs change. What JED cannot hold (level templates, Thing instance names) is listed in `jedLosses` and is
  written only against the reviewed output hash (`jedLossUnacknowledged` otherwise). `inspect`, `plan` and `convert`
  open `.asc` and `.lev` as the app's Open does. The JED writer now names each Thing's template, not its instance name:
  a JKL Thing whose name differed from its template came back from JED with a template that does not exist (the app's
  JED save had the same fault). The MCP `sed_convert` description lists the new formats.
- **Mirror floors:** `mirrorFloor {sector, surface, reflectivity?, openingMaterialIndex?, allowOverlap?}` (68
  operations) is a reflection retail Jedi Knight can draw. The room is copied upside down under the named floor face
  and the floor becomes a translucent portal into the copy that still holds the player up (visible, no movement,
  floor and collision flags), so the renderer's fixed translucent blend shows the room reflected in the floor. The
  copy reflects every vertex, reverses each face's corners and keeps their texture coordinates, which mirrors the
  textures; it is appended as the last sector, so no sector or surface number moves. Its doorways become walls, each
  textured as the continuation of a wall in its plane where the room has one. A copy that would overlap another room
  is refused unless `allowOverlap` is true (nothing can enter it). `bakeLighting` treats each mirror floor as solid
  for the bake (both faces get the editor's block-light bit, then their flags are restored, so the level never
  carries it) and then copies the room's light into the reflection at `mirrorReflectivity` (default 0.5); its
  summary reports `mirrorFloors`. Mirrors are found by geometry, so a room edited after its mirror was built needs
  the mirror rebuilt. Core `levelAddMirrorFloor`, `levelFindMirrorFloors`, `levelRefreshMirrorFloors`. Walked in
  OpenJKDF2 on a copy of Moonwell Relay.
- **Translucent faces in snapshots:** the software snapshot draws translucent world faces (face flag 0x2) after
  everything opaque, farthest first, blended over what lies behind them at the engine's 90/255, testing depth without
  writing it. A mirror floor, a window or a water surface now shows what it covers instead of being drawn solid.
- **Painted lamp glows:** `mat-create` takes `paint {glows, fills, wrap}`: soft additive glows (strength*(1-d^2)^2,
  screen blend) and solid lens fills painted into every cel before the MAT is built, wrapping across tile edges. One
  request made as RGB565 and as dithered indexed8 gives a 16-bit MAT and its 8-bit twin. Core `paintGlows`.
- **Lamp cuts:** `carveSurfaceRect {sector, axis, at, min, max, materialIndex?, uvSize?, lightMode?}` (67 operations)
  cuts a rectangle out of the free faces on an axis-aligned plane of a room and gives the pieces a material, a texel
  fit stretched across the rectangle (keeping each face's handedness) and a light mode, so a fixture or lens can be
  fully lit. Height cuts go first so the cuts across add no corners to the floor or ceiling; built after a mirror-floor
  test made a 36-corner floor the engine refused. Faces are found again by geometry after every cut.
- **Level master colormaps:** `red-agent cmp-master-create` (MCP `sed_cmp_master_create`, 42 tools) builds a master
  colormap with new colours in the per-level palette slots 192-254, as the stock level masters do, so 8-bit textures in
  colours dflt.cmp lacks (teal, violet) stop dithering into noise. Slots a protected MAT draws are passed over and
  reported; colours can be fullbright (8-bit lamps). Light and transparency entries for the new colours follow the
  rules measured on dflt.cmp (luminance-weighted nearest colour: 98.7% of its light tables reproduced exactly);
  every other byte is the master's. Core `makeMasterCmp`, `nearestCmpColor`, `matPaletteIndices`.
- **Player starts left out of static Thing previews:** with `"thingMode": "static"`, snapshots and overviews now omit
  Things whose effective type is `player` (reported as `omitted-player-start`), because the game never draws a start
  from the player's view: a capture from the start position looked out through Kyle's head. `"playerStarts": "draw"`
  shows them. Found comparing Moonwell captures with the engine.
- **Stale collide boxes no longer hide portals:** a cut room kept the collide box of the room it came from, and the
  JKL writer kept an authored box over the computed one. The engine treats a Thing inside a sector's collide box as
  inside that sector without testing its faces, so in the written level a player could not cross the new portal: the
  game held them in the old sector and undid the move. The writer now keeps an authored box only while all its
  corners are inside the sector and otherwise writes the computed one. Found walking a carved copy of Moonwell Relay
  in OpenJKDF2.
- **Tinted colormaps carry a transparency table:** `cmp-create` now copies the master's transparency table (flags 1)
  when the master has one. The stock tinted maps have none, and a translucent face in a tinted sector (water in a
  coloured cistern) would leave the 8-bit renderer without a table to blend through; the table blends palette
  entries, and the palette is the master's, so the master's table is the right one.
- **Surface corner limit:** `validate` reports `surface-corner-limit`, an error, for a JK, MotS or Droidworks surface
  with more than 24 corners. The engine refuses such a face, and with it the whole level, but RED called the level
  valid. Cleaving hands the cut points to neighbouring faces, so a busy wall can pass the limit without a
  25-sided face ever being drawn.
- **Delete rooms from the agent:** `deleteSectors {sectors, materialIndex?}` removes rooms so a volume can be carved
  by cleaving and deleting (a bridge across a void, a pillar), with the faces that were portals into them sealed as
  textured, colliding walls in the given material. Refused while a Thing stands in a named room or once COGs are
  placed.
- **Geometry operations on a level RED wrote:** after a cut across a slanted edge, the level as written could not be
  cut again or gain a room anywhere: the corner the cut put on the edge was rounded to the file's precision, and the
  agent's room check, stricter than the editor's, refused it as no longer convex. Rooms read from a file are now
  admitted with the allowance the editor's geometry tools give their inputs; new rooms stay strict. Found while
  tessellating a copy of Moonwell Relay for baked lighting.
- **Tinted colormaps:** `red-agent cmp-create` (MCP `sed_cmp_create`) makes a tinted CMP from the level's master
  and an RGB tint, the way the stock red maps are made: the 8-bit renderer takes the colour from its light tables
  and the hardware renderer from its header tint. Give it to a sector with `registerColormap` and
  `setSectorColormap` to colour a room without engine changes.
- **Baked-light snapshots:** `snapshot` and `overview` accept `"lighting": "baked"`, which shades each corner by its
  stored light as the game combines it (sector and face extra light, light mode 0 fully lit) and multiplies a
  sector on a non-master colormap by that colormap's header tint, read from the resource archives and reported in
  `colormapTints`. A bake, emissive faces and tinted colormaps can be judged from a RED image before the game is
  started. The image approximates the 16-bit hardware renderer, not the 8-bit light tables. The default stays
  sector ambient.
- **Stair rise advisory:** straight and switchback agent recipes accept an optional caller-selected maximum riser.
  Plan/apply reports warn without clamping geometry, prescribing a universal threshold or claiming gameplay safety.
- CLI/MCP snapshots and overviews accept optional `thingMode: "static"` for bounded LOD0/rest-pose 3DO and
  classic cel0 SPR previews with shared depth, source provenance and explicit omission diagnostics. The default
  world-only profile remains unchanged; no runtime animation, lighting or gameplay parity is implied.

- **MAT request discovery:** `schema mat-create` exposes the indexed8/RGB565 request contract, shared with the MCP
  tool catalog, while retaining existing conversion, captured-file and create-only publication behavior.
- **Model and archive request discovery:** `schema model-import` and `schema archive-create` expose those request
  contracts, shared with the MCP `sed_model_import` and `sed_archive_create` tool catalog. A contract test runs
  each fixture through both the schema and the command, so they cannot drift apart on request shape.
- **Conform Sealed Seams in the editor:** **Map > Conform Sealed Seams** closes the sealed-seam T-junctions validation
  reports, as one undoable edit, with the core pass behind the agent's `conformSeams`. Exact corners go in without a
  question; angled-edge corners are offered first; competing corners are never inserted, and the first seam left is
  selected.
- **Loose resources on case-sensitive file systems:** a loose file now resolves whatever the case of its folders
  and name, as archive entries already did. On Linux a resource folder copied from Windows, or one whose spelling
  differs from the level's (`MISC/CogStrings.uni` for `misc/cogstrings.uni`), used to read as missing, so the editor
  fell back to an archive copy or showed nothing, and Save JKL & Test staged resources the game already had.
- **Snapshot and overview request discovery:** `schema snapshot` and `schema overview` expose the two image
  request contracts, shared with the MCP `sed_snapshot` and `sed_overview` tool catalog and held to the commands
  by the same contract test.
- **Overview image limit (behaviour change):** an overview with `output` rendered any positive size, so one request
  could ask for tens of gigabytes of image and depth buffer. A rendered overview now takes snapshot's upper limits
  (2048 pixels per side, 2,097,152 pixels) and refuses more as a usage error. Framing without `output` is unchanged.
- **Wall UV projection (behaviour change):** `batchUvProject` and the Workbench's Batch UV projection ran V up the
  wall for the +X, -X, +Y and -Y directions, so every projected wall texture was drawn upside down in the game.
  They now run V down the wall and keep U x V on the direction. Floors and ceilings (+Z, -Z) are unchanged. A
  build script that compensated for the old result needs that compensation removed. Projecting toward a wall
  (against its inward normal) shows the texture as its image is stored; projecting along the inward normal shows
  the mirror image, which is the handedness new faces and the retail levels' walls have.
- **Connect:** the `connectSurfaces` patch operation is the editor's Connect for the agent. Two overlapping walls
  are cut along each other's outline and their shared area becomes the portal, so a doorway through an `addBox`
  frame is one operation per side instead of four cleaves and an adjoin. The receipt names the portal faces.
- **Seam conforming:** the `conformSeams` patch operation resolves the sealed-seam T-junctions `validate` reports
  by inserting each neighbour's corner on the edge that lacks it. It splits and renumbers nothing, so it works on
  levels with placed COGs, and it refuses the whole operation rather than leave a partial edit. The receipt
  reports every insertion. See [conforming sealed seams](docs/headless_validation.md#conforming-sealed-seams).

- **Model lighting:** inspect and bake ordinary 3DO vertex lighting through the portable core, CLI, and MCP. New
  asset variants retain all LODs and every non-light source byte. Recipes support scalar/RGB profiles, point and
  directional lights, optional bounded opaque self-occlusion, reviewed output hashes, and create-only publication.
- **Import shading:** OBJ/GLB import preserves authored normals and hard edges by default, with explicit geometric
  recalculation and normal-split diagnostics. Inverse-transpose GLB normals retain valid directions at extreme scales.
- **MCP reliability:** path checks resolve existing symlink targets; journals reject unsafe paths and damaged history.
  Checked journal I/O prevents execution when the initial record fails and discloses a command that ran when its final
  journal record cannot be saved.
- **Linux verification:** CI includes the Qt-free core and agent suite. Its synthetic capture-runner tests need no
  Xvfb; the Linux Qt test gate and real Linux engine captures still use a private Xvfb display.

Model baking is a headless asset-authoring workflow. GUI wiring, texture-aware/world occlusion, animated relighting,
and retail-game acceptance remain separate work; see [model lighting](docs/model_lighting.md).

## 0.6.0 (source milestone; no tagged release)

This version brings the preservation and automation review together with Red's Windows clipboard repair,
launch-to-test workflows, and focused viewport and packaging corrections.

### Highlights

- **Reliable Copy:** entity and architectural-lighting copies verify their private clipboard payloads before
  reporting success. Windows publication services delayed-render requests during bounded retries, and detected
  competing clipboard changes stop the retry instead of replacing the newer entry.
- **Game testing:** separate retail and OpenJKDF2 launch profiles for JKDF2 and MotS preserve the editable project
  path. OpenJKDF2-compatible direct-map startup and retail episode-menu startup are distinct workflows.
- **Preservation:** native project copy/no-op operations retain opaque optional state; unsupported edits refuse
  instead of silently discarding it. KEY editing retains engine fields, sparse identities and fractional timing.
- **Agent authoring:** connected room building, Thing/light operations, bounded detailed queries, stricter JSON and
  numeric validation, immutable resource snapshots, atomic new-file publication and raster depth/bounds fixes.
- **Editor regressions:** grouped resource-tab icons, decimal-format isolation, clipboard routing, selection history
  and architectural wireframe coverage have focused tests.
- **Packaging:** complete Qt notice lists travel through generated settings files, avoiding the Windows shell's
  command-length limit. Native build-tool tests cover long lists and literal path characters.
- **Linux and macOS previews:** a Release builds workflow packages the editor, `red-agent` and `red-mcp` for
  Linux x86_64 and macOS arm64. Floating-point text parsing goes through `sed::text::fromChars`, which falls back to
  a locale-independent parser where Apple's libc++ lacks the standard overloads. Vertex-color rows that overflow
  a float are reported unavailable on every platform, not only under MSVC.

### Verification boundary

The release record distinguishes automated source checks, native editor tests, isolated package startup and actual-game
acceptance. A launch plan or successful process start does not establish playable geometry, COG execution or complete
retail/OpenJKDF2 feature parity. See the [capability matrix](docs/capability_matrix.md) and
[gap register](docs/gap_register.md) for remaining compatibility work.

## 0.5.0

Version 0.5.0 converts all 45 known legacy JED/SED plug-in families (E-01..E-19) to working native RED
equivalents selectable in the editor and the headless agent, each with core implementation, tests, and
capability-matrix receipts. Design-gated items are recorded in the gap register instead of guessed.

### Highlights

- **Light Baker (E-17):** fullbright surfaces emit light, with bounce and sky-emission profiles in the
  editor (QSettings-backed) and agent `bakeLighting`.
- **Recipes and builders (E-02..E-07, E-09, E-10, E-14):** masked surface recipes, stairs/switchback/
  spiral/dome/swept-corridor builders, sector arrays, heightmap terrain with 3DO model output, prefabs,
  thing paths, underwater and breakable-glass compounds.
- **Workbench (E-01, E-08):** level statistics, planar/cylindrical/spherical batch UV projection in the
  Workbench dialog and agent `batchUv*` operations.
- **Assets and scripts (E-11, E-12, E-13, E-18):** agent `cog-check` lexical COG preflight against the
  measured JK/MotS verb registries, KEY timeline editing plus `key-inspect` (with KEY↔3DO node
  cross-checks), DXF export/inspection and the scoped DXF-to-sector compiler, and the `mat-inspect` /
  `mat-export-png` MAT bridge.
- **CI:** GitHub Actions now runs the core gate and the full Qt build on every push.

### Verification boundary

The core/agent MSVC Release gate passes 102/102 CTest entries and the full Qt Release build is green,
including `sed_app_main_window_tests --legacy-tools`. Runtime/game acceptance remains unverified: the
local OpenJKDF2 fork's capture path completes no frame (see gap register F-09), and E-16 (shadow model)
and E-19 (typed extension API) await owner design decisions. See the
[capability matrix](docs/capability_matrix.md) and [gap register](docs/gap_register.md).

## 0.4.1

Version 0.4.1 restores legacy-editor workflows and plug-in compatibility, adds native editing recipes, and fixes
transaction, identity, and input-validation defects found during the follow-up review.

### Highlights

- **Restored Pascal SED commands:** Paste in Place, Revert to Saved, Make a Backup Copy, automatic `.bak` creation,
  Duplicate Entities, Planarize, Calculate Angle, grid-relative transforms, surface-flag repair, component rebuilding,
  Delete Saved Shape, Make Door, camera helpers, and keyboard Nudge.
- **JED plug-ins:** the Plugins menu hosts legacy `IJED`/`IJEDLevel` DLLs, including an out-of-process bridge for
  32-bit plug-ins and crash isolation. The bridge does not provide a sandbox or Pascal SED `ISED` compatibility.
- **Native editing recipes:** Remove Redundant Vertices, Place Sector Lights, Build Stairs, Fit and Adjoin,
  Make Underwater, and Make Breakable Glass provide native alternatives to legacy plug-ins.
- **Game Preview:** adjustable texture brightness is available without changing the level or Editor view.
- **Review fixes:** Planarize and recipe validation preserve level state on refused inputs; orphaned 3DO meshes no
  longer leak into hierarchy-expanded loads; MotS ARCH previews resolve sparse source identities; shared Thing-frame
  operations are available through reviewed agent and MCP workflows.

### Verification boundary

The configured core-only MSVC Release build and all 88 configured CTest entries passed after the final review fixes.
The complete Qt Release gate, package-specific checks, native visual acceptance, and actual-game acceptance remain
separate requirements. See the [capability matrix](docs/capability_matrix.md) and [gap register](docs/gap_register.md)
for the current evidence and open limitations.

## 0.4.0

Version 0.4.0 is a ground-up C++ rebuild of SED using Qt and OpenGL. It carries forward the Sith/JED level-design
model in a modern editor, with geometry authoring, resource tools, project recovery, and a redesigned viewport workflow.

### Highlights

- **Native projects and engine export:** **Save** and **Save As** create the editable `.sed` project, which retains the
  level and supported editing-session state. **Export** and **Export As** write game formats separately: `.jkl` for
  JKDF2/MotS and `.ndy` for IJIM. Optional legacy `.jed` export remains available for JKDF2/MotS. See the
  [save and export guide](docs/user_guide.md#15-save-save-as-and-engine-export).
- **One editing viewport:** Editor mode brings level authoring into a WYSIWYG 3D view, with selectable edit modes,
  multi-selection, configurable camera and input controls, transformed grids, and project-aware snapping. Game Preview
  provides a separate read-only rendering and inspection view.
- **Geometry and UV authoring:** Rebuilt sector and surface editing with staged Multi-cut, editable rectangle, N-Gon,
  arch, and star shapes, plus Join, Merge, and surface extrusion workflows. The docked UV editor adds multi-island
  transforms, Smart UV tools, alignment, stitching, packing, and snapping. Accepted edits preserve history and reject
  invalid previews without partially changing the level.
- **Lighting:** Author baked lights and recalculate their affected regions asynchronously. Inspect frame-local Thing
  lights and headlights, IJIM Sector point lights, and MotS Arch model-vertex lighting in the viewport.
- **Scripts and resources:** The COG Script Editor adds multi-file editing, diagnostics, completion, formatting,
  outlines, and resource helpers. Separate SPR, SND, and UNI editors provide project-aware controls and previews, with
  docked or detached files, local history, and Save All. Template editing and resource discovery are also rebuilt.
- **Session saving and recovery:** Native projects retain workspace and selection, open COG and resource-editor drafts,
  pending Multi-cut work, and optional Level Undo/Redo history. Autosave history, live crash-recovery checkpoints, and
  parity-assisted repair can recover some damaged files. Repair has bounded limits and is not a backup.
- **Reliability and performance:** File and resource readers validate input sizes, structure, and numeric values.
  JKL/NDY imports reject malformed records before a level is published. JKL/NDY exports stage and atomically replace complete
  output. Geometry previews validate before commit, accepted edits remain undoable, and rejected previews leave the
  level unchanged. Asynchronous lighting and asset preparation, targeted viewport updates, and bounded rendering and
  resource caches reduce repeated work.

### Known limitations

- The Pascal DLL plugin system has no source-compatible C++ API, Plugins menu, or Plugin Developer Mode. See the
  [verified Pascal compatibility gaps](docs/porting_guide.md#verified-live-pascal-gaps).
- Game Preview renders and inspects a level. It does not run an executable gameplay simulation. The shaded viewport
  also does not reproduce MotS special shading for 3DO hierarchy nodes marked `0x2`.
- Clipboard workflows have no **Paste in Place** or Insert-style duplication of the selected Sector/Thing/light set.
  Standalone Surface, Edge, and Vertex copy/paste is also unavailable. Copy/paste for Sectors, Things, and baked lights
  is supported.
- JED remains available for import and migration, but `.jed` export is limited to JKDF2/MotS. IJIM exports NDY
  instead. LEV and ASC can be imported, but have no export paths. General 3DO hierarchy authoring, GOB/GOO project
  creation, and launching the game from SED are also unavailable.
- Native recovery repairs only corruption within its checked limits and cannot replace independent backups. SED has no
  **Revert to Saved** or **Make a Backup Copy** command and does not create an automatic `.bak` before overwriting a
  level. Explicit UV pivot choices and unaccepted Template Editor modal drafts are not restored. See the
  [editor-state inventory](docs/editor_state.md).

## Version 0.3
- Fixed the flag editor to allow simultaneous selection of water and lava surface flags, enabling the creation of 'cold water' surface types.
- Changed keyboard shortcut for View-To-Grid command in the Map Editor [PR #36](https://github.com/smlu/SED/pull/36) 
- Updated keyboard key commands in 3D Preview [PR #36](https://github.com/smlu/SED/pull/36) 
- Fixed calculating intensity from RGBA color
- Fixed assigning blue channel in ``SurfaceSetVertexLight` JED plugin API function
- Updated C/C++ SED plugin SDK
- Fixed plugins code
- Added missing definition for `TCOMWFRenderer.DrawThing`
- Changed `TSedPluginLoad` calling convention to `cdecl`
- Changed default data structure alignment to 8 bytes (quad word)
- Fixed calculating intensity from RGBA color
- Added plugin developer mode
- Unify procedure for showing options dialog  
  This resolves issues where not all parts of editor were refreshed after new changes were made to the editor settings.

- Map Editor:
  * Fixed DPI scaling
    This fix also resolves map movement with keys when zoomed in

- 3D Preview:
  * Fixed rendering MOTS level without UV clamping
  * Added option to render in solid geometry mode

- Flags Editor:
  * Removed Clamp X/Y flags for JKDF2 & MOTS projects

## Version 0.2.0
- Fixed division by zero in `FindUVScales` function.
- Fixed calculating yaw & roll angles in `TMatrix43.ExtractPYR` function.
- Fixed scaling color component when recoding MAT texture pixel to destination format.  
  This fixes the issue with the color component being scaled to 0xFF when recoding from 16-bit to 24-bit or 32-bit format and vice versa.  
  In essence, it affects the texture color, especially the alpha channel where solid textures with alpha channel were not fully opaque due to now fixed scaling issue.
- Replaced unit `GLunit` with `dglOpenGL` to support OpenGL up to version 4.6.
- Removed obsolete and unused code & units.

- Map Editor
  * Deprecated software renderer in favor of OpenGL renderer.
  * Renamed renderer units

- 3D Preview:
  * Implemented horizon sky rendering in 3D preview (OpenGL only) [PR #35](https://github.com/smlu/SED/pull/35)
  * Added selection no-clip option to 3D preview settings. [PR #28](https://github.com/smlu/SED/pull/28)
  * Fixed acquisition/release of OpenGL context in `TOGLPRenderer` class.  
  * Fixed thing/surface selection in 3D preview when OpenGL is used.
  * Fixed 'Snap Grid to Item' and 'Cleave by Grid' hotkeys [PR #31](https://github.com/smlu/SED/pull/31)
  * Fixed ceiling sky depth to fall behind the level geometry and thing objects.
  * Deprecated DirectX GAPI: The DirectX Graphics API is being deprecated in favor of OpenGL.  
    The DirectX GAPI will be removed in the next major release.
  * Fixed incorrect sort range when flushing cached polygon
  * Fixed transparency rendering issues by improving distance calculation for sorting transparent polygons.  
    Now using full 3D world space distance from vertex to camera position instead of comparing vertices forward (y-axis) values,  
    which ensures correct rendering of overlapping transparent objects, especially in OpenGL.
  * Fixed rendering of translucent polygons in OpenGL by enabling alpha testing.
  * Updated OpenGL preview renderer to use curCamera FOV.
  * Refactored OpenGL renderer to replace all immediate‑mode (glBegin/glEnd) draws with batched dynamic VBO/IBO updates and single glDrawElements.
  * Renamed renderer units


## Version 0.1.0
- Added support for game Indiana Jones and the Infernal Machine (IJIM)
- Ported to latest Delphi version (unicode support)
- Added indication in window title bar when level is changed (asterisk char '*' is added at the end of filepath)
- Added option to import 3DO as sector to current project
- Added support for 24/32 bit MATs
- Added support to rotate texture UVs of multiple selected surfaces
- Added command to stitch & copy surface properties (Ctrl + stitch key(s))
- Added warning messages when trying to build sector/surface and selected surfaces/edges/vertices doesn't belong to sector
- Added new struct TColorF which represents normalized RGBA color
- Added TVector, TVector2 and TMatrix classes which implements vector and matrix arithmetics
- Added live update of Item editor when item is changed in map editor / 3D preview
- Added templates merge from level on level load
- Added multi-selection count
- Added paste in place command (Ctrl+Shift+V) which pastes from clipboard objects at their copied (original) position
- Editor configuration is now stored in sed.ini and not in windows register
- Changed default behavior to require confirmation before reverting to the saved state. Removed shortcut Ctrl + R
- Fixed adjoin surface properties when undoing sector changes
- Fixed saving surface info for adjoin surface when undoing sector cleaving.
  Due to call to DeleteSector in the undo stack the adjoin parameters were reset to solid surface.
- Fixed adjoin cleaving to leave adjoin surface properties unchanged (e.g.: not resting faceflags/surfflags to 0, not setting geomode to 0 etc..)
- Fixed bug when pasting from clipboard a non-thing data as thing which resulted in locking system clipboard
- Fixed crash in extra mode when no extra object is available
- Fixed layering thing when snapping to surface (bring thing to surface)
- Fixed surface material undo after stitching surface
- Fixed saving the correct sector to undo history before adjoin is changed in `RemoveSurfRefs`
- Fixed rotating multiple objects over pivot
- Fixed sector geo undo when sector is first changed then deleted in the same undo frame.
  This fixes undoing sector join.
- Fixed surface adjoin parameters when pasting sector from clipboard
- Fixed sector(s), light(s) and thing(s) position offset when pasting from clipboard
- Fixed left and right rotating of texture UVs
- Fixed selecting new item on visible layer after current item is deleted.
- Fixed to not highlight last deleted item
- Fixed various out of bounds reads
- Fixed merged surface(s) max vertices
- Fixed calculating surface normals
- Fixed sector values change undo on texture UV scale
- Fixed update on state change (game object, sector, surface etc..)
- Fixed progress dialog to not show up when app is not active
- Fixed resetting auto-save timer
- Fixed returning success when cleaving sector succeeds
- Fixed emitting sector changed notification when adjoining surfaces (MakeAdjoinSCUP)
- Fixed thing undo when adding new thing onto current surface
- Increased undo steps to 1024
- Optimized texture straightening algorithm to perpendicular align texture UVs
- Object editing is immediately updated in the map editor / 3D preview
- Refactored code to use RGBA colors internally
- Removed Direct3DRM
- Removed requirement for game CD (JKDF2 & MOTS)
- Updated UI and enabled per monitor DPI awareness
- Updated SPrintf to allow 0 left padding in the format (max len 9). e.g. "%1.2f" will format 0.25326 as 0.25.

- 3D Preview:
  * Added support for mouse navigation
  * Added Thing rotation via Ctrl + mouse wheel
  * Added Thing translation via Shift + W/A/S/D
  * Added command to move camera to item position (F5)
  * Added command to move Thing to camera position
  * Added command to bring Thing to surface (B)
  * Added commands extrude (Shift + X) and extrude & expand (Ctrl + X)
  * Added command merge (M)
  * Added command join (J)
  * Added command to clear multi-selection (Backspace)
  * Added command to select camera sector (Alt + S)
  * Added thing baked light rendering when light radius <= 0.01 (IJIM)
  * Added fog rendering (IJIM)
  * Added option to highlight selection and optionally surfaces with special properties (solid floors, cog linked surfaces etc...)
  * Fixed showing/hiding sectors and objects visible/hidden layer
  * Fixed selection of rotated model
  * Fixed model selection when clicking on its polygon face with the double-sided face flag set.
  * Implemented loop play of sector sound while camera is in sector
  * Implemented original triangulation algorithm
  * Implemented original rendering of sector tint color (JKDF2 & MOTS)
  * Implemented original vertex color blending (missing dynamic lights)
  * Render texture UV clamping (IJIM)
  * Render ceiling sky
  * Render 3DO model at highest level of detail (LOD 0)

- Consistency Checker:
  * Highlight warning & error entries
  * Added texture geomode check for solid surface
  * Added floor collision flag check
  * Added check for track surface (IJIM)
  * Added adjoin check for visible flag to be set when surface geomode is set to not drawn or translucent faceflag is set
  * Added total issues counter
  * Fixed current selected item when check is refreshed

- Item Editor:
  * Save undo when pasting frame to item
  * Save frame undo on change
  * Live update when current item changes in map editor/3d preview
  * Fixed selecting corresponding linked COG symbol in Placed COG editor
  * Sector:
    - Added fields:
      + ambient light
      + avg. light intensity (IJIM)
      + avg. light position (IJIM)
      + avg. light falloff (IJIM)
      + thrust (IJIM)
  * Surface:
    - Select the mirror adjoin surface when double-clicking the adjoin field
    - Fixed saving sector geometry undo on UV scale change or flip faceflags set/clear
  * Vertex:
    - Added vertex coords copy/past button
  * Thing:
    - Added color pick option for `light` & `lightintensity` param (IJIM)
    - Added sector selection on double click sector field
  * Light:
    - Added button for setting light to sector point light to selected sector(s) (IJIM)

- Flags Editor:
  * Sector:
    - Auto set/clear surface flags 0x80 - aetherium and 0x20000 - water for all sector surfaces when sector flags 0x2 - underwater and 0x100 - aetherium is set/cleared.
  * Surface:
    - Added IJIM surface flags and rename existing JKDF2 flags
    - Auto set collision surface flag when floor surface flag is set, and unset floor surfflag when collision flag is unset.
  * Thing:
    - Added IJIM thingflags and rename existing JKDF flags

- Map Editor
  * Added experimental perspective camera projection to map editor (shortcut: Num 5)
  * Added undo step when calculating lighting
  * Added support for mouse navigation & zooming
  * Added support for mouse wheel to PYR rotate selected Thing object(s)
  * Added thing & frame selection by mesh wireframe
  * Cleave sector(s) from any angle without needing to align with grid
  * Fixed calculating lighting on currently selected sector/surface/edge/vertex
  * Fixed cleaving in normal mode when view is parallel to the grid
  * Fixed scaling texture when in map editor
  * Fixed snap to vertex
  * Implemented snap to XY axis (Alt) and snap to grid (Ctrl) cleaving mode
  * [OpenGL] Implemented wireframe culling
  * [OpenGL] Changed wireframe rendering to use line loop instead of triangles
  * Prevent cleaving level geometry on invisible layer.
  * Removed command to calculate lighting for sectors on non-visible layers.
    Now light data can be calculated only on sectors that are on visible layer.
  * Render dashed object(s) on hidden layer
  * Set OpenGL as default wireframe API

- Placed COG editor
  * Fixed item selection on list refresh
  * Fixed updating COG form when selected item is assigned

- Options Window:
  * Added option to store lights data and layer info in JKL/NDY file
  * Added option to change cleave line color (default green)
  * Added color selection options for 3D preview window and cleave line
  * Added options to set display size of vertex
  * Added options to invert wireframe polygon cull coloring
  * Added option to set max vertex snap distance
  * Added option to lock map zoom in/out
  * Added option to enable/disable playing of sector sound in 3D Preview
  * Added new window resolutions for 3D Preview

- Recovery menu:
  * Added command: 'Fix solid surface(s)' which auto sets collision and floor surface flags to all selected surface(s)/sector(s)
  * Added command: 'Fix passable adjoin(s)' which sets not drawn geo mode (0) to all selected surface adjoin(s) that  can be passed through.

- Tool Window:
  * Added surface(s) rotation
  * Added surface(s) scale
