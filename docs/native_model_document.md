# Native 3DO document editing

The portable [NativeModelDocument API](../src/core/include/sed/assets/native_model_document.hpp) opens an immutable
classic text 3DO 2.1 source snapshot. Each entry in `geosets()` is a raw, mesh-local `Model3do` for one LOD.
An orphan source mesh keeps its ordinal. Two nodes referencing the same mesh still reference one authored source
mesh. The hierarchy, including the source child/sibling fields, remains in the original bytes.

## Changing an existing texture vertex

```cpp
using namespace sed::assets;
const auto document = NativeModelDocument::open(sourceText);
const NativeTextureVertexEdit edits[]{ { 1, 0, 2, -8.5f, 32.0f } };
const auto changed = document.applyTextureVertexEdits(edits);
```

This changes texture vertex 2 of source mesh 0 in LOD 1. Each face corner referencing that row sees the new
coordinates, across all instances of that source mesh. UV ordinals and geometry vertex ordinals are independent.
U and V are absolute texture-pixel coordinates. Negative and tiled values require no material dimensions.
The original document remains available. The views returned by `source()` and `geosets()` borrow from the document.

Only numeric components whose stored float values differ receive new spellings. Equal values retain the original
spelling, including signed zero. Other coordinates, face corner bindings, lighting, authored radii, joint frames,
material names, comments, line endings and all other source bytes are copied exactly. The edit refuses a duplicate
address even if its two requests agree. An invalid address, nonfinite UV or failed validation rejects the whole
batch. Every changed document is reparsed through both the source scanner and the ordinary raw mesh reader; its
requested UVs must match the returned geosets.

## Transforming a source mesh

```cpp
using namespace sed::assets;
const NativeMeshTransform transforms[]{ { 1, 1, 1.25, { 0.125, -0.25, 0.5 } } };
const auto shaped = document.applyMeshTransforms(transforms);
```

This scales every position in LOD 1, mesh 1, about the mesh-local origin and then translates it. Each XYZ
component uses double-precision `std::fma(scale, component, translation)` with one final rounding. Loose vertices
move with the mesh. A shared source mesh changes once for all its instances. An unreferenced source mesh can also
be edited. The insertion offset, hierarchy, pivots and authored radii remain byte-exact. After an edit, review whether
the retained radii still enclose the shape and whether its stored lighting or any external Level/ARCH bindings need
updating. Positive uniform scale preserves normal directions, including authored hard edges. Both normal tables
and all stored lighting remain verbatim.

Only XYZ tokens with a changed numeric value receive a replacement. Equal values retain their spelling, including
signed zero. Every address must be unique, every scale finite and strictly positive, and every translation and
result finite. An empty target or numerically unchanged mesh keeps its bytes. A subsequent edit uses the
reopened candidate's source spans. A failed batch leaves the input snapshot intact.

Each mesh with changed positions must have strictly convex, simple, planar faces before and after rounding.
The check uses the [model-lighting polygon predicate](model_lighting.md#evaluation) for every face, including
undrawn faces. The maximum allowed error in a candidate corner vector relative to its face anchor is `1e-10`
times the expected scaled face extent. The distance between old and new geometric unit normals must be at most
`1e-10`. Distinct source positions may never round to the same XYZ triple within the mesh, even on disconnected
faces or loose rows. Originally coincident rows retain their identities, UVs, colors and normals. Sub-tolerance
drift between disconnected pieces is possible. Global intersection, collision and runtime behavior require
separate evaluation.

## Admission and limits

The document uses the bounded source scanner shared with model lighting, with a stricter native authoring profile.
It requires the four known sections, sequential source indices, the scalar vertex lighting field, and complete vertex
and face normal blocks. Unknown sections or mesh properties, orphan text outside sections, nonclassic versions and
unrepresentable UVs are refused. Floating-point fields follow the shared sign rule: `+1.5e+1` is valid and `+-1` is
refused. A colon inside a material name is also refused because the raw reader would change the exposed name.
An asset refused by native editing can still be inspected through the existing
model reader where that reader supports its layout.

The [scanner ceilings](model_lighting.md#operational-limits) apply, including 16 MiB per input and edited output,
262,144 physical input lines, 4,096 tokens per line, 32 LODs and 50,000,000 scan/load work units per open.
A UV batch accepts at most 262,144 addressed rows. A transform batch accepts at most 4,096 addressed meshes.
Its separate 50,000,000-unit validation budget covers linear vertex and corner visits, position sorting, and both
quadratic polygon checks across all changed meshes. The ordinary 3DO reader applies its own allocation limits to
each raw LOD. These are editor operation limits. Target-specific mesh or renderer acceptance needs separate evidence.

This API changes in-memory document snapshots. The [mesh design](native_mesh_editing_proposal.md) tracks the
remaining source fields, topology, model history and native interaction.

## Verification

The `sed_native_model_document_tests` CTest exercises the project-owned two-LOD relay-sentry fixture, including
a variant with a shared source mesh, an orphan and a shared UV row. Its byte comparison covers every byte outside
selected UV or XYZ components. It also covers chained edits, loose and disconnected position collisions,
authored normal/color seams, fused rounding and shared face-work limits. A separate readback with Blender-Sith's portable strict `load3do` reader at
`34d256c30f66fe9826e5a3318f2fd4397257f24c` opened a variant of `relay_sentry.3do` edited with
`{ 1, 1, 2, -16.5f, 35.25f }`. The reader reported classic 2.1, both LODs and the requested values at that
address. All thirteen other UV rows matched the original. This observation covers portable asset parsing.
