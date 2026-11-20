# Historical Discord model methods: options, authors and limits

> Copied into RED from [CodaLilitari/JK-MotS-Model-Workflow@ca011b8](https://github.com/CodaLilitari/JK-MotS-Model-Workflow/blob/ca011b84c4fa5e1f3755e1543be2c1c1d25063c9/skills/jk-mots-model-workflow/references/community-discord-model-methods.md) (MIT; see [LICENSE](LICENSE.md)). Regenerate with `tools/knowledge/import_knowledge.py` rather than editing this copy.

This reference turns a bounded, server-wide Massassi Discord reading into choices for JKDF2/MotS character and weapon work. It complements the [recent Discord case studies](community-discord-research.md), [JKE/TDIR asset analysis](jke-tdir-shred-research.md), and [export and baking guide](blender-export-and-baking.md). The sources are authors describing their work; this reading did not run Blender, a game, or a multiplayer test.

## Choose a route before building the model

| User's goal | Practical route | Work the choice commits you to |
| --- | --- | --- |
| A new body using stock player animations | Align rigid replacement parts to the stock model, preserve its hierarchy and origins | Check proportions, node order, pivots, fallback pose and every stock animation used |
| Prefer Blender for the whole conversion | Copy corresponding origins and rebuild parent relationships, or adapt a verified stock hierarchy in Blender | Inspect the exported native transforms; an attractive Blender pose alone does not establish compatibility |
| Prefer an established hierarchy-transfer tool | Align new geometry, export, then import the original hierarchy through JED/ZED | Verify coordinate conventions and final metadata across both tools |
| A substantially different character size or motion vocabulary | Design a custom hierarchy and matching KEY set | Treat this as an animation and integration project, including multiplayer resource agreement |
| Custom or mocap-derived animations | Animate a control rig, then bake motion onto the rigid model objects before KEY export | Preserve the editable rig, inspect interpolation and test the exported animation in the target engine |
| More detail within a restrained native budget | Spend geometry on silhouette, use controlled texture bakes and author lower LODs | Test close views, distance transitions, lighting and the intended renderer separately |

These are alternatives, not an automatic ranking. Shred explicitly compares copying each old mesh's origin with the 3D cursor and rebuilding parents against transferring an original hierarchy in ZED. His reported speed reflects his practiced workflow; it is not a promise for an unfamiliar model. [Shred, 29 April 2025](https://discord.com/channels/881034839401328680/881044857328652328/1366903043664384081)

Stormtrooper provides a concrete Blender-only example: split a Luke OBJ into rigid body objects, align them against a deliberately zeroed Kyle hierarchy, reconstruct the hierarchy and export. His follow-up emphasizes matching origins, position, rotation and differing body proportions. The zeroing/world-center steps belong to that prepared example. They are not permission to erase arbitrary models' offsets or pivot semantics. [Construction, 19 May 2026](https://discord.com/channels/881034839401328680/881749278811377695/1506195512204722227), [origin and proportion explanation](https://discord.com/channels/881034839401328680/881749278811377695/1506198611564957757)

## Preserve Shred's methods, including the corrections

Shred's contribution is more specific than an appearance reference for JKE. He documents hierarchy transfer, animation fallback poses, per-mesh budgeting, texture-import setup, LOD choices and experiments with first-person face sorting. Urgon's 2022 Blender-Sith introduction also explicitly credits Shred with helping test several models in game. That introduction carefully distinguishes a broad import batch from comparatively limited export testing. Credit that testing contribution without converting it into universal exporter certification. [Urgon, 25 May 2022](https://discord.com/channels/881034839401328680/881749278811377695/979114556532092938)

Keep a fallback pose. Shred explains that hierarchy rotation values can supply a pose when no KEY animation is playing. He temporarily clears those rotations to align replacement geometry, then restores them; otherwise a character may briefly snap into a visibly inappropriate pose. Preserve both the original values and the reason for them. [Shred, 28 October 2025](https://discord.com/channels/881034839401328680/881044857328652328/1432751682114289804)

Check coordinate spaces before blaming the animation. During an earlier replacement-model investigation, Shred corrected his own diagnosis: the importer had already applied the insert offset, and he had aligned the new geometry incorrectly. The transferable lesson is to compare insert offset, mesh origin, hierarchy transform and pivot treatment explicitly. This is evidence of that resolved alignment mistake, not proof of a current importer defect. [Shred, 4 April 2025](https://discord.com/channels/881034839401328680/881044857328652328/1357887259520532672)

Treat detail and hierarchy as related decisions. Shred describes splitting an over-budget mesh into a main mesh and a child mesh, while recognizing that the extra node can complicate systems that expect a simpler structure. He also recalls using original models for JKE's farthest LOD. Preserve node/animation compatibility while measuring each mesh, and inspect transitions rather than judging only the highest-detail view. [Mesh split, 30 August 2024](https://discord.com/channels/881034839401328680/881749278811377695/1279254233782419608), [JKE LOD recollection, 10 May 2025](https://discord.com/channels/881034839401328680/881044857328652328/1370887667566252133)

For first-person weapons, Shred reports building small test models to understand face sorting, then dividing geometry into shorter segments along the viewing axis. He also reports remaining invisible details on the stormtrooper rifle. Use this as a method for isolating a sorting problem, with an explicit counterexample to any claim that segmentation always solves it. [Shred, 30 December 2022](https://discord.com/channels/881034839401328680/881044857328652328/1058600554537959496)

Shred links his own Blender-to-JK/MotS tutorial and describes a workflow using fewer than 500 triangles per mesh, a combined UV texture, and a 256-square image for the exporter followed by a possibly larger game image. That is an authoring recipe, not a newly measured universal engine limit. The video itself was not watched in this pass. Preserve the [guide's renderer-specific UV checks](blender-export-and-baking.md): do not rescale UVs solely because the final image is larger. [Shred's explanation, 4 August 2026](https://discord.com/channels/881034839401328680/881044857328652328/1534172685033017394), [author-linked tutorial](https://youtu.be/DJnS-FQ8sko)

## Bake animation and geometry for the native contract

Stormtrooper describes importing a 3DO, building an armature, attaching its rigid objects, retargeting mocap to that armature, baking to the mesh objects, and exporting KEY. His later instructions name Visual Keying and overwriting current actions. Our recommendation is to perform the destructive bake on a saved duplicate so the editable control rig survives. A rendered skinned or simulated mesh is not itself a native KEY animation. [Retargeting method, 24 October 2024](https://discord.com/channels/881034839401328680/881044857328652328/1299112932671754260), [bake settings, 21 January 2026](https://discord.com/channels/881034839401328680/881044857328652328/1463741134906982614)

Keep exporter version and failure date with animation reports. In November 2024, Urgon acknowledges Stormtrooper's interpolation/export complaint as a known issue. In February 2026 he links the rc4 release; its visible preview advertises interpolation and unchanged-axis export fixes. Stormtrooper subsequently says he has not tested that version extensively. Historical failure, announced repair and reproduced success are three different states. Inspect intermediate frames and transitions with the actual build being distributed. [Known issue response](https://discord.com/channels/881034839401328680/881044857328652328/1302930806716764161), [rc4 announcement](https://discord.com/channels/881034839401328680/881044857328652328/1470409773986873414), [remaining test uncertainty](https://discord.com/channels/881034839401328680/881749278811377695/1472711403780440114)

SMLiberator gives a particularly useful format diagnosis: sharp shading needs split native vertices because a 3DO normal belongs to a vertex, while Blender can distinguish normals at face corners. He connects the same issue to vertex colors and proposes an explicit merging option. Urgon's response records uncertainty about preserving normals through Blender's recalculation. Preserve those authors' contributions; use the separately documented fork tests to judge the current implementation. A historical belief about Blender's API is not a permanent technical limitation. [SMLiberator, 16 February 2026](https://discord.com/channels/881034839401328680/881749278811377695/1473076275554943109), [Urgon's response](https://discord.com/channels/881034839401328680/881749278811377695/1473079807079223510)

## Make material and lighting failures diagnosable

Start with the intended CMP when importing indexed materials. Shred explicitly tells an importer user to provide the model's palette file, commonly dflt.cmp, through the import options. A missing or incorrect palette is a concrete diagnostic branch before repainting a texture that merely previews incorrectly. [Shred, 23 April 2026](https://discord.com/channels/881034839401328680/881044857328652328/1497007098356174889)

Do not invent a MAT-count ceiling from one broken model. Shred initially asks whether 42 materials explain textures being replaced in game. Later the same day he reports that removing some did not fix it and another model with 49 textures works. The pair defeats that particular diagnosis; it does not prove there are no resource limits. Compare declared material names, indices and loaded resources before changing a supposed numeric cap. [Initial question](https://discord.com/channels/881034839401328680/881044857328652328/1262143101451899031), [counterexample](https://discord.com/channels/881034839401328680/881044857328652328/1262201083174846485)

Edward reports a distinction between accelerated blending and palette brightness lookup. Treat it as a reason to test indexed textures under the intended graphics settings, not as a new runtime result from this research. Use [retail and open-engine testing](testing-methods.md) and compare the same pose, camera, texture, palette and lighting while changing one renderer option. Keep baked shading, vertex lighting, palette behavior and fullbright choices separate. [Edward, 26 June 2026](https://discord.com/channels/881034839401328680/881044857328652328/1520059917229232138)

Dudeman reports finding overlooked problems by comparing valid MAT filenames against 3DO references, including dflt.mat; he describes a similar WAV/SND check. This is a useful bounded advisory task and a candidate for deterministic validation. His separate request for a more general geometry/hierarchy checker remains a feature request, not evidence that such a tool was delivered. [Resource-list comparison, 24 July 2026](https://discord.com/channels/881034839401328680/881034839401328683/1530281246465458247), [checker request, 24 June 2026](https://discord.com/channels/881034839401328680/881044857328652328/1519427248107687987)

## TDIR required a different package and animation commitment

Shred recalls that TDIR used a multi-character hierarchy and required three sets of animation; another message says the hierarchy supported three character sizes. This makes custom proportions a legitimate user option, while exposing the additional animation work rather than promising an ordinary skin swap. These are direct participant recollections, independently useful alongside the [local TDIR analysis](jke-tdir-shred-research.md). [25 July 2022](https://discord.com/channels/881034839401328680/881034839401328683/1001298815292280872), [21 December 2022](https://discord.com/channels/881034839401328680/881749278811377695/1055209704227344385)

In a June 2025 thread, Shred recalls TDIR's resource workaround: a custom res2.gob stripped to the minimum JK files, which prevented installation as an ordinary overlay mod. He contrasts JKE's later limit-raising patch. His question about a contemporary MotS failure remains unresolved; adjacent replies speculate about resource hashing or file mapping, and his later message still does not identify the cause. This supports documenting package assumptions and preserving the actual dependency list. It does not establish an exact resource ceiling, a leak date, or a certified retail MotS repair. [Shred, 16 June 2025](https://discord.com/channels/881034839401328680/1369508766881415228/1384345154088865903)

## Reading coverage and reproducibility

The live pass used server-wide search without a channel restriction. It read 23 valid result pages containing **472 message occurrences, 459 unique message IDs and 13 repeated occurrences**. Result groups covered five text channels (editing, showcase, massassi, techsupport and gaming-screenshots) plus the Mots Co-op Development and JkGR Updates & Discussion threads. Visible message dates ranged from 28 August 2021 to 30 September 2026. This is bounded query coverage, not the entire server or all historical messages.

| Query | Displayed result total | Pages read | Message occurrences read |
| --- | ---: | --- | ---: |
| vertices | 275 | 1 and 11 of 11 | 50 |
| Blender-Sith | 63 | All 3 pages | 63 |
| normals | 525 | 1 of 21 | 25 |
| from:shred5987 model | 142 | All 6 pages | 142 |
| palette | 236 | 1 and 10 of 10 | 36 |
| lighting | 2,060 | 1 and 83 of 83 | 35 |
| MAT | 616 | 1 and 25 of 25 | 41 |
| skin | 289 | 1 and 12 of 12 | 39 |
| model | 2,005 | 1 and 81 of 81 | 30 |
| "vertex normals" | 11 | All 1 page | 11 |

Large broad queries were sampled at their first and last pages. The complete Shred/model and Blender-Sith queries add the intervening author and tool history. Broad matching also returned unrelated word forms and topics; those are included in read counts but excluded from the modeling advice. Level design, unrelated games and personal conversation do not become model evidence. In particular, Dark Forces I palette/3DO instructions are not silently transplanted into JK/MotS.

The three additional TDIR messages were read by the coordinating researcher in a separate complete 21-result TDIR search. The resource-limit message also received an opened-thread context check; the two hierarchy messages were read as complete search results. They are labeled separately in the [source ledger](https://github.com/CodaLilitari/JK-MotS-Model-Workflow/blob/ca011b84c4fa5e1f3755e1543be2c1c1d25063c9/skills/jk-mots-model-workflow/references/evidence/community-discord-model-methods.json) and are not added to this pass's unique-message count.

Attribution uses each message's own author header, timestamp and body, excluding quoted reply headers. Missing reply parents remain missing. Displayed times have no assumed timezone; machine UTC values are retained only where the DOM supplied them. No Discord messages were posted, no attachments or linked tutorial media were inspected, and no game tests were performed. Public research preserves original paraphrases and stable message links, not complete conversations or expiring attachment URLs. Discord links may require server membership; the bundled synthesis remains readable offline.

