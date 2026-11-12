# Dependency inspection implementation plan

> **For agentic workers:** Use superpowers:executing-plans task by task. Preserve the ledger and evidence for continuation.

**Goal:** Expose a bounded, inspect-only dependency graph through portable core, CLI and MCP.

**Architecture:** Share stop-capable reference visitors and archive lookup with packaging. Keep separate traversal
policies so package bytes/receipts remain compatible. The agent captures and validates inputs; core returns value
records; MCP uses the shared domain and path policy.

**Tech Stack:** Portable C++20-compatible source, tested in C++23 Release, existing JSON and GOB/COG code, CMake/CTest.

**Spec:** [dependency_inspection_design.md](dependency_inspection_design.md).

## Global constraints

- Captured raw JKL and at most 16 explicitly ordered hash-bound archives; no loose-root discovery or output/journal.
- Preserve package lookup, callback, scope, manifest, output and publication behavior.
- Use the exact defaults/maxima and completeness definitions in the spec; strict new request/schema parity.
- Keep portable core free of Qt; preserve standalone MCP configuration and isolated journal test linkage.
- Use synthetic fixtures only, date-free living documentation, and no generated artifacts in source commits.

## Review focus

- Duplicate normalized archive rows must report the actual first winning row and matching payload bytes (Task 2).
- Repeated misses share a target; an ambiguous result before an exact alias retains the first outcome (Task 2).
- A queued parent remains valid when adding nodes reallocates containers; no dangling node/reference views (Task 2).
- COG and malformed/large tokens must not bypass work/string/parser admission (Task 2).
- MCP schema/path validation must precede capture and remain independent of journal availability (Task 4).

## Task 1: Establish runnable feature and compatibility evidence

**Files:** Modify `tools/sed-agent/tests/agent_cli_tests.cpp`, `tools/sed-agent/CMakeLists.txt`.
**Interfaces:** Existing `runAgent`, synthetic GOB helpers and real `red-agent` process. Produces a `dependencies`
scenario which expects the new command's graph, hashes, strict request handling and unchanged inputs.

- [x] Capture the clean configured Release baseline and complete PackagePlan compatibility cases outside the repo.
- [x] Add the real CLI graph scenario before production code; run it against the existing binary/test harness.
- [x] Observe an expected unknown-command failure; preserve the exact output and commit the regression.

## Task 2: Portable bounded graph and shared discovery

**Files:** Create `src/core/include/sed/level/dependencies.hpp`, `src/core/src/level/dependencies.cpp`,
`src/core/src/level/dependency_references.hpp`, `src/core/src/level/dependency_references.cpp`,
`src/core/tests/level/dependency_tests.cpp`; modify `src/core/include/sed/assets/gob.hpp`,
`src/core/src/assets/gob.cpp`, `src/core/src/level/package.cpp`, `src/core/CMakeLists.txt`.
**Interfaces:** `DependencyInspection inspectDependencies(const DependencyRequest&)`; captured archive inputs,
explicit limits and graph value records in the public header. `GobArchive::capturedBytes()` exposes a borrowed
optional byte span without opening a path. Internal root/child visitors emit folder/name/relation through stop-capable
callbacks; shared archive selection returns physical row indices and exact/basename tiers.

- [x] Add core tests for the spec's graph/provenance, lookup, cycle/missing aliases, coverage and every limit boundary.
- [x] Establish runnable assertion failures with the minimal declared API, before implementing traversal.
- [x] Implement shared visitors/selection, adapt package without changing its traversal/assembly, and implement graph.
- [x] Run the focused dependency executable and core package group; require pass.
- [x] Rebuild the before/after compatibility driver; compare all complete receipts and bytes exactly; commit.

## Task 3: Strict CLI and shared discovery

**Files:** Create `tools/sed-agent/agent_dependencies.hpp`, `tools/sed-agent/agent_dependencies.cpp`;
modify `agent_workspace.cpp`, `agent_schema.cpp`, `agent_schema.hpp`, both agent/MCP CMake source lists,
and `tools/sed-agent/tests/schema_contract_tests.cpp`.
**Interfaces:** `dependencyRequestSchema()`; `inspectDependencyRequest(levelPath, request, capturedArchives)` consumes
the strict request and core graph. The workspace adapter owns capture/admission using the existing command scope.

- [x] Extend strict schema/runtime tests before the adapter: hashes, unknown fields, types, bounds and extra arguments.
- [x] Add closed request parsing, captured hash checks, graph JSON conversion and focused/no-topic discovery.
- [x] Run `sed_agent_dependencies`, schema contracts, package CLI and the focused core tests; require pass; commit.

## Task 4: Read-only MCP parity and policy

**Files:** Modify `tools/sed-mcp/main.cpp`, `tests/mcp_parity_tests.cpp`, `tests/mcp_stdio_tests.cpp`.
**Interfaces:** `sed_dependencies {path,request}` embeds the same schema and calls `dependencies` through runAgent.

- [x] Add catalog/receipt tests and path/symlink/logical-name/no-journal cases; observe missing-route failures.
- [x] Add strict outer validation, nested archive policy and read-only dispatch; preserve original write identities.
- [x] Add the isolated journal target schema stub when required; update the exact 40-tool inventory.
- [x] Run MCP parity/stdio/journal tests and standalone MCP-only configure/build; require pass; commit.

## Task 5: Documentation, full gate, review and checkpoint

**Files:** Update `README.md`, `docs/README.md`, `docs/agent_workflow.md`, `docs/architecture.md`,
and add `docs/reviews/dependency_inspection.md`.

- [x] Document the current request/receipt/limits and 42-command/40-tool inventory; validate links/examples.
- [x] Build all configured Release targets and run all CTest entries without exclusions; report every failure.
- [x] Request a fresh whole-branch review of exact baseline-to-head range, spec, plan, review focus and ledger.
- [x] Resolve substantive findings with observed failing-then-passing regressions; preserve evidence and limitations.
- [ ] Publish a separate draft stacked on `lily/dependency-inspection`; verify exact remote tree and changed paths.
- [ ] Save verification evidence and update the existing continuation Page; preserve earlier checkpoints.
