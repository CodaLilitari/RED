# Indexed world snapshot implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Render an explicitly selected, bounded indexed JK world preview using the original MAT indices and authored sector CMP light tables.

**Architecture:** Keep resource decoding in the checked MAT/CMP readers and share raster coverage with the software snapshot. Select the shader explicitly and expose the applied contract through the existing snapshot/overview agent entry points and shared MCP schemas.

**Tech Stack:** Portable C++, CMake, existing hand-built binary fixtures and CLI/MCP test harnesses.

**Spec:** [JK indexed world snapshots](lighting.md#jk-indexed-world-snapshots) and [agent workflow](agent_workflow.md). This plan records the portable scope of RED retail lighting audit section 3.

## Global Constraints

Preserve default software-v1 image and receipt contracts. No engine, viewport, transparency-table, animated Thing, MotS or IJIM changes. Source hashes identify selected resources. Reject unsupported visible world content before publishing. Treat output as RED sampling, clipping, and visibility without retail pixel-parity claims.

## Review Focus

- Duplicate palette RGB can mask lost source indices.
- A key is tested on the original texel before any translation, and only on a keyed Textured use.
- A Solid face needs the cel's color number, which can differ from its first texel.
- Incomplete tables and resource-budget overflow must prevent publication.
- An older binary can ignore the option. Consumers verify the applied profile in the receipt.

---

### Checked indexed MAT input

Interface: `MatFile::parseIndexedSnapshotCel(std::vector<std::byte>) -> IndexedMatCel`. Fields: int width/height, uint8_t solidColorIndex, bool colorMaterial/keyed, vector<uint8_t> indices. Only cel zero/mip zero is retained; a color MAT has an empty plane. The regular MatFile parser retains its layout and behavior. Test width=height=2, indices {1,2,3,0}, Solid=11 and only cel 1 keyed; then flip the cel 0 flag and verify it becomes keyed.

Files: `src/core/include/sed/assets/mat.hpp`, `src/core/src/assets/mat.cpp`, `src/core/tests/assets/mat_cmp_tests.cpp`.

- [x] Write a source-plane test for four explicitly positioned indices, cel-local key enable and a separate Solid index. Exercise the ordinary RGB decode alongside this call; only the dedicated result contains the retained plane.
- [x] Run the core asset tests and observe failure before coding.
- [x] Add a dedicated indexed result at the existing checked read. It retains only cel 0/mip 0 without allocating the decoded RGB mip chains. This keeps the legacy MatFile layout unchanged.
- [x] Rerun the same test and existing MAT allocation tests.

### World shading

Interface: optional `SnapshotMaterial::indexed` (IndexedMatCel); `SoftwareSnapshotOptions::indexed8bit` and `indexedColormaps` (vector of complete CmpFile, slot zero is master). Accept geo=3/4, light=0/3 only. Require baked scalar corner input with channel a plus red face extra bounded below by red sector extra. Quantize `floor(clamp(light,0,1)*63)`; 0.5 selects row 31. Apply no header tint. FULLYLIT face or explicit fullbright bypasses the sector mapping. For fixtures, sector row 31 maps 1→17, 2→0, Solid 37→88, master reds for indices 17,0,88 are 170,9,188. Original keyed zero leaves background black; translated zero draws red 9.

Files: `src/core/include/sed/render/software_snapshot.hpp`, `src/core/src/render/software_snapshot.cpp`, `src/core/tests/render/software_snapshot_tests.cpp`.

- [x] Add pixel checks for duplicate RGB, a nonmonotonic sector table, a differing master palette, misleading header tint, fullbright palette entry versus face light mode zero, keyed zero and a nonzero index mapped to zero, and Solid color independent of texture. Include row boundaries and nonfinite lights. Render the legacy fixture through default options.
- [x] Observe failing checks in the core renderer test executable.
- [x] Extend the snapshot options/material with an explicit indexed representation. Use RED's documented JK scalar light model and a documented preview quantization policy. Bypass translation for fully lit faces, resolve translated indices through the master palette, and preserve existing raster coverage. Reject unsupported face/geometry modes, true-color, missing materials and non-world geometry.
- [x] Verify both profiles against their independently asserted pixels.

### Tool contract and resource provenance

Interface: `renderProfile` accepts `software-v1` or `indexed8bit`. For indexed8bit use `lighting: "baked"` by default; explicit ambient is refused. Explicit fullbright is a raw-index preview. Only JK world surfaces are admitted. Successful writes return `appliedProfile: "indexed8bit"`, `renderer: "software-indexed8bit-v1"`, and `indexedResources` entries with archive index, path, SHA-256 and CMP slot when applicable. First matching basename wins. Parse a maximum of 256 CMP slots, 512 unique MATs, 16 MiB per resource, 64 MiB combined source and 64 MiB retained MAT planes. A matching named sector binds its owner CMP even in a JED/native level without a retained integer slot. Raster budget: 256,000,000 candidate samples. Read-only overview returns only a replayable request.

Files: `tools/sed-agent/agent_workspace.cpp`, `tools/sed-agent/agent_schema.cpp`, `tools/sed-agent/tests/agent_cli_tests.cpp`, `tools/sed-mcp/tests/mcp_stdio_tests.cpp`, `docs/lighting.md`, `docs/agent_workflow.md`, `README.md`, and the level-workflow snapshot reference.

- [x] Add CLI/MCP/overview tests for selection, discovery, applied profile, chosen resource names/hashes, and absent/invalid resources; check a failed request creates no PNG. Verify journal replay preserves the response and pixels.
- [x] Run the agent/MCP tests and observe failures.
- [x] Expose `renderProfile: "indexed8bit"` only with defined JK world lighting. Parse complete CMPs, apply the source/decoded/cache/raster budgets, and attach `appliedProfile` to successful selected renders. Reject unknown profile values. Keep unrelated unknown-field behavior. Document verification against older binaries and scope limits.
- [x] Run focused core, CLI and MCP suites, inspect changes, submit for review. Retail captures remain a separate qualification.
