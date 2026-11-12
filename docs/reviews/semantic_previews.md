# Semantic preview review

## Scope and behavior

Generic level `plan` and `apply` now return a bounded semantic preview alongside their existing byte hashes. The
authored comparison shows the net candidate change; the persisted comparison shows the captured source against the
model reloaded from the exact proposed output. This separates requested model values from serialization effects
without changing the writer, request grammar, operations or publication rules. MCP forwards the same result and
records it in the existing optional operation journal.

The [contract](../semantic_preview_design.md) defines positional snapshot addresses, typed descriptors, 21 covered
Level root members, 19 excluded field paths, outside-model exclusions and per-view work/detail limits. Arrays remain
ordered and IDs are values rather than inferred cross-snapshot identities. Complete counts describe change records,
not logical objects. Incomplete scans use a null total and lower-bound observations; shortened details never mean
the unreported state is unchanged.

## Review findings and decisions

Independent design review clarified optional-record presence, missing-owner descriptors, array-length rows and
separate surface vertex/texture arrays before implementation. The final implementation was checked against an
independent inventory derived from the model declarations, including inherited bounds and parameter storage, COG
modifiers, nullable source-light fields and full ARCH baseline/provenance records. No coverage or budget defect was
identified in that source review.

Both sides of a string comparison are admitted atomically with subtraction-based overflow checks. Traversal exhaustion
immediately unwinds the walk using a private sentinel; ordinary exceptions are not hidden as partial results. A row
limit prevents descriptor construction while allowing the bounded comparison to finish counting. Float descriptors
read through const references and retain IEEE bits, including signed zero and signaling/quiet NaN payloads. Invalid
UTF-8 is encoded as exact hex bytes; oversized inline values are omitted with byte counts rather than clipped.

Integration review confirmed that native output restores both RGB and ARCH extensions before retaining the reloaded
model. Text output retains the existing load of the actual exported string. No new Level copy, parse, resource lookup
or serialization is performed for the preview. Source revision checks, native workspace refusal, format/hash checks,
create-only publication and the final source-read-set check remain in the existing path.

## Regression evidence

Tests were written before the comparator. The initial real native plan failed only because its preview was missing.
The direct-model, expanded integration and MCP suites then compiled against interface stubs and failed at their
intended missing-view/capability assertions. The implemented adapter made all three suites pass.

Direct cases cover independent views, same-count edits across model domains, signed integers and raw enums, ordered
duplicate parameters and IDs, optional scalar/composite presence, positional additions/removals, exclusions, exact
float payloads, UTF-8/hex output, unchanged large inputs, before-work limits, exact detail totals and omitted strings.
Integration covers native gravity, both native authoring extensions, JKL rounding, no-op behavior, source preservation,
plan/apply equality, actual output hashes and stale reviewed-output refusal. MCP checks complete domain-result parity,
original-argument journal identity, replayed previews and older receipts that never contained a preview.

| Gate | Evidence |
| --- | --- |
| Linux GCC 14.2, C++23, Release, Qt disabled | Complete build passed |
| Complete configured CTest, no exclusions | 140/141 passed; capture environment gate below |
| Focused preview integration, model and MCP suites | 3/3 passed |
| Pre-feature output compatibility | Six native/text output hashes and all existing receipt fields unchanged |
| Discovery compatibility | All 65 focused schemas and the old inventory unchanged, apart from the added preview capability |
| Public inventory | Still 63 patch operations, 41 CLI command descriptors and 39 MCP tools |
| Standalone MCP with CLI disabled | Both requested targets built; the MCP parity test passed |

The sole configured failure is `sed_engine_capture_runner`: nine of its ten Python subtests pass, while the synthetic
capture subtest requires an Xvfb private display unavailable in this environment. The same test failed at the source
baseline. No test was excluded or weakened. This does not constitute a complete native Release acceptance result.

## Remaining boundaries

The preview is evidence about declared model fields, not a file-preservation, geometry, rendering or gameplay
certificate. Retained source/import bookkeeping and bytes outside Level remain outside coverage. Specialized COG,
asset, conversion and package receipts keep their existing contracts. Existing journal records replay their original
payload, so an older record can legitimately have no semantic preview.

Windows/MSVC, Qt interaction, native rendering and actual-game acceptance remain separate platform gates. The existing
capture prerequisite and remote CI availability must be resolved before those lanes can qualify a release. Nothing in
this review closes the broader feature and acceptance gaps in the [gap register](../gap_register.md).
