# Conversion reliability: lessons and release checks

A playable conversion needs agreement between source intent, saved native resources and observed game behavior.
A loaded archive, an attractive screenshot or a successful geometry check establishes only part of that result.
Use this guide with the [JO/JA importer](jo_ja_import.md), [strict JKL validation](headless_validation.md),
[model comparison](model_collision_compare.md) and [game testing](game_testing.md).

## Failures to catch before release

These cases come from repairing Academy V3, Helm's Deep V2 and the related Nar Shaddaa conversions. They describe
specific observations, not defects attributed to every conversion or engine version.

| Symptom | Established cause | Prevention |
| --- | --- | --- |
| A textured duel floor appears black | Four corner luxels were black while the source lightmap interior was bright. The texture was present. | Compare interior light samples against emitted interpolation; retain geometry needed to represent the field. |
| Grass and leaves stop the player | Decorative and structural faces shared face-colliding Things. | Separate reviewed decorative shaders from solid supports; walk both classes. |
| A stair looks correct but cannot be climbed | Some ramps missed their landings; native face-query ordering could also associate a hit face with the wrong distance. | Audit both landings, compare collision replicas, and walk uphill and downhill. |
| A repair loses templates after import | The authored table exceeded its declared capacity. Compatibility import could discard excess rows. | Check the exact packaged JKL with strict raw preflight before opening and resaving it. |
| Banners glow or halos become dotted discs | An additive source shader became emissive or repeated binary coverage. | Preserve shadeable cloth and choose a target-native light treatment. |
| Glass becomes a noisy pattern | A reflection shader became isolated opaque texels on model panes. | Compare stock glass in its actual geometry context, both pane sides, overlapping layers and every mip. |
| A pickup sits below a floor | Its origin or visible mesh substituted for effective contact geometry and support. | Resolve templates and collision bounds; test support, activation and respawn. |
| A replacement floor has the wrong sound | Contact audio follows the native support's flags and sound class. | Retain the support category and separately listen to footsteps and landings. |

## Preserve source intent

Capture the source BSP hash, conversion recipe, material hashes, target game and authored archive hash. Keep the
unmodified source and preceding playable build for matched controls. Surface indices and generated model filenames
have meaning only within their captured revision.

Draw shaders, brush volume shaders, brush sides and native collision flags answer different questions. A leaf-like
filename, transparent MAT or missing same-name brush does not prove a surface should be passable. Check world
model 0 separately from moving submodels: a solid volume can use a different shader from its visible sides.
Use `jo-ja-inspect` and its brush inspector to locate evidence, then make an explicit authoring decision.

### Reviewed decorative collision

For a model-scene recipe whose architecture should collide, add exact source shader names. Combine this fragment
with the usual hash-bound `source`, target, material mappings and other import settings:

```json
{
  "geometryMode": "model-scene",
  "renderMeshCollision": true,
  "nonCollidingShaders": ["textures/example/leaves", "textures/example/grass"]
}
```

CLI and MCP preparation keep those triangles visible but pack them into separate `collide=0` Things. Structural
chunks keep collision and contact-sound flags, even if they share a target MAT with the decorative chunks. Splitting
precedes mesh optimization and consumes the existing model budget. Inspect the optional `generatedCollisionSelection`
report: its selected models identify the generated Thing, collision setting, source surfaces and shader names.
Its `generatedOutputSha256` binds that report to the generator archive **before authored reimport overrides**.
The ordinary `outputSha256` identifies the final archive. An authored collision override can legitimately change
the final result; inspect the final JKL rather than treating the generator report as a final-world assertion.

A nonempty selection requires `model-scene` and `renderMeshCollision: true`, for JK or MotS. Unknown, duplicate,
skipped or non-emitting selections refuse. Empty and omitted lists retain earlier recipe bytes and hashes.
The GUI's whole-model collision checkbox is unchanged; this selective option uses the CLI/MCP recipe. It does not
retrofit an authored episode. Use [reviewed reimport overrides](jo_ja_import.md#reimporting-an-authored-episode)
when retaining gameplay edits.

Academy's repair separated 2,419 decorative faces while retaining solid trunks and supports. The reusable rule is
explicit separation and verification, not that count or a built-in list of Academy shader names. Walk through a
plant patch and into a known solid trunk as paired controls.

## Lighting and materials

### Sample the interior

Correct corner samples do not prove correct lighting between corners. Academy's three duel pads had the correct
floor MAT, but each large quad sampled four black corners of a 47-by-47 source lightmap region. The map-specific
repair used a 13-by-13 sample grid and 144 coplanar quads per pad, retaining the original plane, UV repeat and art.
Its measured scalar light error was approximately 0.014 RMS on a 0–1 scale.

That grid is an observed repair, not a universal tessellation setting. Measure interior error, preserve the native
triangle-fan interpretation, and keep shared edges, normals and model/Thing limits valid. Check stored intensities
and a fixed-camera game capture. Replacing the texture or raising global ambient would change unrelated lighting.
The importer's `patchSubdivisions` controls quadratic patches; it does not subdivide ordinary planar floors to
recover interior lightmap detail.

Use [first-lightmap inspection](jo_ja_import.md#inspecting-first-lightmap-fidelity) to measure sampled RGB
interpolation error and the color lost when a corner becomes scalar. The report identifies the source triangle
and sample position so an author can inspect the affected surface.

### Match source effects to target rendering

Establish whether brightness belongs to an emitter, additive overlay, ordinary cloth or baked illumination.
Helm's standards needed normal shadeable cloth. Its original carpet really was a single flat red color, so the
woven replacement was an intentional artistic improvement, not recovered missing detail. Record that distinction.

For lights, compare a small physical emissive fixture or translucent crystal with a billboard approximation.
Repeated binary coverage can remain visible as a grid. Native translucency does not reproduce every JA shader
stage, bloom or reflection. Preserve nearby baked illumination independently from a rejected halo.

For glass, inspect the stock control's flags, MAT, geometry context and number of overlapping layers. A translucent
adjoin is not automatically equivalent to a destructible model pane. A restrained rim can preserve clear visibility
when a reflection stage cannot be represented well. Keep damage scripts, hidden collision hulls, health, Thing IDs
and break sounds separate from appearance changes.

Foliage outlines need controlled tests too. In Academy OpenJKDF2 captures, changing only the sky color changed
the pale outline; nearest filtering removed it. That implicated filtered cutout compositing, not white source
pixels. The release retained the artwork and reported the unresolved filtered outline. Recoloring, oversized MATs
or an unqualified alpha format are not established fixes. Compare filtering, mips and backgrounds on the exact renderer.

## Stair support and native collision order

Audit the lower landing, first riser, regular treads, final nosing and upper landing. The final landing can be
higher than the last regular tread. Extend an approach only over verified support; clips over empty terrain may
represent intentional barriers or omitted gameplay. Preserve clearances, walls and openings. The
[player-clip guide](jo_ja_import.md#choosing-a-player-clip-treatment) distinguishes standable hulls from movement
barriers and their projectile consequences.

Native collision can depend on physical face order. In the observed failure, a later polygon's plane test changed
the output distance even when its polygon missed; the earlier hit face remained selected. This can cross mesh
boundaries. See the pinned [sphere/face, mesh and Thing intersection path](https://github.com/shinyquagsire23/OpenJKDF2/blob/545eace7ec5b0b35e18768adc6cd22653fc440ca/src/Engine/sithIntersect.c).
This explains the diagnosed behavior on the tested engine; it does not qualify every retail binary.

Record query origin, normalized direction, radius, maximum distance, returned mesh/face, stored normal and reported
distance. For a point ray, independently check the selected plane and polygon. For a sphere sweep, account for its
radius and edge/vertex contacts instead of treating every result as a point-plane hit. Retain old and repaired traces.

A tested repair may reorder faces or use a separate non-rendering collider. Do not sort arbitrary models by height
as a universal fix: direction, mesh boundaries, nearby solids and broadphase bounds still matter. Keep the visible
model when collision repartitioning would disturb its rendering or lighting.

```sh
red-agent model-collision-compare original.3do collision-replica.3do
```

For exact face preservation, require `sameFaceMultiset: true` and the expected nonzero face count. The command
compares ordered corner coordinates, stored normals and duplicate multiplicities across every physical first-geoset
mesh, deliberately retaining the first corner. `sameFaceSequence` and `sameMeshFaceCounts` separately expose
ordering and partition changes. Completion with differences still exits successfully; inspect JSON, not only exit status.

The tool matched all 15,144 faces of five Helm collision replicas and all 7,810 faces of 23 Academy reordering pairs
used for qualification. It does not compare placed Thing transforms, radius, sound categories, face flags, UVs,
lighting or other LODs. It does not certify target loader limits or runtime traversal. Both files are captured and
hashed, never edited. See the [complete comparison contract](model_collision_compare.md).

## Check the package before compatibility import

Inspect the shipped archive and extract the exact JKL member into a fresh audit location:

```sh
red-agent archive scene.gob
red-agent extract scene.gob jkl/scene.jkl audit/scene.jkl
red-agent validate audit/scene.jkl --strict
```

Create `audit` first and use the member spelling returned by `archive`. Strict validation checks raw
MATERIALS/GEORESOURCE framing and present TEMPLATES/MODELS/THINGS capacities before ordinary import can discard
overflow rows. A rejected Helm candidate declared 248 template slots for 249 rows; import success did not prove
the table survived.

Capacity differs from exact count. Empty, exactly filled and spare-capacity entity tables are valid under this
profile. Thing slots can be sparse or reordered but must fit capacity and not repeat. Model row labels are not
Thing slot addresses. Keep each template row on one physical line for strict validation and compatibility import.
The [strict profile](headless_validation.md#strict-raw-jkl-framing) defines admission and budgets. Check runtime
resource availability separately.
Inspect `valid` and scan completeness as well as process exit status.

Compare parent and candidate archive member names, sizes and hashes. Every added, changed or deleted member must
correspond to an intended repair. Check model limits, template inheritance and projectile resources, stock pickup
equivalents, Force Throw eligibility, item placement, ambience, sound classes and preload behavior. Preserve existing
Thing IDs and script bindings unless their changes were reviewed.

Use the [paired-edition verifier](../tools/engine-capture/README.md#paired-retail-texture-editions) for 8-bit/16-bit
packages. Reserve runtime resources for players and effects, then exercise a real join. Zero spare material slots
are not a framing error; a capacity pass is not multiplayer proof. Rebuild from the captured parent and intended
overlay, then require the approved final archive hash.

## Native acceptance evidence

Use isolated staging with immutable inputs and recorded engine, renderer, settings, recipe and archive hashes.
Create new staged destinations rather than overwriting a path that might hard-link to a retained parent. The
[capture runner](../tools/engine-capture/README.md) already uses owned staging and input identity checks. Keep any
diagnostic instrumentation outside the level package and identify what it changes.

Place the actor once, allow gravity to settle and supply ordinary movement input. Do not repeatedly relocate it
or disable collision. Record actual input consumption and post-physics position, health, floor attachment and
support identity. Allow enough time for the observed climbing speed and require several grounded samples inside
the measured landing. Test both directions, the intended multiplayer stepping and the actual inconsistent-physics setting.

Pair repaired routes with working stairs, passable plants, solid trunks, pad diagonals, corner fixtures and adjacent
geometry. A downward support survey is not a walking test, and a camera at a landing does not prove the actor reached
it. Matched original/repaired walks distinguish a new seam problem from an unchanged solid fixture.

The Academy/Helm qualification used 72 movement checks across six archive variants in an OpenJKDF2 OpenGL local
multiplayer host. All 14 Helm flights ran both directions in JKDF2 16-bit; other editions exercised representative
difficult flights. Academy had seven physically walked stair routes plus a broader support survey. Those results
do not establish every route, retail executables, software rendering, remote peer joining or audible playback.
Bind each acceptance claim to its actual observation.

## Tool coverage and attribution

The portable importer owns collision grouping; `inspectJklStructure` owns bounded raw preflight;
`compareModelCollision` owns strict model comparison. CLI and MCP share those implementations. Regression fixtures
are project-owned synthetic BSP/JKL/3DO data, not copies of the repaired levels.

Current source includes [stock pickup placement and projectile checks](jo_ja_import.md#stock-multiplayer-pickups-and-projectile-checks),
[standable clip footsteps](jo_ja_import.md#choosing-a-player-clip-treatment),
[placed model collision inspection](jo_ja_import.md#inspecting-source-and-target-collision) and
[explicit pickup support checks](headless_validation.md#explicit-authored-collision-support).
For audio, see [packaging sector ambience](package_authoring.md) and [authored sound preloads](jkl_sound_preloads.md).
Check the schema or release notes for the executable used to qualify a package.

Original Academy V3 author: Virtue. Original Helm's Deep V2 author: NaThan. Lilivane authored those conversions.
Nar Shaddaa's source is by Raven Software/LucasArts; retain its original JK conversion-team credits from the upstream
release. Lilivane contributed its MotS adaptation and train fix. Lilitari performed the described repairs and tool integration.
The wider workflow incorporates Red's and Coda's Council optimization and native qualification work, credited
in the [conversion guide](jo_ja_import.md#engineering-sources).
