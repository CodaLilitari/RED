# Advanced COG lessons from recovered systems

> Copied into RED from [CodaLilitari/jk-mots-cog-workshop@b4b43f6](https://github.com/CodaLilitari/jk-mots-cog-workshop/blob/b4b43f6ab1976f673598c90751ec6160da70c506/corpus/notes/case-lessons.md) (MIT; see [LICENSE](../LICENSE.md)). Regenerate with `tools/knowledge/import_knowledge.py` rather than editing this copy.

These are original technical notes derived from complete source reviews and package-integration studies. Some underlying material was supplied through authorized private research. No private messages, identifying communication metadata, source bodies or attachment links are reproduced here. The examples are deliberately generalized. Where the mechanism is an engine contract, the relevant public implementation can be inspected at the [pinned source revision](https://github.com/CodaLilitari/OpenJKDF2-MotS-Fixes/tree/b1be98bd4f170b5384cb71b15443a274aac6a937). These are source findings and test designs, not reports of game sessions.

## A successful call sequence needs a complete data contract

A storage system can reserve fixed-width records inside one COG heap. If a record occupies eight cells and three are header cells, its five payload slots are numbered 0 through 4. Accepting slot 5 writes into the next record. Review the formula, range checks, all callers and initialization together. A discovered off-by-one in a packaged but unbound file is a source defect; it is not proof that the default level executes it.

An indexed declaration sequence is not an allocated array. A model-pose system may organize parameters as repeated triples of model, position and orientation. Iterating the base with stride one mixes resource types and addresses the wrong fields; the natural item index must be multiplied by the record width. Because a COG assignment consumes stack operands without producing a C-style expression result in the pinned VM, a loop condition written as an assignment needs a separate fix and verification. Correct the index domain and the loop condition independently.

Preset APIs need equally careful documentation. A random preset choice made separately for each property request does not produce a stable random character: head, body, gloves and other fields can come from different selections. Resolve the choice once and pass the stable selection through the property sequence if a coherent preset is required. Track all writable settings through preview, acceptance, spawning, saving and reload. Narrow export masks can silently discard editable high bits even when the preview displays them correctly.

## Runtime model manipulation belongs to an explicit extension target

Stock retail verb tables do not include arbitrary model cloning, mesh-holder reassignment or custom string callbacks. A familiar stock item filename can contain a completely extension-dependent tool. Declare the exact extension/build, initialization sequence, required model assets and external helpers rather than assuming modern examples are portable to original JK or MotS.

Some inspected extension getters push several vectors, and a stack-exchange helper swaps complete tagged stack cells. This differs from an ordinary one-value expression. Read the complete push/pop sequence and every error branch; treating a three-result getter as a normal vector-returning call can leave operands for a later operation. The recovered partial extension source does not cover all older verbs or every external helper. Absence from that source is not proof of absence from the whole installed extension.

Direct model-node changes may affect all things sharing a model. Decide whether the operation owns a private clone or intentionally changes a shared resource. Review cache identity, capacity, eviction and lifetime before changing model pointers. A failed cache-search escape can find a reusable entry, then still enter the clone branch after the loop index changes. That establishes discarded reuse in the control flow; without the external clone implementation and actual lifetime observations it does not prove a leak or a runtime crash.

A preview object can become a different owner after acceptance. A message such as "apply current character" may target a preview before confirmation and the player afterward. Follow the point where the sender or target variable changes. Do not characterize the same command as always mutating the player merely because one later branch does so.

## State changes must restore what they actually changed

A temporary loading workaround that reclassifies actors must remember the exact affected things and their prior state. A later sweep over every corpse can also affect original level corpses and actors killed after the workaround began. Similarly, clearing invulnerability everywhere can remove a flag that the level intended to keep. Track membership, prior values and identity; do not infer ownership from the current type alone.

Temporary cameras and color effects share state with other systems. An additive color operation with zero arguments adds zero; it does not reset the accumulated state. A camera-shake setter can take decay deltas rather than a lifetime. Save and restore the actual state that the feature owns, and account for interruption by another camera or effect owner.

Changing a stock pulse interval changes all work executed by that pulse, including inventory drain or selection logic that may look unrelated to the new visual effect. Separate a fast cosmetic update from slower gameplay work where the design requires different cadence. In the pinned implementation, a due pulse can also replace the simple timer before timer cleanup is checked. Schedule cleanup through an independent state/timer design when that interaction matters; verify actual message order in the chosen build.

## Client/server pairs need authoritative state and a replay rule

A relay that forwards an activation and then emits a reply is not an authoritative lock unless it stores and validates the state transition. A guard only in the initiating activation handler can be bypassed by an unguarded reply handler. Model the whole protocol: request, accepted state, response, effect, completion and late-join reconstruction. Decide whether repeated or delayed messages are harmless.

A shared-key protocol with one undifferentiated trigger may be received by more than one placed key script. The source can look reasonable in isolation while actual JKL bindings cause multiple instances to react. Include the intended key or instance identity in the protocol, or use a deliberately shared global state with explicit semantics. Confirm both halves of every client/server pair are actually placed.

The generic parameters of a trigger are floats. They do not receive the typed thing-reference-to-GUID conversion used by another network path. A local thing index sent as a parameter is not automatically the same object on another client. Stock MotS provides explicit GUID lookup verbs; stock JK needs a different agreed identification method or a route already handling typed references. Do not claim correct remote ownership after a single-player test.

## Bit setters, time and geometry are independent concerns

`SetAdjoinFlags` adds bits in the inspected implementation. Applying a smaller mask after a larger one does not clear bits that were already set. Clear the specific bit when the intended transition removes a permission, and review the paired adjoin as well as the edited surface. An open movement bit does not alone establish that a physical door can be walked through: thing collision, geometry and other permissions still matter.

For pivoting doors, distinguish a movement duration from a speed, a timer measured from opening start from a fully-open hold time, and a fixed `Sleep` from actual movement completion. If opening takes four seconds and closing is scheduled ten seconds after opening begins, the nominal fully-open period is six seconds. Reversed motion, zero duration, obstruction and actual frame processing need explicit tests.

Timer scheduling does not suspend the current handler. Deliberate fallthrough can call a scheduling label immediately after a timer is set; deleting it as redundant changes behavior. Conversely, disabling a pulse does not end its current invocation. Put a control-flow exit where cleanup is meant to stop further sound, spawning or motion.

## Native-call availability is necessary but insufficient

A surface index and a material index may both be integers, yet passing one to the other's verb is a contract error. The same applies to a template handle passed to a thing-facing function, an array slot passed where a stored thing index is required, and a weapon ordinal passed as a bin. Trace the producer and consumer of each identifier, not merely its variable type or name.

The MotS weapon conversion boundary especially matters for ordinals at and above the energy-bin threshold. Use the reviewed mapping function and the actual ITEMS.DAT rather than a constant arithmetic shift. A Force power's active inventory flag and its activation clock are also different state. Clamp charged duration independently of a later mana decision; otherwise a rejected activation can still leave inconsistent deactivation behavior.

A missing event declaration can leave an otherwise valid label without an incoming engine route. A named event that is absent from the measured registry can still be accepted by generic symbol insertion, but that does not create a working handler. Test registration, alias binding, instance linkage and actual delivery as separate layers.

## Convert a source concern into an experiment

For each concern preserve the exact source and configuration identity, then state one observable prediction: repeated activation creates one owned object; the fifth payload slot stays inside the current record; a late join receives current door state; a failed creation does not start polling; accepting a preview restores camera ownership; closing clears only the intended adjoin bits. Run a normal path and the nearest discriminating counterexample. Record whether the result is lexical, source-probed, loaded in a named engine, or observed across separate clients.

These generalized lessons complement the public [retail pattern atlas](pattern-atlas.md), [verb contracts](verb-families.md), [language/execution guide](language-and-execution.md) and [BeefCaike integration study](beefcaike-cog-study.md). They do not assert that every source-level concern was active in a released level or that it caused a historical player's reported symptom.
