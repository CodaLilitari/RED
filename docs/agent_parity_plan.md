# Agent parity implementation plan

The [design](agent_parity_design.md) defines a bounded headless parity pass. The [agent workflow](agent_workflow.md)
owns public usage, and the [review record](reviews/agent_parity.md) owns verification evidence. Checkboxes describe this
implementation slice, not full parity with every editor or completion of native/game acceptance.

## Discovery and transport

- [x] Share the 63-operation registry and preserve existing descriptor text and order.
- [x] Publish standalone Draft 2020-12 schemas for patch and operation requests, plus 41 command descriptors.
- [x] Use shared descriptors in CLI help; preserve no-topic discovery and reject unknown/extra topics.
- [x] Expose all 39 named MCP routes with real command dispatch and shared typed patch schemas.
- [x] Preserve original-argument journal fingerprints and file-producing route recovery.
- [x] Admit nested paths and actual resolved lighting resources without repeated recursive root scans.

## Authoring and readback

- [x] Restore checked absolute vertex positioning with reciprocal portal welding.
- [x] Restore pixel-dimension texture fitting and supported shared-edge texture alignment.
- [x] Add cumulative geometry/weld admission and verify exact state retention on refusal.
- [x] Restore MotS ARCH bind/reset/bake with private samples, all-LOD checks and patch budgets.
- [x] Expose detailed sector/template/COG/ARCH/sample/containment collections with bounded pages.
- [x] Bound adversarial filtering and containment work while preserving explicit unknown/ambiguous states.

## Evidence and delivery

- [x] Reconcile every path in the interrupted change inventory with retained or reconstructed source.
- [x] Confirm focused baseline failures before new behavior or fixes.
- [x] Pass focused domain tests and independent JSON Schema validation.
- [x] Review schema, edit, ARCH, query, path and journal behavior independently; resolve substantive findings.
- [x] Update public usage and comparison ledgers to match actual dispatch and scope.
- [x] Complete the configured Linux headless Release build and CTest gate; record any unresolved failure exactly.
- [ ] Preserve a complete source/history checkpoint and publish the reviewed branch with verified tree identity.

## Separate acceptance work

Windows/Qt interaction, Undo/Redo, native renderer output, package launch and real-game behavior remain independent
gates. No checkbox above waives those gates or makes software snapshots a substitute for game observations.
