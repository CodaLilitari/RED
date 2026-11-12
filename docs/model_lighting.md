# Model lighting

## Scope

Ordinary 3DO lighting is an asset-authoring operation. It changes stored vertex intensity or RGB in a new model
variant, independently of world-surface baking, MotS per-Thing ARCHLIGHTING, and the viewport's dynamic-light preview.
The portable core owns inspection, deterministic lighting, and source-preserving output. The CLI and MCP adapter
share that implementation; a future GUI can call the same API without introducing another bake algorithm.

The initial workflow targets classic 3DO 2.1 scalar intensity for JKDF2 and ordinary MotS models, and 3DO 2.3 RGB for
IJIM. It does not establish DroidWorks runtime acceptance. No engine modifications or new asset format are required.

Use this workflow to author a prop's stored shading, then package the new model and explicitly bind the intended
template to it. Every instance using that asset receives the same stored lighting. Placement-specific MotS
[ARCHLIGHTING](lighting.md#mots-archlighting-authoring-and-preview) remains a separate domain.

| Request profile | Existing model version | Written channels |
|---|---|---|
| `jk` | 2.1 | Scalar intensity; admitted RGB rows must already be gray |
| `mots` | 2.1 | The same scalar contract for ordinary model assets |
| `ijim` | 2.3 | RGB vertex intensity |

The operation does not convert versions. Version 2.2, mismatched profiles, and colored classic inputs refuse.

## Headless workflow

The commands are available in a Qt-free build of `red-agent`:

```sh
red-agent model-lighting-inspect prop.3do
red-agent model-lighting-plan prop.3do relight.json
red-agent model-lighting-apply prop.3do relight.json "OUTPUT_SHA256_FROM_PLAN" prop_lit.3do
red-agent model-lighting-inspect prop_lit.3do
```

First inspect the input and copy its `inputSha256` into the request. Replace `OUTPUT_SHA256_FROM_PLAN` with the
plan's actual 64-character `outputSha256`. The model-lighting apply command takes that hash **before** the destination.
The destination must be a new `.3do` file. Planning writes no model, and applying never overwrites an existing one.
Request JSON may also be supplied on stdin by replacing `relight.json` with `-`.

For example, save the following as `relight.json`, replacing the input-hash placeholder with the inspected hash:

```json
{
  "schemaVersion": 1,
  "inputSha256": "INPUT_SHA256_FROM_INSPECTION",
  "profile": "jk",
  "blend": "replace",
  "ambient": [0.12, 0.12, 0.12],
  "lights": [
    {
      "type": "directional",
      "directionToLight": [1, -1, 2],
      "color": [1, 1, 1],
      "intensity": 0.65
    },
    {
      "type": "point",
      "position": [-0.5, 0, 0.8],
      "range": 2,
      "color": [1, 1, 1],
      "intensity": 0.2
    }
  ],
  "selfOcclusion": false
}
```

These are model-space values; choose positions and ranges for the actual model's scale. For an existing 2.3 model,
select `ijim` and use independent RGB components if desired. Inspect `effectiveLightModes` before interpreting a bake:
faces in effective modes 0, 1, or 2 (Fully Lit, Lit, Diffuse) will not display the authored Gouraud vertex-light shape.
The internal mode -1 (`VertexDiffuseOnly`) uses authored color directly in the editor. Modes are reported and preserved, never
silently changed. Models without usable vertex normals receive only ambient and any explicitly added baseline at
those vertices; `zeroNormalVertices` reports that condition.

`red-agent schema` exposes `modelLightingRequestSchema`, a JSON Schema for the request's structure and numeric bounds.
The parser also checks profile/version compatibility and equal classic RGB components. Unknown fields are errors at
every request level. Required fields are `schemaVersion`, `inputSha256`, `profile`, and `lights`; an empty light list
is valid. Defaults are `blend: "replace"`, zero ambient, `selfOcclusion: false`, `shadowBias: 0.000001`, white light
color, and light intensity 1.

RGB components are finite values in `[0,1]`, intensity in `[0,1000000]`, positions and directional components in
`[-1000000,1000000]`, point range in `[0.000000000001,1000000]`, and shadow bias in `[0.000000000001,1000]`.
Directions must be nonzero and are normalized by the core. The bias is a model-space endpoint exclusion distance.

### Agent receipts and publication

Inspection returns the input hash and per-LOD/source-mesh counts, render-instance counts, stored light ranges,
effective face-mode counts, missing/zero/non-unit normals, and saturated vertices. Planning and application return
`before`/`after` inspections, `changedVertices`, `clampedVertices`, `occludedSamples`, `zeroNormalVertices`, the effective
limits, and input/request/output hashes. `requestSha256` hashes canonical request JSON. A successful receipt keeps
`runtimeVerified: false`: it certifies the operation's stored-data result, not an observed frame in a game.

Apply recomputes the bake and compares its output hash with the reviewed plan. It uses the agent's existing bounded
read set and create-only atomic publication; a changed input or request file detected before publication leaves no
new model. These are consistency checks, not a filesystem lock. If a request or source changes, plan again.

| Exit | Meaning for this workflow |
|---|---|
| 0 | Successful inspection, plan, or new-file publication |
| 2 | Invalid request, unknown field, malformed hash, unsupported profile name, or wrong arguments |
| 3 | Input could not be read, or inspection refused unsupported source structure |
| 4 | Stale input, source changed before publication, or output differs from the planned hash |
| 5 | Bake admission/calculation refusal, incompatible version/profile, exhausted budget, or occupied destination |

The detailed `errorCode` distinguishes `modelLightingInvalidRequest`, `modelLightingInputUnsupported`,
`modelLightingStaleInput`, `modelLightingPlanMismatch`, and `modelLightingRefused`; common file-publication errors
retain their existing agent codes. A plan refusal publishes nothing.

### MCP

`red-mcp` advertises the same request schema through three tools:

| Tool | Arguments |
|---|---|
| `sed_model_lighting_inspect` | `path` |
| `sed_model_lighting_plan` | `path`, `request` |
| `sed_model_lighting_apply` | `path`, `request`, `expectedOutputSha256`, `output`, optional `operationId` |

Paths are relative to the selected workspace. Reads may also use configured resource roots; outputs must remain in
the workspace. Existing symlink targets are checked when resolving those paths. This policy is not an OS sandbox or
a guarantee against concurrent filesystem changes. Use an `operationId` for journaled mutation and `sed_outcome` to
check an interrupted operation instead of blindly retrying it. A completed journal replay returns the recorded
result; it does not revalidate a file that may have changed since that original operation.

The internal journal must use an ordinary `.sed-mcp` directory and a regular `journal.jsonl` file. Symlink paths,
unreadable or damaged history, and failed initial append/flush/close refuse with `journalUnavailable` and
`commandExecuted: false`. A failure to record completion after the domain command returned reports
`journalOutcomeUncertain`, `commandExecuted: true`, and the exact `commandResult`; check that result and the output
before recovery. Journaling does not provide interprocess locking or power-loss durability.

## Preservation

Relighting must preserve every LOD, hierarchy record, material, UV, mode, alpha value, comment, unknown section, and
line ending. The general one-geoset 3DO writer is not used. A strict scanner identifies lighting token spans in the
original bytes, validates source structure, and replaces only the admitted intensity/RGB tokens. A no-op returns the
original bytes. Every LOD is evaluated independently. Unreferenced meshes retain their original lighting.

Scalar rows and gray RGBA rows are accepted for version 2.1; version 2.3 requires RGBA rows. Alpha is preserved
verbatim. Admission is deliberately narrower than viewport loading: row indices must be sequential, known sections
unambiguous, counts and references valid, and numbers finite. Unsupported layouts refuse rather than being rewritten
through a more permissive reader. Unknown sections are retained without being interpreted as geometry.

Different hierarchy instances sharing one stored source mesh cannot have independent per-instance intensities. The
initial bake refuses that ambiguity and explains that the mesh must first be made unique. It does not average those
instances or discard geometry. Existing general-writer loss-prevention guards remain in place.

## Evaluation

Lights are expressed in model coordinates in the bind pose, after the model's authored hierarchy transforms.
Directional lights specify a direction from the sample toward the light. Point lights use a finite positive range,
a squared linear falloff, and a normal-facing Lambert term. A supplied ambient value is a stored additive baseline.
Replacement is the default so repeating a recipe does not progressively brighten the model. Explicit additive mode
starts from the source's stored values. Generated RGB/intensity is clamped to the supported 0..1 output range.

Optional self-occlusion uses same-LOD, transformed, drawn model faces as opaque two-sided triangle proxies. It is not
texture-alpha-aware and does not include level geometry, other Things, animation, or dynamic runtime shadows. Work
budgets and cancellation prevent an unbounded sample/light/triangle product. Unsupported polygon layouts refuse
instead of being silently triangulated incorrectly.

Classic scalar vertex intensity is additive, not multiplicative ambient occlusion. Sector ambient can flatten its
contrast; runtime light, face extra light, fullbright/Diffuse mode, and Gouraud-distance switching can change the final
game result. The inspector reports stored data and mode/normal diagnostics, not an observed rendered frame.

For each vertex, replacement computes ambient plus the sum of non-occluded contributions. Each contribution is
`color * intensity * max(0, dot(unitNormal, directionToLight))`. Point lights additionally multiply by
`(1 - distance/range)^2` inside their range; a point exactly on the sample contributes zero. Additive mode also includes
the original stored vertex light. The accumulated channels are clamped to `[0,1]` and rounded to float. Missing or
zero normals contribute no directional term. No face-extra, alpha, mode, geometry, or normal field is changed.

### Operational limits

Limits describe this authoring operation, not retail engine maxima. An optional request `limits` object may lower
individual ceilings with positive integers; it cannot raise them.

| Field | Default and hard ceiling |
|---|---:|
| `sourceBytes` | 16777216 (also limits output bytes) |
| `lines` | 262144 |
| `tokensPerLine` | 4096 |
| `geosets` | 32 |
| `meshes` | 4096 |
| `vertices` | 262144 |
| `triangles` | 524288 |
| `hierarchyNodes` | 4096 |
| `lights` | 256 |
| `workUnits` | 50000000 |

Work admission charges source scans/loads, hierarchy transform visits, lighting samples, and the full potential
sample/light/triangle product before shadow tracing. Work units are a conservative algorithm budget, not elapsed
time. Multiple LODs and large opaque-shadow requests may exhaust it even when individual size ceilings fit. The
existing 3DO loader also retains its independent parser/storage/transform limits.

## Import normals

Model import preserves authored normals and deliberate hard edges by default. OBJ normal/smoothing boundaries and
GLB normal discontinuities can require separate geometric vertices in a 3DO. UV seams alone do not require that split.
The explicit recalculation policy derives area-weighted geometric normals. Mesh splitting and target limits are
applied after normal discontinuities have been represented.

`model-import` accepts `normalPolicy: "preserve"` (the default) or `"recalculate"` and returns that policy plus
`normalSplitVertices`. The latter counts extra geometric vertices required by normal boundaries before mesh
partitioning can duplicate seam vertices. Authored normal directions are normalized; their magnitude is not a light
intensity. OBJ `vn` takes precedence over smoothing groups, `s off` is flat, positive `s` groups smooth only within the
same group, and unspecified smoothing retains the existing smooth geometric fallback. Missing or zero authored
normals fall back to geometric normals. GLB normal transforms use inverse transpose; mirrored nodes retain the
existing winding correction.

The explicit recalculation policy ignores authored normals and OBJ smoothing groups. It derives normals from face
areas, so it can deliberately remove a hard edge. Source meshes split to engine limits by default; `split: false`
refuses when a normal split or source geometry exceeds those limits. The emitted model remains 3DO 2.1.

## Core API and GUI integration

[`model_lighting.hpp`](../src/core/include/sed/assets/model_lighting.hpp) exposes `inspectModelLighting` and
`bakeModelLighting`. Both operate on immutable byte spans without Qt, OpenGL, filesystem access, or application state.
The bake returns patched text and before/after diagnostics. `ModelLightingOptions::cancelled` supports a caller's
cooperative cancellation callback; cancellation and admission failures throw without a partial published result.
The CLI's filesystem and JSON concerns remain in `tools/sed-agent/agent_model_lighting.cpp`.

A GUI integration should call this same core on a background worker with an immutable source snapshot. Bind the
pending result to the resource identity/hash, request options, and document generation, and discard it when any of
those change. Keep worker diagnostics distinct from a renderer preview. Present the original and proposed lighting
with the same camera and runtime lighting settings so ambient or mode changes cannot masquerade as a bake difference.

Save an explicit new asset variant, then make any selected template/Thing binding changes as one validated document
transaction with undo/redo and cache invalidation. Canceling must preserve both the original asset and document.
Do not route this result through the general one-geoset model writer, and do not turn a new asset bake into an
implicit edit of every Thing sharing a template. The current branch exposes the portable API and headless workflow;
it does not add a model-lighting GUI panel.

## Verification boundary

Synthetic core and CLI/MCP tests must establish analytic light values, source preservation, all-LOD behavior,
hierarchy transforms, occlusion, strict admission, cancellation, deterministic replacement, stale-input rejection,
and create-only publication. GUI wiring, native visual comparisons, Windows integration, and retail-game acceptance
remain separate verification steps and must not be inferred from a successful parser round trip.

The focused core target is `sed_model_lighting_tests`. CLI cases include `sed_agent_model-lighting`,
`sed_agent_model-import-normals`, and `sed_agent_input_safety`; MCP cases include `sed_agent_mcp_model-lighting` and
`sed_agent_mcp_path-policy`. Journal refusal and completion failures are covered by `sed_agent_mcp_journal-policy`
and `sed_agent_mcp_journal_completion`. All fixtures are synthetic. Run the full configured Release CTest suite after the focused
checks, and keep Windows/Qt, native visual, and retail-game evidence separate in the review record.
