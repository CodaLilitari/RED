# JO/JA hybrid geometry

## Purpose

The model scene importer encloses every static model in one sector. Retail traverses that sector's object list even
when most of the architecture lies behind another room. Large conversions also consume the shared Thing pool. The
hybrid mode uses native sector boundaries and portals, promotes matching planar architecture to world surfaces,
and retains curved and interior detail as models. It does not simplify away authored detail.

## Bounded source partitions

A visibility leaf is not necessarily empty: static detail and collision brushes can cross it. Creating one Sith
sector per source leaf also needlessly approaches the classic per-view sector limit. Keep at most the requested
number of convex cells (default 64, maximum 96). Prefer useful axial partition planes supplied by rendered planar
triangles on verified finite world brushes. Every supporting triangle must lie on a matching brush side, inside
all the other sides, and use that side's material. Both materials must declare solid, opaque contents without the
translucent bit. A deterministic score balances the workload separated by a cut, its covered interface area and
eligible native triangles against the cost of triangles crossing the cut. It gives a smaller side's workload
12% base credit plus up to 88% for covered area, adds 0.35 per eligible native triangle and subtracts 3.5 per
predicted crossing. These are heuristic triangle-work units, not frame-time measurements. The selector stops when
no candidate has a positive score; the sector setting is a ceiling, not a required count. Large walls must not be
sliced repeatedly merely because another large floor shares their height.

If there is no useful interior architectural cut, partition at source BSP nodes using source geometry workload.
A certified outer floor alone must not erase the source room graph. Branches with only one nonnegative-cluster
child may tighten a cell only after proving that no admitted source artwork lies beyond the proposed boundary.
Keep disconnected components and detail in negative visibility leaves. Structural cells can overlap several
source leaves; their leaf lists are conservative metadata, not an exact visibility or collision transfer.
Player starts and original entity text remain available; this operation does not implement teleporters or portal cameras.

Coarse cells may contain solid source detail. Such detail remains in colliding models when rendered-mesh collision
is requested. Their presence never justifies treating the whole cell as a source collision volume. Portal openings
come from the intersection of adjacent cell faces and the source tree on both sides. Clipped cross-sections of
finite opaque static brushes close the aperture even where a detail collider has no rendered face on that plane;
brushes must have positive thickness. A one-sided draw face can select the partition plane within the source
precision tolerance. Where its brush does not intersect that plane, the shared aperture stays open and the face
remains model detail. Entirely open source subtrees do not introduce artificial visibility-leaf seams.
Visibility rows are diagnostic data, not adjacency. Preserve exact opposite portal windings and the solid remainder
of each interface. A compiled nonnegative visibility leaf does not certify that its entire interior is walkable.

Known masked target MATs are refused in hybrid mode. Compiled solid/opaque source contents alone do not establish
the behavior of alpha-test, blending, two-sided or deforming shader stages. Model scene continues to accept the
existing indexed MAT mapping; a faithful hybrid conversion of transparent artwork needs a richer shader contract.

This deliberately avoids globally subtracting every source brush into separate sectors. That approach can multiply
sectors and create tiny seam fragments. A brush-side draw-surface index alone is not geometric proof: finite plane
bounds and actual triangle support are required. Source shader stages and invisible interior collision still need
separate treatment; compiled contents flags do not describe every rendering effect.

## Fidelity and target geometry

Clip each source triangle into its owning cells. Interpolate UVs, normals and source-sampled corner light at new
vertices. Do not assign a triangle crossing a partition to only its centroid's sector, and do not duplicate the
whole triangle into its neighbors. Promote a fragment only when it lies on a solid cell boundary with matching
inward orientation. Keep other fragments in models. Arbitrary interior world sheets are excluded: RED's geometry
contracts require a convex shell, and the classic software renderer does not provide the same world-face depth
ordering as hardware rendering.

Partition each boundary into non-overlapping native textured faces, solid invisible remainder and portal openings.
Subtract finite polygons only when they overlap with positive area: extended cutter edge lines must never dice a
distant surface. Compute textured pieces against actual holes and earlier artwork, then subtract them from the
invisible remainder. This keeps arbitrary background-mask seams out of the visible geometry.

The finalizer completes incident edge chains, splits overlong faces and portal pairs together, and certifies inward
closed shells, positive volume, reciprocal apertures and target float32 representability. Arithmetic-equivalent
points and exact target-coordinate aliases share a consistent representative. It can report tiny fragments that
vanish at target precision, but cannot discard an entire visible source or disconnect a surviving room graph.
If native placement alone cannot represent an authored surface, a typed refusal requests that source's original
clipped artwork remain in models. Placement retries preserve every genuine portal mask, rebuild the invisible
remainder, require strictly growing excluded source IDs, and share one work budget with at most eight retries.
Each source triangle must survive partitioning within 1e-6 relative area error; final native coverage is checked
again per source surface, and the total includes residual model triangles. An unrepresentable partition is refused
before publication. Preserve interpolated material/lighting seams and stock footstep policy.

Keep each convex clipped piece of an original triangle as one model face, rather than separately storing its fan
triangles. This preserves its affine attributes and avoids repeating clipped positions. Models reuse exactly equal
positions only when their normals and vertex lights also agree, and reuse UV coordinates independently. Pack one
mesh per model, bounded by 512 position vertices, 768 UV vertices, 24 corners per face and a conservative maximum
479 faces. The caller's smaller face budget still applies. Place the object origin strictly inside its
owning convex cell, and include all model geometry in its radius. When localization loses target precision, split
the packet or try an existing source endpoint as a singleton pivot under the same interior constraint. Every
encoded face must remain positive, planar within the rounding bound and convex; a failed singleton is refused.
Reserve dynamic Thing headroom.

## Interface and compatibility

`model-scene` retains the existing default and archive recipe. `hybrid-sectors` is explicit in GUI, CLI and MCP, with
sector, source-triangle, fragment and model budgets visible during preparation. Hash-bound apply includes every
new option. Preparation writes nothing; publication remains create-only and source captures must still match.

Report source triangles, native faces, residual triangles/models, sectors, portals, collapsed target fragments and
coverage. Separate private camera-workload estimates from actual runtime measurements. Preserve the original conversion
as an A/B baseline. Synthetic fixtures and tests are public; original maps and generated game assets remain private.

## Qualification

Tests cover closed rooms, partial doorways, occluded rooms, disconnected components, oblique partitions, supported
interior floors, disjoint cutting masks, triangles crossing boundaries, UV/light interpolation, malformed input,
budgets, target-coordinate aliases and reciprocal portal splitting. Verify the final JKL and models
independently after serialization. Compare original and hybrid from the same camera poses without removing detail.
Retail FPS, software/hardware visibility and player traversal require the actual executable and remain unverified
until tested. A source-derived geometry count or OpenJKDF2 result is not a retail frame-rate measurement.

Rebuilding an existing playable conversion also requires migrating its sector-dependent COG scans, activation
links, moving submodels, sky treatment and Thing references. A newly imported static scene does not replace those
systems. Compare the same source tessellation, materials and camera poses, and retain the original sound work.
