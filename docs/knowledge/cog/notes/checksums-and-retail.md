# Retail checksums: measured mechanism and practical limits

> Copied into RED from [CodaLilitari/jk-mots-cog-workshop@b4b43f6](https://github.com/CodaLilitari/jk-mots-cog-workshop/blob/b4b43f6ab1976f673598c90751ec6160da70c506/corpus/notes/checksums-and-retail.md) (MIT; see [LICENSE](../LICENSE.md)). Regenerate with `tools/knowledge/import_knowledge.py` rather than editing this copy.

> Public research edition. This is original authored analysis, not a source-code dump. References marked "retained local evidence" were inspected during research but their raw artifacts are not distributed. Binary identities, measured registries and public package identities are included; source execution and retail gameplay remain different evidence grades.


The inspected original JK 1.01 and MotS executables compare a client-supplied world checksum with the host's expected value and take a rejection path on inequality. A COG's source file SHA-256, its character count, whether it is called a cheat, and its local-execution flag are not the definition of this checksum. The check consumes selected loaded game data, including a prefix of compiled COG bytecode. This explains why some changed COGs preserve a checksum and others do not, without treating checksum equality as behavioral correctness.

## Exact evidence anchors

Hashes and input paths are in retail_binary_receipts.json (retained local evidence); full annotated excerpts are in RETAIL_BINARY_EVIDENCE.md (retained local evidence) and retail_static_evidence.json (retained local evidence). These are original executable instructions. Names such as “world checksum” are analyst mappings by instructions, arguments and call graph, not original debug symbols.

| Mechanism | JK 1.01 address; disassembly lines | MotS address; disassembly lines |
| --- | --- | --- |
| Rotate/XOR byte helper | `0x42fad0`; 63393–63411 | `0x433390`; 68254–68272 |
| World checksum | `0x4d0f30`; 259517–259591 | `0x51f6d0`; 349045–349125 |
| Bytecode allocation/count/write | `0x4fd0d9`; 321020–321104 | `0x541b3f`; 397061–397144 |
| Join checksum comparison/rejection | `0x4cb30d`; 251410–251436 | `0x51994d`; 340836–340862 |
| Caller supplies checksum seed | `0x4c9d65`; 249739–249746 | `0x5183a5`; 339165–339172 |
| Selected template fields | `0x4cf6a0`; 257346–257491 | World routine calls its template helper; field details here additionally use current source |
| MotS personality callback | Not in JK path | `0x4069f0`; 7323–7398 |

Both exact executable hashes and both disassembly hashes were independently recomputed during the finishing review. All 14 selected excerpt blocks match the complete disassembly at the recorded line ranges. The observed instructions include actual compare, unequal branch, rejection-state construction, send call and return. No original game connection was executed for this report.

## What is hashed

The byte helper rotates a 32-bit accumulator left by one bit and XORs the next unsigned byte, repeating for the passed byte length. The world routine chains that accumulator through the world script list, vertex data (12 bytes per vertex), a field-wise template helper, and the static-world script list. MotS additionally invokes a registered personality-data callback. The script loops do not filter out scripts based on their local/no-sync execution flags.

The key count mismatch is independently visible in **both** executables:

1. The compiler allocates `4 * count + 4` bytes for int32 instruction words and a final return opcode, stores the pointer at script offset `+0x24`, and stores the word count including that terminator at `+0x28`.
2. Code writes use `pointer + index * 4`, confirming the unit is an int32 word.
3. The world and static-script checksum loops pass `+0x28` straight to the byte-reading helper as its length, without multiplying by four.

Therefore a script with stored count `N` has an allocated/code span of `4N` bytes, while this checksum step reads the first `N` bytes. It is a byte prefix, not necessarily an integral number of instructions. A count not divisible by four can end inside a word. This conclusion is supported by allocation, storage, write indexing and call sites together; it is not inferred merely from an ambiguous field name.

The pinned current source mirrors this at [sithWorld.c](https://github.com/CodaLilitari/OpenJKDF2-MotS-Fixes/blob/b1be98bd4f170b5384cb71b15443a274aac6a937/src/World/sithWorld.c), lines 642–675, [sithCogParse.c](https://github.com/CodaLilitari/OpenJKDF2-MotS-Fixes/blob/b1be98bd4f170b5384cb71b15443a274aac6a937/src/Cog/sithCogParse.c), lines 245–339, and [util.c](https://github.com/CodaLilitari/OpenJKDF2-MotS-Fixes/blob/b1be98bd4f170b5384cb71b15443a274aac6a937/src/General/util.c), `util_Weirdchecksum`. A complete source edit can affect the count, prefix operands, label addresses or symbol IDs even when its textual position is late. The checksum is also noncryptographic: changing hashed bytes need not imply a unique new value in every conceivable case.

The source's template helper hashes general flags, type, movement type and control type; for physics templates it additionally hashes physics flags, drag values, mass and height; actor templates add actor flags, health, maximum health, jump speed, maximum thrust and maximum rotational velocity; weapon templates add weapon flags, damage, the field named `unk8`, minimum damage and range. See [sithThing.c](https://github.com/CodaLilitari/OpenJKDF2-MotS-Fixes/blob/b1be98bd4f170b5384cb71b15443a274aac6a937/src/World/sithThing.c), lines 1947–1982 and the JK original helper excerpt. Do not generalize an unchecked model-name field to “all model changes are checksum-safe”: package loading can change other participating data and gameplay may require matching assets regardless.

## MotS `.per` data and a current source divergence

The original MotS callback enumerates `single`, `jedi`, `bounty`, `scout`, and `soldier`, opening `misc\\per\\<class>.per`. The original strings, class-name pointers and the floating multiplier 1000 are independently read from the PE in mots_per_binary_constants.json (retained local evidence). A valid `version 2` adds 2 to the accumulator. Successive `param <index>: <float>` rows with `0 <= index < 256` contribute a class/index-weighted value using the scaled float and XOR constant `0x5b32a`; parsing stops when the next row does not fit the expected condition. Floating conversion edge cases remain dependent on the original arithmetic/control state and were not exhaustively runtime-tested here.

The original loop reads the first parameter before entering the body and the next parameter at the bottom, then jumps directly back to parse that next line. The current fork's [jkPlayer.c](https://github.com/CodaLilitari/OpenJKDF2-MotS-Fixes/blob/b1be98bd4f170b5384cb71b15443a274aac6a937/src/World/jkPlayer.c), lines 2061–2088, calls `ReadLine()` in both its loop condition and body, skipping alternating rows. The stub-reader probe demonstrates this with three parameters: current source result **1,497,722**, every-line control **2,245,742**. This is a characterized source difference, not a claim that original MotS skips those lines, and this research task did not modify production checksum code.

## Changes that do and do not establish compatibility

| Candidate change or observation | What the evidence permits |
| --- | --- |
| Comments/whitespace only | Source bytes are not directly hashed. If the compiled prefix, count, loaded lists and all other hashed inputs remain unchanged, this checksum is unchanged. Parser/load effects still need verification. |
| Same number of source characters | No general conclusion. Different tokens can change instructions, symbol IDs and operands. |
| Editing a late source line | No general conclusion. If compiled data outside the exact prefix changes while count/prefix and other inputs remain identical, that script contribution is unchanged. The textual location alone does not establish this condition. |
| A symbol default or script flag | Not directly hashed as source text by the script loop. It can affect loading/binding, generated layout, or later behavior. Inspect the actual loaded representation. |
| `flags=0x240` or `local` declaration | Neither creates a blanket checksum exemption. These features govern other parts of the script contract. |
| Changed vertices or participating template fields | They are checksum inputs, so such changes can alter the result independently of the COG text. Synthetic probes establish particular examples, not collision-free behavior. |
| Modified MotS personality files | Parsed personality data participates in the original MotS path. Use the original callback semantics, not the current fork's double-read behavior. |
| Matching script SHA-256 | Useful integrity evidence, but other scripts, load order, vertices, templates, personalities and seed can still differ. |
| Matching full loaded checksum | Supports passing this equality check; does not prove all assets match, code behaves correctly, network state remains synchronized, or another version/patch enforces the same rules. |
| Successful join in the captured enhanced fork | Cannot certify retail acceptance: the rejection block is compiled out in that source snapshot. |

In the captured [sithMulti.c](https://github.com/CodaLilitari/OpenJKDF2-MotS-Fixes/blob/b1be98bd4f170b5384cb71b15443a274aac6a937/src/Dss/sithMulti.c), line 220 passes zero with a seed TODO, and lines 1164–1171 wrap the unequal-checksum rejection in `#if 0`. In the original executable excerpts the caller supplies a global seed and the unequal branch really sends rejection and returns. This is a concrete reason to keep enhanced-engine and retail compatibility results separate.

## Bounded executed probes

checksum_probe.c (retained local evidence) contains extracted current production functions with synthetic script, vertex and template structures. source_probe_results.txt (retained local evidence) records 15 passing cases: world/static script prefix and tail changes; script count; vertices; general template flags; unchecked model-name/COG-flags fields; actor health; weapon damage; physics mass; MotS extra callback selection; and seed. The test uses an eight-word synthetic script and hashes eight bytes; changing a later word preserves the tested result while changing the first word does not. It does not compile actual COG source and cannot certify an arbitrary source edit.

The later actual-source parser/code-generation probe (retained local evidence) adds controlled source examples. In its adapter environment, `a = 1 + 2 * 3;` emits 13 words and its 13-byte prefix hashes to 12,561 with seed zero. Longer comments/whitespace leave its code identical. Changing the final literal to 4 changes code and a full-bytecode checksum while leaving that prefix checksum 12,561; changing the first literal to 2 changes the prefix checksum to 12,562. These are observed source-compilation cases, not an arbitrary-edit rule. The identifier adapter differs from real loading, and this test excludes the other world inputs, so none of those numbers is represented as a retail multiplayer checksum.

## Repeatable diagnosis and original-game validation

The bounded original-instruction emulator (retained local evidence) adds 105 guarded cases against the exact owned JK 1.01 and MotS binaries: 101 positive arithmetic/data-path cases and four intentional guard stops. Both original world functions pass unchanged script counts as byte lengths, and the traces verify the exact ordered bytes read for world scripts, vertices and static scripts. Changing a script's unhashed tail or its actual flags field leaves the measured baseline unchanged; prefix/count/order/vertex/seed changes have separately recorded results. The script flag field is at offset `+0`, established from each original parser's `%x` destination; an earlier `+0x20` fixture was corrected and preserved as superseded evidence before integration.

This executes selected original machine instructions under emulation over synthetic memory. It does not run a game, compile a real COG, load an actual level, execute the template helper or actual MotS personality parser, or test a network handshake. The MotS extra-callback experiment uses an explicitly synthetic callback and establishes dispatch/order only. The root reviewer independently reran all 105 corrected cases. Keep this evidence level distinct from both source probes and actual retail play.

1. Identify exact host/client executables and hashes, game/patch versions, episode, mounted archives, loose overrides, and a clean baseline that joins. Preserve stock originals and use controlled copies.
2. Establish which COGs actually load, their order and compiled representation, the static-world scripts, vertex/template inputs, MotS personalities and seed. Compare effective data; archive filename equality alone is insufficient.
3. Change one cause at a time: comments, an early literal, a late literal, declaration order/default, a handler, script flags, a static script, a selected template field, a vertex, or a personality parameter. Predict the specific mechanism and record why.
4. Test edited versus unedited peers, then matching edited peers, in the intended original version. Record actual rejection/join observations and separately test behavior. Include remote effects and cleanup for multiplayer scripts.
5. Retain both supporting and counterexample cases with input hashes. A passing case establishes that case and target. It does not establish “all cheats work,” “all local COGs are exempt,” or “equal length means safe.”

The acquisition of historical cheats and checksum utilities is useful for discovering hypotheses and real examples. Their documentation is participant testimony until the mechanism and target are checked. This report explains the inspected original mechanism and the source probes; actual original two-client experimental coverage remains a separate, explicitly unperformed validation layer.
