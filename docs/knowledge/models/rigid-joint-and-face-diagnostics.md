# Diagnose rigid joints and face registration

> Copied into RED from [CodaLilitari/JK-MotS-Model-Workflow@ca011b8](https://github.com/CodaLilitari/JK-MotS-Model-Workflow/blob/ca011b84c4fa5e1f3755e1543be2c1c1d25063c9/skills/jk-mots-model-workflow/references/rigid-joint-and-face-diagnostics.md) (MIT; see [LICENSE](LICENSE.md)). Regenerate with `tools/knowledge/import_knowledge.py` rather than editing this copy.

Use these checks when a character looks plausible at rest but develops a second
shoulder, a joint step, or a different face in motion. They come from Red's
November 16–18, 2026 Darth Lilitari reviews and R9–R9.3 candidates. These were
private review artifacts, not published model releases. This reference shares
the methods and bounded measurements, not their models, textures or captures.

## Identify the rigid part before changing its texture

Preserve the failing model, KEY, exact timestamp, camera and renderer settings.
Give adjacent rigid parts contrasting solid colors in a diagnostic copy and
inspect the same pose from the failing angle. A surface that stays with the
torso while the arm advances is an ownership problem even if its fishnet or
shading makes it look like an extra sleeve. A texture repaint cannot change which
joint moves those faces.

Red used the hands-together `kystand1` pose to diagnose R9.2's "ghost shoulder."
The authored report identifies three stationary torso contributions: the outer
shoulder left inside an offset cut, inner sleeve faces excluded by a height
threshold, and a tall torso cap. The saved R9.3 segmentation and assembly scripts
corroborate the changed ownership rule and shallow-cap construction. This review
did not rerun the animation or independently judge the saved before/after views.

An actionable repair sequence is:

1. Place the shoulder boundary relative to the actual joint pivot and costume
   surface, then assign the moving shoulder and sleeve to the arm. Recheck the
   inner armpit as well as the visible outside. Do not copy a world-height cutoff
   from a differently proportioned model.
2. Design the stationary cap to remain inside the intended posed silhouette.
   A cap that closes a hole at rest may still become a visible extra shoulder.
   R9.3 used a shallow lid whose maximum height was 0.35 of the cut radius;
   that is a case-specific setting, not a general anatomical ratio.
3. Sample cap color from a surface with the same intended material and exposure.
   Red selected fishnet over skin on the upper back, avoiding glove leather,
   bodice pixels and the generated armpit shadow. Inspect the actual baked cap
   and its mip chain after transfer.
4. Recheck the original failure and distinct poses: arm-forward, gun, side/back,
   saber-ready, run and crouch. Preserve a matched before/after camera. Record
   any remaining smooth patch, dark corner, gap or intersection rather than
   declaring every pose repaired from one favorable view.

The R9.3 source uses a shoulder plane through its recorded pivot and starts the
torso-shell arm assignment at source z=0.70, replacing 0.735. Its earlier boundary
was reported as approximately 1 cm outside that pivot. These source-coordinate
values explain this particular correction; fit a new character's actual joints
instead of treating them as reusable engine units.

## Centre a cut and aim its cap separately

For elbows, wrists, knees and ankles, distinguish the centre of the cut loop,
the intended rotation centre, and the direction in which the cap should close.
Red's R9.2 notes describe caps aimed from an off-centre pivot toward the average
cut vertex. That vector lay nearly in the cut plane and produced steps in bent
poses. The repair centred the relevant pivots on their cuts and built the cap
along the limb toward the neighboring piece, checking that the child edge turned
over a closed surface.

The authored measurements were about 5 mm at elbows, 3 mm at wrists, 8 mm at
knees and 7 mm at ankles for a character described as 1.82 m tall. They are not
new measurements in this workflow release. The useful test is the relationship
between cut, pivot, cap and child motion, not those particular distances. A
shoulder ball/socket lid and a centred hinge cap need different construction.
Do not move a native pivot in an animation master merely to hide a seam without
checking the hierarchy, attachment and KEY consequences.

## Preserve proportions with an explicit animation contract

R9 rebuilt the body from front/back Tripo Smart Mesh references. Red reports an
11,952-face quad-oriented source and a 4K texture, then planar rigid slicing,
whole-part rotations and separate game-atlas baking. Unlike earlier skinning
onto Kyle, the source was not stretched to his limb lengths. Static adapter nodes
between the preserved Kyle slots and their parents carried different lengths,
while the existing KEY tracks continued to address their original indices.

This is a deliberate adapted hierarchy, not automatic retargeting. Shorter arms
change where a stock chin-touch, two-handed grip and gun pose land. The R9 notes
retain those reach differences. Reserved indices used by other KEY sets were
kept as empty fillers and all 92 packaged KEYs declared `JOINTS 64`; these are
reported package contracts, not a guarantee about an arbitrary third-party KEY.
Check every intended PUP/KEY corpus, pose and carried-weapon combination. Use
[the native binding audit](blender-sith-fork.md#120-native-pivots-and-binding-audit)
for structural diagnostics, followed by movement and reach checks.

## Count native faces after cuts, caps and atlas work

Preserve quads when planarity, corner UVs and other face data permit it. Cutting,
cap insertion, reduction and atlas seams can change polygon types. Do not label
the finished model with the generator's requested quad or triangle budget.

A fresh static parse of R9.3 `dlil.3do`, SHA-256
`064d5a7f9ac436d5fc55cb3eb09d47b6ad583cef6747517a41559ab046eea54a`, checked
declared face corner counts against the stored vertex/UV pairs. Each of its four
geosets contains:

| Native quantity, per geoset | Count |
|---|---:|
| Triangles | 3,597 |
| Quads | 9,863 |
| Other polygons | 94 |
| Total native faces | 13,554 |
| Triangle equivalents, sum of `corners - 2` | 23,692 |

The 1.2.0 companion's native inspection worker reproduced these counts from
the same file using the released Windows add-on in background Blender. It
also reported 38 meshes in each geoset, 64 hierarchy nodes and zero faces
with fewer than three corners. This is a worked use of `inspect_3do` with a
mixed-polygon model.

The 94 other polygons comprise 81 pentagons, four 11-corner faces and nine
12-corner faces. Matching counts across geosets do not establish a useful
distance-reduction policy. Red reports identical full-detail levels; do not
present four geosets as four optimized LODs. The retained validator reports 64
nodes, 38 meshes and maxima of 497 vertices, 751 texture vertices and 455 faces
per mesh. Those maxima are attributed validator results, separate from the fresh
face recount and from a universal runtime-performance claim.

## Register face paint before resculpting

When the face drifts from a reference, match camera/framing and normalize by a
stable feature such as pupil spacing. Compare reference, intended portrait,
flat albedo render and clay render from the same camera. Measure eye width,
opening height, iris size, nose width and base, and mouth width. Inspect whether
the painted eyelid follows the sculpted lid; changing the sculpt can worsen a
paint-registration error.

In the November 16 survey, Red's hand measurements were fractions of pupil
spacing, estimated to about 0.01. The reference eye width was 0.46 and its
painted mesh render 0.38, while the clay opening was about 0.49. The reference
nose width was 0.50 and its painted mesh render 0.71. This was evidence of paint
and geometry disagreement in that checkpoint, not a reason to universally widen
eyes or narrow noses. Red explicitly had not measured the later R4/R5 files.

Align paint feature by feature, compare it with the intended sculpt, remove only
unwanted baked lighting, and repeat the matched views before a further geometry
edit. Preserve the author's identity landmarks and the actual reference. Then
test palette/renderer changes in the game: an albedo match in Blender does not
establish the same result under indexed lighting or a different camera.

## Evidence and credit

The authored sources are `README_R9.md`, `README_R9.2.md`, R9.3 `README.md`,
`seg9.py`, `asm9.py`, `validation_r93.json` and
`SURVEY_2026-11-16.md`. Their text and the relevant script logic were inspected;
the native face recount is a separate November 20 static check. Red reports
OpenJKDF2 OpenGL/software captures and bounded original JK 1.01/Wine checks;
those executions were not repeated for this workflow update. They do not imply
every frame, a remote multiplayer peer, or final artistic approval. Red's notes
explicitly say Lily had not reviewed R9–R9.3.

Credit Red/Claude for these diagnoses, reconstruction and authored tests; Andrei
for direction, references and identifying remaining defects; Tripo for generated
sources; Lily for the earlier portrait/R4/R5 contributions, not R9 approval or
R9 costume paint. The R9 notes credit the Mandalorian method and Biddy's adapter
rig, and the Andrei/Coda modelling workflow and Blender-Sith tooling. Preserve
these distinctions rather than attributing the entire later model to one prior
contributor.
