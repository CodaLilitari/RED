# Package node budget contract

## Intent

This contract defines the package planner's resource-node admission boundary. The planner reserves the generated
episode and source level before admitting distinct resource nodes, including stock omissions. A future read-only
dependency inspector may reuse this accounting without changing the existing package API.

The package planner exposes a caller-selected `maxNodes`. Its reported graph includes the generated episode,
the source level and distinct resource nodes, including stock omissions. A caller must not receive a buildable
package whose admitted graph exceeds that limit.

## Node accounting and admission

The [package API](../src/core/include/sed/level/package.hpp) defines the options and receipt. The
[implementation](../src/core/src/level/package.cpp) first checks that the limit can contain the two mandatory nodes.
Resource traversal runs only when that minimum fits. Before resolving a new distinct resource, its current count
must be less than `maxNodes - 2`; the minimum check makes that subtraction safe. The reported node total adds the
same two mandatory nodes to the admitted resource count.

Admission applies only to a new distinct node. An already admitted resource can still require a callback lookup
for another authored spelling: matching bytes share the node, while conflicting bytes refuse with
`PackagePathCollision`. A full node budget must not bypass that check. The
[package tests](../src/core/tests/level/package_tests.cpp) cover exact fits, one-over refusal, the mandatory minimum,
stock omissions, duplicate references and callback admission, including aliases at the exact cap.

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
- Requests that remain admitted under the corrected limit retain their output bytes, output hash, manifest hash,
  entry order, stock omissions and unverified-scope behavior.

A new reference encountered at full capacity is conservatively refused before lookup, even if it would turn out
to be missing and `allowUnverifiedReferences` is true. That option does not bypass node admission. With spare node
capacity, a missing reference can still follow the explicit warning policy without adding a resolved resource node.

The refusal assertions deliberately do not prescribe the partial node count, issue count/order or diagnostic
wording. They test the public budget and no-output behavior, not one internal implementation.

## Approaches and decision

A comparison-only change is small but does not address limits below the two mandatory nodes and can leave
overflow-prone arithmetic in admission. A shared dependency-graph refactor would mix a new public feature with
this correction.

The correction stays in `planLevelPackage(const PackageRequest&)`: establish the mandatory-node boundary and use
checked subtraction for remaining-node admission. The existing package API, canonical manifest serialization and
transport behavior remain unchanged. Budget refusals still reach the common receipt and manifest construction path.

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
[AGENTS.md](../AGENTS.md). The [verification record](reviews/package_node_budget.md) distinguishes observed Red/Green,
valid-output compatibility, the complete configured Release run and environment-dependent acceptance.
Static review of a test is not execution evidence; a failed environment-dependent gate is not a passing release.
