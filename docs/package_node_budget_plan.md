# Package Node Budget Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development or superpowers:executing-plans to implement this plan task by task. Steps use checkbox syntax for tracking.

**Goal:** Enforce the package graph node limit before admitting a resource, while preserving valid package outputs.

**Architecture:** Keep `sed::level::planLevelPackage(const PackageRequest&)` and its public types unchanged.
Pin the boundary in the existing core test group, then make the smallest admission correction.
A later dependency-inspection feature may reuse the corrected scanner; it is outside this plan.

**Tech Stack:** Existing portable C++, CMake/CTest and synthetic in-memory package fixtures; no new dependencies.

**Spec:** [Package node budget contract](package_node_budget_design.md).

## Global Constraints

- Follow [AGENTS.md](../AGENTS.md); one source/build writer per path and regression-first TDD.
- No production edit before the new regression compiles and fails for the expected reason.
- Node totals include two mandatory nodes: episode and level.
- Refusal returns `PackageBudgetExceeded` and no output bytes, size or output hash.
- Existing valid package bytes and canonical manifest inputs remain unchanged.
- No GUI, file-format, schema, CLI, MCP or journal changes in this correction.
- Living documentation is date-free; source fixtures contain no personal game data.

## Review Focus

1. One resource beyond the exact node budget must refuse, while the same fixture fits the next larger budget.
2. Limits below the two mandatory nodes must refuse without unsigned-subtraction overflow.
3. Duplicate references must retain the existing distinct-node accounting.
4. Stock omissions must still count as graph nodes.
5. A denied new node must not invoke the resource resolver.

Each condition is assigned to the test steps below. No package-only test selector exists in the current runner;
`sed_core_tests` is the smallest existing registered gate that includes the package group.

## Files and interfaces

- Test: `src/core/tests/level/package_tests.cpp`, inside `sed::tests::runPackageTests()`.
- Modify after observed Red: `src/core/src/level/package.cpp`, `sed::level::planLevelPackage()`.
- Update contract if required: `src/core/include/sed/level/package.hpp`.
- Existing interface: `PackagePlan planLevelPackage(const PackageRequest& request)`.
- Existing fixture helpers: `baseRequest()`, `hasEntry(const PackagePlan&, const std::string&)`,
  and `hasIssue(const PackagePlan&, const std::string&)`.

## Task 1: Establish the exact-fit and one-over regression

- [x] Write a synthetic one-material case. With `maxNodes = 3`, require a buildable three-node plan, the material
  entry and nonempty package bytes. Change only `maxNodes` to two and require the specified budget refusal
  and absent output data/size/hash. This authored draft is not an observed Red result.
- [ ] Configure the documented headless Release build from the project root:

```sh
cmake -S . -B build/core-linux -DSED_BUILD_APP=OFF -DSED_BUILD_AGENT=ON -DSED_BUILD_MCP=ON -DSED_CPP_STANDARD=23 -DCMAKE_BUILD_TYPE=Release
```

- [ ] Build the existing test executable:

```sh
cmake --build build/core-linux --target sed_core_tests --parallel 2
```

- [ ] Run the registered core gate, which applies its configured working directory and temporary-file isolation:

```sh
ctest --test-dir build/core-linux -R '^sed_core_tests$' --output-on-failure
```

Expected from source inspection: the exact-fit control passes and
`package: node budget refuses a third node without package output` fails.
A compile error, a different assertion failure or a passing regression is not the required Red evidence.
Do not use `--package`: it is not a supported test-runner selector.

## Task 2: Pin the remaining admission boundaries

- [ ] Add a zero-resource fixture with the mandatory episode/level nodes. Budgets zero and one must refuse with
  the budget issue and no output; budget two must build. Run the core gate and record the intended failures.
- [ ] Add a repeated material reference at `maxNodes = 3`; require one material output entry and successful
  admission. Do not assert an incidental edge counter.
- [ ] Repeat the exact-fit/one-over case with the material supplied by a stock archive. Require its omission
  receipt and graph-node accounting at the admitted limit, and output refusal at the smaller limit.
- [ ] Add an optional resolver that records calls and returns the material bytes. At the denied-node limit,
  require zero calls; at the admitted limit, require one call and successful output.
- [ ] Run the core gate before production changes. Preserve passing compatibility controls and record which
  new assertions fail. Do not infer results from the source expression.

## Task 3: Correct admission and verify

- [ ] In `planLevelPackage()`, reject a budget that cannot contain the two mandatory nodes, and check a new
  distinct node against the remaining budget before resource lookup/insertion. Use subtraction only after
  establishing the mandatory-node minimum. Keep structured failure/manifest construction intact.
- [ ] Rebuild `sed_core_tests` and rerun the registered core gate; all cases must pass.
- [ ] Review the production diff for unchanged lookup precedence, callback alias collision checks, stock child
  traversal, deterministic entry order and canonical manifest serialization.
- [ ] Commit the tested correction separately from the tests-first draft.

## Task 4: Complete the source-bound gate and handoff

- [ ] Build all configured Release targets:

```sh
cmake --build build/core-linux --parallel 2
```

- [ ] Run the complete configured suite without exclusions:

```sh
ctest --test-dir build/core-linux --output-on-failure
```

This includes the existing agent/MCP package behavior and publication tests. Report every failure by name;
environment failures do not become passing results. Windows/MSVC, Qt and native acceptance remain separate
when unavailable. The Windows commands and build prerequisites are in [the developer guide](README.md).

- [ ] Obtain an independent source review, resolve actionable findings through regressions, verify the final
  publication diff, and update the continuation checkpoint with exact commits and actual test results.
- [ ] Begin the larger dependency-graph extraction only after this correction is independently verified.
