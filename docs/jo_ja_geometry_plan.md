# JO/JA hybrid geometry implementation plan

**Goal:** Reduce the work caused by one-sector architecture conversions while retaining source visual detail.

**Design:** [Hybrid geometry](jo_ja_geometry_design.md). Portable conversion owns topology and fidelity. GUI and
agents share the same options, reports and reviewed output hashes.

- [x] Retain and validate source BSP topology, brush references and visibility data without changing mesh-only imports.
- [x] Add source-derived bounded sector partitions, shared portal construction and shell validation, with synthetic tests first.
- [x] Clip source triangles by their owning cells and promote matching boundary geometry, proving area/attribute coverage.
- [x] Reuse exact residual model vertices/UVs within classic mesh and Thing budgets; preserve seams and place origins inside cells.
- [x] Add hybrid options and diagnostics to generation, CLI/MCP and the editor, retaining model-scene compatibility.
- [x] Compare original private maps at identical source fidelity, capture structural and workload evidence, and inspect final output.
- [ ] Review the diff, update owning documentation, run focused tests and the complete available Release gate.
- [ ] Record retail traversal, renderer and frame-time checks separately from static qualification.

Static original-map qualification does not certify every requested sector count. Some finer partitions can still
refuse an unrepresentable shell or exceed model/fragment limits; a smaller explicit budget can yield a valid plan.
Migrating an existing playable episode's COG scans, activation links, moving objects and sky is separate authoring work.
