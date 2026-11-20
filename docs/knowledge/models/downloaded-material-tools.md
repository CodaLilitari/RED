# Downloaded tools and material bytes: additional surface findings

> Copied into RED from [CodaLilitari/JK-MotS-Model-Workflow@ca011b8](https://github.com/CodaLilitari/JK-MotS-Model-Workflow/blob/ca011b84c4fa5e1f3755e1543be2c1c1d25063c9/skills/jk-mots-model-workflow/references/downloaded-material-tools.md) (MIT; see [LICENSE](LICENSE.md)). Regenerate with `tools/knowledge/import_knowledge.py` rather than editing this copy.

This chapter examines actual downloaded bytes: two converter ZIPs (including a passively extracted CHM), two pinned source/documentation sets, and three skin GOBs inside their ZIPs. No legacy executable, CHM viewer or game was run. This offline pass made no new external requests and did not republish the original assets. All original capture/archive hashes used here matched their retained receipts.

The main practical addition is a clearer texture choice: indexed palette-aware reskinning, RGB565 opaque color, and ARGB1555 binary cutout are distinct routes. Fractional alpha in a modern replacement-image path remains renderer-specific. Versioned converters and rich MAT cell/mip structures need their own round-trip checks.

## asset-surface-01: Mat16 1.1 actual archive and included history

**Credit:** BoBo Fett; changelog credits Craig Urquhart for the ARGB1555 discovery. **Date:** 2004-12-04 (included Mat16.txt version date). [Source](https://discord.com/channels/881034839401328680/881044857328652328/881047589217247232).

Version 0.5b removed broken 16-bit transparency support in 2001; version 0.7 restored transparent 16-bit support in April 2002 using ARGB1555, crediting Craig Urquhart. Version 1.0 records an RGB565 bit-shifting fix. These entries explain why early converter advice can disagree with later behavior.

Version 0.9 adds multi-cell materials with mipmaps and dimension checks; 1.1 adds 16-bit dithering and alpha-loading tools. The included spec identifies RGB565 and ARGB1555 separately. The documentation is not a runtime compatibility test.

**Addition and limits:** Adds ARGB1555 as an explicitly named candidate for binary cutout transparency, distinct from opaque RGB565, face translucency and modern fractional-alpha replacement images. Select by actual target tests.

**Rights:** Included notice allows only original unmodified free distribution with notice sent to author. No redistribution or author contact performed. Preserve local archive; do not bundle it by default.

## asset-surface-02: JKPaint 0.8 beta actual ZIP and passively extracted CHM manual

**Credit:** Craig Urquhart (UGG), Code Alliance; credits BoBo_Fett and other contributors. **Date:** 2003 copyright; exact release date not established. [Source](https://jkdf2.net/JKBot/JKArchive/JKDF2/JKDF2/Game%20Files/tools/OTHER/JKPaint08.zip).

MAT help distinguishes cells/frames from their mip chains. Select indexed palette and properties before adding frames; Generate Mipmaps controls automatic construction. Shift commands reorder frames, while mipmaps cannot be independently deleted or shifted in that UI.

Extract Bitmaps can emit current or all frames as 8- or 24-bit BMPs. Filename numbers start at 1 for frames and letters at A for mip levels. Preserve this ordering during round trips instead of using arbitrary filename sort.

History says 0.2 added transparent 16-bit MAT support and mip-size checks; 0.2.1 fixed the floating-point error; 0.8 fixed transparency-color selection. CMP help distinguishes RGB entries from light and blend lookup tables.

**Addition and limits:** Adds a specific cell/mip inspection/extraction option and file-naming contract. Manual-wide statements about 1024 dimensions, four mips, sixteen frames or black transparency are historical program documentation, not newly certified universal engine limits.

**Rights:** Manual copyright retained by Craig Urquhart; assets/manual remain private. No general documentation redistribution grant inferred.

## asset-surface-03: MAT16 V1.04 selected implementation and documentation

**Credit:** JKBoBoFett / BoBo Fett. **Date:** 2022-12-21 release; source tag pin 3b53c7c20d2e5feb761ea6895bd2a1cb92eee6b7. [Source](https://github.com/JKBoBoFett/MAT16/tree/3b53c7c20d2e5feb761ea6895bd2a1cb92eee6b7).

The tag contains actual implementation, while the separately pinned default main documentation tree does not. Selected source proves GUI recipe conversion, not an unattended CLI: MAIN.PAS opens a MAT16S file dialog, shows a progress form, calls TBMPArray.OpenMTS, and saves via the recipe name; Mat16.DPR constructs forms then enters Application.Run.

SetLightLevel calls the CMP light remapping function and refreshes indexed display state. Use such views to diagnose indexed skin appearance, without equating preview to baked pixels or runtime lighting.

The default README marks internal-CMP MATs, RGBA4444, RGBA5551, RGB24 and RGBA32 as custom-engine-only. Script-created variants do not establish retail support.

**Addition and limits:** Corrects both possible overstatements: MAT16 is not documentation-only, and a script recipe feature is not automatically a headless agent API. The actual parser/helper implementation was not acquired here, so exact recipe grammar/fidelity remains unresolved.

**Rights:** Observed repository metadata lacks a general license and selected files contain no general reuse grant. No third-party source redistribution or fork proposed from these limited files.

## asset-surface-04: bahstrike/Matt source at 5df4ed4bc72396b796b9d377ee4c2a9bcd355e35

**Credit:** bahstrike / BAH_Strike; README credits JKSpecs, BoBo Fett, smlu and shiny. **Date:** 2024-04-01T00:39:19Z commit author timestamp. [Source](https://github.com/bahstrike/Matt/tree/5df4ed4bc72396b796b9d377ee4c2a9bcd355e35).

Keep Current Colormap preserves interpretation of the source while a different output palette is selected. Optional Floyd–Steinberg dithering and exclusion of self-illuminated colors offer distinct indexed-skin conversion choices. The source marks equal endpoint light-table indices as self-illuminated (except RGB black), which is a heuristic, not an audit of every intermediate brightness table.

The displayed fuchsia option is preview-only. The indexed conversion path treats input alpha exactly zero as transparent and assumes palette index zero for its output; partly transparent input therefore needs an explicit, tested cutout policy.

Material preview selects MipmapData[0]; the image-to-MAT path creates one material record, one texture and one mip. A rich multi-cell/mip MAT is not preserved by simply converting its preview back to MAT.

Static defect: Matt.cs lines 650–662 have an independent if(Bpp==16) write followed by if(Bpp==24)...else(uint write). For 16-bit output this performs a 32-bit store while advancing two bytes; the final pixel writes two bytes beyond the allocated array. No binary was run. The write need not visibly crash, and overlapping writes can restore most in-array pixel bytes; no observed corrupted image or crash is claimed. Treat this pinned writer as repair/reference work, not a ready converter recommendation.

**Addition and limits:** Adds palette remapping and diagnostic options with implementation-level limits that a features list misses. README excludes its richer 24/32-bit paths from baseline retail. Source-specific defects must not be generalized to all tools or later commits.

**Rights:** Included LICENSE is GPL version 3; keep attribution and applicable license obligations if code is later reused. This research does not distribute code.

## asset-surface-05: Cyborg

**Credit:** KnightSpider; credits Al Macdonald’s Dark Trooper MATs. **Date:** 2000-01-14 (bundled readme). [Source](https://www.massassi.net/levels/files/540.shtml).

A texture-only player reskin is a legitimate method: this GOB contains only nineteen indexed MATs under stock-looking ky*.mat paths, and its own readme says it replaces Kyle without a new 3DO. Absence of a 3DO is consistent with its claimed method, unlike an unresolved custom-name mapping. No matching retail geometry was loaded in this inspection.

Some material references in the supplied Matrix/Destroyer models are outside their package; these remain external or unresolved until checked against the target installation. Cyborg intentionally has no model according to its readme.

These bytes also include palette-index-zero pixels with the transparency flag disabled; do not infer rendered holes from index zero alone. No visual result is claimed.

No CMP is supplied in this package. Preserve the intended external game palette when interpreting indexed pixels; do not treat a guessed color preview as recovered appearance.

**Addition and limits:** Adds actual package measurements and per-member hashes to the method choice. One-mip/non-square usage is an observed artifact profile, not universal quality or compatibility guidance.

**Rights:** Readme permissions and donor rights retained; some templates contain contradictory may/may-not language. Original assets and full readmes remain private, with no redistribution permission inferred.

## asset-surface-06: Matrix Skin Pack

**Credit:** Elyas Machera and Matrix__Apoc (both in the readme); skinning credited to Elyas Machera. **Date:** 2000-01-01 (bundled readme). [Source](https://www.massassi.net/levels/files/545.shtml).

Nine supplied 3DOs and sixty-one indexed MATs illustrate a separate selectable-skin package. Twenty MAT texture records have the transparency flag enabled, forty-one disabled. All use one texture and one stored mip. These bytes demonstrate mixed transparency metadata within one skin pack, not actual rendered holes or verified multiplayer compatibility.

Some material references in the supplied Matrix/Destroyer models are outside their package; these remain external or unresolved until checked against the target installation. Cyborg intentionally has no model according to its readme.

These bytes also include palette-index-zero pixels with the transparency flag disabled; do not infer rendered holes from index zero alone. No visual result is claimed.

No CMP is supplied in this package. Preserve the intended external game palette when interpreting indexed pixels; do not treat a guessed color preview as recovered appearance.

**Addition and limits:** Adds actual package measurements and per-member hashes to the method choice. One-mip/non-square usage is an observed artifact profile, not universal quality or compatibility guidance.

**Rights:** Readme permissions and donor rights retained; some templates contain contradictory may/may-not language. Original assets and full readmes remain private, with no redistribution permission inferred.

## asset-surface-07: Destroyer Droid Mod

**Credit:** J. Zimmerman; model, MATs and project credited to Zymotico. **Date:** 2000-12-10 (bundled readme). [Source](https://www.massassi.net/levels/files/1289.shtml).

One supplied model and thirty indexed MATs include non-square 128×256 and 64×256 textures, in addition to smaller sizes. This is a direct counterexample to treating square atlases as a required storage layout. The included custom keys/PUP/COG are outside this texture-only subinspection; the readme acknowledges unfinished sound.

Some material references in the supplied Matrix/Destroyer models are outside their package; these remain external or unresolved until checked against the target installation. Cyborg intentionally has no model according to its readme.

These bytes also include palette-index-zero pixels with the transparency flag disabled; do not infer rendered holes from index zero alone. No visual result is claimed.

No CMP is supplied in this package. Preserve the intended external game palette when interpreting indexed pixels; do not treat a guessed color preview as recovered appearance.

**Addition and limits:** Adds actual package measurements and per-member hashes to the method choice. One-mip/non-square usage is an observed artifact profile, not universal quality or compatibility guidance.

**Rights:** Readme permissions and donor rights retained; some templates contain contradictory may/may-not language. Original assets and full readmes remain private, with no redistribution permission inferred.

## Reusable checks and options

- Preserve source palette interpretation separately from destination palette remapping. Compare dithering and constrained palette colors at final game size.
- For cutout hair, holes or visors, choose and verify the actual MAT dialect and transparency flags; RGB black alone is not a format-independent transparency contract.
- A preview-export can lose cells or stored mips. Count cells, levels, dimensions and filenames before and after conversion; keep originals.
- A texture-only replacement can be intentional when its names match the model being replaced. Custom material names still require matching model references.
- Use non-square textures or multiple small materials when they fit the model; square atlases and a particular final resolution are design choices subject to target checks.
- Keep tool history attached to findings. Old failures, later fixes, package bytes and fresh runtime validation establish different things.

The [public evidence ledger](https://github.com/CodaLilitari/JK-MotS-Model-Workflow/blob/ca011b84c4fa5e1f3755e1543be2c1c1d25063c9/skills/jk-mots-model-workflow/references/evidence/downloaded-material-tools.json) contains archive and source hashes, measured profiles and representative material-member hashes. Full source receipts, complete readmes and all 110 per-material records remain in the private audit corpus. No actual image colors were reconstructed because these packages do not supply their external CMP. No fresh high-to-low bake or converter CLI was established.
