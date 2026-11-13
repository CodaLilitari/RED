# RED agent workflow

`red-agent` and `red-mcp` share the Qt/C++ editor's level model and core operations. They are distinct implementations from the older .NET SED-MotS agent. Use the executable's current `schema` response, this workflow, and focused format documentation together. An operation listed in another editor's schema is not automatically available here.

## Discovery and execution

Run `red-agent schema` to obtain the compatible command/operation inventory, request-size limits, exit codes and
snapshot scope. The shared registry supplies 63 patch operations and 42 CLI commands. Focused discovery requires no
level file:

```text
red-agent schema commands
red-agent schema patch
red-agent schema fitSurfaceTexture
red-agent schema dependencies
```

Each focused response is a standalone Draft 2020-12 JSON Schema. `commands` validates a command name and includes
exact positional usage and descriptions in `x-commands`; it does not claim a body schema for every CLI command.
`patch` describes the request envelope and all operation alternatives, while an exact operation name returns that
operation's required fields, nested objects, alternatives and bounds. `dependencies` returns the shared dependency
request schema, also exposed as `dependencyRequestSchema` in the no-topic inventory. Runtime checks still decide
geometry validity, resource compatibility, source identity and cross-field arithmetic; follow the returned
`x-runtimeConstraints`.
Unknown topics and extra topic arguments are usage errors. No-topic inventory behavior remains compatible, including
existing domain schemas such as `modelLightingRequestSchema`.

Run `red-agent help` for the principal command argument order. The CLI currently has no `--version` contract; record
the executable SHA-256 and source revision with a durable run. Invoke it with an argument array, capture stdout and
stderr, check the exit code, then parse its JSON stdout. Send UTF-8 request JSON on stdin using `-`, or use an explicit
request file.

Exit codes are 0 success, 2 invalid request/usage, 3 input failure, 4 stale input/conflict, and 5 refusal. A structured
`code` / `errorCode` refines some failures. These meanings differ from the .NET agent's codes. Existing optional hashes
and permissive legacy fields remain compatible; new operation families can reject unknown fields. Use the schema for
the actual operation instead of assuming one universal unknown-field policy.

Requests are limited to 1 MiB while reading and source files to 128 MiB. Captured archives share a 512 MiB byte budget and 100,000-entry budget across one command; index admission occurs before the core parser allocates its entry table. Materializing COG/package archive sets additionally limits aggregate entry payloads to 512 MiB, including overlapping payload ranges. Lighting snapshots share 256 MiB and 4,096 resource lookups across all bakes in a command, including missing lookups. These are agent admission limits, not new format limits on the general core archive API. JSON rejects duplicate object fields, malformed numbers and unescaped control characters; Unicode escape pairs decode as UTF-8 scalars. Invalid integer selectors, flags and overflowing float values refuse before narrowing.

Keep generated levels, build trees, render output and request/result receipts outside source checkouts. Building does not install a new release over an existing distribution.

## Inspect, plan, apply, reopen

1. Inspect the exact input with `inspect` and `statistics`. Query bounded geometry, Thing, light, template, COG or ARCH pages and use `select` for spatial targets. Accept a spatial selection only when `match` is `unique`; preserve the returned revision and target.
2. Prepare a patch with `schemaVersion: 1`, the returned `inputSha256` and `sourceRevision`, and an `operations` array. Obtain the exact field contract from `schema patch` or `schema <operation>`; the generic inventory is not a promise to accept arbitrary legacy fields.
3. Run `plan <input> <patch.json|->`. Review both `semanticPreview` views and their completeness fields, then keep the returned `outputSha256` with the request. Planning performs no output publication.
4. Run `apply <input> <patch.json|-> <new-output> <outputSha256>`. A new output must use the intended file format; use `convert` for a reviewed format change. If input, resources or operations change, plan again.
5. Reopen the actual output with `inspect`, query the edited fields, and run `validate`. After geometry changes, obtain fresh spatial selections. Check both ends of every adjoin and the intended room path.

The CLI's legacy optional-hash fields remain accepted for compatibility; workflows should supply the hashes and reviewed output argument. Supplying no hash does not establish that a previous plan was approved or remains current.

`semanticPreview.authored` shows net changes to the isolated patch candidate. `semanticPreview.persisted` shows changes
present after reloading the exact proposed file, including writer rounding and normalization. Both compare against the
same captured input and repeat the exact source/output hashes. Their paths address storage positions in each snapshot;
an array index is not a stable entity identity across topology changes. `comparisonComplete: false` means the scan
stopped, `totalChanges` is null and observed changes are only a lower bound. `detailsComplete: false` can also mean a
complete scan with shortened rows or omitted string values. Read `reasons` and the declared exclusions before treating
a result as unchanged. [Semantic change previews](agent_design.md#review-semantic-changes) describes the limits and
typed values. This receipt is available for generic plan/apply and MCP `sed_plan`/`sed_apply`; specialized command
receipts keep their existing contracts. Replayed MCP operations return the original recorded receipt, including old
receipts created before previews existed.

Detailed `query` pages include source hashes, `total`, `offset` and `nextOffset`; follow `nextOffset` until null while
binding each request to the same revision. `sectors`, `templates`, `cogs`, `architecture`, `architecturalVertices` and
`thingContainment` expose stored records or explicitly qualified analysis. For example:

```json
{"collection":"sectors","offset":0,"limit":20}
```

```json
{"collection":"cogs","search":"door","offset":0,"limit":20}
```

These are separate bodies for `red-agent query next.sed <request.json|->`. See
[detailed readback](agent_design.md#read-back-the-edited-records) for every collection, required ARCH selectors, page
budgets and null/unknown semantics. A default page holds 50 records and a requested page is capped at 200. Reduce the
page size or use a more focused collection after `queryDetailsTooLarge`; do not read a refused page as an empty
collection. Template rows contain
declarations, COG rows remain unverified until resource-aware `bindings`, and ARCH sample rows remain unverified until
resource-aware `arch-validate`. Point containment is separate from collision-volume clearance and gameplay.

Agent output uses exclusive sibling staging, complete write/flush/close, then an OS create-only publication. A competing destination is preserved. On POSIX, filesystems without the required hard-link operation fail closed. This is separate from GUI/core asset saves whose explicit contract replaces an existing file.

The command retains a read set for files captured through its bounded input path. After staging and before publication it verifies those files again; changed or unavailable sources cause a conflict and leave no output. Archive consumers parse the admitted byte snapshot rather than reopening a mutable archive for each entry. Lighting bakes separately retain bounded MAT/CMP lookup results, including missing results, and recheck loose-file bytes and winner identity before publication. Captured archive files remain in the source read set. These checks are a pre-publication consistency guard, not a filesystem lock or a claim of full template/resource closure for every importer.

## MCP tools, paths and recovery

`red-mcp` serves newline-delimited JSON-RPC on stdio. Start it with `--workspace <dir>` and any explicitly permitted
read-only `--resource-root <dir>` values. Its `tools/list` catalog contains 40 routes:

| Domain | MCP tools |
| --- | --- |
| Discovery and document summary | `sed_discover`, `sed_schema`, `sed_inspect`, `sed_statistics` |
| Readback and diagnostics | `sed_query`, `sed_select`, `sed_diagnose`, `sed_workbench_query`, `sed_arch_validate` |
| Level publication | `sed_plan`, `sed_apply`, `sed_new`, `sed_convert` |
| Archives and packages | `sed_archive`, `sed_archive_create`, `sed_dependencies`, `sed_package_plan`, `sed_package_apply` |
| Observation | `sed_snapshot`, `sed_overview` |
| COGs | `sed_bindings`, `sed_cog_check` |
| Prefab and DXF | `sed_prefab_capture`, `sed_export_dxf`, `sed_dxf_inspect`, `sed_dxf_import` |
| Materials | `sed_mat_inspect`, `sed_mat_export_png`, `sed_mat_create` |
| Models | `sed_model_import`, `sed_terrain_3do`, `sed_model_lighting_inspect`, `sed_model_lighting_plan`, `sed_model_lighting_apply` |
| Animation | `sed_key_inspect`, `sed_key_plan`, `sed_key_apply` |
| Launch planning, native inspection and recovery | `sed_launch_plan`, `sed_native_inspect`, `sed_outcome` |

`sed_discover {}` and `sed_schema {}` return the inventory. A `topic` of `commands`, `patch`, `dependencies`, or an exact
operation returns focused discovery. The compatible `sed_discover {"path":"level.sed"}` form still inspects a document;
`sed_inspect` is the explicit inspection route. A discovery call cannot combine `path` and `topic`, and `sed_schema`
does not accept a document path. For example:

```json
{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"sed_schema","arguments":{"topic":"patch"}}}
```

`sed_plan` and `sed_apply` embed the typed patch schema in their tool input schemas; `sed_dependencies` embeds the
same closed request schema returned by `schema dependencies`. Other domains retain their own command contracts;
the existence of a generic `request` object in a tool catalog does not type every nested
field. `addCog` and `rebindCogReference` are available as patch operations through plan/apply rather than additional
standalone MCP aliases. `sed_archive` combines archive listing and exact-entry extraction.

Relative document, resource and output paths resolve against the configured workspace, independently of the process
working directory. Read paths must stay in the workspace or a declared resource root; writes must stay in the
workspace. MCP resolves existing path components before checking membership, including symlink targets. The same
policy covers documented nested paths: captured archives/templates, prefab inputs, baking resource roots/GOBs, model
validation inputs, package/dependency archives, MAT cels/palette, model source/MAT files and optional base-color output,
and `overview`'s `request.output`. Resource-directory admission checks the root and each resource actually read; unused
descendants are not recursively scanned. Repeated root declarations therefore do not trigger repeated whole-tree walks.
Ordinary level parameter strings are not rewritten
merely because their name resembles a path.

Resource-aware `sed_diagnose` checks its captured archive paths as well. During a lighting bake, policy also applies
to the actual resolved loose MAT/CMP files named by the document, before their bytes are read. Publication rechecks
the capture's policy and winning resource identity; changed or disallowed resolution conflicts or refuses. Direct CLI
invocations retain their existing filesystem access. These checks are application-level admission, not an
OS sandbox or protection against every filesystem race.

For `sed_launch_plan`, relative `gameDirectory` and `projectDirectory` use the workspace. A relative `executable`
uses the resolved game directory, matching the CLI launch planner, and the resulting path still passes read policy.
This tool prepares a launch plan without starting the game. `sed_native_inspect` requires a supported Windows host,
an explicit worker configuration, and its verified adjacent `sed-cpp-bridge.exe`; it is not a live GUI editing bridge.

Optional `operationId` journaling requires ordinary journal paths and intact JSONL history. A journal failure before
the domain command runs returns `journalUnavailable` and `commandExecuted: false`. A failure to record completion
after it ran returns `journalOutcomeUncertain`, `commandExecuted: true`, and the actual `commandResult`; inspect that
result and output before recovery. Use `sed_outcome` after an interrupted request. This protocol does not add an OS
sandbox, interprocess locking, or power-loss durability.

File-producing routes use the same optional journal, including asset import/export, prefab capture, snapshot PNGs,
and an overview with `request.output`. A camera-only overview remains read-only. Repeating a completed operation ID
with the same tool arguments returns its recorded result without executing the write again; different arguments
conflict. A started or failed record is not silently retried. Retain the original arguments and inspect the outcome
before choosing a new operation ID.

## Dependency inspection

`red-agent dependencies <level.jkl> <request.json|->` and MCP `sed_dependencies` inspect captured raw JKL and an
explicitly ordered set of hash-bound GOB/GOO archives. The request requires `schemaVersion: 1`, `inputSha256` and
`archives`; each archive requires `path` and `sha256`, with optional `stock` (default false). An empty archive array is
valid. Optional `extraResources` adds `{name, folder}` logical roots, and `limits` selects operational bounds.
Unknown fields and non-JKL inputs are usage errors. The command does not load native workspaces, discover loose
files, assemble a package or publish output. MCP accepts only `path` and `request`, rejecting `output` and
`operationId`; its normal workspace/resource-root and symlink rules cover the level and every archive.

For example, run this POSIX-shell command from a directory containing `mission.jkl`, `project.gob` and `stock.gob`.
It computes their raw SHA-256 values and passes the request on stdin; change the ordered archive list to match the
intended sources:

```sh
python3 - <<'PY' | red-agent dependencies mission.jkl -
import hashlib
import json
from pathlib import Path

def sha256(path):
    digest = hashlib.sha256()
    with Path(path).open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()

print(json.dumps({
    "schemaVersion": 1,
    "inputSha256": sha256("mission.jkl"),
    "archives": [
        {"path": "project.gob", "sha256": sha256("project.gob")},
        {"path": "stock.gob", "sha256": sha256("stock.gob"), "stock": True}
    ],
    "limits": {"maxDetailRecords": 128}
}))
PY
```

For MCP, use `{"path":"mission.jkl","request":...}` with the same request object. Read failures return exit 3;
source/archive hash mismatches return exit 4. A valid diagnostic report returns exit 0 and `ok: true` even when
references are missing or a limit stops the scan. Check these independent receipt fields:

| Field | Meaning |
| --- | --- |
| `scanComplete` | The supported traversal finished within its admitted work. A budget stop makes it false; read `scanReasons`. |
| `detailsComplete` | All graph/issue rows fit the detail allowance. A complete traversal can still return shortened detail; read `detailReasons`. |
| `allReferencesResolved` | False after an examined missing/ambiguous reference; null if scanning stopped before a clean conclusion; otherwise true. Unsupported dependency readers remain separate scopes. |
| `runtimeClosureVerified` | Always false. Resolution and lexical coverage do not prove runtime closure or game acceptance. |
| `nodesObserved` / `nodesTotal` | Observed counts are lower bounds and totals are null when `scanComplete` is false. The same pairing applies to `edges`, `issues`, `missing`, `ambiguous` and `unsupported`; detail truncation alone preserves exact totals. |

Node zero is the level, with no generated episode node. Every admitted occurrence records an edge with `parent`,
`target`, `relation`, `authoredName`, `folder` and `reused`; resolved resources are traversed once in breadth-first
order. Stock resources still have their dependencies inspected. A node's `sha256` and `provenance` identify the exact
winning payload: `archiveOrdinal`, `entryOrdinal`, `entryName`, `offset`, `bytes` and `lookupTier` (`folderExact`,
`authoredExact` or `basename`). `sourceArchives` separately records each archive's ordinal, label, raw-file hash,
byte length, physical entry count and stock flag. Exact lookup across the ordered archives precedes basename fallback.
The inspector reads the selected physical row; the legacy package adapter still materializes by name, so leading-slash
aliases can yield different payloads. Archive raw-file hashes also differ in meaning from the package manifest's
normalized-entry archive digests. These receipts identify captured bytes, without an atomic multi-file snapshot or an end-of-read guarantee
that the current paths still contain those bytes.

Supported roots are JKL resource tables/materials, template resource parameters, placed COG scripts, colormaps and
explicit extras. Transitive readers follow 3DO material tokens, PUP keyframes, SND sounds, SPR materials and resource
defaults from complete COG schemas, including local symbols. KEY/CMP/MAT/WAV are leaves; AI and unknown families
are unsupported. Empty SPR and incomplete COG schemas produce coverage issues. Every COG discloses
`cog-literal-references`, because resource names used only in code are not scanned. Inspect `issues` and
`unverifiedScopes` even when both completeness flags and `allReferencesResolved` are true. This lexical scan does not
validate complete asset grammars or replace the JKL export-capacity scan.

The receipt echoes effective `limits` and `usage`. All twelve limits are operational, with defaults and maxima in
the [dependency inspection contract](dependency_inspection_design.md#completeness-and-accounting):

| Limit | Charged work or retained state |
| --- | --- |
| `maxNodes` | Level plus distinct resource targets, admitted before lookup. |
| `maxEdges` | Reference occurrences, including reused targets. |
| `maxDepth` | Newly discovered target depth; existing targets can still receive edges. |
| `maxWorkUnits` | Physical index rows, visited text lines/tokens, reference attempts and archive/tier lookup attempts. |
| `maxIndexEntries` | Physical archive rows admitted to lookup indexes. |
| `maxPending` | Queued, unvisited resolved text nodes; known leaves and unsupported families are classified immediately. |
| `maxScanBytes` | Complete JKL and traversed dependency text, admitted before scanning. |
| `maxResourceBytes` | Newly selected payload bytes, admitted before hashing. |
| `maxStringBytes` | Retained string copies and transient lookup keys, admitted before copying. |
| `maxDetailRecords` | Returned node, edge and issue rows under one allowance. |
| `maxDetailBytes` | Conservative returned-row byte accounting. |
| `maxIssues` | Stored issue rows; observed issue counts continue after storage fills. |

The first traversal admission failure stops scanning globally. Issue/detail limits can truncate output without
stopping traversal; zero detail limits do not suppress traversal. Detail is a prefix in node, edge, issue order; omitted rows do not imply an empty graph. COG parsing additionally pre-admits at most 256 KiB and 1024
lines, plus a 16 MiB conservative evidence-expansion estimate for declaration diagnostics. Raw capture/hashing and
the fixed COG cap-admission pass are outside the shared-reader work counter; these limits do not bound total process
memory or elapsed time. Identifier strings use the receipt's reversible `textEncoding: "ascii-byte-escape-v1"`, defined in the
[full graph contract](dependency_inspection_design.md#graph-and-identity).

## Geometry and ordinary authoring

`new <output.jkl|output.sed> [mots|jk|droidworks]` creates a closed room scaffold. Placeholder resource names do not provide MAT/CMP bytes or a playable mission. Resolve real resources, templates and scripts before packaging or claiming gameplay.

`.ndy` denotes IJIM text; it is refused for JK, MotS and DroidWorks creation/conversion. `convert` changes the supported container, not the project's game identity.

See [design operations](agent_design.md) for box creation, shared-core extrusion/adjoins, material/colormap registration,
surface render modes, texel UVs and independent white/RGB corner lighting. Touching geometry is not automatically
connected. Basic topology operations deliberately refuse unsupported or ambiguous imported state and geometry they
cannot safely classify.

`setVertexPosition` restores checked absolute vertex edits with exact reciprocal portal welding. `fitSurfaceTexture`
fits a face to explicit integer texel dimensions in pixel space. `alignSurfaceTexture` continues a representable
affine reference mapping across one exact shared edge while preserving independent light channels. All three validate
simple planar convex faces and publish only after their complete candidate passes. Their coordinate, geometry and UV
space limits are documented under [vertex and texture editing](agent_design.md#absolute-vertices-and-fittedaligned-textures).

Numeric vertex Position controls in the Properties panel use the same portable checked operation as
`setVertexPosition`. They move all exact reciprocal portal counterparts together, preserve independent coincident
geometry, and refuse invalid geometry or hidden/locked counterpart sectors without creating history. Refusal restores
the displayed value and reports the reason in the status bar. The GUI admits general finite model coordinates and
face sizes under an eight-million-unit work budget, including its layer scan and portal correspondence checks;
the agent additionally retains its explicit 64-corner and +/-1000000 coordinate policies. Neither route bypasses
geometry or work admission for an unchanged position. Successful GUI changes use one ordinary undo/redo transaction.

MotS `bindThingArchitecture`, `rebindThingArchitecture` with explicit `policy: "reset"`, and
`bakeThingArchitecture` use patch-root hash-bound archives and the selected Thing's effective model. They create
private samples, validate all-LOD addressing, and clear only that owner's retained automatic-bake baseline on success.
The explicit point-source and world-occluded profiles have bounded work and do not claim retail-baker equivalence.
The existing v1 bake-input digest excludes world blocker geometry, so preserve the plan/apply source hashes and
rebake after relevant world changes. See [MotS architectural lighting](agent_design.md#mots-architectural-lighting)
for exact profiles, all-LOD constraints and readback.

Other exposed families include batch UV projection/transforms, placement arrays, typed COG creation/rebinding, Thing frames, world lighting bake, prefab/DXF operations, and selected legacy geometry recipes. Use current discovery and their core contracts. KEY timeline operations edit standalone animation files; they do not edit Thing mover frames. See [KEY format](formats/key_format.md).

`addThing` creates a fresh instance with the exact requested name. Both it and `placeThingArray` consume optional patch-root `resourceArchives` for the placed-COG append guard; a COG-bearing document without complete source-bound schemas refuses. Appending records also requires a positive next entity ID greater than every existing entity ID. Invalid or reused allocation state remains inspectable for repair, but cannot silently create duplicate identities. No automatic renumbering occurs.

## File and resource preservation

RED `.sed` is a binary native workspace. The older .NET editor also accepted historical text `.sed`; suffix equality does not make those grammars interchangeable. See [native SED policy](sed_format.md) before transforming a workspace. A no-op or native copy retains complete optional sections; an edited workspace with opaque history/buffers refuses until those dependencies can be updated safely. Known RGB/ARCH-only documents remain editable through their supported paths.

JKL/NDY export and legacy JED/NED import have format-specific preservation limits. Keep the source, companion templates and resource provenance. Successful parsing alone does not establish preservation of every historical dialect or unknown record. Follow `convert`'s actual RED signature rather than the .NET resource-request signature.

Resource registration creates a table entry, not the resource itself. Typed COG requests require the exact script/archive evidence expected by that command. Keep archive order, exact entry names and hashes. A package or launch plan does not execute the game.

Engine-facing package and `archive-create` output uses lowercase ASCII physical GOB/GOO entry names with
backslashes; package receipt paths use forward slashes. Original authored resource names still select their
resource winners, and logical references, episode map spelling and payload bytes are preserved. For example,
the authored `cogStrings.uni` extra resource in `misc` is emitted as `misc\cogstrings.uni`. The engine lowercases
GOB lookups but indexes physical names verbatim, so RED's case-insensitive archive readback alone cannot prove
that the game can open a mixed-case physical row. The raw archive reader and serializer retain original row
spelling for inspection and preservation; canonicalization applies at runtime authoring boundaries.

Runtime entry paths must be relative printable ASCII without spaces, drive letters, wildcards, empty components
or `.`/`..` components, and must fit 127 bytes plus the engine terminator. Invalid package destinations report
`InvalidPackageEntryName` or `PackageEntryNameTooLong` and emit no archive. `archive-create` reports
`invalidArchiveEntryName`; case-colliding names still refuse with `duplicateArchiveEntry`. Package aliases with
different selected bytes still refuse with `PackagePathCollision`. These checks concern emitted physical paths,
not validation of UNI keys or proof of runtime resource closure. The engine behavior is pinned in
[stdGob.c](https://gitlab.com/codalilitari/OpenJKDF2-MotS-Fixes/-/blob/6be121d4238e43b8254e8b1a167b7503e132920e/src/Win95/stdGob.c)
and [stdHashtbl.c](https://gitlab.com/codalilitari/OpenJKDF2-MotS-Fixes/-/blob/6be121d4238e43b8254e8b1a167b7503e132920e/src/General/stdHashtbl.c).

A package plan's `unverifiedScopes` names every dependency family the planner did not inspect; `runtimeClosureVerified` stays false and `canBuild` only means the explicitly resolved entries can be copied. One permanent scope is `cog-literal-references`: symbol defaults of a completely parsed COG schema are followed, but resources named only as literal call arguments in the code section are not scanned, so a plan containing any COG discloses that scope. Treat a buildable plan as an inventory under those stated limits, not as runtime dependency closure.

Package `options.maxNodes` counts the generated episode, source level and distinct resources, including stock
omissions. A limit below two or a new resource beyond the remaining capacity returns `PackageBudgetExceeded` and
no package bytes. Repeated references share a node; callback aliases still have to prove matching content at the
exact limit. See the [node-budget contract](package_node_budget_design.md).
Admission precedes lookup, so even a missing reference at full capacity refuses; `allowUnverifiedReferences`
does not waive the node limit.

`validate` also runs an exact, read-only fan-degeneracy inspection over every surface fan. Its receipt carries `fanDegeneracyStatus` and the effective `fanDegeneracyLimits` (`maxFanTriangles` default 4000000, `maxIssues` default 100000; a request may lower or raise them within the documented bounds 64000000/1000000, and unknown fields are usage errors). A fan triangle whose stored corners are coincident or exactly collinear is reported with code `fan-degeneracy`, its sector/surface/fan index, kind, and `repairAuthorized:false`. Coincident corners (`kind: "coincident-corners"`) are an error and make `valid` false. Exactly collinear corners (`kind: "collinear-corners"`) are a warning and leave `valid` unchanged: an edge vertex beside the fan origin always produces one, the shipped JK multiplayer levels carry between 117 and 1,145 each, and a cleaved doorway adds them to the neighbouring faces by design. A surface whose every fan triangle is degenerate has no area and no plane at all; it is reported once more as `zero-area-surface`, an error that makes `valid` false. A scan that cannot finish reports `fan-degeneracy-scan-incomplete` instead of a silent clean result and makes `valid` false. Near-zero area is not judged: a completed scan with no fan findings certifies only the absence of exactly degenerate fan triangles, not fan quality, convexity, or engine safety, and no repair is offered or implied.

The `new` request accepts optional starter-room geometry: `origin` ([x, y, z], finite) and `size` ([x, y, z], finite, each component in (0, 100000]). The room spans exactly origin..origin+size and is composed from checked core operations (unit cube, one checked scale per axis, checked translation), so a refused transform publishes nothing. Omitting both fields builds the default room (origin [-1, -1, -1], size [2, 2, 2]) with the editor's new-project header and spare export capacity; the receipt always echoes the effective `origin` and `size`. Either way every face gets the new-face texture mapping that `addBox`, the geometry recipes and extruded side walls use: 320 texels per JKU on the face's own plane, anchored at the world origin, so the scaffold no longer stretches one texture tile across each face. Only the room box is configurable: materials, ambient and other scaffold defaults are unchanged, and a configured DroidWorks scaffold places its walkplayer at the room center.

The basic patch operations `translateSectors` (`sectors`, `delta`), `scaleSectors` (`sectors`, `pivot`, exactly one of uniform `factor` or per-axis `factors`, optional `scaleTextures` default true) and `rotateSectors` (`sectors`, `pivot`, `axis`, `angleDegrees`) transform existing geometry through the checked core transforms. They move the named sectors plus the adjoined mate faces that connect them to neighbours (duplicates move once): portal coincidence is preserved and neighbour geometry incident to those corners shears along, while everything else — including coincident corners that are not adjoined — stays, so moving connected rooms rigidly still means listing every sector that must travel. Things, Lights and frames keep their positions. Scale factors must be positive (mirroring is refused, it is not a scale), rotation follows the core angle convention (+90 degrees about +Z maps +X towards −Y), and like their sibling geometry operations all three refuse documents with unmodeled retained JKL state and publish nothing on refusal. The entity counterparts `translateThings` (`things`, `delta`, optional `moveFrames` default true) and `translateLights` (`lights`, `delta`) move contents the same way, so a room and everything in it can relocate in one atomic patch (sector op first, then the entity ops). Thing translation refuses ARCH-bound Things and any landing position that is not strictly inside the Thing's uniquely classifiable sector — the same bar as `setThingPosition` — and duplicate indices move once. The patch operation `makeDoor` (`thing`, optional `height`) is the headless counterpart of the GUI's Make Door: it binds the fixed stock script `00_door.cog` from the patch `resourceArchives` (symbols loaded with defaults, script hash checkable like `addCog`) and converts the Thing into a two-frame sliding door; it refuses without the script, without a resolvable height (explicit `height` or a template bounding box), for ARCH-bound Things, or when the Thing already declares `numframes`. No script is authored or modified — elevator and switch mechanisms remain compositions of frames and bound COGs (`setThingFrames`, `addCog`), with no dedicated builder operation.

### Building a level from nothing

These operations and commands came out of building a complete level (rooms, doors, stairs, a switch-operated exit, an end trigger, custom textures and a custom prop) with the agent alone, and replace the steps that previously needed the editor or hand-edited files:

- Geometry: `addBox` also takes exact corners `{min, max}`, so a room can share a wall plane with an existing one without rounding. `cleaveSurface {sector, surface, normal, point}` splits one free face and `cleaveSector {sector, normal, point}` splits a room; with them a doorway is cut to an exact rectangle and joined with `adjoinSurfaces` (both refuse once the level has placed COGs, so build geometry first). `buildStairs` and `buildSwitchbackStairs` expose the editor's straight and landing flights (the first step is at floor level; the flight's near face is a full wall to join to the room below).
- Header: `setLevelHeader` sets gravity, sky, horizon, mipmap, LOD, perspective and Gouraud distances; `new` now writes the editor's defaults for these and its spare export capacity.
- Resources: `renameMaterial {materialIndex, name}` and `renameColormap {colormapIndex, name}` replace the scaffold's placeholder names everywhere they are bound. `importTemplates {templates, templateSource}` copies templates (and everything they inherit or name) from a hash-bound `.tpl`; `defineTemplate {name, parent, values}` adds a level template, parsed by the same reader, for a prop whose model lives in a custom archive. The JKL writer keeps the templates that Things, placed COGs (template-typed values) and other saved templates name, plus the static templates the level defines; with a patch-root `templateSource` it also writes the remaining static templates and the templates that only scripts name. It drops templates nothing references, so define or import a template in the same patch as the `addThing` or `addCog` that names it. `defineTemplate` refuses names and values the game would split differently: commas, a leading `;`, backslashes and unbalanced parentheses.
- COGs: `addCog` bindings take `{symbol, kind, targetIndex}` for thing/sector/surface/cog symbols (`-1` for none), `{symbol, kind, sector, surface}` for a surface without counting surface numbers, and `{symbol, kind, value}` or `{symbol, kind, default: true}` for every other kind (flex, float, int, vector, sound, material, model, template, keyframe, ai). `defaults: true` on the operation gives every unbound non-reference symbol its declared default; references are never filled in, and a symbol with no declared default is refused. Values are stored the way the engine reads them back: an int is a decimal 32-bit number (a `0x` value is converted to decimal; a fraction or trailing text is refused, and a JSON number is written exactly), a flex or float is a finite decimal number within float range, a vector is `(x/y/z)`, and a resource name has at most 31 printable characters and no spaces, commas, `=`, `#`, quotes or backslashes. The binding's `kind` must match the declaration (flex and float are interchangeable). A vector's default is `(0/0/0)` because the engine ignores a vector initializer, and an int default written in hex is refused because the engine reads it as 0. `setCogValue {cog, symbol, value | default: true}` changes one typed value of a placed COG under the same rules (references go through `rebindCogReference`). `rebindCogReference` takes `sector`/`surface` the same way as `addCog`.
- Export capacity: whenever a patch supplies patch-root `resourceArchives` and writes a JKL or NDY level (from JKL, NDY, JED or NED input), the JKL writer reads what models, puppets, sound classes and scripts load and declares the SOUNDS, MATERIALS, KEYFRAMES and COG capacities the level needs. The receipt's `exportBuffers.dependencyScan` is `complete`, or `incomplete` with `warnings` naming what could not be read. The scan reads only those hash-bound archives, shared with the rest of the patch and admitted against the same expansion limits as COG resolution; a resource name in the level never opens a file on disk and reads as missing instead. Archives given on a single operation serve that operation and do not enable the scan. A patch-root `templateSource` (the master template file) also lets the writer emit the static templates and resolve templates that only scripts name; with the stock archives a scan is complete only with it. Without the scan a headless level can declare too little room and the game crashes while loading the player's own weapons; `resolveExportDependencies: false` keeps the document-only counts. The standalone `addCog` and `rebindCogReference` commands run the same scan and report the same receipt.
- Assets: `mat-create <request> <new.mat>` maps 1–16 PNG (any PNG colour type, non-interlaced) or baseline/extended-sequential JPEG cels to an opaque 8-bit MAT under a CMP palette, with 1–4 mipmaps and optional Floyd-Steinberg dithering; the receipt reports colours used and the mean colour error. Invalid cel counts refuse before image or palette reads. [Material Authoring](material_authoring.md) provides ordinary GUI controls for the same core conversion. `archive-create <request> <new.gob>` packs loose files (`{name, path, sha256}`) into a GOB, refusing duplicate or escaping names and reading MAT, 3DO, COG and template entries back by type. Mount the result as a `resourceArchives` entry while building and list it in the package request's `archives` so its files are copied into the level GOB.
- Models: `model-import <request> <new.3do>` imports Wavefront OBJ or binary glTF 2.0 (GLB) into 3DO 2.1, with a hash-bound `source {path, sha256}` and explicit MAT mappings in `materials`. Each mapping supplies `{name, mat, size: [width, height]}` or `{name, mat, matFile: {path, sha256}}`. OBJ UVs become texels `u*width`, `(1-v)*height`; GLB uses `u*width`, `v*height`. The default y-up conversion is `(x, -z, y)`; `upAxis: "z"`, positive `scale`, `origin` (`base`, `center`, `none`), and `meshName` configure placement. Authored normals and hard edges are preserved by default; `normalPolicy: "recalculate"` explicitly derives smooth geometric normals. The receipt reports `normalPolicy` and `normalSplitVertices`; see [model lighting and import normals](model_lighting.md#import-normals). Meshes split to JK limits by default (512 faces, 512 vertices and 768 texture vertices per mesh; 64 meshes per model); `split: false` requests refusal instead. Every emitted mesh has an identity hierarchy node under one root. Materials, UVs, supported primitive types, finite coordinates and total limits are checked before create-only publication, and the output is read back through the core 3DO reader. Unsupported constructs refuse instead of being guessed. Pack the model as `3do\<name>.3do` and its MATs under `mat\` and `3do\mat\`.
- Model shading: `model-lighting-inspect`, `model-lighting-plan`, and `model-lighting-apply` inspect and bake ordinary 3DO assets using the portable core. They preserve all LODs and every non-light byte, require source and planned-output hashes, and publish a new model variant. Profiles are classic `jk`/`mots` for 2.1 scalar light and `ijim` for 2.3 RGB. See [model lighting](model_lighting.md) for complete requests, MCP tools, limits, and the distinction from per-Thing ARCHLIGHTING and runtime lighting.

### Resource-aware export preservation

The material capacity receipt distinguishes dependency collection from retained wire rows. Its `materials.collected`
counts required material names, while `materials.total` includes the imported material slots that the writer retains,
plus the authored spare capacity. The total can therefore exceed `collected + additional`. The portable capacity
inspector and the writer derive this total from the same material table, including duplicate retained slots; the
agent's `exportBuffers` receipt reports that same capacity.

When the engine-format dependency scan has a resource locator, export hydrates imported COG arguments and omitted
defaults from the captured scripts before collecting dependencies. A public COG argument naming a template therefore
keeps that template in the exported dependency set. This COG-only step preserves geometry, texture coordinates and
their coordinate-space tag; it does not normalize pixel UVs through available MATs. Valid numeric arguments may acquire
their canonical typed spelling, which remains visible in the persisted semantic preview. An unavailable or malformed
script leaves its raw arguments pending, and the scan reports incomplete rather than claiming those arguments were
resolved. Native workspace saves and `resolveExportDependencies: false` retain their existing paths without this
export hydration.

The export step also compares the actual script payload selected by the export locator with every script payload
already checked by a COG mutation or append guard in the same command. Conflicting bare/prefixed aliases or conflicting
operation-local and patch-root sources refuse with `exportCogResourceAmbiguous` before output publication. Aliases
with identical bytes remain valid. This check preserves the existing archive lookup policies rather than silently
changing the source selected by a guard or exporter.

Strict COG-only hydration refuses with `exportCogReferenceUnresolved` when it would need to repair an imported
reference or default, including an invalid surface target or a negative pointer value other than the null sentinel
`-1`. It publishes no staged COG changes when a later value fails. Repair the reference explicitly and plan again;
the export is not a reference-recovery command. This preservation check does not certify reference validity: for
example, an out-of-range positive Thing, sector or COG index can remain unchanged because the ordinary parser does not
repair it. Use resource-aware reference validation separately. Ordinary resource-backed GUI import retains its
established recovery behavior. Successful hydration and capacity calculation remain separate from full resource
closure and runtime acceptance.

The GUI's **File > Save JKL & Test** stages the current document through the normal export boundary without changing its editable project path, dirty state, undo history or ordinary export destination. Save modified COG/resource-editor drafts first. Follow [game testing](game_testing.md) for the selectable Retail/OpenJKDF2 profiles, isolated stage, resource closure and cancellation rules. The agent's `launch-plan` shares the portable planner but does not export or start a process. OpenJKDF2 direct-map startup and retail's **New Game > RED Test** selection are different acceptance paths; retain observations for each game and engine requested.

## Visual and runtime acceptance

`snapshot` and `overview` currently use the built-in `software-v1` renderer for world surfaces. They omit Things/models, sprites and dynamic lighting. Keep camera, resource hashes and render profile fixed for comparisons; inspect the image instead of treating PNG creation as visual approval. Use the editor or declared runtime for excluded content.

Geometry mode zero is hidden even when an adjoin retains a material. Other nonzero modes use filled rasterization; dedicated point/wireframe presentation and exact engine Solid/Textured shading are outside this preview's scope. Orthographic depth uses view-space distance, so nearer surfaces remain in front independently of input order.

Keep structural validation, editor rendering, game loading and player interaction as separate evidence. A three-room portal test does not certify retail loading; a script event does not prove player Use reach. See [capability matrix](capability_matrix.md) and [gap register](gap_register.md) for the remaining product and platform gates. Current-source tests and independent source reviews do not replace native macOS/Linux execution or Windows UI qualification.
