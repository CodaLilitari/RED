# Public COG research corpus

> Copied into RED from [CodaLilitari/jk-mots-cog-workshop@b4b43f6](https://github.com/CodaLilitari/jk-mots-cog-workshop/blob/b4b43f6ab1976f673598c90751ec6160da70c506/corpus/README.md) (MIT; see [LICENSE](LICENSE.md)). Regenerate with `tools/knowledge/import_knowledge.py` rather than editing this copy.

The 0.1.4 [flag reference](flags.md) covers every historical Massassi flag/value table, plus five related domains. [reference/flags.json](https://github.com/CodaLilitari/jk-mots-cog-workshop/blob/b4b43f6ab1976f673598c90751ec6160da70c506/corpus/reference/flags.json) contains 31 families, 385 entries, owning fields, source hashes/anchors, evidence limits and separate Indy comparison data. Every ThingFlags bit is accounted for, with unresolved semantics retained. This adds original synthesis; it does not change the frozen COG catalog or establish new retail runtime coverage.

This initial release publishes substantive original analysis, measured JK/MotS registries, a source bibliography and exact COG identity/lexical metadata. It does not redistribute raw retail scripts, community packages, copied manuals, private messages or capture data. Public availability of a referenced work is not a blanket redistribution license.

The frozen local research snapshot contains 70,346 COG occurrences and 25,364 distinct bodies. The public subset is deliberately smaller: consult [stats.json](https://github.com/CodaLilitari/jk-mots-cog-workshop/blob/b4b43f6ab1976f673598c90751ec6160da70c506/corpus/stats.json) for generated counts. It contains only identities with approved public-source or retail provenance. Game labels on catalog records describe that provenance; they do not prove that every packaged script works in the named engine.

| Public artifact | What it provides |
| --- | --- |
| [catalog/sources.jsonl](https://github.com/CodaLilitari/jk-mots-cog-workshop/blob/b4b43f6ab1976f673598c90751ec6160da70c506/corpus/catalog/sources.jsonl) | Public source URL, limited title/author/category metadata, recorded response size/hash and status where available. No page text, message text or nested capture metadata. |
| [catalog/cogs.jsonl](https://github.com/CodaLilitari/jk-mots-cog-workshop/blob/b4b43f6ab1976f673598c90751ec6160da70c506/corpus/catalog/cogs.jsonl) | Exact COG hash/size, public or retail member names, game associations, lexical calls/messages/tags and approved provenance IDs. No source bodies, declarations or literal string data. |
| [registries/compatibility.jsonl](https://github.com/CodaLilitari/jk-mots-cog-workshop/blob/b4b43f6ab1976f673598c90751ec6160da70c506/corpus/registries/compatibility.jsonl) | Per-name JK 1.01 versus MotS availability for native verbs and integer message spellings. |
| [registries/jk101-retail-registry.json](https://github.com/CodaLilitari/jk-mots-cog-workshop/blob/b4b43f6ab1976f673598c90751ec6160da70c506/corpus/registries/jk101-retail-registry.json) and [MotS registry](https://github.com/CodaLilitari/jk-mots-cog-workshop/blob/b4b43f6ab1976f673598c90751ec6160da70c506/corpus/registries/mots-retail-registry.json) | Measured binary hashes, registration/name addresses and implementation address or message ID. No executable or disassembly payload. |
| [notes/](https://github.com/CodaLilitari/jk-mots-cog-workshop/blob/b4b43f6ab1976f673598c90751ec6160da70c506/corpus/notes) | Original research on language/execution, target differences, checksums, 16 verb families, 21 retail examples, historical generators, community integration and generalized lessons from authorized private research. |
| [PUBLICATION_AUDIT.json](https://github.com/CodaLilitari/jk-mots-cog-workshop/blob/b4b43f6ab1976f673598c90751ec6160da70c506/corpus/PUBLICATION_AUDIT.json) | Approved input identities, exact-version note gates, output hashes, exclusions and scope limits. |

The registry union has 539 verb spellings and 47 message spellings. Original JK has 451/41, original MotS 538/47; aliases count as spellings. The full source-specific explanation is in [engine findings](notes/engine-findings.md). Metadata lookup and availability checks work offline through the bundled skill scripts.

## Research checkpoint in 0.1.3

The [target contracts and lifecycle study](notes/target-contracts-and-lifecycle.md) updates the human guide and machine workflow with original MotS weapon-bin findings, bounded timer ownership, visibility/containment distinctions, model cleanup, multiplayer acknowledgements and extension resource contracts. Thirteen of SM HQ's 37 unique COG bodies have complete source reviews; the remaining 24 have availability checks only. Static review is not a retail or connected-client test.

The [Sneaky Hax III recovery record](https://github.com/CodaLilitari/jk-mots-cog-workshop/blob/b4b43f6ab1976f673598c90751ec6160da70c506/corpus/recoveries/sneaky-hax-iii.json) identifies its archived wrapper, recovered GOB and 46 COG members by hash and size. None of those 46 bodies has a complete functional review in this checkpoint. These identity records remain separate from the frozen catalog totals. No installer was executed and no third-party script body is redistributed. A recovered cheat package does not establish recovery of its author's COG maker.

Catalog and registry snapshots remain unchanged. [RESEARCH_UPDATE_0.1.3.json](https://github.com/CodaLilitari/jk-mots-cog-workshop/blob/b4b43f6ab1976f673598c90751ec6160da70c506/corpus/RESEARCH_UPDATE_0.1.3.json) records the maintained public-file revisions and publication boundary. Research is paused after this checkpoint.

## Previous checkpoint in 0.1.2

The [classic COG and binding study](notes/classic-cogs-and-binding-lessons.md) adds original analysis of public historical material and measured registries. The large catalog remains the frozen snapshot described above. Newly captured private material is not added to this public catalog or represented as a complete search of any server. Acquisition is paused at this release checkpoint; standalone fader variants, a separately identified CL0wN maker, the missing Sanctium package and uncensored SM Purgatory remain recovery gaps. Those gaps are not evidence of nonexistence.

## Optional local source import

Use your own installation or another source you are authorized to inspect. From the release root:

```text
python skill/jk-mots-cog-workflow/scripts/import_local.py --game jk --out PRIVATE_JK GAME_DIRECTORY
python skill/jk-mots-cog-workflow/scripts/import_local.py --game mots --out PRIVATE_MOTS GAME_DIRECTORY
python skill/jk-mots-cog-workflow/scripts/corpus.py --root PRIVATE_JK search MoveToFrame --kind cog
python skill/jk-mots-cog-workflow/scripts/corpus.py --root PRIVATE_JK show FULL_SHA256 --source
```

The importer accepts explicit files or recursively inventories the explicit directory for `.cog`, `.gob`, `.goo` and `.zip`. It verifies GOB directory/member bounds, ZIP CRCs, fixed expansion/depth limits and immutable object hashes. It never executes scripts or installers. Unsupported/encrypted containers and failed members are reported in `import-errors.json`; exit 2 means the import is incomplete. Choose a new empty output directory. The output is marked private, contains the original local paths in its provenance, and is not included by the public exporter.

For custom public packages, locate the original credited release through the bibliography, check its terms and obtain it through a supported download route. Import the exact local file and compare its SHA-256 with the catalog. A matching package hash proves identity with that recorded package, not game correctness or a redistribution license. The release does not silently fetch anything or resume research.

## Reproduce the public projection

Maintainers with the authorized source corpus and exact reviewed skill revision can run:

```text
python tools/export_public_corpus.py --private-corpus PRIVATE_CORPUS --source-skill SOURCE_SKILL --output PUBLIC_WORKSHOP
```

The exporter selects explicit public source lanes and fields, validates public URL hosts/query keys, omits private-only provenance and pins each reviewed authored note by hash. Changed notes require fresh review. Its final marker scan is defense in depth, not the basis for deciding what is safe to publish. Original release notes and portable helpers are maintained directly in this repository. Future exports must preserve the same private/public distinction.

Every raw artifact excluded for privacy or rights reasons remains excluded even when its lessons inform original technical notes. The public research does not imply a fresh retail launch, a verified multiplayer session, complete archive recovery or universal engine compatibility.
