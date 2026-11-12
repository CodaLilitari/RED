# Package node budget verification

## Scope

The correction enforces the [package node-budget contract](../package_node_budget_design.md) in the portable
planner. The episode and level consume two nodes. A new distinct resource must fit the remaining capacity before
lookup; stock omissions still count, and existing callback aliases retain their content-collision checks.
The public API, CLI/MCP request shapes, publication guards and canonical manifest serialization are unchanged.

This is a package admission correction. The inspect-only dependency graph, archive-row provenance and broader
scan/detail budgets remain separate work.

## Regression evidence

The original one-material regression compiled against unchanged production code and failed only at
`package: node budget refuses a third node without package output`. Its three-node positive control passed.

The expanded pre-correction core run produced exactly five failing assertions:

- A third node was not refused at a two-node limit.
- A zero-node limit did not refuse the mandatory nodes with a receipt.
- A one-node limit did not refuse the mandatory nodes with a receipt.
- A stock omission could bypass the node limit.
- A denied resource could reach the resolver.

The exact-fit, duplicate-reference and callback-alias controls passed before the correction. The existing alias
fixture runs at a three-node limit, so both identical aliases and conflicting aliases are exercised when the
distinct-node capacity is full. The corrected core run passes all cases. Refusals check the structured budget
issue and absence of output data, size and hash; mandatory-minimum cases also require a manifest receipt.

These fixtures are synthetic lexical dependency inputs. Opaque material payloads exercise graph admission and
package copying; they do not claim binary MAT validity or game acceptance.

Independent source review found no production-code issue and identified one compatibility qualification: a missing
reference at full capacity is refused before lookup even when `allowUnverifiedReferences` is true. The contract and
agent workflow now state that boundary. An additional regression checks full-capacity refusal and preserves the
missing-resource warning policy when there is spare capacity. Running the expanded tests against the original
implementation produced that expected failure alongside the five established failures. Restoring the reviewed
correction passes the core run and preserves the full-suite result below. No further production change was needed.

## Successful-output compatibility

Nine successful requests were executed before and after the production correction with identical request options:
one material at the default and exact limits; mandatory nodes only; one stock material; duplicate references;
callback aliases; a model-to-material dependency; the same transitive graph from stock inputs; and a COG default
material with its disclosed scope.

The complete public `PackagePlan` records match byte for byte, including archive bytes, output and manifest hashes,
node/edge counts, ordered entries, stock omissions, issues and unverified scopes. The serialized comparison contains
10,563 bytes with SHA-256 `0e91693667409ae39220b31f758e2090196d9ca05b609740a8d802bc66cd345f`.
This establishes compatibility for those fixtures. The admission-only production diff also leaves lookup precedence,
stock-child traversal, output ordering and canonical manifest construction intact.

## Build and test results

Configuration: Linux, GCC 14.2, CMake 4.4.4, C++23, Release, Qt application disabled, agent and MCP enabled.
The initial pre-correction build emits six existing `std::filesystem::u8path` deprecation warnings in
`src/core/src/update/updater.cpp`. No new compiler warning appears in the correction or full-target build.
Updater API modernization remains a separate maintenance item.

```sh
cmake -S . -B build/core-linux -DSED_BUILD_APP=OFF -DSED_BUILD_AGENT=ON -DSED_BUILD_MCP=ON -DSED_CPP_STANDARD=23 -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=/usr/bin/g++-14
cmake --build build/core-linux --target sed_core_tests --parallel 2
ctest --test-dir build/core-linux -R '^sed_core_tests$' --output-on-failure
cmake --build build/core-linux --parallel 2
ctest --test-dir build/core-linux --output-on-failure
```

| Check | Result |
| --- | --- |
| Initial regression build | Passed |
| Initial registered core run | Expected Red: one intended assertion failure |
| Expanded regression build | Passed |
| Expanded registered core run | Expected Red: five intended assertion failures |
| Corrected core build and registered run | Passed, 1/1 CTest entry; final run 7.65 seconds |
| Missing-resource compatibility boundary | Additional expected Red, then Green with the reviewed correction |
| Nine complete successful package comparisons | Exact match |
| All configured Release targets | Build passed |
| Complete configured CTest, no exclusions | 140/141 passed; final run 23.62 seconds |

The sole full-suite failure is `sed_engine_capture_runner`, specifically
`test_full_capture_with_synthetic_engine`: the Linux capture profile requires an unavailable Xvfb executable.
The same 9/10 Python subtest result was reproduced before the production correction. This remains a failed gate;
it is not counted as passing and no test was skipped or weakened.

## Acceptance boundaries

The complete Windows/MSVC/Qt Release gate, native application checks and actual-game acceptance were not run.
The missing Xvfb prerequisite also prevents a completely green configured Linux suite. The scoped correction has
observed Red/Green and compatibility evidence, while release acceptance remains incomplete. GitLab CI is not
used as evidence for these local results.
