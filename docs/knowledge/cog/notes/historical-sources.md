# Historical archives, authors and generator research

> Copied into RED from [CodaLilitari/jk-mots-cog-workshop@b4b43f6](https://github.com/CodaLilitari/jk-mots-cog-workshop/blob/b4b43f6ab1976f673598c90751ec6160da70c506/corpus/notes/historical-sources.md) (MIT; see [LICENSE](../LICENSE.md)). Regenerate with `tools/knowledge/import_knowledge.py` rather than editing this copy.

The public catalog preserves observed source URLs and retrieved-byte identities. It is a finding aid and an attribution record, not a mirror of the referenced works or permission to redistribute them. No historical executable was run to produce these notes. The initial release includes cheat-related metadata because unusual scripts reveal language, native-call and network behavior; disruptive gameplay claims still need a controlled test environment.

## EAH_Triscuit and COG makers

The recovered author-site pages at the [archived EAH_Triscuit catalog](https://web.archive.org/web/20010206081215id_/http://eah-triscuit.com/cogs.html) identify many COG packages and readmes. Some retrievals preserve only documentation or error bodies. A successful HTTP response and a `.zip` suffix do not establish that a ZIP payload was recovered. Consult content identity, size, format and extraction evidence independently.

The [JKArchive COG tools collection](https://jkdf2.net/JKBot/JKArchive/?dir=JKDF2/JKDF2/Game%20Files/tools/COG) supplied `cogmakersource.zip`, SHA-256 `51f6fe586c92e0ed9d28c78a74449f43c58bbf93d3ed533a539e84a45bd2a897`, 39,371 bytes. Its attributed VB form `cogsource.frm` is 175,213 bytes with SHA-256 `c253bc5ac5e4a05dd2726131df6ae392f3d3aae9a157a0f39852ec2a86138cfe`.

The research statically reconstructed nine weapon templates under two bounded option sets from the writer statements. All 18 reconstructed code sections were accepted by a harness using the pinned generated lexer/parser and code-generation implementation. This is reconstruction from source, not witnessed output from running the original VB tool. Eight option pairs retained equal generated word lengths and identical tested checksum prefixes; the sequencer pair differed by two words. Read [the detailed generator study](triscuit-generator-engine.md) before drawing a compatibility conclusion.

Disabled options can still emit unknown calls and preserve argument shapes. Unknown calls are not guaranteed clean no-ops: their evaluated arguments can remain on the VM stack. Unreachable compiled padding also contributes to code layout. Equal bytecode-prefix measurements in a source adapter do not establish an original retail join result, because symbol numbering, loaded resources and other world inputs differ.

## CL0wN archives

Recovered packages include M.A.C., Sneaky Hax II and Sith Sabers source. The [Massassi Sith Sabers package](https://www.massassi.net/media/levels/files/jkmod/cl0wns_sith_sabers.zip) matches the observed JKArchive copy at SHA-256 `65af7c3612bdc7e41c5c1b095ddc2c93642904596aab207fb46269d6929a5654`, 1,039,016 bytes. The historical studies examined 23 M.A.C., 38 Sneaky Hax II and 36 Sith Sabers COG bodies; counts describe the inspected packages, not a complete author archive.

Several apparently invalid constructs in these collections were accepted by the pinned parser. The important distinctions were native-call availability, runtime symbol type, stack residue, repeated labels, actual event reachability and cleanup. A pluralized weapon-flag setter absent from both measured original registries is not made callable by parser acceptance. Duplicate labels can overwrite an earlier dispatch location. A saved model index must not be overwritten by an incoming actor index merely because both are integers.

The requested CL0wN COG-maker identity and complete original archive remain unresolved in this initial release. Another person's maker or a tool with a generic filename is not substituted for that missing artifact. Retained failed leads do not prove that no surviving capture exists.

## BeefCaike and BeefyPack

The author-operated [Dralloc site](https://www.dralloc.net/) publishes [BeefyPack](https://www.dralloc.net/levels/BeefyPack.zip), 517,972,940 bytes, SHA-256 `c20025c3892e788cd6c29389bb539ebb5e982afe0f14fdbe7f64cdd3f3055b7b`. The inspected pack has 24 GOB members, 333 COG occurrences and 175 distinct source hashes. A focused study reads 25 complete variants and their actual world/template bindings.

This is an unusually useful collection for studying integration: positional audio, gravity protocols, elevators, healing, spawn routing, bowling, Plinko, race clocks and visual displays. Preserve both original COG authorship and the level integrator's work. The source headers also credit collaborators and retained LucasArts source. A level's author is not automatically the author of every packaged member. Read [the detailed BeefCaike study](beefcaike-cog-study.md) for the source-specific mechanisms and test plans.

## 420 and SM levels

The [420 Union page](http://420union.atspace.com/) publishes a JKArchive link for The 420 Lounge. The acquired [Sith Mercenaries-hosted ZIP](http://www.sithmercenaries.net/downloads/420lounge.zip) and the observed JKArchive payload share SHA-256 `d12cb713d69548e6cfc76929ff7d459c0e7e763962d8b7e2370cacfd24d3e274`, 4,015,483 bytes. All seven packaged COGs were studied with their level bindings. The live clan attribution identifies Sith Mercenaries; a tentative expansion of “SM” as Sith Masters was not adopted as fact.

The inspected Massassi and larger JKArchive Purgatory ZIPs are different package bytes, but their level GOB is identical: 4,051,877 bytes, SHA-256 `49f7365b31f5169b5fc02262bd243989b027c0e0a7535abfc56e436197693a4c`. The larger ZIP is therefore not evidence that the uncensored level was recovered. Historical public discussion links a distinct uncensored release on a GeoCities author page; that particular payload remains a gap. Existing Purgatory analysis is explicitly tied to the established edition.

## What coverage means in this release

The frozen research reached all 2,309 observed Massassi JK/MotS catalog package URLs and acquired 2,308 nonempty responses, covering the nine observed category lists. Sanctium's empty HTTP 200 response remains a package gap. The observed JKArchive game catalog had 4,162 URLs; TACC had 172, with one recorded 404. Public COG/editing forum collection, other sites and author archives provide further bibliographic evidence. These are measured frontiers, not a claim that every historical release anywhere on the internet has been found.

The public export uses a narrower source allowlist than the full local research. Its counts are in `corpus/stats.json` (or the installed skill's `data/stats.json`). A hash can identify the same source in multiple packages; package counts, source occurrences and distinct script bodies are different measurements. Acquisition, complete source understanding, parser acceptance, actual retail behavior and multiplayer compatibility are different milestones.
