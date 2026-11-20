# Model compatibility: rig, native formats, and engine targets

> Copied into RED from [CodaLilitari/JK-MotS-Model-Workflow@ca011b8](https://github.com/CodaLilitari/JK-MotS-Model-Workflow/blob/ca011b84c4fa5e1f3755e1543be2c1c1d25063c9/skills/jk-mots-model-workflow/references/compatibility.md) (MIT; see [LICENSE](LICENSE.md)). Regenerate with `tools/knowledge/import_knowledge.py` rather than editing this copy.

Use this reference for player, NPC, creature, and weapon models in Jedi Knight: Dark Forces II and Mysteries of the Sith. A supported target is a complete combination of game executable, patch or source-port build, renderer, model, materials, animation resources, and integration route. The name “JK compatible” alone is insufficient.

For attributed community procedures and failure cases, read [character and animation methods](community-modeling-research.md): hierarchy and pivot placement, adapted keys, node-type blending, event markers, MP selection and separate POV hands.

## Choose a modeling method

Start by asking what the character needs to do differently. Offer the user the plausible methods below with their visual gains and cost. More complex rigging is useful when the design requires it; it is not automatically the better result.

| Method | Choose it for | What the user gains | Effort and tradeoffs |
| --- | --- | --- | --- |
| Texture reskin on an existing permitted rig | New costume, faction, color scheme, face treatment | Fast recognizable variation; existing motions remain useful | Low. Silhouette and articulation largely stay with the base model; obtain rights to distribute the base |
| New geometry on the stock numbered rig | A new humanoid or familiar creature with compatible motion | Original silhouette and surfaces while reusing suitable game animations | Moderate. Fit limbs and joints carefully; extreme proportions may look wrong under stock keys |
| Expanded stock rig with rigid child detail | Hair, armor, backpack, skirt panels, claws, segmented mechanical detail | More shape/detail without authoring a complete new animation library | Moderate. Reserve KEY indices, support complete KEY allocation, budget nodes, and accept rigid secondary motion |
| Custom KEY/PUP rig, as demonstrated by TDIR | Very small/large characters, radically different proportions, deliberate custom movement | Articulation authored for the design and action set | High. New animations, gameplay-role mappings, weapon aim/attachments, transitions, and distribution dependencies |
| Enhanced-engine model/material tier | Larger atlases, supported replacement materials, extra visible detail | Higher close-up fidelity on a stated engine build | Moderate to high. It still needs an appropriate rig and budgets; offer a separate original-engine version when useful |

These methods can be combined. For example, a generated skull can become new geometry on the stock head node, separately modeled teeth can share that node, and a deliberately reduced mane can use rigid children. AI-generated source geometry is a donor; selecting the final rig method is a separate decision.

For a first usable version of a conventional MP humanoid, offer **new geometry on the stock numbered rig plus a small number of rigid details**. For a dramatically different gait or body size, offer **a custom KEY/PUP package** alongside the compromise stock-motion version, explaining the extra animation work. For an NPC, begin with the matching creature's actual puppet rather than assuming the player skeleton.

## Three practical recipes

**Selectable MP armored character.** Preserve the chosen player rig's numbered body joints and weapon roles; fit original body meshes around those pivots. Add helmet equipment, hair, or a backpack as neutral children after the complete intended KEY domain. Author separate texture regions for face, torso, limbs, and accessories; create consistent LOD meshes. Generate local KEY allocation support if the expanded hierarchy needs it, register the new model, and test normal profile selection plus a second client for an interoperability claim.

**SP creature replacement.** Start with the intended NPC's native hierarchy and PUP, record the AI's action set, then choose either new geometry on that rig or a custom animation package. Put expressive silhouette features in geometry and shallow skin detail in baked color. Retain gameplay role mappings and required sound/model dependencies. Exercise the actual creature's idle, locomotion, attack, hit, death, and save/load behavior. This is model integration into existing content; level construction is outside this workflow.

**TDIR-style large fighter.** Design an explicit large-character skeleton and its matching KEY/PUP motions. Preserve or deliberately remap head/torso/weapon roles, author aim and attack transitions, and package every dependent resource. A custom rig can remain an asset-only original-engine mod when it fits that engine; an engine patch is a separate dependency decision. Build an economical 256-tile material lane first, then offer an enhanced texture tier if useful.

## Select the target before authoring

| Target | Authoring contract | Required proof |
| --- | --- | --- |
| Original JK / MotS, indexed materials | Native 3DO and palette-indexed MAT; animation and hierarchy remain within the intended executable's storage | Actual original executable; confirm the tested software or accelerated setting; preview and gameplay separately |
| Original JK / MotS, RGB565 materials | Native 16-bit MAT plus a functioning accelerated renderer | Confirm acceleration in the game's actual Display menu and render the exact files |
| Patched original executable | Exact patch, supported starting binary, and documented features | Record the complete binary/patch/wrapper chain; test networking if claimed |
| OpenJKDF2 / OpenMotS | Exact source revision and binary; optional enhanced material features stated explicitly | Runtime in that port build; retail results remain separate |

Indexed MATs can also be used with acceleration. [Same-name local material editions](same-name-material-editions.md) can preserve one character identity across separately installed indexed and RGB565 packages; this is a manual edition choice, not automatic in-GOB fallback.

RGB565 is not inherently an unofficial-patch-only MAT format. Conversely, a successful modern PNG replacement does not show that an original executable can load that PNG. JKE's package-level patch requirement is real, but it is not evidence that every detailed character needs a larger per-mesh limit. See [the historical comparison](jke-tdir-shred-research.md).

### Select the KEY export target

The [Blender-Sith 1.1.0 integration](blender-sith-fork.md#110-integration)
adds the upstream target-aware exporter. Use the verified API's explicit module
and game target; this recipe does not imply that the shared installation has
already changed:

```python
from sith.key import keyExporter

keyExporter.exportKey(root, bpy.context.scene, str(key_output_path), target='JKDF2')
# For Mysteries of the Sith, pass target='MOTS' instead.
```

The API default is `IJIM`, not JK. Both `JKDF2` and `MOTS` validate at most 64
joints, 8 markers and 31-character resource/joint names; IJIM uses different
marker/name limits. `CUSTOM` removes game-specific limits but retains format
safety checks. It is not a certificate that an original or patched engine can
use the result. A target-specific failure requires a deliberate animation/asset
change or a separately proven engine contract, not retrying under `CUSTOM` to
obtain a green export.

These checks complement the numbered rig and complete KEY/PUP allocation rules
below. They neither retarget a model nor prove reachable animation completeness.
3DO version, KEY target and MAT encoding are separate choices: retain the 2.1
3DO lane and the selected indexed/RGB565 material contract for JK/MotS. The 3DO
writer can warn about overlong legacy names while still writing them; review
warnings and enforce the intended game's resource limits independently.

Supported sparse axes, fractional key times, held segments and Bezier curves are
sampled into native KEY entries/differentials. Preserve unkeyed object-channel
defaults and evaluate the serialized result at its actual frame times and between
entries; added samples mean entry number is not frame number. Check explicit
nonzero-to-zero returns, wrap boundaries, long Euler turns and markers. Record
scene FPS and any deliberate `fps` override. Unsupported animation layers,
drivers, NLA or constraints require an explicitly prepared/baked animation;
failure is not permission to silently omit them. Quaternion orientations alone
cannot recover an unrecorded full revolution.

Compare restored native pivots, weapon attachment matrices, mesh motion and
KEY/PUP role mappings, then test the exact game and transition/blending cases.
Synthetic serialization/interpolation checks do not establish gameplay.

## Preserve the numbered animation contract

JK/MotS characters are assembled from rigid hierarchy parts. A successful Blender deformation preview does not mean a modern weighted skeleton transfers into native KEY/3DO animation. Decide which geometry moves with each native node, where its pivot is, and whether an extra detail is a rigid child or needs an independently authored animation track.

Before retopology, extract a private rig contract from the owner's intended installation or licensed author source:

1. Record ordered hierarchy indices, names, parent/child/sibling links, node types, mesh references, local positions, rotations, and pivots.
2. Record PUP mappings for head, neck, torso, weapon firing, and aiming. These are role mappings to node indices; do not infer them from visual names alone.
3. Resolve every reachable KEY across PUP modes and `BasedOn` inheritance. Add keys invoked by COGs, weapon scripts, substitutions, and game code explicitly.
4. Hash the corpus and preserve the base rig before fitting geometry. Recheck when a weapon, mod, puppet, or animation changes.

PUP chooses KEYs by action and mode and associates gameplay roles with hierarchy nodes. This makes puppet compatibility more than a list of animation filenames. [Massassi PUP specification](https://www.massassi.net/jkspecs/pup.htm)

### Rule A: reserve animation-addressed indices

An 18-node base rig exposes indices 0–17. It does not follow that index 18 is free. In the studied JK player corpus, `kyfall.key`, `kyidle1b.key`, `kyidle2b.key`, and `kysbsabr.key` declare 19 joints and address node 18. Three of those names occur with that extension in the studied MotS corpus; inspect the actual installation instead of hard-coding either list.

For rigid detail that must receive no independent KEY transform:

```text
first_detail_index = max(base_node_count, largest_declared_KEY_JOINTS)
```

Reserve the entire declared allocation, including unused slots in sparse KEYs. Reject a `NODE` outside its declared allocation. Intermediate reserved hierarchy slots must be invisible leaves; otherwise descendants can inherit the unwanted animation. Put detail at or beyond the computed boundary, with a neutral local transform under its intended base node. Verify the exported matrices rather than trusting a metadata flag.

This rule was motivated by actual model loads that succeeded while decorative geometry received the wrong saber-related transforms. The maintained [KEY-domain guard on GitHub](https://github.com/CodaLilitari/OpenJKDF2-MotS-Fixes/tree/main/scripts/model-workarounds/key-domain-guard), also available [on GitLab](https://gitlab.com/codalilitari/OpenJKDF2-MotS-Fixes/-/tree/main/scripts/model-workarounds/key-domain-guard), provides PUP-reference scanning, reserved-leaf planning, summary checks, and synthetic regression cases. It complements this workflow's smaller allocation inspector. Its JSON validator trusts supplied mesh counts and neutral-transform declarations; it is not a complete 3DO validator or runtime certificate.

### Rule B: allocate KEY storage for the complete hierarchy

Avoiding occupied animation indices does not enlarge a KEY's storage. For a 24-node model, an otherwise compatible KEY declaring only 18 or 19 joints can still leave the original runtime accessing a short table. The inspected official JK 1.01 code reads a node entry before its later range guard; modern source-port protection cannot be projected backward onto that executable.

For the retail-compatible local-copy approach, set each applicable KEY's `JOINTS` allocation to at least the exported hierarchy count while retaining all track data, markers, timing, and other bytes. The keyword `NODES` counts the explicit animation blocks; changing it is not the allocation fix. Do not add dummy animation blocks or renumber existing tracks as an incidental “repair.” Derive the changed copies from the owner's installation and place them in the mod overlay. Keep original archives unchanged.

A 24-node T-800 implementation uses this allocation padding and had bounded indexed-material runtime checks in original JK 1.01 and MotS. That precedent does not validate every custom KEY/PUP combination. Reachable-corpus completeness and actual runtime remain gates. Engine references: [KEY reader](https://github.com/shinyquagsire23/OpenJKDF2/blob/0dbd1ea8bb2211c158f6af928b5e4fb14dfa0dac/src/Engine/rdKeyframe.c), [puppet implementation](https://github.com/shinyquagsire23/OpenJKDF2/blob/0dbd1ea8bb2211c158f6af928b5e4fb14dfa0dac/src/Engine/rdPuppet.c).

### Custom proportions and custom rigs

Keep familiar numbered joints when their movement is appropriate. Change the silhouette through geometry and rigid detail before changing animation semantics. A long mane can follow the head; belt, skirt panels, equipment, or mechanical trim can follow appropriate body parts. This gives rigid motion, not cloth, skin deformation, or physics.

If stock pivots cannot support the design, author an explicit custom KEY/PUP package and test aiming, weapon attachment, locomotion, transitions, death, and respawn. TDIR's separate size branches are a precedent for this route. A profile edit or `SetThingModel` only selects geometry; it does not retarget unrelated animation indices.

## Budget each mesh and each animation domain

| Domain | Conservative working policy | Evidence and qualification |
| --- | --- | --- |
| Geometric vertices per mesh | Stay below 512 with room for exporter splits | 512 parser bound in the inspected JK 1.01/MotS executables and studied source port |
| Texture vertices per mesh | Stay below 768 with room for UV seams | 768 parser bound; UV count can grow independently of position count |
| Faces per mesh | A 450–500-triangle target is a conservative working policy; count emitted native face records separately | 512 face-record parser bound; Shred18 reported a 500-triangle working limit in his toolchain. A valid native quad uses one record, with triangle equivalent two |
| Puppet hierarchy | Keep at most 64 slots, with room for additions | Studied track cache has 64 entries; dynamic model allocation does not remove animation storage limits |
| Native LODs | One to four geosets with consistent mesh ordering | Validate all geosets and actual transitions; importing only geoset 0 is insufficient |
| Total model cost | Choose a measured project budget | No universal safe total-triangle count follows from the per-mesh limit |

The [Sith Engine Specs limits page](https://jksith.wikidot.com/limits) gives the 512/768/512 per-mesh values. [Shred18's 8 August 2005 forum reply](https://forums.massassi.net/Editing_Forums/Jedi_Knight_and_Mysteries_of_the_Sith_Editing_Forum/thread_34814_page_1.html) is valuable practical evidence, not an alternative binary parser measurement. Leave headroom: a model close to the face cap may still overflow UV or hierarchy storage after splitting.

For generated mixed topology, keep a triangle/quad/n-gon histogram and triangle equivalent alongside the native face count. A numeric pass does not validate folded or concave polygons. Check planarity, convexity, winding, material/UV continuity and the intended triangulation before retaining or merging faces; see [the measured Tripo count examples](tripo-to-jk.md#count-the-download-not-the-quad-label).

Partition by movement first, then by material and budget. Splitting a mesh only to meet a number can create seams, extra hierarchy costs, or disconnected moving surfaces. Keep animation joints and stable pivots fixed when reducing LODs. Reduce invisible/internal faces and subpixel strands before sacrificing silhouette.

## Native export checks

Check the emitted files independently of the authoring scene:

- All material references resolve with the intended case-insensitive game lookup and no ambiguous duplicates.
- All geosets have expected mesh count/order; hierarchy mesh references are valid at every detail level.
- Counts match the actual arrays. Indices, UV indices, finite coordinates, winding, face normals, vertex normals, and nondegenerate triangles are valid.
- Parent links are acyclic and agree with child/sibling traversal; reserved slots have no children or visible mesh.
- Mesh radii cover their vertices, and model bounds cover the intended geometry. Enlarging the visible body is not a gameplay collision or reach change by itself.
- Texture/geometry/lighting modes are intentional at both mesh and face level. A higher face mode need not override a lower mesh mode in the selected renderer.
- Surface seams remain closed or deliberately overlapped across actual motion. Watertightness is judged per intended solid part, not imposed blindly on every decorative surface.

The [3DO format specification](https://www.massassi.net/jkspecs/3do.htm) defines separate geometric and texture vertices, native hierarchy fields, and geoset ordering. Treat format descriptions as a starting point and verify behavior in the chosen engine.

## Mechanical parts and hands

A piston shell made from complementary faces assigned to two independently moving nodes can tear even when the neutral pose looks correct. Place the shell on one motion owner and design its connection to the other part across the actual animation translation/rotation domain. For a known linear translation interval, checking endpoint inclusion inside a convex guide can establish containment for that interval; sampled pose screenshots alone cannot. Maintain an explicit scope for custom keys and blended motions outside the measured domain.

The external weapon model may contain its own flesh hand. Replacing the body hand does not remove that geometry, and first-person weapon hands are another separate model set. A body-hand cover can hide it in some views but introduce bulk or obscure a held object. Compare actual rendered faces at multiple poses; a convex hull around an open hand is not proof of visual concealment. Shared weapon edits, per-skin runtime changes, and a deliberately partial cosmetic solution have different scopes. State the chosen one.

## Materials and effects that alter the compatibility claim

Keep an indexed fallback tied to the intended game palette; JK and MotS palette conversions may differ. A custom map palette can change appearance. Keep RGB565 and enhanced replacement paths separately named and tested. Model display depth, MAT bit depth, and menu-preview buffer depth are different things.

Do not blindly multiply UV coordinates when raising atlas resolution. The studied native OpenGL path clamps its UV normalization dimension to 256, and a modern Shred 2048-square atlas uses a 256-unit UV period. Inspect the actual renderer and author a diagnostic grid before changing UV scale. An atlas repack still requires its own island remapping.

Fullbright eyes are a material/face choice. Scalar dynamic lighting is not colored illumination, a body-attached light is not necessarily head-joint tracking, and a bloom preview is not retail proof. Treat glow as an optional separately tested dependency after the model works.

Continue with [runtime and packaging](runtime-and-packaging.md) before labeling an export playable.
