# Native model document implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development or superpowers:executing-plans. Steps use checkbox syntax.

**Goal:** Open every authored classic 2.1 LOD and edit independent texture coordinates in native pixel units.

**Architecture:** Validate source through the bounded source scanner shared with lighting and load raw LODs. Bind patch addresses to the same owned source, rewrite only changed numeric fields, then reopen and verify.

**Tech Stack:** Portable C++20, sed_core, CMake, CTest.

**Spec:** [Native mesh editing proposal](native_mesh_editing_proposal.md), MESH-01 and the UV subset of MESH-02.

## Global constraints

Keep all materials, joints, original source meshes, all LODs and unchanged text. Reject unknown sections, missing normals and nonclassic input. No filesystem publication, GUI, position edits or additional dependencies. Keep valid model-lighting inputs compatible.
The shared scanner also corrects one legacy admission error: existing lighting inputs containing doubled leading signs
such as `+-1` require correction before inspection or baking. The material-name colon restriction is native-only.

## Review focus

Exercise shared and orphan source meshes, independent UV seams, CRLF and numeric spelling, invalid late batch entries, and finite input/output budgets.

## Files and interfaces

Create `src/core/include/sed/assets/native_model_document.hpp` and `src/core/src/assets/native_model_document.cpp`. Register the implementation and `src/core/tests/assets/native_model_document_tests.cpp` in `src/core/CMakeLists.txt`. Capture UV spans through the existing parser in `src/core/src/assets/model_lighting.cpp` via the private `src/core/src/assets/native_model_source.hpp` interface. Describe the shipped admission and limits in `docs/native_model_document.md`.

- `static NativeModelDocument open(std::string_view source)`
- `std::string_view source() const noexcept`
- `std::span<const Model3do> geosets() const noexcept`
- `NativeModelDocument applyTextureVertexEdits(std::span<const NativeTextureVertexEdit> edits) const`
- `NativeTextureVertexEdit{std::size_t geoset, sourceMesh, textureVertex; float u, v;}`

## TDD and verification

- [x] Add fixture tests for raw all-LOD readback, byte-exact no-op, targeted numeric replacement, signed-zero equality, whole-batch refusal, unknown data and admission bounds.
- [x] Compile the new target against baseline. Observe failure for missing document API.
- [x] Implement bounded validation, raw loads and immutable edit/reopen. Compare requested fields after independent RED model-reader parsing.
- [ ] Run `ctest --test-dir BUILD -R sed_native_model_document_tests --output-on-failure`; expect one passing target.
- [ ] Integrate with parent build and review. Record exact coverage separately from any external-reader or target-engine results.
