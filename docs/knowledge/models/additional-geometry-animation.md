# Additional geometry and animation research from the downloaded corpus

> Copied into RED from [CodaLilitari/JK-MotS-Model-Workflow@ca011b8](https://github.com/CodaLilitari/JK-MotS-Model-Workflow/blob/ca011b84c4fa5e1f3755e1543be2c1c1d25063c9/skills/jk-mots-model-workflow/references/additional-geometry-animation.md) (MIT; see [LICENSE](LICENSE.md)). Regenerate with `tools/knowledge/import_knowledge.py` rather than editing this copy.

Prepared 30 September 2026. This original synthesis supplements the workshop. The corpus database was opened with SQLite `mode=ro`; no corpus rebuild, network request, game launch or historical executable execution occurred. Source HTML, its indexed text, author testimony and our proposed tests remain separate.

The most useful additions are **KeyEd's documented retargeting failure and correction**, **Sige's warning about triangles crossing bone assignments**, **Shred's invisible torso/hip compatibility nodes**, and **SavageX's own explanation of muzzle-flash animation and lifecycle cleanup**. Several tutorials also expose concrete mistakes that should be preserved as warnings rather than copied into a new skill.

## New methods and corrective details

### 1. Retarget proportions with explicit node correspondence and inspect idle differentials

[KeyEd =D](https://forums.massassi.net/Editing_Forums/Jedi_Knight_and_Mysteries_of_the_Sith_Editing_Forum/thread_28662_page_1.html) is a particularly rich primary discussion, absent from the workshop 1.2.0 Markdown guides. Hell Raiser describes comparing source and target hierarchies, mapping corresponding nodes, adjusting their positions and recomputing KEY differentials. His 11 January 2005 release post #25 limits the initial tool to hierarchies named after Kyle with different node positions; broader cross-model compatibility remains a next stage.

On 16 January, Shred18 reports that his converted Yoda mostly works but certain idle animations flinch or separate body parts (#26). On 17 January, Hell Raiser attributes the failure to tiny original differentials becoming visible after scaling and announces that version 0.2 avoids scaling values below 0.00001 (#27). That is a dated author-reported repair, not a newly tested universal threshold. A modern conversion should compare the source and target motion across idle loop boundaries and make differential handling explicit; it should not blindly zero all motion.

This is stronger than the existing general instruction to adapt KEYs: it gives a specific conversion method, a reported counterexample and a precise historical correction. It does not prove KeyEd 0.2 currently runs or handles every skeleton.

### 2. Preserve semantic control nodes while replacing the visible body

The same thread adds Shred18's 10 January suggestion (#11): keep blank KY_TORSO and KY_HIP nodes and attach the visible replacements beneath them. He responds to Hell Raiser's warning that PUP head-pitch roles will otherwise refer to the old node numbers (#10). On 11 January Shred separately points out that a linked hierarchy of rigid mesh objects can be animated directly in 3ds Max without a deforming-bone workflow (#24).

This supplies a concrete option for users who need different geometry while preserving established control roles. It remains subject to native node numbering and KEY allocation constraints already explained in `compatibility.md`; creating a familiar name alone does not establish compatibility.

A separate [PUP JOINTS discussion](https://forums.massassi.net/Editing_Forums/Jedi_Knight_and_Mysteries_of_the_Sith_Editing_Forum/thread_11115_page_1.html) adds direct Shred attribution. On 18 February 2003 (#4), he explains matching PUP values to the 3DO hierarchy and suggests retaining arm/leg control nodes. Edward chooses another route: remapping semantic roles for his six-part actor (#6). Descent_pilot distinguishes the role number from the hierarchy-node value (#7); zagibu explains the ordinary player weapon hand's external-model relationship (#8). Retain these as two implementation choices, not an instruction to copy Kyle's numeric JOINTS table into every creature.

### 3. Decide rigid mesh boundaries before exporting a skinned donor

Sige's posts #17, #21 and #23 in the KeyEd thread distinguish one-bone-per-vertex storage from a valid rigid-mesh export. A triangle can still have corners assigned to different bones; splitting only by bone can produce jagged boundaries or duplicate triangles in adjacent moving meshes. His advice is to separate intended rigid parts while constructing the model rather than letting an exporter invent the boundary.

This refines the existing skeletal-animation caveat into a useful preflight: identify triangles spanning multiple intended rigid parts, deliberately cut or rebuild their articulation boundaries, and inspect the open/overlap behavior through motion. We did not retrieve or execute the plugins offered in that thread during this forum pass.

### 4. Use paired translation and pivot compensation carefully in legacy tools

[Fixing Pivot Points on 3dos](https://www.massassi.net/tutorials/pivot_fix/) explicitly credits **Mr. Xeinsane and Lilguy of Frontier Creature Creations**. Their Fun3do/JED method records original and moved node XYZ values, adds the old-minus-new difference to pivot XYZ, and only then rebuilds parent relationships. They warn that changing parents moves children and that another JED save can require repeating the repair.

The transferable method is to preserve the apparent geometry while changing its pivot, with before/after coordinate checks. It is not a universal modern Blender recipe. The tutorial's claim that certain collinear extra vertices make faces invisible is historical testimony requiring a focused reproduction. Do not delete intended boundary/seam vertices automatically. No original publication date is supplied in the inspected tutorial body.

### 5. Keep mesh replacement surgical, and detect numeric mistakes in instructions

Jeff Walters' [Build Your Own Character](https://www.massassi.net/articles/df/make3do.htm) is **JKDF2**, despite the site's `articles/df` path: it names res2.gob, ky.3do, io.3do and JK multiplayer selection. He credits Pax for introducing him to 3DO work and Alexei Novikov for ConMan. The example replaces a head mesh while remapping the donor's material references into the destination table.

The text first identifies Kyle's target as mesh 5, then later calls it mesh 7. Preserve the named-part/material-remap principle; inspect the actual file rather than trusting those inconsistent numbers. His saved-game warning is another historical reason to separate a fresh-start smoke test from save/load compatibility, not evidence that every current custom model breaks saves.

Darth DarKal's [Adding Sections/Sectors to 3dos](https://www.massassi.net/tutorials/char_3do/) adds a concrete legacy UV hazard: keep the imported part still, move the new piece to it, preserve the intended export reference, then replace only the corresponding mesh block and remap materials. The author also warns that applying the same detailed replacement to the other geosets can reduce performance. This is evidence for checking export origin, UV preservation and all LODs separately. It is not an endorsement of high-detail duplication at every distance. The body supplies no publication date.

### 6. Keep a minimal actor fixture, with explicit warnings about tutorial shortcuts

[Basic enemy modeling](https://www.massassi.net/tutorials/enemy/) is authored and signed **EvanC / Evan C**, not Mike Starr. It builds a three-mesh, three-node actor, assigns matching KEY tracks and demonstrates a PUP action table. Its value is an incremental model → animation → actor test, separate from a polished full creature.

Two shortcuts must stay bounded: the tutorial routes every action to the same walking KEY and removes JOINTS for its demonstration; neither is a production combat-actor recipe. Also, the displayed hierarchy table uses leg offsets ±0.075 and -0.05 while the explanatory sentence says ±0.15 and -0.1. Derive coordinates from the actual intended geometry and native transform semantics. The tutorial notes JED preview caching; refresh/reopen before concluding that an external edit failed. No tutorial publication date was established.

### 7. Recover the original two-axis mesh-limit experiment, with its counterexamples

In [3do surface limit per mesh — mystery solved!](https://forums.massassi.net/Editing_Forums/Jedi_Knight_and_Mysteries_of_the_Sith_Editing_Forum/thread_11842_page_1.html), **molotov42**, 30 March 2004 post #1, describes six purpose-built models separating a vertex boundary from a face boundary. His reported examples include success at 512 and failure at 513, changing one count while holding the other below or at the boundary. This provides a useful historical experiment design, more specific than a remembered polygon budget.

Friend14 reports failures below both counts (#3); molotov42 asks for a reproducible example (#4). GBK supplies a large-total-model counterexample (#5), while the later texture-vertex discussion remains unresolved (#6–9). The numeric claims must stay labeled reported tests on that historical setup. The six-model ZIP was not inspected or run in this pass, and no universal UV-count ceiling or complete explanation of default-cube failures was established.

### 8. Build weapon representations around their distinct coordinate references

SavageX's [Creating New Weapons](https://www.massassi.net/tutorials/savguns/) separately constructs pickup, external hand-held and POV models. The external export uses the hand as the reference; the internal model uses a prepared hierarchy and is checked with existing KEYs in Puppet JED. The nine-texture rule is explicitly tied to reserved hand slots in the included template.

This adds author attribution and a complete historical three-representation route to the existing weapon guidance. Do not transplant its numeric rotations, UV scales or material indices into an unrelated model. The tutorial is explicitly a JED 0.93-era procedure; publication date was not established from its body. The template download was not inspected during this forum/tutorial pass.

### 9. Distinguish local POV animation from network-visible effects and cleanup

In [Muzzle Flashes](https://forums.massassi.net/Editing_Forums/Jedi_Knight_and_Mysteries_of_the_Sith_Editing_Forum/thread_8106_page_1.html#post155391), **SavageX**, 7 June 2001 post #10, explains his own method rather than another person's guess. For POV he moves a one-face flash mesh between a hidden position and the muzzle, describes removing motion differentials to obtain an instantaneous change, and says he also used the approach for clip reloads. This wording needs native KEY inspection before turning it into a literal edit recipe; it does not authorize removing required syntax.

For the external model he describes switching between flash/no-flash variants, restoring the ordinary mesh on a timer and also on deselection, deactivation and death, with timer cancellation. For rapid fire he reports using an animated multi-cell material to avoid frequent network-visible mesh changes. The transferable test is interrupted-state cleanup and observing another player's view, not copying his 0.05-second or 10-fps values as universal settings.

The discussion contains earlier uncertain guesses from Sabre_Wulf and Jipe; SavageX's own explanation is the stronger attribution. Seifer supplies a different material-switch approach (#11), without a reproduction in the thread. No network-performance measurements were made here.

### 10. A custom player PUP is an integration feature, not just another skin file

[Making a new MP player model use a new pup](https://forums.massassi.net/Editing_Forums/Jedi_Knight_and_Mysteries_of_the_Sith_Editing_Forum/thread_6645_page_1.html) begins with Juztyn's custom snowman failing despite a models.dat entry (11 December 2000). Slug suggests model-dependent PUP selection in the player COG on 12 December (#4), and Deathbane27 recognizes the move from a skin pack into a mod (#6). The later code proposal even hedges between `pup` and `puppet`; do not publish it as working code.

[Skin Pack Problem!](https://forums.massassi.net/Editing_Forums/Jedi_Knight_and_Mysteries_of_the_Sith_Editing_Forum/thread_11194_page_1.html) records Spiral's similar alien problem in March 2003. Sabre_Wulf's trigger-based proposal explicitly says it is untested (#5) and warns about mod/host agreement. Its sample is incomplete. Keep the useful design requirement—coordinate model, puppet, animation resources and clients—and build a tested implementation separately.

These discussions add historical evidence for the workflow's ordinary-skin versus custom-animation choice. They do not establish multiplayer correctness, late-join behavior, respawn handling or a retail/MotS-safe ParseArg recipe.

### 11. A successful legacy re-export can hide lost LODs

In [3do Help](https://forums.massassi.net/Editing_Forums/Jedi_Knight_and_Mysteries_of_the_Sith_Editing_Forum/thread_47753_page_1.html), 0734ws reports the original model reappearing at distance. Shred18 recommends JED/ZED re-export and explicitly says that JED exports only the first LOD (18 July 2007 #7); the requester reports that the model then works (#8). This is a concrete caution: disappearing LOD symptoms may be removed by discarding geosets. Compare geoset counts before presenting that round trip as lossless repair.

Later the requester reports Stormtrooper's weapon tutorial successfully produced a concussion-rifle enemy (#13), then resolves an apparently inactive custom animation as merely too fast (#14), while firing still fails. That separates geometry, playback speed and gameplay-event failure. The linked tutorial was not opened externally here, and the grenade/fire problem remains unresolved.

## Evidence boundaries and material excluded from recommendations

All 13 retained source pages are additional citations relative to the workshop 1.2.0 Markdown guides; some corroborate an established method while adding an author, counterexample or tool-specific failure. Five are tutorial/article pages and eight are forum threads. Their local HTML hashes match their capture receipts, and each indexed full text matches its saved text file. Selected original HTML post headers supply precise author/date/post anchors. Tutorial dates remain unknown where the page provides none.

Eight other fully read pages were excluded from the retained set:

- ID26984, thread11853: unresolved hierarchy-math questions and an unsupported claim that pivots determine solidity. Do not treat it as a transform implementation.
- ID31692, thread9420: unanswered mesh-name/hierarchy problem.
- ID31189, thread8917: useful mismatch report, but the proposed universal zero-offset fallback conflicts with the need to distinguish engine and KEY allocation semantics; no verified repair.
- ID32411, `jo_character_models/4_SkeletalExtract/`: clearly Jedi Outcast/XSI/weighted-bolt material; excluded from native JK/MotS methods.
- ID27694, thread42173: relevant blending report, but its terminology is less precise than the current engine-correlated guide and supplies no needed replacement.
- ID26292, thread11172: an unqualified instruction to leave one walk translation at zero; too little context for a new recipe.
- ID29967, thread7697 and ID30642, thread8372: no demonstrated mounting/short-character repair; the latter's explanation that an extra hand makes the engine ignore a weapon is not established.

This is **21 fully read unique pages**: 13 retained and eight excluded (the final bullet contains two). Search coverage comprises 18 distinct read-only FTS query expressions across the corpus's 13,084 forum and 2,133 documentation records. The first 17 retrieved at most 40 ranked candidates each; the differential/interpolation follow-up retrieved 35. Those are retrieval limits, not a claim that every result body was read. Full query expressions, match totals, source links, attribution and separate raw/text SHA-256 hashes are in `evidence/additional-geometry-animation.json`. Original captures were retained for verification; their content is not republished in this ledger. No complete-corpus, current-runtime or source-image review is implied.

