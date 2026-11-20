# Research from the downloaded JK / MotS material collection

> Copied into RED from [CodaLilitari/JK-MotS-Model-Workflow@ca011b8](https://github.com/CodaLilitari/JK-MotS-Model-Workflow/blob/ca011b84c4fa5e1f3755e1543be2c1c1d25063c9/skills/jk-mots-model-workflow/references/downloaded-corpus.md) (MIT; see [LICENSE](LICENSE.md)). Regenerate with `tools/knowledge/import_knowledge.py` rather than editing this copy.

Checked 30 September 2026. This pass examines actual retained packages, native assets, bundled documentation and tool source alongside the earlier community discussions. It covers character, creature, player-skin, weapon and first-person model decisions. Scenery encountered in an archive is inventoried but does not expand the workflow into level design.

## What was examined

The local Massassi collection contains **2,308 nonempty JK/MotS packages totaling 5,540,773,350 bytes**. Every package was rehashed against its acquisition record; all matched. The contemporaneous category reconciliation records 2,309 entries across the nine selected game categories, with one empty download: [Sanctium, entry 240](https://www.massassi.net/levels/files/240.shtml). This is broad game-package coverage, not a claim to possess every historical Massassi upload or every unavailable attachment.

The passive traversal visited 2,421 ZIP containers and 2,471 GOB/GOO containers, including nested archives. It recorded:

| Native asset | Member occurrences | Distinct SHA-256 hashes |
|---|---:|---:|
| 3DO models | 26,794 | 16,421 |
| MAT materials | 119,977 | 39,069 |
| KEY animations | 6,001 | 2,912 |
| PUP animation/role definitions | 706 | 401 |
| CMP colormaps | 148 | 62 |
| DAT tables | 589 | 464 |
| SND classes | 1,729 | 1,010 |
| UNI string resources | 2,301 | 2,112 |

There are also **3,158 indexed bundled documents**. Occurrences include repeated files, stock-derived assets and scenery; distinct hashes are byte deduplication, not character counts. Header indexing does not mean every model has been visually reviewed, fully parsed or tested in game. The chapters below identify the selected interpretive work.

The older WBW and SED original-package collections contribute **151 and 50 archives**, respectively. The other project's retained reconciliation identifies all 201 as hash matches to this download set. The separate SED tool/source collection has 29 components and two supplementary attachments; its 128 selected files total 33,994,107 bytes. Selected animation, model and material tools from that collection supply additional evidence rather than being counted as another 201 new mod packages.

The [portable coverage ledger](https://github.com/CodaLilitari/JK-MotS-Model-Workflow/blob/ca011b84c4fa5e1f3755e1543be2c1c1d25063c9/skills/jk-mots-model-workflow/references/evidence/downloaded-corpus.json) lists each package's public source, author, byte count, SHA-256, indexed asset counts and exceptions. Original game/mod binaries and complete third-party readmes are not bundled. The distributed research is the analysis, source identifiers and measured evidence needed to reuse these conclusions offline.

## Choose an example by the decision it helps answer

| Decision | Evidence to use | Practical option |
|---|---|---|
| Keep a working humanoid and change its appearance | Cyborg's actual texture-only package; Matrix's model-and-texture pack | Replace known material names for a reskin, or add a separately selected model when geometry changes |
| Preserve standard movement with a new silhouette | Kell Dragon Mutant and the verified hierarchy records | Keep the established animation contract and author visible shape around it |
| Change proportions or articulation | Shred/Hell Raiser's retargeting discussion, Puppet Jedi manuals, native custom-rig packs | Map nodes explicitly and adapt the whole reachable KEY/PUP family; compare idle and action transitions |
| Add MotS selectable characters | BoaM, Al's Skins, Darktrooper and creature packages | Inspect the actual MP model tables, personality resources and relevant scripts together |
| Make a weapon | SavageX's native template and weapon tutorial | Maintain separate pickup, external and POV representations and preserve each coordinate/material contract |
| Convert or paint a skin | MAT16, JKPaint and selected real MAT payloads | Choose indexed color, opaque RGB565 or a target-tested cutout route; preserve cells, mips and palette semantics |
| Diagnose an exporter/importer failure | Pinned source, retained fixtures and attributed community comparisons | Reproduce the precise version and isolate scene, rest pose, material and geometry variables |

Read the detailed additions in [native package integration](downloaded-model-integration.md), [downloaded tools and templates](downloaded-tools-geometry.md), [material bytes and converters](downloaded-material-tools.md), [geometry and animation methods](additional-geometry-animation.md), [painting and UV methods](additional-surface-methods.md), and [additional community diagnostics](additional-discord-model-methods.md).

## Additional native examples and identity checks

[DarkShroom's Kell Dragon Mutant](https://www.massassi.net/levels/files/1419.shtml) supplies one character model with texture and selection resources, and no KEY or PUP members. Its readme explicitly describes an MP character without new animations. This supports the stock-animation route as a concrete packaging choice; this pass does not claim a fresh gameplay test. Use the package's actual model structure when evaluating a similar creature instead of assuming that a reptilian appearance requires a wholly new rig.

[Milhouse's Imperial Enemy Pack](https://www.massassi.net/levels/files/617.shtml) shows the separate work required to turn a multiplayer skin into an SP actor. The Royal Guard example links its model to a PUP, sound class, actor COG, AI and a separate severed-arm model. The Deeptrooper example has its own animation/sound resources and behavior. Milhouse credits **Al MacDonald** for the donor skins, **Red and the Skinners** for specified character work and **Eeth Koth** for Deeptrooper contributions. Those historical names are their own provenance and must not be conflated with a present AI collaborator named Red. The pack's templates target JK; its author explicitly leaves MotS adaptations conditional. These are asset/integration lessons, not a level-building tutorial.

The larger package search also checked the earlier recovery leads. [DBZ Saiyan 2.0](https://www.massassi.net/levels/files/1903.shtml), by BeowulftheBard, identifies its own predecessor and credits Tom Slater's skin. [DBZ Fighter v1.0 beta](https://www.massassi.net/levels/files/2436.shtml) identifies IA_D_Vadergator / MG_Dash_Rendar. Neither is an authenticated TDIR beta. A bundled description in [Atom's Hyperbolic Time Chamber](https://www.massassi.net/levels/files/2552.shtml) explicitly associates TDIR with DBZ 4.0, adding a contemporary alias reference without supplying the missing build. Continue using the [bounded TDIR recovery findings](tdir-beta-research.md).

The filename search found a `sektor.mat` in [Calling the Corners — Sabers Version](https://www.massassi.net/levels/files/867.shtml). The inspected package's models and readme do not establish a Sektor player skin, and no model in that package references that material in its declared material table. Treat this as a filename hit, not a recovered character. The [Cyrax/Sektor investigation](mortal-kombat-research.md) remains unresolved at the playable-asset level.

## Read old format specifications against actual readers

The archived JKSpecs pages are valuable format references, but their tentative statements should not become mandatory export rules. The [3DO specification](https://www.massassi.net/jkspecs/3do.htm) speculates about ignored insertion offsets, vertex intensity and extra light, and generalizes about node types. A pinned OpenJKDF2 source comparison at `0dbd1ea8bb2211c158f6af928b5e4fb14dfa0dac` gives narrower, verifiable facts:

- The [model loader](https://github.com/shinyquagsire23/OpenJKDF2/blob/0dbd1ea8bb2211c158f6af928b5e4fb14dfa0dac/src/Primitives/rdModel3.c) reads insertion offsets, vertex intensities and face extra light. Their uses depend on the rendering and object path; parsing a field alone does not establish its visual effect.
- One [thing-creation path](https://github.com/shinyquagsire23/OpenJKDF2/blob/0dbd1ea8bb2211c158f6af928b5e4fb14dfa0dac/src/World/sithThing.c) computes and rotates the template/marker insertion-offset difference before finding a creation position. That directly prevents a blanket recommendation to discard the field.
- The [puppet implementation](https://github.com/shinyquagsire23/OpenJKDF2/blob/0dbd1ea8bb2211c158f6af928b5e4fb14dfa0dac/src/Engine/rdPuppet.c) uses the keyframe/node type bit intersection to choose high versus low priority. A zero node type is not categorically rejected by that expression. Preserve intended blending behavior instead of imposing a type mask from an old example.
- The [active colormap reader](https://github.com/shinyquagsire23/OpenJKDF2/blob/0dbd1ea8bb2211c158f6af928b5e4fb14dfa0dac/src/Engine/rdColormap.c) reads the palette's lighting table and, when flagged, its transparency table and additional RGB16 maps. Preserve the whole CMP; an RGB palette screenshot is insufficient to reconstruct its lighting behavior.

These are source-derived observations about that pin, not fresh original-retail tests. In particular, the commented legacy branch in `rdColormap.c` must not be cited as the active implementation. Keep exact renderer and object-creation paths attached to conclusions.

## Treat old editor round trips as versioned transformations

The actual [JOKE 0.77 ZIP](https://jkdf2.net/JKBot/JKArchive/JKDF2/JKDF2/Game%20Files/tools/3DO/joke077.zip) includes a dated history. Version 0.6.2 records fixes for faces being assigned the first material on save and duplicate material insertion. Version 0.7.5 fixes stale materials carrying across model loads and removes unused materials on load. Versions 0.7.6–0.7.7 add and repair 16-bit material handling. These records explain why a saved file can change independently of an artist's geometry edits.

For legacy repair, save to a new candidate, compare material names and per-face assignments, verify all geosets and hierarchy fields, then test the target engine. Do not describe every JOKE version as broken or treat a changelog entry as an independently reproduced fix. The [native-example ledger](https://github.com/CodaLilitari/JK-MotS-Model-Workflow/blob/ca011b84c4fa5e1f3755e1543be2c1c1d25063c9/skills/jk-mots-model-workflow/references/evidence/downloaded-native-examples.json) and [tools ledger](https://github.com/CodaLilitari/JK-MotS-Model-Workflow/blob/ca011b84c4fa5e1f3755e1543be2c1c1d25063c9/skills/jk-mots-model-workflow/references/evidence/downloaded-tools-geometry.json) preserve inspected file identities.

## Coverage boundaries that matter

The broad scan records **76 errors or exclusions**, not 76 unusable packages. These include ancillary EXEs, unexpanded installers and RAR/7Z containers, unsupported ZIP compression, damaged members, one encrypted document and a deeply nested GOB. Some `.gob`/`.goo`-named members are actually text rather than containers. Such records must be classified before interpreting them as missing model data. Previously recovered installer evidence, including TDIR, remains in its own identified research lane.

The scanner reads declarations and binary headers; the detailed material review validates selected payload layouts separately. Repeated member names can occur even inside a GOB, so selected-file checks use hashes as well as names. A successful parse, archive integrity result or historical author's retail statement is not runtime proof. No historical program or game was executed for this pass, and no new retail or connected-multiplayer compatibility is asserted.

Use this collection as a searchable source of concrete alternatives, exact failure cases and attributable examples. The research makes the next modeling decision better; it does not require reproducing every old experiment before a simple reskin.
