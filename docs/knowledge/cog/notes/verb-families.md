# Practical COG verb families

> Copied into RED from [CodaLilitari/jk-mots-cog-workshop@b4b43f6](https://github.com/CodaLilitari/jk-mots-cog-workshop/blob/b4b43f6ab1976f673598c90751ec6160da70c506/corpus/notes/verb-families.md) (MIT; see [LICENSE](../LICENSE.md)). Regenerate with `tools/knowledge/import_knowledge.py` rather than editing this copy.

> Public research edition. This is original authored analysis, not a source-code dump. References marked "retained local evidence" were inspected during research but their raw artifacts are not distributed. Binary identities, measured registries and public package identities are included; source execution and retail gameplay remain different evidence grades.


These 16 families are a working reference for authoring and review, not an exhaustive API dictionary. Signatures were reconstructed by reading the complete pinned implementations, including wrappers and conditional pops; arguments below are in **COG call order**, not C helper order or stack-pop order. The source revision is `b1be98bd4f170b5384cb71b15443a274aac6a937`. The original source receipt (retained local evidence) and three supplemental complete source files (retained local evidence) preserve exact evidence. The original-binary tables establish spelling availability; the semantics below describe this pinned source unless expressly stated otherwise. They do not certify every original executable's behavior.

Notation: `thing`, `sector`, `surface`, `template`, `sound`, `keyframe`, and `cog` are different reference domains. `flex` is a numeric value; a vector is a three-component vector value. `void` means the inspected wrapper pushes no return value, so do not assign it to a variable. An invalid-input path that pushes nothing is explicitly called out. Unless labeled **MotS**, the listed spellings occur in both measured original-game tables. A source synchronization gate refers to `COG_SHOULD_SYNC`: output stream present, no `0x200` no-sync flag, and not startup/shutdown; it does not describe every network path in the engine.

## 1. Message context and explicit calls

```text
GetSenderRef() -> int/reference       GetSenderType() -> int
GetSourceRef() -> int/reference       GetSourceType() -> int
GetSenderID() -> int                  GetParam(index) -> flex
SendMessage(targetCog, message) -> void
SendMessageEx(targetCog, message, p0, p1, p2, p3) -> flex on a valid route
ReturnEx(value) -> void
```

Reference and type belong together: a surface sender is not a player thing. `GetSenderID` is the link/timer identifier, not a thing index. `GetParam` accepts indices 0–3 and pushes `-9999.0` outside that range. `SendMessageEx` forwards the caller's current source fields, identifies the caller as COG sender, and uses link ID zero. If the target COG or message range is invalid, its wrapper pushes **nothing**. A valid call pushes the dispatcher return value; a forwarded network call is not a guaranteed synchronous remote return. `ReturnEx` changes that field but does not stop execution: use `return;` separately. Message-context fields and shared symbols can change during reentry or sleep.

Full source: [sithCogFunction.c](https://github.com/CodaLilitari/OpenJKDF2-MotS-Fixes/blob/b1be98bd4f170b5384cb71b15443a274aac6a937/src/Cog/sithCogFunction.c), lines 41–64, 532–555 and 692–711; [sithCog.c](https://github.com/CodaLilitari/OpenJKDF2-MotS-Fixes/blob/b1be98bd4f170b5384cb71b15443a274aac6a937/src/Cog/sithCog.c), lines 1247–1457. Read the producer of each engine message before interpreting its parameters.

## 2. Suspension, pulse and timers

```text
Sleep(seconds) -> void
SetPulse(seconds) -> void
SetTimer(seconds) -> void
SetTimerEx(seconds, timerID, p0, p1) -> void
KillTimerEx(timerID) -> void
```

`Sleep` suspends the current continuation; another handler may still enter the same COG instance. `SetPulse` controls one repeating pulse, and `SetTimer` replaces the simple timer deadline. Nonpositive values disable those two facilities. `SetTimerEx` converts seconds to integer milliseconds and creates a separate event when that result is nonnegative; its timer ID arrives via `GetSenderID`, with the two values in `GetParam(0/1)`. The inspected `KillTimerEx` searches only IDs greater than zero and removes all matching queued timer events owned by the current COG. Use positive IDs and explicit state guards when cancellation or repeat activation matters.

Full source: [sithCogFunction.c](https://github.com/CodaLilitari/OpenJKDF2-MotS-Fixes/blob/b1be98bd4f170b5384cb71b15443a274aac6a937/src/Cog/sithCogFunction.c), lines 82–113 and 317–422; [sithCog.c](https://github.com/CodaLilitari/OpenJKDF2-MotS-Fixes/blob/b1be98bd4f170b5384cb71b15443a274aac6a937/src/Cog/sithCog.c), lines 1704–1773; [sithCogExec.c](https://github.com/CodaLilitari/OpenJKDF2-MotS-Fixes/blob/b1be98bd4f170b5384cb71b15443a274aac6a937/src/Cog/sithCogExec.c), lines 289–319 and 1198–1231.

The separate 3 October event-manager review found that successful repeated extended-timer IDs coexist. Cancellation removes queued events matching the current COG and a positive ID; it does not match a player payload or cancel a callback already removed for dispatch. A 256-entry shared pool can reject an event while `SetTimerEx` supplies no COG success result. This manager was inspected from a separately hashed current-local source file, absent from the pinned snapshot; matching pinned/current wrapper bodies do not make it original-retail evidence. Plan renewal, stale callbacks and effect/audio ownership explicitly. See [the lifecycle update](target-contracts-and-lifecycle.md).

## 3. Vectors and scalar operations

```text
VectorNorm(vector) -> vector
VectorDot(vectorA, vectorB) -> flex
VectorScale(vector, scale) -> vector
```

`VectorNorm` returns a vector, not its original length. `VectorScale` takes the scalar second. These wrappers delegate their arithmetic to vector helpers; they do not apply a game's world-to-display distance conversion. Distinguish an orientation expressed as Euler angles from a look direction and from a world position, even though all three can be represented as vectors. Use actual vector values such as `'0 0 1'`; a quoted string is a different value type. The language's ordinary logical operators have separate integer-pop and non-short-circuit behavior, so use explicit numerical comparisons when guarding vector-dependent operations.

Full source: [sithCogFunction.c](https://github.com/CodaLilitari/OpenJKDF2-MotS-Fixes/blob/b1be98bd4f170b5384cb71b15443a274aac6a937/src/Cog/sithCogFunction.c), lines 463–502 and 738–746; language and execution (retained local evidence) covers the expression VM.

## 4. Physics and object identity

```text
SetThingVel(thing, velocity) -> void
ApplyForce(thing, impulseVector) -> void
GetThingGUID(thing) -> int             [MotS]
GetGUIDThing(guid) -> thing/-1         [MotS]
```

Both physics wrappers require a physics-moving thing. `SetThingVel` replaces velocity; `ApplyForce` delegates to code that adds the supplied vector divided by positive mass. The latter also has a vertical-detach threshold and a fork-specific local-noclip early return. Both wrappers mark position synchronization under the COG gate. They do not move a path-based elevator by setting its path frame. `GetThingGUID` returns `-1` for an invalid thing; `GetGUIDThing` resolves a GUID to the current local thing index or `-1`. A saved numeric thing index is not durable identity after removal and slot reuse. Generic trigger float parameters do not automatically convert thing indices into GUIDs.

Full source: [sithCogFunctionThing.c](https://github.com/CodaLilitari/OpenJKDF2-MotS-Fixes/blob/b1be98bd4f170b5384cb71b15443a274aac6a937/src/Cog/sithCogFunctionThing.c), lines 870–900 and 2021–2042; [sithPhysics.c](https://github.com/CodaLilitari/OpenJKDF2-MotS-Fixes/blob/b1be98bd4f170b5384cb71b15443a274aac6a937/src/Engine/sithPhysics.c), lines 207–225; [sithDSSCog.c](https://github.com/CodaLilitari/OpenJKDF2-MotS-Fixes/blob/b1be98bd4f170b5384cb71b15443a274aac6a937/src/Dss/sithDSSCog.c), lines 10–111.

## 5. Creation, orientation and destruction

```text
CreateThing(template, locationThing) -> thing/-1
CreateThingAtPos(template, sector, position, orientation) -> thing/-1
DestroyThing(thing) -> void
```

`CreateThing` requires a valid location thing with a live type and sector, plus a template. `CreateThingAtPos` requires a valid template and sector. Both creation paths return `-1` when creation fails and use the COG gate for network creation. The template's model/sprite insertion offset participates in the final position.

`SetThingModel(thing, model)` in this pinned source returns the previous model ID on its valid path, but only calls the model setter when the thing already has a render model. A model-less thing takes the -1 branch without receiving its first model. Put the initial model in the template instead of assuming this verb fills a blank `_structure`. Inspect `sithCogFunctionThing.c`, lines 1136–1165. The statue repair exercise (retained local evidence) checks stock template inheritance and preserves the original thirty parameter slots; it is source-checked, not runtime-certified.

The final vector in **JK source mode** is passed to a rotation-matrix builder; in **MotS source mode** the object is created at identity rotation, and a nonzero final vector is normalized as a look direction. MotS's offset is initially added without the requested look-direction rotation; JK transforms the offset with the Euler rotation. MotS `CreateThingAtPosOld` is registered to the JK-style helper. This is a porting decision with source evidence, not a tested assertion about every retail patch. `DestroyThing` may send a destruction record under the gate, then removes the local thing; clear or revalidate references held elsewhere.

Full source: [sithCogFunctionThing.c](https://github.com/CodaLilitari/OpenJKDF2-MotS-Fixes/blob/b1be98bd4f170b5384cb71b15443a274aac6a937/src/Cog/sithCogFunctionThing.c), lines 39–59, 129–272, 328–340 and 2638–2652. Inspect template inheritance, class/capture COGs, and the actual stock variant before choosing a replacement.

## 6. Damage and return contracts

```text
DamageThing(victim, damageAmount, damageFlags, attacker) -> flex, or -1
```

The inspected wrapper requires a positive damage amount and a valid victim. An invalid attacker falls back to the victim. It emits a network damage operation under the COG gate, calls the engine damage routine, and returns that routine's numeric result. Otherwise it pushes integer `-1`. Do not interpret the return as the victim's new health without tracing the delegated damage routine. A stock actor's `damaged:` handler can transform damage through `GetParam` and `ReturnEx`; the handler's sender/source contract differs from the argument order of this verb. Duplicating authoritative damage on several clients can multiply effects.

Full source: [sithCogFunctionThing.c](https://github.com/CodaLilitari/OpenJKDF2-MotS-Fixes/blob/b1be98bd4f170b5384cb71b15443a274aac6a937/src/Cog/sithCogFunctionThing.c), lines 274–295; read the same-game actor COG and its caller alongside the damage routine.

## 7. Path movement and waiting

```text
MoveToFrame(pathThing, frameIndex, speed) -> void
StopThing(thing) -> void
WaitForStop(pathThing) -> void
```

`MoveToFrame` requires path movement and a frame below the loaded-frame count; the wrapper does not itself reject negative frames, so validate both bounds in authoring. It multiplies speed by `0.1`; zero after that conversion becomes `0.5`. It schedules path movement and emits a path record under the COG gate. `WaitForStop` suspends only when a valid path thing currently has one of its low two movement bits set; it is not a general wait for physics velocity to become zero. `StopThing` stops a path mover and may transmit that stop, while its physics branch resets movement without that wrapper-level path transmission. Stock doors use `arrived`/`blocked` state handling for a reason: movement completion, obstruction and user activation are separate events.

Full source: [sithCogFunctionThing.c](https://github.com/CodaLilitari/OpenJKDF2-MotS-Fixes/blob/b1be98bd4f170b5384cb71b15443a274aac6a937/src/Cog/sithCogFunctionThing.c), lines 364–379, 494–512 and 551–568.

## 8. Sector/surface lighting and material cels

```text
SetSectorLight(sector, extraLight, duration) -> void
SetSurfaceLight(surface, extraLight, duration) -> void
SetWallCel(surface, celIndex) -> previousCel/-1
```

The light wrappers require a valid reference and nonnegative target light. Duration zero directly sets extra light and queues synchronization under the COG gate; nonzero duration enters a delegated animation path. Do not infer identical timing/replication behavior from the immediate branch alone. `SetWallCel` needs a material and accepts `-1 <= celIndex < material.num_texinfo`; it returns the **previous** cel on success and `-1` on failure. A return of `-1` is therefore ambiguous if the old cel itself was `-1`. These calls affect different domains; applying sector lighting is not the same operation as changing a surface material's cel.

Full source: [sithCogFunctionSector.c](https://github.com/CodaLilitari/OpenJKDF2-MotS-Fixes/blob/b1be98bd4f170b5384cb71b15443a274aac6a937/src/Cog/sithCogFunctionSector.c), lines 91–113; [sithCogFunctionSurface.c](https://github.com/CodaLilitari/OpenJKDF2-MotS-Fixes/blob/b1be98bd4f170b5384cb71b15443a274aac6a937/src/Cog/sithCogFunctionSurface.c), lines 181–205 and 479–501.

## 9. Sound resources versus playback handles

```text
PlaySoundThing(sound, thing, volume, minDistance, maxDistance, flags) -> handle/-1
PlaySoundLocal(sound, volume, pan, flags) -> handle/-1
StopSound(handle, fadeSeconds) -> void
```

The returned handle identifies a playing channel, not the loaded sound resource. Save that handle when later fading or stopping it. The thing wrapper scales nonnegative distance arguments by `0.1`; negative minimum/maximum select internal defaults `0.5`/`2.5`, and the maximum is raised to at least the minimum. Volume is clamped to `[0,1.5]`. `0x80` follows the thing; otherwise a valid thing supplies a fixed position. An invalid thing falls back to unpositioned playback in this implementation. This wrapper can transmit under the COG gate.

`PlaySoundLocal` clamps pan to `[-1,1]`, strips following/absolute positioning bits, and does not call the network sound sender. It still executes wherever the COG handler runs, so gate it to the intended listener. `StopSound` takes a playback handle and sends a stop under the COG gate when a live channel exists; positive fade time schedules a fade, otherwise it stops immediately.

Full source: [sithCogFunctionSound.c](https://github.com/CodaLilitari/OpenJKDF2-MotS-Fixes/blob/b1be98bd4f170b5384cb71b15443a274aac6a937/src/Cog/sithCogFunctionSound.c), lines 22–105, 161–200 and 250–272; [types_enums.h](https://github.com/CodaLilitari/OpenJKDF2-MotS-Fixes/blob/b1be98bd4f170b5384cb71b15443a274aac6a937/src/types_enums.h), lines 768–769.

## 10. Inventory and Force-bin lifecycle

```text
SetInv(player, bin, amount) -> void
ChangeInv(player, bin, delta) -> resultingAmount, or 0
ActivateBin(player, delaySeconds, bin) -> void
```

These require a player and appropriate player-inventory state. `SetInv` pushes no result even though its delegated setter returns one; `ChangeInv` pushes the resulting clamped amount, or zero for an invalid wrapper route. The inventory implementation clamps to ITEMS.DAT minimum/maximum and dispatches a `changed` message when the amount changes. Such dispatch can reenter COG logic.

In MotS mode, `SetInv`/`ChangeInv` first map bin arguments below `SITHBIN_ENERGY` (11) through the weapon-selection-order conversion. Higher inventory bins remain raw bin IDs. `ActivateBin` pops arguments in a different pattern: player, delay, bin; it requires nonnegative delay and does not perform that same remapping in its wrapper. Read the same-game bin declaration and stock Force/weapon lifecycle before treating every numeric identifier as interchangeable.

Full source: [sithCogFunctionThing.c](https://github.com/CodaLilitari/OpenJKDF2-MotS-Fixes/blob/b1be98bd4f170b5384cb71b15443a274aac6a937/src/Cog/sithCogFunctionThing.c), lines 777–815; [sithCogFunctionPlayer.c](https://github.com/CodaLilitari/OpenJKDF2-MotS-Fixes/blob/b1be98bd4f170b5384cb71b15443a274aac6a937/src/Cog/sithCogFunctionPlayer.c), lines 493–507; [sithInventory.c](https://github.com/CodaLilitari/OpenJKDF2-MotS-Fixes/blob/b1be98bd4f170b5384cb71b15443a274aac6a937/src/Gameplay/sithInventory.c), lines 286–327 and 516–574; [types_enums.h](https://github.com/CodaLilitari/OpenJKDF2-MotS-Fixes/blob/b1be98bd4f170b5384cb71b15443a274aac6a937/src/types_enums.h), line 954.

## 11. Weapon selection and projectiles

```text
GetCurWeapon(player) -> selectionIndex/-1
GetCurInvWeapon(player) -> bin/-1         [original JK spelling]
GetCurInvWeapon2(player) -> bin/-1        [inspected original MotS; enhanced binding differs]
GetWeaponBin(selectionIndex) -> bin/-1   [MotS]
FireProjectile(shooter, template, fireSound, mode, fireOffset, aimError,
               scale, scaleFlags, autoaimFov, autoaimMaxDistance) -> thing/-1
FireProjectileData(the same ten arguments, extraData) -> thing/-1   [MotS]
```

The inspected original MotS registry binds `GetCurInvWeapon2` to the raw inventory-bin getter at `0x5551d0`. It does not apply weapon-ordinal conversion. The nearby converting function at `0x555190` is not that registration. The pinned enhanced source instead binds the name to a getter that converts the bin to selection order in MotS mode. Keep these two implementations separate.

`GetCurWeapon` returns an ordinal in the inspected original MotS binary; `GetWeaponBin` maps an ordinal through weapon-marked inventory definitions. Stock fists map ordinal 1 to bin 121 and the saber maps ordinal 11 to bin 131. This is not a universal `+120` rule. Original MotS `SelectWeapon` and `SetCurInvWeapon` convert arguments `<=10` as ordinals; larger values are treated as bins. Passing saber ordinal 11 directly therefore supplies bin 11, not the saber. `SetInvAvailable` takes a bin without that conversion.

For that original MotS executable, retain `GetCurInvWeapon2(player)` as a bin and restore a validated, available bin through `SelectWeapon`; do not map that result again. For an adaptation spanning the original target and this fork, obtain a validated ordinal from `GetCurWeapon`, convert it once with `GetWeaponBin`, and preserve the bin. A request to select a weapon does not prove the transition completed. See [the target-contract update](target-contracts-and-lifecycle.md) for evidence grades and test boundaries.

`FireProjectileData` pops the **last** extra integer before entering the shared ten-argument helper. That helper requires a valid shooter, delegates projectile/autoaim/template handling, truncates scale flags to 16 bits, and returns the new thing index or `-1`. Availability says nothing about whether the selected template, POV animation, fire sound and ammunition lifecycle agree. Preserve the same-game stock weapon's activated/fire/deactivated/selected/deselected contract.

Full source: [sithCogFunction.c](https://github.com/CodaLilitari/OpenJKDF2-MotS-Fixes/blob/b1be98bd4f170b5384cb71b15443a274aac6a937/src/Cog/sithCogFunction.c), lines 1149–1185 and 1403–1430; [sithCogFunctionThing.c](https://github.com/CodaLilitari/OpenJKDF2-MotS-Fixes/blob/b1be98bd4f170b5384cb71b15443a274aac6a937/src/Cog/sithCogFunctionThing.c), lines 1482–1513; [sithInventory.c](https://github.com/CodaLilitari/OpenJKDF2-MotS-Fixes/blob/b1be98bd4f170b5384cb71b15443a274aac6a937/src/Gameplay/sithInventory.c), lines 286–327. The enhanced `GetCurInvWeaponMots` wrapper calls the conversion without assigning its result; do not conflate that wrapper with the ordinary implementation.

## 12. AI destinations, modes and alignment

```text
AISetMovePos(actor, position) -> void
AISetLookPos(actor, position) -> void
AISetMode(actor, flagMask) -> void
AISetAlignment(actor, alignment) -> void     [MotS]
```

Destination/look calls require a valid vector, AI control type and actor state. Their wrappers pop the thing only if vector conversion succeeds; an invalid vector may therefore leave the earlier argument on the stack. `AISetMode` ORs flags into the existing mode and emits an AI event only if it changes. It does not replace the entire flag word. The fork's no-AI-events debug flag suppresses these ordinary setters.

`AISetAlignment` writes `actor->pClass->alignment`, not a standalone per-thing field, so inspect whether the class object is shared before assuming a change affects only one actor. It also has a different wrapper path from the debug-guarded setters. Scripts that mix AI steering with keyframe/path movement need an explicit owner for position and facing.

Full source: [sithCogFunctionAI.c](https://github.com/CodaLilitari/OpenJKDF2-MotS-Fixes/blob/b1be98bd4f170b5384cb71b15443a274aac6a937/src/Cog/sithCogFunctionAI.c), lines 24–46, 133–160, 213–243 and 554–562.

## 13. Camera focus and keyframe playback

```text
SetCameraFocus(cameraIndex, thing) -> void
SetCurrentCamera(cameraIndex) -> void
PlayKey(thing, keyframe, priority, flags) -> track/-1
```

Camera selection and focus are separate operations, and their source wrappers are local presentation calls without a COG network-send gate. The normal checked range is 0–6; the fork's QOL path remaps index 7 and can restore local-player focus, which is not a portable retail contract. Save and restore the prior focus/camera when building a temporary cutscene or scope view.

`PlayKey` requires a thing, puppet and keyframe. It passes priority as a low bound and priority+2 as the high bound, while the last argument is **flags**, despite the wrapper's misleading local variable name `trackNum`. It returns the created/reused track or `-1`, may stop an existing path movement, and may transmit under the COG gate. In MotS mode it rejects the local player when health is below 1. Do not pass a previous track handle as the flags argument.

Full source: [sithCogFunction.c](https://github.com/CodaLilitari/OpenJKDF2-MotS-Fixes/blob/b1be98bd4f170b5384cb71b15443a274aac6a937/src/Cog/sithCogFunction.c), lines 814–833 and 873–892; [sithCogFunctionThing.c](https://github.com/CodaLilitari/OpenJKDF2-MotS-Fixes/blob/b1be98bd4f170b5384cb71b15443a274aac6a937/src/Cog/sithCogFunctionThing.c), lines 1063–1109; [sithPuppet.c](https://github.com/CodaLilitari/OpenJKDF2-MotS-Fixes/blob/b1be98bd4f170b5384cb71b15443a274aac6a937/src/Engine/sithPuppet.c), lines 222–279.

## 14. Explicit trigger and radius protocols

```text
SendTrigger(targetPlayerThingOrMinusOne, protocolID, p0, p1, p2, p3) -> void
SendMessageExRadius(center, radius, thingTypeMask, message, p0, p1, p2, p3) -> void [MotS]
```

`SendTrigger` does not consult `COG_SHOULD_SYNC`. With the local player as target it broadcasts locally; with a valid active remote player it sends to that network peer; with no valid target it transmits broadcast and executes locally. Its `protocolID` becomes the trigger event's source reference; the sender is the local player thing. A non-player valid thing follows no sending branch. Define a unique protocol, parameter domains, duplicate handling and authority before adding effects to a trigger handler.

The radius call scans current-world things, checks the bit corresponding to thing type, excludes disabled/dead/destroyed objects, and compares distance to radius. Its message gate is `0 <= message < ENTERBUBBLE` (45 in the captured enum), not every registered MotS message. It delivers through each thing's COG route and returns no result. It is not an unrestricted broadcast to every COG in the level or a line-of-sight test.

Full source: [sithCogFunction.c](https://github.com/CodaLilitari/OpenJKDF2-MotS-Fixes/blob/b1be98bd4f170b5384cb71b15443a274aac6a937/src/Cog/sithCogFunction.c), lines 1197–1254 and 1719–1763; [types_enums.h](https://github.com/CodaLilitari/OpenJKDF2-MotS-Fixes/blob/b1be98bd4f170b5384cb71b15443a274aac6a937/src/types_enums.h), line 127; [sithDSSCog.c](https://github.com/CodaLilitari/OpenJKDF2-MotS-Fixes/blob/b1be98bd4f170b5384cb71b15443a274aac6a937/src/Dss/sithDSSCog.c), lines 10–111.

## 15. Per-instance heap values

```text
HeapNew(size) -> void
HeapSet(index, value) -> void
HeapGet(index) -> tagged value, or integer 0
HeapFree() -> void
```

The heap belongs to the COG instance and stores tagged values. `HeapNew` accepts positive size, discards an existing heap, allocates and zeroes the new storage; a nonpositive size does nothing. `HeapSet` writes only when value extraction succeeds and the index is in range. `HeapGet` copies all value tag/data fields; out-of-range access logs and pushes integer zero in this fork.

The recovered DataMaster 0.19 page instead documents two arguments and a return. Static inspection of the registered original JK1.01 and MotS `HeapNew` bodies independently confirms one size pop and no result push, agreeing with the signature above. See the versioned documentation review and complete original-body excerpts (retained local evidence). Do not copy the historical two-argument form or infer a return value from leftover stack data.

The pinned `HeapFree` frees the pointer and sets size to zero but does **not** null the stored pointer. Consequently, repeated freeing or a later allocation path that tests only the pointer deserves explicit review for this revision; do not infer safe idempotence from the verb name. This is a source finding, not a tested retail crash claim. Bound storage, own its lifetime once, and avoid confusing heap indices with the contiguous-symbol `base[index]` technique.

Full source: [sithCogFunction.c](https://github.com/CodaLilitari/OpenJKDF2-MotS-Fixes/blob/b1be98bd4f170b5384cb71b15443a274aac6a937/src/Cog/sithCogFunction.c), lines 935–1002.

## 16. Visibility, containment and failed queries

Measured original tables register `HasLOS` in both games, while `FindSectorAtPos` and `IsSphereInSector` are MotS-only against those tables. Line of sight, sector containment and collision are different questions. In the inspected enhanced path, an invalid ghost handle can make `HasLOS` return -1, which is truthy. Validate handles, then require the success value explicitly; a bare nonzero test is insufficient. This is not an original-game runtime observation.

The historical sector-finding example also has an index/count boundary error and globally enables adjoins without restoring their prior state. A bounds error yielding a zero vector is not proof that the origin is the desired point. Review shared world state and verb-specific replication before using a temporary all-open pass. See [target contracts and lifecycle](target-contracts-and-lifecycle.md).

## Using this reference in a real change

Retrieve a same-game example, record each event's sender/source/reference domains, and identify assets and bin/template/level binding before editing. Check every used spelling against the exact target registry. For a verb not covered here, use `implementation_index.json` to locate the complete body, follow wrappers and delegated functions, then verify each branch's argument and return behavior. The index's lexical pop list is a locator, not an authoritative signature. Run the intended engine and multiplayer scenario before describing a script as retail-compatible or synchronized.
