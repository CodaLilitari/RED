# Triscuit generator: parser, code-size and native-call findings

> Copied into RED from [CodaLilitari/jk-mots-cog-workshop@b4b43f6](https://github.com/CodaLilitari/jk-mots-cog-workshop/blob/b4b43f6ab1976f673598c90751ec6160da70c506/corpus/notes/triscuit-generator-engine.md) (MIT; see [LICENSE](../LICENSE.md)). Regenerate with `tools/knowledge/import_knowledge.py` rather than editing this copy.

> Public research edition. This is original authored analysis, not a source-code dump. References marked "retained local evidence" were inspected during research but their raw artifacts are not distributed. Binary identities, measured registries and public package identities are included; source execution and retail gameplay remain different evidence grades.


**All 18 statically reconstructed fixtures were accepted by the pinned generated COG lexer/parser and emitted code through the actual AST/code-generation bodies.** Eight default/effects option pairs have equal generated word lengths; the sequencer pair differs by two words. This is a measured parser/code-generation result, not observed output of Triscuit's original VB program and not evidence of successful retail loading, gameplay or multiplayer joining.

The independently rehashed source form is `cogsource.frm`, 175,213 bytes, SHA-256 `c253bc5ac5e4a05dd2726131df6ae392f3d3aae9a157a0f39852ec2a86138cfe`. The historical analysis statically selects writer literals and equality guards to reconstruct nine weapon templates under two documented option sets. Each fixture hash was independently verified before probing; 13 specific emitted statements were also joined back to their exact original VB writer lines. The VB form, FRX and acquired EXE were never executed. The reconstructed fixture manifest and line maps retain the transformation explicitly. Reconstructed fixture manifest (retained local evidence) 13 original-writer statement witnesses (retained local evidence)

## Measured output

| Weapon | Default words | Effects/split words | First differing byte in adapter code | Same prefix extent and bytes? |
|---|---:|---:|---:|---|
| Fists | 458 | 458 | 856 | Yes |
| Bryar | 513 | 513 | 980 | Yes |
| Stormtrooper rifle | 531 | 531 | 1,092 | Yes |
| Thermal detonator | 830 | 830 | 2,176 | Yes |
| Crossbow | 1,023 | 1,023 | 1,560 | Yes |
| Repeater | 1,003 | 1,003 | 1,620 | Yes |
| Rail detonator | 692 | 692 | 1,376 | Yes |
| Sequencer charge | 516 | 514 | 1,304 | No: prefix lengths differ |
| Concussion rifle | 697 | 697 | 1,256 | Yes |

“Prefix” here means exactly the helper's generated-word-count-as-byte-count convention. For example, the Bryar emits 513 int32 words (2,052 bytes), while this helper call hashes its first 513 bytes with seed zero. Its first changed byte is 980. All eight equal-length pairs therefore have identical tested prefix bytes and equal helper results; every pair has different full code words and different all-bytecode helper results. This is stronger than merely observing a hash collision: the tested prefixes themselves are identical in this adapter. It still says nothing definitive about the original game's symbol numbering, header/resource values, complete world checksum or session acceptance. Exact pair and prefix comparisons (retained local evidence)

Every pair changes some opcode kinds, such as symbol pushes becoming integer/float pushes. The eight equal-length pairs also retain equal instruction counts, but their opcode skeletons are **not identical**. The sequencer default contains two extra unary negations in `x(x, x, -x, -x, ...)`; the enabled projectile call uses `fireSound, 18` in those positions. The emitted NEG count changes from 9 to 7, with 309→307 instructions and 516→514 words. The exact source diff is preserved. Sequencer source diff (retained local evidence)

The probe used installed WSL GCC with UndefinedBehaviorSanitizer, unchanged generated lexer/parser units and unchanged extracted AST/code-generation bodies. It supplies host I/O, minimal declarations and its own symbol interning. It does not run the real header loader, native symbol registry initialization, type/arity validation, resource resolution, COG VM or original executable. All stdout/results, source-unit hashes, fixture hashes, adapter hashes and the build command are preserved. Full 18-case results (retained local evidence) Reproducible runner (retained local evidence)

## Disabled X calls are syntactically valid, not verified no-ops

The VB writer deliberately emits `X(...)` forms for disabled options while keeping argument/expression shapes. The measured original JK and MotS native-call tables contain no `x` verb. The advisory registry review identifies `x` in 15 of the 18 fixtures per target; no other absent call spelling appears in this bounded review. This absence is not itself a compiler-rejection test. 36 advisory original-target reports (retained local evidence)

The pinned source explains the distinction:

- `sithCogParse_LexerSetSymbol` folds names to lowercase, looks them up, and creates an unknown symbol with type 2 and zero value. `X` and `x` therefore refer to the same symbol; they are not separate dummy function and dummy variable names. [Identifier resolver](https://github.com/CodaLilitari/OpenJKDF2-MotS-Fixes/blob/b1be98bd4f170b5384cb71b15443a274aac6a937/src/Cog/sithCogParse.c#L717)
- The grammar permits a symbol call without verifying its native function type. Our 18 accepted code sections demonstrate this for these fixtures.
- `CALLFUNC` first pops the callee symbol. If its value is not a function pointer, it exits that opcode without calling anything. It does **not** remove the already evaluated arguments as a group and does not push a result. Nested unknown calls and subsequent assignments can therefore leave or consume stack entries. [CALLFUNC](https://github.com/CodaLilitari/OpenJKDF2-MotS-Fixes/blob/b1be98bd4f170b5384cb71b15443a274aac6a937/src/Cog/sithCogExec.c#L120)
- `ASSIGN` consumes a value and destination, resolves and writes the destination, and does not push an assignment-expression result. Residual entries from a prior unknown call can materially change what an assignment consumes. Do not assume `X = X(...)` is equivalent to removing the statement. [ASSIGN](https://github.com/CodaLilitari/OpenJKDF2-MotS-Fixes/blob/b1be98bd4f170b5384cb71b15443a274aac6a937/src/Cog/sithCogExec.c#L161)

Several such calls are in reachable declared `pulse` or `timer` regions. Other padding follows an unconditional `Return` with no intervening entry label. Both regions are still compiled; source reachability is a separate analysis from code size. Do not describe every unusual `X` statement as unreachable, or every compiled statement as executed. For the representative Bryar, `activated` also falls through into `pulse` after setting the pulse interval: scheduling does not itself suspend the current handler. Default reconstructed Bryar (retained local evidence)

The spelling `x/x` is also insufficient to conclude a division crash. The pinned `DIV` implementation explicitly pushes zero when its divisor is zero. Other arithmetic paths, changing symbol values and residual stack state need their own analysis. [DIV zero-divisor branch](https://github.com/CodaLilitari/OpenJKDF2-MotS-Fixes/blob/b1be98bd4f170b5384cb71b15443a274aac6a937/src/Cog/sithCogExec.c#L1301)

## Native-call return contracts require individual checks

The reconstructed Bryar includes `Off = SetPulse(0)` and `Speed = SetPulse(0.5)`. The pinned `SetPulse` implementation changes pulse state and consumes its interval, but pushes no return value. The side effect can still occur; the assignment afterward lacks the usual expression result and may consume remaining stack entries. The enabled writer similarly assigns the results of `jkStringClear`, `jkStringConcatAsciiString` and `jkStringOutput`, whose pinned bodies do not push values. This is not a reason to say the functions were never called. [SetPulse](https://github.com/CodaLilitari/OpenJKDF2-MotS-Fixes/blob/b1be98bd4f170b5384cb71b15443a274aac6a937/src/Cog/sithCogFunction.c#L317) [StringClear; also ConcatAsciiString 675 and StringOutput 838](https://github.com/CodaLilitari/OpenJKDF2-MotS-Fixes/blob/b1be98bd4f170b5384cb71b15443a274aac6a937/src/Cog/jkCog.c#L642)

In contrast, `SetThingModel` **does** push the old model ID (or `-1`), and `DamageThing` has a return value. Do not lump their assigned forms into the void-return category. Verify the complete relevant branch before declaring a signature wrong. [SetThingModel](https://github.com/CodaLilitari/OpenJKDF2-MotS-Fixes/blob/b1be98bd4f170b5384cb71b15443a274aac6a937/src/Cog/sithCogFunctionThing.c#L1136) [DamageThing](https://github.com/CodaLilitari/OpenJKDF2-MotS-Fixes/blob/b1be98bd4f170b5384cb71b15443a274aac6a937/src/Cog/sithCogFunctionThing.c#L274)

Enabled Bryar projectile calls use undeclared `ZER0` and `ZERO` as vector arguments. `PopVector` zeroes its output and returns failure when the popped value is not a vector. The pinned `FireProjectileInternal` ignores the two boolean results and continues with the output vectors. Consequently this specific implementation can receive zero vectors from these symbols instead of rejecting the call. Many other vector-taking functions test the return flag, so this fallback must not be generalized. [PopVector](https://github.com/CodaLilitari/OpenJKDF2-MotS-Fixes/blob/b1be98bd4f170b5384cb71b15443a274aac6a937/src/Cog/sithCogExec.c#L478) [FireProjectileInternal](https://github.com/CodaLilitari/OpenJKDF2-MotS-Fixes/blob/b1be98bd4f170b5384cb71b15443a274aac6a937/src/Cog/sithCogFunction.c#L1149)

These native-call conclusions come from the pinned implementation source. No fixture was executed in the COG VM, and no original retail executable behavior was observed for the reconstructed templates.

## How to use this evidence

Keep four stages separate: original VB writer evidence → bounded fixture reconstruction → parser/code-generation measurement → target-runtime verification. Word-count preservation and unreachable padding are useful historical mechanisms to study, but neither establishes retail checksum compatibility. A complete comparison would require original target registration/declaration ordering, resource values and the full original checksum inputs; an actual join outcome needs a controlled retail session.

Preserve these fixtures as research evidence. Do not silently repair the unknown calls, delete padding or replace default resources and then present the result as original generator output. If authoring a clean derivative, make the transformation explicit, respect original attribution and revalidate its lifecycle, native signatures and target resources. The authoring decision guide (retained local evidence) and checksum/retail analysis (retained local evidence) explain the broader evidence boundaries.
