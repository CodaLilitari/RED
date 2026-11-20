# Flag findings: JKDF2, MotS, and Indy

> Copied into RED from [CodaLilitari/jk-mots-cog-workshop@b4b43f6](https://github.com/CodaLilitari/jk-mots-cog-workshop/blob/b4b43f6ab1976f673598c90751ec6160da70c506/docs/FLAG_DISCOVERIES.md) (MIT; see [LICENSE](LICENSE.md)). Regenerate with `tools/knowledge/import_knowledge.py` rather than editing this copy.

**Maintained master reference.** Findings and corrections are consolidated here at the same URL. This reference develops independently of downloadable workshop snapshots; use this page for the current interpretation. Last evidence update: **11 October 2026**.

[Machine-readable master JSON](https://richard-pemberton-research.darkahn.chatgpt.site/projects/jk-mots-cog-workshop/corpus/reference/flag-discoveries.json) · [Complete flag catalog](https://richard-pemberton-research.darkahn.chatgpt.site/projects/jk-mots-cog-workshop/docs/FLAGS.html) · [Machine-readable full catalog](https://richard-pemberton-research.darkahn.chatgpt.site/projects/jk-mots-cog-workshop/corpus/reference/flags.json)

## What is established for each game

| Game / evidence lane | Established scope | Not established |
| --- | --- | --- |
| **JKDF2 1.01 — original executable** | Selected original-binary paths; 20 original-executable bit-30 runtime trials under Wine/Xvfb, with limits below | Uninspected paths, game-wide nonuse, untested runtime contexts or connected-client behavior |
| **MotS — original executable** | Selected original-binary paths; stock COG contracts and 20 original-executable bit-30 runtime trials under Wine/Xvfb | Uninspected paths, untested runtime contexts or connected-client behavior |
| **Indy / Infernal Machine — pinned OpenJones3D reconstruction** | Three selected source observations and a 32-position ThingFlags declaration comparison | Original Indy binary verification, original Indy runtime behavior, or automatic transfer of JKDF2/MotS meanings |
| **Enhanced JK/MotS engines — current source** | Individually identified source consumers and recorded differences from the originals | Retail equivalence inferred from source names or successful enhanced-engine behavior |

There are **27 original-JK/MotS findings**, plus **three separate Indy reconstruction observations**. A verified path proves only the stated behavior under its stated conditions. “Not established” is an evidence gap; “absent in compared routine” is a bounded comparison. Neither means a bit is globally unused. The bit-30 investigation adds **40 bounded original-executable runtime trials** (20 per game); it does not upgrade the runtime status of other findings. No connected-client test is claimed. One separate original-stock-COG observation identifies the MotS carbonite targeting filter.

## Physics 0x40000000: original purpose still unresolved

**This bit has verified script uses; a distinct native gameplay effect has not been established.** It is bit 30, not physics `0x04000000`, and it is unrelated to identically numbered ThingFlags or ActorFlags.

| Target | What was actually checked | Result and boundary |
| --- | --- | --- |
| Original JKDF2 1.01 | Separate original-binary audit and 20 original-executable trials under Wine/Xvfb | No dedicated reader established in inspected paths. Hover matched exactly; worst moving A/B differences were below the larger repeat maximum. Custom-script reuse is not evidence of necessity. |
| Original MotS | Stock flight-script source and level bindings, separate original-binary audit, 20 original-executable trials under Wine/Xvfb | Hover matched exactly. Moving traces retain a small unresolved residual; no reproducible effect has been attributed to the bit. |
| Indy / Infernal Machine | No bit-30 physics investigation in this pass | Original binary and runtime meaning remain unexamined. Indy ThingFlags `MOVABLE` is a different field. |
| Enhanced JK/MotS engines | Navigation/comparison source | Not substituted for either original executable or treated as retail proof. |

### Where the original script uses it

MotS `s4l1_fly.cog` sets flight `0x2000`, clears gravity `0x1`, and sets `0x40000000` on its entry-link crossing. Its other-link branch applies the inverse operations to the saved local player. The source is 710 bytes, SHA-256 `309e082f11ce97819c5406b46ec9c2e931454380d46849fcf7241c73b23df604`.

The stock `s4l1_escortship.jkl` binds this COG three times: entries `4680/1280` with exits `3238/3225`; entry `3312` with exit `3322`; and entries `3400/3399` with **no bound exit surfaces**. Thus even the original placements do not all supply an inverse transition. Darsi Minor contains a byte-identical copy in a custom JK package; that package was inspected, not played in this investigation.

### Other uses found in the bounded corpus audit

**Zee Gee**, a MotS level by **Mara Yates**, dated March 1998, contains a second code body: `nograv.cog`. Its `newplayer` handler obtains the player with `GetSenderRef()`, sets flight `0x2000`, clears gravity `0x1`, and sets `0x40000000`. It has no exit/undo handler. The actual `nograv.jkl` declares the script at line 4540 and instantiates it at line 4567. Its README credits **“Cogking for showing me how to fix the cog.”** This is a historical lead, not evidence isolating the bit's effect or attributing its design to that collaborator.

The 355-byte script has SHA-256 `e1d9be80dd021b4e7153a7860dfd19d2cda6e0b7719296d0a55b9f1bb947de2f`. [Zee Gee archive provenance](https://jkdf2.net/JKBot/JKArchive/JKDF2/JKDF2/Game%20Files/motsmp/zeegee.zip) identifies the locally inspected package. The identical script is also contained in **Alternative Saber Levels**, by **Rob Fuller II**, dated May 1998, for MotS; its inspected JKLs contain no `nograv` reference, so invocation there is **not established**. [Alternative Saber archive provenance](https://jkdf2.net/JKBot/JKArchive/JKDF2/JKDF2/Game%20Files/motsmp/alt-saber.zip). These stored provenance URLs were not freshly checked for network availability in this audit.

The read-only scan covered **38,158 indexed COG rows / 25,365 recorded source hashes**, from the October 1 index snapshot, including extracted tutorial/forum code blocks. The independent stock scan covered **540 JK COG members and 777 MotS COG members**, 75 JKLs and six DAT files across ten owned archives. It found **no additional specific stock use** beyond MotS `s4l1_fly.cog`, and **no bit-30-containing value in 1,690 explicit stock `physflags` fields**. Case, leading zeroes, decimal `1073741824`, and safely evaluable constant combinations were considered. Three specific raw source hashes reduce to two code bodies: the third differs from the stock flight COG only by its final NUL versus space byte.

Broad `ClearPhysicsFlags(...,-1)` calls in JK **Oxyd CTF**, **MarbleBlastJK**, and MotS **MarbleBlast** incidentally include bit 30. JK **Gunrunner** saves the whole physics word and attempts a broad clear using the nine-digit literal `0xFFFFFFFFF`; its original-engine parsing remains unverified. These are whole-word operations, not evidence of a dedicated purpose. Other `0x40000000` matches in ThingFlags, ActorFlags, SurfaceFlags and SectorFlags address different fields.

This is not exhaustive dataflow analysis: variable masks, cross-statement or cross-COG construction, unindexed later acquisitions and undiscovered archives remain outside the scan. The custom packages were inspected, not runtime tested. No Indy result is inferred.

### What the 40 original-game trials establish

Each game completed 20 trials: repeated ABBA hover and free-impulse conditions, followed by gravity/hover controls. A and B retained full physics words `0x6B4E` and `0x40006B4E`, differing by exactly this bit. Nonzero setter-call counts and order matched, controlling the generic synchronization-marking side effect. The moving condition applied velocity `(0.05, 0.025, 0.025)` once and let it evolve freely. All trials completed; movement and gravity controls worked; every hover trace stayed exactly stationary.

These were **unchanged original executables under Wine 8/Xvfb**, using owned stock geometry, a diagnostic world COG and the stock player class COG. They entered through single-player Debug. No connected multiplayer peers or native-Windows execution were tested. The observer read process memory without writing it or injecting code. Both clients and their owned services stopped, and source-preservation/cleanup receipts were retained.

The primary comparison uses the first consistent observation per engine millisecond, aligns to the first sample-0 observation, and interpolates over 750 ms on a 10 ms grid. Maximum position differences over all trial pairs were:

| Game | A/A maximum | B/B maximum | A/B maximum |
| --- | ---: | ---: | ---: |
| JKDF2 1.01 | 0.002302402 | 0.001329071 | 0.002165313 |
| MotS | 0.001658743 | 0.001798522 | 0.002120895 |

Values are world units. **MotS's worst A/B difference is about 18% above its larger repeat maximum**, reducing to about 4.7% with alternative clock-observation handling. Those percentages compare experimental difference extrema; they do not mean an 18% or 4.7% change in game physics. Group-mean comparisons do not replace this worst-pair result. This was not a formal statistical equivalence test.

Repeated readings at a single game-clock value sometimes contained different motion states despite the consistency screen. A subsequent original-byte audit verified a retained per-Thing fractional physics-step remainder at **JK `+0x240` / MotS `+0x24C`**. The originals use nominal 20 ms steps with downward-rounding conversion; the reconstruction's `+0.5` term is absent. **The completed traces did not record that remainder.** It is a plausible source of timing variation, not a measured explanation of the MotS residual.

### Original JK control callbacks: the additional path checked

The original JK 1.01 startup routine at `0x402860` registers four callbacks: player controls `0x4D8830`, weapons `0x4D5B40`, inventory/skills `0x4D2C20`, and HUD `0x4028E0`. Registration at `0x4D6F60` writes the supplied pointer to the table at `0x833A48`; the update's indirect call is at `0x4D705F`. Original bytes corroborate the four startup calls and the registration/dispatch structure. This is static registration evidence, not a captured runtime table.

The newly inspected weapon, inventory/skills and HUD handler bodies did not establish a dedicated reader of the physics word at Thing `+0x1D0`. A tempting weapon-handler `TEST AH,0x40` follows `FNSTSW AX` and tests FPU status, not physics bit 30. The narrow verification covers 14 original-byte ranges totaling 5,508 bytes; its evidence receipt SHA-256 is `461363523ed071e9e5de7f7fde15e9d3f902b777decec9e3d1b905e279c56b0e`.

**Remaining boundary:** COG activation/deactivation/autoselect, deeper weapon selection, HUD/camera/save/device calls and other reachable helpers are not a whole-program transitive audit. This callback follow-up is JK-specific; no equivalent MotS callback closure or new runtime trial is claimed.

### Tests that can still distinguish the hypotheses

1. Repeat A/B with controlled forward/backward/vertical flight input while recording the remainder, thrust, orientation and angular velocity. Keep original-time comparisons and matched setter calls.
2. Exercise the actual Escort Ship entry/exit surfaces, repeated crossings and the placement without exit surfaces. This checks the context in which the programmer used the bit.
3. Isolate wall/slope contact, water transitions and non-player physics movers with positive controls and reversed trial order.
4. Use original connected peers to separate ownership/replication behavior from the generic setter's synchronization request.

These follow-up runtime tests are **planned, not completed results**. A separate independent execution review is pending. This page does not attribute an endorsement or test result to that review.

The appropriate authoring advice is to preserve the bit when reproducing a historical script until its role is resolved. Do not label it a universal flight flag, a proven vestige, or a safe spare bit. The observed flight/gravity operations already have functional consumers; a working combined script cannot identify the contribution of each line.

## Separate discovery: physics 0x02000000 in stock MotS carbonite

The stock `weap_carbgun_m.cog` supplies a positive **COG-defined** use for a different unresolved physics mask: its normal `fire`/`vire` target-selection path skips the targeting/freezing block when a target has `0x02000000`. The script's separate `user0` forced-freeze path lacks that test. This supports **normal stock-carbonite targeting exclusion**, not universal immunity to all freezing, damage or direct script intervention.

This is original MotS **script-source evidence**, not a newly discovered native-engine bit-test or a runtime carbonite result. No equivalent JKDF2 or original-Indy contract was established. Source SHA-256: `314237ce0285bd4beff16569d7144c56121b46472048200910683545e75e1ce3`; target enumeration near line 227, bit guard at lines 239–240. The COG-defined contract can explain meaningful template usage even when a matching native reader has not been found.

## Earlier focused follow-up: four MotS masks

Two additional original-binary findings are established below: the item `0x10` collision gate and the physics `0x00800000` water-entry writer. Physics `0x02000000` and weapon `0x20` received deeper field-aware native-code investigation. Their native meanings remain unresolved; the newer stock-MotS carbonite COG contract for `0x02000000` is described above. Their search scope, positive generic storage evidence and rejected numeric matches are recorded under [Unresolved masks investigated](#unresolved-masks-investigated). Original Indy meanings remain unestablished for all four.

## Findings by field and game

| Field/value | Finding | JKDF2 1.01 — original static | MotS — original static | Indy — original static |
| --- | --- | --- | --- | --- |
| thing `0x00010000` | Level-placement exclusion in the non-multiplayer loading branch | Verified selected code path | Verified selected code path | Not established |
| thing `0x80000000` | One deferred eligible thing in the MotS renderer | Absent in compared routine only | Verified selected code path | Not established |
| surface `0x00000080` | Suppress the inspected surface-collision impact damage | Verified selected code path | Verified selected code path | Not established |
| physics `0x00020000` | Skip initial velocity rotation on creation | Verified selected code path | Verified selected code path | Not established |
| actor `0x00002000` | Eligibility for the saber-blocking branch | Verified selected code path | Verified selected code path | Not established |
| actor `0x00200000` | Gate player controls, with different look handling | Verified selected code path | Verified selected code path | Not established |
| ai `0x00000020` | Fire-target state maintained by AISetFireTarget | Verified selected code path | Verified selected code path | Not established |
| ai `0x00000100` | Mode-change event; persistent-mode meaning remains unproved | Verified selected code path | Verified selected code path | Not established |
| jk `0x00000040` | Consume-once saber hit-history reset | Verified selected code path | Verified selected code path | Not established |
| jk `0x00000100` | MotS bubble-owner registration marker | Not established | Verified selected code path | Not established |
| multiplayer `0x00000002` | Conditional same-team player health-damage suppression | Verified selected code path | Verified selected code path | Not established |
| weapon `0x00010000` | Select the range-based collision-query and optional trail handler | Verified selected code path | Verified selected code path | Not established |
| face `0x00000010` | Original MotS model-face path initially selects lighting mode 1 | Absent in compared routine only | Verified selected code path | Not established |
| light `0x00000006` | Original MotS mode 6 propagates an inherited stored lighting scalar | Absent in compared routine only | Verified selected code path | Not established |
| jk `0x00000080` | Force maximum saber length on the eligible update branch | Verified selected code path | Verified selected code path | Not established |
| physics `0x00008000` | Pending bypass of the inspected attached-physics drag application | Verified selected code path | Verified selected code path | Not established |
| actor `0x02000000` | Skip the target-sector darkness penalty in an AI sight check | Verified selected code path | Verified selected code path | Not established |
| attach `0x00000001` | Surface attachment discriminator | Verified selected code path | Verified selected code path | Not established |
| attach `0x00000002` | Thing-face attachment discriminator | Verified selected code path | Verified selected code path | Not established |
| attach `0x00000004` | Ordinary thing attachment discriminator | Verified selected code path | Verified selected code path | Not established |
| sound `0x00000001` | Looping attached sound cleanup when its owner is freed | Verified selected code path | Verified selected code path | Not established |
| sound `0x00000040` | Absolute stored position after a nonlooping sound loses its owner | Verified selected code path | Verified selected code path | Not established |
| sound `0x00000080` | Followed-owner sound lookup and detachment at owner cleanup | Verified selected code path | Verified selected code path | Not established |
| weapon `0x00004000` | Distance-based damage reduction in the 0x10000 handler | Verified selected code path | Verified selected code path | Not established |
| weapon `0x00008000` | Conditional segmented object-trail creation in the 0x10000 handler | Verified selected code path | Verified selected code path | Not established |
| item `0x00000010` | Permit an item through the MotS weapon-collision entry filter | Absent in compared handler entry only | Verified selected code path | Not established |
| physics `0x00800000` | MotS water-entry writer and skipped vertical-velocity reduction | Verified selected code path | Verified selected code path | Not established |

Thing, actor, physics, JK, sound and attachment flags are separate fields. Light mode 6 is a scalar. Source aliases are search aids, not proof of original developer names. Indy reconstruction evidence appears in its own section below and must not be read as original-executable proof.

## thing 0x00010000: Level-placement exclusion in the non-multiplayer loading branch

**JKDF2 1.01:** Verified selected code path. **MotS:** Verified selected code path. **Indy original:** not established for this finding. **Runtime:** not tested.

The loader adds this bit to its rejection mask when submode bit 1 is clear. A populated level thing whose flags intersect that mask is freed. The corresponding multiplayer branch adds 0x8000 instead. Source multiplayer startup sets the submode bit, supporting the shorthand single-player placement exclusion.

**Limits and authoring consequence.** This is a loading condition. Setting it after loading does not by itself hide or destroy the thing. The precise original predicate is submode bit 1 clear; other rejection bits, including difficulty, still apply.

Evidence anchors: `JK 0x4CEC5D-0x4CEC70 and 0x4CEE07-0x4CEE1A`; `MotS 0x51D36B-0x51D37E and 0x51D527-0x51D53A`; `World/sithThing.c:1547-1552,1599-1601; Dss/sithMulti.c:211`.

Existing source alias: `SITH_TF_10000`. Evidence grade: `original-retail-static`.

## thing 0x80000000: One deferred eligible thing in the MotS renderer

**JKDF2 1.01:** Absent in compared routine only. **MotS:** Verified selected code path. **Indy original:** not established for this finding. **Runtime:** not tested.

Original MotS stores an eligible flagged thing in one local pointer slot and skips its ordinary draw. Later eligible flagged things overwrite that slot. After normal traversal, the final stored thing is drawn once. The corresponding complete JK1.01 routine lacks this test and slot.

**Limits and authoring consequence.** This is not a queue for all flagged things, and it does not establish through-wall rendering. Earlier eligibility/clipping applies. The inspected JK difference is function-specific, not proof of game-wide bit nonuse. No visual runtime result is claimed.

Evidence anchors: `MotS 0x516270,0x5166B5-0x516719`; `JK corresponding routine 0x4C7DD0-0x4C8241 (end exclusive)`; `Engine/sithRender.c:2736-2737,2970-2997`.

Existing source alias: `SITH_TF_80000000`. Evidence grade: `original-retail-static`.

## surface 0x00000080: Suppress the inspected surface-collision impact damage

**JKDF2 1.01:** Verified selected code path. **MotS:** Verified selected code path. **Indy original:** not established for this finding. **Runtime:** not tested.

For the inspected physics thing-versus-surface collision path, the surface bit becomes the fourth argument to the collision response. A set bit bypasses that response branch's impact-damage calculation, hit-damaged sound and FALL-class DamageThing call.

**Limits and authoring consequence.** The collision response itself still runs. This is not universal damage immunity or a guarantee that every source of fall damage honors the bit. The flagged surface must be the actual contact surface in this path.

Evidence anchors: `JK 0x4E8E0D,0x4E8E27,0x4E9707-0x4E9767`; `MotS 0x5365BD,0x5365D7,0x536ED3-0x536F33`; `Engine/sithCollision.c:916-929,1120-1127`.

Existing source alias: `SITH_SURFACE_80`. Evidence grade: `original-retail-static`.

## physics 0x00020000: Skip initial velocity rotation on creation

**JKDF2 1.01:** Verified selected code path. **MotS:** Verified selected code path. **Indy original:** not established for this finding. **Runtime:** not tested.

In CreateThingAtPos, a physics thing normally has its initial velocity transformed by its created orientation. A set 0x20000 skips that transform in both originals.

**Limits and authoring consequence.** This is not a general angular-velocity, continuous-spin or object-orientation disable flag. MotS can apply the COG-supplied look vector after native creation; the transform uses the orientation at native creation time, not necessarily the final look-vector orientation. Setting the flag later does not undo the transform.

Evidence anchors: `JK 0x4CDF28-0x4CDF47; registered CreateThingAtPos 0x502520`; `MotS 0x51C644-0x51C663; registered CreateThingAtPos 0x552E00`; `World/sithThing.c:1107-1108`.

Existing source alias: `SITH_PF_20000`. Evidence grade: `original-retail-static`.

## actor 0x00002000: Eligibility for the saber-blocking branch

**JKDF2 1.01:** Verified selected code path. **MotS:** Verified selected code path. **Indy original:** not established for this finding. **Runtime:** not tested.

The saber-hit path checks this bit on an actor/player being hit before admitting it to field-of-view and chance-based blocking checks, which also reject an invisible attacker. A clear bit goes to the damage path.

**Limits and authoring consequence.** The bit permits this blocking path; it does not guarantee a block. BLEEDS is a misleading source alias for this inspected JK/MotS consumer. Other possible consumers have not been exhaustively excluded.

Evidence anchors: `JK 0x40BD9F-0x40BDA8 and following blocking checks`; `MotS 0x40D303-0x40D30C and following blocking checks`; `Gameplay/jkSaber.c:264-307`.

Existing source alias: `SITH_AF_BLEEDS; historical THING_TYPEFLAGS_ISBLOCKING`. Evidence grade: `original-retail-static`.

## actor 0x00200000: Gate player controls, with different look handling

**JKDF2 1.01:** Verified selected code path. **MotS:** Verified selected code path. **Indy original:** not established for this finding. **Runtime:** not tested.

On the inspected player control path, a set bit skips movement, activation and map handling. Original JK calls its look handler before the flag gate. Original MotS places the look call after the gate, so the flagged path skips it as well.

**Limits and authoring consequence.** This controls processing of these inputs; it does not freeze all physics or prevent every scripted move. Preserve the JK/MotS difference instead of reducing the meaning to immobile.

Evidence anchors: `JK look call 0x4D8C5F; gate 0x4D8C6F-0x4D8C79`; `MotS gate 0x527852-0x52785C; look call 0x5278A3`; `Devices/sithControl.c:1025-1053`.

Existing source alias: `SITH_AF_CONTROLSDISABLED; historical THING_TYPEFLAGS_IMMOBILE`. Evidence grade: `original-retail-static`.

## ai 0x00000020: Fire-target state maintained by AISetFireTarget

**JKDF2 1.01:** Verified selected code path. **MotS:** Verified selected code path. **Indy original:** not established for this finding. **Runtime:** not tested.

AISetFireTarget stores the target pointer and timestamp, then sets this AI-control-block bit when the target is non-null and clears it otherwise. If the flags change, it emits the mode-change event. Both original wrappers agree.

**Limits and authoring consequence.** TOUGHSKIN is not evidence of armor here. Setting the bit alone does not assign a target pointer or cause firing; use AISetFireTarget. Other possible consumers are not exhaustively excluded.

Evidence anchors: `JK AISetFireTarget 0x501550; writes 0x501584-0x5015B4`; `MotS AISetFireTarget 0x551C30; writes 0x551C64-0x551C94`; `Cog/sithCogFunctionAI.c:389-424`.

Existing source alias: `SITHAI_MODE_TOUGHSKIN`. Evidence grade: `original-retail-static`.

## ai 0x00000100: Mode-change event; persistent-mode meaning remains unproved

**JKDF2 1.01:** Verified selected code path. **MotS:** Verified selected code path. **Indy original:** not established for this finding. **Runtime:** not tested.

The inspected use is an AI event identifier and instinct event-subscription mask. AISetMode emits event 0x100 whenever its OR changes the mode word. The event dispatcher sends COG AIEVENT with first extended parameter 256.0 in original JK, but current AI flags converted to float in original MotS.

**Limits and authoring consequence.** This resolves an event role, not a special persistent AI mode. Generic mode setters can store 0x100; that does not prove a dedicated mode behavior. The dispatcher has living/class/disabled/sleeping gates. MotS float conversion need not preserve every high-bit integer exactly.

Evidence anchors: `JK AISetMode 0x5011E0; dispatcher 0x4EA26D-0x4EA28C`; `MotS AISetMode 0x551670; dispatcher 0x537DD0-0x537E03`; `AI/sithAI.c:429-456`.

Existing source alias: `SITHAI_MODE_UNK100`. Evidence grade: `original-retail-static`.

## jk 0x00000040: Consume-once saber hit-history reset

**JKDF2 1.01:** Verified selected code path. **MotS:** Verified selected code path. **Indy original:** not established for this finding. **Runtime:** not tested.

When the eligible saber collision path sees this bit, it clears the bit and resets the counts of already-hit things and surfaces. The stored histories are pointer lists. The separate saber-damage-enable bit is tested afterward.

**Limits and authoring consequence.** This requests a reset when the path next consumes it; it does not itself enable damage or guarantee another hit. Source puppet marker 0xE sets it, but the original marker dispatch table was not independently reconstructed in this pass.

Evidence anchors: `JK 0x40BBB2-0x40BBDE; list consumers 0x40BD5C,0x40C074,0x40C13E`; `MotS 0x40CED7-0x40CEFD`; `Gameplay/jkSaber.c:368-374; Engine/sithPuppet.c:762-764`.

Existing source alias: `JKFLAG_40`. Evidence grade: `original-retail-static`.

## jk 0x00000100: MotS bubble-owner registration marker

**JKDF2 1.01:** Not established. **MotS:** Verified selected code path. **Indy original:** not established for this finding. **Runtime:** not tested.

jkCreateBubble registers owner/radius-squared/type in the bubble array and sets this bit on the owner. jkDestroyBubble clears both the record and bit. A query removes a registered record if its owner no longer has the bit.

**Limits and authoring consequence.** This marks the bubble owner, not a thing merely located inside a bubble. Setting the bit alone does not allocate a bubble record or specify radius/type. Use the bubble verbs. An original-JK meaning is not established; lack of bubble verbs is not proof that the bit is universally unused.

Evidence anchors: `MotS jkCreateBubble 0x40C620 calls 0x40F390; set 0x40F41E-0x40F430`; `MotS destroy 0x40F440-0x40F46C; query 0x40F560-0x40F58D`; `Main/jkEpisode.c:709-791`.

Existing source alias: `JKFLAG_100`. Evidence grade: `original-retail-static`.

## multiplayer 0x00000002: Conditional same-team player health-damage suppression

**JKDF2 1.01:** Verified selected code path. **MotS:** Verified selected code path. **Indy original:** not established for this finding. **Runtime:** not tested.

The original actor/player damage paths test multiplayer mode bit 0x2 and return zero before health subtraction when a same-team helper succeeds. That helper also requires teams bit 0x1, distinct player things, non-null player records, and equal nonzero team numbers. The source parent is compared with the victim.

**Limits and authoring consequence.** Self-damage, team zero and unequal teams do not meet this predicate. Earlier COG/inventory callbacks may already have run. This does not promise suppression of all messages, every other damage path or network convergence.

Evidence anchors: `JK 0x4ECFEA-0x4ED014; helper 0x4C9250`; `MotS 0x53B6F3-0x53B71D; helper 0x517890`; `GetMultiModeFlags JK 0x507EE0 / MotS 0x559940; GetPlayerTeam JK 0x4E10F0 / MotS 0x530280`.

Existing source alias: `MULTIMODEFLAG_2`. Evidence grade: `original-retail-static`.

## weapon 0x00010000: Select the range-based collision-query and optional trail handler

**JKDF2 1.01:** Verified selected code path. **MotS:** Verified selected code path. **Indy original:** not established for this finding. **Runtime:** not tested.

In both original dispatchers, weapon 0x10000 selects this handler after the higher-priority 0x2000 branch. The complete handler traverses returned adjoin crossings and handles the first returned non-adjoin result. Thing hits call damage and can independently attempt force; world hits call surface impact. A non-null impact template enables a creation attempt. The final callee marks the weapon Thing.flags 0x2 for destruction.

**Limits and authoring consequence.** The query uses moveSize as well as range, so this is not established as a zero-width ray. One terminal result is handled; collision ordering and penetration are not established. Destruction is marked, not an immediate storage free. Full downstream helper behavior, successful allocation, visuals and networking remain untested.

Evidence anchors: `JK 0x4D3830-0x4D385A, target 0x4D3C20`; `MotS 0x5221F0-0x52221A, target 0x5225F0`; `World/sithWeapon.c:57-84,246-407`; `Complete bodies: JK 0x4D3C20-0x4D4583; MotS 0x5225F0-0x522F53`; `MotS terminal calls: damage 0x522AAF, force 0x522B12, surface 0x522B31, impact creation 0x522B9F`; `Final mark callees: JK 0x4CCA70-0x4CCA93; MotS 0x51B160-0x51B183`.

Existing source alias: `SITH_WF_10000`. Evidence grade: `original-retail-static`.

## face 0x00000010: Original MotS model-face path initially selects lighting mode 1

**JKDF2 1.01:** Absent in compared routine only. **MotS:** Verified selected code path. **Indy original:** not established for this finding. **Runtime:** not tested.

Original MotS reads model face flags at face+4 and, when 0x10 is set, overrides the initially selected render-entry lighting mode with scalar 1 (source alias NOTLIT). The complete compared JK1.01 model-face routine lacks this override. This proves a lighting consumer beyond the nearest-filter enum label.

**Limits and authoring consequence.** This is an initial mode override in the model-face path. Later ambient/colormap handling can replace mode 1 with mode 0 or 2; no final black/fullbright appearance is established. Nearest filtering, other surface/cache/backend consumers and global JK nonuse are not established. No runtime rendering test.

Evidence anchors: `MotS DrawFace 0x44A620-0x44A9E6 end-exclusive; face flags test 0x44A673, lighting store 0x44A678`; `MotS later ambient/colormap branches 0x44A827-0x44A95F`; `MotS model loader face flags 0x448DFB-0x448E18; model-face caller 0x44A5CB`; `JK DrawFace 0x4467D0-0x446ADF end-exclusive; forwards flags 0x446AA7-0x446AB0 without this override`; `Primitives/rdModel3.c:1713-1724,1731-1733`.

Existing source alias: `RD_FF_TEX_FILTER_NEAREST`. Evidence grade: `original-retail-static`.

## light 0x00000006: Original MotS mode 6 propagates an inherited stored lighting scalar

**JKDF2 1.01:** Absent in compared routine only. **MotS:** Verified selected code path. **Indy original:** not established for this finding. **Runtime:** not tested.

In original MotS, hierarchy node flag 2 selects mesh lighting mode 6 and copies the stored lighting scalar at mesh+0x44 from the first ancestor without node flag 2. Mode 6 bypasses the ordinary mesh lighting cap and copies that scalar into every face extraLight field at face+0x30. One original producer stores the mean of computed scalar vertex-light outputs in mesh+0x44. The compared JK1.01 hierarchy/mesh routines lack this inheritance, cap exception and mode-6 copy.

**Limits and authoring consequence.** Scalar mode, not a flag bit. Requires a valid ancestor chain and mesh; the original walk lacks the modern null checks. Stored values can be stale, and culling, draw order, face-mode caps and later rendering still matter. It is not proof of fullbright, per-vertex RGB or a dynamic-only value. Current source instead transfers radius at this hierarchy step; original MotS parses and uses radius at mesh+0x48, distinct from the transferred +0x44. No runtime proof.

Evidence anchors: `MotS DrawHNode 0x449F80-0x44A04E; mode write 0x449FB7; ancestor scalar transfer 0x449FD4-0x449FDE`; `MotS mode cap exception 0x44A161-0x44A17F; six-way table 0x44A5FC; mode-6 face copies 0x44A4F9-0x44A50F`; `MotS mean-light producer 0x44A4EA-0x44A4EF, callee 0x4507D0-0x450A3A; sum/divide 0x4509EF-0x450A36`; `MotS radius parser destination 0x4489B6-0x4489C0 and culling read 0x44A0B1`; `JK DrawHNode 0x4462E0-0x44635E; DrawMesh 0x446360-0x4467C8, ordinary cap 0x446452-0x44646C`; `Primitives/rdModel3.c:1453-1461,1533-1547,1629,1646-1652; rdModel3.h:157-178`.

Existing source alias: `RD_LIGHTMODE_6_UNK`. Evidence grade: `original-retail-static`.

## jk 0x00000080: Force maximum saber length on the eligible update branch

**JKDF2 1.01:** Verified selected code path. **MotS:** Verified selected code path. **Indy original:** not established for this finding. **Runtime:** not tested.

When neither the extend-4 nor retract-8 branch is selected, bit 0x80 sets saber length to its configured maximum, writes about 0.6 to the source-named timeLeftLengthChange field, clears 4/8 and sets 1. Both original branches retain 0x80; current enhanced source clears it.

**Limits and authoring consequence.** The inspected callers already require JK flag 1 and the routine needs valid player information. Setting 0x80 alone does not enter that caller path. Extend/retract branches take priority. This is not a universal unretractable-saber or runtime appearance claim.

Evidence anchors: `JK length routine 0x40B950-0x40BADB; bit branch 0x40BA77-0x40BA9B; caller 0x40D820-0x40D837`; `MotS length routine 0x40CC60-0x40CDEF; bit branch 0x40CD87-0x40CDAB; caller test 0x40F02B`; `Gameplay/jkSaber.c:179-187`.

Existing source alias: `JKFLAG_SABERFORCEON`. Evidence grade: `original-retail-static`.

## physics 0x00008000: Pending bypass of the inspected attached-physics drag application

**JKDF2 1.01:** Verified selected code path. **MotS:** Verified selected code path. **Indy original:** not established for this finding. **Runtime:** not tested.

The original ApplyForce implementation sets 0x8000 while adding force divided by positive mass to a physics mover velocity, even when the supplied force vector is zero. In the inspected surface/Thing-face attached-physics block, nonzero velocity and nonzero surface drag lead to a bit test: if set, the block clears 0x8000 and skips its linear drag call.

**Limits and authoring consequence.** This is a conditional consumed marker, not permanent friction immunity. If those block prerequisites are not reached it is not consumed there, so a one-frame lifetime is unproved. Setting the bit alone does not apply an impulse. Other drag paths and runtime motion remain untested.

Evidence anchors: `JK ApplyForce wrapper 0x503520; native 0x4F5C20-0x4F5CE1; OR at 0x4F5CCD`; `MotS ApplyForce wrapper 0x554210; native 0x5464F0-0x5465B1; OR at 0x54659D`; `JK dispatcher attachment mask 0x4F5BCA; attached drag test 0x4F7C7A; skipped call 0x4F7D12`; `MotS dispatcher attachment mask 0x54649A; attached drag test 0x5488AD; skipped call 0x548945`; `Engine/sithPhysics.c:222,924-942`.

Existing source alias: `SITH_PF_8000`. Evidence grade: `original-retail-static`.

## actor 0x02000000: Skip the target-sector darkness penalty in an AI sight check

**JKDF2 1.01:** Verified selected code path. **MotS:** Verified selected code path. **Indy original:** not established for this finding. **Runtime:** not tested.

The observer actor bit bypasses the target-sector ambient-light multiplier in the inspected sight-acceptance threshold calculation in both originals. The branch is reached inside the distance and target-light-state gates.

**Limits and authoring consequence.** It does not guarantee detection or sight through geometry. Distance, movement, invisibility, observer blindness, threshold clamping and the final random comparison retain their own effects. Other visibility helpers and runtime probabilities were not established.

Evidence anchors: `JK AI initializer observer binding 0x4E9FC6; complete sight helper 0x4EC3A0-0x4EC58B; bit test 0x4EC450`; `MotS AI initializer observer binding 0x537AC9; complete sight helper 0x53AA60-0x53AC4C; bit test 0x53AB10`; `AI/sithAI.c:245-294,1608-1658`.

Existing source alias: `SITH_AF_SEEINDARK`. Evidence grade: `original-retail-static`.

## attach 0x00000001: Surface attachment discriminator

**JKDF2 1.01:** Verified selected code path. **MotS:** Verified selected code path. **Indy original:** not established for this finding. **Runtime:** not tested.

The native surface attachment setter records its target and writes discriminator 1 into attach_flags. Surface attachment stores a surface; Thing-face stores a parent and face; ordinary Thing attachment maintains parent/child links and a relative position. Detach selects its surface or shared Thing/face branch from this separate field.

**Limits and authoring consequence.** Attachment flags are not Thing.flags: their offsets are JK +0x34 and MotS +0x3C. AttachThingToThingEx ORs its extra mask into this field after ordinary attachment. Arbitrary masks, movement propagation and complete message delivery are unproved; this does not resolve Thing 0x01000000 or attachment 0x8/0x10.

Evidence anchors: `JK discriminator store 0x4CE18B; GetAttachFlags 0x503780; detach 0x4CE660-0x4CE814`; `MotS discriminator store 0x51C8AB; GetAttachFlags 0x5544A0; detach 0x51CD70-0x51CF24`; `Ex OR: JK 0x5038C8-0x5038DD; MotS 0x5545E8-0x5545FD`; `World/sithThing.c:1178-1455; Cog/sithCogFunctionThing.c:971-1039`.

Existing source alias: `SITH_ATTACH_SURFACE`. Evidence grade: `original-retail-static`.

## attach 0x00000002: Thing-face attachment discriminator

**JKDF2 1.01:** Verified selected code path. **MotS:** Verified selected code path. **Indy original:** not established for this finding. **Runtime:** not tested.

The native Thing-face attachment setter records its target and writes discriminator 2 into attach_flags. Surface attachment stores a surface; Thing-face stores a parent and face; ordinary Thing attachment maintains parent/child links and a relative position. Detach selects its surface or shared Thing/face branch from this separate field.

**Limits and authoring consequence.** Attachment flags are not Thing.flags: their offsets are JK +0x34 and MotS +0x3C. AttachThingToThingEx ORs its extra mask into this field after ordinary attachment. Arbitrary masks, movement propagation and complete message delivery are unproved; this does not resolve Thing 0x01000000 or attachment 0x8/0x10.

Evidence anchors: `JK discriminator store 0x4CE37A; GetAttachFlags 0x503780; detach 0x4CE660-0x4CE814`; `MotS discriminator store 0x51CA8A; GetAttachFlags 0x5544A0; detach 0x51CD70-0x51CF24`; `Ex OR: JK 0x5038C8-0x5038DD; MotS 0x5545E8-0x5545FD`; `World/sithThing.c:1178-1455; Cog/sithCogFunctionThing.c:971-1039`.

Existing source alias: `SITH_ATTACH_THINGFACE`. Evidence grade: `original-retail-static`.

## attach 0x00000004: Ordinary thing attachment discriminator

**JKDF2 1.01:** Verified selected code path. **MotS:** Verified selected code path. **Indy original:** not established for this finding. **Runtime:** not tested.

The native ordinary Thing attachment setter records its target and writes discriminator 4 into attach_flags. Surface attachment stores a surface; Thing-face stores a parent and face; ordinary Thing attachment maintains parent/child links and a relative position. Detach selects its surface or shared Thing/face branch from this separate field.

**Limits and authoring consequence.** Attachment flags are not Thing.flags: their offsets are JK +0x34 and MotS +0x3C. AttachThingToThingEx ORs its extra mask into this field after ordinary attachment. Arbitrary masks, movement propagation and complete message delivery are unproved; this does not resolve Thing 0x01000000 or attachment 0x8/0x10.

Evidence anchors: `JK discriminator store 0x4CE5CD; GetAttachFlags 0x503780; detach 0x4CE660-0x4CE814`; `MotS discriminator store 0x51CCDD; GetAttachFlags 0x5544A0; detach 0x51CD70-0x51CF24`; `Ex OR: JK 0x5038C8-0x5038DD; MotS 0x5545E8-0x5545FD`; `World/sithThing.c:1178-1455; Cog/sithCogFunctionThing.c:971-1039`.

Existing source alias: `SITH_ATTACH_THING`. Evidence grade: `original-retail-static`.

## sound 0x00000001: Looping attached sound cleanup when its owner is freed

**JKDF2 1.01:** Verified selected code path. **MotS:** Verified selected code path. **Indy original:** not established for this finding. **Runtime:** not tested.

In the inspected owner-free cleanup, a matching sound with FOLLOWSTHING 0x80 and LOOP 0x1 takes the channel cleanup branch, conditionally stopping active playback and freeing its channel. The matching nonloop branch instead detaches the sound to the last position.

**Limits and authoring consequence.** This adds an original lifecycle consumer of LOOP, not a complete looping/playback contract or an audibility test. The channel must pass owner and follow-state tests. Sound flags are separate from Thing.flags.

Evidence anchors: `JK owner cleanup 0x4DD1B0-0x4DD2C4; FOLLOW test 0x4DD1E0; LOOP test 0x4DD1F1`; `MotS owner cleanup 0x52EBE0-0x52ECF4; FOLLOW test 0x52EC10; LOOP test 0x52EC21`; `Position consumers: JK 0x4DD420-0x4DD595; MotS 0x52EE50-0x52EFC5`; `Devices/sithSoundMixer.c: owner-free and positional-update paths`.

Existing source alias: `SITHSOUNDFLAG_LOOP`. Evidence grade: `original-retail-static`.

## sound 0x00000040: Absolute stored position after a nonlooping sound loses its owner

**JKDF2 1.01:** Verified selected code path. **MotS:** Verified selected code path. **Indy original:** not established for this finding. **Runtime:** not tested.

The matching nonlooping FOLLOWSTHING sound has bit 0x80 cleared and bit 0x40 set during owner-free cleanup, and receives the owner final position. The inspected position update selects the stored position when 0x40 is set.

**Limits and authoring consequence.** This verifies the transition and position selection, not successful playback or audibility. Existing source-based PlaySoundPos behavior is retained in the prior interpretation. It establishes no Thing.flags 0x01000000 meaning.

Evidence anchors: `JK owner cleanup 0x4DD1B0-0x4DD2C4; FOLLOW test 0x4DD1E0; LOOP test 0x4DD1F1`; `MotS owner cleanup 0x52EBE0-0x52ECF4; FOLLOW test 0x52EC10; LOOP test 0x52EC21`; `Position consumers: JK 0x4DD420-0x4DD595; MotS 0x52EE50-0x52EFC5`; `Devices/sithSoundMixer.c: owner-free and positional-update paths`.

Existing source alias: `SITHSOUNDFLAG_ABSOLUTE`. Evidence grade: `original-retail-static`.

## sound 0x00000080: Followed-owner sound lookup and detachment at owner cleanup

**JKDF2 1.01:** Verified selected code path. **MotS:** Verified selected code path. **Indy original:** not established for this finding. **Runtime:** not tested.

Owner-free cleanup selects channels carrying 0x80 whose owner pointer matches the freed Thing. Looping channels enter channel cleanup; nonlooping channels clear 0x80, set absolute-position 0x40 and copy the owner final position.

**Limits and authoring consequence.** This is sound-channel state, not Thing.flags. Other creation and playback gates, networking and runtime audio remain outside this original-static finding.

Evidence anchors: `JK owner cleanup 0x4DD1B0-0x4DD2C4; FOLLOW test 0x4DD1E0; LOOP test 0x4DD1F1`; `MotS owner cleanup 0x52EBE0-0x52ECF4; FOLLOW test 0x52EC10; LOOP test 0x52EC21`; `Position consumers: JK 0x4DD420-0x4DD595; MotS 0x52EE50-0x52EFC5`; `Devices/sithSoundMixer.c: owner-free and positional-update paths`.

Existing source alias: `SITHSOUNDFLAG_FOLLOWSTHING`. Evidence grade: `original-retail-static`.

## weapon 0x00004000: Distance-based damage reduction in the 0x10000 handler

**JKDF2 1.01:** Verified selected code path. **MotS:** Verified selected code path. **Indy original:** not established for this finding. **Runtime:** not tested.

At a terminal collision in the original 0x10000 handler, this bit subtracts rate times hit distance from the local damage amount and floors it at minDamage in both games.

**Limits and authoring consequence.** This updates a local hit amount, not the persistent weapon damage field. Preserve the separately documented ordinary-update decay difference between current source and original JK. Finite-value control flow is established; downstream damage acceptance and runtime outcomes are not.

Evidence anchors: `JK 0x4D408E-0x4D40C7; MotS 0x522A5E-0x522A97`; `Complete handlers: JK 0x4D3C20-0x4D4583; MotS 0x5225F0-0x522F53`; `World/sithWeapon.c:246-407`.

Existing source alias: `SITH_WF_DAMAGE_DECAY`. Evidence grade: `original-retail-static`.

## weapon 0x00008000: Conditional segmented object-trail creation in the 0x10000 handler

**JKDF2 1.01:** Verified selected code path. **MotS:** Verified selected code path. **Indy original:** not established for this finding. **Runtime:** not tested.

The original handler attempts trail-template creation along a locally advanced orientation when bit 0x8000, a trail template and the step/projection versus hit-distance or range gates permit it. The no-hit tail can also create a trail up to range.

**Limits and authoring consequence.** Creation return values are ignored, so allocation and visuals are not proved. The loops have no explicit iteration cap or general progress guard: termination depends on advancing configured step/orientation. This is not a reproduced hang, networking proof or a universal trail contract.

Evidence anchors: `JK complete handler 0x4D3C20-0x4D4583; MotS 0x5225F0-0x522F53`; `MotS first trail loop 0x5226B5-0x522A37; no-hit trail 0x522BC1-0x522F40; creation calls 0x5229DC/0x522EE1`; `World/sithWeapon.c:265-313,362-403`.

Existing source alias: `SITH_WF_OBJECT_TRAIL`. Evidence grade: `original-retail-static`.

## item 0x00000010: Permit an item through the MotS weapon-collision entry filter

**JKDF2 1.01:** Absent in compared handler entry only. **MotS:** Verified selected code path. **Indy original:** not established for this finding. **Runtime:** not tested.

In original MotS, the registered weapon-versus-item collision handler checks whether the collided thing is type 5 (item). An item lacking item flag 0x10 immediately returns 0. A non-item or an item carrying the bit continues into subsequent weapon-collision processing. The compared original JK handler lacks this initial item-bit filter.

**Limits and authoring consequence.** Shootable powerup is a useful shorthand for this gate, not a complete damage or destruction contract. Later weapon/item settings and paths still matter. The JK result describes only the compared handler entry, not game-wide absence of all possible item-bit uses. No in-game collision or damage test was run.

Evidence anchors: `MotS handler 0x5236D0; type test 0x5236DE; item mask 0x5236E3; return 0x5236EC-0x5236F5; continuation 0x5236F6`; `MotS weapon/type-5 collision registration 0x53491C-0x534925`; `JK compared handler 0x4D4BC0; weapon-proximity check 0x4D4BCB`; `World/sithWeapon.c:637-650; types_enums.h:1273-1275`.

Existing source alias: `SITH_ITEM_10`. Evidence grade: `original-retail-static`.

## physics 0x00800000: MotS water-entry writer and skipped vertical-velocity reduction

**JKDF2 1.01:** Verified selected code path. **MotS:** Verified selected code path. **Indy original:** not established for this finding. **Runtime:** not tested.

On the eligible original MotS water-entry path, the code ORs physics flags with 0x00800000, stores the result, and branches if that result is nonzero. The OR guarantees a nonzero result, so the following velocity.z *= 0.25 block is skipped whether the bit was previously clear or set. The corresponding original JK path performs that vertical-velocity multiplication and has no equivalent physics-bit OR/test there. The mapped MotS water-exit routine has no direct read or clear of this physics bit.

**Limits and authoring consequence.** The writer requires entry into an underwater sector, the separate Thing water-state bit previously absent, no drowning branch, the no-splash/no-notify argument zero, and physics movement. This is not a slow-once test. Broader intended meaning remains unresolved; no claim of all drag being disabled, game-wide permanent retention, or demonstrated sinking/fall-damage behavior. The current reconstruction reproduces this original branch anomaly. Runtime remains untested.

Evidence anchors: `MotS SetThingPosEx 0x553DD0 -> SetSector 0x51BBA0 -> EnterSector 0x51BC80 -> EnterWater 0x51BD30`; `MotS OR/store/JNE 0x51BE26-0x51BE37; skipped vertical-velocity block 0x51BE39-0x51BE45`; `MotS mapped ExitWater 0x51BE50-0x51BF3D (end exclusive)`; `JK EnterWater 0x4CD640; vertical-velocity multiplication 0x4CD736-0x4CD748; constant at 0x522E8C = 0.25`; `World/sithThing.c:816-904`.

Existing source alias: `SITH_PF_800000`. Evidence grade: `original-retail-static`.

## Indy: reconstruction evidence, separately scoped

Here “Indy” means **Indiana Jones and the Infernal Machine**. The comparison uses [OpenJones3D](https://github.com/smlu/OpenJones3D/tree/a675b395a5d5cff238332d091368483b849e8b5b), pinned at `a675b395a5d5cff238332d091368483b849e8b5b`. These are inspected reconstruction-source observations, not verified original-Indy executable or runtime results.

| Indy observation | Evidence | Limits |
| --- | --- | --- |
| The compared reconstructed ThingFlags setters OR nonzero masks; clearers AND the complement of nonzero masks. | `reconstruction-source-consumer`; `Libs/sith/Cog/sithCogFunctionThing.c:2935-2988` | Wrapper operations do not prove per-bit meanings, original Indy executable behavior or equivalence with JKDF2/MotS. |
| The Jones SNOW bit has material-sound consumers in the compared reconstructed world code. | `reconstruction-source-consumer`; `Libs/sith/World/sithThing.c:1524-1555,2800-2816` | This differs from the independently traced original JK/MotS placement-exclusion role for the same number. No original Indy binary or runtime test. |
| The compared Jones water-entry path routes splash messages to the entering Thing own COGs. | `reconstruction-source-consumer`; `Libs/sith/World/sithThing.c:1981-1990` | Source-level integration comparison only; it does not establish original Indy behavior or upgrade the compared JK source path to original-binary evidence. |

### Indy ThingFlags declaration inventory

These 32 entries preserve the reconstruction declarations and their grades. A meaningful name alone is not proof of a consumer. The three source observations above are independently labeled and do not upgrade every declaration.

| Value | Indy reconstruction symbol | Declaration evidence | Original Indy binary / runtime |
| --- | --- | --- | --- |
| `0x00000001` | `SITH_TF_EMITLIGHT` | `declaration-only`; `Libs/sith/types.h:62` | Not established / not tested |
| `0x00000002` | `SITH_TF_DESTROYED` | `declaration-only`; `Libs/sith/types.h:63` | Not established / not tested |
| `0x00000004` | `SITH_TF_NOWEAPONCOLLIDE` | `declaration-only`; `Libs/sith/types.h:64` | Not established / not tested |
| `0x00000008` | `SITH_TF_WHIPCLIMB` | `declaration-only`; `Libs/sith/types.h:65` | Not established / not tested |
| `0x00000010` | `SITH_TF_INVISIBLE` | `declaration-only`; `Libs/sith/types.h:66` | Not established / not tested |
| `0x00000020` | `SITH_TF_UNKNOWN_20` | `unresolved-name`; `Libs/sith/types.h:67` | Not established / not tested |
| `0x00000040` | `SITH_TF_STANDON` | `declaration-only`; `Libs/sith/types.h:68` | Not established / not tested |
| `0x00000080` | `SITH_TF_MOUNTABLE` | `declaration-only`; `Libs/sith/types.h:69` | Not established / not tested |
| `0x00000100` | `SITH_TF_REMOTE` | `declaration-only`; `Libs/sith/types.h:70` | Not established / not tested |
| `0x00000200` | `SITH_TF_DYING` | `declaration-only`; `Libs/sith/types.h:71` | Not established / not tested |
| `0x00000400` | `SITH_TF_COGLINKED` | `declaration-only`; `Libs/sith/types.h:72` | Not established / not tested |
| `0x00000800` | `SITH_TF_NOCRUSH` | `declaration-only`; `Libs/sith/types.h:73` | Not established / not tested |
| `0x00001000` | `SITH_TF_UNKNOWN_1000` | `unresolved-name`; `Libs/sith/types.h:74` | Not established / not tested |
| `0x00002000` | `SITH_TF_WOOD` | `declaration-only`; `Libs/sith/types.h:75` | Not established / not tested |
| `0x00004000` | `SITH_TF_SHADOW` | `declaration-only`; `Libs/sith/types.h:76` | Not established / not tested |
| `0x00008000` | `SITH_TF_JEEPSTOP` | `declaration-only`; `Libs/sith/types.h:77` | Not established / not tested |
| `0x00010000` | `SITH_TF_SNOW` | `declaration-only`; `Libs/sith/types.h:78` | Not established / not tested |
| `0x00020000` | `SITH_TF_PULSESET` | `declaration-only`; `Libs/sith/types.h:79` | Not established / not tested |
| `0x00040000` | `SITH_TF_TIMERSET` | `declaration-only`; `Libs/sith/types.h:80` | Not established / not tested |
| `0x00080000` | `SITH_TF_DISABLED` | `declaration-only`; `Libs/sith/types.h:81` | Not established / not tested |
| `0x00100000` | `SITH_TF_SEEN` | `declaration-only`; `Libs/sith/types.h:82` | Not established / not tested |
| `0x00200000` | `SITH_TF_UNKNOWN_200000` | `unresolved-name`; `Libs/sith/types.h:83` | Not established / not tested |
| `0x00400000` | `SITH_TF_METAL` | `declaration-only`; `Libs/sith/types.h:84` | Not established / not tested |
| `0x00800000` | `SITH_TF_EARTH` | `declaration-only`; `Libs/sith/types.h:85` | Not established / not tested |
| `0x01000000` | `SITH_TF_NOSOUND` | `declaration-only`; `Libs/sith/types.h:86` | Not established / not tested |
| `0x02000000` | `SITH_TF_SUBMERGED` | `declaration-only`; `Libs/sith/types.h:87` | Not established / not tested |
| `0x04000000` | `SITH_TF_CLIMBCRATE` | `declaration-only`; `Libs/sith/types.h:88` | Not established / not tested |
| `0x08000000` | `SITH_TF_WATERDESTROYED` | `declaration-only`; `Libs/sith/types.h:89` | Not established / not tested |
| `0x10000000` | `SITH_TF_AIRDESTROYED` | `declaration-only`; `Libs/sith/types.h:90` | Not established / not tested |
| `0x20000000` | `SITH_TF_SPLASH` | `declaration-only`; `Libs/sith/types.h:91` | Not established / not tested |
| `0x40000000` | `SITH_TF_MOVABLE` | `declaration-only`; `Libs/sith/types.h:92` | Not established / not tested |
| `0x80000000` | `SITH_TF_WHIPSWING` | `declaration-only`; `Libs/sith/types.h:93` | Not established / not tested |

In particular, Indy `NOSOUND` (`0x01000000`), `MOVABLE` (`0x40000000`), and `WHIPSWING` (`0x80000000`) are separate declarations. They do not resolve the corresponding JKDF2/MotS unknowns or replace the inspected MotS deferred-render interpretation. Indy `SNOW` (`0x10000`) has the source consumer above; original JK/MotS instead have the documented placement-exclusion path.

## Unresolved masks investigated

These are bounded research results, not additional resolved flag meanings. Generic storage/copy behavior is original-static evidence for the word containing a bit; it does not establish a dedicated gameplay consumer. The separate bit-30 investigation above has bounded runtime results; the older masks below remain untested at runtime. Indy was not audited for these masks.

### physics 0x02000000

**Status:** unresolved-original-semantics.

Original JK 1.01 and MotS expose this bit through generic Get/Set/ClearPhysicsFlags. The inspected template-copy step includes the entire physics-flags word, and the position-message writer and reader copy that word without masking this bit. These are verified storage and transfer operations; no dedicated native gameplay consumer was established in those inspected paths. The later stock-MotS carbonite targeting exclusion is separate COG-source evidence, described above.

**Scope.** Checked registered wrappers, the CreateThing-to-template-copy chain, position-message field writes/reads, and selected checksum call sites. Exact mask/complement searches returned 14 hits per executable; upper-byte/word displacement and immediate bit-25 shift/bit-operation searches returned zero matches. Whole-field displacement counts were 172 JK and 160 MotS syntactic references, not proven consumer counts. The checksum call site supplies all four physics-flags bytes, but the helper and full join chain were not re-audited here.

**Excluded matches.** Thing water-state 0x02000000 reads offset zero, and actor 0x02000000 reads the type-specific parameter field. Neither establishes physics meaning. The inspected MotS position-send suppression gate uses physics 0x04000000, a different bit.

**Limits.** No unused, droid-specific, water-specific or no-position-sync alias is justified. Negative text-pattern searches are not full data-flow analysis: alternate pointer bases, register aliases, composite or computed masks and indirect paths may escape them. Copy preservation is bounded to the inspected operation; later initialization may change state. Static packet handling does not prove delivery, immediate synchronization or connected-client convergence. Checksum input is not a demonstrated join failure. Runtime was not tested.

Evidence anchors: `JK Get/Set/ClearPhysicsFlags 0x504980, 0x5049C0, 0x504A30; physics field +0x1D0`; `MotS Get/Set/ClearPhysicsFlags 0x5557F0, 0x555830, 0x5558A0; physics field +0x1DC`; `JK position-word write 0x4F3434-0x4F3440 and read/store 0x4F35AA-0x4F35B1`; `MotS position-word write 0x5437D4-0x5437E0 and read/store 0x54394A-0x543951; distinct 0x04000000 gate at 0x543746`; `JK template-copy 0x4CDB70, REP MOVSD 0x4CDBAF (0x2B0 bytes); MotS 0x51C270, REP MOVSD 0x51C2AF (0x2C4 bytes)`; `JK physics checksum input 0x4CF6EA-0x4CF6F3; MotS 0x51DE5A-0x51DE63`.

### weapon 0x00000020

**Status:** unresolved-original-semantics.

No verified weapon-specific 0x20 consumer was established in the reviewed original JK 1.01 or MotS update, firing, parser, collision and lifecycle region. The hexadecimal weaponflags parser stores the complete parsed word without a per-bit mask in both originals. The named current-source identifier remains a numeric declaration. Matching mask values in actor, explosion and physics code were rejected after checking their field and type context.

**Scope.** Selected flag loads and operations were reviewed within byte-checked regions JK 0x4D3830-0x4D5571 and MotS 0x5221F0-0x524118 (end exclusive), plus selected typed collision, damage, explosion and checksum paths. This is not a semantic audit of every instruction or downstream call; the JK endpoint does not encompass the complete final helper. Exact shared-union-offset references were used only as candidates (167 JK, 194 MotS); union offset alone does not identify a weapon. Original type-3 checksum call sites supply the full four-byte weapon-flags field; no bit-specific checksum rule or full join result is inferred.

**Excluded matches.** MotS actor-death 0x53BB01 and explosion creation 0x54CA8D, with JK explosion counterpart 0x4FBA9D, operate on other typed parameter domains. Surface-collision 0x20/0xDF operations read physics flags, not weapon flags. A high-byte 0x20 test on a loaded weapon word means whole-word 0x2000, not 0x20.

**Limits.** This is not proof of game-wide nonuse or inertness. Computed masks, pointer aliases, indirect paths and uninspected code remain outside the bounded result. Template occurrence does not establish purpose, and no underwater-grenade behavior is assigned. Changing template input may affect checksum processing even without a demonstrated gameplay consumer. No retail runtime or multiplayer test was run.

Evidence anchors: `JK reviewed weapon region 0x4D3830-0x4D5571; MotS 0x5221F0-0x524118 (end exclusive)`; `JK unmasked parsed-weaponflags store 0x4D46DA; MotS 0x5230AA; Current World/sithWeapon.c:409-423`; `MotS typed actor damage dispatch 0x51B190-0x51B271; actor-death candidate 0x53BB01`; `JK explosion 0x4FBA9D; MotS explosion 0x54CA8D; type-6 creation dispatch 0x51C5BF-0x51C5E8`; `JK type-3 checksum input in 0x4CF6A0-0x4CF7C1; MotS 0x51DE10-0x51DF31`; `Current types_enums.h:139, SITH_WF_20 declaration`.

## What remains unresolved

Thing `0x20`, `0x01000000`, and `0x40000000` still lack established original-JK/MotS meanings. Sound-owner cleanup and physical attachment did not resolve `0x01000000`; those routines use distinct sound and attachment fields. Do not import Indy NOSOUND/MOVABLE labels or treat unknown bits as safe storage.

Actor `0x1000`, `0x4000`, and `0x04000000` remain unresolved after field-aware checks. Shared actor/weapon union offsets and numeric matches are not actor-consumer proof. Weapon `0x2`, `0x10`, `0x20`, `0x40`, `0x20000`, and `0x100000` were not resolved by the completed handler; nearby collision-result masks belong to another domain. Attachment `0x8` and `0x10` received no new original meaning in this pass.

AI `0x100` retains its verified event role without a proved dedicated persistent-mode behavior. Rendering findings remain bounded to their compared routines; final visual results and other original-JK consumers remain unproved.

## Additional attachment and parity observations

At the inspected attachment call sites, entered-message calls require the child Thing mask `0x80100` to be clear; exited-message calls test `0x100` locally. Downstream dispatch was not fully traced, so this does not guarantee delivered events. Face-attachment landing material checks read parent Thing `0x400000` / `0x800000`, not `0x01000000`.

Both original saber 0x80 branches retain that bit, whereas the compared current source clears it. A modern-source one-shot interpretation must not be silently assigned to the original games. The impulse marker is consumed only when the inspected drag block reaches its test, not necessarily on the next frame.

## Research method and parity limits

- Current engine-header virtual addresses can point to different functions in JK1.01. Reconcile via registered verbs, pointer fields, call chains and verified bytes; do not assume a uniform delta.
- Named-symbol searches miss literal consumers. Conversely, a matching numeric value in physics, sound, collision, animation or another field does not establish Thing.flags semantics.
- Source symbols are preserved as search aliases, not official historical names or automatic semantic authority.
- Private executable/source/disassembly data is excluded; this supplement contains authored explanations and identities/anchors.

The nearby current weapon damage-decay branch resets damage to minDamage only when the computed value is <=0; the inspected JK1.01 branch compares against minDamage itself. Matching one flag dispatch does not establish whole-function parity.

The newly mapped original-MotS hierarchy transfers stored lighting at mesh+0x44, whereas current source assigns radius. Original radius is parsed and used at +0x48. This source mismatch is recorded without changing the engine.

The private evidence packet retains selected byte receipts and review notes. The focused follow-up also records unresolved investigation scope in the machine-readable `investigations` array. The public-facing supplement contains no retail scripts, executable payloads, disassembly copies, Discord excerpts, or private local paths. The broad crawl and acquisition goal remain paused.

Original identities:

- jk101: SHA-256 `4509cd690634ba20e132bd16e713f91e144b17e2beaaad5e678f1ddb189b51d9`, 1388544 bytes.
- mots: SHA-256 `23cd064217f904d36d896f52d3cdd17635bb85441a12e0415c0637504191d974`, 1759744 bytes.


## Machine-readable contract

The [master JSON](https://richard-pemberton-research.darkahn.chatgpt.site/projects/jk-mots-cog-workshop/corpus/reference/flag-discoveries.json) uses `schema_version: 2` for its data format, not a workshop release. Each finding has `targets.jkdf2_1_01`, `targets.mots`, and `targets.indy`, with separate static status, evidence lane and runtime status. Existing `retail_jk` and `retail_mots` fields remain for readers of the earlier format.

`indy_comparison.source_observations` contains the three reconstruction-only observations; `indy_comparison.thing_entries` preserves all 32 declaration grades. `original_executables`, `source_files`, anchors and limits retain provenance. `snapshot_provenance` is historical download provenance, not the identity of this master page. Future corrections belong at these same human and machine URLs.

The machine contract also includes `stock_cog_observations` and per-investigation `runtime`, `stock_context` and `next_tests` records. Runtime trial counts are separate from resolved native flag meanings.
