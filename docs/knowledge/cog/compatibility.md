# Target compatibility: JK 1.01, MotS and extensions

> Copied into RED from [CodaLilitari/jk-mots-cog-workshop@b4b43f6](https://github.com/CodaLilitari/jk-mots-cog-workshop/blob/b4b43f6ab1976f673598c90751ec6160da70c506/docs/COMPATIBILITY.md) (MIT; see [LICENSE](LICENSE.md)). Regenerate with `tools/knowledge/import_knowledge.py` rather than editing this copy.

The [flag reference](flags.md) and its machine catalog keep original JK, original MotS, pinned enhanced source, current source and Indy reconstruction separate. A native accepting a 32-bit mask is not proof that every bit has the same effect in both games. New original-binary evidence covers Get/Set/ClearThingFlags and a bounded synchronization path, not every per-bit meaning or remote outcome.

This is an authoring and evidence matrix, not a blanket certification that every registered verb works in every situation. **JK** below means the inspected original JK 1.01 executable; **MotS** means the inspected original Mysteries of the Sith executable. Unless explicitly labeled as separately hashed current-local evidence or another pinned extension revision in the [target-contract study](notes/target-contracts-and-lifecycle.md), enhanced-engine behavior is tied to source commit `b1be98bd4f170b5384cb71b15443a274aac6a937`, inspected on 30 September 2026. Other revisions, unofficial patches and extension versions need their own checks.

## Evidence labels

| Label | What it establishes | What it does not establish |
| --- | --- | --- |
| Original registration | Native/event spelling is recorded in the identified original executable | Correct arguments, delivery, successful behavior in a live game |
| Original static analysis | Selected machine-code paths and constants were inspected | Whole-game execution or a network result |
| Pinned source | The specified source implements this behavior under its stated mode/configuration | Every original executable implements it identically |
| Bounded probe | Listed cases executed in a stated adapter or source harness | Untested inputs, full loading, a retail VM or connected clients |
| Runtime test | The recorded target actually executed the stated steps | Other builds, packages, paths or peers |

The initial teaching examples have lexical and code-section parser checks. They have **no retail runtime or multiplayer test result**. See [EXAMPLE_VALIDATION.md](https://github.com/CodaLilitari/jk-mots-cog-workshop/blob/b4b43f6ab1976f673598c90751ec6160da70c506/docs/EXAMPLE_VALIDATION.md).

## Measured original targets

| Property | JK 1.01 | MotS |
| --- | --- | --- |
| Inspected executable size | 1,388,544 bytes | 1,759,744 bytes |
| SHA-256 | `4509cd690634ba20e132bd16e713f91e144b17e2beaaad5e678f1ddb189b51d9` | `23cd064217f904d36d896f52d3cdd17635bb85441a12e0415c0637504191d974` |
| Distinct native verb spellings | 451 | 538 |
| Integer message spellings | 41 | 47 |
| Shared native spellings | 450 | 450 |
| Native spellings unique against the other table | 1 | 88 |

Counts include message aliases: `activate` and `activated` are two spellings for the same ID. Separately, both binaries register `global0` through `global15` as float symbols. Those are not sixteen additional ordinary events. Generic symbol binding and meaningful event delivery are separate questions.

These hashes identify the inspected versions without distributing the executables. The machine workflow's bundled registration tables contain the measured names. An absent entry is useful compatibility evidence; it does not by itself explain every possible parse or binding outcome.

## Calls and events that change a port

| Feature | Stock JK 1.01 | Stock MotS | Guidance and evidence |
| --- | --- | --- | --- |
| `GetCurInvWeapon` | Registered | Absent | Original registration. Some enhanced builds restore the name; do not use that as retail evidence. |
| `GetCurInvWeapon2` | Absent | Registered; inspected original getter returns a raw bin | Original binary registration/body evidence. The pinned enhanced binding instead converts to an ordinal in MotS mode. Do not convert the original getter result twice. |
| `HasLOS` | Registered | Registered | Validate the handles and explicit success result; the inspected enhanced invalid-handle path returns truthy -1. |
| `FindSectorAtPos` / `IsSphereInSector` | Absent | Registered | Original name availability; sector containment is distinct from visibility and collision. |
| `GetWeaponBin` | Absent | Registered | Original registration; pinned source maps selection order through weapon definitions. |
| `jkSyncForcePowers` | Absent | Registered | A JK-labeled community package can contain a MotS-only native call; package category is not a compatibility test. |
| `activate` / `activated` | Both message ID 1 | Both message ID 1 | Valid aliases in both measured tables. Do not automatically correct `activate` as a typo. |
| `timmer` | Absent | Absent | Distinct from registered `timer`; inspect the declaration and handler label together. |
| `FireProjectile` | Registered | Registered | Shared spelling and reviewed base call contract; templates and weapon lifecycle still differ. |
| `FireProjectileData` | Absent | Registered | Original registration; pinned source adds a final extra-data integer to the base call. |
| `AISetAlignment` | Absent | Registered | Original registration; pinned source writes through AI class state, requiring shared-class review. |
| `SendMessageExRadius` | Absent | Registered | Original registration. The inspected implementation filters thing types and a bounded message-ID range; it is not an unrestricted all-COG broadcast. |
| `CreateThingAtPos` final vector | Registered; angle interpretation in pinned JK mode | Registered; direction interpretation in pinned MotS mode | Semantic distinction is pinned-source evidence, not a dedicated original-game experiment. |
| `CreateThingAtPosOld` | Absent | Registered | Original registration; pinned source uses the JK-style creation implementation. |
| `preblock`, `escaped`, `attachkilled`, `playeraction`, `enterbubble`, `exitbubble` | Absent from integer-message table | Registered at IDs 41–46 | An event name being registered still does not supply its delivery route. |
| `startup`, `activated`, `timer`, `arrived`, `blocked` | Registered | Registered | Used by shared examples; integration and exact event producers remain part of validation. |
| `SetTimer`, `SetSectorLight`, `MoveToFrame`, `AISetMovePos` | Registered | Registered | Native names and source-reviewed contracts support the shared authoring target; runtime remains untested here. |

## Same spelling does not ensure identical behavior

The inspected source has mode-specific weapon selection and inventory conversion paths. In MotS, some setters convert low weapon-related arguments through selection order before applying them; other wrappers use a raw bin. Record which domain a variable holds instead of naming every integer `weapon`.

Template class COG binding also differs in the pinned source: JK adds class/local behavior; MotS adds class and then local unless the existing server flag changes that decision. An already-local flag is not necessarily cleared. This matters when porting a class script's multiplayer assumptions. It is source and bounded binding/dispatch evidence, not an original two-client test.

Limits can differ with build options. More generous stacks, symbol limits or enhanced native registries can let a modern build accept a script that exceeds a conservative configuration. Do not turn an enhanced parse into a stock compatibility claim.

## Enhanced engines and cogext

| Environment | Declare in your package | Do not assume |
| --- | --- | --- |
| Original game | Game, executable/patch version and whether SP or MP is tested | An unofficial patch preserves every behavior |
| OpenJKDF2/OpenMotS-derived build | Repository/revision or release, mode, enhanced-verb setting and relevant options | JK mode has only original JK verbs, or a successful join enforces retail checksum rules |
| cogext-dependent mod | Exact extension build if known; required native names and host mod resources | A partial recovered extension source is a complete versioned API or retail feature |
| Combined extension/enhanced setup | Every dependency and the observed combination | Two individually working features compose without conflict |

The examined enhanced-source registrations differ with options: enhanced JK mode can expose MotS and additional names, while enhanced MotS mode can restore names absent from the original MotS table. The initial public examples intentionally require none of those additional names.

Recovered cogext material includes typed stack manipulation, heap persistence, string and model operations. Some functions can return multiple stack values or mutate shared model resources. Ordinary single-result retail assumptions are not an adequate specification for them. When a needed implementation or exact host version remains unknown, record that dependency instead of inferring it from a similarly named verb.

## Retail checksum distinctions

Original static analysis shows a byte-reading checksum helper receiving the compiled script word count in both inspected games. Thus the script contribution reads a byte prefix of compiled instructions, rather than hashing the original text or necessarily all bytecode. Script flags do not produce a blanket exclusion from these loops. Changing compiled length or earlier layout can change that prefix even when the edited source line is near the end.

The loaded checksum also covers ordered script lists, vertex data and selected template fields. MotS additionally invokes a personality-data callback. Its original callback reads successive parameter rows; the pinned enhanced source has a different read-loop structure. The pinned enhanced source also disables a checksum rejection block. Neither difference should be generalized to an uninspected version.

The research includes selected original-instruction emulation and synthetic source probes. Those help establish particular mechanisms. They are not original retail level-loading, real COG compilation or a network handshake. For a claim of checksum acceptance, test the exact edited package against an identified peer in the intended original executable.

## Release checklist by target

For **JK 1.01**, check against the JK registry, use JK bins/resources/event routes, and test in the selected JK executable. For **MotS**, check against the MotS registry and additionally review weapon mappings, MotS events and personality-related integration. For **enhanced or extended targets**, declare the exact dependency and retain a separate result for any original-game tests.

A cross-target release needs both availability checks, both integration reviews and actual tests in both games before it can be described as runtime-verified on both. The `shared` example directory expresses the first two authoring intentions with bounded offline validation; it does not claim that last step has occurred.

See [SOURCES.md](sources.md) for public implementation references and [GUIDE.md](guide.md) for the practical workflow.
