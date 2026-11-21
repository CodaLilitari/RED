# Reviewed native model authoring

## Purpose and scope

Make the existing all-LOD native model document usable through inspected, reviewed, create-only asset edits. This
increment admits existing UV-row changes and whole-source-mesh positive uniform scale followed by translation.
It advances [the native mesh workspace](native_mesh_editing_proposal.md); individual vertex deformation, rotation,
normal or radius editing, topology changes, GUI interaction, history and recovery remain separate milestones.

Use the admitted classic text 3DO 2.1 source profile. Address a geoset and an authored source mesh by ordinal.
Geometry operations move the shape relative to its existing joint, in mesh-local coordinates. They do not move a
joint or insertion offset. Every hierarchy instance of that source mesh sees the same edit once; an orphan mesh
remains addressable. No proprietary assets or new runtime dependencies are needed.

## Portable transform contract

Extend `NativeModelDocument` with:

```cpp
struct NativeMeshTransform
{
    std::size_t geoset{}, sourceMesh{};
    double scale{ 1.0 };
    TVector translation{};
};

NativeModelDocument applyMeshTransforms(std::span<const NativeMeshTransform> transforms) const;
```

For every position row in an addressed mesh, including loose vertices, compute each candidate component with
`std::fma(scale, component, translationComponent)`. Scale is about the mesh-local origin, before translation.
Scale must be finite and strictly positive; translation and every result must be finite. The core accepts at most
4,096 addressed meshes per request. Duplicate mesh addresses are refused, including empty meshes and identity
requests. Validate addresses and parameters before dropping numerical no-ops. An addressed empty mesh succeeds
without changing bytes. Numeric equality retains the original spelling, including signed zero.

Capture XYZ token spans as doubles beside the existing float UV spans. Match captured values against the ordinary
raw reader for every LOD. Patch only changed XYZ tokens, bound output growth, reopen the resulting document, and
verify the requested doubles. Reuse the same preservation writer for UV changes without altering their existing
formatting or refusal contract. A later operation uses spans captured from its own reopened snapshot.

Positive uniform transforms preserve authored normal directions. Leave both normal tables byte-exact, including
zero, non-unit and intentionally discontinuous normals. Preserve all UVs, connectivity, face attributes, lighting,
authored radii, materials, hierarchy, comments, line endings and untouched LODs. Retained radii can underbound the new
shape. Retained lighting can need rebaking; the edit does not find or update external Level/ARCH bindings.

### Geometry admission and rounding

For each mesh with an effective position change:

1. Require each baseline and candidate face to be a strictly convex, simple, planar polygon under the existing
   model-lighting predicate's anchored, scale-normalized tolerance of `1e-10`. Preserve that predicate's arithmetic,
   check order, fan determinant test and lighting diagnostics when extracting a reusable private adapter. Native
   validation does not create shadow triangles or impose opaque-material restrictions.
2. Compare each candidate face-relative vector to `scale * (sourceCorner - sourceAnchor)`, computed independently
   from the source. Divide differences by the expected face extent and require Euclidean error at most `1e-10`.
   Refuse nonfinite differences, nonfinite extents and underflow of a nonzero expected extent.
3. Compare baseline and candidate geometric unit normals with Euclidean distance at most `1e-10`. Use geometric
   winding rather than authored face normals. Refuse a zero or nonfinite normalization denominator.
4. Across all positions in the selected mesh, refuse newly equal candidate XYZ triples originating from distinct
   source positions. Existing coincident rows remain distinct and are allowed. This also covers loose vertices and
   disconnected pieces. This is not a general self-intersection test; sub-tolerance drift can remain between pieces.

Use one 50,000,000-unit transform-validation pool for the entire request. Before expensive work, charge linear
position/corner visits, a conservative `N * ceil(log2(max(N, 2)))` sorting allowance, and both baseline and candidate
quadratic face checks. Check products and sums before overflow. Each document open retains its separate existing
50,000,000 scan/load-work ceiling. Keep the existing 16 MiB input/output and allocation ceilings. These are editor
operation limits and do not establish engine coordinate, geometry, collision or rendering acceptance.

## CLI and MCP contract

Expose the same implementation through:

```text
red-agent model-native-inspect <input.3do> [query.json|-]
red-agent model-native-plan <input.3do> <request.json|->
red-agent model-native-apply <input.3do> <request.json|-> <expected-request-sha256> <expected-output-sha256> <new-output.3do>
```

The MCP tools are `sed_model_native_inspect`, `sed_model_native_plan` and `sed_model_native_apply`. Their source
argument is `path`. Inspect optionally accepts `query`; plan/apply require `request`; apply additionally requires
`expectedRequestSha256`, `expectedOutputSha256`, and `output`, and accepts the existing optional `operationId`.
Reject unknown outer arguments. Apply the existing readable/writable path policy and operation journal.

### Discovering addresses

An omitted inspect query means `{ "collection": "meshes", "offset": 0, "limit": 64 }`. A supplied query is a strict
object. `collection` is required and is one of `meshes`, `vertices`, `textureVertices`; `offset` defaults to 0 and
`limit` to 64. `offset` is an integer in `0..262144`, `limit` in `1..128`. Mesh pages enumerate all geosets in source
order. The two row collections require `geoset` and `sourceMesh`; those fields are refused for a mesh page.

Return `inputSha256`, `profile: "classic2.1"`, `coordinateSpace: "mesh-local"`, `uvUnits: "texture-pixels"`,
`geosetCount`, `collection`, `total`, `offset`, `nextOffset` (null when exhausted), and `items`. An offset beyond the
end yields an empty page. Mesh rows expose both source ordinals, a name, row/face counts, affected hierarchy-node
count, raw position bounds and the mesh's `authoredRadius`. Names longer than 256 bytes are omitted with
`nameOmitted: true`. Position rows expose their ordinal and XYZ doubles. UV rows expose their ordinal, stored float
U/V, and referencing-corner count, counted once over the selected mesh. Never dump full corner lists. Empty raw
bounds are explicitly null. Do not label the derived model radius as an authored value. State `runtimeVerified: false`.

### Strict edit request

All fields below are required. Unknown fields at any level are errors.

```json
{
  "schemaVersion": 1,
  "profile": "classic2.1",
  "inputSha256": "<64 hexadecimal characters from inspection>",
  "operations": [
    { "op": "setTextureVertex", "geoset": 1, "sourceMesh": 0, "textureVertex": 2, "u": -8.5, "v": 32 }
  ]
}
```

Alternatively, a transform operation has exactly these fields:

```json
{ "op": "transformMesh", "geoset": 1, "sourceMesh": 0, "scale": 1.25,
  "translation": [0.125, -0.25, 0.5], "radiusPolicy": "preserve" }
```

A batch contains 1 through 256 operations, all of one family. Mixed UV/transform batches are refused until their
composition contract is defined. Geoset ordinals are integers `0..31`, mesh ordinals `0..4095`, and UV ordinals
`0..262143`; the source supplies the tighter actual bounds. UV components must be finite and within the finite float
range. Convert once to float, reject a nonzero component that becomes zero, and report the actual stored float. Normal
decimal float rounding is allowed. Adapter scale is in `1e-6..1e6`; translation components are in `-1e6..1e6`.
The required radius policy makes preservation explicit. Runtime validation and published JSON schemas must agree.

Use the existing 1 MiB JSON-input ceiling and parser limits. Bound serialized inspect/plan/apply responses to 1 MiB;
validate the receipt size before publication. Do not truncate a reviewed operation list or silently omit edits.

### Review and publication

Capture the source before reading a potentially interactive request. Check `inputSha256` against those bytes.
Call the relevant core batch method once. Plan writes nothing and returns the source hash, `requestSha256`,
`outputSha256`, the profile, `written: false`, and a bounded per-operation before/after report. Calculate the request
hash as SHA-256 of RED's `toJsonString(validatedRequest)` representation. This is RED JSON serialization, not a
portable canonical-JSON standard. Calculate the output hash from the exact candidate document bytes.

UV reports identify changed/no-op rows, stored before/after values, and shared-row reference counts. Transform
reports identify changed position rows/components, affected node count, before/after raw bounds, and retained
`authoredRadius`. Report normals, hierarchy, radii and stored lighting as preserved. Set `lightingNeedsRevalidation`
when positions actually change. Explain that retained radii need separate review and that runtime verification has
not occurred. A nonidentity request that rounds to an unchanged result must report zero changes truthfully.

Apply requires both reviewed hashes, recomputes the same candidate, and checks both before publication. Output
identity alone does not bind intent: reordered disjoint edits or an additional no-op can create the same bytes.
Publish only to a fresh `.3do` using the existing atomic no-replace writer and `SourceReadSet` recheck. Source rechecks
are optimistic revision checking, not a lock on another process. Keep the original source untouched.

MCP delegates request staging and execution to the existing adapter infrastructure. The journal binds the original
arguments, replays completed identical operations, and conflicts on a changed intent under the same operation ID.
Retain `journalUnavailable` versus `journalOutcomeUncertain`: completion journaling can fail after a complete asset
has been published. Never describe every error as proof that no output exists.

## Evidence and ownership

The source contract is grounded in [the model document](native_model_document.md), [model lighting](model_lighting.md),
[the native workspace design](native_mesh_editing_proposal.md), and Blender-Sith's portable loader at
`34d256c30f66fe9826e5a3318f2fd4397257f24c`. Use the owned relay-sentry fixture and synthetic variants for independent
readback. Exact source preservation and cross-parser agreement are structural evidence. Actual retail/OpenJKDF2
rendering, animation and collision remain separately observed evidence.
