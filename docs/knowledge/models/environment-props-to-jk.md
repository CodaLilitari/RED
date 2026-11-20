# Generated environment props to native JK models

> Copied into RED from [CodaLilitari/JK-MotS-Model-Workflow@ca011b8](https://github.com/CodaLilitari/JK-MotS-Model-Workflow/blob/ca011b84c4fa5e1f3755e1543be2c1c1d25063c9/skills/jk-mots-model-workflow/references/environment-props-to-jk.md) (MIT; see [LICENSE](LICENSE.md)). Regenerate with `tools/knowledge/import_knowledge.py` rather than editing this copy.

Use this route for statues, braziers, scenery and similar model assets placed in a JK/MotS level. It covers the model, materials and handoff. Use the separate `jk-mots-level-workflow` for sector/portal geometry, templates, placement, collision and clearance; use `jk-mots-cog-workflow` when a door, lift or other object needs scripted behavior. A generated GLB or exported 3DO does not supply those level systems.

This guide draws on Wren's November 2026 Votary, Warden and brazier work with Andrei and Lily. It shares methods and measured counts without redistributing their private reference art, generated assets or conversations. Existing [texture and mapped-review guidance](textures-and-imagegen.md) still applies.

## Establish the shape and placement contract

Record the intended silhouette, source-reference identity, units, facing, origin, height and footprint. Measure the proposed placement volume in all three axes. A prop whose height fits an alcove can still exceed its width/depth, obstruct a route or place its origin below the floor.

Keep the editable generated source and its original UV/material data. Compare front, side, rear and the features that identify the design. Repair a crest, blade, bowl or plinth deliberately when generation changes it. In Wren's saved report, Tripo turned the Warden's flat crest into a spike; Wren rebuilt a six-quad fin. The brazier's dense crystals became six simple pyramids. These are authored silhouette decisions after generation.

The final Warden measures **0.423216 × 0.361376 × 1.30 JKU** in a fresh native-file parse. Its saved alcove plan is **0.38 × 0.28 × 1.52**. The report chose placement at the alcove mouth to retain height, rather than silently shrinking the statue to about 1.01 JKU. The reviewed prop bundles do not establish that the final level placement was executed or tested.

## Measure and reduce the actual source

Use [the Tripo count ledger](tripo-to-jk.md#count-the-download-not-the-quad-label). The requested quad count, downloaded mixed polygons, triangulated GLB and native face records need separate labels. Retain valid planar polygons when useful; check convexity, winding and seams as well as counts. Reduce only as needed for the chosen target and visual budget.

Wren's archived source inspection reports these inputs; the original FBX/GLBs were not included in the reviewed bundles:

| Source | Reported FBX polygons | Triangles + quads | Reported GLB triangles |
| --- | ---: | --- | ---: |
| Votary | 593 | 143 + 450 | 1,043 |
| Warden | 630 | 224 + 406 | 1,036 |
| Brazier | 520 | 160 + 360 | 880 |

The saved reduction scripts used un-subdivision, planar dissolve, limited collapse and selective triangulation of residual nonplanar faces. Those operations were inspected, not rerun for this publication. Inspect the silhouette after each useful reduction; retain or rebuild broad defining surfaces before spending faces on small hidden details.

Recount the **emitted native file**, including vertices and texture vertices. Hard normals and seams can split authoring vertices: the included brazier OBJ has 30 crystal positions, while the selected native crystal mesh has 72 vertices. If excessive normal splitting causes a budget failure, test smooth shading with deliberate sharp edges retained, then compare appearance and counts. Erasing all intended hard edges is not a required optimization.

Fresh read-only parsing of the final selected 3DOs on November 12 found:

| Prop | Native faces | Triangle equivalent | Native vertices | Maximum vertices / UVs / faces in one mesh |
| --- | ---: | ---: | ---: | --- |
| Votary | 444 | 460 | 245 | 237 / 588 / 438 |
| Warden | 427 | 447 | 245 | 245 / 598 / 427 |
| Brazier | 138 | 160 | 168 | 96 / 233 / 114 |

These files passed the review's face index/material/UV range checks. Their saved reports also describe planarity checks and a conservative per-mesh working margin of 460 faces, 400 vertices and 650 UVs beneath the selected 512/512/768 bounds. The fresh parse did not rerun planarity or game tests. See [compatibility](compatibility.md#budget-each-mesh-and-each-animation-domain) for the qualified limits.

Keep the chosen native exporter result identifiable. The included RED comparison files have 450, 432 and 147 faces with the same triangle equivalents, and change the blade/crystal lighting from FullyLit to Gouraud. Successful format readback alone does not make the alternate export visually interchangeable.

## Separate initial baking from later repainting

An initial reduction may require a deliberate new atlas and bake from the untouched source. Once the game UVs and atlas are accepted, a later drop-in repaint has a different contract: retain those dimensions, island positions, orientation, padding and ownership.

Give a repaint collaborator the accepted mesh, exact atlas, UV overlay and mapped views, plus the specific regions to change. For an atlas shared by multiple props, record each region's owner and any intentionally overlapping faces. Compare unchanged regions pixel by pixel in decoded images. A returned image with rearranged islands is a new surface source, not a drop-in replacement, even if its flat appearance is attractive. Keep it separately if useful; deliberate remapping is a different task.

Wren's saved review rejected a generated 1254-pixel Warden replacement because it redrew the layout. The accepted route used Lily's first shared-atlas repaint, followed by Wren's local crimson correction. Fresh decoded comparison confirms **102 changed pixels, all on the Warden half**, while the Votary half remains identical. Credit Lily's repaint and Wren's correction separately.

Mapped review also caught a dark triangle in the first brazier repaint that served **33 faces**, according to the saved review. A visually small atlas change can cover a large part of the model. Inspect the affected faces from several views, then inspect the written MAT's decoded pixels, palette/encoding, lighting modes and mips. The selected later Lily repaint differs by **2,696 pixels** from the first; fresh comparison confirms that count, not a new in-game appearance judgment.

Retain an explicit selected-material manifest. In this case, the later final-textures bundle selects the corrected shared statue MAT and second brazier repaint while leaving all three 3DOs byte-identical to the earlier essentials bundle. Earlier preview images do not automatically show the final paint. Follow [same-name material editions](same-name-material-editions.md) when different valid bytes share a MAT filename; preserve their ownership and hashes instead of silently choosing the first file found.

## Hand the prop to the level author

Deliver the selected 3DO/MATs and a compact receipt containing:

- Exact hashes, material lookup names, source/design credits and distribution scope.
- Units, origin, facing, bounds/radius, footprint, height and intended placement/clearance.
- Per-mesh counts, hierarchy, face/mesh lighting modes, palette or RGB565 assumptions, and geoset/mip policy.
- Selected versus superseded textures, native-readback results, mapped preview provenance, and outstanding defects.
- Target engine/renderer and explicit test status, including untested collision, placement and multiplayer behavior.

The level workflow owns templates, thing placement, resource closure, collision settings and sector/portal validity. A visible fullbright crystal is not proof of a dynamic light. A model radius is not a complete collision contract. Door/lift geometry, path frames and COG state/synchronization need their own implementation and tests. Check the target level's actual LOD thresholds and camera distances; one model-data view does not establish far-distance behavior.

For release, test the installed resources in the actual engine and renderer, including intended placement and movement around the prop. Test connected clients for multiplayer claims. Mark unavailable tests pending rather than treating successful parsing or an offline render as a replacement.

## Evidence and credit boundary

The November 12 review compared the essentials and final-textures archives, independently parsed their final 3DO/OBJ geometry and checked selected texture hashes/pixel changes. Wren's saved records report 16 synthetic Blender-Sith checks on Linux, native export/import and RED readback. Those historical checks were not rerun here. The previews are labelled **rendered from model data, not in-game**; Wren described the separate v2.7 level as WIP and untested. The final-textures manifest says those textures are to be packed when v2.7 is built, not that the document itself updates the level.

Credit Andrei's direction and project context; Lily's prop reference designs and texture repaints; Tripo's generated source; Wren's conversion, reduction, fit decisions, mapped review and local crimson correction; and the Blender-Sith Agentic/RED tools used. These distinctions preserve useful collaboration evidence without implying a fresh engine pass or permission to redistribute private assets.

## Dot Lilitari: level and resource handoff

**Kestrel Relay v0.2**, built by **Lilitari on OpenAI Dot** for Andrei on November 10, 2026, supplies a separate level/material example. Dot used Python-driven RED operations, an older SED-MotS starter-room resize, stock JK resources, two generated wall/deck images, procedural interface art and the existing T800-toolchain MAT writer. This is distinct from regular ChatGPT Lily's visual advisory and Wren's prop conversion. The inspected Kestrel files do not establish Tripo or Blender-Sith use by Dot.

The retained sources are Kestrel's v0.2 release README and manifest, `RGB565_WORKFLOW.md`, final `26-kestrel-v02.jkl`, and `observed-runtime-tests.json`, alongside the separate November 10 RED Kestrel engineering handoff. The material notes identify the then-inspected RED source as `2b439e3f30f5b83bfdb13b98a3aef9e1cb8c613d` and identify the installed executable separately; source inspection alone does not prove that executable's build ancestry. These are dated workflow observations, not claims about missing features in today's forks.

**Check runtime allocation and dependencies after authoring.** Kestrel exposed an old export path that reduced its 212-template table to 23 entries and its material capacity to 12. Dot restored the required tables and allocation in the level data; a later, separate engineering change added resource-aware export. Structural validation had not established runtime completeness. For a new handoff:

- Record declared capacity separately from occupied records. The final Kestrel file has **512 material slots and 22 material rows**, not 512 authored materials.
- Compare pre/post template names, inheritance and dynamic actor/weapon/COG dependencies, including unplaced templates. Resolve required dependencies before accepting pruning; retaining every unused template is not a universal requirement.
- Preserve material indices and inspect export diagnostics and unresolved scopes. Recheck the authoritative final native file and installed runtime resources. Kestrel's hard-coded repair and capacity values are not a general-purpose fixer.

Probe the installed tool's actual operations before repeating a workaround. The historical RED agent rejected the saved starter-vertex resize, so SED-MotS supplied that scaffold; RED handled the later level authoring. The subsequent engineering handoff records bounded starter center/size support. It does not follow that current RED needs SED for this task, or that the untested GUI alternative was unavailable. Use `jk-mots-level-workflow` for the level implementation and its current tool checks.

**Validate the selected material contract and its bindings.** Kestrel's opaque RGB565 materials target accelerated rendering; its earlier paletted build is a separate edition. When inspecting indexed MATs, resolve the actual level CMP: the old inspection route could otherwise produce a grayscale fallback. That preview is not evidence of the intended indexed colors. RGB565 decoding does not require a CMP. Keep opaque RGB565, cutout/translucency and replacement-image alpha contracts separate.

Fresh read-only checks on November 12 found a **14-entry production GOB**, with every payload size/hash matching its release manifest, and **ten RGB565 MATs** with complete **256 × 256, four-mip** payloads. Two terminal materials have two cels; the other eight have one. No custom 3DO or QA entry is packaged. The source ZIP passed CRC checking and was hashed. These checks establish the inspected bytes and structure, not a new visual or game test.

For multi-cel materials, audit cel order, texture indices, complete mip payloads and decoded pixels, then separately verify the surface/COG bindings that choose the cel. Kestrel reused the existing one-cel writer and preserved its payloads while assembling multiple cels; its retained synthetic receipt reports byte-identity and asymmetric decoded-pixel checks. A valid two-cel file alone does not make a working terminal. Keep final editable/native sources, selected art, writer identity, bindings, hashes and rebuild receipts together, with QA fixtures outside the production package.

**Keep runtime claims tied to the mechanism tested.** Dot's retained OpenJKDF2 receipts report both relay orders passing 13/13 mission assertions, no-cache completion passing 11/11, and collision-driven actual-player traversal through 47 sectors. The mission tests supplied synthetic sender/source identities; the traversal used normal collision but did not activate the relays. Neither proves physical keyboard Use, a complete human combat/balance playthrough, save/reload or execution in the original retail binary. Those checks remain distinct. Route interaction work through `jk-mots-cog-workflow` and test actual reach, targeting, events and resulting cel/state behavior in the chosen engine.

Credit Dot Lilitari's level/material work and the reused tools separately. This case publishes portable methods and scoped measurements; it does not redistribute the private conversation, source bundle, artwork or stock assets.

## Red: reduce coplanar world-model faces without assuming a speedup

Red's November 20 **Sith Council V2 Candidate04** is a private derivative of
Lily's Candidate03. Its README and the recovered authored account report a
reduction from **185,329 to 131,244 world-model faces**, about **29.2%**, across
**142 of 150 models**. This is a native face-record reduction, not a measured FPS
gain. No level archive, source artwork or capture is distributed here.

The method merges adjacent triangles only when the shared edge uses matching
vertex and texture-vertex indices, face material, flags, geometry/light/texture
modes, extra light and face normal. The combined polygon must remain planar,
convex and within the chosen corner limit; this candidate used at most eight
corners. A visually similar texture or matching positions alone is insufficient.
Preserve seams, per-corner data, winding and deliberate boundaries. Reject an
ambiguous merge instead of silently resampling or changing metadata to make it
fit. Compare native records after writing, not just the authoring mesh.

The authored audit reports unchanged vertex sections and corner coverage,
unchanged non-model archive members, and per-material covered area agreeing to
a worst relative difference of `2.3e-12`. Those are Red's reported checks, not a
fresh reproduction in this workflow release. They also do not prove an identical
shaded image: the engine can interpolate corner lighting along different
diagonals after a merge. Test sharply varying light and texture gradients in the
actual target renderer before accepting the simplification.

Use a fixed camera, scene, renderer and settings for any performance comparison.
Report CPU/GPU/frame timing separately from file size and face count. Very small
curved-patch triangles may be better addressed in source tessellation, while
visibility/sector changes are separate level work with COG, resource and sky
dependencies. Hand those changes to the level/COG workflows instead of treating
a mesh optimizer as a complete level-performance fix.

Credit Lily's Candidate03 base and Red's merge method, audit and explicit
interpolation caveat, with Andrei's direction. The portable findings come from
Candidate04 `README.md` and Red's recovered November 20 account; the private
candidate's acceptance and gameplay/performance results remain separate.
