# JK / MotS COG workshop

> Copied into RED from [CodaLilitari/jk-mots-cog-workshop@b4b43f6](https://github.com/CodaLilitari/jk-mots-cog-workshop/blob/b4b43f6ab1976f673598c90751ec6160da70c506/skill/jk-mots-cog-workflow/SKILL.md) (MIT; see [LICENSE](LICENSE.md)). Regenerate with `tools/knowledge/import_knowledge.py` rather than editing this copy.

> This is the standalone workshop's agent skill, kept for its rules and evidence. Its `scripts/` commands belong to that workshop and are not part of RED. In RED, use `red-agent` and the skills in [docs/agent_skills](../../agent_skills/README.md).

Implement the requested behavior from a close, inspected example and verify it in the intended target. The bundled corpus contains original research notes, public bibliographic metadata and exact-byte identities. It intentionally excludes raw retail scripts, copied community packages and private communications. An optional user-owned local corpus can supply complete source bodies.

For a skill installed with the Workshop's `install-skill` command, begin substantive work with `python scripts/workshop.py version`. This delegates to the registered Workshop launcher and its portfolio check, at most once per 24 hours; a newer verified release can also refresh an unchanged installed public skill. If an update occurs, reread the skill and relevant references before relying on them. Honor offline or update opt-out preferences with `--offline`, `--no-auto-update`, `COG_WORKSHOP_OFFLINE=1`, or `COG_WORKSHOP_AUTO_UPDATE=0`. Direct corpus, inspection, import and report-draft helpers remain offline; reading this skill does not itself execute an update. If the launcher is unavailable, continue with local evidence and state the freshness limit; restore or reconnect the extracted Workshop instead of creating a new updater.

For unresolved or conflicting flag entries, read [the master flag findings](flag-discoveries.md) and `data/flag-discoveries.json` alongside the matching catalog row. This maintained reference has separate JKDF2, MotS and Indy target evidence. Original-executable findings, enhanced-source consumers and pinned Indy reconstruction observations must remain distinct. The human page and JSON use stable URLs; downloadable packages are dated snapshots.

## Choose the target and integration role

Distinguish original JK 1.01, original MotS, a specific patch, a specific enhanced-engine build, and cogext. Infer the target from the project; clarify when it changes the implementation. Identify whether the script is a placed level instance, template class/capture script, inventory/weapon/Force bin, static script, presentation helper, or explicit network protocol.

| Task | Evidence to start with |
| --- | --- |
| Flag/mask or numeric-mode question | Select the owning field in [flags and numeric values](flags.md), or retrieve the relevant family/bit from `data/flags.json`; do not load all source bodies |
| Door, elevator, objective, trap, camera | Same-game stock example, JKL parameter order, thing frames and resources; [pattern atlas](notes/pattern-atlas.md) |
| Weapon, inventory or Force behavior | Same-game script, ITEMS.DAT and class/template messages; [verb families](notes/verb-families.md) |
| Multiplayer behavior | Authority, execution location, verb-specific transport, object identity, late joins and cleanup; [language/execution](notes/language-and-execution.md) |
| JK ↔ MotS port | Both measured registries, call semantics, bins and matching source variants; [engine findings](notes/engine-findings.md) |
| Retail checksum rejection | Exact executable/content/load order and actual compiled inputs; [checksum evidence](notes/checksums-and-retail.md) |
| Generator, old custom code, or cheat analysis | Attributed source, complete control flow, native stack contract and target; [historical sources](notes/historical-sources.md) and [generator study](notes/triscuit-generator-engine.md) |

## Work from evidence to an implementation

1. **Retrieve a close precedent.** Run `python scripts/corpus.py search MoveToFrame --game jk --kind cog`. `show HASH` returns metadata; with an imported private local corpus, `show HASH --source` prints owned source. Public catalog records are discovery and identity evidence, not executable examples or compatibility approvals.
2. **Map the contract.** Record ordered nonlocal parameters, resources, linked objects, event aliases, sender/source/reference domains, state, suspension points and cleanup. Read complete relevant source when available. An absent native spelling can still parse; parser acceptance does not make it callable.
3. **Implement the requested behavior completely.** Preserve attribution and integration contracts; work on a copy of historical source. Use nested guards for potentially invalid operations: the pinned VM does not short-circuit logical operators. Instance symbols are shared across handlers and are not C stack locals.
4. **Run target preflight.** `python scripts/inspect_cog.py candidate.cog --target jk` or `--target mots` reports measured verb/message availability and lexical dependencies. `--strict` exits 2 for absent call/message spellings. This is not a compiler, argument checker, checksum predictor or proof of event delivery.
5. **Test in the actual target.** Verify loading, requested behavior, repeated/invalid input, and relevant death, respawn, save/load or cleanup paths. Multiplayer claims require separate connected clients and remote observations. Use [authoring/testing](notes/authoring-and-testing.md) and `assets/cog-test-plan.json` for a durable record.
6. **Deliver the source and integration.** State target game/build, required resources and bindings, installation/package location, attribution, executed checks and untested cases. Separate original-binary inspection, source probes, original retail execution and connected-client observations.

## Findings that materially change authoring

For static-world/item integration, use [static COG capacity](static-capacity.md) and `python -B scripts/audit_static_capacity.py static.jkl items.dat --reserve 8` with an explicitly chosen reserve. This original preflight, informed by SM Sith Lord's P68 report, counts each authored instance binding rather than unique script names. It does not assess script-table capacity, resource loading or runtime compatibility.

- The measured original tables expose 451 JK verbs/41 message spellings versus 538 MotS verbs/47 message spellings. Alias counts are not distinct event counts. MotS adds 88 verb spellings and omits JK's `GetCurInvWeapon`; it exposes `GetCurInvWeapon2`. A same-named verb can still have target-specific semantics.
- In the pinned source, JK `CreateThingAtPos` interprets its final vector as Euler rotation; MotS uses a look direction. `CreateThingAtPosOld` is MotS's JK-style source path. This is implementation evidence, not an all-version runtime test.
- A `local` symbol declaration, `0x40` local execution, `0x200` no-sync, explicit `SendTrigger`, and checksum acceptance are separate mechanisms. Generic float trigger parameters do not automatically convert a thing index to network identity.
- Native calls consume stack arguments and may push no result. `ReturnEx` sets a response without ending the handler. Unknown native calls can leave evaluated arguments on the stack; arbitrary-looking historical padding needs control-flow and stack analysis.
- A message alias can bind a different label (`message onStart=startup`). Both original binaries also register float globals `global0`–`global15`; these are not additional normal events. Check actual routes and labels.
- The inspected original checksum loops pass a bytecode word count as a byte length, hashing a prefix. Neither equal text length nor local flags establish compatibility. The pinned enhanced source disables the rejection branch and has a MotS personality callback difference; an enhanced join cannot prove retail acceptance.
- Resources have distinct domains: template versus thing, surface versus material, sound resource versus playback handle, weapon ordinal versus inventory bin. A clean availability report does not catch these errors.
- Check existence, loading and reachability separately. A packaged alternative can be unused, comments can promise retransmission absent from the body, and a future timer does not stop current fallthrough.

For classic God sabers, faders, or legacy level integration, read [classic COG and binding lessons](notes/classic-cogs-and-binding-lessons.md). Keep the bolt model, damage, victim-state edits, loaded bindings and network proof separate. Check complete handler flow and shared object ownership; `activate` is a valid original-game alias, while `timmer` is absent from both measured tables and `jkSyncForcePowers` is absent from JK 1.01.

Use [authoring decisions](notes/authoring-decisions.md) to select a discriminating check, [BeefCaike's integration study](notes/beefcaike-cog-study.md) for substantial community systems, and [generalized recovered lessons](notes/case-lessons.md) for advanced lifecycle, cache and protocol pitfalls.

For the newer weapon-domain, timer, visibility, SM HQ and model-ownership findings, read [target contracts and lifecycle](notes/target-contracts-and-lifecycle.md). In inspected original MotS, `GetCurInvWeapon2` already returns a bin; the studied enhanced binding differs. Validate the exact result domain before mapping or restoring it. Repeated timer IDs do not imply replacement, counted acknowledgements need participant/operation identity, and sentinel values need explicit branches. Keep current-local implementation evidence separate from pinned source and original-game execution. Acquisition of a historical pack is not completed analysis or retail compatibility.

For flags, distinguish the setter’s accepted integer mask from per-bit behavior. `data/flags.json` catalogs all 26 historical table families plus related domains, with exact source hashes and unresolved meanings. Source names and Jones annotations are not original-game support. `SetThingFlags` ORs, `ClearThingFlags` clears and neither returns a value in the inspected original JK/MotS wrappers; invalid getters return -1. A conditional sync request does not prove delivery. Prefer lifecycle operations for engine-maintained state. Use the [flag reference](flags.md) selectively, and preserve scalar-versus-mask and original-versus-enhanced distinctions.

## Portable retrieval and private data

Keep bulk research outside Git checkouts and outside the installed skill: downloads, imported sources, expanded archives, caches and test output belong in a separate user data directory. Do not create a junction back into a checkout. The importer and managed updater reject Git-contained output without invoking Git, including linked worktrees and resolved links. Use a focused catalog query for ordinary authoring; do not crawl, reimport, rebuild, recursively hash a private corpus, or run Git to answer a COG question. Import deliberately selected folders in bounded batches, and inspect an exit-2 partial-import report before using its counts. These limits address repository scan and memory pressure; they do not establish stability for every Codex build or machine.

The default `scripts/corpus.py` searches `data/` and these references, with no network. `COG_CORPUS_ROOT` or `--root PATH` selects another corpus; an existing full research corpus with `scripts/corpus_index.py` is delegated to for read-only stats/search/show. To import owned retail or authorized custom files locally, use `python scripts/import_local.py --game jk --out PRIVATE_DIRECTORY GAME_DIRECTORY` (or `--game mots`). It reads ZIP/GOB/GOO/COG files passively, bounds archive members and stores original COG bytes by hash. Do not publish that private output by default. See [retrieval and evidence grades](notes/research-and-retrieval.md).

The public skill is named separately so installation does not overwrite an existing private `jk-mots-cog-workflow` skill. Research notes can be extended from authorized private evidence in original words; raw private material and identifying communication metadata remain local.

## Optional discoveries and corrections

When an implementation produces genuinely new, additional or contradictory understanding, first compare it with the current corpus and record why it changes the existing account. Offer a local finding draft with exact game/build, source identity, reproduction, expected versus observed result, evidence grade, limits and credit. The intended report recipient is `lilitarikiralv@gmail.com`. Reporting is off by default. Use [the reporting workflow](https://github.com/CodaLilitari/jk-mots-cog-workshop/blob/b4b43f6ab1976f673598c90751ec6160da70c506/skill/jk-mots-cog-workflow/references/reporting.md) to prepare and review the exact recipient, subject, body and any proposed attachments. Never send automatically; sending requires the user's explicit per-report instruction through an available authorized email route. A prepared draft is not a sent report. Omit private source/capture material and identifying communication metadata from reports unless the owner explicitly authorizes those exact items for that recipient.
