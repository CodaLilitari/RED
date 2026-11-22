# RED agent workflow

`red-agent` and `red-mcp` share the Qt/C++ editor's level model and core operations. They are distinct implementations from the older .NET SED-MotS agent. Use the executable's current `schema` response, this workflow, and focused format documentation together. An operation listed in another editor's schema is not automatically available here.

## Discovery and execution

Run `red-agent schema` to obtain the compatible command/operation inventory, request-size limits, exit codes and
snapshot scope. The shared registry supplies 100 patch operations and 60 CLI commands. Focused discovery requires no
level file:

```text
red-agent schema commands
red-agent schema patch
red-agent schema fitSurfaceTexture
red-agent schema dependencies
red-agent schema package-plan
red-agent schema package-apply
red-agent schema mat-create
red-agent schema model-import
red-agent schema model-native
red-agent schema model-native-inspect
red-agent schema jo-ja-plan
red-agent schema archive-create
red-agent schema snapshot
red-agent schema overview
```

Each focused response is a standalone Draft 2020-12 JSON Schema. `commands` validates a command name and includes
exact positional usage and descriptions in `x-commands`; it does not claim a body schema for every CLI command.
`patch` describes the request envelope and all operation alternatives, while an exact operation name returns that
operation's required fields, nested objects, alternatives and bounds. `dependencies` returns the shared dependency
request schema, also exposed as `dependencyRequestSchema` in the no-topic inventory. `package-plan` and
`package-apply` return the same package request schema, including the legacy aliases, optional fields and effective
option defaults. `mat-create` describes indexed8/RGB565 requests, captured files and format-conditional fields;
see [material authoring](material_authoring.md#agent-and-mcp-requests) and
[package request discovery](package_authoring.md#cli-and-mcp-request-discovery). `model-import` describes the
OBJ/GLB request (captured source, material mappings with exactly one of `size` or `matFile`, placement and normal
options, defaults), and `archive-create` the ordered captured entries and the entry-name rule; see
[model import](model_import.md#agent-and-mcp-requests). `snapshot` and `overview` describe the two image requests:
the size limits, the projection and its one lens value, the explicit camera, `thingMode`, the archive list and
every default; see [visual and runtime acceptance](#visual-and-runtime-acceptance). Runtime checks still decide
geometry validity, resource compatibility, source identity and cross-field arithmetic; follow the returned
`x-runtimeConstraints`.
Unknown topics and extra topic arguments are usage errors. No-topic inventory behavior remains compatible, including
existing domain schemas such as `modelLightingRequestSchema`.

Run `red-agent help` for the principal command argument order. The CLI currently has no `--version` contract; record
the executable SHA-256 and source revision with a durable run. Invoke it with an argument array, capture stdout and
stderr, check the exit code, then parse its JSON stdout. Send UTF-8 request JSON on stdin using `-`, or use an explicit
request file.

Level `inspect`, `new`, `query`, `select`, `validate`, `plan` and `apply` accept Unicode input, request and
output filenames on Windows. Their MCP routes (including path-based `sed_discover` and `sed_diagnose` for
validation) resolve top-level UTF-8 paths against the workspace and permitted read-only roots. An apply receipt
names the newly created file. The output hash can be checked by reopening that filename. Replaying a completed
operation returns its recorded receipt, including for writes performed by an older build.
For `sed_new`, supply a game alias in `kind` (`jk`, `jkdf2`, `mots`, or `droidworks`) or a JSON object in
`request`. A supplied nonempty game alias takes precedence. Direct CLI `new` also accepts a JSON request
filename or `-` for stdin.

Exit codes are 0 success, 2 invalid request/usage, 3 input failure, 4 stale input/conflict, and 5 refusal. A structured
`code` / `errorCode` refines some failures. These meanings differ from the .NET agent's codes. Existing optional hashes
and permissive legacy fields remain compatible; new operation families can reject unknown fields. Use the schema for
the actual operation instead of assuming one universal unknown-field policy.

Requests are limited to 1 MiB while reading and source files to 128 MiB. Captured archives share a 512 MiB byte budget and 100,000-entry budget across one command; index admission occurs before the core parser allocates its entry table. Materializing COG/package archive sets additionally limits aggregate entry payloads to 512 MiB, including overlapping payload ranges. Lighting snapshots share 256 MiB and 4,096 resource lookups across all bakes in a command, including missing lookups. These are agent admission limits, not new format limits on the general core archive API. JSON rejects duplicate object fields, malformed numbers and unescaped control characters; Unicode escape pairs decode as UTF-8 scalars. Invalid integer selectors, flags and overflowing float values refuse before narrowing.

Keep generated levels, build trees, render output and request/result receipts outside source checkouts. Building does not install a new release over an existing distribution.

## Import a Jedi Outcast or Jedi Academy scene

Run `red-agent jo-ja-inspect scene.bsp` on a compiled Raven RBSP v1 map to get its raw
`sourceSha256`, the shader names and surface flags used by world model 0, the source entity classes,
patch count, brushes and submodels. Put the reported hash in an import request. The example opts into
checker materials for missing shader mappings.

```json
{
  "schemaVersion": 1,
  "source": { "path": "scene.bsp", "sha256": "<64 hex digits from inspect>" },
  "target": "jk",
  "levelName": "joja_scene",
  "allowPlaceholders": true,
  "patchSubdivisions": 4,
  "audioPolicy": "stock-equivalents",
  "sourceProfile": "jo-mp"
}
```

```text
red-agent jo-ja-plan import.json
red-agent jo-ja-apply import.json scene.gob <outputSha256> <manifestSha256>
```

The plan returns the model and material inventories, collision and lighting limitations, full import report and
both review hashes. Apply recalculates the package, recaptures all source files before publication and writes a
new archive. Use `target: "mots"` and a `.goo` destination for Mysteries of the Sith. The equivalent MCP
tools are `sed_jo_ja_inspect`, `sed_jo_ja_plan` and `sed_jo_ja_apply`. Plan and apply take a nested `request`;
apply also takes `output`, `expectedOutputSha256`, `expectedManifestSha256` and an optional journal `operationId`.

For mapped artwork, pass `materials` entries with an exact `shader` name, a target MAT basename in `name`,
and `source: {path, sha256}`. Indexed materials also need `palette: {path, sha256}`. RGB565 support depends on
the target renderer. To omit a known surface effect, such as an additive flare, list its exact world shader name in
`skipShaders`. A shader may be either skipped or mapped. `red-agent schema jo-ja-plan` lists limits and defaults.

The default `geometryMode: "model-scene"` puts static 3DOs inside a single bounding sector. Set
`geometryMode: "hybrid-sectors"` for native rooms, portals and sector-owned model detail. For a large source,
try `trianglesPerModel: 400` and `maxTriangles: 200000`. The original field name `trianglesPerModel` sets
the face limit for each model. Hybrid keeps convex clipped polygons, so a quad counts as one face. It admits up to 479 faces per model and
250,000 source triangles. `maxSectors` defaults to 64 (1–96) and `maxFragments` to 500,000 (1–1,000,000).
Inspect `sectors`, `portalPairs`, `nativeFaces`, `residualFaces`, `residualTriangles` (triangle equivalents), model sector IDs and the area coverage
figures before applying. Keep camera positions and source detail settings fixed when benchmarking the two modes.

Add `"meshOptimization": "strict"` to weld near-duplicate vertices and merge coplanar faces only where light and
texture coordinates stay linear, so the result draws within one 8-bit light step of the original with far fewer
vertices and faces. `"loose"` allows 4/255 of light and half a texel for more merges, and removes corners on straight
edges wherever no face of any generated model keeps them; `"off"` (the default) keeps existing recipes. The plan
reports `optimizedVertices`, `optimizedFaces` and `worstNormalTurnDegrees`, and loose adds `straightCornersRemoved`
and `fanTriangles`. The import dialog offers the same three levels. See
[Mesh optimization](jo_ja_import.md#mesh-optimization). See the [playable conversion checks](jo_ja_import.md#qualifying-a-playable-conversion) for exact mixed-edition joins, controlled movement/shot fixtures and synthetic COG tests.

Moving models, triggers, brush physics, shader scripts, ambient audio and mission logic require further authoring.
Native solid sector boundaries collide. `renderMeshCollision: true` also makes residual rendered model detail
collidable. The default audio policy assigns available stock
hard, metal and earth footstep classes. Review grass, water and other sources without a matching model cue.
Source wave import is unavailable and requesting `source-import` returns `joJaSourceAudioUnavailable`.
Archive readback validates the generated resources. `runtimeVerified` remains false until separate game testing.

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
read-only `--resource-root <dir>` values. Its `tools/list` catalog contains 55 routes.

| Domain | MCP tools |
| --- | --- |
| Discovery and document summary | `sed_discover`, `sed_schema`, `sed_inspect`, `sed_statistics` |
| Readback and diagnostics | `sed_query`, `sed_select`, `sed_diagnose`, `sed_workbench_query`, `sed_arch_validate` |
| Level publication | `sed_plan`, `sed_apply`, `sed_new`, `sed_convert` |
| JO/JA import and collision inspection | `sed_jo_ja_inspect`, `sed_jo_ja_plan`, `sed_jo_ja_apply` |
| Archives and packages | `sed_archive`, `sed_archive_create`, `sed_dependencies`, `sed_package_plan`, `sed_package_apply` |
| Observation | `sed_snapshot`, `sed_overview` |
| COGs | `sed_bindings`, `sed_cog_check`, `sed_cog_compile`, `sed_cog_run`, `sed_assistant_check`, `sed_assistant_script` |
| Prefab and DXF | `sed_prefab_capture`, `sed_export_dxf`, `sed_dxf_inspect`, `sed_dxf_import` |
| Materials and colormaps | `sed_mat_inspect`, `sed_mat_export_png`, `sed_mat_create`, `sed_cmp_create`, `sed_cmp_master_create` |
| Models | `sed_model_import`, `sed_terrain_3do`, `sed_model_hierarchy`, `sed_model_hierarchy_edit`, `sed_model_lighting_inspect`, `sed_model_lighting_plan`, `sed_model_lighting_apply` |
| Native 3DO editing | `sed_model_native_inspect`, `sed_model_native_plan`, `sed_model_native_apply` |
| Animation | `sed_key_inspect`, `sed_key_from_model`, `sed_key_plan`, `sed_key_apply` |
| Launch planning, native inspection and recovery | `sed_launch_plan`, `sed_native_inspect`, `sed_outcome` |

`sed_discover {}` and `sed_schema {}` return the inventory. A `topic` of `commands`, `patch`, `dependencies`,
`package-plan`, `package-apply`, `mat-create`, `cmp-create`, `model-import`, `model-native`, `model-native-inspect`, `archive-create`, `snapshot`, `overview`, or an
exact operation returns focused discovery. The compatible
`sed_discover {"path":"level.sed"}` form still inspects a document;
`sed_inspect` is the explicit inspection route. A discovery call cannot combine `path` and `topic`, and `sed_schema`
does not accept a document path. For example:

```json
{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"sed_schema","arguments":{"topic":"patch"}}}
```

`sed_plan` and `sed_apply` embed the typed patch schema in their tool input schemas; `sed_dependencies` embeds the
same closed request schema returned by `schema dependencies`. `sed_package_plan` and `sed_package_apply` embed the
same shared package request schema returned by either package topic. `sed_mat_create` embeds the same request
schema returned by `schema mat-create`, including the legacy default indexed8 and explicit RGB565 alternatives;
`sed_cmp_create` embeds the one returned by `schema cmp-create`.
`sed_model_import` and `sed_archive_create` embed the request schemas returned by `schema model-import` and
`schema archive-create`, and `sed_snapshot` and `sed_overview` those returned by `schema snapshot` and
`schema overview`.
Package requests retain their permissive legacy
extension fields and do not acquire the dependency inspector's stricter request shape. Other domains retain their own command contracts;
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

Other exposed families include batch UV projection/transforms, placement arrays, typed COG creation/rebinding, Thing frames, world lighting bake, prefab/DXF operations, and selected legacy geometry recipes. Use current discovery and their core contracts.

`bakeLighting` also follows some of LadyEebs' JED Light Baker (see [Light Baker conventions](lighting.md#light-baker-conventions)). A light whose `flags` include `0x02` is a sun: the direction from the anchor (`0x08`, or the origin) to its position points at it, and it reaches a corner only through sky surfaces. `0x20` makes a multiply light, which scales the light within its range by its color, fading to no change at the range (in MotS `0x20` stays Affect Arch Things). Set them with `addLight` or `setLight`. `angleShading: true` scales point lights and emissives by N·L, and `waterAbsorption: true` dims light that crosses underwater sectors by their tint, settling at the tint after 1 m of water however many sectors the water spans. Both are off by default, which keeps JED's results. The bake summary reports `angleShading`, `waterAbsorption`, `sunLights` and `multiplyLights`. KEY timeline operations edit standalone animation files; they do not edit Thing mover frames. See [KEY format](formats/key_format.md).

`addThing` creates a fresh instance with the exact requested name. Both it and `placeThingArray` consume optional patch-root `resourceArchives` for the placed-COG append guard; a COG-bearing document without complete source-bound schemas refuses. Appending records also requires a positive next entity ID greater than every existing entity ID. Invalid or reused allocation state remains inspectable for repair, but cannot silently create duplicate identities. No automatic renumbering occurs.

## File and resource preservation

RED `.sed` is a binary native workspace. The older .NET editor also accepted historical text `.sed`; suffix equality does not make those grammars interchangeable. See [native SED policy](sed_format.md) before transforming a workspace. A no-op or native copy retains complete optional sections; an edited workspace with opaque history/buffers refuses until those dependencies can be updated safely. Known RGB/ARCH-only documents remain editable through their supported paths.

JKL/NDY export and legacy JED/NED import have format-specific preservation limits. The agent writes JED as well as reading it, for JK and MotS levels: `convert` to a `.jed` path, `new` with a `.jed` output, and `plan`/`apply` with the patch field `outputFormat: "jed"` (apply then writes a `.jed` path; without the field a JED input still plans JKL, as before, and a native `.sed` stays native). Every JED is read back before it is reported: a reload that changes the project or loses sectors, faces, corners, Things, their templates, lights or COGs is refused (`jedRoundTripLoss`). What the format itself cannot hold is listed in `jedLosses` (each `{what, count, examples}`): `levelTemplates` (JED keeps no templates; stock ones come back from the master template file, custom ones must live in the project's template file) and `thingNames` (a JED Thing row names its template, so an instance name that differs from it is lost). A lossy JED is written only against the reviewed hash: `convert` refuses with `jedLossUnacknowledged` and the would-be `outputSha256` until it is passed as the expected hash, and `apply` needs the planned `outputSha256`. The agent also opens the two formats RED imports: `.asc` (the legacy ASCII tri-mesh) and `.lev` (a Dark Forces level as JK geometry, with RED's default import options; the objects of a same-name `.O` file are not read, because only the `.LEV` bytes are bound by the input hash). Keep the source, companion templates and resource provenance. Successful parsing alone does not establish preservation of every historical dialect or unknown record. Follow `convert`'s actual RED signature rather than the .NET resource-request signature.

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

For JK, MotS and Droidworks levels `validate` also reports `surface-corner-limit`, an error that makes `valid` false, for every surface with more than 24 corners, with its sector, surface, `corners` and `limit` (24). The engine's surface loader refuses such a face and, with it, the whole level. Cleaving a room hands each cut point to the neighbouring faces that share the cut edge, so a wall beside many cuts can pass the limit without a 25-sided face ever being drawn; split that wall, or cut fewer rooms beside it.

For JK and MotS levels with two or more player starts, `validate` warns with `multiplayer-material-headroom` when the level declares fewer than 32 spare world-material slots. The row gives `playerStarts`, `spareMaterials`, `recommended` (32) and an `evidence` link, and leaves `valid` unchanged. The game refuses a new material once the declared table is full, and a joining player's model and saber add materials at runtime: Sith Council declared exactly its 519 listed materials, played alone, and made the host exit when a second player joined. LucasArts' six FFA maps each keep 32 spare. Raise Materials in **Level Settings > Extra Buffers**, then test a real second-player join, as in [qualifying a playable conversion](jo_ja_import.md#qualifying-a-playable-conversion).

The JKL writer keeps a sector's authored collide box only while all eight of its corners are inside the sector, and otherwise writes the box it computes. The engine treats a Thing inside a sector's collide box as inside that sector without testing its faces, so a box left over from before a cut or a vertex move would hide the sector's portals and hold a player in the old room.

The `new` request accepts optional starter-room geometry: `origin` ([x, y, z], finite) and `size` ([x, y, z], finite, each component in (0, 100000]). The room spans exactly origin..origin+size and is composed from checked core operations (unit cube, one checked scale per axis, checked translation), so a refused transform publishes nothing. Omitting both fields builds the default room (origin [-1, -1, -1], size [2, 2, 2]) with the editor's new-project header and spare export capacity; the receipt always echoes the effective `origin` and `size`. Either way every face gets the new-face texture mapping that `addBox`, the geometry recipes and extruded side walls use: 320 texels per JKU on the face's own plane, anchored at the world origin, so the scaffold no longer stretches one texture tile across each face. Only the room box is configurable: materials, ambient and other scaffold defaults are unchanged, and a configured DroidWorks scaffold places its walkplayer at the room center.

The basic patch operations `translateSectors` (`sectors`, `delta`), `scaleSectors` (`sectors`, `pivot`, exactly one of uniform `factor` or per-axis `factors`, optional `scaleTextures` default true) and `rotateSectors` (`sectors`, `pivot`, `axis`, `angleDegrees`) transform existing geometry through the checked core transforms. They move the named sectors plus the adjoined mate faces that connect them to neighbours (duplicates move once): portal coincidence is preserved and neighbour geometry incident to those corners shears along, while everything else — including coincident corners that are not adjoined — stays, so moving connected rooms rigidly still means listing every sector that must travel. Things, Lights and frames keep their positions. Scale factors must be positive (mirroring is refused, it is not a scale), rotation follows the core angle convention (+90 degrees about +Z maps +X towards −Y), and like their sibling geometry operations all three refuse documents with unmodeled retained JKL state and publish nothing on refusal. The entity counterparts `translateThings` (`things`, `delta`, optional `moveFrames` default true) and `translateLights` (`lights`, `delta`) move contents the same way, so a room and everything in it can relocate in one atomic patch (sector op first, then the entity ops). Thing translation refuses ARCH-bound Things and any landing position that is not strictly inside the Thing's uniquely classifiable sector — the same bar as `setThingPosition` — and duplicate indices move once. The patch operation `makeDoor` (`thing`, optional `height`) is the headless counterpart of the GUI's Make Door: it binds the fixed stock script `00_door.cog` from the patch `resourceArchives` (symbols loaded with defaults, script hash checkable like `addCog`) and converts the Thing into a two-frame sliding door; it refuses without the script, without a resolvable height (explicit `height` or a template bounding box), for ARCH-bound Things, or when the Thing already declares `numframes`. No script is authored or modified — elevator and switch mechanisms remain compositions of frames and bound COGs (`setThingFrames`, `addCog`), with no dedicated builder operation.

### Building a level from nothing

These operations and commands came out of building a complete level (rooms, doors, stairs, a switch-operated exit, an end trigger, custom textures and a custom prop) with the agent alone, and replace the steps that previously needed the editor or hand-edited files:

- Geometry: `addBox` also takes exact corners `{min, max}`, so a room can share a wall plane with an existing one without rounding. `cleaveSurface {sector, surface, normal, point}` splits one free face and `cleaveSector {sector, normal, point}` splits a room; with them a doorway is cut to an exact rectangle and joined with `adjoinSurfaces` (both refuse once the level has placed COGs, so build geometry first). `connectSurfaces {sector, surface, otherSector, otherSurface}` is the editor's Connect and makes that doorway in one operation: two unadjoined, opposite, coplanar faces of different rooms are cut along each other's outline and the area they share is adjoined, so a door frame built with `addBox` between two rooms needs one `connectSurfaces` per side and no cleaves. Where the two faces already match it writes the same level as `adjoinSurfaces`. The cut gives the neighbouring floor, ceiling and wall faces the new corners, and every room it changes must remain a closed convex room. Its receipt entry is `connectSurfacesReports`, one per operation, with `operation`, `sector`, `otherSector`, `addedSurfaces`, `otherAddedSurfaces` and `portals` (each new adjoined pair as `sector`/`surface`/`otherSector`/`otherSurface` with its `corners`); read the portal's surface index from it, because a cut face keeps its index for only one of its pieces. Refusals are `basicConnectNoOverlap` (the faces are not opposite, coplanar and overlapping in an area), `basicGeometryUnsupported` (a face is already adjoined, both are in one sector, or a room would stop being closed and convex) and, like the cleaves, `basicCleaveCogBindings` once COGs are placed. `conformSeams {maxInsertions?, allowApproximate?, allowRemaining?, seamLimits?}` resolves the sealed-seam T-junctions that rooms of different sizes leave where they meet: one operation inserts every admitted corner on the edge that lacks it, without splitting or renumbering a surface, so it replaces a validate-and-cleave loop and is still admitted after COGs are placed. Its receipt entry is `seamConformReports`; see [conforming sealed seams](headless_validation.md#conforming-sealed-seams). `deleteSectors {sectors, materialIndex?}` removes rooms, the carving half of cleave-and-delete: cut a slab out of a large volume with `cleaveSector` and delete it to leave a bridge, a pillar or a beam. Every face of a remaining room that was a portal into a deleted one becomes a wall: it takes `materialIndex` (required when there is such a face), is drawn textured, collides, and gets RED's new-face mapping. The receipt entry `deleteSectorsReports` lists the deleted sectors and the sealed faces by their new indices. It is refused with `basicDeleteOccupied` while a Thing stands in a named room, with `basicDeleteNeedsMaterial` when a face needs a material and none is given, and, like the cleaves, with `basicCleaveCogBindings` once COGs are placed. `carveSurfaceRect {sector, axis, at, min, max, materialIndex?, uvSize?, lightMode?}` cuts a rectangle out of the free faces of a room that lie on the axis-aligned plane `axis = at`; `min` and `max` are the rectangle's corners in the plane's other two axes (for `x`: y then z; for `y`: x then z; for `z`: x then y). It is the lamp cut: a fixture, a lens or a panel gets its own `materialIndex`, a `uvSize` (texels) that stretches the material across exactly the rectangle while keeping each face's own handedness, and a `lightMode` (0 for fully lit). It cuts along the plane's second axis first, the height on a wall, so the cuts across stay inside the band and add no corners to the floor or ceiling edge (the engine refuses a face of more than 24 corners), and it finds the faces again by geometry after every cut, so the rectangle may straddle several faces. The receipt entry `carveSurfaceRectReports` gives `sector`, `surfaces` (the pieces that make up the rectangle) and `cuts`. Refusals are `basicCarveEmpty` (no free face on that plane overlaps the rectangle), `basicCleaveNoSplit`, `basicCarveTooManyCuts` (over 512) and, like the cleaves, `basicCleaveCogBindings` once COGs are placed. `mirrorFloor {sector, surface, reflectivity?, openingMaterialIndex?, allowOverlap?}` gives a room a reflective floor the retail engine can draw: the room is copied upside down under the named floor face, and the floor becomes a translucent portal into the copy that still holds the player up (visible, no movement, floor and collision flags), so the renderer's fixed translucent blend (about 35% floor, 65% reflection) shows the room in the floor. The face must be flat and horizontal, looking up into its room. The copy reflects every vertex, reverses each face's corners and keeps their texture coordinates, which is what mirrors the textures, and takes the room's light at `reflectivity` (0..1, default 0.5). It is appended as the last sector, so no sector or surface number moves. The copy has nothing beyond its doorways, so they become walls: each takes the material and the mapping of a wall in its plane, continued across it, or the room's most used wall material, or `openingMaterialIndex` when given. The renderer draws a room once per frame, clipped by the last portal it came through, so each mirror floor face gets its own copy; to make several floor faces reflect, mirror each. Things are not reflected. A copy that would overlap another room is refused with `mirrorFloorOverlap` unless `allowOverlap` is true (nothing can enter the copy, so the game tolerates it). `bakeLighting` keeps the reflection lit like the room: for the bake each mirror floor stands solid (both faces get the editor's block-light bit, then their flags are restored, so the level never carries it), and afterwards the room's light is copied into the reflection at `mirrorReflectivity` (default 0.5); the bake summary counts the mirrors in `mirrorFloors`. Mirrors are found by geometry, so run `conformSeams` and finish cutting the room before mirroring it; a room edited afterwards no longer matches its mirror and is skipped until the mirror is rebuilt. The receipt entry `mirrorFloorReports` gives `sector`, `surface`, `mirrorSector`, `mirrorSurface`, `reflectivity`, `sealedOpenings` (the copy's faces that were doorways), `openingsContinued` and `overlapsSectors`. Other refusals are `mirrorFloorNotFloor`, `mirrorFloorAdjoined` (the face is already a portal) and `mirrorFloorOpeningMaterial` (a doorway and no textured wall to copy). The app's remaining Edit, Sector and Surface commands are patch operations too (the inventory and the gaps still open are in [agent parity](agent_parity.md)); each reports in `editReports` with its `op` and `operation` index. `deleteThings {things}`, `deleteLights {lights}` and `deleteCogs {cogs}` delete by index; a COG value that pointed at a deleted Thing or COG is cleared to -1 and counted in `cogReferencesCleared`, and a Thing with MotS architectural lighting is refused (`parityThingHasArchitecture`; clear it first). `setSectorProperties {sectors, extraLight?, tint?, sound?, soundVolume?}` sets the sector fields the JKL carries (a light is one number or `[r, g, b]` within 0..1; `sound` is a plain file name, empty to clear). JK and MotS sectors have no stored thrust; a COG sets it at run time. `setSurfaceProperties {surfaces, extraLight?, uvScale?}` sets a face's extra light and texture scale. `unadjoinSurfaces {surfaces, materialIndex?}` turns portals back into walls; a side that was undrawn or had no material takes `materialIndex` (refused with `parityUnadjoinNeedsMaterial` without it, `parityNotAdjoined` for a face that is not a portal). `makeUnderwater {sectors, tint?, sound?, soundVolume?, waterSurfaces?, waterMaterialIndex?}` floods rooms and, unless `waterSurfaces` is false, makes each portal into a dry room a translucent water surface. `placeSectorLights {sectors, intensity}` puts one light at each room's centre reaching its farthest corner, and `removeRedundantVertices {sectors}` merges duplicate, unused and collinear corners without renumbering faces (its plan reports the counts without writing). A second batch covers the Transform and remaining Surface commands. `flipSurfaces {surfaces}` reverses faces' winding. `mirrorSectors`, `mirrorThings` and `mirrorLights {…, normal, point}` reflect over a plane. `rotateThings {things, pivot, angleDegrees, axis?, orientation?}` and `rotateLights {lights, pivot, angleDegrees, axis?}` rotate about a pivot, in the core's convention shared with `rotateSectors` (+90 degrees about +Z turns +X towards -Y); a moved Thing takes the one sector it lands strictly inside (`parityThingOutsideLevel` otherwise), and a Thing with an ARCH binding or path frames is refused (`parityThingHasArchitecture`, `parityThingHasFrames`). `duplicateSector {sector, offset}` appends a moved copy of a room without adjoins. `fitAndAdjoin` and `stitchSurfaces {sector, surface, otherSector, otherSurface}` fit a face onto another and adjoin them, or carry a face's texture mapping onto its neighbour (`parityFitRefused`, `parityStitchRefused`). `mergeSurfaces {surfaces}` uses the document's typed COG bindings and returns an operation-specific reference map. An unresolved binding identifies its script and argument in `parityOpaqueSurfaceReference`. `mergeSectors {sector, otherSector}` and `subdivideSurfaces {surfaces}` continue to require a level without placed COGs (`parityCogBindings`). A selection without a mergeable pair returns `parityMergeRefused`. `insetSurface {sector, surface, factor, adjoinPair?}` shrinks a face toward its centre (a portal only with `adjoinPair`, both sides together; `parityInsetRefused`). `planarizeSurfaces {surfaces}` flattens faces and reports `stillNonPlanar`, and `repairSurfaces {surfaces, solid?, passable?}` applies the editor's Fix Solid Surfaces and Fix Passable Adjoins. `buildStairs` and `buildSwitchbackStairs` expose the editor's straight and landing flights (the first step is at floor level; the flight's near face is a full wall to join to the room below). A cut that crosses a slanted edge puts a corner on it. Levels written before 0.7.0 rounded that corner to eight decimals, so it was no longer exactly on the edge; RED now writes such a corner with the shortest spelling that reads back exactly, so a save no longer moves it (corners eight decimals hold, or that differ only by arithmetic roundoff, keep JED's spelling). The geometry operations still admit a room read back from an older file with the allowance the editor's geometry tools give their inputs, so it can be cut again and gain rooms beside it; a room an operation builds is held to the strict bar unless its input needed the allowance. `shadowCuts {mode?, lights?, maxEdge?, minPiece?, maxNewSurfaces?, maxDepth?}` adds corners where the lights' shadows fall, before `bakeLighting`: `shadowEdges` (the default) follows each light through the openings and cuts the surfaces it partly reaches along the edges of its view, and `sizeLimit` splits every surface a light reaches until no edge is longer than `maxEdge` (default 0.5). `lights` picks lights (default every light). Openings are never cut, and a cut that would give a surface more than 24 corners is undone. Its entry in `editReports` gives `plannedCuts`, `surfacesCut`, `newSurfaces`, `refusedCuts`, `cornerLimitSkips`, `lightsOutside`, `walkTruncated` and `pieces`; over `maxNewSurfaces` (default 2000) it is refused with `shadowCutsOverBudget` and nothing changes. `removeShadowCuts {sectors?}` merges coplanar neighbouring solid faces with the same material, flags, modes and texture scale back together (`merges`, `sectorsChanged`). Like the other cuts, both are refused once COGs are placed. See [shadow cuts](lighting.md#shadow-cuts).
- Texturing: new faces from `addBox`, the recipes and extrusion are mapped upright at 320 texels per JKU, as the left-right mirror image of the stored texture. That is the handedness the retail levels' walls have (3,831 of 3,958 textured walls in five retail multiplayer levels), and it only shows on a texture with lettering, digits or an arrow. To remap existing faces, `batchUvProject {surfaces, direction, origin, texelsPerUnit}` projects along one world axis with U x V equal to the direction, and the direction is the way the projector looks. The game shows the image as stored where U x V points away from the viewer, so for a wall whose texture must read as drawn, give the axis opposite its inward normal (a wall facing into the room along `+y` is projected with `-y`). The axis along the inward normal gives the mirror image, matching new faces. The horizontal directions run V down the wall, which is how the game draws a texture's rows, so both are upright. (Before this change they ran V up the wall and every projected wall texture was drawn upside down.) `snapshot` draws textures with the same handedness as the game, so a mirrored texture shows there first. `batchUvCylindrical` runs V along `axisDirection`; point the axis down, `[0, 0, -1]`, for an upright texture around a vertical cylinder. `batchUvSpherical` already runs V away from the pole.
- Header: `setLevelHeader` sets gravity, sky, horizon, mipmap, LOD, perspective and Gouraud distances; `new` now writes the editor's defaults for these and its spare export capacity.
- Resources: `renameMaterial {materialIndex, name}` and `renameColormap {colormapIndex, name}` replace the scaffold's placeholder names everywhere they are bound. `importTemplates {templates, templateSource}` copies templates (and everything they inherit or name) from a hash-bound `.tpl`; `defineTemplate {name, parent, values}` adds a level template, parsed by the same reader, for a prop whose model lives in a custom archive. The JKL writer keeps the templates that Things, placed COGs (template-typed values) and other saved templates name, plus the static templates the level defines; with a patch-root `templateSource` it also writes the remaining static templates and the templates that only scripts name. It drops templates nothing references, so define or import a template in the same patch as the `addThing` or `addCog` that names it. `defineTemplate` refuses names and values the game would split differently: commas, a leading `;`, backslashes and unbalanced parentheses.
- COGs: `addCog` bindings take `{symbol, kind, targetIndex}` for thing/sector/surface/cog symbols (`-1` for none), `{symbol, kind, sector, surface}` for a surface without counting surface numbers, and `{symbol, kind, value}` or `{symbol, kind, default: true}` for every other kind (flex, float, int, vector, sound, material, model, template, keyframe, ai). `defaults: true` on the operation gives every unbound non-reference symbol its declared default; references are never filled in, and a symbol with no declared default is refused. Values are stored the way the engine reads them back: an int is a decimal 32-bit number (a `0x` value is converted to decimal; a fraction or trailing text is refused, and a JSON number is written exactly), a flex or float is a finite decimal number within float range, a vector is `(x/y/z)`, and a resource name has at most 31 printable characters and no spaces, commas, `=`, `#`, quotes or backslashes. The binding's `kind` must match the declaration (flex and float are interchangeable). A vector's default is `(0/0/0)` because the engine ignores a vector initializer, and an int default written in hex is refused because the engine reads it as 0. `setCogValue {cog, symbol, value | default: true}` changes one typed value of a placed COG under the same rules (references go through `rebindCogReference`). `rebindCogReference` takes `sector`/`surface` the same way as `addCog`.
- Export capacity: whenever a patch supplies patch-root `resourceArchives` and writes a JKL or NDY level (from JKL, NDY, JED or NED input), the JKL writer reads what models, puppets, sound classes and scripts load and declares the SOUNDS, MATERIALS, KEYFRAMES and COG capacities the level needs. The receipt's `exportBuffers.dependencyScan` is `complete`, or `incomplete` with `warnings` naming what could not be read. The scan reads only those hash-bound archives, shared with the rest of the patch and admitted against the same expansion limits as COG resolution; a resource name in the level never opens a file on disk and reads as missing instead. Archives given on a single operation serve that operation and do not enable the scan. A patch-root `templateSource` (the master template file) also lets the writer emit the static templates and resolve templates that only scripts name; with the stock archives a scan is complete only with it. Without the scan a headless level can declare too little room and the game crashes while loading the player's own weapons; `resolveExportDependencies: false` keeps the document-only counts. The standalone `addCog` and `rebindCogReference` commands run the same scan and report the same receipt.
- COG scripts: `cog-compile <file.cog> <jk|mots> [--strict] [--listing]` compiles a script the way the retail game
  loads it. `loads` is the engine's verdict. `diagnostics` gives each finding's severity, code, file line and message:
  syntax errors with the engine's own line count, unknown verbs and messages, wrong argument counts, missing labels and
  exhausted engine tables. `handlers` gives each message's code address, and `limits` gives symbols, links, handlers
  and label slots against the engine's limits. `--listing` adds the compiled code, and `--strict` exits 5
  (`cogCompileErrors`) when the script won't load or has errors. See [the COG compiler](cog_compiler.md).
- Running COGs: `cog-run <request.json|->` compiles scripts and runs them on a simulated clock against a stand-in
  world built from an optional hash-bound level. Scripts take a placed COG's values (`placedCog`) or explicit
  `bindings`; timed events activate, enter, cross or damage objects, or send a message to one script. The result is
  a timed log of handlers, verbs, prints, arrivals and unsimulated verbs, then the things, surfaces and sectors that
  changed. Nothing is written. See [the COG runtime](cog_runtime.md).
- Gameplay assistants: the patch operations `doorAssistant{pieces,openOffset,speed?,wait?,light?}`,
  `elevatorAssistant{elevator,travel,button?,callAdjoins?,speed?,wait?,startWait?}`,
  `switchAssistant{pieces,openOffset,switches,speed?,wait?,light?}`,
  `objectivesAssistant{goals[{trigger,target}],goalBase?,exit?,endWhenDone?}` and
  `cutsceneAssistant{camera,trigger?,speed?,hold?,once?}` write path frames and one placed COG, or refuse with an
  `assistant…` code and change nothing. Surfaces are `[sector, surface]`; goal triggers are `reach` (a sector),
  `use` or `kill` (a Thing). Door, elevator and switch read their stock script from the patch's `resourceArchives`.
  `assistant-check <request.json|->` runs the placed COG's scenario and returns expectations, a timeline,
  handler-entry `breakpoints` with symbol watches and pending timers, and executed `unsimulated` calls.
  With `strict: true`, a failed expectation or unsimulated call exits 5 with `assistantCheckFailed`.
  `assistant-script <red_goals.cog|red_cutscene.cog> <new.cog>` writes RED's two scripts. `pursuitAssistant{enemy, route?,
  wakeSector?, wakeDistance?, moveSpeed?, ramDamage?, fireInterval?, survivalChance?}` places Lily's `kcswoop.cog`
  enemy controller from the COG template library. See [the gameplay assistants](gameplay_assistants.md).
- COG template library: `cog-templates [set]` lists the vehicle, enemy and item sets, or one set's scripts, roles,
  placed-COG values in order, template rows (or `namedTemplates`, the templates its scripts name, when the library
  has no rows for it) and the files the user supplies; `cog-template-write <set>
  <project>` copies a set into a project (`cog/`, `templates/<set>.tpl`, `templates/<set>.md`) and refuses with
  `cogTemplateConflict`, writing nothing, when a different file is in the way. The library is found beside
  red-agent (`data/cog_templates`), in `RED_DATA_DIR`, or at a folder given as the last argument. See [the COG
  template library](cog_templates.md).
- 3DO hierarchy and KEY authoring: `model-hierarchy <model.3do>` lists the node tree and its issues;
  `model-hierarchy-edit <model.3do> <request.json|-> <new.3do>` applies `rename`, `reparent`, `setTransform`,
  `addNode` and `removeNode` operations (`{inputSha256, operations}`) to a fresh 3DO or refuses; `key-from-model
  <model.3do> <new.key> [frames] [fps]` starts a KEY with a rest-pose track per node; `key-plan`/`key-apply` take
  `{"op":"computeDeltas"}`; `key-inspect <key> <model>` adds `modelIssues`. MCP: `sed_model_hierarchy`,
  `sed_model_hierarchy_edit`, `sed_key_from_model`. See [3DO hierarchy and KEY authoring](model_hierarchy.md).
- Assets: `mat-create <request> <new.mat>` maps 1–16 PNG (any PNG colour type, non-interlaced) or baseline/extended-sequential JPEG cels to an opaque MAT. Default `format: "indexed8"` uses a captured CMP palette and optional Floyd-Steinberg dithering; explicit `format: "rgb565"` writes palette-free 16-bit pixels and refuses palette-only options. Both support 1–4 mipmaps; the receipt reports colours used and the mean colour error. RGB565 receipts identify the format/bpp, and target-engine compatibility remains a separate qualification. Invalid cel counts refuse before image or palette reads. [Material Authoring](material_authoring.md) provides ordinary GUI controls for the same core conversion. `archive-create <request> <new.gob>` packs loose files (`{name, path, sha256}`) into a GOB, refusing duplicate or escaping names and reading MAT, 3DO, COG and template entries back by type. Mount the result as a `resourceArchives` entry while building and list it in the package request's `archives` so its files are copied into the level GOB.
- Native 3DO editing: inspect paged source meshes, positions and UVs, then plan a batch of existing UV edits or positive uniform mesh transforms. Apply checks the reviewed request and output hashes and publishes a fresh asset. See [native model authoring](native_model_authoring.md) for requests, receipts and journal behavior.
- Models: `model-import <request> <new.3do>` imports Wavefront OBJ or binary glTF 2.0 (GLB) into 3DO 2.1, with a hash-bound `source {path, sha256}` and explicit MAT mappings in `materials`. Each mapping supplies `{name, mat, size: [width, height]}` or `{name, mat, matFile: {path, sha256}}`. OBJ UVs become texels `u*width`, `(1-v)*height`; GLB uses `u*width`, `v*height`. The default y-up conversion is `(x, -z, y)`; `upAxis: "z"`, positive `scale`, `origin` (`base`, `center`, `none`), and `meshName` configure placement. Authored normals and hard edges are preserved by default; `normalPolicy: "recalculate"` explicitly derives smooth geometric normals. The receipt reports `normalPolicy` and `normalSplitVertices`; see [model lighting and import normals](model_lighting.md#import-normals). Meshes split to JK limits by default (512 faces, 512 vertices and 768 texture vertices per mesh; 64 meshes per model); `split: false` requests refusal instead. Every emitted mesh has an identity hierarchy node under one root. Materials, UVs, supported primitive types, finite coordinates and total limits are checked before create-only publication, and the output is read back through the core 3DO reader. Unsupported constructs refuse instead of being guessed. Pack the model as `3do\<name>.3do` and its MATs under `mat\` and `3do\mat\`.
- Model shading: `model-lighting-inspect`, `model-lighting-plan`, and `model-lighting-apply` inspect and bake ordinary 3DO assets using the portable core. They preserve all LODs and every non-light byte, require source and planned-output hashes, and publish a new model variant. Profiles are classic `jk`/`mots` for 2.1 scalar light and `ijim` for 2.3 RGB. See [model lighting](model_lighting.md) for complete requests, MCP tools, limits, and the distinction from per-Thing ARCHLIGHTING and runtime lighting.

### Optional stair rise warning

For `buildStairs` or `buildSwitchbackStairs`, add `"walkability":{"maxStepRise":0.03125}` when that value is a
limit you deliberately selected from your target-game testing. It is not a built-in safe player threshold.
`plan` and `apply` then return `stairWalkabilityReports`; an exceeded limit produces a warning without refusing or
changing otherwise valid geometry. No option means no new report field. A report never marks gameplay verified.

This compares requested internal risers at recipe creation only. It does not check external room joins, later edits,
width, headroom, player collision settings or actual movement. The first straight step is at the origin floor, so a
one-step flight has zero internal transitions; a switchback with one step per flight still has two landing transitions.
Keep the request with its receipt: changing only the advisory does not change the level-byte output hash.
See the [exact advisory contract](agent_design.md#optional-stair-rise-advisory), then test physical traversal.

What retail JK climbed, as a starting point for your own limit: in JK 1.01's 16-bit renderer, with the stock player,
holding forward only, straight stairs with 0.10 JKU treads climbed at 0.025 and 0.031 JKU risers, and 0.12 JKU treads
climbed at 0.04. Treads of 0.06 JKU stopped the player on the first steps even at a 0.0308 riser. A shallow tread is
the bigger risk: keep treads deeper than the player's collision radius (0.065 JKU). Switchbacks with 0.10 JKU treads
and 0.029–0.031 JKU risers, joined to galleries with `connectSurfaces`, climbed end to end in both the 16-bit and
8-bit renderers. These are one level's measurements, not a guarantee for other player templates, jumping, crouching
or strafing.

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

`snapshot` and `overview` default to the built-in `software-v1` world-surface renderer. Omitting
`thingMode`, or setting it to `"none"`, retains the existing image and receipt contract. Opt in to
`"thingMode": "static"` for real placed 3DO geometry and supported SPR cards in the same depth buffer:

```json
{
  "width": 640,
  "height": 360,
  "thingMode": "static",
  "fullbright": true,
  "camera": { "position": [0, -2, 1], "target": [0, 0, 1] },
  "resourceArchives": [{ "path": "assets.gob", "sha256": "<captured SHA-256>" }]
}
```

Pass this request to `red-agent snapshot <level> <request.json> <new.png>`, or as `request` to
`sed_snapshot`. `overview`/`sed_overview` accepts the same option; its emitted `snapshotRequest` retains
static mode and resource bindings for exact replay. Automatic framing still uses world surfaces, not Thing bounds.
Supply an explicit camera when the desired objects lie outside that framing. Unsupported mode values or types are
usage errors. Existing output files remain protected.

Player starts (Things whose effective type is `player`) are left out by default: a start is a spawn point, the game
never draws one in front of the player's eyes, and a capture taken from a start would otherwise look out through the
model's head. Each is reported in `thingPreview.diagnostics` as `omitted-player-start`, and `thingPreview.playerStarts`
says `omit`. Pass `"playerStarts": "draw"` to see where spawns are. `playerStarts` without `"thingMode": "static"`, or
with another value, is a usage error; an overview's `snapshotRequest` carries it for replay.

`red-agent schema snapshot` and `red-agent schema overview` return the two request contracts. A snapshot is 64 to
2048 pixels on each side and at most 2,097,152 pixels. An overview that only frames accepts any positive size; with
`output` it renders, so it takes the same upper limits (2048 per side, 2,097,152 pixels) and refuses a larger image
as a usage error before allocating it.

The opt-in renderer identifies itself as `software-v1-static-things`, with scope
`world-surfaces-and-static-things`. Its deliberately bounded profile is:

- Actual 3DO 2.1/2.3 LOD0 geometry in static hierarchy/rest pose, including pivots and shared-mesh instances,
  followed by the authored Thing position and pitch/yaw/roll. The retained model insertion offset is not applied
  again. 2.1 texture-pixel UVs and 2.3 normalized UVs are independent of the world's UV convention.
- Effective local/embedded-template `model3d` (or `model3do`) and `sprite` references. A nonempty model reference
  takes precedence; an unavailable model is reported, not replaced with a guessed sprite or icon. No external
  template database is discovered. An explicit `model3d=none` can leave a sprite eligible.
- JKDF2/MotS SPR resources with exactly one eleven-token noncomment record, positive finite dimensions and
  stored type 0, 1 or 2. These use the classic camera-facing frame, including authored offset and roll.
  IJIM projects, fourteen-token IJIM records and other sprite profiles are explicitly unsupported/invalid.
- Cel0, mip0, nearest texture sampling, alpha cutout and shared world/object depth. Double-sided model faces
  and sprite cards remain visible with back-face culling enabled. Hidden mesh/face geometry writes no depth.
  Other nonzero geometry modes retain the software renderer's filled approximation.

The additive `thingPreview` receipt reports `modelsPrepared`, `spritesPrepared`, submitted polygons/triangles,
per-Thing `diagnostics`, missing/malformed `materialIssues`, and the archive index, entry and SHA-256 of selected
resource payloads. `prepared` means geometry was assembled, **not that any pixel was visible**. Missing/invalid
assets, invalid placement sectors and unsupported sprites are diagnosed and skipped without marker substitutes;
unavailable MATs retain the existing flat fallback and are reported. Resource selection deliberately retains the
legacy first archive/entry basename match, with the master CMP taken from the last match. This is an explicit
preview lookup policy, not a claim of engine alias precedence. Only supplied hash-bound archives are read; model
or material names never open arbitrary files.

Static preparation admits at most 4,096 Things, 64,000,000 conservative template-lookup work units across the actual
inheritance chains, and 512 distinct primary model/sprite lookups (including misses) when using the
indexed archives supplied to the snapshot. The generic core resolver retains its 128-reference default.
Repeated instances of a model use one lookup but count separately toward geometry budgets. Both profiles retain
16 MiB per primary resource and 64 MiB total primary bytes, 128 MiB estimated retained decoded asset cache,
250,000 transformed vertices, 1,000,000 face corners and 65,536 meshes across all instances. Names are capped at
255 bytes before instance expansion. The underlying model parser retains its independent decode/transform limits;
cache admission occurs after that bounded parse and does not include transient parser peak or allocator overhead.
Static rendering additionally admits 512 distinct texture lookups, 16 MiB per texture source, 64 MiB total texture
source bytes, 64 MiB decoded texture cache and 256,000,000 candidate raster samples across both world and Things.
Budget exhaustion refuses the whole image before publication. These are preview admission limits, not game-format
limits or adjustable engine settings.

World surfaces shade by sector ambient light unless the request asks for `"lighting": "baked"` (or for
`fullbright`; the two are refused together). Baked lighting reads each corner's stored light and combines it the way
the game does: `max(sector extra light, min(1, corner light + face extra light))`, interpolated across the face, with
light mode 0 (fully lit) drawn at full light. JK reads one value per corner (the one a bake writes); MotS and IJIM read
RGB. A sector on a colormap other than the master one is multiplied by that colormap's header tint, as the hardware
renderer does; each tint is read from the first matching CMP in `resourceArchives` and reported in the receipt's
`colormapTints` (`resolved`, `missing` and drawn untinted, or `invalid`). Use it to judge a bake, emissive faces and
tinted colormaps before starting the game:

```json
{ "width": 640, "height": 360, "lighting": "baked",
  "camera": { "position": [0, -2, 0.3], "target": [0, 1, 0.2] },
  "resourceArchives": [{ "path": "assets.gob", "sha256": "<captured SHA-256>" }] }
```

To preview JK indexed world surfaces, add `"renderProfile": "indexed8bit"` to either request and mount
the GOB files containing the original MATs and complete CMPs. Check discovery for the `renderProfile` field before
sending the request. Older binaries can ignore unknown fields. Verify `appliedProfile == "indexed8bit"` and
`renderer == "software-indexed8bit-v1"` on the successful PNG receipt. `indexedResources` lists each selected
archive index, entry name, SHA-256 and CMP slot where applicable. Archive and entry order break basename ties.
Slot zero supplies the master palette. Without a table the level's master CMP (or `dflt.cmp`) supplies it.
Name-only sector bindings resolve against these slots and may add a named CMP. The agent requires all declared slots.
The PNG remains RGBA for ordinary image viewers.

The indexed default is baked scalar lighting. Omit lighting when requesting fullbright to display source
indices through the master palette. Its snapshot receipt reports `fullbright: true` and `lightingSource: "fullbright"`
and omits `lighting`. A baked indexed snapshot includes `lighting: "baked"`. Explicit ambient is refused for this profile. Both framing-only and rendered
indexed overviews require 64 to 2048 pixels per side and at most 2,097,152 pixels so their emitted request replays. Static Things, translucent faces, sky projections, RGB MATs and unsupported modes cause a refusal before
publication. The profile admits 256 CMP slots and 512 unique MAT names, 16 MiB per resource, 64 MiB combined
selected source bytes and 64 MiB for retained texel planes, and 256 million raster samples. An overview without
`output` returns the replayable request; only a rendered output has an applied profile. [Modes and shading](lighting.md#jk-indexed-world-snapshots)

The default renderer's baked image approximates the 16-bit hardware renderer. The 8-bit renderer looks colours up in the colormap's
64 light tables instead, so banding and tinted dark tones can differ; compare in the game for that. Things keep
sector ambient light. MotS ARCHLIGHTING, runtime lights, the camera-distance Gouraud switch, translucency blending,
KEY/puppet animation, runtime LOD/cel changes, active-Thing filtering and COG-driven state are not reproduced. Static mode includes authored placements, including spawn/player
models that may not all exist simultaneously at runtime. `gameplayVerified` and `engineRenderingParity` remain false.
Keep camera, resource hashes and render profile fixed for comparisons; inspect the image rather than treating PNG
creation as visual approval. Use the editor or declared runtime for excluded content. The
[Engine Capture dialog](game_testing.md#reproducible-engine-capture) can test a declared GOB/GOO directly and run
bounded ordinary-control recipes with a compatible Linux agent engine; those retain their own evidence limits.

Geometry mode zero is hidden even when an adjoin retains a material. Translucent faces (face flag `0x2`) are drawn after everything opaque, farthest first, blended over what lies behind them at the engine's 90/255, testing depth without writing it, so a mirror floor shows its reflection and a window or water surface what it covers. Other nonzero modes use filled rasterization; dedicated point/wireframe presentation and exact engine Solid/Textured shading are outside this preview's scope. Orthographic depth uses view-space distance, so nearer surfaces remain in front independently of input order.

Keep structural validation, editor rendering, game loading and player interaction as separate evidence. A three-room portal test does not certify retail loading; a script event does not prove player Use reach. See [capability matrix](capability_matrix.md) and [gap register](gap_register.md) for the remaining product and platform gates. Current-source tests and independent source reviews do not replace native macOS/Linux execution or Windows UI qualification.

### Ray transport for a lighting bake

Set `transport` to `"ray"` on a `bakeLighting` operation in a plan or apply request. The agent and its MCP adapter use the portable CPU intersector. A request can set `raySamples` (1–128, default 32), `rayBounces` (0–4, default 2), `rayReflectivity` (0–1, default 0.65), `skyColor` (RGB, each 0–1), and `skyIntensity` (0–1000, default 1). Zero bounces still samples sky and FULLYLIT radiance. The report includes the backend, actual ray queries, limits, blocker triangles and dependency count. Point-light shading, water absorption and stored-corner floor/cap controls remain available.

```json
{"operations":[{"op":"bakeLighting","transport":"ray","resourceRoots":["episode"],"raySamples":32,"rayBounces":2,"rayReflectivity":0.65,"skyColor":[0.65,0.8,1],"skyIntensity":0.8}]}
```

Only explicitly sky-flagged, non-adjoined surfaces sample the sky. Their resolved diffuse color tints the selected sky radiance. Opaque single-cel MATs supply mip-zero average linear RGB after sRGB decoding of each texel, with the sector or master CMP required for indexed colors. FULLYLIT world surfaces emit their resolved material color once. Visible, enabled static architecture contributes opaque absorbing blockers from its authored placement, hierarchy bind pose and geoset zero: models with LEVELGEO (Thing flag 0x8), and models drawn in the ordinary Thing pass with IGNOREGOURAUDDISTANCE (0x04000000), as a JO/JA import with `worldModelRenderPass: "ordinaryThing"` writes them. A template with `move=path` and no frames is static; a moving ordinary-pass model is left out of the blockers. Ray transport writes sector corner samples only. A missing required MAT, CMP or 3DO, a moving or puppet-animated architectural model, a multi-cel, color-keyed or alpha-channel MAT, a translucent face, or an unsupported model polygon refuses the operation before publication. The patch root accepts the usual hash-bound `templateSource` for placements whose inheritance uses project templates. A missing template refuses the ray bake before assuming a Thing is dynamic. Ordinary dynamic actors and items are outside this static-lighting pass. Model blockers affect sky and indirect rays. The existing direct-light calculation retains its sector-geometry occlusion rules.

Use the default `legacy` transport for mirror-floor levels and the legacy emitter `intensity`, `range`, `skyRange`, `bounceStrength`, `bounceRange` or `mirrorReflectivity` settings. Ray transport rejects these options. Both transports use the existing resource-root/archive lookup and publication checks.

### Check projectile templates in an authored level

Run `red-agent projectile-audit <level.jkl> <jk|mots>` on the final serialized
JKL (extract it from a GOB/GOO first if necessary). The command does not modify
the level or require stock archives. It reports known ordinary stock firing
templates for placed weapons and the assumed stock starting Bryar and fists.
Case-equivalent duplicate root or dependency declarations give an `unverified` finding.
A missing row in the captured JKL is not proof that the template is absent
from an external static world. A bound row is not proof of successful firing,
asset resolution, or the behavior of custom inventory and COG overrides.
