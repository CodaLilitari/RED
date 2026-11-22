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

Strict validation checks two required sections and three optional resource tables.

- The CLI requires `MATERIALS` and `GEORESOURCE`. The portable helper reports
  their presence so callers can distinguish a fragment from a level.
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

When present, `TEMPLATES`, `MODELS` and `THINGS` each require a single
`WORLD <section> <capacity>` declaration before their rows and an `END` at
the section boundary. Capacity ranges from 0 through 2,147,483,647. Empty
tables and tables filled exactly are valid. Count the authored logical rows
even if a loader could reuse a cached model or template. This release-authoring
check catches a table with two templates under `WORLD TEMPLATES 1` before the
compatibility importer can discard the second template.

Thing rows address actual slots. Under `WORLD THINGS 9`, slots 8 and 2 may
appear in that order. Slot 9 or a second row for slot 8 is an error. Thing
names may repeat. Model row labels do not select runtime slots and the scanner
leaves them uninterpreted. The scan checks capacities and Thing slot addresses.
Template inheritance, full row grammar and actual runtime allocation are outside
its scope.

For a checked section, write its header and each record on one physical line.
Use a space after `SECTION:`, as in `SECTION: TEMPLATES`. A trailing header
annotation is accepted when the game and RED both select the same table.
Use decimal capacity digits and separate the table header and leading row
fields with whitespace. Use hash comments inside these tables: a column-one
semicolon comment occupies a physical record in RED's compatibility importer.
Finish a resource table with a standalone `END`.

Preflight recognizes the game's logical-line joins, including a continuation
before an inline hash comment as described in [sound preloads](jkl_sound_preloads.md).
If the native and compatibility readers disagree on a checked section, its
logical record boundaries or its capacity, strict validation reports the
ambiguity before import. For example, `END,comment` receives
`ambiguous-terminator` and a semicolon comment in TEMPLATES receives
`ambiguous-record`. An over-budget joined row or embedded NUL stops the scan.
Diagnostics generally reference the first physical line of a logical row.

This does not fully validate surface-record semantics, all sections, resource
dependencies or runtime allocation requirements. It does not replace the
compatibility parser, automatically repair text or install a GUI pre-save gate.

Raw diagnostics include `code`, `section`, one-based physical `line`, and
`message`. EOF findings generally reference the relevant header line. Examples
include `missing-terminator`, `missing-normal`, `count-mismatch`,
`capacity-exceeded`, and the CLI's `missing-section`.

The response identifies `rawStructureScope: "materials-georesource-and-entity-tables"` and
`rawStructureStatus: "checked"` or `"incomplete"`. `checked` means the supported
scan completed, not that it passed. If raw preflight fails or is incomplete,
`valid` is false and later checks are skipped:

```json
{"rawStructureScope":"materials-georesource-and-entity-tables","rawStructureStatus":"checked","seamStatus":"not-run-raw-structure","cogReferenceStatus":"not-run-raw-structure","valid":false,"cogCompileStatus":"not-run-raw-structure"}
```

This fragment illustrates status fields, not a complete response. Fix the raw
findings and rerun before interpreting geometry or COG status.

Portable raw-preflight defaults are 128 MiB of source, 1 MiB per physical
line or joined logical row, 1,000,000 distinct Thing slots and 256 diagnostics.
The scan tracks slots as it encounters them. Declared capacity does not reserve memory. Exceeding a budget prevents successful complete inspection.
The CLI also has its normal bounded input read, which can fail before a raw
report is available. The CLI's required-section diagnostics are added after the
portable report and can add up to two rows. There is no CLI/MCP override for
these preflight budgets.

## Placed COG scripts

Supply ordered, hash-bound archives containing the scripts in the level's `COGS` placements. The same request works
with ordinary and strict validation, and inside the `request` object of MCP `sed_diagnose`.

```json
{
  "schemaVersion": 1,
  "resourceArchives": [
    { "path": "mission.gob", "sha256": "<64 hex digits for the exact archive>" }
  ]
}
```

For JK and MotS, validation compiles each distinct placed script name using the imported level's target. Classic
`VERSION 1` alone defaults to JK. A MotS marker such as `ARCHLIGHTING` identifies MotS. Case variants of a placed
name share a result. Both positional checking and compilation read the same captured source. Within each archive,
resolution tries the `cog/` path, the exact authored path, then a unique basename. An ambiguous winning tier stops
the search. A complete symbol layout is required for typed positional checks. The compiler can still diagnose
malformed script bytes when the layout is incomplete.

Read `cogCompileStatus` alongside `valid`:

| Status | Evidence |
|---|---|
| `checked` | Every distinct placed script produced a compiler verdict, including scripts with findings. |
| `incomplete` | At least one placed script was unresolved or exceeded the automatic compile budget. Other verdicts remain available. |
| `unverified-no-resources` | The level has placed scripts and no captured archives were supplied. |
| `unsupported-target` | The level has placed scripts for a target outside the JK/MotS compiler. |
| `no-placed-cogs` | The level has no placed script instances to compile. |
| `not-run-raw-structure` | Strict JKL preflight stopped before level import. |

`cogCompileSummary` counts the distinct names inspected (`scriptsTotal`), the names with retained rows
(`scriptsReturned`), and how many were `compiled`, `unresolved` or `budgetExceeded`. Its `errors`, `warnings`
and `diagnosticsTotal` include every completed compile, including sources whose rows were omitted.
`diagnosticsReturned` counts the compiler findings retained in `issues`.

`cogScripts` retains script summaries in first-placement order. Each row gives `scriptId`, `script`, `target` and
`status`. The numeric ID joins a finding to its script summary. `cogScriptNameEncoding` gives the encoding of the
`script` field in summaries and compiler findings. With `ascii-byte-escape-v1`, after JSON decoding, a literal
backslash appears as `\\`, bytes outside printable ASCII appear as `\xHH` (uppercase hex), and other printable
ASCII bytes appear directly. A name longer than 128 bytes displays its last 128 raw bytes in this encoding,
sets `scriptTruncated` and gives `scriptNameSha256` for the complete authored name at its first placement.
A `compiled` row includes
the exact `sourceSha256`, compiler `loads` verdict, error and warning counts, and `diagnosticsReturned` /
`diagnosticsTotal`. Rows marked `unresolved` or `budget-exceeded` have no load verdict. Each compiler row in
`issues` identifies the script, `code: "cog-compile"`, compiler-specific `diagnosticCode`, severity, file line and
message. Line zero denotes a finding without a single source line. A script can load and still have an error,
for example a call to an unknown verb. Both loader rejection and compiler errors set `valid: false`. Missing
evidence and warnings leave that field unchanged. The diagnostic command still succeeds with exit 0.

Automatic compilation admits 256 KiB per script and charges each attempted source at least 1 KiB against an
8 MiB aggregate allowance. It bounds recursive parser entries
at 64 and generated tree depth at 1024. The report admits up to 512 script summaries within 256 KiB and up to
4096 compiler findings within a separate 512 KiB. These allowances charge serialized JSON rows including their
delimiters. Summaries and their findings stop at the first row that exceeds the corresponding allowance.
Compare returned and total counts to detect shortened detail. Use
`cog-compile <file.cog> <jk|mots>` for the individual source report.

`cogReferenceStatus` describes the separate positional pass. Before parsing each distinct resolved name, this
pass checks the source against limits of 256 KiB, 1024 physical lines and 16 MiB of estimated declaration and
diagnostic data. Admitted estimates count toward a 32 MiB aggregate allowance. A skipped schema sets
`cogReferenceStatus` to `incomplete-schema-budget` and produces a `cogSchemaBudgetExceeded` warning for each
affected placement. Later, smaller schemas can still fit. Compilation uses its own allowance and can still
report a verdict for a script whose positional check was skipped.

A `missingScript`, `incompleteCogSchema`,
`scriptNameMismatch` or `parameterCountMismatch` issue explains why an instance's raw positional values could
not be checked. A count mismatch reports both counts. In particular, an omitted trailing value can use an engine
default while still leaving RED's exact-count binding check unverified.

The scan covers the level's placed COGs. Loose files, template-only scripts, dynamic loads, event delivery and
gameplay are outside its scope. The [archive resolver](../src/core/src/assets/cog_schema.cpp),
[level importer](../src/core/src/assets/jkl.cpp), [compiler](../src/core/src/assets/cog_compiler.cpp) and
[compiler evidence](../THIRD_PARTY_NOTICES.md#openjkdf2-cog-compiler-runtime-and-verb-signatures) describe the
source selection, target detection and grammar used here.

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
sectors, unchanged references or format preservation. Validation never mutates
and offers no repair. Acting on these observations is a separate, explicit
patch operation, [`conformSeams`](#conforming-sealed-seams), which checks each
of those conditions itself.

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
{"schemaVersion":1,"strict":true,"seamLimits":{"maxSectorPairs":256000000,"maxSurfacePairs":16000000,"maxEdgePairs":256000000}}
```

```sh
red-agent validate validation-room.jkl larger-audit-request.json
```

Place the same object inside `request` for `sed_diagnose`. Choose limits for the
specific audit rather than always selecting the maximum; larger limits permit
more work and do not guarantee completion. Responses that reach seam inspection
echo the selected three work limits (`maxSectorPairs`, `maxSurfacePairs` and
`maxEdgePairs`) in `seamLimits`; raise the one whose budget ran out. A raw-structure failure still
returns before seam inspection and does not include that echo. Check `seamStatus`
and `valid` after every run, including one with increased limits.

## Conforming sealed seams

`conformSeams` is a patch operation (`plan`, `apply`, and `sed_plan` /
`sed_apply` over MCP) that resolves the T-junctions the inspection reports. In
the editor, **Map > Conform Sealed Seams** runs the same core pass as one
undoable edit; see the [user guide](user_guide.md#-conform-sealed-seams). For
each insertion candidate it admits, it gives the target edge the corner the
neighbouring room already has there:

```json
{"schemaVersion":1,"operations":[{"op":"conformSeams"}]}
```

All fields are optional:

- `maxInsertions`: integer from 1 through 100,000; default 4,096. Needing more
  refuses the operation.
- `allowApproximate`: boolean, default false. Admits `approximate-only`
  candidates, the ones on edges that are not axis-aligned.
- `allowRemaining`: boolean, default false. Inserts what is admitted and
  reports the issues left, instead of refusing when some issue has no admitted
  candidate.
- `seamLimits`: the same three work limits, with the same bounds, as the
  `validate` request above.

The operation uses the core edge insertion rather than cutting a face, so it
differs from repairing a seam with `cleaveSurface`:

- **Topology.** No surface is split and no sector or surface is added, removed
  or renumbered. Placed COGs keep their targets, so the operation is admitted
  on a level with placed COGs, where the cleaves are refused.
- **Corner attributes.** The new corner's texture coordinates and stored light
  are interpolated along the edge it lands on, in every face that receives it.
- **Shared edges and portals.** The corner is also inserted into the faces of
  the same sector that share the edge and into adjoined mirror faces, so a room
  stays closed and both sides of a portal keep the same corners.
- **Coordinates.** An `exact-on-edge` corner takes the donor's stored
  coordinates bit for bit. An `approximate-only` corner is the donor's nearest
  point on the edge, so the two can differ by up to the 0.002 contact distance;
  the receipt reports the largest such `maxDeviation`. A difference of rounding
  size is removed in both cases.
- **Ambiguity.** `ambiguous` candidates are never inserted. Choosing between
  two nearby donor coordinates would be a weld, and that remains a decision for
  the author.
- **Rooms.** Every changed sector that was a closed convex room with planar
  convex faces before the operation is checked to still be one, and the
  operation is refused if it is not. Changed sectors that did not meet that bar
  before (imported geometry, for example) are counted in `uncheckedSectors`
  rather than certified.
- **Preservation.** Like the other geometry operations it refuses a document
  with unmodeled retained JKL state, and the result goes through the normal
  plan/apply preservation checks.

The operation is failure-atomic. Every refusal leaves the level unchanged and
publishes nothing:

| Code | Meaning |
| --- | --- |
| `seamConformUnresolved` | An issue has no admitted candidate. The message names the surface pair and how many ambiguous and approximate-only candidates were skipped. |
| `seamConformBudgetExceeded` | More than `maxInsertions` corners are needed. |
| `seamConformInspectionIncomplete` | An inspection did not complete; the message carries its status. Raise the `seamLimits` field that ran out. |
| `seamConformInsertionRefused` | The core refused an edge insertion. |
| `seamConformResultInvalid` | A room that was closed and convex would no longer be. |

Plan and apply receipts carry `seamConformReports`, one entry per `conformSeams`
operation: `operation` (its index in the patch), `issuesBefore`,
`issuesRemaining`, `insertions`, `exactInsertions`, `approximateInsertions`,
`maxDeviation`, `ambiguousCandidatesSkipped`, `approximateCandidatesSkipped`,
`inspections`, `changedSectorCount`, `changedSectors` (the first 256),
`uncheckedSectors`, and `details`: the first 256 insertions with their target
`sector`/`surface`/`edge`, `donorSector`/`donorVertex`, `point`,
`classification` and `deviation`, with `detailsTruncated` set when there were
more. The `edge` is the corner index the inspection that found the candidate
reported; earlier insertions on the same surface shift later indices.

One inspection serves every surface it reported, and a further inspection finds
the junctions those insertions expose on other neighbours, so `inspections` is
normally a small number even for hundreds of insertions. Each inspection is
charged against `seamLimits` separately.

Each inserted corner is collinear with its neighbours on that edge, so
`validate` reports additional `collinear-corners` warnings afterwards. That is
the expected result of matching a boundary, as described below. Run `validate`
after applying: the operation resolves sealed-seam findings and certifies
nothing else about the level.

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
ctest --test-dir ../red-headless-build --output-on-failure -R '^(sed_jkl_structure_tests|sed_sealed_seam_tests|sed_seam_conform_tests|sed_agent_validate-strict|sed_agent_validate-new-convert|sed_agent_mcp_protocol)$'
```

CMake target names retain the `sed_` prefix; the tools' output names are
`red-agent` and `red-mcp`. Disabling the app avoids the Qt editor dependency; it
does not establish native GUI or game acceptance. The focused command is not the
complete Release gate. Follow [build and test](README.md#build-and-test) for the
full gate and use only one writer per build tree.

## Static item placement check

Run `red-agent item-placement-check map.jkl request.json` (or use a native
SED level) to inspect 1–128 distinct zero-based Thing slots without writing
anything. The request uses `{"schemaVersion":1,"things":[0,1]}` plus
`resourceArchives` in the usual ordered `[{ "path": "...", "sha256": "..." }]`
format. Use archives for the exact JK or MotS target; there is no cross-game
stock fallback. Optional `inputSha256` or `sourceRevision` reject stale maps.
Type and model fields follow embedded template inheritance; unresolved fields
or missing winning model bytes are reported, not guessed. Before measuring,
the bounded [3DO source scanner](model_lighting.md#operational-limits) checks
the declared mesh, coordinate, face and hierarchy records. An unsupported
layout or failed check gives `modelUnresolved` without coordinates or clearance.
The row retains the identity of the winning bytes when available.

The check transforms vertices referenced by LOD0 visible bind-pose faces,
then tests one lowest vertex's XY against nearby convex, horizontal, flagged,
upward-facing non-adjoined surfaces in its sector. It ignores candidate floors
over 0.5 JKU from the authored origin. Distinct overlapping candidate heights
are ambiguous. `belowSectorFloor` means that one sample is more than 0.0001
JKU below the comparison surface; `notBelowSectorFloorAtSample` does not
clear the rest of the model. Exact model hashes identify the measured payload
(first archive/entry basename match). No game was run and no fix is offered.
### Explicit authored collision support

Optionally nominate up to 128 distinct `supportThings` slots outside `things`
for hybrid levels. This examines only those models; it does not discover
all colliders. A candidate must be a standable COG Thing with `collide=3`
(including equivalent decimal spellings such as `03` or `+3`)
and `move=none` or `move=path` without authored frames or frame capacity.
Local scripts, velocity fields, puppets, nonzero model insert offsets and
multiple geosets are unsupported. A candidate that cannot be resolved closes
the comparison for items in its sector rather than falling back silently.
A named collider requires a complete, unambiguous template chain in the saved
level. Missing parents, cycles, and conflicting definitions are unresolved.
The bounded 3DO source check also applies to the raw support model. A missing
declared mesh, a missing vertex-normal or face-normal table, or an unreadable coordinate gives `supportThingUnresolved`
and closes the floor comparison. A complete model with no eligible
upward triangle leaves the sector floor eligible for comparison.
The check uses vertex winding to select horizontal upward-facing triangles from raw geoset-zero
meshes under the placed Thing's position and rotation, including geometry mode
zero and meshes with no draw node. Native collision consumes geosets and vertex
positions and face normals in physical record order, independently of their numeric row labels.
Each triangle needs an authored face normal within 0.001 of the unit winding normal
after the Thing rotation. Missing, zero or contradictory normals give `supportThingUnresolved`.
Visual hierarchy offsets neither move these
collision triangles nor duplicate them for repeated draw nodes. The pickup's
visible lowest vertex still includes its own model hierarchy. Non-triangular
support models are unsupported.
The same nearby-height and sampled-XY constraints apply. A collider result
includes the support slot, model SHA-256 and `supportGeometryBasis: "rawGeoset0"`.
This follows the [native face-collision path](https://github.com/shinyquagsire23/OpenJKDF2/blob/545eace7ec5b0b35e18768adc6cd22653fc440ca/src/Engine/sithIntersect.c#L188-L205).
The engine's [sphere-face intersection](https://github.com/shinyquagsire23/OpenJKDF2/blob/545eace7ec5b0b35e18768adc6cd22653fc440ca/src/Engine/sithIntersect.c#L639-L648)
uses the face normal and movement direction. The sampled comparison leaves the moving contact test to the engine.

The comparison does not establish that another COG cannot move that Thing
at runtime, that the engine will use it as a floor, or that the pickup settles or
renders that way. Slopes and moving geometry are not evaluated. Use native
capture/observation to qualify the reported visual symptom.
