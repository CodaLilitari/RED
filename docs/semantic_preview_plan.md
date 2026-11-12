# Semantic preview implementation plan

**Goal:** Give agents bounded, source-bound evidence of candidate and persisted level changes through generic plan/apply.

**Architecture:** One portable agent adapter compares immutable Level snapshots. Reuse the already verified output
reload. Forward the shared receipt through MCP. Preserve output bytes, review hashes, command counts and patch schemas.

**Tech stack:** Existing C++20-compatible agent code, sed_core, minimal agent JSON, CMake and synthetic CTest fixtures.

**Spec:** [Semantic change previews](semantic_preview_design.md).

**Global constraints:** One source/build writer per path. Tests first. No GUI dependency, new package, format change,
resource lookup, guessed identity or complete model JSON allocation. Living documentation stays date-free. Preserve
the completed parity branch. Publication is a separate draft review branch; no merge or release.

**Review focus:** Honest incomplete results, before-work budgets, exact scalar representation, complete stated field
coverage, positional addressing, output reload reuse, unchanged publication guards and standalone MCP linkage.

## Task 1: Establish the behavioral regression

Add `tools/sed-agent/tests/semantic_preview_tests.cpp` and register `semantic_preview` in the existing CMake contract
test loop. Start with a native starter fixture, a gravity patch, and assertions for authored/persisted changes,
source/output hash binding, deterministic plan/apply receipts, no plan publication, and unchanged source bytes.
Build and run only this target against the unchanged production domain; record an assertion failure for the missing
`semanticPreview` field. Compilation or fixture failures do not establish the red gate.

## Task 2: Implement the bounded adapter

Create `agent_semantic_preview.hpp/.cpp` with this shared interface:

```cpp
struct SemanticPreviewLimits
{
    std::size_t maxVisitedSlots{1000000};
    std::size_t maxComparedStringBytes{4 * 1024 * 1024};
    std::size_t maxChanges{128};
    std::size_t maxInlineStringBytes{512};
    std::size_t maxTotalInlineStringBytes{32 * 1024};
};
Json buildSemanticPreview(const sed::Level& source, const sed::Level& candidate,
    const sed::Level& persisted, std::string_view inputSha256,
    std::string_view outputSha256, const SemanticPreviewLimits& limits = {});
Json semanticPreviewCapabilities();
```

Place these declarations in `sed_agent`. Use typed field walkers and explicit coverage rather than query projections
or codec equality. Add direct model tests for all covered domains, absence, ordered duplicates, index shifts, signed
flags, exceptional floats, UTF-8/hex strings, traversal/text/detail limits and source immutability. Keep each view's
counters independent. Add the source to both shared-domain CMake lists. Root integrates staged worker files and owns
all builds.

## Task 3: Integrate plan/apply and discovery

Retain the Level already loaded by the native/text `buildOutput` verification using an optional pointer output, and
move it into the caller without a new full model copy. Add the two-view preview after output hashing in generic
`planOrApply`. Leave standalone commands and publication guards untouched. Advertise capabilities additively in the
general schema response, preserving old operation descriptors and exact help usage. Extend the focused tests for
text rounding/normalization, duplicate identities, native authoring and stale output hash refusal. Verify the original
regression becomes green and compare output hashes against the pre-feature executable on the same fixtures.

## Task 4: Verify transport and documentation

Add MCP checks for plan/apply preview equality and operation-ID replay of the recorded receipt. Build with CLI enabled
and with only MCP enabled. Update owning agent design/workflow documentation and the documentation map with scope,
addresses, limits, omissions and examples. Run focused schema, edit, ARCH, native preservation and MCP regressions.

## Task 5: Review, gate and preserve

Perform independent whole-diff review; fix findings through focused regression tests. Run the complete headless
Release build and CTest suite, reporting any environment gate by name. Verify the final diff, clean source checkpoint
and published draft branch tree. Update the durable continuation archive and existing continuation Page with exact
identities, evidence and remaining platform gates. Keep historical recovery and parity evidence intact.
