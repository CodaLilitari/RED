# JO/JA scene import implementation plan

**Goal:** A shared, reviewable Raven BSP scene importer for the GUI, CLI and MCP, with explicit remaining authoring work.

**Architecture:** Portable bounded parsing and generation in `src/core`; file capture, request admission and publication in the existing agent and Qt adapters. Existing target writers remain authoritative.

**Spec:** [Scene import design](jo_ja_import_design.md).

## Constraints and review focus

Follow `AGENTS.md`, including regression-first tests, portable core ownership, source-preserving operations and one writer per file/build tree. No new runtime dependency or proprietary fixture is needed. The key failure classes are malformed BSP counts/indices, excessive tessellation or asset bytes, drift between preparation and publication, unsafe material/entry names, and silently changing collision or unsupported gameplay semantics.

## Work

- [x] Add a synthetic RBSP fixture and tests that fail before the parser exists; implement `raven_bsp.hpp/.cpp`, checked parsing and per-surface triangulation.
- [x] Add failing scene-generation tests; implement `jo_ja_import.hpp/.cpp`, light sampling, bounded chunks, explicit materials, target scene and archive readback.
- [x] Add failing agent/schema tests; implement `jo-ja-inspect`, `jo-ja-plan` and `jo-ja-apply`, strict request capture, reviewed hashes and create-only publication.
- [x] Add MCP discovery/path-policy coverage and wire those commands through the existing adapter.
- [x] Add GUI conversion tests and ordinary controls, worker cancellation, input invalidation and new-archive publication through the shared core.
- [x] Correct independently demonstrated shadow-cut regressions before qualification, preserving unrelated editor behavior.
- [ ] Update the user/agent documentation, review the complete patch and run focused tests followed by the available full Release gate. Record any missing platform/runtime qualification precisely.
