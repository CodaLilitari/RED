# Native model authoring implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development or superpowers:executing-plans. Steps use checkbox syntax.

**Goal:** Deliver reviewed CLI/MCP native UV editing and source-preserving whole-mesh scale and translation.

**Architecture:** Extend the immutable all-LOD source document with double XYZ token edits, then adapt that document
through strict inspect/plan/apply commands and the existing atomic publication and MCP journal services. Keep
geometry, transport and public documentation ownership separate.

**Tech Stack:** Portable C++, existing JSON/schema helpers, CMake, CTest, existing Qt-free agent/MCP executables.

**Spec:** [Reviewed native model authoring](native_model_authoring_design.md).

## Global constraints

- Follow `AGENTS.md`, progressive documentation onboarding, regression-first TDD and the complete configured Release gate.
- Use the exact interfaces, coordinates, limits, tolerances, schemas and hash/publication contract in the spec.
- Preserve classic 2.1 source and all unaddressed values. Add no dependency, format migration or proprietary asset.
- Do not infer retail, OpenJKDF2, GUI or native Release qualification from portable tests.
- One writer owns each source file and build tree. Independent core and adapter tasks use separate worktrees.

## Review focus

The tests in the owning task must explicitly cover these five cases:

1. Loose positions or disconnected pieces become coincident through finite floating-point rounding.
2. A different request produces identical output bytes and tries to reuse the old reviewed request hash.
3. An empty addressed mesh bypasses invalid address, parameter or duplicate checks.
4. A malformed last operation, oversized receipt or publication race leaves a partial artifact.
5. Large corner counts, long or invalid-UTF-8 names, or page offsets exceed intended work/response contracts.

## Task 1: Source-preserving mesh transforms

**Owns:** `src/core/include/sed/assets/native_model_document.hpp`,
`src/core/src/assets/native_model_document.cpp`, `src/core/src/assets/native_model_source.hpp`,
`src/core/src/assets/model_lighting.cpp`, `src/core/tests/assets/native_model_document_tests.cpp`,
and `docs/native_model_document.md`. Change core CMake only if needed for the focused tests.

**Consumes:** `NativeModelDocument::open`, its existing UV edit path, raw `Model3do` objects, and the existing strict
polygon predicate in `model_lighting.cpp`.

**Produces:** The exact `NativeMeshTransform` and `applyMeshTransforms` interface in the spec. No filesystem writes.
The existing UV public API and formatting remain compatible. The adapter reads ordinary `geosets()` to build reports.

- [ ] Add a failing regression invoking the new API on relay-sentry LOD 1, mesh 1, with scale 1.25 and translation
  `(0.125, -0.25, 0.5)`. Assert each XYZ result and exact source preservation outside changed position spans.
- [ ] Build the focused target and record the expected missing-interface failure before editing production code.
- [ ] Add double XYZ capture/readback and the common bounded token patch path. Retain the existing UV behavior.
- [ ] Implement the frozen transform contract, strict face/rounding admission and shared bounded validation work.
- [ ] Cover shared instances with nonzero pivots, an orphan mesh, duplicate names, loose positions, independent UVs,
  normal/color seams, CRLF/comments/signed-zero spelling, and chained UV then transform edits.
- [ ] Cover empty and identity edits, duplicates including empty targets, all invalid indices, nonfinite parameters,
  nonpositive scale, overflow, double-only precision, source-invalid faces and finite rounding collapse/warp.
- [ ] Cover newly coincident loose points and disconnected faces, original coincident rows, subnormal expected
  extents and the normal-extent boundary, face-work refusal and output growth beyond 16 MiB. Verify failure leaves
  the original snapshot unchanged.
- [ ] Run `ctest --test-dir BUILD -R 'sed_native_model_document_tests|sed_model_lighting' --output-on-failure` and
  the focused parser/model tests. Record exact commands/results and commit the coherent core change.
- [ ] Update the owned contract documentation and return a task report with the Red/Green evidence and commit ID.

## Task 2: Reviewed authoring through CLI and MCP

**Owns:** New `tools/sed-agent/agent_model_native.hpp` and `.cpp`; agent dispatch, schema and CMake registration;
`tools/sed-agent/tests/agent_cli_tests.cpp`, `schema_contract_tests.cpp`, applicable input-safety tests and test CMake;
`tools/sed-mcp/main.cpp`, MCP stdio/parity tests and test CMake; `docs/agent_workflow.md`; new
`docs/native_model_authoring.md`. Keep unrelated commands and their contracts intact.

**Consumes:** Task 1's exact core API, existing `readRequest`, `readFileBytes`, `writeNewFileAtomic`, `SourceReadSet`,
JSON serialization, MCP path policy, request staging and `runWrite` journal behavior.

**Produces:**

- `Json modelNativeInspect(const std::vector<std::string>& args)`
- `Json modelNativePlanOrApply(const std::vector<std::string>& args, bool apply)`
- `Json modelNativeRequestSchema()` and `Json modelNativeInspectQuerySchema()`
- The three CLI commands and three MCP tools from the spec; focused `model-native` schema discovery.

- [ ] Write failing CLI, schema and MCP scenarios for inspect, plan and apply. Observe baseline command/schema
  absence before production edits; until Task 1 is integrated, a missing transform API remains an expected dependency.
- [ ] Implement strict parsed request/query validation, paged discovery and schema parity with the exact spec bounds.
- [ ] Implement one immutable batch call and truthful per-operation reports; bind both reviewed hashes, check receipt
  size before writing, and publish with the existing revision-checked create-only writer.
- [ ] Register dispatch/help/discovery, then MCP path validation, query/request staging and operation journal routing.
- [ ] Exercise real CLI and MCP fixture inspect -> plan -> apply, independent readback, unchanged original source,
  UV shared rows, transform bounds/radii/lighting warnings, and byte-identical numeric no-ops.
- [ ] Reject wrong request hashes despite identical output, stale source/request, wrong output hashes, mixed/empty/
  duplicate/oversized requests, unknown fields, underflowing nonzero UV components and out-of-range JSON numbers.
- [ ] Verify paging endpoints, long/invalid-UTF-8 name omission, the serialized response limit, occupied/aliased destinations,
  invalid final operations, publication fault/race handling, denied paths, replay/conflict and journal uncertainty.
- [ ] Run the relevant agent CLI/schema/input-safety and MCP stdio/parity targets with `ctest --output-on-failure`.
  Record exact commands/results and commit the adapter, tests and owned workflow documentation.

## Task 3: Integration, independent review and delivery

**Owns:** Root integration branch, this plan's progress, README/docs index/roadmap links, the reproducible
`tests/fixtures/relay-sentry/native_authoring_readback.py` helper and final verification reports.
Core and adapter implementation files remain owned by their agents until task handoff.

- [ ] Review each task independently against the spec and actual diff; fix findings before final integration review.
- [ ] Integrate the core followed by the adapter. Run all configured Qt-free Release tests, with no concurrent build
  writer, using `cmake --build BUILD --parallel 3` and `ctest --test-dir BUILD --output-on-failure --parallel 3`.
- [ ] Run the compiled authoring commands on the owned two-LOD fixture. Open the candidate with the pinned independent
  Blender-Sith loader and compare edited positions plus preserved UVs, normals, radii, hierarchy and other LOD data.
- [ ] Update public discovery to describe the delivered headless slice without marking the native GUI milestone done.
  Validate changed Markdown links, examples and formatting.
- [ ] Obtain an independent whole-branch review; fix concrete remaining findings and rerun the affected verification.
- [ ] Publish a reviewable PR with AI assistance disclosed and exact evidence, preserving PR 3's independent review
  history. Run available platform CI; distinguish ordinary Qt build checks from the complete native Release gate.

## Plan self-review

Core geometry and transport are separable ownership units with one frozen public interface. Every preservation,
numeric, work, schema, revision and publication requirement maps to Task 1 or Task 2. Task 3 owns external readback,
integration and evidence. The approved larger workspace remains tracked in the parent design; no omitted UI or
topology work is represented as completed here.
