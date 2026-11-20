# MotS model and personality integration: seven inspected packages

> Copied into RED from [CodaLilitari/JK-MotS-Model-Workflow@ca011b8](https://github.com/CodaLilitari/JK-MotS-Model-Workflow/blob/ca011b84c4fa5e1f3755e1543be2c1c1d25063c9/skills/jk-mots-model-workflow/references/downloaded-model-integration.md) (MIT; see [LICENSE](LICENSE.md)). Regenerate with `tools/knowledge/import_knowledge.py` rather than editing this copy.

Original research from the downloaded Massassi corpus, 30 September 2026. These are passive observations of actual archive members, not descriptions copied from download pages and not game tests. Seven ZIP hashes were matched to the acquisition ledger; every ZIP member read passed CRC checking; six GOO directories were checked for bounds; 187 3DOs, nine KEYs and 1,931 MATs were parsed. The 3DO analysis checks material/hierarchy counts and records geometry counts and node contracts. The MAT analysis checks headers, texture/mipmap bounds and byte consumption; it does not assess their appearance under a palette or renderer. No game, original executable or archive script was executed, and no original package was modified.

Selected measurements, package identities and exact source-member hashes are recorded in [the evidence ledger](https://github.com/CodaLilitari/JK-MotS-Model-Workflow/blob/ca011b84c4fa5e1f3755e1543be2c1c1d25063c9/skills/jk-mots-model-workflow/references/evidence/downloaded-model-integration.json). This research includes original analysis; the original game assets and full source readmes are separate works, with their authors’ rights retained.

| Package / credited author | 3DO / MAT / KEY / PUP members | Distinct useful route |
|---|---:|---|
| [BoaM Skin Pack v1.2](https://www.massassi.net/levels/files/1007.shtml), Anthony Piggott and individually credited contributors | 31 / 184 / 1 / 1 | Selectable skins; a compound carried-character model and associated death animation |
| [Al's Skins for MotS S.E.](https://www.massassi.net/levels/files/1386.shtml), Al MacDonald; MotS compilation/sabers by Galen Starfield | 123 / 1,514 / 0 / 0 | Large skin catalog using existing animation contracts and personality-specific registrations |
| [Phase 2 Darktrooper](https://www.massassi.net/levels/files/837.shtml), package credited to Alfred The Ewok / aLfreD_444 | 6 / 18 / 0 / 0 | Existing body resource replacement plus separately selected POV and external weapon models |
| [MotS children](https://www.massassi.net/levels/files/861.shtml), JM | 6 / 25 / 0 / 0 | Reusing a non-player actor hierarchy and geometry with material variants |
| [Trade Federation](https://www.massassi.net/levels/files/1769.shtml), Robban | 15 / 163 / 6 / 5 | A walking enemy with a dedicated eleven-node rig, six native animations and a matching PUP |
| [Predator Patch Beta](https://www.massassi.net/levels/files/836.shtml), Rulefreak and named contributors | 5 / 18 / 2 / 0 | Personality, body-state switching, inventory and weapon-model integration |
| [Squashed Jabba](https://www.massassi.net/levels/files/991.shtml), Andy / Darth Dude | 1 / 9 / 0 / 0 | A counterexample to treating a familiar filename or node count as proof of rig compatibility |

Counts describe bundled files, not unique playable characters or demonstrated compatibility. The large Al's archive heavily weights the texture statistics below.

## Concrete options and lessons

### 1. A skin catalog is a registry, not a count of 3DO files

Al's MotS compilation contains 123 3DOs but `misc/models.dat` declares and contains 193 numbered rows, with unique indices 0–192. Nineteen existing models appear twice under different personality values: for example the red Mandalore model is registered with personality 0 and again with personality 2 and a different sound class. The table contains personality values 0–4. BoaM declares and contains 86 rows, also without duplicate indices, while bundling only 31 models. Many registry dependencies therefore come from the base installation.

**User option:** add a selectable skin with its sound/personality mapping while reusing the intended stock behavior. **Checks:** count rows, reject duplicate indices, preserve the chosen personality classification, resolve model and sound references against both the patch and the exact game resources, and confirm the normal selection UI. Do not label all 193 Al's entries as 193 new meshes. Absence from the patch alone is not a missing-resource failure.

Al's `models.dat` also references `3po.snd`, while this archive supplies `c3po.snd`; `amidala.snd` is another referenced name not supplied here. Those are concrete dependency-check targets, not established runtime defects without checking the base/resource search path. The included `sabers.dat` has twenty entries, complementing the skin catalog with separately registered saber side/tip materials and preview BMs.

### 2. Replacing existing body names and replacing POV weapons are separate operations

Phase 2 Darktrooper supplies byte-identical bodies under `3do/kk.3do`, `3do/mj.3do` and `3do/ky.3do` (SHA-256 `63469f12aa462d65349c4fec36261870cc49dd94197bfb4b65f524ae4e06b037`). Each has eighteen nodes and four geometry sets of sixteen meshes. The same bytes occur as `3do/phase2.3do` in Al's compilation. That is evidence of shared asset lineage; it does not by itself prove the direction of copying. Al's readme credits its skins to Al MacDonald; Alfred is credited for the separate integration package. Preserve both distinctions rather than crediting all geometry to the integration author.

The assault cannon is supplied separately as a one-node external model (`acang.3do`) and four-node POV models (`acanv.3do`, `acanv_m.3do`). Both bundled weapon COGs select the appropriate POV model and assign the external weapon mesh. **User option:** a replacement for known SP/MP body resources plus a coordinated weapon package. **Checks:** both protagonists, first-person hands, external mesh, weapon switches, firing and death/drop states. Three body filenames do not imply three independently authored meshes.

The bundled readme has stale Espo title/filename fields and says new KEYs are present, but the payload contains no KEY. Use the inspected files to determine requirements; retain the readme as attribution/history evidence, not as an infallible inventory.

### 3. A carried object can be a rigid child with one specially authored transition

BoaM's `dfkyjspr.3do` retains a recognizable player-node sequence and adds node 18, `JAN`, parented to node 17, `k_torso`. It has nineteen hierarchy nodes, seventeen meshes and one geometry set. Its material table has 51 rows but only 27 distinct filenames, a useful reminder that material slots and unique textures are different budgets.

The companion `kyjdie.key` declares nineteen joints and nineteen tracks, including a seven-entry `JAN` track. `misc/pup/ky.pup` directs ground death/death2 and armed/saber death paths to that key while retaining other existing player motions. **User option:** keep an extra rigid carried object attached through ordinary movement, then animate it during a particular event. **Checks:** the added node's transform/type, every KEY allocation needed by the full hierarchy, normal and exceptional deaths, swimming, weapon transitions and the patch's effect on other skins. This is package evidence for the design, not proof that every transition is retail-safe.

The globally named `ky.pup` is an integration surface that can conflict with another mod and can affect other models selecting that PUP. Do not transplant only the 3DO and assume its special behavior follows. The model header credits Anthony Piggott's mesh, Al MacDonald's Kyle clothing and SavageX's Jan torso textures. It expressly requires written permission for use outside BoaM3/the BoaM Skin Pack; the taller Vader header also restricts reuse. The readme's permission to distribute the original pack does not automatically remove those asset-reuse conditions.

### 4. Material variants can reuse geometry exactly, while the actor rig still differs from Kyle

JM's six child 3DOs have identical decoded content from `SECTION: GEOMETRYDEF` through the hierarchy, after normalizing line endings for comparison. Their material-resource tables provide the variations. Each retains four geometry sets with seventeen meshes and an eighteen-node actor hierarchy: head is node 1, torso node 16 and weapon node 17. Kyle-like player examples instead use head 15 and torso 17. Equal node counts therefore do not establish interchangeable animations.

The readme explicitly derives the actors from a MotS pirate and supplies six template examples using `kid.pup`, `kid.snd`, `actor_child.cog`, reduced health and defined `size`/`movesize`. The ZIP contains the sound class and COG but no `kid.pup` or KEYs, so integration must resolve the game-side dependencies. **User option:** adapt a suitable existing actor, share its geometry across material variants and preserve its actor-specific contract. **Checks:** resource closure, locomotion and behavior, sound references, collision envelope and palette appearance. JM allows derivative use with credit in the readme; retain that author credit. Personal anecdotes in the source readme are unnecessary to the public technical lesson.

### 5. A very different enemy shape can justify a dedicated native rig and PUP

Robban's walking droid-star fighter replacement is `at.3do`: eleven nodes and ten meshes, organized around BODY, HEAD, paired wing and foot segments. Six `tfdf-*.key` files each declare eleven joints and tracks for all indices 0–10. The bundled `atst.pup` selects scanning, walking, flying/run, shooting and two death animations. Its weapon and aiming roles map to node 2 (HEAD), rather than assuming a Kyle hand node.

**User option:** author a dedicated rigid hierarchy and KEY/PUP set when body mechanics no longer fit a biped. **Checks:** PUP roles → actual hierarchy nodes, event markers, animation states, aiming origin and actor behavior. The separately bundled flying versions have thirteen nodes and parent fields set to -1; they must not be assumed to share the walking model's eleven-node contract merely because they depict the same machine. This is model/actor integration research, with no level-layout work.

Seven battle-droid body replacements in the same archive have twenty nodes; their appended `Cone` nodes are separate roots. The associated player-style PUP files refer mainly to unbundled Kyle KEYs. Record this as an allocation and attachment audit target; do not infer that an old released package proves arbitrary extra roots are safe on every engine.

### 6. A shared filename and eighteen nodes can conceal a completely different animation contract

Squashed Jabba bundles only `3do/ky.3do` and nine MATs. Its eighteen nodes are a Jabba hierarchy with tail, eyelid, mouth and tongue, not the Kyle order. Node 15 is `tailtip`; node 17 is `uglyhead`; no replacement KEY or PUP is included. **Use as a diagnostic counterexample:** a skin loader accepting the filename, or a validator checking only node count, says little about what its motion will look like. Full ordered hierarchy and attachment comparisons are required. No runtime defect was reproduced here, and the package is not offered as a sound retargeting recipe. Its readme credits Andy/Darth Dude and additionally LucasArts and Anthony Piggott, and forbids derivative use.

### 7. A personality conversion ties the model to inventory, weapons and reversible body states

Predator Beta modifies three `.per` files and the item/model registries as well as fourteen COGs. Its bounty personality identifies personality 2, selects the disc as default weapon, grants battery/cloak inventory and sets inventory bin 118. The fists script tests bin 118 to select Predator POV/external meshes; this is a concrete linkage between personality state and visual assets. The cloak script switches between `kyH4.3do` and `ky2.3do`, including deactivation/death restoration paths. Both models have the same eighteen-node topology signature, although one has four geometry sets and the cloak form has one. Their model/material state is therefore deliberately different without replacing the animated node contract.

**User option:** a character class with distinct equipment and effects, rather than a cosmetic skin alone. **Checks:** ordinary selection and spawn, class changes, switch/equip, cloak activation/depletion/death, restoration of the visible body, and client observation. Rulefreak credits Kell for wristblade/body assets, Matt the Hutt for shoulder-cannon code, Vadal for zoom/cloak work, Speak and Will Price for jump animations and Tom “Magi” Smallwood for part of the cloak skin. Preserve those contributions separately.

### 8. Historic releases supply negative tests as well as techniques

Predator's `models.dat` declares 56 models but has 58 data rows, repeating indices 18 and 19. Ten rows select the same Predator body for personality 2. This is an exact serialized-data inconsistency; the engine's response was not tested. Its readme also reports client-side disc return, remote shield visibility and shoulder-cannon scoring issues. Together these support a validation checklist that checks registry arithmetic before launching and then separately tests local appearance, remote appearance, resource lifetime and attribution/scoring where a character package changes combat.

The source is labeled beta and contains unresolved behavior. A useful workflow should offer its architecture while explicitly declining to treat the package as a fully tested template to copy unchanged.

### 9. Small indexed textures and authored geometry sets are viable choices, not automatic quality guarantees

All 1,931 sampled MAT headers use indexed color mode and eight bits per pixel. There are 1,900 texture MAT files and 31 color MAT files. Most texture records use one mip level; one Trade Federation texture has four. Rectangular sheets are common, including 32×64 and 64×32, and Al's saber resources include 32×256. The sample supports keeping an explicit indexed-texture route and choosing dimensions for the surface, rather than requiring every replacement to start as a large square PBR map. It does not establish a global size limit or that historical one-level mip choices are optimal.

None of these seven packages supplies a CMP. Test the intended target palette/renderer explicitly rather than assuming that eight-bit status implies correct colors. BoaM's readme itself reports poor research-worker texture appearance in software mode. Its report justifies comparing software and accelerated paths; it does not demonstrate the result of a new render here.

Four geometry sets are common but not universal: Al's pack has 73 four-geoset models and 50 one-geoset models; BoaM has 24 and seven respectively. Representative Phase 2 and IG-88 models reduce face counts across successive sets while keeping the same mesh slots. **User option:** preserve coherent authored lower-detail sets when adapting a stock contract, or deliberately author one set with measured target-engine performance. **Checks:** same animated mesh roles at each distance, silhouette/seams, material assignments and transitions. Merely finding four sets is not proof that they are visually good LODs.

### 10. Cosmetic effects and sound identity may carry global integration changes

BoaM's Vader breathing is implemented in the included `kyle_m.cog`, with model gating against `kydarth.3do`, a `vbreath.wav` sound and a model-specific sound-start path. The skin package also supplies sound and taunt assets. Al's compilation ships twenty-one SND files and twenty saber definitions without new KEY/PUP files. These demonstrate different levels of scope: a selectable model and sound mapping can be relatively contained, whereas changing the global player COG expands the testing and merge surface. A skin workflow should expose that choice instead of silently replacing global scripts for an ornamental feature.

## Limits and next checks

This is structural and source-level evidence. No palette render, animation playback, retail/OpenMotS comparison, client/server run or asset-license clearance was performed. Package-internal references were identified; stock archives were not traversed here. Some containers use `mat/`, others `3do/mat/`, and the loose child pack places MATs at its root; dependency analysis must preserve the actual packaging layout and intended engine search rules. The reference material is useful as a method corpus, but full source assets should remain separate from the distributable skill unless their rights are individually resolved.
