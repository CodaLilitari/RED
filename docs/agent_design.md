# Headless level design operations

`red-agent` and `red-mcp` share the same design-operation implementation. A patch edits an isolated Level, prepares
the complete output, and publishes a new file only after the request and preservation checks succeed. Keep the source
and use a fresh output name for every accepted step. The current bounded design adapters support JKDF2 and MotS.

Start a closed two-unit room with `red-agent new start.jkl mots` or `red-agent new start.jkl jk`. The scaffold's
`scaffold.mat` and `scaffold.cmp` names are placeholders; this does not install game assets or produce a playable
mission by itself. Use `schema` to discover the operations exposed by the executable actually being run.

## Review and apply

1. Inspect the source and record its `inputSha256` and `sourceRevision`. Bind those values in the patch request.
2. Select entities from that exact source. A surface selection takes `target: "surface"`, a world `point`, and its
   outward `direction`. A successful unique result supplies the sector and surface indices. Do not guess indices from
   another saved revision.
3. Put supported operations in a request with `schemaVersion: 1` and the source bindings. Run
   `red-agent plan source.jkl patch.json`, inspect the reported changes, and retain `outputSha256`.
4. Run `red-agent apply source.jkl patch.json next.jkl <outputSha256>`. The hash ties the accepted output to the bytes
   reviewed during planning. A stale source or rejected operation publishes no level output.
5. Reopen the new file, query the affected records, and validate structural and resource requirements. Keep editor
   preview, engine loading, collision clearance, and actual gameplay observations as separate evidence.

Apply preserves the source container: native input needs `.sed`, classic text needs `.jkl`, and IJIM text needs
`.ndy`. Use explicit `convert` for a different container. The native workspace/history preservation boundary is
documented in [the SED format contract](sed_format.md#headless-native-preservation).

Before a patch's first entity allocation, append admission checks that the stored next editor ID is positive and above
every live Sector, Thing, light, COG and hierarchy-node ID. A reused or invalid next ID refuses with
`entityAppendInvalidIdentity`; the operation does not silently renumber existing entities. This does not prevent
inspection or a supported no-op copy of the source. Exhausted ID capacity remains a separate allocation refusal.

## Read back the edited records

`query` returns paginated `items` with the source hash/revision. For geometry, supply an exact `sector`:

```json
{"collection":"surfaces","sector":0,"offset":0,"limit":50}
```

Surface rows include the local address, material slot/name, render modes, flags, normal, portal peer, declared UV
coordinate space, and ordered `corners`. Each corner supplies its vertex index, position, UV pair, independent `mono`,
and `rgb`. Use this order for `setSurfaceUv`. `cornerRecordsValid` describes the presence/finiteness of these records;
it is not a convexity or gameplay certificate. Invalid references are exposed with null detail instead of being
dereferenced or repaired by a query. Nonfinite numeric values appear as null.

`{"collection":"vertices","sector":0}` returns local vertex indices and positions. Geometry pages construct only
the requested rows, clamp the page size to 200, and refuse a surface page over 4096 corners with
`queryDetailsTooLarge`; reduce the page size instead of treating a refused page as empty.

`{"collection":"things"}` includes position, pitch/yaw/roll orientation, layer, ARCH index, and ordered
`sourceParameters`. These are the **instance-local** records (`parameterScope: "instance"`), including repeated keys
and frames; they are not a flattened effective-template dictionary. Each record exposes its stored type. Resolve
inheritance from the project's template data and resource-aware validation before treating a parameter as effective.

`{"collection":"lights"}` includes position, RGB color, white intensity, primary range, independent RGB intensity,
effective RGB intensity, inheritance state, signed legacy range metadata, flags, and layer. Null and explicit zero
have different meanings. These queries make after-save verification possible through the same headless interface.

## Geometry, material, UV, and vertex light

Every field shown below is required unless described otherwise. Unknown fields are refused, so misspelled controls
cannot silently become no-ops. Indices address the document state after preceding operations in the same patch.

The existing `setSectorFlags` operation accepts the legacy `flags` spelling or RED's original `value` spelling,
with exactly one present. Stored bit patterns must fit 32 bits; oversized integers cannot wrap into another value.

| Operation | Fields | Contract |
| --- | --- | --- |
| `addBox` | `center`, `halfSize` | `center` is `[x,y,z]`. The legacy scalar `halfSize` creates a cube; `[x,y,z]` half-extents create a rectangular box. Each half-extent is `0.0001..100000`. Appends one untextured closed room without inventing a resource registration. |
| `extrudeSurface` | `sector`, `surface`, `distance` | Positive distance `0.0001..100000` extrudes outward from a free boundary and creates reciprocal portals through the shared core. The operation appends one room; it does not accept an Expand parameter. |
| `adjoinSurfaces` | `sector`, `surface`, `otherSector`, `otherSurface` | Joins unadjoined complete opposite boundaries in two different sectors. Shared core portal defaults hide the two faces and enable the normal visibility/movement/sound connection. Partial overlaps are refused. |
| `registerMaterial` | `name` | Appends an ASCII `.mat` basename of at most 127 characters. Case-insensitive duplicate names, paths, traversal, whitespace, quotes, and comments are refused. Registration does not read or copy a MAT. |
| `registerColormap` | `name` | The same registration contract for `.cmp` basenames. |
| `setSurfaceMaterial` | `sector`, `surface`, `materialIndex` | Uses an already registered exact material slot and name. |
| `setSectorColormap` | `sector`, `colormapIndex` | Uses an already registered exact colormap slot and name. |
| `setSurfaceRenderModes` | `sector`, `surface`, `geometryMode`, `lightMode`, `textureMode` | Geometry is `0..4`; light and texture modes are `0..3`. Geometry mode zero hides a face; this operation does not change portal flags. |
| `setSurfaceUv` | `sector`, `surface`, `uv` | One `[u,v]` texel pair for each existing corner, in its current order, with `3..64` corners. Signed coordinates are bounded to `+/-1000000`. Vertex-light channels remain unchanged. |
| `setVertexLight` | `sector`, `vertex`, `channel`, `value` | Updates every corner referring to the sector-local vertex. `channel` is `mono`, `red`, `green`, or `blue`; value is `0..1000`. Other channels and other vertices remain unchanged. JK stores mono only; RGB edits require MotS. This is direct authored light, not a bake. |

Geometry admission requires finite coordinates within `+/-1000000`, closed manifold convex rooms, and planar convex
faces wound inward. It rejects malformed corners, existing adjoins on the requested face, and any new volume for which
a separating face cannot establish non-overlap with existing rooms. This is deliberately conservative for rotated or
complex layouts. Use the GUI's broader editing tools when a valid design falls outside this bounded route.

Geometry changes also refuse unmodeled JKL sections or records and unresolved COG surface mappings whose imported
order differs from editor order. Keeping opaque bytes is insufficient if a topology edit could change what their
indices mean. Existing resource tables and supported COG identities are retained by the shared model/export path.

Appending a room checks whether an existing COG initializer would become newly valid in the added sector or global
surface index range. Such a change is refused with `basicGeometryReferenceCapture` before an output is published.
Known typed references use their declared index space; a typed integer is not treated as a reference just because its
number matches a new face. Preferred native surface-pair text is resolved through the shared parser before and after
the proposed append; a newly resolved or retargeted pair is also refused. Negative sentinels and references outside
the newly added range retain their values.
Unresolved numeric initializers have no trustworthy scalar/reference declaration. The adapter conservatively checks
their decimal numeric prefix and explicit hexadecimal interpretation against both new index ranges. This refusal
does not assert that an unknown argument is a reference: resolve its COG schema or explicitly repair its binding
before retrying. Preserving a raw argument's bytes alone cannot establish that its meaning stayed unchanged.

`setSurfaceUv` accepts explicit **texture-pixel coordinates**. A normalized-UV native document is refused with
`basicUvCoordinateSpaceUnsupported`; the adapter does not guess texture dimensions or treat texels as normalized
fractions. Use a pixel-space engine copy or an editor workflow with verified dimensions. A source surface's corner
order is part of the request, so preserve it when editing UVs.

## Connected-room example

This workflow uses spatial selection to obtain the face addresses. The synthetic regression exercises the same geometry edits, hash-bound apply, and reopen using independently located normal-based face indices:

1. Create the starter room, which spans `[-1,1]` on each axis.
2. Select the wall at `[1,0,0]` facing outward `[1,0,0]`, then extrude it by `2`. The appended room spans X `1..3`.
3. Add a box centered at `[4,0,0]` with `halfSize: 1`. It spans X `3..5` and touches the second room.
4. Select both opposing faces at `[3,0,0]` using outward directions `[1,0,0]` and `[-1,0,0]`. Adjoin their returned
   sector/surface addresses. The resulting three rooms have two reciprocal connections.
5. Register a material and colormap, query their resulting slots, and assign them to the intended wall/sector. Set
   render modes, per-corner texel UVs, and independent MotS mono/RGB light values.
6. Reopen and check the exact resource slots, four directed portal records, UV corner order, edited light channels,
   and unchanged neighboring vertices. This proves the synthetic editing/persistence route; it does not prove that a
   supplied material resolves or that a player can complete a mission.

The focused regression is [basic_operations_tests.cpp](../tools/sed-agent/tests/basic_operations_tests.cpp), registered
as `sed_agent_basic_operations`. Native optional-state regression is `sed_agent_native_preservation`. Run the relevant
focused tests during iteration and the complete Release gate before delivery.

## Source lights and Things

| Operation | Fields | Contract |
| --- | --- | --- |
| `addLight` | `position`, `rgb`, `intensity`, `range`; optional `rgbIntensity`, `legacyRgbRange`, `flags`, `layer` | Appends one editor source light. Position is `[x,y,z]`; RGB has three components in `0..1`; intensity is `0..1000`; primary range is `0.0001..100000`. Flags/layer default to zero. |
| `setLight` | `light` plus one or more light fields above | Omitted fields retain their stored values. Changing white intensity does not rescale an explicitly authored RGB intensity. |
| `addThing` | `sector`, `template`, `name`, `position` | Appends one fresh Thing using an existing template and the exact requested name. Requires unique strict containment in the explicit sector. Names must be unique without case distinctions. Defaults are zero orientation/layer, no local parameters and no ARCH binding. |
| `setThingPosition` | `thing`, `position`, `sector` | Sets an existing Thing's absolute position and explicit sector. The position must lie strictly inside that uniquely classifiable sector. Boundary, outside, overlapping, and unclassifiable points are refused. Path-frame coordinates stay unchanged. |
| `setThingOrientation` | `thing`, `orientation` | Sets absolute `[pitch,yaw,roll]` in degrees. |
| `setThingName` | `thing`, `name` | Sets the display name, retaining the template and entity index. |
| `setThingTemplate` | `thing`, `template` | Uses a template already defined in this document. A Thing with local path-frame records requires a coordinated frame workflow before changing its template. |
| `setThingValue` | `thing`, `key`, `value` | Replaces one local parameter in place or appends an override, preserving unrelated ordered records. `key` contains ASCII letters, digits, or underscore; `value` is an engine token. Repeated target keys require an ordered-record editor. |
| `clearThingArchitecture` | `thing` | Uses the shared MotS ARCH unbind operation. The Thing must be an eligible Arch Thing; the binding/provenance are cleared while the existing sample arrays remain stored. |

Source-light `rgbIntensity` is either `0..1000` or null to inherit white intensity. `legacyRgbRange` is metadata in
`-100000..100000`, or null to remove that metadata; it never changes the primary falloff range. Colored source lights
and independent RGB authoring require MotS. Existing imported metadata remains unchanged when its fields are omitted.
Flags are unsigned 32-bit values in decimal JSON; layer must already exist, with layer zero available when no named
layers are present. Adding or editing a source light does not bake world corners or ARCH samples.

`addThing` does not copy an existing instance, flatten inherited template parameters, or create resources. Existing
Things retain their identity. An ARCH-capable template still creates an unbound instance: model-resource staging and
ARCH binding/baking are separate operations. Exporting an unbound ARCH Thing to JKL may require model resources to
size its default samples; native creation alone does not establish that engine export is ready.

Appending Things requires complete, matching schemas for every placed COG so the shared guard can check its positional
Thing arguments. Supply the request's explicit `resourceArchives` entries with exact paths and SHA-256 hashes for
schema resolution; both `addThing` and `placeThingArray` use that captured evidence. Without a captured schema resolver,
the operation refuses with `cogAppendGuardRequired`. References
must be canonical `-1` or currently placed Thing indices; an unchecked runtime or future index refuses. The guard
does not analyse arbitrary script code, computed indices, or template-attached scripts. Unmodeled retained JKL records
or sections refuse single-Thing and array creation with `entityAppendUnknownState`; native opaque-state protection also applies.
Strict point containment does not establish bounding-volume collision clearance.

Thing positions/orientations are finite and bounded to `+/-1000000`. Name/template/value tokens are nonempty ASCII,
at most 127 characters, without whitespace, double quotes, or comment markers. Native scalar parameter declarations
are preserved when editing; invalid numeric/flag values are refused. Most engine parameter names are represented as
opaque tokens rather than a comprehensive typed engine schema, so token validation does not establish resource or
gameplay validity. Native typed tuple records require their dedicated editor.

Direct `frame` or `numframes` changes are refused with `entityFramesRequireOperation`; use `setThingFrames` so the
ordered frames and allocation count remain coherent. Changing `move` while local frames exist is also refused.
Changes to an ARCH-bound Thing's pose, template, or parameters require an explicit `clearThingArchitecture` first.
Unchanged values remain valid no-ops. This prevents a generic field edit from silently claiming that old samples fit
a new model or pose.

The entity persistence regression is
[entity_operations_tests.cpp](../tools/sed-agent/tests/entity_operations_tests.cpp), registered as
`sed_agent_entity_operations`. Detailed query coverage is
[query_details_tests.cpp](../tools/sed-agent/tests/query_details_tests.cpp), registered as `sed_agent_query_details`.
