# Target contracts and lifecycle: authoring findings for 0.1.3

> Copied into RED from [CodaLilitari/jk-mots-cog-workshop@b4b43f6](https://github.com/CodaLilitari/jk-mots-cog-workshop/blob/b4b43f6ab1976f673598c90751ec6160da70c506/corpus/notes/target-contracts-and-lifecycle.md) (MIT; see [LICENSE](../LICENSE.md)). Regenerate with `tools/knowledge/import_knowledge.py` rather than editing this copy.

These original notes extend the human guide and machine workflow. They distinguish original executable inspection, original retail script inspection, enhanced-engine source, package bindings, and archive inventory. None of the new studies reported original-game execution or a two-client multiplayer result. A source calculation is not observed damage; a selection, movement, creation, or synchronization request is not proof of completion.

## 1. Preserve weapon bins and selection ordinals as different values

**Evidence: original MotS executable inspection, compared with enhanced source and inventory definitions.**

The inspected original MotS registration of `GetCurInvWeapon2(player)` returns the current inventory bin. The inspected enhanced source binds that spelling differently and converts the bin to selection order. Identify the implementation behind the name rather than merely adding the MotS suffix.

For the inspected original MotS binary, save a valid getter result as a bin and restore that bin with `SelectWeapon`. Applying `GetWeaponBin` to that already-raw result introduces an incorrect second conversion. A candidate approach spanning the inspected original MotS and enhanced MotS implementations is to obtain an ordinal with `GetCurWeapon`, validate it, convert once with `GetWeaponBin`, and retain the validated bin. This is a target-test candidate, not a completed portable implementation.

Original MotS `SelectWeapon` and `SetCurInvWeapon` convert arguments at or below 10 through the selection mapper, but treat larger arguments as bins. Stock fists are ordinal 1/bin 121; the saber is ordinal 11/bin 131. Passing saber ordinal 11 directly supplies bin 11, not bin 131. These mappings come from inventory definitions; there is no universal addition of 120. `SetInvAvailable` uses bins without that selection conversion, so a JK loop over bins 1–10 does not cover MotS weapons 121–140.

Validate ownership and availability before selection. Preserve every availability flag changed by the feature, and test release, death, repeated events, and delayed or missing completion. A getter's null-lookup error branch is not a safety guarantee for arbitrary non-player things.

## 2. Extended timers need a renewal and ownership policy

**Evidence: enhanced timer wrappers and declarations, plus a separately inspected current enhanced event manager.**

The separately inspected current event manager is identified by SHA-256 `fa3202f22f8d0a434254153b7abf52ed8b41ad12d2071028a1db4ee145dc231f` (3,981 bytes), with header SHA-256 `abefc05635f739c161e1302cb6bf4a0724c4fa272c90f8e3cfe0ddb681c5c7e6` (1,235 bytes). The pinned snapshot lacked the event manager. Its complete timer wrappers and event declarations matched the current source after newline normalization. This supports joining their call contracts, but does not establish identical builds, ABI compatibility, or retail behavior.

Each successful `SetTimerEx` call allocates another event in the inspected manager. Reusing an ID does not replace the previous event; equal deadlines retain insertion order. `KillTimerEx` accepts positive IDs and removes all queued matches for the owning COG index. It does not select by a player parameter or remove a callback already detached for dispatch.

The event supplies its ID as the timer handler's sender ID and its two payloads as parameters 0 and 1. Use those fields rather than assuming a shared player variable still describes the request. A delayed numeric thing index is not durable object identity. Destruction/reuse and whole-world event-reset ordering still need review.

The shared pool has 256 slots. Creation can fail, and the wrapper provides no COG success result. Choose whether renewal cancels and replaces owned queued work, permits independent requests, or rejects renewal. Use an appropriate generation/state check for stale callbacks; cancellation cannot retract dispatched work or a handler that has already suspended itself.

## 3. Separate hazard traversal from effect cleanup

**Evidence: complete custom-source reviews and original-JK comparison scripts; no damage or audio runtime observations.**

A `return` inside a sector-occupant loop leaves the entire handler. If the first protected occupant triggers it, later unprotected occupants receive no damage calls during that invocation. A per-occupant skip requires a control-flow restructuring supported by COG; ordinary C `continue` is unavailable in the inspected grammar. Returning from one pulse does not disable future pulses.

Reading an occupant's type does not filter anything unless a condition uses it. An inventory-bin description in a header does not prove the body consults that bin. Report the calls reached, then separately establish damage eligibility, damage-type handling, attribution, and final health loss.

Treat duration, warning sound, looping audio, actor flags, and user data as separately owned resources. Older callbacks can expire a renewed effect. Overwriting a playback handle can lose access to an earlier loop; cancelling the timer that normally stops audio can remove its only cleanup path. An early death-state return can bypass expiry cleanup.

Restore prior shared state where required, or define a single owner. Distinguish player `killed` and `newplayer` handlers from a pickup's `respawn` handler. An inventory flag, actor flag, and user-data marker are different mechanisms.

## 4. Distinguish visibility, containment, and collision results

**Evidence: original-game registration tables and pinned enhanced wrapper/VM inspection.**

Both inspected original games register `HasLOS`. Original MotS also registers `FindSectorAtPos` and `IsSphereInSector`, absent from the measured JK 1.01 table. Visibility between things, a containing-sector query, and a first-hit raycast answer different questions. Searching for a verb named “trace” cannot establish absence of all related functionality. A projectile technique needs its own collision timing, ownership, cleanup, and result contract.

The inspected enhanced `HasLOS(a,b)` returns -1 for an invalid thing, and its VM treats -1 as true. Validate created handles first, then explicitly test `HasLOS(...) == 1`. Logical AND/OR evaluates both operands in that VM, so a combined condition is not a safe substitute for nested validation. These error-return semantics were not newly established in original binaries.

A historical sector-search technique illustrates several composition risks: unchecked ghost creation can feed the invalid-return path; an inclusive vertex-count loop obtains a zero-vector fallback at its extra index; enabling all adjoins afterward fails to preserve initially disabled adjoins. The extra cleanup-sector call instead takes a no-op path in the inspected wrapper. Preserve actual prior state and inspect synchronization before temporarily changing global geometry. Underlying collision/containment algorithms were outside this bounded review.

## 5. Match movement feedback and generator limits to bindings

**Evidence: complete source bodies, actual JK level bindings, and a limited enhanced pivot-wrapper review.**

SM HQ coverage is now **13 completely reviewed bodies out of 37 unique bodies**; 24 remain availability-only. That denominator describes this package, not every SM release or a tested MotS port.

The studied swing protocol counts arrivals without identifying the participant and movement. Feedback left over after opening can affect the next closing count in a conditional source trace. Missing feedback has no timeout or reconciliation path. Both inspected bindings use one moving leaf: hypothetical multi-leaf failures are not observed facts. Identify expected participants and current movement, reject duplicate/stale completion, and define recovery when completion never arrives.

The inspected enhanced `RotatePivot` wrapper uses the selected frame rotation directly, rather than subtracting the prior frame rotation. Valid selected frames were present. Its callee and original-game motion were not verified; this is a reason to inspect semantics, not silently rewrite rotations.

A bound `generate_num=-1` does not implement unlimited generation when the body unconditionally tests `cur_num >= generate_num` after incrementing. The reviewed binding requests pulse shutdown after its first qualifying attempt. Creation failure is unchecked, killed decrements unconditionally, and a restart target is unassigned. Handle sentinels explicitly, validate creation before capture/counting, and bind restart targets. The visibility condition uses `IsThingVisible`; do not substitute an assumed line-of-sight meaning.

## 6. Coordinate model restoration with other owners

**Evidence: complete disguise/client/suit-change source and episode wiring reviews.**

A disguise can save a model, install another, then overwrite a legitimate skin change during restoration. All eight levels' COG/player wiring was checked in the reviewed episode, where suit-changing instances coexist with disguise. The suit-change body updates model references without clearing disguise state or notifying its cleanup. A changed appearance does not itself mean the power was cancelled.

Define ownership and transfer explicitly. An alternate client can handle base effects without sharing every main-client behavior. Enemy-contact cancellation remained untraced across eleven reviewed bodies; that does not prove absence from every bound script or engine path.

Separately, the pinned enhanced `SetThingModel` requires an existing render model. A model-less thing reaches its -1 branch instead of receiving an initial model. Supply that initial model through the template. This is an enhanced-source contract, not new original-binary or runtime evidence.

## 7. Dynamic POV is a separate extension target

**Evidence: the [inspected dynamic-POV revision](https://github.com/LadyEebs/OpenJKDF2-Experimental/tree/d4c5145ebebfeaca0bdffb008844472b73095459), with source/resource integration inspection.**

The examined weapon/camera registrations and sprite declaration, loading, and stack conversion depend on `DYNAMIC_POV`, outside MotS-only registration branches. Keep these extensions out of stock allowlists. A later matching implementation does not establish an older executable's identity.

- `jkGetMuzzleOffset(thing, fallback)` can retain the fallback while clearing shared cache on nonlocal/multiplayer branches. It does not recompute the pose. Preserve a useful fallback and account for rendering.
- Zero idle-waggle smoothing preserves accumulated sway in the inspected recurrence. A parameter setter is not necessarily a reset. Negative velocity scale deliberately selects a different branch.
- Reassigning a valid POV sprite resets scale and cancels the shared flash timer, even when unchanged. The wrapper rejects null sprites. Assign before scaling; actor/player type alone does not guarantee valid player information.
- Flash presentation requires animation marker 3 and a `k_muzzle` node; this revision uses a 60 ms expiry. Assignment alone does not display a flash. Zero scale is not a validated disable method: a conditional light calculation divides by a scale-dependent radius.
- `SetPOVWaggle(vector, speed, smooth)` has no player argument and changes shared camera state, distinct from weapon waggle. Its phase clamps current time to 0.02 before multiplying by speed, while smoothing uses uncapped delta time. These are source consequences to test, not reproduced visual defects.

Pin the build and compile features. Test selection, interruption, resources, aiming conditions, local/remote use, and cleanup independently.

## 8. Retain API corrections and stock Force differences

**Evidence: original binary or retail source where specified; otherwise enhanced source.**

Original JK 1.01 and MotS `HeapNew` body inspection confirms one size argument and no pushed result, correcting a historical two-argument/return description. Residual stack data is not a return value.

Stock Force Speed and Protection bind to bins 22 and 29, with armor bin 61 capped at 200 in both games. Protection requests rank times 75; stored 75/150/200/200 follows from the inspected clamp rather than runtime measurement. MotS's protected-energy branch tests exact damage type 2, so a composite type containing that bit is not automatically equivalent. Its armor subtraction result is a callback-local calculation, not final health loss.

JK/MotS differ in affordability, spending exemptions, decay limits, flag changes, and bubble handling. Speed cleanup does not explicitly cancel its timer in the reviewed bodies. Port the activation/expiry/dispatcher contract, not just an effect flag.

The existing workflow already covers multi-result extension getters and shared models. Add the specific warning that inspected extension `HeapLoad()`/`HeapSave()` take no filename argument and push no status, while persisting raw typed VM slots. Resource/pointer values are not thereby a portable save format. A partial extension source cannot establish absence from the whole extension.

## 9. Report archive inventory separately from understanding

Sneaky Hax III is recovered as a RAR self-extracting wrapper whose named GOB was decoded statically. The historical executable was not run. The GOB has **96 members, including 46 acquired COG source members totaling 204,657 bytes; those 46 bodies remain unreviewed**. All member extents and hashes were checked.

Against the existing Sneaky Hax II inventory, 35 normalized same-name COG pairs have different hashes; no III COG hash matches any II COG hash under another name. Byte differences do not establish new algorithms or gameplay behavior. The readme's undetected/normal-game claims remain author testimony. This package does not resolve the separately attributable COG-maker gap.

Nonprivate identities:

- Wrapper: 320,253 bytes; SHA-256 `9a4c256a3273e04147604e4221447def9db8ea92000de891d9f3685b3798262b`.
- GOB: 3,236,545 bytes; SHA-256 `5664ded2ec20f4d2c2f91d83788a3893087926566e7b36bf2eef1502d398d1db`.

A differently packaged script with a similar Damage Control title does not establish recovery of the separately cataloged archive. Attribution, archive identity, complete review, integration, and runtime validation remain separate claims.

## Applying the update

Use the [pinned enhanced source](https://github.com/CodaLilitari/OpenJKDF2-MotS-Fixes/tree/b1be98bd4f170b5384cb71b15443a274aac6a937) only for claims attributed to that revision, not as a substitute for original-binary evidence or the separately examined event manager.

Record the target, identifier domains, complete return contract, event owner, saved state, bindings, and cleanup. Pair the normal path with a discriminating case: invalid creation, repeated activation, stale completion, exhausted capacity, changed model ownership, or interruption. Preserve evidence separately from repairs, and label reconstructed traces, parser checks, source findings, archive inventory, and actual game observations accurately.
