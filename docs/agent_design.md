# Headless level design operations

`red-agent` and `red-mcp` share the same design-operation implementation. A patch edits an isolated Level, prepares
the complete output, and publishes a new file only after the request and preservation checks succeed. Keep the source
and use a fresh output name for every accepted step. The current bounded design adapters support JKDF2 and MotS.

Start a closed two-unit room with `red-agent new start.jkl mots` or `red-agent new start.jkl jk`. The scaffold's
`scaffold.mat` and `scaffold.cmp` names are placeholders; this does not install game assets or produce a playable
mission by itself. Use `schema` to discover the operations exposed by the executable actually being run.

## Discover exact request shapes

The shared registry describes 63 patch operations and 42 CLI commands; `red-mcp` exposes 40 named tools over the
same agent implementation. No-topic `red-agent schema` retains the compatible inventory response. Focused topics
return standalone Draft 2020-12 JSON Schemas:

```text
red-agent schema commands
red-agent schema patch
red-agent schema setVertexPosition
red-agent schema bakeThingArchitecture
red-agent schema dependencies
```

`commands` validates a command name and supplies exact positional usage and scope in `x-commands`; it is not a schema
for every command's request body. `patch` describes the complete patch envelope and its 1..1000 operations. An exact
operation topic describes that operation's required fields, alternatives, nested values and bounds. `dependencies`
returns the closed request schema also embedded in MCP `sed_dependencies` and exposed as `dependencyRequestSchema`
in the no-topic inventory. Unknown topics and extra topic arguments are usage errors. The inventory and focused
operation list come from the same registry.

Use the focused schema before composing a request. Geometry validity, current indices, model compatibility, digest
matches and cross-field arithmetic still require runtime validation. `x-runtimeConstraints` describes those limits.
The schema preserves each existing family's unknown-field behavior; legacy root fields and optional source bindings
remain compatible. The six vertex/texture/ARCH operations below reject unknown fields.

## Inspect dependency evidence

`red-agent dependencies <level.jkl> <request.json|->` and MCP `sed_dependencies` inspect raw JKL plus explicitly
ordered, hash-bound GOB/GOO archives. The closed request requires `schemaVersion: 1`, `inputSha256` and `archives`,
with optional `extraResources` and `limits`. Each archive supplies `path`, `sha256` and optional `stock`. This route
captures bytes and returns evidence without assembling a package, publishing output, journaling or discovering loose
resources. It does not load a native workspace or certify a JKL game dialect.

The graph retains each admitted parent/target relation, authored name/folder and reused target. Node zero is the level;
resource identities follow package destination keys. Stock resources still have their children inspected. A resolved
node's hash and provenance identify its exact archive ordinal, physical row, original row name, byte range and lookup
tier. This is physical-row evidence; the unchanged package adapter's name-based reads can differ for leading-slash
aliases. Raw archive capture hashes also differ from package manifest normalized-entry digests.

`scanComplete` and `detailsComplete` are independent. A stopped scan leaves totals null and observed counts as lower
bounds; a completed scan can still omit detail. `allReferencesResolved` is false after an examined missing/ambiguous
reference, null if scanning stops before a clean conclusion, and true only for the completed supported reference scan.
Unsupported families, incomplete readers and COG code-only references remain explicit in `issues` and
`unverifiedScopes`; `runtimeClosureVerified` is always false. These receipts describe captured bytes without an atomic
multi-file snapshot or a final recheck of every path. They do not establish runtime closure or dependency-complete
prefabs.

Use the [workflow example](agent_workflow.md#dependency-inspection), [dependency inspection contract](dependency_inspection_design.md)
and [review record](reviews/dependency_inspection.md) for the supported readers, twelve operational limits, shared
schema, exact provenance fields and source-bound verification scope.

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

### Review semantic changes

Generic `plan` and `apply` return a versioned `semanticPreview` bound to the receipt's exact input and output hashes.
Its `authored` view compares the captured input model with the final patch candidate; `persisted` compares the same
input with the model reloaded from the exact proposed output bytes. Writer rounding, filtering, normalization and
regenerated numbering can therefore appear in the persisted view. The preview is a net comparison, not an operation
history or a validation result. Standalone asset, COG, conversion and package commands retain their existing receipts.

Paths address stored positions within each snapshot. For example, `/sectors/0/surfaces/2/material` names the field at
that position separately before and after the patch. Inserting or removing earlier records can change later slots;
stored IDs and names do not establish an entity correspondence. Arrays retain order and duplicate parameter names.
Each record reports `value`, `arrayLength` or `presence` with typed before/after descriptors. Floating-point `bits`
preserve signed zero and nonfinite distinctions; optional absence is separate from explicit zero.

Check `comparisonComplete` and `detailsComplete` in each view before accepting an apparent empty result. A stopped
comparison has `totalChanges: null`; `observedChanges` is a lower bound and `changed` is null until a difference is
observed. A complete comparison with shortened details still has an exact total. Long strings carry `omitted: true`
and their byte count, rather than a misleading clipped value. The default per-view limits are one million visited
slots, 4 MiB of compared string bytes, 128 change rows, 512 raw bytes per inline string and 32 KiB of inline strings.

Coverage includes the declared level-model fields, geometry, parameters, COG modifiers, source lights and ARCH
samples/provenance. Import bookkeeping, retained source text, renderer handles and data outside Level are explicitly
excluded. A complete zero-change comparison is limited to that coverage. General `schema`/`sed_discover` advertises
scope, exclusions and limits. See [the semantic preview contract](semantic_preview_design.md) for exact row, value,
budget and incompleteness semantics. Keep normal output readback, preservation checks and validation in the workflow.

## Read back the edited records

`query` returns paginated `items` with `inputSha256`, `sourceRevision`, `total`, `offset`, and `nextOffset`.
`nextOffset: null` ends the collection. Supply the source bindings when following pages so a different saved revision
cannot silently supply the next page. The default page size is 50 and the maximum is 200; selectors and offsets are
nonnegative 32-bit integers. Invalid numeric selectors are usage errors. Queries expose stored state without repairing
it, and nonfinite numeric components appear as null.

For geometry, supply an exact `sector`:

```json
{"collection":"surfaces","sector":0,"offset":0,"limit":50}
```

Surface rows include the local address, material slot/name, render modes, flags, normal, portal peer, declared UV
coordinate space, and ordered `corners`. Each corner supplies its vertex index, position, UV pair, independent `mono`,
and `rgb`. Use this order for `setSurfaceUv`. `cornerRecordsValid` describes the presence/finiteness of these records;
it is not a convexity or gameplay certificate. Invalid references are exposed with null detail instead of being
dereferenced or repaired by a query. Nonfinite numeric values appear as null.

`{"collection":"vertices","sector":0}` returns local vertex indices and positions. Surface pages construct only
the requested rows and admit at most 4096 corners in total. A page that exceeds a detail budget refuses with
`queryDetailsTooLarge`; reduce its size or use a more focused collection instead of treating the refusal as an empty
result.

`{"collection":"things"}` includes position, pitch/yaw/roll orientation, layer, ARCH index, and ordered
`sourceParameters`. These are the **instance-local** records (`parameterScope: "instance"`), including repeated keys
and frames; they are not a flattened effective-template dictionary. Each record exposes its stored type. Resolve
inheritance from the project's template data and resource-aware validation before treating a parameter as effective.

`{"collection":"lights"}` includes position, RGB color, white intensity, primary range, independent RGB intensity,
effective RGB intensity, inheritance state, signed legacy range metadata, flags, and layer. Null and explicit zero
have different meanings. These queries make after-save verification possible through the same headless interface.

The following detailed collections provide broader after-save evidence:

| Collection | Request controls | Returned state and limits of interpretation |
| --- | --- | --- |
| `sectors` | `offset`, `limit` | Editor ID/number, signed legacy flags, layer, scalar/RGB ambient and extra light, tint, sound/volume, thrust, colormap, geometry counts/bounds and neighbor indices. Empty or nonfinite geometry has null bounds. `neighborReferencesValid` checks addresses; it does not certify reciprocal or coincident portals. |
| `templates` | Optional `search`, `offset`, `limit` | Name, parent, and ordered typed `sourceParameters` with `parameterScope: "declared"`. Duplicate declarations remain visible. Inherited values are not flattened into this row. |
| `cogs` | Optional `search`, `offset`, `limit` | Editor ID/number, script name, ordered typed `sourceParameters`, and the compatible positional `values` array. `bindingStatus` remains `unverified-use-bindings-with-explicit-resources`; use `bindings` with captured script archives to resolve references. |
| `architecture` | `offset`, `limit` | Stored ARCH object index, per-mesh `meshVertexCounts`, `boundThings`, and `samplesSha256`. `modelCompatibility` remains `unverified-use-arch-validate-with-explicit-resources`; the sample digest alone does not establish a model match or a current bake. |
| `architecturalVertices` | Required `lightingIndex`, `mesh`; `offset`, `limit` | Ordered stored sample indices with independent `mono` and `rgb`. These are lighting samples, not model coordinates or hierarchy-transformed vertices. |
| `thingContainment` | Optional `search`, `offset`, `limit` | Thing identity/position, assigned sector, a uniquely containing sector when known, candidate count, tolerance and diagnostic detail. Status is `inside`, `boundary`, `outside`, `ambiguous`, or `unknown`. |

`search` is an ASCII case-insensitive substring filter for template names, COG names, and containment Thing names or
template names. It applies before pagination and leaves returned indices in their original document index space.
Matching is linear in pattern and candidate length. A nonempty filter shares an eight-MiB allowance for the pattern,
candidate-name bytes and one unit per examined record, including names that do not match. Candidate bytes are charged
before scanning even if a match would occur early. Empty filters select a page without scanning unselected names.
Exhaustion refuses with `queryDetailsTooLarge`; narrow the collection or split the document instead of treating it as
an empty search result.
For example, pass each object below as a separate `query <level> <request.json|->` request:

```json
{"collection":"templates","search":"door","offset":0,"limit":20}
```

```json
{"collection":"architecture","offset":0,"limit":20}
```

```json
{"collection":"architecturalVertices","lightingIndex":0,"mesh":0,"offset":0,"limit":50}
```

```json
{"collection":"thingContainment","search":"door","offset":0,"limit":20}
```

These six detail collections admit a page before constructing expensive rows. They share a 4096-record nested-data
budget: sector vertex/surface scans, template/COG parameters, and ARCH mesh/binding rows consume it as applicable.
Emitted source strings have a separate 1 MiB allowance before string copies; the two COG value representations both
count. This is a source-text allowance, not a 1 MiB serialized-response promise, because JSON escaping and row syntax
add bytes. An ARCH page admits at most 1,000,000 samples for hashing and 1,000,000 object-by-Thing binding comparisons.
Containment pages have a 20,000,000 geometry-work estimate across the selected Things and level geometry. It includes
empty-sector visits, vertex and surface scans, quadratic face checks and precision vertex-by-face-corner work.

Containment reports point membership in classifiable convex sectors. Boundary points are distinct from strict
interior points; overlapping matches remain ambiguous. Unclassifiable geometry, including nonfinite sector vertices,
reports `unknown` rather than inferring that a Thing lies outside. `assignedSectorMatches` and `onBoundary` are null
for unknown or ambiguous results, and the candidate count is null when unknown. This does not test a Thing's collision
volume, movement reachability or gameplay. Keep those observations separate.

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
| `setVertexPosition` | `sector`, `vertex`, `position` | Sets one sector-local vertex to absolute `[x,y,z]`, following exact reciprocal portal copies. Coordinates are finite and bounded to `+/-1000000`; all affected rooms must remain closed and convex with simple planar convex faces. |
| `fitSurfaceTexture` | `sector`, `surface`, `width`, `height` | Fits the face's dominant world-axis projection to explicit texel dimensions. Width and height are integers in `1..16384`. Requires texture-pixel UV space and preserves target lighting, scales and flags. |
| `alignSurfaceTexture` | `sector`, `surface`, `otherSector`, `otherSurface` | The first face is the target; the other face is the reference. Continues a supported affine reference mapping over one exact complete shared edge through the shared UV service. The reference remains unchanged. |
| `setVertexLight` | `sector`, `vertex`, `channel`, `value` | Updates every corner referring to the sector-local vertex. `channel` is `mono`, `red`, `green`, or `blue`; value is `0..1000`. Other channels and other vertices remain unchanged. JK stores mono only; RGB edits require MotS. This is direct authored light, not a bake. |

Room creation/extrusion admission requires finite coordinates within `+/-1000000`, closed manifold convex rooms,
and planar convex faces wound inward. It rejects malformed corners, existing adjoins on the requested face, and any
new volume for which a separating face cannot establish non-overlap with existing rooms. This is deliberately
conservative for rotated or complex layouts. Use the GUI's broader editing tools when a valid design falls outside
this bounded route.

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

### Absolute vertices and fitted/aligned textures

The three restored edit operations support JKDF2 and MotS. Their indices must address existing records. Every touched
face has 3..64 distinct corner references and a matching UV/light array, finite positions and UVs within
`+/-1000000`, nonzero edges, and a simple planar convex boundary. Crossed polygons, self-touching/retraced boundaries
and unsupported geometry refuse before publication.

`setVertexPosition` follows exact reverse-cyclic, reciprocal adjoin boundaries, including connections meeting at a
corner. Merely coincident or nearby unconnected vertices do not acquire a weld. Every collected copy receives the same
absolute position; all affected sectors are staged and validated before any is published. Entity IDs, references,
corner order, UVs and independent lighting remain intact. This operation also observes the retained-JKL/COG geometry
admission described above. It does not move Things or source lights.

Each `setVertexPosition` operation shares an 8,000,000-unit geometry/weld allowance across every affected sector,
collected vertex copy and portal visit. Both geometry passes, precision vertex/face/corner checks, face-boundary
checks, repeated portal matching and normal updates count before expensive work. Exhaustion returns input error
`editWorkLimitExceeded` before publishing any Level or output. This is an agent adapter limit; it does not change the
general Level format or GUI geometry limits. Separate operations each have their own allowance, within the existing
patch operation-count limit.

`fitSurfaceTexture` uses the legacy dimension-based projection: XY for a dominant Z normal, XZ for dominant Y, and YZ
for dominant X. The projected coordinate bounds become `0..width` and `0..height`. It does not set a world-space
texture density. Normalized native UV space refuses with `editUvCoordinateSpaceUnsupported` instead of guessing MAT
dimensions.

`alignSurfaceTexture` requires different source/target faces, exactly one complete edge with identical endpoints, and
a consistent affine reference mapping. Coplanar neighbors must lie on opposite sides of the edge. Either declared
texture-pixel or normalized space is admissible, with finite positive `ppunit`, when the shared core can reproduce the
reference frame and density. Unsupported frames, non-affine mappings or a result that cannot continue the seam refuse
with `editTextureUnsupported`. A successful alignment updates target UVs and the reference scale/flip state while
retaining the target material, independent light channels and other flags. It preserves the declared coordinate space.

Use `schema setVertexPosition`, `schema fitSurfaceTexture` or `schema alignSurfaceTexture` for the exact request shape.
For example, this is one operation in a normal hash-bound patch:

```json
{"op":"fitSurfaceTexture","sector":0,"surface":2,"width":64,"height":128}
```

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

## MotS architectural lighting

ARCH authoring is per-Thing stored model-vertex lighting. It is separate from ordinary 3DO asset lighting, world-corner
baking and runtime lights. The selected Thing must already be an effective MotS Arch owner (`thingflags` bit `0x8`,
including inheritance) with a resolvable effective `model3d`/`model3do`. These operations do not promote an ordinary
Thing by setting that flag.

| Operation | Required fields | Result |
| --- | --- | --- |
| `bindThingArchitecture` | `thing`, `lightingIndex` | Copies a compatible existing stored sample object into a new private object and binds the selected Thing to it. Other owners retain the original samples. |
| `rebindThingArchitecture` | `thing`, `policy: "reset"` | Creates a new private all-zero sample object for the resolved model and rebinds the selected Thing. Reset is the only supported policy. |
| `bakeThingArchitecture` | `thing`, `profile` | Creates a new private sample object from one of the explicit source-point profiles below and binds the selected Thing to it. |

All three require nonempty patch-root `resourceArchives`, each with a captured `path` and exact `sha256`; an
operation-local resource list is not accepted for ARCH. The optional patch-root hash-bound `templateSource` supplies
external template inheritance. Model names resolve through the shared archive-only resource locator with its canonical
candidate precedence, including explicit `3do/` paths. The 16 MiB model-entry limit is enforced before copying an
archive payload. No loose model path is opened implicitly. Missing resources, changed captures or invalid models
refuse the operation.

Every model LOD is inspected. Stored samples must exactly match geoset zero's source mesh/vertex counts, and every
lower LOD must fit those mesh/vertex prefixes. Bind checks this against the existing samples. Reset also enforces the
shared all-LOD validator: a lower LOD larger than geoset zero refuses with `archLowerLodOverflow`; it does not publish
an expanded shape that the validator would reject. Bake evaluates the geoset-zero source vertices in model-hierarchy
and Thing world space, preserving source mesh ordinals for the shared lower-LOD addressing. Shared or orphan source
meshes with ambiguous world placement refuse baking; topology-compatible bind/reset need no unique world placement.

Both bake profiles use all Level source lights and independent mono/RGB accumulation with squared point falloff
`(1 - distance / range)^2` inside each positive range. They do not filter by the GUI automatic-bake
**Affect Arch Things** selector. The profiles are:

- `source-points-v1`: direct source-point contributions without occlusion.
- `source-points-world-occluded-v1`: additionally tests two-sided world-surface triangle blockers. Valid reciprocal
  open adjoins do not block unless their face carries `SAF_BlockLight`; `LF_NoBlock` lights bypass blocker tests.
  Blocking faces require simple, planar, strictly convex 3..4096-corner fans. Model/self shadows are outside this
  profile.

These are explicit editor recipes, with no claim of reproducing a retail baker. Start with the focused operation
schema and include one of these operations in the reviewed patch:

```json
{"op":"rebindThingArchitecture","thing":3,"policy":"reset"}
```

```json
{"op":"bakeThingArchitecture","thing":3,"profile":"source-points-world-occluded-v1"}
```

Per-model admission uses the strict model inspector: 16 MiB source bytes, 32 LODs, 4096 source meshes, 262144 source
vertices, 524288 model triangles, and 4096 hierarchy nodes, with totals spanning LODs. Across a patch, ARCH operations
share 256 MiB of admitted model-entry bytes and 50,000,000 work units, including parsing/hierarchy work and potential
sample-by-light-by-blocker tests. Baking admits at most 256 source lights. The world-occluded profile admits at most
262144 world surfaces and 262144 blocker triangles.

Successful bind/reset/bake clears only the selected Thing's retained GUI automatic-bake baseline, so a later automatic
bake captures the newly authored samples. Other owners' baselines and shared sample objects remain unchanged. A
refusal retains all baselines and bindings, and a later failure in the patch discards the isolated candidate. If a
retained automatic baseline names the selected Thing's ID and another Thing shares that ID, the operation refuses
with `archBaselineOwnerAmbiguous` before authoring. It cannot safely assign that baseline to one owner. Duplicate IDs
without an affected baseline, or unrelated duplicate IDs, do not trigger this narrow guard or get silently repaired.

Authoring records the model-entry hash and stored sample digest; a bake also records its profile and v1 bake-input
digest. That input digest includes lights and world sample positions but **does not include world blocker geometry**.
It cannot by itself establish that world occlusion is still current. Retain the source and reviewed-output hashes for
plan/apply, which bind the operation to the actual level bytes, and rebake after relevant world changes. Query
`architecture`, then `architecturalVertices` using the selected Thing's new `archLightingIndex`, and use
`arch-validate` with explicit model resources for compatibility evidence. Stored sample readback alone is not a
rendering or gameplay observation.

Focused regression coverage is `sed_agent_edit_operations`, `sed_agent_arch_operations`,
`sed_agent_extended_query`, and `sed_agent_schema_contract`. Native GUI and engine qualification remain separate.
