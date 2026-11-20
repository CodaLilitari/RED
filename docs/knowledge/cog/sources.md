# Sources, attribution and evidence boundaries

> Copied into RED from [CodaLilitari/jk-mots-cog-workshop@b4b43f6](https://github.com/CodaLilitari/jk-mots-cog-workshop/blob/b4b43f6ab1976f673598c90751ec6160da70c506/docs/SOURCES.md) (MIT; see [LICENSE](LICENSE.md)). Regenerate with `tools/knowledge/import_knowledge.py` rather than editing this copy.

This release's teaching prose and six examples are original work. Its research method is indebted to the game's creators and the editing community, including the authors who preserved examples, explanations, corrections and tools. Source availability is not a grant to redistribute every historical package. The public release separates original notes and discovery metadata from proprietary payloads and private material.

## Public implementation references

The pinned source used for the guide is commit `b1be98bd4f170b5384cb71b15443a274aac6a937` in the [OpenJKDF2-MotS-Fixes repository](https://github.com/CodaLilitari/OpenJKDF2-MotS-Fixes), a fork of [shinyquagsire23/OpenJKDF2](https://github.com/shinyquagsire23/OpenJKDF2). It is an inspected implementation, not a claim that every reconstructed function exactly matches every original executable.

| Topic | Public source at the inspected revision |
| --- | --- |
| Grammar and operator structure | [cog.y](https://github.com/CodaLilitari/OpenJKDF2-MotS-Fixes/blob/b1be98bd4f170b5384cb71b15443a274aac6a937/src/Cog/cog.y), [generated lexer](https://github.com/CodaLilitari/OpenJKDF2-MotS-Fixes/blob/b1be98bd4f170b5384cb71b15443a274aac6a937/src/Cog/lex.yy.c) |
| Declaration and code generation | [sithCogParse.c](https://github.com/CodaLilitari/OpenJKDF2-MotS-Fixes/blob/b1be98bd4f170b5384cb71b15443a274aac6a937/src/Cog/sithCogParse.c) |
| Tagged values, execution, calls and suspension | [sithCogExec.c](https://github.com/CodaLilitari/OpenJKDF2-MotS-Fixes/blob/b1be98bd4f170b5384cb71b15443a274aac6a937/src/Cog/sithCogExec.c) |
| Instances, parameter binding and event dispatch | [sithCog.c](https://github.com/CodaLilitari/OpenJKDF2-MotS-Fixes/blob/b1be98bd4f170b5384cb71b15443a274aac6a937/src/Cog/sithCog.c) |
| Timers, messages, heap and triggers | [sithCogFunction.c](https://github.com/CodaLilitari/OpenJKDF2-MotS-Fixes/blob/b1be98bd4f170b5384cb71b15443a274aac6a937/src/Cog/sithCogFunction.c) |
| Thing creation, path motion, inventory and keyframes | [sithCogFunctionThing.c](https://github.com/CodaLilitari/OpenJKDF2-MotS-Fixes/blob/b1be98bd4f170b5384cb71b15443a274aac6a937/src/Cog/sithCogFunctionThing.c) |
| AI look/move requests and modes | [sithCogFunctionAI.c](https://github.com/CodaLilitari/OpenJKDF2-MotS-Fixes/blob/b1be98bd4f170b5384cb71b15443a274aac6a937/src/Cog/sithCogFunctionAI.c) |
| Sector extra-light contract | [sithCogFunctionSector.c](https://github.com/CodaLilitari/OpenJKDF2-MotS-Fixes/blob/b1be98bd4f170b5384cb71b15443a274aac6a937/src/Cog/sithCogFunctionSector.c) |
| Inventory selection and bins | [sithInventory.c](https://github.com/CodaLilitari/OpenJKDF2-MotS-Fixes/blob/b1be98bd4f170b5384cb71b15443a274aac6a937/src/Gameplay/sithInventory.c) |
| World checksum chaining | [sithWorld.c](https://github.com/CodaLilitari/OpenJKDF2-MotS-Fixes/blob/b1be98bd4f170b5384cb71b15443a274aac6a937/src/World/sithWorld.c) |
| Multiplayer join handling in this fork | [sithMulti.c](https://github.com/CodaLilitari/OpenJKDF2-MotS-Fixes/blob/b1be98bd4f170b5384cb71b15443a274aac6a937/src/Dss/sithMulti.c) |
| MotS personality path in this fork | [jkPlayer.c](https://github.com/CodaLilitari/OpenJKDF2-MotS-Fixes/blob/b1be98bd4f170b5384cb71b15443a274aac6a937/src/World/jkPlayer.c) |

Original registration counts and selected checksum conclusions also rely on static examination of owned original executables. The exact identities are recorded in [COMPATIBILITY.md](compatibility.md). The binaries, retail archives, disassembly dumps and private provenance are not teaching-example dependencies and are not reproduced by these documents.

## Historical research entry points

- [The Massassi Temple](https://www.massassi.net/) hosts editing resources, levels and author records. Preserve each package's own credits and terms when following its links.
- [Massassi's archived message boards](https://www.massassi.com/etc/archived_msgboards/) preserve public editing discussions. A historical post is evidence of what its author reported; compare the actual target and later corrections before adopting a rule.
- [JKArchive](https://jkdf2.net/JKBot/JKArchive/) preserves historical JKDF2/MotS material, including authoring tools. An archive entry is a discovery lead, not proof of original authorship or permission to relicense its contents.
- EAH_TRISCUIT's [COG Maker source archive](https://jkdf2.net/JKBot/JKArchive/JKDF2/JKDF2/Game%20Files/tools/COG/cogmakersource.zip) supplied historical generator evidence. Its generated output must be inspected separately from the UI's description. The original tool and its source are not copied into the teaching examples.
- EAH_CL0wN's [M.A.C. release record](https://www.massassi.net/levels/files/1487.shtml) supplies attributed historical context for custom COG systems. Tool-maker attribution and a recovered mod are distinct findings.
- [CogWriter 0.3's historical download](https://www.massassi.net/programs/cog/cogwriter_03.zip), credited by Massassi to David Lovejoy and Alexei Novikov, is an example of contemporary tooling. A tool's catalog is not an authoritative replacement for the target executable's registered names.

The research also studied BeefCaike's level systems, Triscuit's generator semantics, CL0wN's custom scripts, and many other credited projects. Those studies informed practical lessons about integration and failure cases. Their source code, author branding and assets remain their authors' work; this guide does not present those lessons as a license to republish the original payloads.

Robert Huebner's *Adding Languages to Game Engines*, published in *Game Developer* in September 1997, is a useful contemporary design account. Its sample names and approximate interface descriptions should be read as design history, then checked against the shipped target. No scan or copied article text is included here.

## Private-derived notes

Some research used authorized private discussions or attachments. Public notes may explain a technical lesson in new words, corroborated where possible against public source or original-game contracts. They omit private messages, screenshots, attachments, account details, participant lists, message identifiers and private links. A public technical claim is not evidence that its underlying private record is redistributable.

When evidence is incomplete, retain that limit. A package's acquisition, a complete source reading, lexical acceptance, code-section parsing, an isolated source probe and a retail multiplayer result are different achievements. The guide and example validation record keep them separate.
