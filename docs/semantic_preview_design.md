# Semantic change previews

## Goal and boundary

Generic `plan` and `apply` expose a deterministic, bounded `semanticPreview` alongside the existing input and output
hashes. The preview helps a person or agent review the net effect of a patch. It does not change the patch grammar,
operation ordering, serialization, validation, resource admission, publication rules, or the bytes bound by the
reviewed output hash. It is not a gameplay, preservation, or validation certificate.

The first version applies to generic level patches, including their COG operations. Standalone COG, animation,
material, model, conversion, and package commands keep their existing receipts. MCP `sed_plan` and `sed_apply`
forward the shared receipt without a second implementation. Discovery describes the preview without adding commands
or operations.

## Two comparisons

`semanticPreview.version` is 1. `inputSha256` and `outputSha256` repeat the exact byte identities in the surrounding
receipt. The `authored` view compares the captured input model with the isolated candidate after all patch
operations. The `persisted` view compares that same input model with the model reloaded from the exact proposed output
bytes. The existing serialization verification already performs this reload; the preview retains its result instead
of parsing or serializing again.

The authored view describes net candidate changes, not an operation history. The persisted view includes writer
rounding, filtering, normalization, and regenerated numbering. Neither view subtracts changes from the other or claims
to assign a serializer effect to a particular operation. Candidate fields are not themselves a serialized file.

## Addresses and coverage

`addressing: "snapshot-storage-slots"` means a path such as `/sectors/0/surfaces/2/material` addresses that position
separately in the before and after snapshots. Ordered arrays are paired by position. IDs, numbers, names, coordinates,
and ARCH wire indices remain ordinary stored values; none establishes cross-snapshot identity. Insertions, removals,
or reordering can consequently change many positions. The preview does not invent entity moves or deduplicate records.

Coverage is `level-model-v1`: all fields of the header and export capacity extras; project and UV-space tags; sectors,
vertices, surfaces, corners and adjoins; Things and ordered complete parameter storage; templates; COGs and explicit
link modifiers; lights including optional RGB metadata; hierarchy nodes; ARCH objects, samples, wire indices,
baselines and authoring provenance; material, colormap and layer tables; masterCMP, ppunit, lvisstring, curId and the
source-light metadata marker. Nested record fields, marks, bounds, normals, slot bindings and stored enum codes are
included. Surface `d3dId` is excluded because it is a renderer handle.

Import bookkeeping and retained serialization source are excluded: `importedJklSurfaceRefs`,
`importedJklCogRowsPresent`, every `importedGeo*` member, and every `retainedJkl*` member. Native optional section bytes
and other data outside Level are also excluded. The receipt carries these exclusions explicitly. Existing format
preservation checks still govern output. A complete zero-change result means only that the covered model fields agree.

## Record and value contract

Each view contains `beforeSnapshot: "source"`, `afterSnapshot: "candidate" | "output"`, `comparisonComplete`,
`detailsComplete`, `changed` (boolean or null), `observedChanges`, `totalChanges` (number or null), `changes`,
`reasons`, and `work` counters. Each change has `path`, `kind` (`value`, `arrayLength`, or `presence`), `before`,
and `after`. Change counts count these records, including collection length and optional-record presence changes;
they do not count logical entities. Ordering is deterministic model-field order, then array order.

An `arrayLength` row uses the array path and `type: "size"` length descriptors. Traversal then visits the greater
array length, using absent values for elements missing on one side. The surface `vertices` and `texVertices` arrays
are separate stored sequences. Added/removed nonoptional records need no extra presence row. Optional scalars emit
one row: `presence` when engagement changes, otherwise `value`. Optional composite records emit a `presence`
row when occupancy differs, then recurse into nullable children. Its boolean descriptors state occupancy. A missing
owner is absent; an existing owner with a disengaged optional has a present boolean false. Scalar absence covers a
disengaged optional or missing owner; surrounding array lengths and record fields establish the structural context.

Value descriptors have `present: false` for absence. Present descriptors have a `type` and a `value` unless the value
is explicitly omitted. Signed and unsigned storage remain distinct. Enum descriptors preserve the raw integer code.
Float32 and float64 descriptors contain fixed-width lowercase hexadecimal `bits` and `classification` (`finite`,
`negativeZero`, `positiveInfinity`, `negativeInfinity`, or `nan`). Nonfinite values use JSON null with their explicit
classification; signed zero remains distinguishable through bits. Comparison uses IEEE storage bits, not tolerance.

Strings have `type: "string"` and `byteCount`. Inline valid UTF-8 has `encoding: "utf8"` and the exact `value`.
Inline invalid UTF-8 uses `encoding: "hex"` and an exact hexadecimal byte string. Oversize or over-budget values
carry `omitted: true` and their byte count; they are never silently clipped. Ordered parameter lists retain duplicates,
type tags, inactive base storage and optional modifiers. An absent optional scalar is distinct from an explicit zero.

## Budgets and incomplete results

Each view has independent defaults: 1,000,000 visited slots, 4 MiB of compared string bytes (both sides charged),
128 emitted change records, 512 raw bytes per inline string, and 32 KiB of aggregate raw inline string bytes. Every
record, collection, optional container and scalar charges traversal before work, including unchanged and empty
records. Text admission precedes equality, validation, encoding or copying. No complete JSON model, unbounded digest,
cross-product matching, resource lookup, or additional Level copy is built for the preview.

`comparisonSlots` or `comparisonStringBytes` stops that view. `comparisonComplete` becomes false, `totalChanges`
becomes null, and `observedChanges` is only a lower bound. `changed` is true if any change was observed, false only
after a complete zero-change comparison, and otherwise null. A `changeLimit` truncates detail rows while comparison
continues, so a complete scan can still report an exact total. `inlineStringLength` and `inlineStringBytes` explain
omitted values. `detailsComplete` is true only when comparison completed and every change and value is present.
Reason codes are unique and deterministic. The effective limits and counters are returned. Budget exhaustion makes
the preview partial; it does not introduce a new refusal for an otherwise admissible patch.

These bounds constrain comparison work and response size; they do not change the existing file/request admission
limits. This version provides no caller-configurable preview settings. Internal limit parameters support focused
boundary tests. Ordinary programming and allocation errors are not hidden as truncation.

The top-level `limits` object uses `maxVisitedSlots`, `maxComparedStringBytes`, `maxChanges`,
`maxInlineStringBytes`, and `maxTotalInlineStringBytes`. Each view's `work` contains `visitedSlots`,
`comparedStringBytes`, and `inlineStringBytes`. Top-level `excludedFields` lists the excluded Level field paths;
`excludedScopes: ["nativeOptionalSectionBytes", "dataOutsideLevel"]` states the outside-model boundary.
General discovery's `semanticPreview` object describes this version, coverage, addressing, exclusions, default
limits, and `commands: ["plan", "apply"]`; `views` describes authored and persisted scope in plain text. A command's
preview carries the same metadata with effective limits, the two exact hashes, and its two result views.

## Implementation boundary

`tools/sed-agent/agent_semantic_preview.hpp/.cpp` is a portable agent presentation adapter over immutable Level
references. It owns no authoritative model state. The shared domain target and the standalone MCP fallback compile
the same source. `buildOutput` and `buildNativeOutput` gain an optional reopened-Level output parameter; existing
callers that do not request a preview retain their current path. Generic plan/apply constructs the preview after
hashing the exact output and before returning or publishing. No serializer or format implementation changes.

## Verification

Focused tests exercise real plan/apply with synthetic native and text fixtures, exact output hashes, source
preservation and no plan publication. Direct model fixtures cover bit-preserving values, signed flags, duplicate
ordered parameters and IDs, topology position semantics, ARCH metadata and all budget boundaries. Separate tests
show authored-versus-persisted serialization differences and discovery/MCP forwarding. Existing output review,
native preservation, schema, geometry, ARCH and MCP regressions remain gates. Full headless Release and standalone
MCP builds complement these tests; native GUI and game acceptance remain separate platform checks.
