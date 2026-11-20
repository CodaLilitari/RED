# Retail COG pattern atlas: JKDF2 and Mysteries of the Sith

> Copied into RED from [CodaLilitari/jk-mots-cog-workshop@b4b43f6](https://github.com/CodaLilitari/jk-mots-cog-workshop/blob/b4b43f6ab1976f673598c90751ec6160da70c506/corpus/notes/pattern-atlas.md) (MIT; see [LICENSE](../LICENSE.md)). Regenerate with `tools/knowledge/import_knowledge.py` rather than editing this copy.

> Public research edition. This is original authored analysis, not a source-code dump. References marked "retained local evidence" were inspected during research but their raw artifacts are not distributed. Binary identities, measured registries and public package identities are included; source execution and retail gameplay remain different evidence grades.


This atlas explains 21 exact retail source objects in nine gameplay families. It is based on reading their bodies, comparing variants, and tracing message and state relationships. It does not claim that every archived script is mounted by a shipped level, that the scripts compile unchanged on every executable, or that any scenario below has been run. A source in a retail archive can be unused, stale, or contain a mistake. Keep the original bytes and headers; make experiments in a separate mod.

The source IDs below identify full hashes, member filenames and handler entry lines in the registry at the end. The public metadata catalog supplies approved source associations; original objects, detailed member chains and literal comparison spans remain local evidence and are not distributed. Author initials are reproduced as present in the headers; this atlas does not expand them speculatively.

## How to use a pattern

Select an example from the same game and gameplay role first. Read its entire symbol section and every incoming message, then map each required resource into the proposed level or mod. Preserve declaration order when indexed access such as `door0[i]` traverses consecutive symbols. Distinguish the event source, sender reference, and link ID at each entry point; copying an expression into a different handler can change the object domain it refers to.

Write a state table before editing: incoming message, guards, state read, action, state written, asynchronous continuation, cleanup. A `Sleep`, timer, pulse, animation, or moving frame creates a later observation point. Examine death, deselection, interrupted motion, join, and save/load separately. Counts of verbs and messages are discovery aids; they are not execution traces or compatibility verdicts.

The corrected lexical index recognizes both `float` and `flex` as the parser's flex-valued declaration family, and `ai` as the AI-class resource declaration. `aiclass` is not one of the inspected parser's declaration branches. A message alias such as `message onStart=startup` retains both local label `onStart` and registered event `startup`. The index is deliberately not a compiler: unknown symbol lines, unsupported block-comment markers, and heuristically detected asset references still require inspection. `exposed_parameters` means ordered nonlocal, nonmessage declarations found lexically, not proof of a particular level binding.

## Doors: aggregate motion and geometry state

`00_door.cog` [D1], credited `[IS]`, has identical source bytes in the observed JK and MotS sets. It is a useful baseline for an ordinary two-frame door with up to four moving parts. Its four consecutive thing symbols carry link IDs 0–3 and mask `0x405`; the speed, close delay, and light value are declared `float`. The startup handler counts valid parts, obtains the sector through `door0`, disables its adjoins, and sets light. This creates dependencies on a valid `door0`, suitable sector/adjoin geometry, and frame 0/1 conventions.

| Entry | Actual source decision and continuation |
|---|---|
| `activate` (source spelling; same registered event ID as `activated`) | Sum movement and current frames. Ignore activation while any part moves. If summed frames are zero, enable sector adjoins and command all valid parts to frame 1. |
| `arrived` | Recompute the aggregate. Once all parts stop, a frame sum equal to the number of parts schedules the closing timer; a zero sum disables adjoins. |
| `blocked` | Command all valid parts open again. |
| `timer` | Command all valid parts to frame 0. |

The engine source registers both `activate` and `activated` as event ID 1 (`engine/source_snapshot/src/Cog/sithCog.c`, lines 65–66). This is a registered-name synonym; an explicit declaration alias such as `message onStart=startup` is a separate syntax mechanism. The array expression is symbol-table indexing: the inspected VM adds the numeric index to the base symbol ID (`engine/source_snapshot/src/Cog/sithCogExec.c`, lines 101–119), so declaration order is part of this script's data layout.

The aggregate works because the intended parts have frames 0 and 1. Additional frames or inconsistent initial placement can satisfy a numerical sum without representing the intended physical state. That is an inference from the arithmetic, not an observed runtime fault. Preserve the consecutive declarations if retaining the indexed loops. Do not infer that every stock elevator has the same blocked behavior just because both use `MoveToFrame`.

Suggested bounded tests: one and four parts; unused slots at `-1`; unequal travel times; repeated activation during opening and closing; obstruction during closure; all parts initially closed; an intentionally invalid initial frame setup; visibility and collision through the controlled sector. In multiplayer add host/client activation and late join observations; this script alone does not specify a full join protocol. [D1]

## Elevators: a sequential state machine versus coupled motion

JK `00_elev_doors.cog` [E1], credited `DS`, coordinates two landing doors and a two-stop platform. The header requires floor 0 initially, with both doors closed; startup opens door 0. External call buttons have link IDs 0 and 1, internal move buttons ID 5, and the moving things ID 10. The body is the authority for state numbering: its comments describe five steps, but the code never sets state 1.

| State in the body | Meaning | How it advances |
|---|---|---|
| `movestatus=0` | Accepting activation | A different-floor call or interior button closes the current landing door, sets switch cels, and assigns state 2. |
| `2` | Waiting for door arrival | `arrived` calls `move_elev`, disables current landing adjoins, commands the opposite platform frame, then sets state 3. |
| `3` | Waiting for platform arrival | `arrived` enables destination landing adjoins and opens that door, then sets state 4. |
| `4` | Waiting for door arrival | `arrived` resets switch cels and returns to state 0. |

This is a protocol among the platform, doors, and buttons, not a single movement call. All three moving things share a link ID and the `arrived` handler relies on state rather than explicitly checking which thing arrived. Preserve the expected event ordering or add an independently tested sender guard when extending it. There is no `blocked` handler. Also review the sound expression in `resetstatus`: it uses `SurfaceCenter(GetSenderRef())` even though the normal continuation comes from `arrived`. The body warrants a sender-domain check; it does not by itself establish a retail runtime defect. [E1]

JK `00_std_elev.cog` [E2], credited `[IS]`, is smaller: crossing either configured lower adjoin, while the platform is stationary at frame 0, sleeps for `start_wait` and commands frame 1. Arrival away from frame 0 schedules a timer; the timer returns it to frame 0. Its history comment explicitly records removal of the blocked handler. The delay precedes movement, so repeated crossings during that delay belong in the test plan. Do not copy a door's reopen-on-block assumption into this elevator. [E2]

MotS `s1l3_elev_auto.cog` [E3] moves the elevator and two struts together. An `entered` event must have link ID 2; activation filters that ID and a lit button cel. Both paths command all three things to frame 1. `arrived` either resets the button when the elevator is at frame 0 or schedules the return timer, which moves all three to frame 0. A `start_wait` symbol exists but is unused. The source header says `S1L3_elev_switch`, while the archive member is `s1l3_elev_auto.cog`; retain both rather than silently renaming evidence. [E3]

For elevator changes test wrong-floor and same-floor calls, calls while each part is moving, extra/unexpected arrival events, obstruction, missing struts, different speeds, and reset after interruption. The sequential-door pattern and coupled-strut pattern solve different dependencies; the latter is not evidence that MotS inherently requires struts.

## Weapons: fire cadence, selection protocol, and MotS integration

JK `weap_bryar.cog` [W1], credited `[YB & CYW]`, separates continuous weapon activation from actual `fire` events. `activated` obtains the player from the source and fire mode from the sender, suppresses waggle, and calls `ActivateWeapon` with `fireWait / bin63`. Each `fire` rejects dead players, handles empty bin 11 through an optional automatic weapon switch, fires `+bryarbolt`, consumes one unit, plays the POV key, and updates the fire rate from the same power-boost bin. Selection installs POV/world models, plays mounting audio and animation, clears relevant JK flags, sets weapon 2, and sets the mount wait. Deselection schedules holster-key cleanup with a timer. `autoselect` returns a priority only when weapon ownership bin 2 and ammunition bin 11 are nonzero. [W1]

These are coupled contracts with inventory definitions, templates, POV/world models, keys, sounds, player animation modes, and the engine's weapon callbacks. Altering the projectile alone is a much smaller change than replacing the selection or activation protocol. The division by bin 63 assumes its surrounding game initialization gives a useful value; do not infer a safe zero behavior from this script.

MotS `weap_bryar_m.cog` [W2] preserves that structure but adds substantial integration. The header adds `[RF]`. Read the exact differences before porting:

| Concern | JK source [W1] | MotS source [W2] |
|---|---|---|
| Empty-ammo autoselection | Passes `AutoSelectWeapon` result to `SelectWeapon` | Wraps that result in `GetWeaponBin` |
| Player hands | One POV model | Bin 67 chooses Kyle or Mara POV model |
| Bin 93 state | No branch | Activation sends `skill` value 1100 to the player class COG; firing substitutes `+swsparks` and `weapfail.wav` while still consuming ammo |
| Class coordination | No corresponding send | Firing sends `user1` value 3 to the local player's class COG |
| Dismount key flags | Literal `0x18` | Literal `18` (decimal) |

The last row is an actual numeric-literal difference, not a formatting equivalence. Its precise animation consequences require engine/runtime checking. The class-message arguments and bins are meaningful only in the matching surrounding scripts and data; this atlas does not label bin 93 from its number alone. A verb being present in both games does not make its argument domain interchangeable. [W1] [W2]

Test primary and secondary activation, held fire, last-ammo consumption, autoselect disabled/enabled, selection during holster, death during fire, bin-63 changes, both MotS hand models, and the MotS bin-93 branch with the corresponding class COG installed. Track projectile creation, inventory consumption, camera animation, and world animation separately.

## Force Jump: charge duration and cancellation paths

JK `force_jump.cog` [F1], credited `[YB]`, owns inventory bin 21, costs 20 from bin 14, and computes a maximum charge duration of rank × 0.35. Activation marks the bin active before checking mana; only when mana is at least 20 does it call `ActivateBin` and start the pulse. In that affordable branch it conditionally plays charging audio while grounded, on a face, or on the water surface. Deactivation obtains the duration, checks player state and mana again, clamps both short taps below 0.15 and overlong holds to the maximum, adds a vertical component of duration + 2.5 to existing velocity, detaches the player, sets velocity, and clears the water-related flag. Cost is skipped if either bin 64 or 65 is 1. It gives bin 21 a 1-second wait. A short extended timer clears the temporary JK flag; death and new-player handling stop the pulse and clear activation. [F1]

MotS [F2] adds `enterbubble` and `exitbubble`, refuses activation inside the bubble or while current weapon equals 10 (commented EWEB), and permits use when mana meets cost **or** reaches the maximum stored in bin 69. It skips cost only for bin 64. In single player it emits a radius `splash` message carrying 21 as a Force disturbance. It does not contain JK's `SetBinWait(player,21,1.0)`. The grounded/face/water test remains at execution, while the charge sound placement differs. [F2]

The bubble paths differ even between MotS powers: Jump's `enterbubble` sets `inbubble=1`, with its immediate stop call commented out. Deactivation clears pulse/activation before its bubble early return. The EWEB check is earlier than `DeactivateBin`. Therefore test entering a bubble or changing to EWEB during an existing charge, rather than assuming the activation guard fully handles mid-action changes. Compare tap, maximum hold, rank zero, air, water surface, insufficient mana, a full maximum pool below 20, cost exemption bins, death, and new-player events. These are proposed tests, not assertions of failure. [F1] [F2]

## Force Pull and actor COGs: a cross-script return-value protocol

JK Force Pull [F3], also credited `[YB]`, periodically scans players, actors, and items, checks line of sight, range, flags, and view dot product, and highlights the chosen target. Release rechecks LOS and mana, spends cost, and branches on target type. For an armed actor it calls `SkillTarget(victim,player,24,0)` and expects the returned thing handle to be the dropped weapon. It then clears the actor weapon and amputates a joint. An item gets its own skill query; other players receive a rank-bearing query and follow a separate path. [F3]

JK `actor_st.cog` [A1], credited `[CYW, YB & CR]`, supplies the other half. Its `skill` branch for value 24 creates the weapon powerup, sets its lifetime to 30 seconds, returns its thing handle through `ReturnEx`, installs `noweapon.ai`, and makes the actor flee. The Force script and actor script must agree on that returned value and on who mutates the actor's weapon/joint. `ReturnEx` is followed by a normal `return`; it should not be treated as synonymous with terminating a handler. The actor also doubles damage type 16, performs a probabilistic lethal-damage dismemberment path, and drops a longer-lived weapon on `killed` if still armed. [A1]

Pull's thrown item temporarily loses gravity, receives a directed force, and carries a set of extended timers. Timer 1 restores gravity after 1.5 seconds; timer 2 clears it at 3.5 and again 3.6 seconds; multiplayer timers 3 and 4 disable the object and set remaining lifetime with the later value adjusted by 0.1 seconds. The source contains repeated scheduled state updates here. Its comments identify packet-loss mitigation as the author's intention; actual network sends depend on the called verbs and execution context. These are not measured delivery guarantees. Do not generalize duplicate timers into reliable networking for arbitrary non-idempotent actions. [F3]

MotS Pull [F4] adds target-selection cost every third pulse, the full-bin-69 affordability rule, bubble exclusion, a positive-mass filter, and a Force-disturbance send. It changes initial/release wait values, skips the gravity/throw block for actor or player objects, and immediately calls `stop_power` on `enterbubble`. Its CTF-item test is `GetThingType(potential) == 5`; JK's corresponding source is `GetThingType(potential == 5)`. Those parse differently. Preserve this exact historical difference and verify intended behavior before copying the JK expression. [F3] [F4]

MotS `actor_ys.cog` [A2], credited `[CR + YB]`, shows the bubble provider: `created` calls `jkCreateBubble` and starts looping audio; `killed` calls `jkDestroyBubble`. Its `damaged` handler returns zero for damage type 8 and doubles type 16. Its skill response differs by requested Force value. It is a lifecycle example and an inter-script dependency for the MotS Force behavior, not a drop-in JK actor. [A2]

Test armed/disarmed actor pulls, item acceptance/refusal, players, LOS loss after targeting, CTF flags, zero-mass objects, bubble entry mid-targeting, target destruction before timers, repeated pulls, death/deselection cleanup, and host/client observations. Verify template definitions and `.ai` resources alongside the COGs. A returned handle, an inventory amount, a bin ID, and an actor type are different domains even when all appear as numbers.

## Cameras: continuous pivot versus captured player actions

JK `00_cameratricks.cog` [C1], credited `[SXC]`, attaches a camera to a fulcrum, adjusts the boss AI mode, and starts a timer. The timer rotates the fulcrum about its configured frame, switches to camera 0, and focuses it on the camera thing; subsequent `arrived` events rotate again. The header explicitly says this script does not freeze the player or game, and says the boss is reactivated in another script. The body contains no general exit/restore path. Reusing it requires finding or providing the surrounding cleanup and AI-resume ownership. [C1]

MotS `00_unicam.cog` [C2], credited `[YB]`, is an interactive single-player camera with an explicit lifecycle. Activation refuses weapon 13, rejects already-active state, sets actor flags, stops the player, installs itself with `SetActionCog`, saves health, position, and prior camera, enables a pulse, and focuses the camera. `playeraction` handles activation as exit, allows firing via a return value, and filters other actions. The pulse exits when the player dies, loses health, or changes position. `stop_cam` restores focus and prior camera, stops pulse, resets the switch, releases `SetActionCog`, and clears actor flags. A matching trigger changes the stored camera reference to `disabledCamera`; it does not call `show_cam` itself. [C2]

These examples teach different camera responsibilities. The MotS body explicitly owns input capture and restoration, while the JK pivot body is one piece of a larger sequence. For an interactive camera, record which actor flags existed before entry if changing the stock ownership assumptions. Test exit by input, health damage, displacement, death, repeated activation, switch destruction/trigger during viewing, scoped weapon rejection, and conflicts with another action-capture COG. The stock header's single-player scope is not multiplayer proof.

## Inventory: instant consumption and maintained state

`item_bacta.cog` [I1], credited `[CYW & YB]`, is byte-identical between the observed JK and MotS copies. Activation uses the source player, requires a positive count in bin 40 and health below 100, displays string 250, plays the healing sound, heals 30, consumes one, and makes bin 40 unavailable when empty. Its early guard means using it at full health does not consume the item. The inventory/bin definition, string table, and sound are part of the dependency set. Test full health, one remaining item, low health, repeated activation, and availability after consumption. [I1]

`item_fieldlight.cog` [I2], also identical between the two observed sets and credited `[CYW & YB]`, maintains more state. Ownership is bin 42, energy is bin 13, the on/off actor bit is `0x4`, and inventory activation is updated in parallel. Turning on with energy starts a 1.5-second pulse. Each pulse consumes one energy unit; reaching exactly zero stops pulse, clears the actor bit and activation, and plays the corresponding feedback. Manual toggle and the matching player's `killed` event perform cleanup too. [I2]

The light is a good example of synchronized local state variables that must agree: visual actor flag, inventory active flag, and pulse scheduling. The exact-zero depletion test is a source fact; behavior with fractional or externally changed energy deserves a test if a mod changes the energy system. Check other equipment sharing bin 13 before changing drain. Byte identity of these two small item scripts does not establish that every inventory bin or item policy is portable between games.

## Objectives and level progression: UI state is not the exit condition

JK `01_endlevel.cog` [O1], credited `[IS]`, registers itself as master, establishes the goal-string base in bin 99, displays goal 0, initializes weapons/ammo, resets weapon wait state, and selects a starting weapon. `user0` marks the goal complete, plays feedback, sets actor flags, stops the player, applies a temporary invulnerability flag, sleeps three seconds, removes invulnerability, then ends the level. `shutdown` rewards a Force star when the secret counters indicate all secrets were found. [O1]

The MotS archive's same-named script [O2] shares much of startup and the goal completion feedback but lacks the JK freeze/invulnerability sequence and shutdown reward. It still contains the older direct `SelectWeapon(AutoSelectWeapon(...))` call. This is a concrete warning against treating a retail archive member as proof of current usage: the filename and header do not show which level mounts it. [O2]

MotS `s1l2_endlevel.cog` [O3], credited `[KS]`, is a more specific example. Startup uses string base 2000, multiple visible goals, an `.san` startup cutscene, and `GetWeaponBin(AutoSelectWeapon(...))`. Its `user0` branches on parameter 0 to update goals; entering the configured exit sector eventually calls `jkEndLevel` without an explicit all-goals check in that handler. Therefore a UI completion bit and the actual exit predicate must be traced separately. The shutdown awards two stars plus a secrets bonus. [O3]

One branch in [O3] assigns `done=1` although the declaration is `done1`; some goal feedback occurs before the one-shot guard. Preserve this anomaly in source analysis. Do not silently fix the evidence or assert that a shipped mission necessarily executes that branch. If adapting it, use a distinct mod copy and test repeat delivery, out-of-order goal messages, early exit-sector entry, save/load around completion, secrets rewards, and cutscene/weapon initialization together.

## Multiplayer: authoritative state, presentation, and a join snapshot

JK `c3_largelevelsrv.cog` [N2] and `c3_largelevel.cog` [N1], credited `[SD & YB]`, form a useful pair. The server-oriented script's startup runs its setup only for server or single-player mode. It initializes `small_level`, broadcasts trigger 10001, obtains the master COG after a short sleep, and registers itself through `user0`. Its `damaged` handler filters the configured switch and lock state, changes the level-size state, updates switch/audio/string feedback, and broadcasts the new state. Its `join` handler syncs the switch surface and sends trigger 10001 with the current state specifically to the joining player. Its `user0` locks changes on parameters 3 or 4. [N2]

The presentation script [N1] has `flags=0x240`, listens to `trigger`, rejects sources other than 10001, takes the state from parameter 0, and changes face geometry and adjoin flags for its configured surfaces. It does not own an activation handler. This is a source-backed separation of state selection and triggered geometry updates, with an explicit late-join state send. The engine evidence in the workflow must determine the precise locality and synchronization meaning of the flags; the architecture alone does not prove identical state on every peer. [N1] [N2]

Two caveats make this pair especially useful for research. The server script's comment promises ten retransmissions, but the inspected body has no loop or pulse handler implementing that promise. The presentation body calls `SectorAdjoins(close1,1)` even though `close1` is not declared in its symbol section. These are exact source discrepancies, not reasons to invent missing behavior. Contrast this with Force Pull's actual duplicate timers. When auditing reliability, count executable resend paths, not comments. [N1] [N2] [F3]

For a new multiplayer mechanism write down who changes canonical state, which message carries it, which parameters encode it, whether applying it twice is safe, what a joining peer receives, and what happens if a message is late or absent. Test both change directions from host and client, repeated switch damage, lock transitions, a join before and after toggling, and participant disagreement. Those tests are still required even when following a retail design. Do not use this pair to assert checksum acceptance; checksum construction and compatibility are separate evidence lanes.

## Porting and review checklist derived from these sources

1. Establish executable/game/distribution, archive order, actual resource name, and the source hash. The owned JK Disc 2 audit found three source variants absent from the initially inspected Steam retail set, so a single retail label is insufficient provenance.
2. Map declared resources and numeric domains: frames, things, surfaces, sectors, templates, message/link IDs, inventory bins, weapon identifiers, strings, and flags. Find the associated definitions and calling scripts.
3. Trace state and cleanup across every message. Name the owner of pulse, timer, moving-part completion, input capture, actor flag, and returned thing handle.
4. Compare matching JK/MotS bodies. The examples show differences in weapon-bin conversion, actor/class messages, Force affordability, bubbles, targeting costs, AI disturbance, and cleanup. Identical names and overlapping verbs are insufficient.
5. Record anomalous source text as evidence. Create a proposed correction separately, with rationale and a discriminating test. Stock comments can be stale; stock source can be unused.
6. Keep evidence levels explicit: extraction/hash verified; source analyzed; parser/VM inspected; compile result; single-player observation; multiplayer host/client/join observation; checksum result on a named executable. This atlas establishes the first two, with parser references for the analyzer corrections.

## Analyzer audit and source discipline

The analyzer corrections were checked against `src/Cog/sithCogParse.c` declaration dispatch and `sithCogParse_ParseMessage`, and against `src/Cog/cog.l` literal/comment/token rules. It preserves string/vector literals while stripping only recognized line comments, masks literals during call discovery, stops at the first case-folded `end` token, and detects inline labels. It warns on C-style block-comment markers rather than treating them as accepted syntax. The lexical metadata is not an AST, type checker, resource resolver, call graph, reachability proof, or compiler.

The declared `local` symbol attribute is separate from COG execution locality. Asset-reference regex results omit template definitions and can include non-resource text; the verb-prefix tags deliberately overinclude sender/ref and local-player calls. Neither tag counts nor observed-retail-only call lists determine verb availability. Use measured registries and engine evidence from the main workflow for that question.

All proposed test cases here remain unexecuted. No recovered tool, cheat installer, or extracted retail COG was executed to produce this atlas.


## Exact source registry

Each ID identifies an exact retail source hash; source bytes are intentionally excluded. Full ISO/archive/member chains remain retained local evidence; the public registry below preserves the exact member hashes and handler line numbers. These are retail LucasArts scripts; original authorship and rights remain in each source header. This atlas is analysis, not a grant to redistribute the sources.

| ID | Observed retail game | Member filename | Full SHA-256 |
|---|---|---|---|
| [D1] | jk, mots | cog/00_door.cog | `a07fb10fadbc81bf4a970cd4ced495240ccfa86f35e3f9de954729d65cbfba5a` |
| [E1] | jk | cog/00_elev_doors.cog | `e84ea6fdfe576f50319003470248f9c57aeb053757d9b32bfc3f3b2689ee4d87` |
| [E2] | jk | cog/00_std_elev.cog | `709ec0fb746726a0d991d5af6aaf0e9b93ef9a90c4f15f52058855ac09974b58` |
| [E3] | mots | cog/s1l3_elev_auto.cog | `709e53c5a23ad8a111e35da02e5b7519c62095f67ac5736972552437854fb5b6` |
| [W1] | jk | cog/weap_bryar.cog | `066e48483ac08da081acd7034a9c9a43c44be64320457594305a0a6fe7a79a31` |
| [W2] | mots | cog/weap_bryar_m.cog | `e28fe06cf471a13a1a765acad6553255881f474d3e711ccad11363f997f21523` |
| [F1] | jk | cog/force_jump.cog | `36c2b8176c38bfdea43b1c6431892a9022d5e7a98435af79164d9105a484c882` |
| [F2] | mots | cog/force_jump.cog | `644f7d04fe4570ab1f735002f3b21445cb01c5e387a1644c1711287bc5b8ceb9` |
| [F3] | jk | cog/force_pull.cog | `9ab4429c7334bcf1baa286e7027bc4e7ea4e4bcc9907342254d27b2659605141` |
| [F4] | mots | cog/force_pull.cog | `e1dc89003c77f3a211294febd4f2447b4f390bb6e3258ead26e6488b722a7f65` |
| [A1] | jk | cog/actor_st.cog | `b2b27471f59972e928746f57442bc40e0a29b3080b0433713d56bfdfd19ed1d5` |
| [A2] | mots | cog/actor_ys.cog | `f297c6ccdd27aecb96be0f1f48f6dda876c706515f6db99f3353a3cd08618271` |
| [C1] | jk | cog/00_cameratricks.cog | `59a690f4fadcd859e6a40a3518bdd1678b25228e89b472fee6ad7db47ac557d4` |
| [C2] | mots | cog/00_unicam.cog | `2a70095301b3951e1b6f7fb206c6bc40f9f51d6c999f22575f681197cff480d5` |
| [I1] | jk, mots | cog/item_bacta.cog | `5f8a22f023819621b2c8af0ed037b10b1b115393364bebdc2e9311eab84f16d7` |
| [I2] | jk, mots | cog/item_fieldlight.cog | `bd81a39f19d25dfe04463ee281b8bb98f98c59725417d31beb8c091bfa7ccd54` |
| [O1] | jk | cog/01_endlevel.cog | `622f607883b1e36d495b379a2a8892b06a6b8f99d8ca340546b0d0a06276928f` |
| [O2] | mots | cog/01_endlevel.cog | `38fbd47d539c5e513410b34552e13b12f77cd69f62e6aaab17b3186c2f95bc2e` |
| [O3] | mots | cog/s1l2_endlevel.cog | `5f9e8f1b0dadec0338b9ac4bbac7ecfbcce21377c93ab1c935d2c1bc0a401275` |
| [N1] | jk | cog/c3_largelevel.cog | `247dafc2c9acdc4e9541be58ca351340ec8e27845df351632ab11620b3f12269` |
| [N2] | jk | cog/c3_largelevelsrv.cog | `83db3c285c68f080aa226b47471e5ac3d80c652b94cae8de9e5fab62b4bdfeae` |

Exact handler entry lines (1-based in the preserved files):

- [D1]: `startup` 39; `activate` 50; `arrived` 61; `blocked` 73; `timer` 79; `OpenDoors` 85; `CloseDoors` 92; `CheckStatus` 99
- [E1]: `startup` 55; `activated` 63; `arrived` 73; `callfloor` 82; `closedoor` 89; `opendoor` 102; `resetstatus` 118; `move_elev` 129
- [E2]: `crossed` 35; `arrived` 44; `timer` 49
- [E3]: `entered` 22; `activate` 31; `arrived` 41; `timer` 55
- [W1]: `fire` 69; `activated` 99; `deactivated` 110; `selected` 120; `deselected` 146; `autoselect` 168; `timer` 193
- [W2]: `fire` 72; `activated` 111; `deactivated` 125; `selected` 135; `deselected` 164; `autoselect` 186; `timer` 211
- [F1]: `startup` 52; `activated` 62; `deactivated` 83; `pulse` 136; `selected` 147; `killed` 153; `newplayer` 156; `timer` 164
- [F2]: `startup` 56; `activated` 67; `deactivated` 88; `pulse` 153; `selected` 164; `killed` 170; `newplayer` 173; `enterbubble` 181; `exitbubble` 188; `timer` 194
- [F3]: `startup` 54; `activated` 61; `pulse` 80; `deactivated` 137; `timer` 237; `selected` 274; `deselected` 280; `killed` 286; `newplayer` 289; `stop_power` 296
- [F4]: `startup` 60; `activated` 68; `pulse` 90; `deactivated` 156; `timer` 268; `selected` 305; `deselected` 311; `killed` 317; `newplayer` 320; `enterbubble` 326; `exitbubble` 333; `stop_power` 339
- [A1]: `damaged` 35; `killed` 56; `skill` 69
- [A2]: `damaged` 33; `created` 51; `killed` 58; `skill` 64
- [C1]: `startup` 38; `timer` 46; `arrived` 54
- [C2]: `startup` 48; `activated` 55; `playeraction` 86; `pulse` 123; `show_cam` 151; `stop_cam` 157; `trigger` 179
- [I1]: `activated` 25
- [I2]: `activated` 30; `pulse` 70; `killed` 86
- [O1]: `startup` 25; `user0` 52; `shutdown` 78
- [O2]: `startup` 24; `user0` 51
- [O3]: `startup` 34; `user0` 63; `entered` 102; `shutdown` 111
- [N1]: `trigger` 40
- [N2]: `startup` 40; `damaged` 63; `join` 97; `user0` 107


