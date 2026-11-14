# Archive and Package Authoring

**Tools > Archive and Package Authoring…** creates a new GOB or GOO from explicit
files. Its two tabs expose the same portable authoring operations as
`red-agent archive-create` and `package-plan` / `package-apply`. The dialog does
not save or modify the open level, change its filename, mount its output, or
launch a game. Use [Save JKL and Test](game_testing.md) for an editor export and
private test installation.

## Resource Archive

1. Add one row per source file. Choose the file for the selected row and enter its
   logical archive destination, such as `mat/custom.mat` or `3do/prop.3do`.
2. Arrange rows with **Up** and **Down**. Physical entry order follows this list.
3. Select GOB or GOO, then **Prepare**.
4. Inspect **Contents**, **Issues**, and **Full report**. The review includes the
   canonical physical destinations, byte counts, payload hashes, parser readback
   results, and layout warnings.
5. Choose a new output filename matching the reviewed format and **Create new
   archive**. An existing destination is refused without replacement.

Runtime destinations use the existing engine-facing authoring rules: lowercase
ASCII names and backslash separators, at most 127 bytes, no absolute paths or
traversal. Case-colliding destinations are refused. This normalization applies
only to authoring destinations; raw GOB reader/writer fidelity, resource references
inside files, and captured payload bytes are unchanged.

Known MAT, 3DO, COG-symbol, and TPL payloads must pass their existing readers.
Other file types remain byte-preserved and are reported as having no typed
readback. This is structural admission, not proof that an asset works in a game.
The inherited CLI extension classification uses the platform's lexical
`std::filesystem::path` behavior; unusual nested dotfile names are not a portable
typed-file convention. Use explicit ordinary extensions for portable readback.

There may be 1–4096 entries, at most 256 MiB per source, and at most 512 MiB total
including the GOB header and index. A failed entry poisons the captured builder;
no partial archive can subsequently be finished. A completed archive is reopened
with the GOB reader before it becomes a publishable review.

## Exported Level

1. Choose an explicit exported `.jkl` file. Native `.red`, `.sed`, and `.jed`
   documents must be exported first.
2. Set the level filename inside the package, episode title and type, and GOB/GOO
   format. These are ordinary controls; no request JSON is required.
3. Add the GOB/GOO archives used to resolve references. Mark stock archives in the
   **Stock** column. Resource archives are searched before stock archives, with
   list order retained inside each group. **Full report** records that effective
   order and the source files' raw hashes.
4. Optionally add extra referenced asset names and resource folders. These are
   roots for dependency resolution, such as a COG and its `cog` folder; they are
   not loose filesystem search directories.
5. Set node, edge, depth and byte budgets and the explicit option to allow
   unverified references. Prepare and inspect the complete plan before creating
   the new archive.

The dialog calls `planLevelPackage` with captured bytes and its existing options.
The [package node budget contract](package_node_budget_design.md) counts the
episode, level, and distinct resolved resources, including stock omissions. A
zero depth limit means no extra depth restriction. The byte limit is an actual
maximum output size, so zero does not produce a publishable package. Stock
resources can satisfy references while their bytes are omitted from the output.
The report distinguishes included entries from stock omissions, shows each
source category and hash, and lists every issue and unverified dependency scope.

The displayed output SHA-256 binds the exact prepared archive bytes. The manifest
SHA-256 identifies the planner's normalized inputs and decisions. Archive hashes
inside that manifest describe normalized entry data; they are different from the
raw archive file hashes listed separately in the review. Historical CLI package
materialization resolves each physical archive row by its name, including aliases;
the GUI preserves that rule and charges the selected payload before allocating it.

Capture is limited to 128 MiB of JKL, 16 archives totaling 512 MiB of raw archive
bytes, 100,000 physical archive entries, 512 MiB of expanded payloads, and 1024
extra roots. The index is admitted before parsing or materialization. Planner
controls allow up to 100,000 nodes, 1,000,000 edges, 100,000 depth, and 512 MiB
of package bytes. These are dialog admission bounds, not new file-format limits.

## CLI and MCP request discovery

Obtain the package request contract without opening a level:

```text
red-agent schema package-plan
red-agent schema package-apply
```

Both return the same standalone Draft 2020-12 JSON Schema. MCP `sed_schema` and
`sed_discover` accept either name in their `topic` field, and the `request`
properties in `sed_package_plan` / `sed_package_apply` embed that exact shared
schema. The no-topic inventory retains its existing contract. Discovery describes
the existing parser; it neither changes package planning nor applies stricter
schema validation ahead of the legacy commands.

The JSON request is separate from the explicit source, output and review hashes:

```text
red-agent package-plan mission.jkl package.json
red-agent package-apply mission.jkl package.json new-mission.gob <outputSha256> <manifestSha256>
```

Use the two hashes returned by the reviewed plan for the final arguments. MCP
uses `path` and `request`, plus `output`, `expectedOutputSha256` and
`expectedManifestSha256` for apply; `operationId` retains its optional journal
contract. Both routes require an exported `.jkl`, and output is create-only.

For example, this minimal request is schema-valid, with empty explicit resource
sets. It can produce a blocked plan when the level names unavailable resources:

```json
{
  "schemaVersion": 1,
  "episode": {
    "episodeTitle": "My Mission",
    "levelFileName": "mission.jkl"
  },
  "archives": [],
  "stockArchives": [],
  "extraResources": []
}
```

| Request field | Existing package contract |
| --- | --- |
| `episode` | Required object. `episodeTitle` is 1–80 printable ASCII characters excluding `"` and `#`. Optional `levelFileName` is a plain `.jkl` filename, defaulting to the source basename. Optional `episodeType` is a signed 32-bit integer, default 1. |
| `schemaVersion` | Optional; when supplied it must equal 1. |
| `inputSha256` | Optional 64-digit hexadecimal hash of the raw JKL. Supply it to bind the request to the reviewed source; hash comparison ignores hexadecimal case. |
| `archives` | Optional ordered array of objects with `path` and raw-file `sha256`. `fileName` is the existing path alias. No per-entry `stock` flag is consumed by this package parser. |
| `stockArchives` | Optional ordered array of filesystem path strings. These are not hash-bound objects. Stock resources satisfy references but their bytes are omitted from output. Resource archives are searched first. |
| `extraResources` | Optional array of logical roots with `name` and nonempty `folder`. `fileName` is the existing name alias. These fields never name loose filesystem search directories. |
| `archiveExtension` | Optional `.gob` or `.goo`, case-insensitive; default `.gob`. |
| `options` | Optional object containing the budgets below and Boolean `allowUnverifiedReferences` (default false). |

All three arrays may be empty or omitted. Unknown root and nested fields remain
accepted by the legacy parser. In the CLI, `path` takes precedence over
`archives[].fileName`, and `name` takes precedence over
`extraResources[].fileName`; a shadowed alias is not read. An invalid primary
field does not fall back to its alias. Prefer the primary names in new requests.

MCP retains its additional filesystem policy: every supplied archive `path` and
`fileName`, including a shadowed alias, must be a nonempty path string admitted
under the workspace or a declared resource root. The same policy covers stock
paths, resolves existing symlinks, and restricts writes to the workspace. This
admission is a runtime condition, not a promise made by matching the shared JSON
Schema. Direct CLI paths retain their ordinary filesystem semantics.

| Option | Default | Zero behavior |
| --- | ---: | --- |
| `maxNodes` | 4096 | Blocked: the episode and level already need two nodes. |
| `maxEdges` | 8192 | No resource-reference edge is admitted. |
| `maxDepth` | 0 | No additional depth restriction. |
| `maxPackageBytes` | 134217728 | Blocked: no nonempty archive can fit. |

Budget values are nonnegative integers representable by the running build's
`size_t` after JSON number decoding. Discovery emits the corresponding exclusive
upper bound; it does not import the GUI's smaller control ranges or 16-archive
limit. The independent CLI admission limits remain 1 MiB of request JSON,
128 MiB of source JKL, 512 MiB of captured archive bytes, 100,000 physical archive
entries and 512 MiB of materialized payloads. Request byte limits also bound how
many array elements can be supplied. `allowUnverifiedReferences` can turn missing
resource errors into warnings; invalid physical paths, collisions and exceeded
budgets still block the package.

Read `x-runtimeConstraints` as well as the field schema. A syntactically valid
request can still fail source/archive identity, file admission or create-only
publication, or return `canBuild: false`. Invalid request shapes use exit 2;
input failures and blocked plans use exit 3; stale source/archive/reviewed hashes
use exit 4; publication refusals such as an existing output use exit 5. A
buildable plan still reports `runtimeClosureVerified: false`. Resource closure,
valid JKL grammar and playable behavior require their own qualification.

## Review, cancellation, and publication

**Export full report** writes the complete captured review as UTF-8 plain text to
a new filename, including a blocked package plan. The tables and report are not
truncated. A blocked plan cannot create an archive. A package report always says
that runtime closure is unverified; successful packaging does not prove retail,
OpenJKDF2, palette, COG, multiplayer, or platform behavior.

Input edits invalidate the review. Preparation and staging run on immutable
worker snapshots, with cancellation checks during bounded file reads and archive
expansion. Core parsing/planning has no interrupt callback, so cancellation there
discards the eventual result; it does not claim to stop work instantly. Closing
the dialog cancels publication without waiting for the worker on the GUI thread.

Before publication, sources are recaptured and compared by canonical path, size,
and SHA-256; the output bytes must retain their reviewed identity. The adapter
checks the owning document/resource context at command entry and worker completion.
A changed context, changed source, cancellation, or owner-check exception prevents
publication and asks for a fresh review. These checks detect drift; they are not
a filesystem lock against an external writer after the final check.

The adapter uses the existing staged create-only file protocol. It checks a new
destination, writes and closes a sibling temporary file, rechecks sources, then
publishes only while the owner and generation remain current. Existing files and
symbolic-link destinations are refused. Filesystems that cannot provide the
required create-only publication primitive fail with the operation's error;
choose a local supported destination. If a post-publication host notification
fails, the status still reports the file as created and includes that error.

The portable helper in `sed/assets/runtime_archive.hpp` owns deterministic archive
admission, canonical destinations, typed readback, hashes and output bytes. The Qt
adapter owns file capture, workers, review widgets and publication. The agent
retains its request/hash checks and exact error categories while sharing the
same captured-entry helper. Neither adapter adds Qt or filesystem discovery to
the portable authoring operation.
