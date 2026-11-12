# Agent parity review

## Scope and source identity

This review covers the bounded headless agent parity implementation described in the
[design](../agent_parity_design.md) and [plan](../agent_parity_plan.md). It extends the published
`lily/model-lighting-headless` source at commit `c64dff87c1dafd37bf50c70509d33d8d00c36f78`, whose tree is
`3fe441b3ef56dded3615d0df688563e285e0b575`. All 1,082 baseline file objects and modes were independently
reconciled with that tree before integration.

The interrupted, unpublished work was only partly recoverable. Its 30-path inventory comprised 17 complete
retained files, four partial source excerpts and nine paths represented by specifications or checkpoint
records. Complete retained files were reapplied and reviewed; incomplete files were reconstructed against
the published source and actual domain contracts. Every inventoried path is now present. This review is the
additional, previously uncreated delivery record. Reconstruction is not a claim of byte identity with the
lost unpublished checkout. Historical test claims do not qualify reconstructed source.

The local recovery history uses an explicitly synthetic baseline commit because the published GitLab commit
object was unavailable. A review branch must be based on the real published commit, and its resulting tree
must match the verified local source. Recovery receipts, private paths and raw logs remain outside this repository.

## Resulting behavior

| Area | Implemented contract | Important boundary |
| --- | --- | --- |
| Discovery | One registry publishes 63 operation shapes, 41 command descriptors and focused Draft 2020-12 schemas. CLI help and MCP patch schemas share it. | Legacy descriptor text/order and optional envelope fields remain compatible; command descriptors do not fully type every non-patch command body. |
| Vertex and UV edits | Absolute vertex positions propagate through exact reciprocal reverse-cyclic portal copies; texture fitting uses explicit pixel dimensions; alignment continues a representable mapping across a shared edge. | Unconnected coincident vertices stay independent; unsupported geometry/UV space refuses. |
| ARCH authoring | MotS bind, reset/rebind and bake resolve hash-bound resources, validate all LODs and append private samples under bounded model/occlusion work. | Imported unknown data remains unverified; ambiguous affected baseline ownership refuses; no retail-baker equivalence claim. |
| Readback | Sector, template, COG, ARCH object/sample and containment collections retain ordered duplicates, signed flags, typed values and explicit unknown/ambiguous states. | Resource-aware COG/ARCH validation remains a separate command; containment is point classification, not player collision clearance. |
| MCP | 39 named routes delegate to the same domain, normalize documented nested paths, journal original caller arguments and check actual resolved loose resources. | Application-level confinement and rechecks are not an OS sandbox, lock or multi-file transaction. |
| Build | CLI, MCP and their domain contracts link one shared portable domain library. MCP retains a conditional domain target when the CLI is disabled. | Native Qt application and Windows process/UI behavior require separate configurations. |

## Review findings and resolution

The work was reviewed independently across schema admission, geometry/UV/query bounds, ARCH preservation,
and MCP paths/journals. The fresh regressions exercised actual domain commands with synthetic fixtures.

| Finding | Resolution and regression evidence |
| --- | --- |
| Repeated substring searches and whole-collection containment work were insufficiently bounded. | Linear ASCII matching admits a cumulative 8 MiB of pattern/candidate bytes and visits; containment estimates include empty sectors, corners, quadratic face work and vertex/corner products. Three fresh assertions failed before correction. |
| Vertex validation and reciprocal portal matching could exceed practical per-operation work. | One cumulative 8,000,000-unit budget reserves copying, validation, traversal and matching before the work. Eight fresh refusal/preservation assertions failed before correction; bounded positive cases remain admitted. |
| Duplicate stored Thing IDs could erase an ambiguously owned automatic ARCH baseline. | Bind/reset/bake refuse only when the selected owner has an affected baseline and another Thing shares its ID. Fifteen fresh assertions failed before correction. Accepted native duplicate-ID input, exact source preservation, unrelated duplicates and owners without an affected baseline are covered. |
| A focused schema wrongly applied placement RGB limits to enabled sky lighting. | Sky color accepts the finite float range used by the actual lighting validator; placement color retains its own 0..1 limits. A valid sky-color request failed before correction. |
| COG schema conditions, alias precedence, script grammar and reserved Thing frame fields differed from deterministic runtime admission. | Five local schema mismatches were corrected against the actual adapter/core rules. Matching C++ and independent Draft 2020-12 runs each reproduced 27 failures first; all 121 fixtures now pass, including positive compatibility boundaries. Independent source recheck resolved all five findings. |
| Mandatory MCP fixtures assumed Windows symbolic-link privileges. | Only link-dependent setup/assertions are conditional, consistent with existing tests. Portable path, repeated-root and resource checks remain enabled on all platforms; Linux link checks remain mandatory. Windows execution remains a separate gate. |

The independent query/edit review found no additional actionable defects in the corrected bounds, weld
propagation, staged publication or rich-query semantics. The MCP production review found no confirmed
path-admission, journal-identity or preservation defect. Review results are scoped source evidence, not an
independent native/runtime test pass.

## Verification evidence

The configured host is Linux with GCC 14.2, CMake 3.31.10 and C++23 Release; the Qt application is disabled.
The independent validator is Python `jsonschema` 4.26.0 using Draft 2020-12.

```sh
cmake -S . -B build/core-linux -DSED_BUILD_APP=OFF -DSED_CPP_STANDARD=23 -DCMAKE_BUILD_TYPE=Release
cmake --build build/core-linux --parallel 2
ctest --test-dir build/core-linux --output-on-failure --parallel 2
```

| Gate | Observed result |
| --- | --- |
| Published baseline source reconciliation | Exact match: 1,082 file objects/modes; tree identity above. |
| Missing-feature RED gates | Three schema assertions, 76 MCP assertions and 11 rich-query assertions failed against the baseline; shared CLI usage also differed. |
| Focused integrated domain contracts | Six suites passed after the schema-review follow-up in 1.87 seconds: schema, edit, ARCH, rich query, journal completion and MCP parity. |
| Independent schema validation | 65 emitted schemas and 121 fixtures passed; all 57 legacy operation descriptors matched exactly, and all 41 help/catalog usages agreed. |
| Complete configured Release build and CTest | Final configured Release build passed. All 139 CTest checks ran with no exclusions: 138 passed, one failed, in 12.89 seconds. The only failure is the synthetic engine-capture test, which requires an unavailable Xvfb private display; nine of its ten Python subtests passed. |
| Standalone MCP configuration | Configured with SED_BUILD_AGENT=OFF and SED_BUILD_MCP=ON; the conditional domain library and MCP executable/contract test built successfully. MCP parity passed in 0.05 seconds. The normal full configuration was restored before the final complete gate. |
| Final source/documentation and remote tree reconciliation | Pending final commit and publication. |

The capture runner was not skipped or weakened. Its observed failure is missing Xvfb. A separate direct probe
also returned `EPERM` when creating an `AF_UNIX` stream socket, before bind/listen; installing Xvfb alone would
leave that environment prerequisite unresolved. The runner itself did not reach the socket error in this run.
This is an explicit failing environmental gate, not a claim that all configured tests passed.

Static documentation review reconciled all 30 original paths plus this review, matched the 39 public MCP route
names to their catalog, checked 83 local links (including 10 anchors), and parsed 11 JSON examples. A stale
pre-existing parity-guide anchor and an imprecise CMP capture description were corrected. `git diff --check`
passed. No additional implementation gap was found in this bounded source/delivery review.

Test fixture corrections are distinguished from production defects. A schema fixture accidentally contained
72 hexadecimal characters instead of the required 64; an MCP launch-plan fixture incorrectly selected Retail
on Linux; a snapshot fixture used an unsupported size; and the initial schema test had an incorrect include
path. Two new fixture strings also needed distinct C++ raw-string delimiters. Correcting these did not weaken production admission. Existing GCC warnings about `u8path` originated
in the restored baseline and are not represented as new parity defects.

## Remaining acceptance and feature gaps

This pass does not qualify the Qt application, Windows/MSVC execution, native Undo/Redo, renderer pixels,
portable packaging, or actual JKDF2/MotS/IJIM/DroidWorks behavior. Earlier Qt failures remain tracked in the
[historical handoff](2026-11-09-red-handoff.md). The source review and headless gate do not waive them.

The shared ARCH v1 bake-input digest intentionally omits world blocker geometry. Plan/apply source hashes
bind the level used for a bake, but subsequent world changes require a new plan and rebake. The vertex budget
is per operation; the existing patch operation-count limit is separate. Work budgets bound admitted inputs
and estimates, not wall-clock duration.

Exact old .NET envelope/alias compatibility, unknown-record and text-SED/companion-template migration,
dependency-complete prefabs, atomic multi-file authoring, a COG compiler, richer model/sprite snapshots and
causal game acceptance remain open. Straight/switchback agent recipes and captured-palette MAT export already existed
in the published baseline; their stale comparison rows were corrected without claiming new implementation.
The [capability matrix](../capability_matrix.md) and [gap register](../gap_register.md) preserve these distinctions.
