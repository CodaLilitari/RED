# Headless validation

`red-agent validate` and the `red-mcp` tool `sed_diagnose` inspect a level without
rewriting it. Validation findings, successful serialization, editor rendering and
actual game acceptance are separate evidence. A successful command is not a
certificate of collision safety, multiplayer balance or resource completeness.

See [agent workflow](agent_workflow.md) for source hashes and reviewed edits,
[design operations](agent_design.md) for geometry admission, and
[game testing](game_testing.md) for runtime checks.

## CLI and MCP requests

```text
red-agent validate <path> [request.json|-] [--strict]
```

The optional request is a JSON object. `-` reads it from standard input. Strict
mode can be enabled with either `--strict` or the boolean request field `strict`.
If either is true, strict mode runs; `strict: false` does not override the flag.
Repeating `--strict`, supplying a nonboolean `strict`, or supplying extra
positional arguments is a usage error. Existing requests without strict mode
retain compatibility loading.

For a synthetic example, use a fresh filename in a working directory outside
the source checkout, with the executables on PATH:

```sh
red-agent new validation-room.jkl jk
red-agent validate validation-room.jkl
red-agent validate validation-room.jkl --strict
```

This creates a room scaffold, not a playable mission. Its placeholder material
and colormap names do not supply game assets. To use a request file, save the
following as `validation-request.json`:

```json
{"schemaVersion":1,"strict":true}
```

```sh
red-agent validate validation-room.jkl validation-request.json
```

On a POSIX shell, the equivalent standard-input request is:

```sh
printf '%s\n' '{"schemaVersion":1,"strict":true}' | red-agent validate validation-room.jkl -
```

For an initialized MCP connection, call `sed_diagnose` with these arguments,
using a path inside the configured workspace:

```json
{"path":"validation-room.jkl","request":{"schemaVersion":1,"strict":true}}
```

The adapter forwards the request to the same validation implementation; it does
not implement a second parser or repair path. Optional `inputSha256` and
`sourceRevision` bindings retain their existing conflict checks. Strict mode
checks the captured raw bytes before compatibility import and imports that same
captured byte string if preflight succeeds.

Read the JSON result as well as the process exit code. Findings can return exit
code 0 with `ok: true` and `valid: false`: the command ran successfully and found
problems. Usage, input, conflict and refusal failures retain the exit-code
contract in [agent workflow](agent_workflow.md).

## Strict raw-JKL framing

Strict mode is an additive preflight for classic JKDF2/MotS JKL text. It is not
a native `.sed` or IJIM `.ndy` strict validator. Renaming another format to `.jkl`
does not make it supported. The CLI refuses non-JKL filename extensions; files
without an extension are also eligible for raw-text inspection.

The supported scope is `MATERIALS` and `GEORESOURCE` framing:

- The CLI requires both sections. The portable helper separately reports which
  sections were present, so helper success alone does not prove a complete file.
- `MATERIALS` requires its sentinel `END` before the next section or EOF.
- `GEORESOURCE` does not require `END`. Its ordered headers, exact record counts
  and grouped normals must be complete at the next section or EOF; an explicit
  `END` is also accepted. Omitting this optional terminator is not a defect.
- `WORLD MATERIALS` is a capacity. Material rows may fill it exactly; **zero
  spare slots are valid**. Runtime resource needs must be assessed separately.
- Classic `GEORESOURCE` requires `WORLD COLORMAPS`, `WORLD VERTICES`,
  `WORLD TEXTURE VERTICES`, `WORLD ADJOINS` and `WORLD SURFACES` in that order.
  Their declared record counts are exact. Missing COLORMAPS does not select a
  different dialect.
- Surface records must be followed by a complete grouped block of normal rows
  containing a prefix label and finite XYZ values. The engine ignores these
  prefix labels; the check does not assign them surface-index meaning or require
  index ordering. This is a framing check, not proof that normals are unit length,
  correctly wound or geometrically correct.
- Duplicate sections/count headers, invalid counts and count-order problems are
  reported. Unknown sections and COPYRIGHT do not acquire invented terminator
  rules.

This does not fully validate surface-record semantics, all sections, resource
dependencies or runtime allocation requirements. It does not replace the
compatibility parser, automatically repair text or install a GUI pre-save gate.

Raw diagnostics include `code`, `section`, one-based physical `line`, and
`message`. EOF findings generally reference the relevant header line. Examples
include `missing-terminator`, `missing-normal`, `count-mismatch`,
`capacity-exceeded`, and the CLI's `missing-section`.

The response identifies `rawStructureScope: "materials-and-georesource"` and
`rawStructureStatus: "checked"` or `"incomplete"`. `checked` means the supported
scan completed, not that it passed. If raw preflight fails or is incomplete,
`valid` is false and later checks are skipped:

```json
{"rawStructureScope":"materials-and-georesource","rawStructureStatus":"checked","seamStatus":"not-run-raw-structure","cogReferenceStatus":"not-run-raw-structure","valid":false}
```

This fragment illustrates status fields, not a complete response. Fix the raw
findings and rerun before interpreting geometry or COG status.

Portable raw-preflight defaults are 128 MiB of source, 1 MiB per physical line,
and 256 diagnostics. Exceeding a budget prevents successful complete inspection.
The CLI also has its normal bounded input read, which can fail before a raw
report is available. The CLI's required-section diagnostics are added after the
portable report and can add up to two rows. There is no CLI/MCP override for
these preflight budgets.

## Bounded sealed-seam diagnostics

Ordinary and strict validation both run sealed-seam inspection after a successful
load. Exactly matching sealed walls are not seams. The legacy diagnostic checks
partial collinear edge overlap between coplanar faces in different sectors and
excludes a pair when both faces have adjoins. It is not a complete solid-flag,
portal or manifold checker.

Each `sealed-seam` issue retains `sector`, `surface`, `otherSector` and
`otherSurface`. These are current zero-based addresses, not durable identities.
An issue represents one surface pair, not a count of vertices to insert.
`edgeContacts` lists `edge` and `otherEdge` corner indices for the overlapping
directed edges. Pair and edge traversal order are deterministic.

`insertionCandidates` adds observations, not a repair plan approved for mutation:

- `sector`, `surface`, `edge` identify the target edge.
- `donorSector`, `donorVertex` identify the existing source vertex.
- `point` is the **original donor coordinate**, never a snapped/projected point.
- `classification` is `exact-on-edge`, `approximate-only` or `ambiguous`.

`exact-on-edge` currently certifies only axis-aligned edges with identical
transverse coordinates and strict interval containment. Oblique points can
remain `approximate-only` even when mathematically collinear. Diagnostic proximity
alone is insufficient to authorize welding. Distinct nearby donor coordinates
proposing the same edge are `ambiguous`; identical coordinates are not. There is
no transitive clustering or automatic choice of a winning coordinate.

Every issue sets `repairAuthorized: false`. Even an exact observation does not
prove safe UV/light interpolation, reciprocal-adjoin propagation, closed convex
sectors, unchanged references or format preservation. No `fix-seams` mutation
command is provided by this workflow.

The preserved diagnostic thresholds are 0.002 world units for point, line and
plane proximity, and 0.001 **dimensionless edge parameter** for overlap. These
are diagnostic heuristics, not edit tolerances or grid spacing.

Always check `seamStatus`. `complete` means this scan finished. Other statuses
include `budget-exceeded`, `invalid-geometry`, `arithmetic-unresolved` and
`allocation-failure`; a defensive `not-implemented` status is also represented.
Incomplete scans add `sealed-seam-scan-incomplete` and make `valid` false. Their
findings are only a prefix, and candidate classifications remain provisional.
An empty incomplete report must never be presented as a clean level.

Current aggregate per-call defaults are:

| Budget | Limit | Charged work |
| --- | ---: | --- |
| `maxCorners` | 1,000,000 | Sector, vertex, surface and referenced-corner records |
| `maxSectorPairs` | 64,000,000 | One bounds test for every pair of sectors, including empty and distant ones |
| `maxSurfacePairs` | 1,000,000 | Surface pairs whose bounds, grown by the contact distance, meet |
| `maxEdgePairs` | 16,000,000 | Surface bounds tests, plane-corner work, edge pairs, endpoint observations and same-target candidate comparisons |
| `maxIssues` | 100,000 | Surface-pair findings |
| `maxContacts` | 200,000 | Edge contacts |
| `maxInsertions` | 200,000 | Candidate observations |

A broad phase compares sector bounds, then surface bounds, before any geometric
test. Two faces can share an overlapping edge only where their bounds meet, so
it changes which pairs cost work and never which findings are reported. With
it the defaults complete on the shipped JK multiplayer levels and on community
levels of about two thousand sectors.

Very large or densely overlapping inputs can still exhaust the default budgets.
Returning an incomplete result is intentional; no automatic global repair
follows it. To rerun a larger read-only audit, CLI and MCP requests accept a
bounded `seamLimits` object with only these optional fields:

- `maxSectorPairs`: integer from 0 through 1,000,000,000; default 64,000,000.
- `maxSurfacePairs`: integer from 0 through 32,000,000; default 1,000,000.
- `maxEdgePairs`: integer from 0 through 256,000,000; default 16,000,000.

Despite its legacy name, `maxEdgePairs` is the shared geometric/arithmetic work
budget described in the table, including plane corners, endpoint observations
and ambiguity comparisons. It does not count only literal edge pairs. Admission
and output limits remain fixed in CLI/MCP. Unknown fields, non-object
`seamLimits`, nonnumeric values, fractions and out-of-range values are refused.
Omitted fields retain their defaults; zero is a valid explicit work budget, not
a request for unlimited work.

For example, after a default audit reports budget exhaustion, save this as
`larger-audit-request.json` for a larger bounded audit:

```json
{"schemaVersion":1,"strict":true,"seamLimits":{"maxSurfacePairs":16000000,"maxEdgePairs":256000000}}
```

```sh
red-agent validate validation-room.jkl larger-audit-request.json
```

Place the same object inside `request` for `sed_diagnose`. Choose limits for the
specific audit rather than always selecting the maximum; larger limits permit
more work and do not guarantee completion. Responses that reach seam inspection
echo the selected two work limits in `seamLimits`. A raw-structure failure still
returns before seam inspection and does not include that echo. Check `seamStatus`
and `valid` after every run, including one with increased limits.

## Collinear corners and engine evidence

Forward-collinear boundary subdivisions can be valid and can preserve matching
shared boundaries. Do not blindly remove them, reject every such polygon, or
grid-split the whole level to eliminate a warning. Removing a corner can recreate
a T-junction or discard authored UV/light information.

Rendering algorithms differ by engine build and rendering path. A zero-area
triangle does not by itself prove an entire face disappears, and a universal
vertex-0 fan is not assumed here. This workflow adds neither a blanket collinear
invalidity rule nor an engine-specific triangulation advisory. Diagnose actual
missing faces against the affected geometry and identified runtime separately.

## Build and focused tests without Qt

The existing options are `SED_BUILD_APP`, `SED_BUILD_AGENT` and `SED_BUILD_MCP`;
there is no new `RED_BUILD_GUI`, `seams` or `solid` build alias. From the source
root, a single-configuration Ninja example using a sibling build directory is:

```sh
cmake -S . -B ../red-headless-build -G Ninja -DCMAKE_BUILD_TYPE=Release -DSED_BUILD_APP=OFF -DSED_BUILD_AGENT=ON -DSED_BUILD_MCP=ON -DSED_BUILD_UPDATE=OFF -DSED_BUILD_PLUGIN_BRIDGE=OFF
cmake --build ../red-headless-build --target sed_core_tests sed_agent_tests sed_mcp_tests --parallel 1
ctest --test-dir ../red-headless-build --output-on-failure -R '^(sed_jkl_structure_tests|sed_sealed_seam_tests|sed_agent_validate-strict|sed_agent_validate-new-convert|sed_agent_mcp_protocol)$'
```

CMake target names retain the `sed_` prefix; the tools' output names are
`red-agent` and `red-mcp`. Disabling the app avoids the Qt editor dependency; it
does not establish native GUI or game acceptance. The focused command is not the
complete Release gate. Follow [build and test](README.md#build-and-test) for the
full gate and use only one writer per build tree.
