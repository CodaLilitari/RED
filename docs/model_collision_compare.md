# Compare model faces for a collision replica

When a converted stair or landing uses a separate collision Thing, compare its model against the original before testing the route. This read-only command compares the first geoset in two classic text 3DO 2.1 files. Each input must meet the [native document admission rules](native_model_document.md#admission-and-limits), including complete face normals and sequential physical rows. Each file is limited to 16 MiB. The receipt includes a SHA-256 digest for each captured file.

```sh
red-agent model-collision-compare original.3do collision-replica.3do
```

For a multiplayer player skin, the active Thing can use a sphere for incoming hits. Matching 3DO faces cannot
measure its radius or its height above the floor. See the [small-player hierarchy and game checks](model_hierarchy.md#small-players-with-stock-animations).

The matching MCP tool is `sed_model_collision_compare`:

```json
{"path":"original.3do","candidate":"collision-replica.3do"}
```

Both paths must be inside the configured workspace or declared readable resource roots. Unknown arguments are
refused. The command creates no archive, model, journal entry or other output file.

`sameFaceMultiset` is true when the two models have exactly the same face records under this comparison. For each face RED compares every mesh-local XYZ corner in order, starting at the original first vertex, and the stored XYZ face normal. The native plane test uses that first vertex and normal. Reversing a winding, rotating its first corner or changing a stored normal can therefore change the result. Coordinates and normals compare at RED's parsed numeric precision. Equivalent numeric spellings and signed zeros compare equal.

`matchedFaces` counts occurrences present on both sides. `missingFaces` counts remaining source occurrences and `extraFaces` counts remaining candidate occurrences. An extra copy cannot cancel a missing, different polygon. The comparison includes every first-geoset source mesh once, including undrawn faces and meshes with no hierarchy references. Vertex IDs, mesh grouping, draw-node transforms and other LODs do not contribute to these three counts.

`sameFaceSequence` compares the face records in file order across successive meshes. `sameMeshFaceCounts` compares the number of faces per mesh in mesh order. A replica can preserve all faces while changing either result. A valid comparison exits successfully even when differences exist. A script checking a geometry-preservation requirement should require `sameFaceMultiset` and the expected nonzero face count for the selected model.

Each side separately reports `meshes`, `faces` and `untexturedModeZeroFaces`. The last field counts faces that declare both material `-1` and face geometry mode `0` in geoset zero. Later visual LODs, other material or face flags, UVs, lighting, Thing placement and collision mode, contact sounds, bounding radii, target loader limits and renderer output need independent checks. `runtimeVerified` is always false. Run both directions across the intended landing in the target engine to verify traversal. The [conversion reliability guide](conversion_reliability.md) describes the complete review sequence.
