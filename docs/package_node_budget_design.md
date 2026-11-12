# Package node budget contract

## Intent and status

This contract defines the resource-node admission boundary that must be pinned before extracting the package
dependency scanner for a read-only inspection command. The initial change adds a regression and an implementation
plan. It does not implement the correction or establish an observed failing test result.

The package planner exposes a caller-selected `maxNodes`. Its reported graph includes the generated episode,
the source level and distinct resource nodes, including stock omissions. A caller must not receive a buildable
package whose admitted graph exceeds that limit.

## Evidence and failure hypothesis

The [package API](../src/core/include/sed/level/package.hpp) defines the options and receipt.
The [implementation](../src/core/src/level/package.cpp) checks
`nodesByKey.size() + 2 > request.options.maxNodes` before inserting a new resource, then reports
`nodesByKey.size() + 2` as the final node total. Source inspection predicts that a single resolved material can
produce a three-node package when the requested limit is two.

That is a source-derived hypothesis until the regression runs against unchanged production code.
The [existing package tests](../src/core/tests/level/package_tests.cpp) already establish that the episode and
level count as two nodes. They also provide synthetic archive fixtures and structured budget-issue helpers.

## Required behavior

- A level and episode with no resource nodes fit a node budget of two.
- One resolved material, plus the level and episode, fits a node budget of three.
- The same material request with a node budget of two returns `canBuild == false`, a
  `PackageBudgetExceeded` issue, empty `outputData`, zero `outputBytes` and an empty `outputSha256`.
- Limits of zero or one cannot accommodate the two mandatory nodes and must refuse package output.
- A repeated reference to an already admitted resource does not consume another distinct node.
- A stock resource still occupies a graph node even though its bytes are omitted from the package.
- Admission of a new node occurs before resolving or inserting that resource. Rejected admission must not call
  the optional resource resolver for that new node.
- Valid requests retain their output bytes, output hash, manifest hash, entry order, stock omissions and
  unverified-scope behavior.

The refusal assertions deliberately do not prescribe the partial node count, issue count/order or diagnostic
wording. They test the public budget and no-output behavior, not one internal implementation.

## Approaches and decision

A comparison-only change is small but does not address limits below the two mandatory nodes and can leave
overflow-prone arithmetic in admission. A shared dependency-graph refactor would mix a new public feature with
this correction.

Use a narrow correction in `planLevelPackage(const PackageRequest&)`, after the focused regression has failed
for the intended reason. Establish the mandatory-node boundary and use checked subtraction for remaining-node
admission. Keep the existing package API, canonical manifest serialization and transport behavior.

This is an engineering choice within the continuation work. It is not a claim that a new dependency-inspection
interface has been implemented or that its broader design is settled.

## Excluded work

This correction does not redefine edge/depth counters, the zero-byte-budget convention, queue/string allocation
limits, archive precedence, callback aliases, native Level export, prefab formats or runtime closure.
Those are separate regression and design work before a bounded dependency graph can be published.

The prospective graph should expose parent/relation evidence, actual winning archive entries and separate
scan/detail completeness over captured JKL/archive bytes. Existing `package-plan` already provides a transitive
inventory and builds archive bytes. A complete supported-reference scan is not asset grammar validation or
proof of runtime dependency closure.

## Acceptance

Follow [the implementation plan](package_node_budget_plan.md) and the mandatory regression-first workflow in
[AGENTS.md](../AGENTS.md). Compilation and observed Red are required before a production correction.
Static review of the test is not execution evidence. The complete configured Release suite and existing package
transport scenarios must run before this correction is described as validated.
