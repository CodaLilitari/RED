# Additional surface research from the downloaded corpus

> Copied into RED from [CodaLilitari/JK-MotS-Model-Workflow@ca011b8](https://github.com/CodaLilitari/JK-MotS-Model-Workflow/blob/ca011b84c4fa5e1f3755e1543be2c1c1d25063c9/skills/jk-mots-model-workflow/references/additional-surface-methods.md) (MIT; see [LICENSE](LICENSE.md)). Regenerate with `tools/knowledge/import_knowledge.py` rather than editing this copy.

This chapter adds attributed methods and corrections to the 1.2 research. Selected retained captures were checked by SHA-256. This is source research, not a fresh game or converter test; the download contains this original synthesis and its evidence ledger, while original message bodies and third-party assets are not republished. The selected methods cover JK/MotS models; scenery-origin examples are explicitly limited to transferable diagnosis.

The key addition is practical choice: preserve and repaint a donor skin, use separated value/color painting, repair a repeating material, keep legacy UV tools in a controlled round trip, or diagnose export and material state before remodeling. Existing 1.2 advice about final UVs, palette checks and renderer-specific testing remains in force.

## surface-01: The Tynar — Mastering the MATs

Separate value painting from color painting: preserve the source palette, edit a 24-bit grayscale version, paint color on a duplicate, Multiply the two, then quantize back to the intended palette.

**Addition and limits:** Adds an attributed artist recipe, including a face example, to the existing high-level hand-painting option. Authored historical tutorial; images not independently inspected here. The author acknowledges that the standalone example looked less good with the complete skin. Preserve model-space inspection and do not adopt the tutorial’s demo-save workaround or claim that more expensive paint software guarantees quality.

[Original source](https://www.massassi.net/articles/df/tutorial.htm). Date: not established in the captured page. Locator: MAT Editing Tutorial. Corpus record 32283; capture and text hashes are in `evidence/additional-surface-methods.json`.

## surface-02: CosmoDNA — Skins - The Massassi Temple

Keep the new face nose and brow aligned with the donor geometry; blend through palette color ramps. The tutorial then raises color depth, applies a small Gaussian blur, and reloads the target palette, warning that contrasting adjacent colors can quantize into muddy intermediate pixels.

**Addition and limits:** Previously cited for packaging and palettes, but its actual painting technique and failure mode were not synthesized in 1.2. The .50–.65 blur radius belongs to that historical image/editor example. Mirroring of its front-face texture is model-specific, not a general rule for all 3DOs. Palette-index transparency and renderer behavior require separate checks; do not universalize its black-pixel shorthand.

[Original source](https://www.massassi.net/tutorials/skins/). Date: 1998-07-23 (catalog date from existing source ledger; tutorial body undated). Locator: The Art / Blur. Corpus record 32506; capture and text hashes are in `evidence/additional-surface-methods.json`.

## surface-03: Rosebud — Texture FAQ - The Massassi Temple

For a repeating source, offset each layer by half its dimensions and repair the exposed seam with cloning or drawing. Keep highlights/shadows on separate layers. Prefer controlled brush edges over repeatedly blurring then sharpening.

**Addition and limits:** Adds a concrete seam-repair recipe and reversible painted-light organization. This is general texture authoring, transferable only to deliberately repeating model materials; it is not UV-island seam repair or a model bake. Painted directional light remains an optional style choice. The footer says 2001–2002 but does not establish a precise publication date. Photographic/donor overlays still require rights.

[Original source](https://www.massassi.net/tutorials/texturefaq/). Date: not established in the captured page. Locator: Texture FAQ. Corpus record 32537; capture and text hashes are in `evidence/additional-surface-methods.json`.

## surface-04: molotov42 — New ForceRemap version 1.1 - Jedi Knight and Mysteries of the Sith Editing Forum - The Massassi Temple Forums Archive

ForceRemap 1.1 announces a fix for corruption of K_RHAND pivot values and says the hierarchy section is no longer affected. It explicitly leaves the scaled-texture-vertex selection bug unresolved.

**Addition and limits:** Adds exact historical tool/version and a reason to compare hierarchy before/after UV-only work. This is the tool author’s changelog, not a fresh executable test or assurance about every version.

[Original source](https://forums.massassi.net/Editing_Forums/Jedi_Knight_and_Mysteries_of_the_Sith_Editing_Forum/thread_11367_page_1.html). Date: 2003-06-17. Locator: post #1; ForceRemap 1.1 HISTORY. Corpus record 26491; capture and text hashes are in `evidence/additional-surface-methods.json`.

## surface-05: UGG — JKPaint Beta Testing - Jedi Knight and Mysteries of the Sith Editing Forum - The Massassi Temple Forums Archive

JKPaint distinguishes adding an animation frame/cell from adding lower-resolution mip levels. With Generate mipmaps checked, adding a frame builds its mips; otherwise use Add mipmap. UGG explains the expected halving sequence, with 128×64 → 64×32 → 32×16 → 16×8 as an example.

**Addition and limits:** Adds an actual converter procedure and the author’s correction to ambiguous specs wording. Preserve the distinction between a base image, animation cells and mip levels. Beta crash reports in the same thread prevent treating this as universal converter reliability; do not promote its claimed 16-cell limit to an engine theorem. The continuation page adds no correction to the selected procedure.

[Original source](https://forums.massassi.net/Editing_Forums/Jedi_Knight_and_Mysteries_of_the_Sith_Editing_Forum/thread_11195_page_1.html). Date: 2002-02-26. Locator: posts #12, #14 and #16; full page and continuation read. Corpus record 26315; capture and text hashes are in `evidence/additional-surface-methods.json`.

## surface-06: DogSRoOL; Friend14 — 16-bit MipMaps - Jedi Knight and Mysteries of the Sith Editing Forum - The Massassi Temple Forums Archive

DogSRoOL initially reported disabled 16-bit mip controls in JKPaint 0.2.1 beta and later guessed that indexing the input fixed it. The final self-correction says the auto-generate mipmaps checkbox had been enabled without their noticing.

**Addition and limits:** Adds a resolved UI-state confound: inspect automatic generation before inferring that 16-bit MATs cannot carry mipmaps or unnecessarily reducing source color depth. Friend14 also reports successful 24-bit BMP to 16-bit custom-mip use. No converter execution performed in this review.

[Original source](https://forums.massassi.net/Editing_Forums/Jedi_Knight_and_Mysteries_of_the_Sith_Editing_Forum/thread_11344_page_1.html). Date: 2003-06-03 through 2003-06-07. Locator: posts #1, #9, #14–16. Corpus record 26468; capture and text hashes are in `evidence/additional-surface-methods.json`.

## surface-07: Ermine13; MikeC; dbclick — Can you make mats transparent on 3dos? - Jedi Knight and Mysteries of the Sith Editing Forum - The Massassi Temple Forums Archive

Ermine13 describes JOKE’s selected-face transparency checkbox for walkplayer ghosts; MikeC corrects the term to translucency. dbclick reports the original problem resolved but does not identify exactly which proposed method solved it.

**Addition and limits:** Adds a historical, model-specific distinction between face translucency and texture cutout. Do not label the final thanks as confirmation of every recipe in the thread, or infer a MAT alpha channel from a UI checkbox.

[Original source](https://forums.massassi.net/Editing_Forums/Jedi_Knight_and_Mysteries_of_the_Sith_Editing_Forum/thread_7709_page_1.html). Date: 2001-04-17 through 2001-04-18. Locator: posts #5–7, with #1–4 context. Corpus record 29979; capture and text hashes are in `evidence/additional-surface-methods.json`.

## surface-08: money•bie; Emon; LKOH_SniperWolf; ProdigyOddigy — Problems with transparency - Jedi Knight and Mysteries of the Sith Editing Forum - The Massassi Temple Forums Archive

A transparent 3DO in front of another 3DO produced different results from combining the geometry. Emon reports that Blocks Light did not fix translucency; LKOH_SniperWolf cites JED’s CalcLighting functions as its editor-only use. ProdigyOddigy reports an asymmetric architecture-flag experiment and remaining stray triangles.

**Addition and limits:** Adds a counterexample to a common flag-fix recipe. The original example is scenery, so transfer only the diagnosis of layered 3DO transparency relevant to hair, visors, overlapping cloth or effects. Do not turn architecture flags into a player-model recommendation, a numeric layer limit, or the disputed 80% success claim. JED source was not independently checked in this pass.

[Original source](https://forums.massassi.net/Editing_Forums/Jedi_Knight_and_Mysteries_of_the_Sith_Editing_Forum/thread_35097_page_1.html). Date: 2005-08-18 through 2005-08-19. Locator: posts #1, #6, #9 and #12; whole thread read. Corpus record 27570; capture and text hashes are in `evidence/additional-surface-methods.json`.

## surface-09: Shred — editing / Shred / 2022-03-23T00:13:25.495Z

For MilkShape, Shred advises extracting MATs as BMP previews and applying those to material slots when a textured preview is needed; for an already-mapped model he suggests first checking the material slots rather than rebuilding them. After export, compare scale in JED beside ky.3do.

**Addition and limits:** Adds a preservation-oriented legacy round-trip route and an explicit reference-model scale check. Material-slot count alone is only Shred’s tentative triage, not proof of names, face assignments or renderer correctness. MilkShape/add-on version was not stated.

[Original source](https://discord.com/channels/881034839401328680/881044857328652328/955982567943839794). Date: 2022-03-23T00:13:25.495Z. Locator: message 955982567943839794 (UI: March 22, 8:13 PM). Corpus record 64515; capture and text hashes are in `evidence/additional-surface-methods.json`.

## surface-10: SMLiberator — editing / SMLiberator / 2024-08-09T19:22:41.231Z

SMLiberator reports combining JOKE and ForceRemap for UV mapping with Blender for merging or cleaving faces while preserving UVs.

**Addition and limits:** Adds a practitioner’s multi-tool choice rather than forcing every operation into one editor. This is a personal workflow report; no particular versions or universal round-trip guarantee are established.

[Original source](https://discord.com/channels/881034839401328680/881044857328652328/1271549232335028286). Date: 2024-08-09T19:22:41.231Z. Locator: message 1271549232335028286. Corpus record 54495; capture and text hashes are in `evidence/additional-surface-methods.json`.

## surface-11: Shred — editing / Shred / 2026-01-16T22:02:23.270Z

Shred reports a 3DO that still failed after ZED re-export; exporting as OBJ and reimporting into Blender preceded a working result. He speculates that normals were involved.

**Addition and limits:** Adds a last-resort isolation experiment, not an identified root cause. If trying it, use a duplicate and re-establish hierarchy, pivots, face flags and material identity afterward because OBJ does not retain the complete JK model contract. The preservation warning is our recommendation; the source supplies only the anecdote.

[Original source](https://discord.com/channels/881034839401328680/881044857328652328/1461843051759800460). Date: 2026-01-16T22:02:23.270Z. Locator: message 1461843051759800460. Corpus record 43160; capture and text hashes are in `evidence/additional-surface-methods.json`.

## surface-12: Stormtrooper — editing / Stormtrooper / 2026-01-31T04:35:24.305Z

Stormtrooper reports flipped faces inside the hand and fingertips; after fixing them and re-exporting from Blender, the model loaded in game.

**Addition and limits:** Adds a bounded successful author report of a geometric orientation repair. Exporter version, complete diff and exact engine are not supplied. Do not infer that all invisible/default-cube models are normal problems, or confuse face orientation with split shading normals.

[Original source](https://discord.com/channels/881034839401328680/881044857328652328/1467015387907624961). Date: 2026-01-31T04:35:24.305Z. Locator: message 1467015387907624961 (UI: January 30, 11:35 PM). Corpus record 42841; capture and text hashes are in `evidence/additional-surface-methods.json`.

## surface-13: Urgon — editing / Urgon / 2025-04-27T11:37:49.997Z

Urgon explains that the then-unfinished sector-to-3DO exporter duplicated vertices to retain differing per-polygon-corner colors because 3DO stores vertex color differently.

**Addition and limits:** Adds the concrete reason duplicate geometry might be deliberate. The quoted Shred report says that particular SED output failed as a default cube while ZED output worked; that report remains attributed to Shred, not Urgon. Transfer to models only as a warning to compare color/normal/UV discontinuities before welding, not as a level-design recipe or a tested exporter fix.

[Original source](https://discord.com/channels/881034839401328680/881044857328652328/1366015480942563328). Date: 2025-04-27T11:37:49.997Z. Locator: message 1366015480942563328. Corpus record 49883; capture and text hashes are in `evidence/additional-surface-methods.json`.

## surface-14: EvanC — Multi-Celled MATs - The Massassi Temple

EvanC demonstrates a MatMaster MTM input listing two same-size/same-palette BMPs under TEXTURES: 2, compiled with the selected color map. The count lists animation cells, not a mip chain.

**Addition and limits:** Adds a concrete scriptable cell-construction route. The demonstration is a switch; only material construction transfers to model materials. It does not show how to drive a cell change on a player model. No historical executable run or universal cell limit is claimed.

[Original source](https://www.massassi.net/tutorials/mats_multicell/). Date: not established in the captured page. Locator: Multi-Celled MATs; MTM example. Corpus record 32442; capture and text hashes are in `evidence/additional-surface-methods.json`.

## Coverage and exclusions

Fourteen selected source records; one existing CosmoDNA source contributes new technique detail. Selected forum pages were read in full, including the JKPaint continuation; Discord attribution was checked against the own message body and UTC snowflake. The public evidence ledger records exact FTS terms, counts and rank limits. Counts are index-document matches, not unique threads or completeness claims. This pass does not establish a new geometry bake recipe, universal texture/mesh limit or renderer behavior. JO/JA character/UV tutorials, level-wide lighting/fog recipes, uncertain reports and duplicate sources were excluded or retained only as context. The accompanying [downloaded-tool and material analysis](downloaded-material-tools.md) covers direct archive and source inspection.
