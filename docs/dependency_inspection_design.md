# Dependency inspection

## Purpose and scope

`dependencies` and MCP `sed_dependencies` inspect captured raw JKL plus explicitly ordered, hash-bound GOB/GOO
archives. They return a deterministic graph explaining which supported reference led to which resource and the
physical archive row that supplied its bytes. They do not assemble a package, publish a file, use a journal, discover
loose files, load a native workspace, or prove runtime closure.

This is a supported lexical-reference scan. It is distinct from asset-grammar validation, the richer JKL export-capacity
scan, and the GUI's live document/resource drafts. JK/MotS/DroidWorks dialect acceptance is not inferred from `.jkl`.

## Architecture and alternatives

Wrapping package-plan would still assemble episode/GOB output and cannot recover discarded parent/row evidence.
Unifying native export, prefabs and packaging would require a much broader compatibility redesign. The implementation
therefore shares reference visitors and archive selection with packaging while retaining separate traversal policies.
This is the implementation choice made for the authorized inspect-only feature.

Portable core owns graph traversal and value receipts. Shared stop-capable visitors preserve package root/child rules;
shared archive indexes return row ordinals and lookup tiers. Package assembly retains its existing queue, budgets,
callbacks, scopes, diagnostics, manifest and output ordering. A captured-byte view on `GobArchive` lets inspection
borrow physical payload ranges without file I/O or eager entry expansion. An archive opened from a path rather than
captured bytes is rejected by the inspector.

## Request and transport

```text
red-agent dependencies <level.jkl> <request.json|->
sed_dependencies {"path":"level.jkl","request":{...}}
```

The closed request object requires `schemaVersion: 1`, `inputSha256` and `archives`. Each of at most 16 ordered archive
objects requires `path` and `sha256`, with optional boolean `stock` (default false). An empty array is allowed.
`limits` is an optional closed object containing the limits below. `extraResources` is an optional array of at most
1024 `{name, folder}` logical references, with both fields required (name 1..1024 UTF-8 bytes,
folder 0..128 bytes). Filesystem paths are nonempty, NUL-free and at most 4096 UTF-8 bytes. The source must have a plain JKL basename of at most 255 bytes. Hashes are exactly 64 hexadecimal digits,
case-insensitively accepted and echoed lowercase. Unknown fields, wrong types, fractional/negative/oversized limits,
extra CLI arguments and non-JKL source paths are usage errors. MCP rejects `output` and `operationId` and admits
source/archive paths under its existing workspace/resource-root and symlink policy. Logical resource strings never
open files. One shared Draft 2020-12 request schema is used by focused CLI discovery and the MCP catalog.

Valid diagnostic reports return exit 0 / `ok: true`, including missing references and budget stops. Read errors retain
exit 3; source/archive hash mismatches retain exit 4. No successful report implies that every reference resolved.

## Graph and identity

Node zero is the captured level. There is no generated episode node. Resource nodes use the package's flattened,
case-insensitive destination key. Every destination reuses its first resolved, missing or ambiguous result. With
immutable archives and global basename fallback, a genuinely missing reference cannot later acquire an exact winner
with the same basename. An ambiguous result can precede an exact authored alias; that alias still reuses the first
ambiguity. Every admitted occurrence retains an edge, including repeated incoming
and cycle-closing edges. A resolved resource is traversed at most once, in breadth-first discovery order.

Edges retain parent/target IDs, relation, authored name and folder, and whether the target was reused. Source/child
ordering and relation labels match package discovery. Stock resources still have their children visited.

Selection checks each archive for folder/name and then authored-name exact matches before any basename fallback.
Archive order wins within a tier. An ambiguous basename in an earlier archive does not hide a unique winner in a later
archive. Physical aliases count for basename ambiguity; exact normalized aliases select the first physical row.

Each selected resource reports archive ordinal, physical row ordinal, original row name, payload offset/size, payload
SHA-256 and lookup tier. Source archive metadata separately reports ordinal, display label, captured raw-file SHA-256,
byte length and physical-row count. The raw-file digest is not the package manifest's normalized-entry digest.
Same-filename archives remain distinguishable by ordinal and digest. Tiers are `folderExact`, `authoredExact` and
`basename`; archive order wins within the exact phase. Reused edges do not claim another lookup. The inspector reads
the selected physical row directly. The legacy package adapter's name-based materialization remains unchanged;
leading-slash aliases can therefore expose different physical payloads in inspection than that adapter materializes.
Inspection does not certify byte identity with a package produced through that legacy adapter.

The JSON receipt declares `textEncoding: "ascii-byte-escape-v1"`. Dependency-command error messages use this same
encoding within the existing error envelope, preserving error codes and exit statuses. Captured/user-origin identifier strings (labels,
paths, authored names, folders and entry names) use reversible ASCII byte encoding: literal backslashes are doubled,
bytes outside 0x20..0x7e become `\xHH` with uppercase hex, and other bytes are unchanged before normal JSON escaping.
This preserves arbitrary captured byte spellings while always producing valid UTF-8 JSON. Core value records retain
the original bytes. Fixed protocol labels and hexadecimal hashes remain ordinary ASCII. Request strings must be valid
UTF-8; JSON Schema character lengths are supplemented by explicit runtime byte limits.

## Reference coverage

Roots preserve the package rules: JKL materials/resource tables, template resource parameters beginning at the third
token, placed COG scripts, GEORESOURCE colormaps, then explicit extra roots. The first meaningful row after SECTION
is its header. Transitive 3DO reads every `.mat` token; PUP reads KEYs in the second token; SND reads its second tokens;
SPR reads its first meaningful token. PUP/SND/SPR retain their existing comment and whitespace semantics.

COG defaults are followed only from a complete schema, including local resource symbols. Matching quotes are trimmed;
whitespace-containing defaults remain excluded as today. Every COG discloses `cog-literal-references`; incomplete
schemas also disclose `cog-resource-defaults`. AI and unknown resource families disclose unsupported dependencies.
KEY, CMP, MAT and WAV are leaves in this model. Empty SPR and incomplete schemas are explicit coverage issues.

## Completeness and accounting

`scanComplete` means the supported traversal exhausted its admitted work; it is false after a work/budget stop.
`detailsComplete` independently says whether all graph/issue rows were returned. `allReferencesResolved` is false if
an examined reference was missing or ambiguous, null if scanning stopped before a clean conclusion, otherwise true.
`runtimeClosureVerified` is always false. Unverified scopes and typed issues distinguish unsupported/incomplete
dependency readers from missing, ambiguous and budget-stopped results.

Observed node/edge/issue counts are lower bounds when scanning stops; corresponding totals are null. Detail limits
alone preserve exact completed totals. Omitted detail never implies an empty graph. IDs are local to one deterministic
receipt; a detail prefix does not describe the entire observed graph.

Limits must decode to finite, whole, nonnegative JSON numbers within their bounds. The existing JSON parser uses
double precision; this contract validates decoded values, not mathematical precision beyond that parser. Integral
decimal/exponent forms are accepted. Zero stops the relevant work (except maxNodes, whose minimum is one).

| Limit | Default | Maximum |
| --- | ---: | ---: |
| maxNodes | 4096 | 16384 |
| maxEdges | 8192 | 65536 |
| maxDepth | 64 | 1024 |
| maxWorkUnits | 1000000 | 4000000 |
| maxIndexEntries | 100000 | 100000 |
| maxPending | 4096 | 16384 |
| maxScanBytes | 67108864 | 134217728 |
| maxResourceBytes | 134217728 | 536870912 |
| maxStringBytes | 4194304 | 67108864 |
| maxDetailRecords | 512 | 4096 |
| maxDetailBytes | 262144 | 4194304 |
| maxIssues | 128 | 4096 |

One work unit is precharged for each physical row indexed, physical text line or token visited by the shared
reference reader, reference admission attempted, and per-archive/per-tier lookup attempt. Failed admission does not increment the counter.
COG line/token preflight is charged before invoking its parser. The preceding fixed COG cap check visits at most
256 KiB and 1025 lines outside this counter; it admits or refuses the reader before parsing. String accounting is cumulative and conservatively
charges every retained index/cache/graph string copy before copying, plus transient lookup keys before construction. Scan bytes charge the complete
level and each traversed dependency text before visiting; resource bytes charge each newly selected payload before
hashing. Pending means queued, unvisited resolved text nodes; known leaves and unsupported families are classified immediately. Queue/node/edge limits apply before insertion. The first
operational refusal stops traversal globally. Depth limits apply to new targets; existing cycle/repeated
targets can retain edges without another traversal. Issue storage and final detail records/bytes can truncate detail
without stopping traversal. Observed issue counts continue after issue storage fills. Detail uses one shared allowance
in node, edge, issue order and stops at the first row that cannot fit: 512 bytes of fixed overhead per node, 256 per
edge/issue, plus six times its raw string byte lengths. The bounded metadata envelope is separate. Counts and limit
reasons remain available when no detail fits. Zero detail limits only make existing omitted detail incomplete.

COG inspection additionally pre-admits at most 256 KiB and 1024 lines before the existing schema parser. A fixed
16 MiB evidence-expansion estimate prevents a comma-separated modifier list from multiplying full-line diagnostics:
for each raw line, count nonempty atoms split on whitespace or commas (even inside quotes/comments), then charge
`(atoms + 8) * (2 * lineBytes + 256)`. The check uses division before multiplication and allocates no strings.
It conservatively covers declaration/diagnostic string expansion; it is not a formal peak-memory bound. This guard
leaves the shared schema parser and package consumer unchanged. Exceeding that cap is a scan stop. These are operational
limits, not format maxima. Outer capture/index admission remains 512 MiB and 100000 physical entries per command;
the core also rejects uncaptured/oversized archive sets. Raw-input hashing and validated capture ownership are outside
the scanner work counter. Neither counter is a total-process-memory or wall-clock guarantee.

Receipts describe the captured bytes. Read-only return does not revalidate every path, lock the filesystem, or establish
an atomic multi-file snapshot. Existing package apply source checks, review hashes and journal replay remain unchanged.

## Verification requirements

Tests must establish chains, all supported families, stock traversal, repeated/cyclic/missing references, exact and
basename precedence, ambiguous fallback, duplicate physical rows, same-label archives, ambiguous-then-exact aliases,
unsupported/incomplete text, every work/detail boundary, and absence of file/journal writes. CLI/MCP strictness,
capture hashes, path/symlink admission and receipt/schema parity are required. Compare every public PackagePlan field
and output bytes before/after extraction for successful and blocked cases, including callback aliases and stock
promotion. Run all configured Release tests and separately identify unavailable native/game acceptance.
