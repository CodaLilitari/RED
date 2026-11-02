# Guarded-COG-authoring port slice — target behaviour spec

Status: **spec for implementation**, 2026-11-01. This document describes
behaviour and contracts to reimplement originally in C++/Qt. It does not copy
C# code. Fixtures are synthetic/original only; no retail assets are used or
required.

| Item | Value |
| --- | --- |
| Coda repo (read-only, never modified) | `C:\Users\Andrei\Documents\ChatGPT\SED-MotS-Net`, branch `main` |
| Fork range | `dotnet-source/main..main` = 46 commits; COG work is `8d04786` (EDIT-01) and `370a881` (PLAY-01), on `5febeb3` foundation |
| Port target (writable, this file only) | `C:\Users\Andrei\Documents\ChatGPT\SED-Qt-Port`, branch `codalilitari/features-port` (`smlu/SED` develop lineage: `src/core`, `src/app`, CMake, tests) |
| COG references (read-only) | `C:\Users\Andrei\.codex\skills\jk-mots-cog-workflow\references\` (`language-and-execution.md`, `verb-families.md`, `authoring-and-testing.md`, `cogext-source-contracts.md`, `dynamic-pov-extensions.md`, `jk101-retail-registry.json`, `mots-retail-registry.json`, `retail-global-floats.json`) and `scripts\inspect_cog.py` |
| Companion inventories | [CODA_FEATURES.md](CODA_FEATURES.md) F-10/F-11, [REWRITE_MAP.md](REWRITE_MAP.md) §1 COG |

Where Coda states a contract, the citation gives the Coda doc path and section
title at HEAD. Section titles are HEAD titles.

---

## 0. Terminology — read this first

Three different checks are routinely conflated. The port keeps them separate
in code, in diagnostics, and in wording:

| Check | What it proves | What it does **not** prove |
| --- | --- | --- |
| **Symbols-schema parse** | The `symbols … end` block is completely understood; its non-`local`, non-`message` declarations form the ordered level-value contract | That the `code` section parses, that verbs exist, or that the script runs |
| **Lexical preflight** | Every call spelling and declared message spelling occurs in the **measured registry for the selected target** (`jk` or `mots`); declarations/labels are inventoried | Argument count/types, label reachability, resource existence, bytecode layout, multiplayer checksum, or runtime behaviour. `inspect_cog.py` states this in its own output: `method: lexical-preflight-v3-not-a-compiler` |
| **Compiler / engine load** | The target engine's parser/codegen accepted the script | Nothing in this slice claims it. No component in either codebase compiles COG |

Coda implements the first row plus typed binding inspection/guarded edits
(§1). The COG workflow references define the second row (§3.6). Neither
codebase implements the third row, and this slice does not add it: **no COG
compiler, no VM, no execution, no checksum prediction** (see §3.10).

Targets:

- `jk` = original **JK 1.01** retail executable.
- `mots` = original **MotS** retail executable.
- Enhanced-engine (OpenJKDF2/OpenMotS source, forks, `cogext`, dynamic-POV
  builds) is a **different target**, never folded into `jk`/`mots` (§3.6.3).

---

## 1. Coda inventory — guarded COG authoring (fork)

Feature F-10 (EDIT-01, `8d04786`) with the `addCog` extension from F-11
(PLAY-01, `370a881`). Foundation types arrive in `5febeb3`.

### 1.1 Symbols-schema parser — `CogScript`

Source: `dotnet/src/Sed.Formats/Cogs/CogScript.cs` (M in `8d04786`).
Tests: `CogSchemaTests.cs`, `CogScriptTests.cs` (see §4).

Behaviour:

- Parses **only** the `symbols` block. `CogScript.Parse(name, text)` retains
  the exact `SourceText`, per-symbol `RawDeclaration`, `RawModifiers` in
  source order, `LineNumber`, and comment-derived `Description`.
- Symbol types (`CogSymbolType`): `message, int, flex, float, vector, thing,
  sector, surface, sound, material, model, template, keyframe, ai, cog`
  (plus `Unknown`). Comment stripping respects single/double quotes; `#` and
  `//` are line comments; quoted defaults such as `'1 2 3'` or
  `"a,# // b"` do not shift the positional schema.
- Modifiers, comma-joined forms included (`nolink,desc=…`): `local`,
  `nolink`, `desc=`, `linkid=`, `mask=`. Link state is kept three ways —
  explicit (`ExplicitLinkId`/`ExplicitMask`, `null` when absent), engine
  default (`EffectiveLinkId = 0`, `EffectiveMask = 0x401`), and `NoLink`
  (a later `linkid=` overrides `nolink`). `Effective*` is documented in
  source as *"OpenJKDF2 reference-symbol interpretation; not proof of a
  runtime subscription."*
- `LevelValues` = symbols with `TakesLevelValue` (`!Local && Type != Message`)
  in declaration order. The header comment records the retail verification:
  placed-COG values map in order onto `LevelValues`, matched for all 96
  resolvable placed COGs across `01narshadda`, `03katarn`, `07yun`,
  `09fuelstation` with no mismatches. This is a **data observation**, not a
  language theorem; the port re-derives it from its own fixtures (§4).
- `IsComplete` ⇔ `HasSymbolsBlock && HasSymbolsEnd && Diagnostics.Count == 0`.
  Only a complete schema is eligible for typed positional edits. Every
  layout problem is a `CogSchemaDiagnostic(Code, LineNumber, Message,
  RawDeclaration)`; unknown declarations stay in `RawSymbolDeclarations`
  even though permissive display skips them.

Diagnostic codes emitted by the parser (complete list at HEAD):

| Code | Trigger |
| --- | --- |
| `missingSymbolsBlock` | No `symbols` block found |
| `missingSymbolsEnd` | Block has no terminating `end` |
| `multipleSymbolBlocks` | Second `symbols` block |
| `duplicateSymbol` | Case-insensitive duplicate name |
| `unknownDeclaration` | Unknown type token |
| `unsupportedDeclaration` | No name, bad name/layout, empty default, unterminated quote |
| `unknownModifier` | Modifier outside `local/desc=/nolink/linkid=/mask=` |
| `invalidModifier` | `linkid=` not a complete decimal int32; `mask=` not a complete hex uint32 |

Contract citation: `docs/EDITING_FOUNDATIONS.md` § "Agent contracts"
(`bindings` paragraph) — *"Missing/incomplete schemas do not type raw
integers."*

### 1.2 Typed binding model — `CogBindings`

Source: `dotnet/src/Sed.Formats/Cogs/CogBindings.cs` (A in `8d04786`,
extended in `370a881`). Tests: `CogBindingsTests.cs`,
`AgentCogBindingTests.cs`.

Read-only inspection — `Describe(document, cogIndex, script)` returns a
`CogBindingDescription`:

- `ScriptStatus`: `missing` / `nameMismatch` / `complete` / `incomplete`;
  `SchemaComplete`; `ActualParameterCount`; `ExpectedParameterCount`
  (`null` unless complete); `ParameterCountMatches` (`null` unless complete).
  Diagnostics add `missingScript`, `scriptNameMismatch`,
  `parameterCountMismatch` on top of the parser codes.
- One `CogBindingRow` per position up to `max(actual, expected)`: symbol
  name/type, `RawValue`, full `Symbol` (modifiers included), resolved
  `Target`, and `EventLinkCandidate`.
- `CogBindingTarget(Kind, Status, Index, SectorIndex, SurfaceIndex, Detail)`
  with `Kind ∈ {Thing, Sector, Surface, Cog, Other}` and
  `Status ∈ {Unknown, NotReference, Missing, Invalid, Null, Level,
  UnplacedUnchecked, StaticUnchecked}`.
- Resolution rules:
  - No unambiguous symbol → `Unknown` (*"No unambiguous symbol type is
    available for this raw parameter"*); missing script never labels raw
    values as references.
  - Non-reference kinds (`int/flex/…`) → `NotReference`.
  - Index text must satisfy `CogReferenceRules.TryIndex` (complete decimal
    int32, optional leading sign; `1tail`, `1.0`, `2147483648` are
    `Invalid`).
  - Negative → `Null`; only `-1` is canonical for authored writes, other
    negatives are retained legacy sentinels on read.
  - `thing`/`sector` → `Level` iff `index < placed count`, else `Invalid`.
    Declared capacity (`ThingCapacity`) is **not** headroom.
  - `surface` → global serialized index, resolved through the document's
    retained-source surface map to `(SectorIndex, SurfaceIndex)`; ambiguous
    retained identity → `Unknown` and blocks rebinding
    (`unresolvedSurfaceIdentity`). Imported levels must use the
    `JklDocument` overload; the `Level` overload is for newly authored
    model-order geometry only.
  - `cog` → `CogReferenceRules.CheckCogIndex`: bit `0x8000` set →
    `StaticUnchecked`; `index < placedCogCount` → `Level`; otherwise
    `UnplacedUnchecked` (template/runtime instances allocate after placed
    COGs, so the placed list cannot prove an upper bound).
- `EventLinkCandidate` is `true` only when the target is `Level`, the kind
  is `thing/sector/surface`, `EffectiveLinkId >= 0` and
  `EffectiveMask != 0`. The description carries the fixed disclaimer
  `EventLinkSemantics = "OpenJKDF2 reference-symbol modifiers; static
  candidate only, not runtime or multiplayer proof"`. A reference and a
  potential event link are reported **separately** (`CogBindingsTests.
  ReportsReferencesSeparatelyFromPotentialEventLinks`).

Guarded mutations — both return a shared undoable command and never mutate
on refusal (`CogBindingException` with stable `Code`):

- `PrepareAdd(document, scriptName, script, bindings, existingScriptResolver?)`:
  - Script name: ASCII `.cog` basename, `≤ MaxAuthoredScriptName = 31`
    chars (legacy `SithCogScript.aName` is a 32-byte field; imported longer
    names stay readable, only **new authored** names are limited)
    → `invalidCogScriptName`.
  - Script must be non-null, `IsComplete`, and name-matching
    → `incompleteCogSchema`.
  - `bindings.Count` and `script.LevelValues.Count` ≤
    `MaxAuthoredBindings = 256` → `cogBindingBudget`.
  - Every external symbol supplied **exactly once by name**, matching
    declared kind; count equality is exact; **no defaults are inferred**
    → `duplicateCogBinding`, `cogParameterCountMismatch`,
    `cogSymbolMismatch`.
  - Only `thing/sector/surface/cog` external kinds are supported for new
    COGs; other kinds must be script-local constants
    → `unsupportedCogBindingType`.
  - `TargetIndex < -1` refused; only `-1` authors null; resolved target must
    be `Level` or `Null` → `invalidCogTarget`.
  - Append-closure check (`RequireAppendPreservesExistingReferences`):
    every existing placed COG must resolve to a matching complete schema
    with exact parameter count (`incompleteExistingCogSchema`,
    `existingCogParameterCountMismatch`); budgets
    `MaxExistingCogsForAppend = 4096` COGs / 100 000 parameters
    (`cogClosureBudget`); an existing `cog`-typed value whose
    `CheckCogIndex` classification would change after append
    → `existingCogReferenceCapture`; one that stays `UnplacedUnchecked`
    still refuses → `existingCogRuntimeReference`, because append can shift
    later runtime/template allocation. The value is **not assumed invalid**
    — the author must resolve it explicitly.
- `PrepareRebind(document, cogIndex, parameterIndex, expectedSymbol,
  expectedType, targetIndex, script)`:
  - Requires complete schema (`incompleteCogSchema`), exact existing count
    — **never pads or discards** (`cogParameterCountMismatch`), in-range
    index (`cogParameterOutOfRange`), exact symbol name+type match
    (`cogSymbolMismatch`), supported kind (`unsupportedCogBindingType`),
    existing `StaticUnchecked`/`UnplacedUnchecked` values refused rather
    than reinterpreted (`unsupportedCogReference`), `targetIndex < -1`
    refused (`invalidCogTarget`), surface identity understood even when
    nulling (`unresolvedSurfaceIdentity`), target resolves to `Level` or
    `Null`.
  - On success the prepared `SetCogValueCommand` is shared/undoable and all
    other raw values (including spellings like `" +000 "`, `" 1.2500"`,
    `"texture.mat"`) are preserved byte-for-byte until applied.

Low-level commands (`dotnet/src/Sed.Core/Editing/CogCommands.cs`):
`SetCogValueCommand` pads with `"0"` rather than leaving a gap (a gap would
shift every later symbol); `SetCogScriptCommand` clears values wholesale on
script change; `CreateCogCommand`/`DeleteCogCommand` renumber on apply and
undo restores the deleted COG **at its original index** because scripts
refer to each other by COG number.

### 1.3 Consistency validation

Sources: `dotnet/src/Sed.Formats/Cogs/CogValidation.cs` (bridge),
`dotnet/src/Sed.Core/Validation/ConsistencyChecker.cs` (`CheckCogs`),
`dotnet/src/Sed.Core/Validation/CogReferenceRules.cs`.

- `CogValidation.Options(library, retailProfile)` supplies
  `ResolveCogParameters`: it returns the typed parameter definitions only
  when the script resolves, `IsComplete`, **and**
  `LevelValues.Count == cog.Values.Count`; otherwise `null`.
- `CheckCogs` with no resolver emits one warning per COG: *"COG script
  schema unavailable; positional references were not checked."* With a
  resolver it checks each typed parameter: non-decimal → error; `cog`
  `StaticUnchecked`/`UnplacedUnchecked` → **warning** (runtime bounds
  unverified); `thing/sector/surface` `index >= count` → error.
- `RetailProfile` only enables the inferred retail thing-capacity warning
  (serialized capacity > 640); it does not clamp or apply retail assumptions
  to OpenJK profiles. Contract: `docs/EDITING_FOUNDATIONS.md` § "Agent
  contracts" (`query`/capacity paragraphs).

### 1.4 Script resolution — `CogScriptLibrary`

Source: `dotnet/src/Sed.Formats/Cogs/CogScriptLibrary.cs`. Tests:
`CogSchemaResolutionTests.cs`.

- Resolves a placed name (e.g. `00_door.cog`) through the shared resource
  lookup preferring `cog\`, searching **both** level and resource archives
  (retail `03katarn`: all 25 distinct placed scripts resolve only when both
  are consulted). Results, including misses, are cached case-insensitively.
- Ambiguity is a refusal, not a choice: if the first matching archive holds
  ≠1 entries (including duplicates under `cog/` vs another folder, with
  `cog/` winning within one archive), `Get` returns `null` and caches the
  refusal; a higher-priority ambiguous archive never falls through to a
  valid lower one. Basename fallback matches the explicit agent provider
  policy exactly.
- GUI lifetime rule (from EDIT-01 review corrections, `docs/PROJECT_STATE.md`
  completed-milestone paragraph for `8d04786`): ambiguity/lifetime defects
  in GUI script resolution were corrected; the editor window discards
  uncommitted text when resource providers are replaced
  (`CogEditorWindow.CloseForResourceChange`).

### 1.5 Agent surface — `bindings`, `addCog`, `rebindCogReference`

Source: `dotnet/src/Sed.Formats/Automation/AgentCogOperations.cs`.
Contracts: `docs/EDITING_FOUNDATIONS.md` § "Agent contracts";
`docs/AGENT_PROTOCOL.md` § "Commands" (`bindings` row), § "Placed COG and
movement-path authoring".

- `bindings` (read-only query): requires `query cogs` index first plus
  explicit hash-bound `resourceArchives`. Response carries `scriptSha256`,
  `SchemaComplete`, exact counts, original parameter indices, raw values,
  modifiers, resolved target addresses, and `eventLinkCandidate`s, paged
  (`offset`/`limit`, default from `AgentQueries`, max 200; 1 MiB script and
  response budgets; fields are never truncated). Read scripts are decoded
  Latin-1. `bindingResourceRevision` rechecks **this query's** read set;
  it is explicitly not the patch `resourceRevision`.
- Patch ops `addCog` / `rebindCogReference` go through the ordinary
  plan/apply protocol: explicit `resourceArchives` and reviewed
  `scriptSha256` are mandatory; a script that is missing or differs from
  the reviewed bytes is a conflict (`AgentConflictException`), at plan time
  and again at commit time, with no output written. Plan leaves inputs
  unchanged; applied output is reopened and compared
  (`docs/EDITING_FOUNDATIONS.md` § "Acceptance and evidence").
- The new COG's index appears in the plan and is addressable by later ops
  in the same batch. Scripts are *"neither rewritten nor executed, and a
  correct binding is not gameplay proof"* (`docs/AGENT_PROTOCOL.md`
  § "Placed COG and movement-path authoring").

### 1.6 GUI and generation

- `dotnet/src/Sed.App/CogEditorWindow.cs` (M in `8d04786`): lists placed
  COGs, resolves scripts via `CogScriptLibrary`, labels values with symbol
  name/type, and edits references through `CogBindings.PrepareRebind` for
  `thing/sector/surface/cog` (pickers list the same descriptions Find
  shows). When the script is missing/ambiguous, the schema is incomplete,
  or counts mismatch, it falls back to **positional** editing with an
  explicit note ("editing values positionally without guessed types",
  "no symbol layout is assumed") and still shows the symbol reference.
  Each row shows target `Status`/`Detail` plus `Link ID`, `mask`, and the
  event-link-candidate wording ("runtime unverified"). Changing the script
  commits `SetCogScriptCommand` (values cleared, §1.2).
- `MasterCogGenerator` / `CogGeneratorWindow`
  (`dotnet/src/Sed.Formats/Cogs/MasterCogGenerator.cs`,
  `dotnet/src/Sed.App/CogGeneratorWindow.cs`): generates an original level
  master COG (goals via `SetGoalFlags`, starting weapons/ammo with the
  documented conditional-ammo rules, Force rank on timer). It writes a
  `.cog` file the author then places; it does **not** write `cogstrings.uni`
  and says so in the UI status. Its verb spellings are a generation
  template, not a verb registry (see §1.8).

### 1.7 Append guards beyond COG creation

Source: `dotnet/src/Sed.Formats/Cogs/CogThingAppendGuard.cs` (foundation in
range). Tests: `CogThingAppendGuardTests.cs`. Contract:
`docs/TOOL_WORKBENCH.md` § "New thing and light arrays".

- Before appending a **thing** (array or single), every placed COG must have
  a matching complete schema with exact count; each external `thing`
  parameter must be canonical `-1` or a currently placed index. Future,
  runtime, `0x8000`-bit, non-canonical (`-01`, `1.0`, `0x0`, overflow) and
  malformed values refuse (`invalidExistingThingReference`,
  `existingThingRuntimeReference`, `incompleteExistingCogSchema`,
  `existingCogParameterCountMismatch`). Scalar values and script-local
  `thing` constants are not guessed to be references.
- Budgets `MaxCogs = 4096`, `MaxParameters = 100 000` are editor work
  limits, checked **before** any resolver call beyond the budget.
- The guard revalidates on every append: no schema cache survives a call;
  current thing count is rechecked. It explicitly does not analyse
  arbitrary script code, local constants, computed indices, or
  template-attached scripts.

### 1.8 What Coda does **not** do (verified absence)

Searched `dotnet/src` at HEAD: there is **no verb table, verb registry, or
per-target verb/message availability check** anywhere in the fork. Coda
never opens the `code` section for validation; `CogScript` stops at the
symbols block, and `MasterCogGenerator`'s emitted spellings
(`SetMasterCOG`, `GetSelfCOG`, `SetInv`, …) are template text, not a
checked registry. Message handling is limited to: `message` symbols are
excluded from `LevelValues`, and `EffectiveLinkId/EffectiveMask` candidate
reporting. Consequently:

- "Verb availability per target" in this slice is a **new port behaviour**
  specified in §3.6 from the measured registries — it must not be described
  as ported from Coda.
- The Coda fixtures in §4 prove schema/binding behaviour only. They prove
  nothing about verb spellings, message delivery, or runtime execution.

### 1.9 Scenarios / fixtures Coda uses

Named, original/synthetic fixtures (no retail assets in tests):

| Fixture / test file | COG behaviour exercised |
| --- | --- |
| `CogScriptTests.cs` — `00_door.cog`-shaped script (tabs, defaults, locals, `desc=`) | Parse types/defaults/locals; `LevelValues` excludes locals+messages; comments→descriptions; code-section text ignored; `SetCogValueCommand` padding; script-change clears values; delete restores original index; JKL round-trip |
| `CogSchemaTests.cs` — `original.cog`, `x.cog` layouts | Raw declaration/modifier retention; explicit-vs-default link/mask distinction; 15 diagnostic-code cases; quoted defaults/comments; unknown declarations retained |
| `CogSchemaResolutionTests.cs` — temp GOB archives | Duplicate entries unresolved; ambiguous higher archive blocks fallthrough; `cog/` folder wins; basename fallback; first-archive priority; incomplete schemas never supply types (3 layouts); count mismatch refuses; complete+exact retains kinds incl. `local`/`message` exclusion |
| `CogBindingsTests.cs` — `LevelWithValues`, `fixture.cog`, noncanonical two-sector JKL | Reference vs event-link separation; COG index status table (`-1/-8/1/2/32768/1tail/2147483648/1.0`); placed-count bounds (capacity ignored); missing/ambiguous schema leaves values untyped; count mismatch describes rows, never pads; undoable rebind preserves other raw values; 4 supported kinds rebind/clear; wrong symbol/type/range/target refusals; static/runtime COG refusal both directions; imported global surface order resolves + survives save; changed retained identity → `Unknown` + refusal |
| `AgentCogBindingTests.cs` — `original.cog` in temp GOB + generated JKL | Paged typed discovery binds bytes/modifiers without writing; guarded plan/commit replays resource, preserves other values (`1.25`); script change before/after plan refuses without output; incomplete schema shows `Unknown`, refuses mutation; missing script keeps raw values untyped; invalid page inputs refuse |
| `CogThingAppendGuardTests.cs` — `guard.cog` | Placed/`-1` pass without mutation; future/static values refuse; non-canonical negatives refuse; scalars/locals not guessed; missing/incomplete schema refuses; name+count exactness (case-insensitive, no basename-only); revalidation each call; all COGs inspected, per-call cache only; 4096/100 000 budget boundaries |
| `PlayableAuthoringTests.cs`, `PlayableAuthoringCorrectionTests.cs`, `PlayableSurfaceBindingTests.cs` (`370a881`) | `addCog` + `setThingFrames` authoring, append-refusal corrections, surface binding across multi-sector/retained order; four-room PLAY fixture authored via tool ops (see `docs/PLAYABLE_AUTHORING.md` § "Acceptance to run" gates 1–7). Runtime acceptance is engine evidence, **out of scope** for this slice (§3.10) |

`ConsistencyChecker` COG behaviour is covered through
`CogSchemaResolutionTests` (warning when schema unavailable) — there is no
separate COG consistency fixture; the port adds one (§4, A-14).

---

## 2. Rewrite inventory — what the C++/Qt port already has

HEAD `b822559`, branch `codalilitari/features-port`. Summary in
[REWRITE_MAP.md](REWRITE_MAP.md) §1 "COG": *"Core parses the `symbols`
section only … It does not compile or execute COG"* plus a full app script
editor. Detail:

### 2.1 Core (`src/core`, Qt-free)

| Piece | Behaviour today | Evidence |
| --- | --- | --- |
| Symbols parser | `loadCogSymbolsNoLocals[Result]`, `loadCogSymbolsWithDefaults[Result]`, `loadCogSymbolsFromTextResult` → `CogSymbolLoadResult{status: loaded/missing/malformed, symbols, diagnostic}`. Type spellings incl. aliases (`sec/srf/thg/key/mat/msg/3do/wav/tpl/vect/integer`); attributes `local/nolink/desc/mask/linkid`; quote-aware comment stripping; vector declaration initialisers ignored → `(0/0/0)` (engine quirk, tested); unset refs default `-1`, `int 0`, `flex 0.0`, resource/message `""`; repeated `symbols`/`local` orphan lines tolerated per engine config-reader behaviour; unterminated quote → `malformed` with **no** symbols; invalid value/declaration → `malformed` **with** compatibility payload retained | `src/core/include/sed/assets/cog_script.hpp`, `src/core/src/assets/cog_script.cpp` (`parseCogSymbolsText`) |
| Value model | `CogValue : Value` (`cogType`, `local`, `description`, `mask`; **no** link-id fields), `Cog{name, values}`, `Level::cogs` | `src/core/include/sed/values.hpp`, `src/core/include/sed/level.hpp` (`struct Cog`) |
| JKL import hydration | Wire COG rows stored as raw `paramN` (`CogType::unk`) values; `finalizeJklImportCogValues` resolves schemas after full level decode, maps global surface wire indices through `importedJklSurfaceRefs`, fills missing trailing values from script defaults, keeps generic values+spellings when the script is missing/malformed, and **throws** when unresolved raw values coincide with ambiguous pending surface references | `src/core/src/assets/jkl.cpp` (`finalizeJklImportCogValues`, `finalizeJklImportResources`); `Level::hasPendingJklCogValues`, `hasAmbiguousPendingJklCogSurfaceReferences` |
| Geometry COG preservation | `captureCogReferences` captures `sector`/`surface` typed values semantically; `GeometryCogReferences` remaps across sector split, owner swap, sector/surface erase; publish rewrites global indices and clears stale paired text | `src/core/src/level/cog_reference_utils.hpp` |
| Delete fixups | `levelDeleteThing` clears/decrements typed `thing` values and clears stale text; `levelDeleteCog` clears/decrements typed `cog` values; `renumberCogs`; structural edits refuse while ambiguous pending references exist | `src/core/src/level/edit.cpp` |
| Tests | `cog_script_tests.cpp` (601 lines: defaults, grammar/quoting, mask hex semantics `-1→0xFFFF`, status taxonomy, compatibility payloads), `pending_cog_import_tests.cpp`, `level_cog_reference_tests.cpp` | `src/core/tests/assets/`, `src/core/tests/editor/`, `src/core/tests/level/` |

Divergences from Coda to close (not bugs per se — different contracts):

1. **Failure shape.** Coda keeps per-line diagnostics and `IsComplete`;
   the port collapses to one `malformed` string and, for invalid
   values/declarations, still returns symbols. Typed authoring must gate on
   a completeness predicate equivalent to Coda's `IsComplete` (§3.3), not
   on `status == loaded` alone.
2. **Modifier retention.** The port parses `desc/mask/linkid/nolink` into
   `description`/`mask` only; `linkid`/`nolink` have no stored effective
   form, so event-link candidates cannot be reported. §3.3 adds them.
3. **Surface pair form.** The port accepts legacy surface pair text
   (`"1 2"`) as a surface value; Coda's binding model treats surface as a
   single global index and refuses pair text as untyped. The slice keeps
   the port's JED-pair read compatibility at **import** but the binding
   layer (§3.4) only authors single global indices.
4. **Defaults on import.** The port fills missing trailing JKL values from
   script defaults during hydration; Coda's `bindings`/`PrepareAdd` never
   infer values for **authoring**. Both can coexist: hydration is an import
   read path, authoring (§3.5) never infers. The spec keeps that line
   explicit so neither behaviour leaks into the other.

### 2.2 App (`src/app`)

| Piece | Behaviour today | Evidence |
| --- | --- | --- |
| Script editor | Multi-tab `.cog` editing, formatting, completion, outline/symbol navigation, go-to-definition, mask/flags popups with stale-buffer recheck, watcher-backed reload | `src/app/src/ui/cog/*`, `src/app/src/main_window/cog/editor.cpp`, `scripts.cpp` |
| Structural diagnostics | `buildCogScriptDiagnostics(text, knownMessages, projectType)` → line/column/length spans: sections, flags form/range/duplicate, statement termination + continuation, braces/brackets/strings, duplicate symbols, label-vs-declaration checks (non-message labels expect an `int/flex/float` declaration; missing backing = warning), redundant symbol-line semicolons (warning), message words checked against per-project lists | `src/app/src/ui/cog/cog_script_diagnostics_utils.cpp` (2 934 lines), contract in `docs/algorithms.md` § "COG diagnostics and source marking" |
| Message vocabulary | Per-project message word lists (`JKDF2` 40 words, `MotS` = JKDF2 + `preblock/escaped/attachkilled/playeraction`, `IJIM` separate list), `global0..15`, keywords/types/attributes for completion/highlighting | `src/app/src/ui/cog/cog_script_language_utils.cpp` (`jkdf2MessageWords`, `motsMessageWords`, `ijimMessageWords`, `cogScriptProjectMessageWords`) |
| Level checks | `cogParameterExtension` (typed resource params carry `.ai/.key/.mat/.wav/.3do`), `cogScriptResource` (every placed COG names an available script) | `src/app/src/ui/checks/level_check_utils.cpp` (`appendCogChecks`, resource checks) |
| Placed-COG creation | `createPlacedCogFromScript` loads symbols-with-defaults and creates the COG with those values; **no** named-binding completeness gate, no existing-COG closure check | `src/app/src/main_window/cog/scripts.cpp` |

### 2.3 Gaps this slice closes

| # | Gap | Closed by |
| --- | --- | --- |
| G-1 | No link/modifier model → no reference-vs-event-link reporting | §3.3, §3.4 |
| G-2 | No typed binding description (status per row, count match, target resolution) for placed COGs | §3.4 |
| G-3 | Placed-COG create/rebind without completeness, count, closure, or null-canonicalisation guards | §3.5 |
| G-4 | Thing append without placed-COG reference guard | §3.5.3 |
| G-5 | No verb availability check at all; message words are editor lists, not measured registries | §3.6, §3.7 |
| G-6 | No consistency-checker COG reference pass (counts/indices/static/runtime warnings) | §3.8 |
| G-7 | Core has no hash-bound script identity for guarded edits (app resolves via `ResourceLocator` without digest capture) | §3.5.4 |

Out of scope for this slice: CLI/MCP surfaces (absent in the rewrite,
[REWRITE_MAP.md](REWRITE_MAP.md) §8). The slice is core-first so a future
CLI can expose the same contracts; it does not design that CLI.

---

## 3. Target behaviour (normative)

Keywords **must/never** below are acceptance requirements; **should** is a
strong default that a deviation must justify in review.

### 3.1 Principles

1. **No invented verbs.** A spelling is available only if it occurs in the
   measured registry for the selected target (§3.6). Editor word lists,
   completion lists, Coda templates, and enhanced-engine source are never
   availability evidence.
2. **JK and MotS registries stay distinct.** No merged "classic" table.
   Cross-target presence is reported as data (`present_in_other_retail_table`),
   never as availability.
3. **Lexical ≠ compiler.** Preflight output must carry the
   not-a-compiler qualifier wherever findings are shown, exported, or
   logged (mirroring `inspect_cog.py` notes).
4. **No execution, no rewriting of scripts.** The slice reads script text
   to derive schemas; it never executes COG, never edits `.cog` source as
   part of a level edit, and never infers gameplay from a correct binding.
   (Coda: `docs/AGENT_PROTOCOL.md` § "Placed COG and movement-path
   authoring".)
5. **Unknown stays unknown.** Missing/ambiguous/incomplete schemas leave
   raw values untyped. Nothing guesses a reference from an integer.
6. **Refuse, don't repair.** Guarded mutations fail with a stable code and
   no partial mutation, mirroring Coda's `CogBindingException` codes (§3.5).
7. **Synthetic fixtures only.** Tests use generated text/GOB bytes; no
   retail script, archive, or level is a test input.

### 3.2 Module placement

New portable logic lives in `sed_core` (no Qt), so GUI and tests share it:

| Proposed file | Contents |
| --- | --- |
| `src/core/include/sed/assets/cog_schema.hpp` + `src/core/src/assets/cog_schema.cpp` | Full symbols-schema parse (§3.3): `CogSchema`, `CogSymbol`, `CogSchemaDiagnostic`, completeness |
| `src/core/include/sed/level/cog_bindings.hpp` + `src/core/src/level/cog_bindings.cpp` | Binding description + guarded prepare operations (§3.4–§3.5), COG index rules |
| `src/core/include/sed/assets/cog_preflight.hpp` + `src/core/src/assets/cog_preflight.cpp` + generated registry tables | Lexical preflight (§3.6–§3.7) with embedded measured registries |

Existing `cog_script.cpp` stays the compatibility loader for import
hydration (§2.1); the schema parser is the authoring-grade reader. Where
they disagree on a declaration, the schema parser's diagnostic wins for
authoring and the loader's payload is import-only. `CogValue` gains
`linkId`-family fields additively (defaults preserve current files):
`explicitLinkId` (`std::optional<std::int32_t>`), `explicitMask`
(`std::optional<std::uint32_t>`), `noLink` (`bool`). Effective values are
computed, not stored: `effectiveLinkId = noLink ? -1 : explicitLinkId.value_or(0)`,
`effectiveMask = explicitMask.value_or(0x401)`.

### 3.3 Symbols-schema parser contract

Port of §1.1 behaviour, stated as C++ requirements:

- Input: script name + captured text (already read; the parser does no I/O).
  Output: `CogSchema { name, sourceText, symbols, rawDeclarations,
  hasSymbolsBlock, hasSymbolsEnd, diagnostics, isComplete(), levelValues() }`.
- Each `CogSymbol` retains: type (15 kinds incl. `message`), name, default
  text, `local`, description (explicit `desc=` wins over trailing comment),
  raw declaration, line number, raw modifiers in order, `noLink`,
  `explicitLinkId`, `explicitMask`.
- Tokenisation is quote-aware for `'`/`"`; `#`/`//` comments end the code
  portion of a line outside quotes; comma-joined modifiers split outside
  quotes. Unterminated quote → `unsupportedDeclaration`.
- Diagnostics use **Coda's code strings verbatim** (§1.1 table) so port and
  Coda findings are comparable, plus binding-layer codes in §3.4. Each
  diagnostic keeps its raw declaration.
- `isComplete()` ⇔ block present **and** terminated **and**
  `diagnostics.empty()`. `levelValues()` = non-local, non-message symbols
  in declaration order.
- The parser must not throw on malformed input; the 16 MiB editor source
  cap (`cog/editor.cpp` `kMaximumCogScriptSourceBytes`) remains an app-side
  read bound, and the core parser additionally refuses inputs above a
  documented schema-inspection budget (Coda uses 1 MiB for agent reads;
  core adopts 1 MiB for schema inspection and reports it as a diagnostic,
  not an exception).

### 3.4 Binding description (read-only)

`describeBindings(level, cogIndex, schema | null)` →
`CogBindingDescription` mirroring §1.2, including:

- `scriptStatus` (`missing/nameMismatch/complete/incomplete`),
  `schemaComplete`, `actualParameterCount`, `expectedParameterCount`
  (null unless complete), `parameterCountMatches` (null unless complete),
  schema diagnostics + `missingScript` / `scriptNameMismatch` /
  `parameterCountMismatch`.
- Rows to `max(actual, expected)` with symbol, raw value, target, and
  `eventLinkCandidate`. Target resolution follows §1.2 exactly, including:
  `TryIndex` completeness, placed-count bounds ignoring declared capacity,
  surface resolution through serialized global order → (sector, local)
  with ambiguous-identity → `Unknown`, and COG `0x8000`/placed/unplaced
  classification.
- The row UI/report wording must keep reference status and event-link
  candidacy as separate fields and carry the static-candidate disclaimer.
  `EffectiveLinkId/EffectiveMask` are labelled as the OpenJKDF2
  reference-symbol interpretation (Coda §1.1 wording), not as retail proof.
- Imported-document surface identity: the description takes the level plus
  its retained-source surface map when present (port equivalent:
  `Level::importedJklSurfaceRefs` while pending, else current serialized
  order). When identity is ambiguous, surface rows are `Unknown` and §3.5
  refuses — the port's existing pending-import throw
  (`finalizeJklImportCogValues`) remains the import-time form of the same
  rule.

### 3.5 Guarded mutations

All mutations are **prepare-then-apply**: a `prepare*` function validates
against the current level + captured schema and returns either a stable
error code or an apply step that the document/undo layer executes. Prepare
never mutates. Error codes reuse Coda's strings (§1.2, §1.7).

#### 3.5.1 Create placed COG (`prepareAddCog`)

- Name: ASCII `.cog` basename ≤ 31 chars for **new** names →
  `invalidCogScriptName`. (Imported longer names remain readable.)
- Schema non-null, complete, name-matching → `incompleteCogSchema`.
- Bindings: every `levelValues()` symbol exactly once by name with
  matching kind; ≤ 256 → `duplicateCogBinding`,
  `cogParameterCountMismatch`, `cogSymbolMismatch`, `cogBindingBudget`.
  **No defaults inferred**, including for trailing symbols.
- Kinds: only `thing/sector/surface/cog`; others →
  `unsupportedCogBindingType` (use script-local constants).
- Targets: `< -1` refused; `-1` = null; resolved must be `Level`/`Null`
  → `invalidCogTarget`; surface identity ambiguous →
  `unresolvedSurfaceIdentity`.
- Closure: re-run the existing-COG inspection on **every** call (no cache
  across calls): each existing placed COG needs a matching complete schema
  with exact count (`incompleteExistingCogSchema`,
  `existingCogParameterCountMismatch`); budgets 4 096 COGs / 100 000
  params (`cogClosureBudget`); `cog`-typed values whose classification
  would change → `existingCogReferenceCapture`; values staying
  `UnplacedUnchecked` → `existingCogRuntimeReference`. Refusal text must
  state the value is not assumed invalid.
- Serialisation order is declaration order, independent of request order.
  The created COG is one undo step (`CreateCogCommand` semantics: undo
  restores numbering exactly).

#### 3.5.2 Rebind one reference (`prepareRebindCogReference`)

Mirrors §1.2 `PrepareRebind`: complete schema, exact count (never pad/
discard), in-range parameter, expected name+type match, supported kind,
existing static/runtime COG values refused (`unsupportedCogReference`),
`-1`-only null authoring, surface identity required even when nulling.
Success changes exactly one positional value; every other raw value —
including non-canonical spellings — is preserved. One undo step.

Positional (untyped) editing remains available in the GUI for unresolved
layouts, as in Coda's editor, but it is a distinct mode labelled as such;
it never relabels raw values with guessed types. The low-level pad-with-`0`
semantics of `SetCogValueCommand` apply only there, never in typed paths.

#### 3.5.3 Thing-append guard (`validateThingAppend`)

Mirrors §1.7: before any thing append (single or array), all placed COGs
need complete schemas + exact counts; external `thing` values must be
canonical `-1` or currently placed (`invalidExistingThingReference`,
`existingThingRuntimeReference`). Scalars and script-local constants are
not references. Budgets checked before resolving beyond them. Revalidate
per append. The guard analyses positional symbols only — not code, not
computed indices, not template-attached scripts — and its refusal text
says so.

#### 3.5.4 Script identity

Guarded operations take the script's captured bytes + SHA-256 (the port's
`ResourceLocator` read plus digest), not a path re-read at apply time.
If the resolved bytes differ from the reviewed digest at prepare or at
apply, the operation refuses as a conflict with no output — the Coda
plan/commit rule (§1.5) expressed in core terms. Archive ambiguity
(≠1 match, higher-priority ambiguity blocking fallthrough, `cog/` folder
preference within an archive) follows `CogScriptLibrary` (§1.4); the port
implements the same precedence over `ResourceLocator` candidates and
caches per-resolution-call only.

### 3.6 Lexical preflight — verb/message availability (new)

Reference implementation: `scripts/inspect_cog.py`
(`lexical-preflight-v3-not-a-compiler`). The port reimplements its
**method** in C++; it does not embed Python.

#### 3.6.1 Measured registries (provenance)

| Registry | File | Binary SHA-256 | Entries |
| --- | --- | --- | --- |
| JK 1.01 | `jk101-retail-registry.json` | `4509cd690634ba20e132bd16e713f91e144b17e2beaaad5e678f1ddb189b51d9` | 492 = 451 verbs + 41 messages |
| MotS | `mots-retail-registry.json` | `23cd064217f904d36d896f52d3cdd17635bb85441a12e0415c0637504191d974` | 585 = 538 verbs + 47 messages |

Generation rule: the C++ tables are generated from these two JSON files by
a checked-in script, with the binary hashes recorded in the generated
header. Hand-editing a table without regenerating from measured data is a
review blocker. Names are stored lower-case; lookup is case-insensitive
(the generated lexer folds ASCII case — `language-and-execution.md`
§ "Grammar and expression behavior").

Measured differences the tables must preserve (spot checks for §4):

- `getcurinvweapon`: JK only. `getcurinvweapon2`, `getweaponbin`,
  `fireprojectiledata`: MotS only. (Original MotS `GetCurInvWeapon2`
  returns the raw bin without ordinal conversion — availability is
  spelling-level; semantics stay in `verb-families.md` §11 and are not
  preflight's business.)
- MotS-only examples: `enterbubble/exitbubble` messages,
  `jksyncforcepowers`, `jkbegincutscene`, `findsectoratpos`,
  `issphereinsector`, `sendmessageexradius`, `getthingguid` (94
  MotS-only spellings total; exactly 1 JK-only spelling).
- Both: `settimer`, `settimerex`, `heapnew`, `haslos`, `jkgetlocalplayer`.

#### 3.6.2 Preflight algorithm

Input: script text + explicit target (`jk` | `mots`). No default target:
the caller passes the project kind (`ProjectType::JKDF2 → jk`,
`ProjectType::MOTS → mots`); `IJIM` has no measured registry in this slice
and preflight reports `target-unmeasured` rather than substituting a list.

1. Scrub comments/strings for call discovery while preserving line numbers
   (the helper's `scrub` contract; cog lexer has `#`/`//` comments and no
   `/* */` rule — block-comment markers produce a warning, contents are
   not silently removed).
2. Split header/code at the `code` line; header lines reset quote handling
   per line (an apostrophe in `desc=where's` must not open a vector
   literal spanning the code section).
3. Calls = identifiers followed by `(` in code, excluding
   `if/while/for/switch/return/call`. Each call spelling is checked
   against the target verb set. Findings: `calls_absent_from_target_registry`
   with line numbers and `present_in_other_retail_table` flag.
4. Declarations = `symbols` block lines; unknown declaration types are
   inventoried separately (`unknown_declarations`), never silently dropped.
5. Messages: declared `message` names (resolving `message alias=registered`
   aliases) checked against the target message set →
   `messages_absent_from_target_registry`. Names binding a measured global
   float (`global0..15`, from `retail-global-floats.json` for the target)
   are reported as nonstandard bindings, not as unknown messages: generic
   symbol lookup can accept them, but *"normal event delivery is not
   established"* (`language-and-execution.md` § "Message context").
6. Output record (JSON-equivalent struct): file identity + SHA-256, target,
   `method: lexical-preflight-v3-not-a-compiler`, retail binary SHA-256,
   `flags=` value, `has_symbols`, `has_code`, call inventory,
   absent-call/message findings, declarations, literal resource-reference
   heuristic list, warnings, and the fixed notes (availability ≠ argument/
   type/runtime check; message aliases vs delivery separate; resource
   spellings are a heuristic; no checksum prediction).
7. Strict mode: any availability finding is a failure result (the helper's
   `--strict` exit 2). Non-strict mode reports findings as warnings.
   Availability findings never block §3.5 operations by themselves; they
   are diagnostics the author must acknowledge (see §3.7.3 for the one
   hard gate).

Signatures are **not** checked by preflight. Where argument/return
contracts matter, reviewers use `verb-families.md` (15 families, COG call
order, void-vs-value and invalid-input push behaviour) and the complete
engine implementation; the index's lexical pop list is a locator, not a
signature. Authoring guidance carried into docs/help text: no short-circuit
guards (`&&`/`||` evaluate both sides), `!=`/`&&`/`||`/`!` use integer-pop
paths (prefer explicit `> 0.0` / tolerance comparisons), missing arguments
can consume stack data, void verbs push nothing — all from
`language-and-execution.md`, labelled as pinned-source observations, not
retail execution results.

#### 3.6.3 Enhanced-engine, cogext, and dynamic-POV spellings — not retail

- Verbs present only in OpenJKDF2/OpenMotS source (e.g. fork QOL camera
  index 7 path, `GetCurInvWeaponMots` wrapper behaviour) are **enhanced**
  findings: if a future table records them, they live in a separate
  `enhanced` set tagged with the exact engine revision, and preflight
  reports them as *"requires enhanced engine <revision>"*, never as retail
  availability. This slice ships no enhanced table; an unknown spelling
  that an author believes is enhanced must be added only with pinned-source
  evidence (file/line + revision), per `verb-families.md` provenance rule.
- **cogext** (Quib Mask, rev029 study): 168 active registrations, none in
  JK 1.01, only `GetCogFlags/SetCogFlags/ClearCogFlags` overlapping MotS —
  and name overlap does not prove equivalence
  (`cogext-source-contracts.md`). These names stay **out** of both retail
  tables; a script needing them declares a cogext dependency outside this
  slice.
- **Dynamic POV** (`DYNAMIC_POV`, LadyEebs/OpenJKDF2-Experimental
  `d4c5145…`): `jkSetWaggleEx`, `jkSetIdleWaggle`, `jkGetMuzzleOffset`,
  `jkSetPOVAutoAim`, `jkSetPOVSprite`, `jkSetPOVSpriteScale`,
  `SetPOVWaggle` — absent from both measured registries by measurement
  (`dynamic-pov-extensions.md`). Same treatment: not retail contracts,
  never added to `jk`/`mots` tables.

### 3.7 Message/symbol binding checks

Lexical/structural checks over the parsed schema + scrubbed code. None of
these is a compiler check; each is labelled with its level.

1. **Declared vs handled.** Every `message` declaration (alias resolved)
   has a corresponding code label, and every message-named label has a
   declaration or is a registered message constant used as a handler.
   Missing and duplicate labels are checked **separately**: the code
   generator overwrites duplicate label locations, so duplicates are a
   dispatch-contract warning even though the parser accepts them
   (`language-and-execution.md` § "Message context"). Plain subroutine
   labels need no message declaration; a sender-side constant needs no
   receiving label in the sender.
2. **Alias validity.** `message onStart=startup`: right-hand side checked
   against the target registry, left-hand side against code labels. A
   valid alias is never rejected merely because the label name is not a
   global message.
3. **Global-float binding.** `message global0` (etc.) is a nonstandard
   binding finding (§3.6.2 step 5), distinct from unknown-message.
4. **Symbol hygiene.** Duplicate symbol names (case-insensitive) are
   schema diagnostics (§3.3). Undeclared identifiers in code are **not**
   lexical errors — the loader auto-creates a float-zero local — but
   preflight inventories identifiers that are neither declared, nor
   labels, nor registry verbs, as a typo/integration review warning
   (explicit declarations make behaviour reviewable;
   `language-and-execution.md` § "Grammar and expression behavior").
5. **Reference-symbol modifiers.** `linkid/nolink/mask` are validated as
   in §3.3; mask text is hexadecimal by engine `scanf("%x")` semantics
   (bare `10` means `0x10`; `-1` is the all-types sentinel). Event-link
   candidacy stays a §3.4 row property; preflight does not upgrade it to
   a subscription claim.
6. **Hard gate (only one).** §3.5 operations require §3.3 completeness.
   Message/verb findings do not block them, because Coda's guarded edits
   are schema operations on scripts whose code the editor does not judge.
   The GUI should surface preflight findings alongside, so the author
   sees both before writing.

### 3.8 Consistency-checker COG pass

Core, read-only, mirroring §1.3 (`ConsistencyChecker.CheckCogs`):

- Without a resolvable complete schema + exact count: one warning per
  placed COG, *"COG script schema unavailable; positional references were
  not checked"* — raw values untyped.
- With schema: per typed parameter — non-decimal → error; `cog`
  static/unplaced → warning (runtime bounds unverified); `thing/sector/
  surface` out of placed range → error. Issues carry COG index and
  parameter index.
- This pass joins the port's existing checks (`cogParameterExtension`,
  `cogScriptResource` in `level_check_utils.cpp`) without changing their
  wording; the three checks answer different questions (extension hygiene,
  script presence, reference validity).

### 3.9 GUI behaviour (app)

- The existing script editor keeps structural diagnostics
  (`buildCogScriptDiagnostics`) and gains a preflight findings view fed by
  §3.6 for the tab's project target; findings show line spans and the
  not-a-compiler qualifier. The editor's current per-project message word
  lists remain **completion/highlighting** data only; availability wording
  must cite the measured registry, and any list/registry disagreement is
  resolved in favour of the registry (with the editor list fixed or the
  difference documented as IJIM-only vocabulary, which has no registry in
  this slice).
- Placed-COG editing (item editor / COG panel) uses §3.4 descriptions:
  typed rows when complete+exact, explicit positional fallback otherwise
  (Coda §1.6 wording), event-link candidacy shown with its disclaimer.
  Create/rebind go through §3.5 prepare steps; refusals show the stable
  code + message and change nothing.
- Keywords with lexer tokens but no grammar production (`break`,
  `continue` have lexer rules but no statement production; `switch` is not
  a captured keyword) must not be presented by completion/templates as
  working statements (`language-and-execution.md`). Existing completion
  lists containing them are corrected as part of this slice's GUI work.

### 3.10 Explicit non-goals and boundaries

- No COG compiler, VM, execution, or "Test in engine" claim. No bytecode,
  no checksum prediction (source edits can change bytecode layout —
  preflight says so).
- No retail-parity or gameplay claim from bindings, preflight, or
  consistency passes. PLAY-01's runtime acceptance
  (`docs/PLAYABLE_AUTHORING.md` § "Runtime contract", § "Acceptance to
  run") is a separate engine harness, not part of this slice.
- No cogext or dynamic-POV support, tables, or dependencies (§3.6.3).
- No script-source editing as part of level edits; no `cogstrings.uni`
  generation (Coda's master-COG generator explicitly leaves goal strings
  to the author, §1.6 — the port does the same if it ports generation at
  all; generation itself is **not** in this slice).
- No implicit game-directory discovery: scripts resolve only from the
  document's explicit resource roots/archives (Coda §1.4/§1.5 rule).
- IJIM: schema/binding behaviour applies (types already include IJIM
  spellings in the loader), but lexical preflight has no measured IJIM
  registry and must say `target-unmeasured` (§3.6.2).

---

## 4. Acceptance tests

All fixtures synthetic/original, generated in-test (text strings, temp
directories, in-memory GOB bytes via the port's archive writer where
needed). Each port test names the Coda fixture it derives from. Suites run
under `sed_core_tests` (core) and the existing app test executable (GUI);
no retail asset, game install, or network is required.

### 4.1 Schema parser (derive: `CogSchemaTests.cs`, `CogScriptTests.cs`)

| ID | Case | Expected |
| --- | --- | --- |
| A-01 | Door-shaped script (tabs, defaults, locals, `desc=`, comments) | Types/defaults/locals exact; `levelValues` = non-local non-message in order; comment→description; code-section `startup:` not a symbol |
| A-02 | Modifier retention line `thing door=-1 nolink,linkid=7 mask=0xFFFFFFFF desc=door` | Raw declaration/modifiers verbatim; `noLink` true yet effective link 7 (later `linkid` wins); explicit mask `0xFFFFFFFF` |
| A-03 | Defaults distinct: bare `thing a` vs `linkid=3 nolink mask=0` | Explicit nulls vs effective `0 / 0x401` distinguished both ways |
| A-04 | Diagnostic table (15 layouts from `CogSchemaTests.UnsupportedOrIncompleteLayout…`) | Each yields `!isComplete` + the exact code: `missingSymbolsBlock`, `missingSymbolsEnd`, `unsupportedDeclaration` ×5, `unknownDeclaration`, `unknownModifier` ×2, `invalidModifier` ×3, `duplicateSymbol`, `multipleSymbolBlocks` |
| A-05 | Quoted defaults/comments (`'1 2 3'`, `"a,# // b"`) | Positional schema unshifted; local vector excluded from `levelValues` |
| A-06 | Unknown declaration (`futuretype b`) | Symbol absent, raw declaration retained, diagnostic carries it, `!isComplete` |

### 4.2 Resolution (derive: `CogSchemaResolutionTests.cs`)

| ID | Case | Expected |
| --- | --- | --- |
| A-07 | Duplicate entries in one archive; ambiguous high-priority + valid low; `cog/` vs other folder; basename fallback; first-archive priority | Ambiguous → unresolved (cached); no fallthrough past ambiguity; `cog/` wins in-archive; unique basename resolves; missing high may fall through |
| A-08 | Incomplete layouts (unknown type / no `end` / duplicate) with count-matched values `999` | Resolver returns no typed parameters; consistency pass emits the single schema-unavailable warning |
| A-09 | Complete schema, counts 0 and 2 vs expected 1 | No typed parameters either way |
| A-10 | Complete + exact (`thing/sector/surface/cog/int` + local + message) | Kinds retained in order; locals/messages excluded |

### 4.3 Bindings (derive: `CogBindingsTests.cs`, `AgentCogBindingTests.cs`)

| ID | Case | Expected |
| --- | --- | --- |
| A-11 | Reference/event-link separation script (`nolink`, `linkid=7`, surface, `cog`, `-1`, `mask=0`, `int`) | Candidates `[false,true,true,false,false,false,false]`; surface resolves to (sector 1, surface 1); `-1` → `Null`; `int` → `NotReference` |
| A-12 | COG index table `-1,-8,1,2,32768,1tail,2147483648,1.0` (2 placed COGs) | `Null, Null, Level, UnplacedUnchecked, StaticUnchecked, Invalid, Invalid, Invalid`; raw values preserved |
| A-13 | Index == placed count with declared capacity 1000 (thing/sector/surface) | `Invalid` — capacity is not headroom |
| A-14 | Consistency pass: schema unavailable vs typed out-of-range vs static/unplaced COG | Warning / error / warning taxonomy of §3.8 with COG+parameter indices |
| A-15 | Missing/ambiguous/name-mismatched schema | `!schemaComplete`, expected/count-match null, row `Unknown`, rebind refuses `incompleteCogSchema`, value untouched |
| A-16 | Count mismatch short (1 vs 2) and long (3 vs 2) | Short: extra row `Missing`; long: extra row `Unknown`; rebind refuses `cogParameterCountMismatch`; no pad/discard, counts unchanged |
| A-17 | Prepared rebind undo/redo with neighbours `" +000 "`, `" 1.2500"`, `"texture.mat"` | Only target changes to canonical `"1"`; undo restores all spellings; redo reapplies |
| A-18 | Four kinds rebind + clear to `-1`; wrong name/type, out-of-range, targets `-2/2/INT_MAX` | Success per kind; refusals `cogSymbolMismatch` / `cogParameterOutOfRange` / `invalidCogTarget`, no mutation |
| A-19 | Existing static/runtime COG values (`2`, `32768`) rebind attempt; new static/runtime targets | `unsupportedCogReference` / `invalidCogTarget`; value unchanged |
| A-20 | Non-reference kind (`int`) rebind attempt | `unsupportedCogBindingType` |
| A-21 | Noncanonical two-sector JKL (global order ≠ sector order): surface describe/rebind/save/reopen | Global index resolves to correct (sector, local); after rebind + save + reopen the sector/local address is the new one; undo restores `"0"` |
| A-22 | Retained identity broken (source line edited or sector order reversed) | Describe → `Unknown`; rebind + surface-target enumeration refuse `unresolvedSurfaceIdentity`; value unchanged |
| A-23 | Agent-shaped flow (temp GOB + JKL): paged discovery | Page binds digest/modifiers, writes nothing (file bytes + dir listing unchanged); plan/commit replays digest and preserves `1.25`; script changed before/after plan refuses with no output file; incomplete → `Unknown` rows + refused mutation; missing script → raw values untyped; bad `cog/offset/limit` refuse |

### 4.4 Append guards (derive: `CogThingAppendGuardTests.cs`, `CogBindings.PrepareAdd`)

| ID | Case | Expected |
| --- | --- | --- |
| A-24 | `prepareAddCog`: fully local script, empty bindings; 4-kind bindings out of request order | Created values in declaration order; one undo step |
| A-25 | `prepareAddCog` refusals: bad name (>31, non-ASCII, separators), incomplete/mismatched schema, duplicate/missing/extra binding, wrong kind, unsupported kind, target `< -1`, target out of range | Exact codes of §3.5.1; level unchanged |
| A-26 | Append closure: existing COG with unchecked COG value that append would capture; value staying unplaced; missing/incomplete existing schema; count mismatch | `existingCogReferenceCapture` / `existingCogRuntimeReference` / `incompleteExistingCogSchema` / `existingCogParameterCountMismatch` |
| A-27 | Thing-append guard: placed/`-1` pass; `1/999/32768` with 1 placed thing + large capacity; `-2/-01/1.0/0.5/0x0/1suffix/overflow/empty` | Pass without mutation; `existingThingRuntimeReference`; `invalidExistingThingReference` respectively |
| A-28 | Guard scope: scalar `32768`, local `thing computed=999` | Pass — not guessed as references |
| A-29 | Budgets: exactly 4 096 COGs / 100 000 params pass; +1 refuses before resolving further | `thingAppendCogBudget` / `cogClosureBudget` at the boundary |
| A-30 | Revalidation: schema changes between calls; thing count shrinks between calls | Second call re-derives and refuses; no stale cache |

### 4.5 Lexical preflight (new; method from `inspect_cog.py`)

Fixtures are original scripts written for these tests.

| ID | Case | Expected |
| --- | --- | --- |
| A-31 | Script calling `GetCurInvWeapon` preflighted as `jk` vs `mots` | Available for `jk`; absent-finding for `mots` with `present_in_other_retail_table = true`; mirror for `GetCurInvWeapon2`, `GetWeaponBin`, `FireProjectileData` |
| A-32 | MotS-only spellings (`jkSyncForcePowers`, `FindSectorAtPos`, `SendMessageExRadius`, `enterbubble` message) as `jk` | Absent-findings; as `mots`: clean |
| A-33 | Invented spelling (e.g. `TeleportPlayerHome`) both targets | Absent-finding both; strict mode fails; non-strict warns; output carries not-a-compiler notes + correct binary SHA-256 per target |
| A-34 | cogext/POV spellings (`HeapLoad`, `jkSetPOVSprite`) both targets | Absent-findings both — never silently available (§3.6.3) |
| A-35 | Message handling: declared `startup` handled; `message onStart=startup` alias; `message global0`; undeclared handler label; duplicate `damaged:` labels | Alias valid; `global0` = nonstandard-binding finding (not unknown); missing/duplicate reported separately per §3.7 |
| A-36 | Header apostrophe (`desc=where's`) + code with `/* */` markers | No phantom vector/string span; block-comment warning emitted; line numbers in findings match source |
| A-37 | `IJIM` project preflight | `target-unmeasured` result, no substituted table |

### 4.6 Port regressions kept

Existing `cog_script_tests.cpp`, `pending_cog_import_tests.cpp`, and
`level_cog_reference_tests.cpp` must keep passing unchanged in behaviour
(import compatibility of §2.1, including vector-initialiser and pair-text
read paths). New tests live beside them:
`src/core/tests/assets/cog_schema_tests.cpp`,
`src/core/tests/level/cog_binding_tests.cpp`,
`src/core/tests/assets/cog_preflight_tests.cpp`, registered in
`src/core/CMakeLists.txt` like the other core test files.

---

## 5. Implementation order

1. **Schema parser** (§3.3) + A-01…A-06. Pure core; unblocks everything.
2. **Registry generation + preflight** (§3.6–§3.7) + A-31…A-37.
   Independent of bindings; gives the editor honest verb diagnostics early.
3. **Binding description** (§3.4) + A-11…A-22 (read-only; no mutation risk).
4. **Consistency pass** (§3.8) + A-14, wired beside existing checks.
5. **Guarded mutations** (§3.5) + A-23…A-30, behind document undo.
6. **GUI wiring** (§3.9): preflight view, typed placed-COG rows with
   positional fallback, prepare-step refusals. Completion-list keyword
   correction (`break`/`continue`/`switch`) lands here.

Each step lands with its tests; a step is not done while any of its
A-cases fail or any §4.6 regression suite is red.

## 6. Risks and open questions

- **Registry drift:** the JSON files are the only measured source. If they
  are re-measured, regenerate tables and update §3.6.1 hashes in the same
  change; never patch a single name by hand.
- **Editor word lists vs registry:** the app message lists include
  OpenJKDF2-source vocabulary. Expect disagreements; §3.9 assigns the
  registry precedence for availability claims, but completion may keep a
  wider vocabulary if labelled as such.
- **Surface identity after hydration:** once pending refs clear, serialized
  order is authoritative. The binding layer must use the same order the
  JKL writer emits, or A-21/A-22 diverge silently. Derive it from the
  writer, do not re-sort.
- **MotS weapon-bin semantics** (`GetCurInvWeapon2` raw-bin behaviour,
  ordinal↔bin conversion) are runtime contracts documented in
  `verb-families.md` §11 with binary evidence; preflight deliberately does
  not encode them. A future signature-checking slice needs its own spec.
