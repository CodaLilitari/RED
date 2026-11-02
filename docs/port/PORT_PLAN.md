# Port plan — phased implementation with exit criteria

> Companion to [PORT_LEDGER.md](PORT_LEDGER.md). Inputs:
> [CODA_FEATURES.md](CODA_FEATURES.md) (Coda contracts F-00…F-14),
> [REWRITE_MAP.md](REWRITE_MAP.md) (rewrite status at HEAD `b822559`),
> [COG_SLICE_SPEC.md](COG_SLICE_SPEC.md) (normative F-10 detail, A-01…A-37).
> Behaviour/contracts only; synthetic/original fixtures; no retail assets.
>
> **Ordering principle (binding):** foundations first — format/document parity
> (F-00, F-06) before CLI/MCP surfaces (F-01, F-02) before GUI breadth
> (F-12 dialogs, F-08 inspector, renderer polish). The COG core steps are the
> single early exception: they are headless and fully specified, so they run
> in Phase 1 beside the writer work.

## Build/test baseline (all phases)

From [REWRITE_MAP.md](REWRITE_MAP.md) §9–10. Every phase's exit criteria
assume this gate is green end-to-end (a partial run is not the Release gate):

```powershell
$env:QTDIR = "<Qt MSVC kit directory>"
cmake --preset msvc-ninja
cmake --build --preset msvc-ninja-release --parallel 1
ctest --preset msvc-ninja-release
```

New core tests register in `src/core/CMakeLists.txt` (`sed_core_tests`
`add_executable(...)` list) with a `runXxxTests()` declaration in
`src/core/tests/test_main.cpp`, in the existing `check()`/`failures` style
(`src/core/tests/test_common.hpp`). New executables follow the
`sed_document_allocation_tests` precedent: own `add_executable`, own
`add_test`, `LABELS`/`TIMEOUT`/`WORKING_DIRECTORY` set.

---

## Phase 0 — Baseline freeze (no new behaviour)

**Scope.** Record the starting line so later phases can prove "no regression".

- [ ] Clean build + full `ctest --preset msvc-ninja-release` run recorded
  (core 69 + app 107 test `.cpp`; [REWRITE_MAP.md](REWRITE_MAP.md) §9).
- [ ] `git status` shows only `?? docs/port/` — the port docs are the only
  change on `codalilitari/features-port`.

**Exit criteria.** Reproducible green baseline noted in the phase log. No
feature work begins against a red or unrecorded baseline.

## Phase 1 — Foundations: preservation parity + transactional export + COG core

**Scope.** F-00, F-06, and F-10 steps 1/3/4 (schema, binding description,
consistency — all read-only core). This is Slices 1 and 2 (§Slices below).

**Exit criteria (all required).**

1. F-00 parity fixtures pass: unknown-section round-trip, signed-zero UV,
   refuse-don't-repair matrix (refusal leaves the file byte-untouched).
2. F-06: slot-identity preservation across edit; mismatch refuses **before**
   any file write; failed promotion leaves the previous file and material
   registrations untouched; clean retry is byte-identical.
3. F-10 A-01…A-10 and A-11…A-22, A-14 pass; A-cases run under
   `sed_core_tests`. §4.6 regressions (`cog_script_tests.cpp`,
   `pending_cog_import_tests.cpp`, `level_cog_reference_tests.cpp`) green
   **unchanged in behaviour** ([COG_SLICE_SPEC.md](COG_SLICE_SPEC.md) §4.6).
4. No GUI behaviour change in this phase (core-only diff, plus test
   registrations).

## Phase 2 — Headless surfaces: `sed-agent`, then `sed-mcp`

**Scope.** F-01 (full command set), then F-02 as a thin adapter. F-10 step 2
(registry generation + lexical preflight) and step 5 (guarded mutations) land
here too — they need the CLI's review discipline for acceptance, and the
preflight registry work is independent of bindings.

**Exit criteria.**

1. F-01 exit-code table (0/2/3/4/5) covered by tests driving the real binary;
   `plan` provably writes nothing; stale `inputSha256` → exit 4; existing
   output refused (no overwrite flag exists); 1 MiB request cap enforced.
2. F-10 A-23…A-37 pass, including A-33 (invented spelling absent-finding in
   strict + non-strict modes), A-34 (cogext/POV spellings never available),
   A-37 (IJIM `target-unmeasured`). Registry tables are **generated** from the
   measured JSON with binary SHA-256 recorded in the generated header
   ([COG_SLICE_SPEC.md](COG_SLICE_SPEC.md) §3.6.1) — a hand-patched name fails
   review.
3. F-02 stdio tests pass, including interrupted-write → `sed_outcome`
   discipline and snapshot PNG validation. MCP contains no domain logic
   (review check: `tools/sed-mcp/` diffs touch transport, file policy,
   journal, snapshot worker only).

## Phase 3 — Self-contained core features

**Scope.** F-07 (spatial selection), F-13 (vertex-color readiness), F-12 core
commands (exact query filters, batch UV, placement arrays), F-03(b)
(source-light metadata), F-05 (ARCH validation), F-08 (ARCH authoring core).

**Exit criteria.**

1. F-07: `none`/`ambiguous` are successful null-token results (never a
   guessed match); stale token conflicts; original Relay-Outpost-shaped
   fixture passes.
2. F-13: evaluator uses finite `float32` equality (no epsilon); `Unknown`
   survives conversion disagreement; assessed bytes are the plan's
   `outputSha256` bytes.
3. F-12: bounds refuse rather than truncate (256-face / 4096-corner;
   256/array, 1024/patch); name collisions refuse; radial arrays do not
   rotate things.
4. F-03(b): null-vs-explicit-zero survives JKL save/reopen; checksum-mismatch
   metadata is rejected, not applied; fixed checksum vector re-derived.
5. F-05/F-08: lower-LOD overflow refused even when the preview LOD fits;
   undo restores the exact prior ARCH binding; imported-sample provenance is
   never upgraded to "verified bake".

## Phase 4 — GUI breadth + renderer semantics

**Scope.** F-10 step 6 (GUI wiring: preflight view, typed placed-COG rows with
explicit positional fallback, prepare-step refusals, `break`/`continue`/
`switch` completion correction), F-12 dialogs, F-08 inspector, F-03(a)
renderer channels, F-04 (MAT flag rules, draw ordering, 3DO writer if
scheduled).

**Exit criteria.**

1. GUI and CLI produce identical results for shared operations (parity checks
   through the Phase 2 harness — the GUI must not grow a private edit path,
   [CODA_FEATURES.md](CODA_FEATURES.md) F-00).
2. F-10 GUI: positional fallback is labelled *"editing values positionally
   without guessed types"*; refusals show the stable code and change nothing
   ([COG_SLICE_SPEC.md](COG_SLICE_SPEC.md) §3.9).
3. F-04: offscreen probe confirms index-zero discard only for
   masked/translucent; no transparency-fidelity claim anywhere in docs/UI.

## Phase 5 — Engine harnesses (externally gated)

**Scope.** F-09 capture harness (adapted pattern), F-11 authoring completion
+ acceptance oracle.

**Entry gate.** A `sed-engine-capture-v1`-capable OpenMotS build is
available. Without it this phase does not start and engine acceptance stays
**unverified** (prior result 2) — screenshots and retail executables are not
substitutes ([CODA_FEATURES.md](CODA_FEATURES.md) F-09).

**Exit criteria.**

1. F-09 receipts: exit-0 + exact P6 RGB8 + exclusive outputs + unchanged
   inputs; `effectiveCameraObserved` / `runtimeResourceResolutionObserved`
   remain `false`.
2. F-11: all four negative controls (no activation, wrong objective, barrier
   behind gate, pre-completed goal) **fail** acceptance; witnesses are bound
   to observed wall time. Any witness weakening re-opens the phase
   (`c935187` precedent, [CODA_FEATURES.md](CODA_FEATURES.md) F-11).

## Phase 6 — Updater (adapted, last)

**Scope.** F-14 re-derived for the Qt layout on top of the existing
`sed_release_package` / `RELEASE_MANIFEST.json` machinery
([REWRITE_MAP.md](REWRITE_MAP.md) §8).

**Exit criteria.** Equal-version/downgrade refusal; staged-beside (never
overwrite) install; per-file receipts rechecked at launch; failure leaves the
old editor running; docs state the feed is not independently signed
([CODA_FEATURES.md](CODA_FEATURES.md) F-14).

---

# First three implementation slices (concrete)

## Slice 1 — F-00/F-06: preservation parity + transactional JKL export

**Goal.** Prove (and where needed, fix) that open → edit → save preserves
unmodelled data and slot identities, and that export is transactional.

**Files to add.**

| File | Contents |
|---|---|
| `src/core/tests/assets/jkl_preservation_parity_tests.cpp` | Unknown-section round-trip; signed-zero UV; numeric-spelling preservation; refuse-don't-repair matrix |
| `src/core/tests/assets/jkl_export_transaction_tests.cpp` | Slot-identity preservation across a sector edit; pre-write refusal on mismatch; failed-promotion leaves previous file + registrations untouched; clean-retry byte-identity |

**Files to touch.**

| File | Change |
|---|---|
| `src/core/src/assets/jkl.cpp` | Split export into non-mutating build + commit-on-successful-replacement (mirror Coda `JklWriter.Build`/`Save`, [CODA_FEATURES.md](CODA_FEATURES.md) F-06); retained-source hash re-check before section replacement |
| `src/core/include/sed/assets/jkl.hpp` | Declare the build/save split beside `inspectJklExportBuffers` |
| `src/core/tests/test_main.cpp` | `runJklPreservationParityTests()` / `runJklExportTransactionTests()` declarations + calls |
| `src/core/CMakeLists.txt` | Register both test files in `sed_core_tests` |

**Tests to add.** The two suites above (derive cases from Coda
`JklMotsPreservationTests.cs` 21, `JklExportReferenceTests.cs` 19,
`JklWriterTransactionTests.cs` 5, `SectorColormapIdentityTests.cs` 8 — re-derived
with original fixtures). **Negative controls:** corrupted retained-source hash
→ refusal with zero bytes written; simulated promotion failure → previous file
hash unchanged.

**Done when.** Phase 1 criteria 1–2 pass and the full CTest gate is green.

## Slice 2 — F-10 core: COG schema, binding description, consistency

**Goal.** [COG_SLICE_SPEC.md](COG_SLICE_SPEC.md) §5 steps 1, 3, 4 — all
read-only, zero mutation risk.

**Files to add.**

| File | Contents |
|---|---|
| `src/core/include/sed/assets/cog_schema.hpp` + `src/core/src/assets/cog_schema.cpp` | Full symbols-schema parse (§3.3): `CogSchema`, `CogSymbol`, `CogSchemaDiagnostic`, `isComplete()`, `levelValues()`; Coda diagnostic code strings verbatim |
| `src/core/include/sed/level/cog_bindings.hpp` + `src/core/src/level/cog_bindings.cpp` | `describeBindings(...)` (§3.4): script status, rows to `max(actual, expected)`, target resolution, event-link candidacy with the static-candidate disclaimer |
| `src/core/tests/assets/cog_schema_tests.cpp` | A-01…A-06 |
| `src/core/tests/level/cog_binding_tests.cpp` | A-07…A-13, A-15…A-22 (A-14 lands with the consistency pass) |

**Files to touch.**

| File | Change |
|---|---|
| `src/core/include/sed/values.hpp` | `CogValue` gains `explicitLinkId` (`std::optional<std::int32_t>`), `explicitMask` (`std::optional<std::uint32_t>`), `noLink` (`bool`) — additive, defaults preserve current files (§3.2). Effective values computed, never stored |
| `src/core/src/ui/checks/level_check_utils.cpp` equivalent (`src/app/src/ui/checks/level_check_utils.cpp`) | Wire the §3.8 consistency pass beside `cogParameterExtension` / `cogScriptResource` without changing their wording |
| `src/core/tests/test_main.cpp`, `src/core/CMakeLists.txt` | Register the two new suites |

**Explicitly not in this slice.** `cog_script.cpp` stays the import-hydration
compatibility loader (§2.1); the schema parser is the authoring-grade reader.
Where they disagree, the schema parser wins for authoring, the loader's
payload stays import-only (§3.2). No mutations, no preflight, no GUI.

**Done when.** A-01…A-22 + A-14 pass; §4.6 regression suites green unchanged.

## Slice 3 — F-01 subset: `sed-agent` skeleton

**Goal.** A headless executable with `help`, `schema`, `inspect`, `query`,
`plan`, `apply` over `sed_core`, establishing the protocol contract every
later surface reuses.

**Files to add.**

| File | Contents |
|---|---|
| `tools/sed-agent/CMakeLists.txt` | New executable target linking `sed_core` only (no Qt); `add_test` entries per the allocation-test precedent |
| `tools/sed-agent/main.cpp` | Argument/JSON entry; exit-code mapping (0/2/3/4/5); 1 MiB request cap |
| `tools/sed-agent/agent_workspace.*` | Isolated-document plan/apply: `plan` writes nothing and returns `outputSha256`; `apply` optionally requires it, re-checks inputs, creates a **new** file atomically, never overwrites |
| `tools/sed-agent/tests/agent_cli_tests.cpp` (+ end-to-end script) | Exit-code table; stale-hash conflict; existing-output refusal; plan-writes-nothing proof (directory listing + hashes before/after) |

**Files to touch.**

| File | Change |
|---|---|
| `CMakeLists.txt` (root) | `add_subdirectory(tools/sed-agent)` behind an option (default ON, mirroring `SED_BUILD_APP`) |

**Protocol contract (from [CODA_FEATURES.md](CODA_FEATURES.md) F-01).**
Numeric selectors valid only for the exact `inputSha256`/`sourceRevision`
they were read from; the CLI *"does not claim to implement MCP."* Later
commands (`select/validate/new/convert/archive/extract/snapshot/launch-plan`,
diagnostics) extend this skeleton — they do not redesign it.

**Done when.** Phase 2 criterion 1 passes for the six skeleton commands and
the full CTest gate (including the new target) is green.

---

## Risks carried forward

- **Registry drift (F-10):** the measured JSON files are the only source.
  Re-measurement regenerates tables + header hashes in the same change;
  single-name hand patches are review blockers ([COG_SLICE_SPEC.md](COG_SLICE_SPEC.md) §6).
- **Surface identity after hydration (F-10/F-06):** derive serialized order
  from the JKL writer; never re-sort (§6). A-21/A-22 catch divergence.
- **Coda totals are not port evidence:** checkpoint suite sizes quoted in
  [CODA_FEATURES.md](CODA_FEATURES.md) (e.g. 1594 Core + 271 MCP) describe
  Coda's verified state. Each phase re-runs the equivalent port tests before
  claiming a feature ported (Appendix, [CODA_FEATURES.md](CODA_FEATURES.md)).
- **External gate (Phase 5):** no capture-capable engine build → Phase 5
  stays unstarted and engine acceptance stays unverified. Do not simulate it.
