# Native model editing session implementation plan

**Goal:** Provide the portable transaction and history foundation for the approved [native model workspace](native_mesh_editing_proposal.md), using the existing all-LOD [native document](native_model_document.md).

## Contract

- Keep source mesh and texture-vertex ordinals explicit, including duplicate names, shared instances and orphan meshes. An empty selection is valid outside an interaction.
- Freeze the operation family, selection and committed baseline at Begin. Recompute each proposal from that baseline. A refused proposal discards the previous preview so Apply cannot commit stale input.
- Publish one exact authored revision per effective Apply. Cancel, refusal and numeric no-op do not change history or truncate Redo. Undo, Redo and selection changes require an idle interaction state.
- Store exact source strings and transition selections in a bounded timeline; reparse an Undo/Redo destination before changing live state. Prepare commit allocations and retention before replacing the live timeline.
- Default to at most 64 transitions and 64 MiB of retained timeline source payload, including the current slot. Smaller limits are supported for bounded hosts and tests. Zero transitions retains only the current slot. A current source that cannot fit refuses atomically.
- Keep revision tokens opaque and session-bound, with monotonic authored identities independent of cursor position. Only an acknowledgement of the exact current revision can mark it clean.
- Retain original source SHA-256 and byte count, without permanently retaining a second original parsed document. Timeline payload limits exclude parsed current/preview documents, parser working memory, string capacity and external snapshot owners.
- Add no GUI controls, file publication, persistence/recovery format, CLI operations, topology edits, or changes to Level history.

## Work and verification

1. Add focused tests against the owned relay-sentry fixture and synthetic textual variants. Cover selection admission, frozen targets, baseline-relative previews, invalid-proposal invalidation, exact Undo/Redo/Cancel, cumulative UV/mesh commits and source preservation.
2. Record a failing build or test before implementing the new API; then implement the smallest portable session satisfying these contracts.
3. Add boundary coverage for count and payload eviction, impossible current budgets, no-op/failed commit retention of Redo, dirty-state branching and eviction, stale and foreign save acknowledgements, malformed input and wrong interaction families.
4. Inspect the diff independently, update the owning contract and existing discovery links, run the focused session and native-document tests, then run every configured Qt-free Release test.

The native GUI Release gate and target-game acceptance remain separate. The session alone does not complete Model Studio interaction or recovery.

```sh
cmake -S . -B build/core -G Ninja -DCMAKE_BUILD_TYPE=Release -DSED_BUILD_APP=OFF -DSED_CPP_STANDARD=23
cmake --build build/core --target sed_native_model_session_tests sed_native_model_document_tests --parallel 3
ctest --test-dir build/core --output-on-failure --no-tests=error -R 'sed_native_model_(session|document)_tests'
cmake --build build/core --parallel 3
ctest --test-dir build/core --output-on-failure --no-tests=error --parallel 3
```
