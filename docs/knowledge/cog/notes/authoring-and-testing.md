# Authoring and testing COG behavior

> Copied into RED from [CodaLilitari/jk-mots-cog-workshop@b4b43f6](https://github.com/CodaLilitari/jk-mots-cog-workshop/blob/b4b43f6ab1976f673598c90751ec6160da70c506/corpus/notes/authoring-and-testing.md) (MIT; see [LICENSE](../LICENSE.md)). Regenerate with `tools/knowledge/import_knowledge.py` rather than editing this copy.

> Public research edition. This is original authored analysis, not a source-code dump. References marked "retained local evidence" were inspected during research but their raw artifacts are not distributed. Binary identities, measured registries and public package identities are included; source execution and retail gameplay remain different evidence grades.

## Before changing source

Identify the actual entry path. A level-instance COG takes ordered values from JKL; a template class/capture COG receives thing-related events; an inventory COG is assigned through a bin; a static script is loaded in a different world context. Read the mount precedence and final loaded filename, not just the file edited on disk. Keep stock originals intact and use a separate test episode/mod directory.

For each relevant message, write down who causes it, what `GetSenderRef`, `GetSourceRef`, `GetSenderID` and parameter verbs mean in that dispatch, and what may be invalid after a sleep or thing destruction. Do not assume every message uses the same actor as its source. Treat resource indices, world indices and GUIDs as different concepts.

Trace the requested state transition: initial state, valid event, guard, state update, visible effect, completion and cleanup. For doors/movers, inspect frame ordering, travel speed, arrival, blocking and repeat activation. For weapons and Force, inspect selection/deactivation, bin availability, cooldown, fire rate, projectiles and respawn. For objectives, inspect per-player versus world state, progression, save/load and repeated triggers.

## Multiplayer design

Choose who owns the gameplay change. Inventory, damage, world movement, visual/audio effects and UI feedback may follow different networking paths. A local script flag does not make every verb local. Inspect both dispatch behavior and the individual verb's synchronization. Use a documented protocol identifier and payload contract for `SendTrigger`/message exchanges; account for sender validation, idempotence and late joins.

Avoid multiplying authoritative effects by running the same damage/spawn code independently on every peer. Conversely, do not rely on a server-only visual change to reach clients without the relevant replication path. Preserve existing protocol conventions when modifying a working mod. An arbitrary change to a trigger number can silently break peer scripts.

## Validation ladder

1. **Lexical review:** declarations, messages, labels, flags, known verbs, obvious missing resources. The helper strips comments/strings for call discovery but is not a complete syntax checker.
2. **Target registration and signature review:** use the measured retail tables, stock calls, historical specs and source implementations. Availability alone does not prove argument order or semantics. Enhanced engine verbs need a declared dependency.
3. **Load/parse:** capture the target's diagnostic output and verify the intended script actually loaded. A script in an unused archive is not tested.
4. **Behavior:** test the requested state transition and a meaningful failure/repeat case. Observe actual world/thing state as well as the visible effect where instrumentation exists.
5. **Lifecycle:** test interruption, object removal, death/respawn, level transition or save/reload when these can affect the script's role.
6. **Connected clients:** establish a clean baseline, join with separately identified content, trigger the event from each relevant side, verify remote observations, then exercise late join and cleanup. A hosted game with one player is not a two-client result.

Use source probes to isolate hypotheses, and label them as source probes. Use original-binary analysis to locate behavior, and label it as static evidence. Neither replaces executed multiplayer tests. Preserve failures as counterexamples, including their exact input and target identities.

## Useful experiments for checksum claims

Make one variation per candidate: comments only; whitespace only; early literal; late literal; symbol default; declaration insertion; new handler; flags; static script; template field; vertex; MotS personality file. Compare compiled layout as well as source text. Run the original candidate and the edited candidate against an unchanged peer, then a peer with matching edits. Log the seed, checksummed inputs and rejection path when the harness can observe them. Do not infer a universal rule from one unchanged checksum or accepted join.

## Reading cheats and generators

Classify the actual mechanism: altered local presentation, inventory/Force state, movement/physics, spawning/projectiles, network message abuse, patched executable behavior, or a generator that emits ordinary COG. Keep a historical author's labels separate from what the source does. Use private/offline test environments for disruptive examples. Read self-extracting archives with a static extractor and preserve warnings; do not execute unknown binaries to obtain their scripts.

A generator's UI label is not a semantic specification. Preserve its source/template and inspect generated output, especially fixed symbol slots, message labels, embedded templates and target-specific verbs. When executable-only tools cannot be run safely or do not survive, retain their archived documentation and make the limit explicit.
