# Dependency inspection review

## Scope and disposition

The bounded dependency inspector is ready for draft review and checkpointing. It adds portable-core traversal,
`red-agent dependencies`, focused request discovery and MCP `sed_dependencies`. The public inventory is 42 CLI
commands, 40 MCP tools and 63 unchanged patch operations. See the [contract](../dependency_inspection_design.md),
[implementation plan](../dependency_inspection_plan.md) and [workflow](../agent_workflow.md#dependency-inspection).

An independent whole-branch review covered the complete implementation, tests, package extraction, capture and path
policies, standalone/isolated linkage, and current capability ledgers. Two Important findings and one Minor finding
were resolved with observed failing-then-passing regressions and independent final binary checks. No actionable
Critical, Important or Minor finding remained in the reviewed feature scope. This does not establish release or
native/game acceptance.

## Verified behavior

- Captured raw JKL and explicitly ordered, hash-bound archives produce parent/target edges, authored names,
  relation labels and physical archive-row provenance. The selected row's exact byte range supplies its payload hash.
- Repeated references, cycles and first-result destination reuse retain incoming edges without repeated traversal.
  Stock resources still have their supported dependencies inspected.
- Shared streaming readers and archive selection preserve package discovery while graph and package traversal
  policies remain separate. Existing callback, manifest, assembly and publication behavior is retained.
- Missing, ambiguous, unsupported, incomplete-reader and budget-stop evidence remain distinct. Scan completeness,
  detail completeness and reference resolution are independent; observed counts survive omitted details and unknown
  totals. Runtime closure is always unverified.
- Strict CLI/MCP requests, shared schemas, byte-safe receipt/error text, nested path policy and read-only dispatch
  reject unsupported requests without publishing output or entering a journal route.

## Review findings and corrections

### COG diagnostic expansion

The existing schema parser retains a full raw declaration for each invalid modifier. Byte/line caps alone admitted a
three-line COG with thousands of comma-separated modifiers, allowing small captured text to generate excessive
diagnostic allocation. A 20,018-byte reproduction exhausted a child-only 128 MiB address-space allowance before the fix.

Inspection now pre-admits a conservative diagnostic-evidence estimate in addition to the 256 KiB / 1024-line caps.
The allocation-free check sums `(atoms + 8) * (2 * lineBytes + 256)` across raw lines, counting comma/whitespace atoms
even inside quotes and comments. It refuses before parsing when the estimate exceeds 16 MiB, checking division before
multiplication. The shared parser and package consumer are unchanged. This is an operational guard, not a formal
peak-memory bound.

Two new refusal assertions failed before the fix. Final regressions cover adjacent 2009/2010-modifier cases,
whitespace equivalents and exact/over byte caps. Independent reruns of the 5000- and 10000-modifier reproductions now
return successful diagnostic reports with `scanComplete: false` and `cogParserLimit` under the same child memory
allowance; the small control still completes.

### Error text from malformed captures

A hash-matching malformed GOB could put an arbitrary entry-name byte into its bounds-error message, producing invalid
UTF-8 JSON. The exact 152-byte malformed archive and a missing request filename containing byte `0xFF` produced two
expected failing assertions before correction.

The dependency-command boundary now encodes `AgentError.message` using the same reversible ASCII byte scheme as
receipt identifiers, preserving exit status, machine error codes and payload state. Independent final verification
confirmed valid ASCII/UTF-8 JSON and unchanged exit 2 / `invalid_request` for the malformed archive. Other command
families and the shared GOB/JSON implementations are unchanged.

### Source-path schema parity

MCP's source-path schema initially omitted the NUL exclusion already present on archive paths, despite identical
runtime refusal. The source/archive schema equality regression failed before correction. Both now use
`dependencyPathSchema()`, including the runtime byte-limit annotation. The isolated journal target has the matching
stub. Independent Draft 2020-12 validation confirms the schema is valid and rejects an embedded NUL.

An additional transport regression fixed arbitrary-byte unknown outer keys with a constant ASCII diagnostic. The
older semantic-preview inventory assertion was updated from 41 to 42 commands; its preview-specific plan/apply and
63-operation checks remain intact.

## Verification evidence

| Gate | Result |
| --- | --- |
| Linux GCC 14.2, C++23, Release; Qt off, agent/MCP on | All configured targets built |
| Final focused core/CLI/schema/package/MCP/preview checks | 18/18 passed |
| Complete configured CTest, no exclusions | 142/143 passed |
| Standalone MCP-only configuration and build | Passed with `SED_BUILD_AGENT=OFF` |
| Standalone stdio dependency call and external Draft 2020-12 validation | Passed; 40 tools, identical source/archive path schemas, unchanged inputs and no journal |
| Complete package compatibility captures | All 23 before/after cases byte-identical |
| Documentation stdin example | Executed successfully against synthetic input |
| Fresh whole-branch review and adversarial binary rechecks | No remaining actionable finding in scope |

Package comparison covers every public `PackagePlan` field and exact output bytes for successful and blocked cases,
including callback aliases/collisions, stock promotion, exact/basename precedence, all lexical resource families,
local COG defaults and node/edge/depth/missing/ambiguous refusals. Each complete capture is 22,358 bytes, SHA-256
`2a66eac5df5726c09070a056741a753560d35dc40f7bb7606fa9ade5c2fd4007`.

The sole configured failure is `sed_engine_capture_runner`: its synthetic engine-capture subtest cannot obtain a
ready Xvfb display. Xvfb is present, but an independent minimal probe finds AF_UNIX socket creation denied with
`EPERM`. Nine of ten Python subtests pass. This lane also failed at the unchanged baseline; it was not disabled,
weakened or bypassed. Therefore the complete Release gate is not green.

## Acceptance boundaries

Windows/MSVC, Qt interaction and Undo/Redo, renderer pixels, native worker behavior, real resource corpora and actual
game loading remain separate acceptance lanes. Read-only receipts identify captured bytes, without a filesystem lock,
an atomic multi-file snapshot or final path revalidation. Operational limits do not guarantee total process memory
or wall-clock time. The supported lexical scan does not validate complete asset grammars, follow AI/COG-code resource
loads, or make prefabs dependency-complete. Full parity with the historical SED-MotS fork is not claimed.

Source, final forge identities, command logs and checkpoint receipts are tracked in the continuation record. No merge
or release is part of this implementation step.
