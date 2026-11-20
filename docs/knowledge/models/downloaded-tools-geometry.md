# Retained modeling tools and native examples

> Copied into RED from [CodaLilitari/JK-MotS-Model-Workflow@ca011b8](https://github.com/CodaLilitari/JK-MotS-Model-Workflow/blob/ca011b84c4fa5e1f3755e1543be2c1c1d25063c9/skills/jk-mots-model-workflow/references/downloaded-tools-geometry.md) (MIT; see [LICENSE](LICENSE.md)). Regenerate with `tools/knowledge/import_knowledge.py` rather than editing this copy.

Prepared 30 September 2026. This report examines six already downloaded ZIPs, their member inventories, two native 3DO examples, one nested KEY example, a pinned Blender importer, and two earlier original regression receipts. ZIP hashes were independently checked against acquisition records. No historical executable, installer, Blender add-on or game was run during this review. The separate `evidence/downloaded-tools-geometry.json` ledger carries identifiers, versions, source links, hashes and bounded findings without distributing the original tool or game-derived asset bytes.

## Use the native weapon template as a contract

SavageX's [Creating New Weapons](https://www.massassi.net/tutorials/savguns/) has a retained [example archive](https://www.massassi.net/tutorials/savguns/savguns.zip), not just tutorial prose. Its `temp.3do` header identifies a JED 0.91 beta export in 3DO 2.1 format. Direct inspection establishes one geoset, four meshes and five hierarchy nodes. Mesh 3, `K_WEAPON`, deliberately has zero vertices and faces; its hierarchy node is attached below the right hand. The forearm, hand and shoulder retain their separate meshes and pivots.

The material table has **16 entries**: slots 0–8 are `dflt.MAT` placeholders and slots 9–15 are hand/arm materials. This makes the tutorial's nine-texture instruction a constraint of this prepared template, not a universal weapon or engine limit. Replace the intended weapon mesh, remap its face material references, and preserve the hand hierarchy before checking animation. Do not infer that a modern exporter will retain empty meshes automatically; compare the emitted hierarchy and mesh references.

The native file also demonstrates why mesh order and hierarchy order must not be conflated. The shoulder is mesh 1 but hierarchy node 3; the hand is mesh 0 but hierarchy node 2. A converter that identifies parts by list position rather than the hierarchy's mesh index can silently attach the wrong geometry. The hand mesh contains 85 vertices and 79 faces; these are measurements of the example, not budgets. The bundled Word document was not decoded, and no permission to redistribute the stock-derived hand geometry is inferred.

## Puppet Jedi versions and authoring behavior need separate evidence

The retained [Puppet Jedi 0.61 ZIP](https://www.massassi.net/programs/3dokey/pjedi_061.zip) contains only `pjedi.exe` and `pjedi_help.hlp`. Both were inventoried and hashed. The WinHelp content was not decoded; its presence does not establish that its instructions were read or that the program works on a current system.

[PJB's Keys tutorial](https://www.massassi.net/tutorials/keys/) describes **Puppet Jedi 0.7**, credited to Code Alliance. It teaches animating the shoulder, forearm and hand separately, then adding hand lag and subtle torso movement for a less mechanical gesture. PJB reports that typing positions into an entry can leave intermediate frames incompletely filled, and chooses the mouse editing route instead. Treat that as a dated, version-specific author report and inspect the resulting KEY entries; it is not proof of a current Blender or engine interpolation bug.

The tutorial and binary have different versions. The apparent successful `pjedi_07.zip` acquisition in another manifest is only a 114-byte HTML response. A 200 status or a ZIP-shaped URL therefore does not establish a usable 0.7 download. KeyEd's forum discussion supplies important retargeting methods, but no usable KeyEd executable was found in this selected tool pass. Fun3do likewise remains historical method evidence here.

## TrajJedi provides a concrete interpolation example, within a narrower role

[TrajJedi 1.1](https://www.massassi.net/programs/3dokey/trajjedi1_1.zip), by **Jereth Kok**, includes `Demo_KEY.zip` and an explanatory text. The nested archive contains a native `tkey.key` and `keyplay.cog`. The KEY's creation comment is 22 February 1998; its measured header is 171 frames, 8 FPS, one joint and one animated node, with 171 entry rows. The selected track is `tie~bmb`, so this is a single-object trajectory example rather than humanoid retargeting evidence.

Jereth says he used Transform Object and Auto Orientate, and explicitly warns of a sudden flip during vertical flight. The file also contains rotations crossing the 360/0 boundary. These make good cases for an original interpolation test: examine wraparound, near-vertical orientation and the transition between the final and first entry. Their presence does not identify the flip's mathematical cause or prove that all exporters handle such motion correctly.

The companion COG delays four seconds and calls `PlayKey` with flags zero; its comments describe looping and two alternate completion behaviors. Those comments are source testimony, not newly verified engine semantics. The level's layout and startup inventory setup are outside this workflow. For a character model, use the fixture's testing ideas while supplying the actual character hierarchy, semantic PUP roles and gameplay events.

## An independent viewer is a comparison tool, not an engine certificate

**Zymotico's 3do View**, public release 2 dated 30 July 2003, has a retained [ZIP](https://www.massassi.net/programs/3dokey/zy3doview2.zip) containing its executable, manual and `n1.3do`/`n1-skin.mat` example. The manual documents wire/flat/smooth display, texture and OpenGL-lighting toggles, environment mapping, screenshots and a generated model-statistics file. It also warns that a transparent first mesh can affect later rendering.

The example has 3DO version 2.1, one material, one geoset, two meshes and three hierarchy nodes. Its mesh names and hierarchy labels are reversed relative to each other: hierarchy node `LID` references mesh 0 named `ZYN1`, while node `ZYN1` references mesh 1 named `LID`. This is a concrete reason to follow numeric references rather than matching labels. No conclusion about intended appearance follows without a render.

Use an independent viewer to help isolate an exporter problem from a Blender display problem. Keep its OpenGL lighting, environment map, transparency behavior and material fallback separate from retail JK/MotS behavior. We did not launch this viewer or verify its command-line interface.

## Historical conversion and skin tools offer different levels of automation

The retained [3DO Converter](https://www.massassi.net/programs/3dokey/3doconverter.zip) contains a single executable. The [Massassi program catalog](https://www.massassi.net/programs/) credits **Frank Krueger** and describes 3D Studio ASCII `.ASC` to 3DO conversion. No manual, source, native example, callable interface or conversion result was found in that ZIP. It is an interoperability lead, not a justified headless pipeline recommendation.

[TSM 1.0.8](https://www.massassi.net/programs/3dokey/tsm.zip), credited in the catalog to **Curtis Brazzell**, contains an installer and a readme identifying version `01.00.08`. The readme describes editing body-part bitmaps through the operating system's BMP association and selecting the resulting character in JK. Its multiplayer warning makes a useful practical point: other clients need the corresponding resources. Its suggestion to send an entire Resource directory should be replaced in a modern workflow by a minimal, explicitly licensed mod package.

TSM's readme assumes a particular JK installation path and historical Windows components. It presents Patch Commander integration as a later-version goal even while mentioning Patch Commander for manual packaging. Do not turn that into verified automatic packaging, MotS support or modern Windows compatibility. The installer was neither extracted beyond the ZIP container nor executed.

## Pin importer behavior before adopting it for agent work

The locally retained **Ecki81 Jedi Knight to Blender** importer is pinned to revision `88aed6f7182dad72d5042d2f70d5eef0b7ed6b78`. Its [3DO importer source](https://github.com/Ecki81/jediknight2blender/blob/88aed6f7182dad72d5042d2f70d5eef0b7ed6b78/import_3do.py) supplies a useful alternative inspection route, but its actual code constrains the recommendation. The texture-loading path opens the configured JK `Res2.gob` and `01narsh.cmp`; the module reads the first mesh-count group rather than iterating geosets, and does not import the native normal arrays. If an image cannot be found, UV normalization falls back to 64×64.

Those details matter for a custom character: a plausible import can still omit alternate geosets, use the wrong palette or texture size, and rebuild shading. Inspect the importer and export requirements before using its Blender view as evidence of a lossless asset round trip. These are static findings at that revision, not an execution test or a claim about a newer release.

## Preserve format fields and test sampling boundaries

Two original SED format-library fixtures retained from 25 September 2026 extend the general verification checklist. The saved insertion-offset result records `(1.25, -2.5, 0.75)` being read, then written as zero, while an independent hierarchy-pivot control survives. That distinguishes insertion offset from pivot preservation. The earlier audit also found in source that the supported GUI export path constructs new zero-offset models; it did not establish ordinary GUI corruption of imported models.

The saved KEY fixture declares 21 frames but starts its single entry at frame 10. The earlier packaged-library execution accepted it and threw `ArgumentOutOfRangeException` when sampled at frame 0, while frame 10 returned the expected pose. That demonstrates a robustness boundary in the recorded package, not that the input is valid retail content or that extrapolation is the correct fix. Both receipts identify package revision `ea4d6f4a8c39501198064a52264317b16e5741e0` and were reviewed without rerunning them. Add independent field-preservation and before-first-entry cases when extending a model exporter or viewer.

The research folder named `pup` is explicitly **Indiana Jones and The Infernal Machine documentation**, not Puppet Jedi source. Its filename conventions and PUP claims were excluded from JK/MotS recommendations. Similar resource names across Sith-engine games do not establish identical formats or semantics.

## Coverage and credit

This pass opened six retained ZIPs and one nested ZIP, checked their member hashes, profiled two native 3DO examples and one native KEY, read one complete 3DO importer module plus its README, read PJB's complete tutorial and the relevant program-catalog section, and reviewed two earlier original regression receipts. It is a selected tools pass; the broader download inventory is recorded separately. The conclusions above credit SavageX, Code Alliance, PJB, Jereth Kok, Zymotico, Frank Krueger, Curtis Brazzell and Ecki81 for the specific methods or tools examined. No binary runtime, source-image inspection, new acquisition or full-archive research completion is implied.
