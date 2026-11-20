# TDIR, JKE, and Shred: reusable modeling methods

> Copied into RED from [CodaLilitari/JK-MotS-Model-Workflow@ca011b8](https://github.com/CodaLilitari/JK-MotS-Model-Workflow/blob/ca011b84c4fa5e1f3755e1543be2c1c1d25063c9/skills/jk-mots-model-workflow/references/jke-tdir-shred-research.md) (MIT; see [LICENSE](LICENSE.md)). Regenerate with `tools/knowledge/import_knowledge.py` rather than editing this copy.

These projects show three useful choices: TDIR demonstrates purpose-built proportions and animation, JKE demonstrates detailed characters built around familiar numbered joints, and later Shred models demonstrate deliberate atlas use and compact geometry in an enhanced installation. Choose the approach that matches the character's silhouette and movement rather than treating polygon count or a patch name as a quality setting.

The later [community model-methods research](community-discord-model-methods.md) adds Shred's own account of TDIR's three animation sets and replacement-resource strategy, plus practical rig/export decisions. For pre-release identity and the advertised additional characters, use the separate [TDIR / DBZ 4.0 investigation](tdir-beta-research.md); an advertised roster is not a recovered beta payload.

## Which precedent fits the requested model?

| Desired result | Useful precedent | Apply it this way |
| --- | --- | --- |
| A recognizable MP humanoid with armor, hair, skirt panels, or equipment | JKE Boba and Boc | Keep the animation contract; distribute geometry across body parts and neutral child meshes; give important regions deliberate UV space |
| An oversized fighter or substantially different proportions/gait | TDIR Broly and its three-size animation system | Author a matching rig and custom KEY/PUP resources, including weapon/aim role mappings |
| Good original-engine visual quality without a huge mesh | TDIR silhouette economy and JKE texture/LOD work | Spend faces on contour and articulation; use authored 256-square tiles, readable color regions, and useful mipmaps |
| A modern creature replacement with sharper surface detail | Shred's MotS+ Sith Undead and Vornskr | Use the corresponding creature rig and a renderer-supported atlas tier; do not infer MP registration from an NPC asset |
| A single skin usable across several installations | Combined portable approach | Native budgeted 3DO, explicit animation support, indexed and tested RGB565 options, plus a separately declared enhancement pack |

The historical authors supply strong artistic and technical precedents. No geometry or texture from their packages is included here. The measurements below are factual observations of identified releases; they are not a license to convert or redistribute the artwork.

## TDIR: custom animation without an identified executable patch

[The Destruction Is Real](https://www.massassi.net/levels/files/2727.shtml), credited to the TDiR Team and released 27 June 2003, offered differently sized characters and transformations. Its original readme states that executable files are not modified. Its installation and launcher evidence point to resource preparation and normal JK launch, with no identified engine-patch requirement. That supports an asset-based explanation of its character system; it is not a claim that every historical launch path has been instrumented.

The TDIR 1.1 character archive contains 97 3DOs: 92 have 52 hierarchy nodes and five have 18. The larger hierarchy contains ordinary, small, and large body branches. The mod supplies 99 KEY files in another archive and selects custom animations through `ky.pup`. The character archive's own “no new keys” declaration concerns that archive, not the entire TC.

| Model | Visible branch | Nodes | Highest geoset faces | Meshes | Largest mesh faces |
| --- | --- | ---: | ---: | ---: | ---: |
| Goku | Stock-indexed parts at 2–15, 17 | 52 | 2,019 | 15 | 402 |
| Krillin | Small branch at 18–28, 30–33 | 52 | 1,551 | 15 | 280 |
| Cell | Large branch at 36–51 | 52 | 2,082 | 16 | 398 |
| Normal Broly, `nbroli.3do` | Large branch at 36–49, 51 | 52 | 1,304 | 15 | 364 |
| Broly / USS Broly | Large branch at 36–49, 51 | 52 | 1,348 | 15 | 408 |

Counts are authored polygon records, not an assumption that every historical file is triangulated. One TDIR character mesh has 502 faces, so even this corpus does not support an absolute 500-face assertion.

Broly's two selected materials are ordinary 256×256 indexed MATs. His visual strength is not evidence of a special high-polygon renderer. The transformed Broly variants set face extra light to 1.0, while normal Broly uses 0.0. The TC also replaces the default colormap: 31 of its 256 RGB palette entries differ from the studied original JK default. A standalone skin on arbitrary stock maps does not inherit that palette control.

The original character credits name Shred18 and Gsusnme and credit Jenny Kitty for conversion to the new animation system. The tools listed include Milkshape 3D, Mat16, MatMaster, and Paint Shop Pro. Their transferable contributions are the silhouette, geometry segmentation, paint/UV work, and coordinated rig-animation package.

**Practical consequence:** choose custom KEY/PUP work when the design needs different movement. Merely changing the `.mpc` model field or using `SetThingModel` does not animate a different branch. A retained OpenJKDF2 headless test started 186 stock-key cases across the three Broly variants; their visible large branches remained rigid after root-motion removal even though the keys started and ordinary invisible player joints moved. That is a loading-versus-articulation result, not a rendered retail or full-TC compatibility test.

TDIR's PUP retains familiar role indices for head, neck, torso, weapon fire, and aim. Its documentation limits torso mouselook bending to medium-sized characters. When borrowing the multiple-size method, plan the gameplay role mapping as well as the animation tracks.

The launcher evidence identifies `.mpc` character selection. COG model replacement is a second mechanism used for runtime transformations. Keep these separate in a new implementation: selection, animation, and runtime model changes solve different problems.

## JKE: detailed geometry on a familiar animation contract

[Jedi Knight Enhanced](https://www.massassi.net/levels/files/3087.shtml), by Shred18, is a particularly useful visual benchmark for original-game character work. Its package readme requires its included December 2006 Unofficial Patch and 3D acceleration. Respect that supported installation, while examining the individual models to understand what actually creates their quality.

The JKE GOB contains 181 models, 433 MAT entries, four replacement KEYs, and a model registry. The four replacement keys are `kyfall.key`, `kyidle1b.key`, `kyidle2b.key`, and `kysbsabr.key`; no replacement PUP is included.

| Model | Faces at highest detail | Meshes / nodes | Largest mesh faces | Native LOD face totals |
| --- | ---: | ---: | ---: | --- |
| Kyle, `ky.3do` | 3,089 | 17 / 19 | 443 | 3,089 / 2,541 / 1,692 / 737 |
| MP Boba, `kyh4.3do` | 3,324 | 20 / 22 | 482 | 3,324 / 2,270 / 1,292 / 438 |
| MP C-3PO, `kya16.3do` | 3,250 | 16 / 18 | 466 | 3,250 / 1,849 / 1,240 / 283 |
| Stormtrooper, `st.3do` | 4,018 | 18 / 19 | 496 | 4,018 / 3,061 / 2,396 / 1,644 |
| Boc, `boc.3do` | 2,941 | 23 / 25 | 490 | 2,941 |

The Boba and Boc entries are explicitly registered as multiplayer models. Their usefulness as MP precedents is therefore stronger than inference from an attractive NPC filename.

Kyle retains stock indices 0–17 and adds hair under the head. Boba attaches jetpack/cape/ropes beneath the torso and a belt beneath the hip. Boc adds a skirt and five flaps beneath the hip, with tails beneath the head. Neutral rigid descendants add silhouette without requiring independent animation tracks. They do not provide cloth simulation, and a new model must still reserve its own reachable KEY domain before adding nodes.

Boba uses six 256-square RGB565 materials; Boc uses five. They allocate texture space by meaningful body/equipment region and retain four mip levels. Selected models use Gouraud lighting and perspective texture mode 1, generally without fullbright faces. Boba's two-sided flag occurs on 19 faces rather than being applied everywhere. These are useful defaults for controlled authoring experiments, not mandatory settings for every material.

**Practical consequence:** for a new MP humanoid, original body geometry on the numbered player rig, a small number of neutral detail children, deliberate UV islands, and multiple LODs is a strong middle option. It can produce convincing characters with a few thousand well-used faces. A 20,000-face source with noisy textures or poor proportions is not automatically an improvement.

### What the historical patch requirement means

ZeqMacaw's 4 December 2006 patch notes describe changes to resource/world limits and texture handling. They do not list a larger 3DO vertex, UV, face, or hierarchy budget. The same notes distinguish ordinary use from the optional `-p` COG extensions, which affect multiplayer checksum compatibility.

The package requirement should therefore be preserved without turning it into a false rule that “JKE-quality models require a polygon-limit patch.” Nor should the old patcher be applied to a modern OpenJKDF2 executable: it expects a particular original JK binary. Choose an explicit supported runtime for the model package.

## Later Shred creature work: intentional atlas space

The [MotS+ release](https://www.massassi.net/levels/files/3157.shtml), maintained by Shimstock, credits Shred's original models separately from his conversions of third-party work. The selected original creature models give useful surface and rig examples:

| Model | Highest-detail faces | Meshes / nodes | Largest V / UV / F | Selected material tier |
| --- | ---: | ---: | ---: | --- |
| Vornskr, `vr.3do` | 6,173 | 31 / 32 | 265 / 328 / 263 | One 2048-square RGB565 atlas |
| Sith Undead, `du.3do` | 4,406 | 20 / 21 | 267 / 302 / 255 | One 2048-square RGB565 atlas |
| Pirate 2, `p2.3do` | 3,891 | 22 / 23 | 234 / 261 / 292 | Three 1024-square body atlases plus a gun material |

The maxima are independent across meshes; they need not occur in one mesh. These assets have one geoset in the selected release. The asset archive alone does not establish that each is an MP skin. MotS+'s installation section labeled “VANILLA” still requires MotS Remastered 4.0, so those large atlases are not pristine-retail texture proof.

The Sith Undead preserves the base numbered joints, parents, and joint positions while adding a jaw/detail partitions. Some rest rotations and pivots differ; “compatible contract” should not be casually restated as “byte-identical rig.” Vornskr retains a creature-specific skeleton rather than being forced onto the player rig.

**Practical consequence:** use high-resolution atlas space for recognizable bone, cloth, facial, or armor structure; preserve readable regions and a strong contour. Offer an enhanced tier when that resolution matters, and build a deliberate lower-resolution material version for broader engine support. Do not simply downsample a noisy sheet or copy a one-mip choice without a distance/shimmer check.

Increasing texture resolution does not universally require proportional UV scaling. In the studied native GL path, normalization is capped at 256 and Shred's 2048 Undead atlas uses a 256-unit UV period. Test the intended renderer before changing that scale. Repacking islands is a different operation and does require a deliberate mapping.

## Primary forum lessons

The forum posts below provide attributable historical advice. Keep their working observations distinct from executable-specific measurements.

| Source | Contribution | Use in this workflow |
| --- | --- | --- |
| [Shred18, “3do limits,” 8 August 2005](https://forums.massassi.net/Editing_Forums/Jedi_Knight_and_Mysteries_of_the_Sith_Editing_Forum/thread_34814_page_1.html) | Reports a 500-face practical limit for triangulated exports from modeling packages, and recognizes a vertex limit | Budget below the tested 512/768/512 parser bounds and count UV splits independently |
| [ProdigyOddigy, JediKirby, and contributors, “3do limitations,” 8 July 2005](https://forums.massassi.net/Editing_Forums/Jedi_Knight_and_Mysteries_of_the_Sith_Editing_Forum/thread_33977_page_1.html) | Discussion corrects an asserted mesh cap and includes a working 54-mesh report | Separate per-mesh constraints, total model complexity, and uncertain anecdotal totals |
| [Ruthven, “highres textures,” 20 September 2005](https://forums.massassi.net/Editing_Forums/Jedi_Knight_and_Mysteries_of_the_Sith_Editing_Forum/thread_35901_page_1.html) | Describes detailed weapons using multiple 256-square 16-bit textures | Multiple well-authored tiles are a useful original-engine alternative to a huge atlas |
| [ProdigyOddigy, Emon, scelestus, and contributors, “A fully lit thing is a very, very bad thing,” 25–26 August 2005](https://forums.massassi.net/Editing_Forums/Jedi_Knight_and_Mysteries_of_the_Sith_Editing_Forum/thread_35291_page_1.html) | Investigates extra-light data and then a different lighting cause | Diagnose the actual material/model/scene combination; do not make everything fullbright |
| [Quib Mask, Quadranscentennial author changelog, November 2023](https://forums.massassi.net/Editing_Forums/Jedi_Knight_and_Mysteries_of_the_Sith_Editing_Forum/thread_68242_page_3.html) | Documents version-specific dynamic node manipulation and support for unanimated-node RLU overrides | Treat those features as explicit patched-engine dependencies, not stock COG abilities |

The [Jedi Knight Sith Engine Specs limits](https://jksith.wikidot.com/limits) give separate 512-position, 768-UV, and 512-face per-mesh bounds. Original executable inspection in this project corroborated those values for official JK 1.01 and the identified owned MotS binary. Dynamic allocation of model meshes/hierarchy is not an unlimited animation budget; the studied puppet cache has 64 node entries per track.

The [Shred Blender-to-Jedi-Knight tutorial](https://www.youtube.com/watch?v=DJnS-FQ8sko) is an additional author resource. Follow its current author-provided instructions for the intended Blender/add-on version; the compatibility conclusions above rest on the specified files and documented contracts.

## Identified release inputs

These identifiers let future users distinguish release revisions without redistributing the source archives or artwork.

| Input | Identity |
| --- | --- |
| [Massassi TDIR ZIP](https://www.massassi.net/media/levels/files/jkmod/TDiR.zip) | 34,989,718 bytes; SHA-256 `8aea49c26e5e1fb9b39742eb4fd034bef8b9fff99495929959be568c24c47de1` |
| TDIR 1.1 republished archive, used for detailed character measurements | SHA-256 `28b6d55e3e936792efd07e12255d0485cae29eef2a852473456059da17163f49`; [republication page](https://www.moddb.com/mods/the-destruction-is-real/downloads/the-destruction-is-real-111) |
| TDIR 1.1 `tdircharacter.gob` | SHA-256 `e09b6bf37f709e9d035a0aeb59ef8b851f79e79ecf0c87b1d2f23fffa361932f` |
| [Massassi JKE ZIP](https://www.massassi.net/media/levels/files/jkmod/JK_Enhanced.zip) | 40,948,383 bytes; SHA-256 `6e34e9b25cf2154496add2d809909e4a51e68b8cf5d494ccca803e928f31bd1c` |
| JKE `JKE.gob` | 178,637,896 bytes; SHA-256 `e031ccb4814e5928e22ba29ad95872b4e2245c78693a99a5d8bbc810ccf70bbf` |
| MotS+ 5.11 `mods/JKMRemasteredAssets.goo` | 452,144,378 bytes; SHA-256 `bd80cac9e6f2655ddbb0978af6aaff883444a32aa87fcf013b863bccdf645f92`; no full MotS+ ZIP hash is claimed |

Shred18, the TDIR team, Gsusnme, Jenny Kitty, the credited JKE patch authors, Shimstock, and the separately credited MotS+ contributors retain credit for their work. This workflow contributes its own practical synthesis, implementations, and tests; segmentation, child meshes, custom keys, LODs, and direct profile selection all have prior art.

For implementation choices, continue with [compatibility](compatibility.md). For release procedures, use [runtime and packaging](runtime-and-packaging.md).
