# Native 3DO document and UV editing

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
A batch accepts at most 262,144 addressed rows. The ordinary 3DO reader applies its own allocation limits to
each raw LOD. These are editor operation limits. Target-specific mesh or renderer acceptance needs separate evidence.

This API changes in-memory document snapshots. The [mesh design](native_mesh_editing_proposal.md) tracks the
remaining source fields, model history, native interaction, automation and create-only publication.

## Verification

The `sed_native_model_document_tests` CTest exercises the project-owned two-LOD relay-sentry fixture, including
a variant with a shared source mesh, an orphan and a shared UV row. Its byte comparison covers every byte outside
the selected UV component. A separate readback with Blender-Sith's portable strict `load3do` reader at
`34d256c30f66fe9826e5a3318f2fd4397257f24c` opened a variant of `relay_sentry.3do` edited with
`{ 1, 1, 2, -16.5f, 35.25f }`. The reader reported classic 2.1, both LODs and the requested values at that
address. All thirteen other UV rows matched the original. This observation covers portable asset parsing.
