# Editing native 3DO meshes and UVs

Use `red-agent` or the matching MCP tools to edit an existing classic 2.1 text 3DO and publish a fresh variant.
The editable addresses are source ordinals. Changing a mesh affects every joint that instances it at that LOD.
An unreferenced mesh can be edited by its ordinal too.

Source, query, request and output paths use UTF-8. Windows command-line arguments retain their original Unicode
filenames, including characters outside the system code page. Quote a path containing spaces. Invalid UTF-8 and
embedded NUL bytes are rejected; a successful receipt names the same destination that receives the file.

## Find a source row

```text
red-agent model-native-inspect relay_sentry.3do
red-agent model-native-inspect relay_sentry.3do query.json
```

The default page lists up to 64 meshes across all geosets, in source order. Each item gives `geoset`, `sourceMesh`,
`name`, `vertices`, `textureVertices`, `faces`, `affectedNodeCount`, `bounds` and `authoredRadius`. Bounds use raw
mesh-local positions including loose vertices, with `min` and `max` XYZ triples or null for an empty mesh. A name
longer than 256 bytes or containing invalid UTF-8 is omitted with `nameOmitted: true`. Its address remains usable.

For a texture-vertex page, put this query in `query.json`:

```json
{"collection":"textureVertices","geoset":0,"sourceMesh":0,"offset":0,"limit":32}
```

Each UV item reports `textureVertex`, `u`, `v` and `referencingCorners`. A count above one means the edit reaches
several corners. Geometry vertex and texture vertex indices are independent. Use `collection: "vertices"` for
`vertex`, `x`, `y`, `z` rows or `collection: "meshes"` to page the inventory. Supply both mesh address fields for
row collections and omit them for the mesh inventory. Offset ranges from 0 through 262144 and limit from 1
through 128. Continue at `nextOffset` until it is null. A past-end offset returns an empty `items` array.

## Plan an edit

The following request changes a shared UV row in the [relay-sentry fixture](../tests/fixtures/relay-sentry/relay_sentry.3do).
Replace the hash with `inputSha256` from inspection if your source bytes differ.

```json
{
  "schemaVersion": 1,
  "profile": "classic2.1",
  "inputSha256": "208a02b32b47f7aa447ee43b0c70b09640687c2df2c0410d52441203a337c3ab",
  "operations": [
    {"op":"setTextureVertex","geoset":0,"sourceMesh":0,"textureVertex":1,"u":-8.5,"v":32}
  ]
}
```

Alternatively, replace the operations array with a mesh transform array:

```json
[
  {"op":"transformMesh","geoset":1,"sourceMesh":1,"scale":1.25,
   "translation":[0.125,-0.25,0.5],"radiusPolicy":"preserve"}
]
```

```text
red-agent model-native-plan relay_sentry.3do request.json
```

A request contains 1 through 256 operations of one kind. Each UV row or transform mesh can appear once per batch.
All fields shown are required and unknown fields are rejected. Use geoset ordinals 0–31, source mesh ordinals
0–4095 and texture vertex ordinals 0–262143, within the actual row counts. UVs are absolute texture pixels.
The plan reports before/after values at stored float precision. A nonzero input that would round to zero is refused.

Mesh transforms scale every original position about its local origin, then translate it. Scale is between
`1e-6` and `1e6` inclusive, and each translation component is between `-1e6` and `1e6` inclusive. The operation
preserves authored normal directions, including existing seams, and the joint frame and pivot. The core checks
face geometry and floating-point rounding before accepting a changed mesh. See the [native document contract](native_model_document.md).

Review `changedOperations`, `noopOperations` and the complete ordered `operations` receipt. UV rows include shared
corner counts. Transform rows include changed position and component counts, both bounds, the affected node count
and retained mesh radius. For a changed shape, review the authored radii against the new extent and revalidate
stored lighting and external Level/ARCH bindings. The command leaves these values intact. A plan with zero effective
changes preserves every original byte. `runtimeVerified: false` records that no in-game check was performed.

## Publish the reviewed bytes

```text
red-agent model-native-apply relay_sentry.3do request.json <requestSha256> <outputSha256> relay_variant.3do
```

Copy both digests from the plan. The request digest covers RED's serialized parsed JSON. The output digest covers
the exact candidate bytes. Adding a no-op or reordering two disjoint operations can change the request digest while
the output digest stays the same. A stale input or either mismatched review digest requires another plan.

The source is captured before an interactive stdin request. Pass `-` in place of either JSON filename to use stdin.
Apply stages a complete sibling and rechecks captured source and request files immediately before create-only
publication. A competing destination survives. Concurrent source updates after the recheck remain possible.
Source and candidate are limited to 16 MiB each. Request JSON and serialized successful receipts are limited to
1 MiB each. An oversized receipt refuses before publication.

## MCP and discovery

| CLI | MCP | Arguments |
| --- | --- | --- |
| `model-native-inspect` | `sed_model_native_inspect` | `path`, optional `query` |
| `model-native-plan` | `sed_model_native_plan` | `path`, `request` |
| `model-native-apply` | `sed_model_native_apply` | `path`, `request`, `expectedRequestSha256`, `expectedOutputSha256`, `output`, optional `operationId` |

Get the edit schema with `red-agent schema model-native` and the page schema with `red-agent schema model-native-inspect`.
MCP publishes these same nested schemas in `tools/list` and passes requests to the same domain validator. Inputs use
the workspace or a permitted read-only resource root. Output stays within the workspace and must have a filename stem before the `.3do` extension.
A fresh `.edited.3do` is valid. The basename `.3do` has no extension.
Unicode paths also work in the `--workspace` and `--resource-root` options and in the temporary directory used to
stage an MCP request. Filesystem policy is checked on the native paths before command execution.

With a nonempty UTF-8 operation ID, an identical completed call replays the receipt. Changing the arguments with the same ID
returns `operationIdConflict`. Apply budgets for the serialized `operationId` field and the replay's
`journaled: true` field before publishing. JSON-RPC framing is separate from the domain receipt. If the journal
cannot start a write, `journalUnavailable` reports `commandExecuted: false`. If recording completion fails,
`journalOutcomeUncertain` includes the actual command result. In that case a complete output may already exist.
Use [MCP recovery and outcome lookup](agent_workflow.md#mcp-tools-paths-and-recovery) to investigate.

## Verification

The [relay-sentry native authoring check](../tests/fixtures/relay-sentry/README.md#native-authoring-readback) runs all
three commands and independently parses UV, transform and chained candidates through Blender-Sith. The automated
agent/MCP tests additionally cover exact bytes, shared/orphan mesh scope, strict schemas, stale input and review
refusal, response limits, publication races/faults, and journal recovery. Windows integration tests launch the actual
CLI and MCP executables with accented, Romanian and CJK names, compare the receipt with native directory entries and
verify that an existing destination survives a retry. The complete native workspace and
target-engine qualification remain separate acceptance gates.
