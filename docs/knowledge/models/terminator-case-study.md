# Terminator case study: combining conversion, proportion work, baking, and retail integration

> Copied into RED from [CodaLilitari/JK-MotS-Model-Workflow@ca011b8](https://github.com/CodaLilitari/JK-MotS-Model-Workflow/blob/ca011b84c4fa5e1f3755e1543be2c1c1d25063c9/skills/jk-mots-model-workflow/references/terminator-case-study.md) (MIT; see [LICENSE](LICENSE.md)). Regenerate with `tools/knowledge/import_knowledge.py` rather than editing this copy.

The T-800 work is useful because several different methods contributed to one character. Red supplied a conversion checkpoint; Lily refined the hybrid's proportions, optics, and baked finish; Coda added retail integration and later articulation/hand repairs; Andrei chose the design direction and corrected results that did not meet it. Each contribution solved a different part of the problem.

The reusable approach is to preserve the useful conversion, make bounded shape and surface changes, and offer distinct hand/material/engine options. A complete rebuild would have discarded working decisions without solving the integration problems.

## Choose the intervention that matches the defect

| User-visible problem | Method used or compared | Why choose it | Tradeoff |
| --- | --- | --- | --- |
| The supplied model needs the game's rig and native files | Red's initial conversion/fitting checkpoint | Establish a usable numbered hierarchy and model/material conversion | Inherited rig claims still need independent comparison and runtime testing |
| Head sits too high or body proportions feel wrong | Lily's local rigid head/jaw/eye translation with support-band adjustment | Change proportions while retaining joints, topology, UVs, and the accepted face | A good neutral silhouette does not ensure every neck pose remains attractive |
| Chrome finish is inconsistent | Surface-aware color baking into existing UV/material regions | Match an approved head finish while preserving panel/recess detail | Baked highlights stay fixed; this is not runtime reflection or PBR |
| Leg piston surfaces split during motion | Coda's single-owner piston shells and seated upper guides | Repair the motion relationship at the actual joint | New connector geometry needs pose-range clearance and an aesthetic check |
| Stock flesh hand remains beside a mechanical body hand | Original curl, bulkier concealment, stock-shaped fist, or custom mechanical fist options | Let the user choose recognizable hand shape versus concealment/size | Shared weapon hands vary; complete concealment can make the model comically bulky |
| A new hand looks flat gray beside the chrome forearm | Reuse the existing approved arm atlas with deliberate connector/cap UVs | Match finish without another texture generation or geometry change | Pattern can be busier at close range; review actual game views |

## Red's conversion checkpoint

The retained source notice credits Red with the supplied `t800_jk_v2` conversion and initial game-model fitting. It records Red's description of fitting to JKE Kyle's skeleton without including JKE geometry or textures. Preserve that attribution, while treating exact JKE conformity as a claim requiring comparison against the intended reference rig.

The original earlier package was not retained as the current native input; the later package carries an accepted checkpoint. The practical lesson is to carry forward exact accepted files and their hashes, not to reconstruct a previous agent's output from a verbal summary.

Red's later work also contributed the idea of eye-face extra light. An implementation idea and a transplanted source asset are different contributions. The inherited hybrid notice specifically excludes SFM geometry/textures and a generated atlas from that revision.

## Lily's hybrid integration and proportion method

Lily's September 23 integration notice credits tapered chest narrowing/flattening, rigid head/jaw adjustment with support extension, optical extraction and far-LOD restoration, an appended eye child, forward-lens UVs with a small procedural red MAT, normal repairs, bounds repairs, bounded far-distance proxies, and unique material filenames.

Her later revisions narrowed the task rather than replacing the character:

- The r2 head pilot shortened the added neck extension while preserving the core hierarchy and existing mesh/UV topology. It also established a surface-normal-based baked chrome treatment using the head material's existing panel detail.
- The r3 revision shortened the neck from that accepted pilot and retained all five active MATs.
- The r4 revision lowered skull, jaw, and eyes together another 0.0009 torso-local units, adjusted the support band, and extended the accepted tonal approach to torso, arms, and legs. The approved head and eye MATs remained byte-identical.

This gives a practical method for requested proportion corrections: move the complete anatomical group rigidly, deform only its support transition, preserve the rig, and compare old/new geometry under identical materials and cameras. Then compare old/new materials on identical geometry. That separates a shape improvement from a lighting illusion.

The support deformation used a monotone inverse and inverse-transpose normal handling. More generally, when a nonlinear edit changes the transition surface, recompute normals consistently and verify that the field does not fold. Keep the scale in native coordinates unless a physical unit conversion is actually established.

## Surface-aware chrome without replacing the atlas

The r4 finish preserved the source material's panel markings, apertures, recesses, UV ownership, and every face-corner UV coordinate. It evaluated reflection-like bands on actual surfaces and used elongated part axes to favor longitudinal highlights on cylindrical pieces. That is a useful alternative when an image-generated atlas does not line up with the model's islands.

Three implementation lessons transfer to other models:

1. **Shade each contributing surface before averaging overlapping UV samples.** Averaging opposite surface normals first can cancel them to zero and destroy the intended response.
2. **Skip and report zero-area UV triangles when defining bake coverage.** Do not divide by zero or explode the coordinates. Preserve their original native references unless the task includes UV repair.
3. **Use the final decoded MAT for inspection.** A larger working canvas can improve sampling/padding, but it does not turn a final 256-square RGB565 material into a high-resolution texture.

LOD0 had priority in the bake and later LODs filled uncovered samples. The body MATs retained 256/128/64/32 mip levels; the small eye tile retained its existing mip chain. The generated finish is baked base color with fixed highlights, not dynamic chrome, extra lights, or bloom. It should be judged in motion and under several engine lighting conditions.

For the implementation recipe, use the analytic finish option in [Blender export and baking](blender-export-and-baking.md).

## Coda's retail integration

The incoming r4 package contained native 3DO/MAT and inspection exports, but did not claim an in-game-tested player release. Coda's integration retained the 24-node hierarchy and addressed two separate compatibility requirements: keep animation-reserved slot 18 invisible, and derive mod-local KEY copies whose `JOINTS` allocation covers all 24 nodes. Existing tracks, markers, and timing stayed intact.

The selectable MP package derived registry/display-string resources from the owner's installation and produced separate JK/MotS indexed palettes plus RGB565 options. It did not silently become a campaign-player replacement. This is a useful packaging method when the user wants a standalone skin and source game resources should remain private.

The released v1.0.1 baseline includes original-hand and holding-curl options and four LODs. Its original-hand triangle totals are 4,258 / 3,396 / 2,355 / 1,316. The curl retains the detailed posed hand across the body's LODs, producing 4,258 / 3,448 / 2,472 / 1,501. That is an explicit hand-readability choice, with a distance-cost tradeoff.

## Leg pistons: repair motion ownership before adding a cover

The original piston shells included complementary faces assigned to independently animated hip and thigh parts. They looked connected at rest but could separate in motion. The successful repair placed each shell's faces under its corresponding thigh and added narrow closed continuations that seat into compact hip guides.

The guides used the actual stock animation translation range. Endpoint inclusion inside the guide planes established containment across the checked linear intervals; a separate pose sample suite and matched renders supported the visible result. This is stronger than choosing a guide size from a handful of screenshots. Custom KEYs, priority blends outside the measured domain, and untested LOD transitions remain separate.

The final patch changed only hip/thigh mesh blocks in each native LOD. It restored all non-leg sections to the approved baseline, including the original neck. The abandoned experimental neck work did not become part of the released leg repair, and the release did not claim a neck-cable fix. A repair can be successful while another behavior is intentionally retained.

## Hand alternatives: expose the choice to the user

The external weapon models contain separate hand faces. Consequently, “make the T-800 hand larger” is not a guaranteed per-skin removal mechanism. A readable mechanical hand and full concealment are different visual objectives.

| Option | Useful result | Known compromise in this case |
| --- | --- | --- |
| Approved original hand | Preserves the original design | Stock external-weapon flesh can remain visible |
| Curl assembled from original segments | Compact open grip without new cover geometry | Does not reliably conceal stock hands; not a clenched fist |
| Solid covered curl | No flesh visible in the sampled original-MotS views | Large, bulky hand; obscures a small held thermal detonator; rejected artistically |
| Slightly enlarged stock-shaped silver fist | Familiar grip silhouette and small source change | Repeater/thermal views retain exposure |
| Forward-shifted wrist trial | Keeps a connected extended wrist | Introduces exposure in previously better fists/rifle cases; not promoted |
| Closed mechanical fist with shaped fingers | More mechanical detail and a solid core | Some repeater/thermal flesh remains; requires a finish match |
| Same mechanical geometry mapped to arm chrome atlas | Better continuity between hand and forearm | Busier close-up pattern; does not establish concealment for untested weapons |

The stock-derived fist geometry is generated locally from the owner's game files rather than bundled as a workflow asset. Construction at a modest scale increase and closed overlapping solids provided a useful option; repeating enlargement indefinitely did not. A convex hull around open/concave geometry was withdrawn as concealment proof when actual surface/runtime evidence contradicted that interpretation.

The latest studied September 30 chrome-fist checkpoint changes UV/material mapping while preserving the mechanical geometry exactly. Its four hand LODs have 251 vertices, 460 faces, and 456 UVs each. Connector UVs follow the part coherently, cap islands have positive two-dimensional area, and the unchanged arm atlas supplies the finish. Black material detail remains opaque because color-key/alpha masking is disabled in that material lane.

This later checkpoint remained a local revision at the time of the study, with bounded original-MotS fists/rifle observations. It was not a new public release, a connected-client certification, or a claim of final user acceptance. Earlier repeater/thermal exposure was still unresolved.

## Collaboration that improves the model

Give a reviewer a concrete choice: matched geometry views, a texture-only comparison, the current runtime captures, and the exact question. Lily's later hand reviews helped evaluate wrist continuity, proportions, solidity, and finish, while keeping visible stock-hand exposure explicit. Andrei's rejection of a technically concealing but bulky design correctly changed the next method choice.

A reviewer recommendation is useful input; direct source comparison and game inspection remain part of implementation. Preserve the accepted variant and the rejected experiment separately so the next change starts from the right checkpoint.

## Attribution and source boundary

The source notice identifies [T 800 low poly (PORT/P3D)](https://sketchfab.com/3d-models/t-800-low-poly-portp3d-ce8c971d7b4240229bbebed9b53198b6), credited to [Jonn](https://sketchfab.com/Sketchfab_apk_gratis), with a declared CC BY 4.0 license. The title and notice identify it as a port; that declaration does not independently establish earlier mesh authorship or franchise rights. This case study includes original analysis, not the source model or its textures.

Credit Red for the initial conversion checkpoint and eye-light idea; Lilitari/Lily for hybrid integration, proportion, optic, and baked-finish work; Coda for retail integration, material-lane packaging, animation allocation support, later repairs, and validation; Andrei for project direction and acceptance decisions. Preserve earlier source credits when distributing any asset that depends on them.

Use [compatibility](compatibility.md) to select the rig/engine route and [runtime and packaging](runtime-and-packaging.md) to turn the chosen option into a stated release.
