# JO/JA scene import design

## Intent

Bring a Jedi Outcast or Jedi Academy map into RED through the same portable operation in the editor and agent interfaces. Reuse the tested ideas in Lilivane's Bespin, Sith Council and Midgar tooling: direct Raven BSP coordinates, bounded model chunks, explicit patch tessellation, source light sampling and a visible record of the work that still needs authoring.

This first import produces an editable **scene scaffold**, not a declaration that a multiplayer map or mission has been converted faithfully. It keeps the source untouched and publishes one new resource archive containing the JKL and generated assets.

## Chosen approach

Implement the reusable operation in portable C++ rather than launch map-specific Python scripts. The scripts depend on private intermediate files and workstation paths, while RED already owns the target model, material, level and archive writers. A native reader also removes the need to install Q3Map2 merely to inspect an RBSP.

The alternative of reconstructing native sectors from arbitrary BSP cells is substantially more difficult: source BSP partitions are not automatically valid convex Sith sectors, and rendering triangles do not define source collision semantics. That work remains separate. Running the original scripts unchanged would preserve their map-specific assumptions and cannot supply a stable GUI contract.

## Shared core contract

- Accept Raven `RBSP` version 1 with 18 checked lumps. Reject other dialects explicitly. Keep parsing bounded and failure-atomic.
- Preserve source shader names, entities, model identities and counts. Convert world model 0 only; moving submodels remain visible in the report instead of being frozen into the world.
- Read positions directly as Z-up. Default scale is `1 / 320`. The Q3Map2 OBJ axis rotation does not apply to native BSP coordinates.
- Tessellate each quadratic patch block with an explicit subdivision count, default 4, range 1–16. Describe fixed sampling honestly; do not claim adaptive error bounds or crack-free LOD compatibility.
- Use first-channel baked lightmap luminance at corners when available, otherwise authored vertex color. Preserve provenance and report omitted dynamic light styles and shader-stage effects.
- Split world triangles into small spatially grouped 3DOs, each at most 140 triangles / 420 flat corners by default. Never exceed the classic per-mesh limits.
- Allow explicit shader-to-MAT mappings. Unmapped visible shaders receive clearly reported generated checker materials only when the caller opts into placeholders. Sky/nodraw surfaces and billboards are skipped and counted. This version does not interpret shader scripts or read assets from implicit directories.
- Offer visual-only or explicitly approximate rendered-mesh collision. Do not silently convert clip brushes, player-only filters, triggers, doors, lifts, water physics, sound or mission logic. Map available stock hard/metal/earth footsteps through separate Thing chunks; mark source families with no exact static-model cue. Source-audio import explicitly refuses until verified playback mapping exists.
- Create a bounding sector with static model Things and converted source player starts where present. Preserve all source entity text as a separate archive entry for follow-up authoring. Target-specific stock player references are selected for JK or MotS; stock game bytes are never bundled.
- Target archive is GOB for JK and GOO for MotS. Indexed MATs require an explicit matching CMP; RGB565 remains a separate material edition. Do not advertise fallback switching within one archive.
- Return generated-file hashes, model/material counts, placeholder and skipped shader inventories, unsupported entity counts and limitations. Successful generation always reports runtime acceptance as unverified.

## Adapters

The CLI provides inspection, preparation and hash-bound apply. Requests name every source file explicitly and bind it by SHA-256. Apply recaptures inputs, recomputes the plan, checks the reviewed output and manifest hashes, and uses RED's create-only atomic file publication. MCP uses the same requests plus its existing workspace/resource-root path policy.

The GUI presents ordinary source, target, scale, patch density, collision and material controls. Inspection lists shaders; preparation writes nothing. Generation runs in the existing bounded worker pool. Editing inputs invalidates the prepared result; cancellation and stale document ownership revoke publication. The new archive is independent of the open document and never silently replaces it.

## Verification

Synthetic, project-owned RBSP fixtures cover binary bounds, malformed indices, entity parsing, patch interpolation, source-light sampling, shader mapping, chunk limits and output readback. CLI/MCP tests cover schema parity, path policy, stale hashes and existing outputs. GUI tests cover preparation, invalidation, cancellation and publication. Private source files from the handoff may supply additional smoke evidence but are excluded from the repository and release artifacts.

The complete available Release suite is required. Editor checks and structural package readback remain separate from retail/OpenJKDF2 gameplay, multiplayer, sound and renderer acceptance.
