# Engine evidence for JKDF2 and Mysteries of the Sith COG

> Copied into RED from [CodaLilitari/jk-mots-cog-workshop@b4b43f6](https://github.com/CodaLilitari/jk-mots-cog-workshop/blob/b4b43f6ab1976f673598c90751ec6160da70c506/corpus/notes/engine-findings.md) (MIT; see [LICENSE](../LICENSE.md)). Regenerate with `tools/knowledge/import_knowledge.py` rather than editing this copy.

> Public research edition. This is original authored analysis, not a source-code dump. References marked "retained local evidence" were inspected during research but their raw artifacts are not distributed. Binary identities, measured registries and public package identities are included; source execution and retail gameplay remain different evidence grades.


This report distinguishes original executable inspection, executed source probes, and source interpretation. It does not claim a retail launch, a successful retail join, or exhaustive behavior verification for every verb. Use [language and execution](language-and-execution.md) when authoring and [checksums and retail](checksums-and-retail.md) when diagnosing compatibility.

## Pinned inputs and independently checked integrity

The source snapshot is repository commit `b1be98bd4f170b5384cb71b15443a274aac6a937`, captured on 30 September 2026. source_receipt.json (retained local evidence) records 41 files and SHA-256 values. The finishing review independently recomputed every snapshot hash and compared each file to the current repository: all 41 matched at review time. These are the current fork's sources; comments saying “MOTS added” or “original” remain source testimony unless separately checked in an original executable.

| Original input | Size | SHA-256 |
| --- | ---: | --- |
| JK executable recovered from the inspected official 1.01 update | 1,388,544 bytes | `4509cd690634ba20e132bd16e713f91e144b17e2beaaad5e678f1ddb189b51d9` |
| Owned stock MotS `JKM.EXE` | 1,759,744 bytes | `23cd064217f904d36d896f52d3cdd17635bb85441a12e0415c0637504191d974` |

Exact local input paths, disassembly commands and output hashes are in retail_binary_receipts.json (retained local evidence). The finishing review found both inputs present with matching hashes, both complete disassembly hashes matching, and all 14 selected disassembly excerpts identical to their recorded line ranges. This establishes integrity of the inspected evidence; it does not turn static instructions into observed game execution.

## Measured verb and message registrations

| Target/profile | Distinct verb spellings | Message spellings |
| --- | ---: | ---: |
| Inspected original JK 1.01 | 451 | 41 |
| Inspected original MotS | 538 | 47 |
| Current fork, JK mode, enhanced verbs off | 451 | 41 |
| Current fork, JK mode, enhanced verbs on | 594 | 45 |
| Current fork, MotS mode, enhanced verbs off | 538 | 47 |
| Current fork, MotS mode, enhanced verbs on | 552 | 47 |

The original-binary extraction locates constant-name calls to independently identified registration helpers, with separately inspected inline first-message registrations. There were zero unparsed candidate calls. Each row in jk101_retail_registry.json (retained local evidence) and mots_retail_registry.json (retained local evidence) includes the executable hash, name address, registration call address, implementation address or message ID, and disassembly line. See retail_registry_summary.json (retained local evidence) and the extraction scripts for the measured scope. Registrations are evidence of exposed names, not exhaustive startup reachability or successful execution of every function.

The source registries were produced by compiling extracted startup bodies with `JKM_AI` and `QOL_IMPROVEMENTS`, selecting both compatibility modes and both enhanced-verb settings, and replacing registration operations with recorders. registry_execution.tsv (retained local evidence), verb_registry.json (retained local evidence), message_registry.json (retained local evidence), and registry_differences.json (retained local evidence) retain the actual results. The baseline source and binary name sets agree for both games. Message counts count aliases: `activate` and `activated` both use ID 1, so 41 spellings are not 41 distinct event IDs.

The MotS binary adds 88 verb spellings relative to JK and removes one, `GetCurInvWeapon`. It exposes `GetCurInvWeapon2`. Its six additional message spellings are `preblock`, `escaped`, `attachkilled`, `playeraction`, `enterbubble`, and `exitbubble` (IDs 41 through 46). Enhanced JK mode backports many MotS names and extensions; enhanced MotS mode restores names absent from the original table. An enhanced-engine parse cannot establish original-game compatibility.

The 41/47 counts cover **registered integer message spellings**, not every symbol accepted by the declaration parser. A follow-up independently verified `global0` through `global15` as zero-initialized **float globals** in each original binary. Their helpers at JK `0x4e0950` and MotS `0x52c9c0` set value type tag 2; all 32 name callsites and exact binary hashes are recorded in retail_float_globals.json (retained local evidence), reproducible with extract_retail_float_globals.py (retained local evidence). The pinned `sithCogParse_ParseMessage` uses generic global-symbol lookup and copies raw value bits without a type check. A declaration referring to a float global therefore requires a separate nonstandard-binding warning, not a claim that its name is absent from the target's global symbols. These are not 16 additional named events, and successful symbol lookup does not establish event delivery. Original-binary parser acceptance of arbitrary aliases was not separately traced in this follow-up.

## Differences that affect actual ports

- **Availability is only the first check.** The source chooses different implementations for `CreateThingAtPos`, `CreateThingAtPosNR`, and `GetActorWeapon` between baseline modes. See the exact rows in registry_differences.json (retained local evidence).
- **Position creation takes a different orientation interpretation.** In the pinned source, JK's helper builds a rotation matrix from the final vector; MotS creates with an identity rotation and normalizes a nonzero final vector as a look direction. MotS's `CreateThingAtPosOld` is registered to the JK-style implementation. Inspect [sithCogFunctionThing.c](https://github.com/CodaLilitari/OpenJKDF2-MotS-Fixes/blob/b1be98bd4f170b5384cb71b15443a274aac6a937/src/Cog/sithCogFunctionThing.c), lines 129–272 and 2638–2652. Check same-game stock call sites and the desired target before translating a PYR vector into a direction vector. These semantic details have source evidence here, not dedicated original-binary execution tests.
- **Weapon bins and selection mappings differ.** The pinned source's `GetCurInvWeapon` implementation performs an additional MotS selection-priority mapping; another MotS helper returns the raw current-weapon index. `GetActorWeapon` also remaps a player weapon template index in MotS. See [sithCogFunctionThing.c](https://github.com/CodaLilitari/OpenJKDF2-MotS-Fixes/blob/b1be98bd4f170b5384cb71b15443a274aac6a937/src/Cog/sithCogFunctionThing.c), lines 1482–1512 and 1704–1799. Do not port solely by changing a suffix: inspect the caller's required bin/index domain and the relevant `ITEMS.DAT`.
- **Class COG locality has a mode-sensitive binding rule.** The pinned source gives JK template COGs class and local flags. MotS adds class, then adds local unless the existing server bit is set. Existing local flags are not forcibly cleared. See [sithThing.c](https://github.com/CodaLilitari/OpenJKDF2-MotS-Fixes/blob/b1be98bd4f170b5384cb71b15443a274aac6a937/src/World/sithThing.c), lines 1857–1876. The source's binding/dispatch regression covers this rule; it is not a retail two-client test.
- **Execution limits are build-sensitive.** The source configuration has 64 stack entries in conservative profiles and 65,536 in the desktop QOL profile, different symbol/link limits, and parser-node limits with historical comments. See [engine_config.h](https://github.com/CodaLilitari/OpenJKDF2-MotS-Fixes/blob/b1be98bd4f170b5384cb71b15443a274aac6a937/src/engine_config.h), lines 94–115. The parser still explicitly limits one script to 32 message handlers and 128 symbol references, and the executor uses a four-frame call stack. This review did not independently measure every original executable resource ceiling.

## What was executed

source_probe_results.txt (retained local evidence) records GCC 15.2.0, four registration profiles, 15 synthetic checksum cases, the `.per` callback characterization, and the production-source locality harness. The latter reports 30,766 assertions across 2,882 binding and 2,882 dispatch cases, UBSan enabled, rejection of the old-condition mutant, and unchanged production input hashes. Loader, VM executor and transport are stubbed in the locality harness; checksum tests use synthetic structures and extracted functions, without a COG parser or retail process.

The finishing review reran the source probes successfully with generated files isolated in Linux temporary storage; the original Windows output path refused an initial write with `Errno 22`. The independent rerun log (retained local evidence) retains its output. The integrity receipt (retained local evidence) additionally verifies all 41 source hashes, both original binary/disassembly hashes, all 1,077 recorded PE name strings and registration line locations, and all 14 selected excerpts.

The checksum cases demonstrate that changing a compiled tail beyond the hashed byte prefix can preserve this checksum, while sampled changes to the prefix, code size, vertex bytes and selected template fields alter it. They also check static scripts, seed changes and MotS's extra callback. They do not justify a blanket “late edits always work” rule: compilation may move earlier operands or change the count. The `.per` case proves the current source skips the middle line of a three-parameter fixture; original MotS's separately inspected loop consumes every successive parameter line.

A later headless parser/code-generation probe (retained local evidence) compiled the actual generated lexer/parser and extracted AST/code-generation bodies with a small host/symbol adapter. All 18 cases passed under UBSan, including three retail code sections, the authored light fixture, rejected unsupported grammar, and controlled source-to-bytecode comparisons. Comments/whitespace produced identical code; a particular tail-literal change altered compiled words but not the tested prefix checksum. This is stronger than lexical spelling or synthetic-byte-array evidence for those cases, but it does not load the full symbols/resource sections, use retail symbol IDs, execute the VM, or predict the complete original-game world checksum. The receipt (retained local evidence) preserves those boundaries.

## Lookup and future verification

implementation_index.json (retained local evidence) maps implementations to source locations and stack-pop sequences; mode_sensitive_branches.json (retained local evidence) identifies compatibility branches. These are navigation aids. A pop-order extraction is not a complete signature: wrapper arguments, conditional pops, conversions and return branches still need inspection.

Practical verb families (retained local evidence) follows those bodies through representative wrappers and branches, documenting call-order arguments, pushed results, reference domains and synchronization paths for 15 authoring families.

For a new claim, retain the script/package and executable hashes, loaded resource identity, exact build definitions, a narrowly stated expected result, and the observed outcome. Original multiplayer checksum acceptance requires actual original clients with controlled inputs and a clean baseline. Claims about replicated state require remote observations, including relevant late-join and cleanup behavior. Nothing in this report substitutes an enhanced-engine success for that evidence.
