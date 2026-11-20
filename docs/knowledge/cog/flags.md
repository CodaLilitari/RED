# COG flags and numeric values: an evidence-backed reference

> Copied into RED from [CodaLilitari/jk-mots-cog-workshop@b4b43f6](https://github.com/CodaLilitari/jk-mots-cog-workshop/blob/b4b43f6ab1976f673598c90751ec6160da70c506/docs/FLAGS.md) (MIT; see [LICENSE](LICENSE.md)). Regenerate with `tools/knowledge/import_knowledge.py` rather than editing this copy.

> Read the [master flag findings](https://richard-pemberton-research.darkahn.chatgpt.site/projects/jk-mots-cog-workshop/docs/FLAG_DISCOVERIES.html) for the current interpretations, explicit JKDF2/MotS/Indy evidence and machine-readable companion. Other catalog rows retain their existing evidence grades.

Maintained flag catalog • Evidence updated 2026-10-11

This replaces the working reference for every flag/value family on the historical Massassi page: **26 historical families, 31 total families, 386 catalog entries and all 32 ThingFlags bit positions**. Completeness means coverage of those families and positions. It does **not** mean every meaning has been proven in original JK, original MotS or every enhanced engine. Unknowns and conflicting names remain visible.

The human reference and machine catalog describe the same evidence. The release bundles `corpus/reference/flags.json` and an identical skill `data/flags.json`. Use family IDs for targeted retrieval instead of loading the entire corpus. The old page remains useful historical evidence; it is not the authority for an unverified entry here.

## Evidence and target boundaries

| Evidence | What it establishes | What it does not establish |
| --- | --- | --- |
| Original JK 1.01 binary | Registered ThingFlags wrappers, bounded queue/serializer trace and explicitly marked per-value findings | Every bit meaning, connected-client convergence or every executable version |
| Original MotS binary | Independently traced ThingFlags contract and explicitly marked per-value findings | That all JK meanings carry over, or that source annotations prove MotS-only support |
| P: pinned enhanced source | Exact files attributed to revision `b1be98bd4f170b5384cb71b15443a274aac6a937` | Original-game parity |
| C: current enhanced source | Exact separately hashed files; selected readers/writers and behavior | Membership in the pinned revision or original retail parity |
| Indy comparison | Separately pinned OpenJones3D reconstruction, with selected world/wrapper comparisons | Original Indy execution or transferable JK/MotS labels |
| Retail COG usage | An active call exists in a particular source body | That a branch runs in a level, a client accepts a change, or a bit never appears elsewhere |

**Declaration only** means an exact source symbol/value was found. It is a search key, not a verified behavioral promise. A semantic-looking symbol can be wrong, cross-game, backend-specific or dependent on other state. **Source consumer** describes a selected reader/writer with its conditions. **Unresolved** does not mean unused or safe to repurpose. Original per-bit semantics remain unverified unless specifically stated; original-binary findings apply only to the marked entries and selected paths, not every row.

P/C anchors below use paths relative to `src/`. The JSON source manifest records complete-file hashes. Current-only files carry no pinned revision. Names annotated Jones remain visible without being promoted to JK/MotS support. The maintained bit-30 investigation now includes 40 original-game Wine/Xvfb trials. Other rows retain their stated runtime limits; no connected-client test is claimed. Downloadable 0.1.6 remains an older immutable snapshot. DroidWorks was not analyzed in this flags pass.

## SetThingFlags: what original JK and MotS actually accept

Both inspected originals store the thing flags as a 32-bit word at thing offset0. `SetThingFlags(thing, mask)` ORs a nonzero mask into it; `ClearThingFlags(thing, mask)` ANDs with the complement. They preserve unrelated bits and push no COG result. Neither wrapper has a list of permitted bits or dispatches a separate gameplay operation for each bit. Template `thingflags=` replaces the whole field in the inspected source.

`GetThingFlags` returns the whole word, or integer `0xFFFFFFFF` (signed -1) when thing resolution returns null. Do not treat an invalid result as “every flag is enabled.” Validate the thing before interpreting the word. Accepting arbitrary masks does not establish that arbitrary bits are meaningful, harmless, synchronized or checksum-compatible.

| Original executable identity | Getter | Setter | Clearer |
| --- | --- | --- | --- |
| JK 1.01 SHA-256 `4509cd690634ba20e132bd16e713f91e144b17e2beaaad5e678f1ddb189b51d9` | `0x503D00` | `0x503D30` | `0x503D90` |
| MotS SHA-256 `23cd064217f904d36d896f52d3cdd17635bb85441a12e0415c0637504191d974` | `0x554A50` | `0x554A80` | `0x554AE0` |

These are virtual addresses in images based at 0x400000. Recorded decoding was checked against fresh original instruction bytes and executable hashes; no new disassembler run or execution is claimed.

After mutation, the original wrappers conditionally request update type2. The inspected gates require a nonzero global, no COG-field 0x200, and executing message other than startup3/shutdown22. The queue merges repeated thing pointers and allows 16 distinct entries. A full queue can reject a new entry after its local flags have already changed. The immediate type2 serializer copies the **whole current word at drain time** into payload offset 0x1C, subject to further object/predicate gates. MotS has an additional separate-field guard. Transport, receive handling, scheduling and remote convergence remain untraced. A request is not a broadcast guarantee.

### Retail source examples and limits

The bounded scan of 26 indexed retail COG bodies containing the spelling, with comments removed and active calls read, found these literal masks:

| Target | Active literal masks | Examples |
| --- | --- | --- |
| JK | `0x4`, `0x8`, `0x10`, `0x80000` | `pow_supershield.cog`, `19_payload.cog`, `06_hidefish.cog`, `02_crowend.cog` |
| MotS | `0x4`, `0x10`, `0x14`, `0x80000` | `pow_supershield.cog`, `weap_carbgun_m.cog`, `kyle_m.cog`, `force_pull.cog` |

`0x14` combines 0x10 and 0x4. The MotS Force Protection occurrence of `SetThingFlags(...,4)` is commented out and was excluded. This establishes source use of some bits, not that every undocumented bit has a stock setter example. Template words, engine writers, clear calls and computed masks are other paths; this bounded scan cannot prove nonuse across all custom COGs. Raw retail scripts are not distributed.

### Authoring rules

- Identify the owning field before choosing a number. Actor, weapon, physics, JK, inventory, surface, face and animation flags are separate domains.
- Use OR for adding bits, AND-complement for clearing, and bitwise tests. Do not use addition to accumulate a bit twice. Scalar geometry/light/collision/type values are assignments, not independent switches.
- Prefer lifecycle operations for timer, pulse, water, attachment, death and respawn state. Direct bit mutation does not initialize associated fields, deliver the same notifications or undo a completed transition.
- Use nested validity checks before getter/mutation calls; the studied COG VM does not promise C-style short-circuit evaluation. Do not use mutators as value-returning expressions.
- `0x80000000` is bit31; its signed32-bit representation is-2147483648. Do not classify flags with `flags > 0`. Verify literal parsing/conversion and native argument width for the actual target instead of passing large masks through flex arithmetic and assuming exactness.
- Verify exact native spelling/availability, then arguments, resources and target behavior. The same enum name in another game is not a porting rule.

## Family index

- [Thing flags](#thing) — thing.flags
- [Actor flags](#actor) — actor/player type parameters
- [AI mode flags](#ai) — AI controller mode
- [Attachment flags](#attach) — thing attachment state
- [Collide type values](#collide) — thing collision type
- [Damage types](#damage) — damage operation / projectile damageclass
- [Explosion flags](#explosion) — explosion type parameters
- [Geometry mode values](#geometry) — render geometry mode
- [Inventory definition flags](#inventory) — shared inventory-bin definitions
- [Goal flags](#goal) — goal+100 inventory bin amount
- [World item flags](#item) — world pickup type parameters
- [JK player/saber flags](#jk) — thing.jkFlags
- [Multiplayer mode flags](#multiplayer) — multiplayer game mode
- [Particle flags](#particle) — particle type parameters
- [Physics flags](#physics) — physics movement parameters
- [Sector flags](#sector) — sector.flags
- [Sound flags](#sound) — sound playback/channel state
- [Surface flags](#surface) — world surface.flags
- [Surface adjoin flags](#adjoin) — adjoin.flags
- [Light mode values](#light) — render lighting mode
- [Face type / surface type flags](#face) — face.typeFlags
- [Thing types and masks](#thing_type) — thing.type and type-selection mask
- [Weapon/projectile flags](#weapon) — weapon type parameters
- [Per-player inventory status](#inventory_status) — player inventory state
- [COG execution flags](#cog) — COG instance flags
- [Legacy type aliases](#legacy_type_aliases) — legacy names overlapping type parameters
- [Keyframe playback flags](#keyframe) — PlayKey caller options
- [Texture mode values](#texture) — face or thing desired texture mode
- [Player superflags](#super) — local selected player super-weapon/shield/Force state
- [Multiplayer respawn masks](#respawn) — multiplayer spawn-index selection
- [Surface/material animation flags](#animation) — runtime material/surface animation

<a id="thing"></a>

## Thing flags

**Domain:** thing.flags. **Kind:** `bitmask`.

Every 32-bit position is catalogued. Semantic evidence is bounded and target-specific.

| Bit | Exact source label | Inspected behavior / evidence | Limits |
| --- | --- | --- | --- |
| `0x00000001` | `SITH_TF_EMITLIGHT` | ThingLight nonzero immediate branch enables the bit; template light= also enables it. Current renderer uses it with positive light and excludes disabled/hidden/destroyed things in the inspected light-collection path. P: Cog/sithCogFunctionThing.c:449–468; World/sithThing.c:1786–1793. C: Engine/sithRender.c:706–729. | A bit alone supplies neither positive intensity nor a model. Zero immediate ThingLight updates intensity but does not clear the bit. |
| `0x00000002` | `SITH_TF_DESTROYED` | Marks pending removal: ordinary update takes the RemoveThing branch when set; DestroyThing sets it and conditionally sends removed. P: World/sithThing.c:221–302,448–455. | Setting the bit directly bypasses DestroyThing’s immediate removed-notification step. Clearing it is not resurrection or recreation. |
| `0x00000004` | `SITH_TF_4` | Participates in a projectile deflection/ricochet branch when the colliding weapon has SITH_WF_IMPACTSOUND; successful collision handling reflects velocity, changes orientation and plays deflected sound. C: World/sithWeapon.c:667–691. | Not a demonstrated universal invulnerability switch. Conditions include weapon flags, bounce limits and collision handler success; other damage routes are separate. |
| `0x00000008` | `SITH_TF_LEVELGEO` | Separates model things into the level-geometry rendering pass; the ordinary thing pass excludes them. C: Engine/sithRender.c:2486–2513,2770–2772. | Does not by itself make physics immobile, create collision geometry or ensure a model exists. |
| `0x00000010` | `SITH_TF_10` | Filters a thing out of inspected geometry/ordinary rendering and light-collection paths. C: Engine/sithRender.c:706–707,2490–2492,2770–2772. | Describe as render-hidden in these paths, not global disabled: no claim that scripts, physics, collision or sound stop. |
| `0x00000020` | `SITH_TF_20` | No symbolic consumer found in the bounded reviewed runtime files; remains a numeric enum entry. P: types_enums.h:1329; bounded search scope below. | This is not proof that the bit is unused, safe as user storage, absent in retail, or never set in historical COGs. |
| `0x00000040` | `SITH_TF_STANDABLE` | A RAYCAST_10 collision search only accepts candidate things carrying this bit. Pinned floor-finding chooses that mode for non-wall-stick downward search. P: Engine/sithPhysics.c:66–78. C: Engine/sithCollision.c:243–250. | Still requires ordinary collision eligibility; does not create collision geometry, an actor attachment or a valid floor automatically. |
| `0x00000080` | `SITH_TF_80` | When searchFlags includes RAYCAST_8, only thing candidates with this bit pass that filter. Current CheckPathClear issues 0x12A, which includes8. C: Engine/sithCollision.c:243–250,1367–1386. | Use this exact query-filter meaning; do not call it universal collidable, LOS-blocking or ignored-by-all-physics. Other query modes and candidate checks differ. |
| `0x00000100` | `SITH_TF_INVULN` | Suppresses selected COG notifications such as damaged/removed/sector exited. Pinned multiplayer setup marks non-server non-actor things with it. Current actor damage branch bypasses health subtraction when multiplayer and this flag is set. P: World/sithThing.c:448–491,732–733; Dss/sithMulti.c:185–200. C: World/sithActor.c:90–114,152–169. | Not the separate actor SITH_AF_INVULNERABLE bit. The inspected single-player actor path does not make this thing flag a universal damage shield; suppressing damaged notification can also bypass a script’s damage adjustment. |
| `0x00000200` | `SITH_TF_DEAD` | DamageThing rejects dead things. Current DestroyActor sets DEAD while converting the thing to corpse and changing physics/lifetime; current KillActor returns early when already dead. P: World/sithThing.c:470–477. C: World/sithActor.c:213–222,478–486. | DEAD is distinct from DESTROYED. Setting it alone does not perform the complete killed/corpse sequence or schedule removal. |
| `0x00000400` | `SITH_TF_CAPTURED` | CaptureThing sets capture pointer plus bit; template class COG binding and linked COG references also set it. Several engine notifications test it. P: Cog/sithCogFunctionThing.c:638–658; Cog/sithCog.c:1918–1932; World/sithThing.c:1857–1877. | Setting the bit alone installs no COG pointer or link. ReleaseThing clears the bit only if no class COG or surviving thing link requires it. |
| `0x00000800` | `SITH_TF_NOIMPACTDAMAGE` | Suppresses two explicit crush/impact-damage calls generated when a moving/pushing thing cannot move its attached passenger or collided target far enough. The tested flag is on the mover/pusher. C: Engine/sithCollision.c:682–705,985–1005. | Do not document as blanket fall/impact immunity on the victim. Ordinary collision movement/blocking remains, and unrelated damage paths are not covered. |
| `0x00001000` | `SITH_TF_NOEASY` | Loading computes 0x1000 << jkPlayer_setDiff and frees an explicit level thing when its flags intersect that exclusion mask; this is bit 0x1000 for difficulty index 0. P: World/sithThing.c:1547–1552,1599–1602; types_enums.h:1336. | Identified consumer is a load-time exclusion; runtime SetThingFlags does not rerun the loader. Easy label comes from enum. |
| `0x00002000` | `SITH_TF_NOMEDIUM` | Same load-time mask, bit 0x2000 for difficulty index 1. P: World/sithThing.c:1547–1552,1599–1602; types_enums.h:1337. | Do not present as a continuously enforced runtime hide/disable switch. |
| `0x00004000` | `SITH_TF_NOHARD` | Same load-time mask, bit 0x4000 for difficulty index 2. P: World/sithThing.c:1547–1552,1599–1602; types_enums.h:1338. | Do not present as a continuously enforced runtime hide/disable switch. |
| `0x00008000` | `SITH_TF_8000` | The loader includes 0x8000 in its exclusion mask when g_submodeFlags bit 1 is set. Multiplayer startup sets that submode bit, supporting a multiplayer-exclusion label for this path. P: World/sithThing.c:1547–1552,1599–1602; Dss/sithMulti.c:204–218. | Template/placement/load-time behavior, not a general runtime multiplayer-locality flag. Do not confuse with COG execution or transport flags. |
| `0x00010000` | `SITH_TF_10000` | **Level-placement exclusion in the non-multiplayer loading branch.** The loader adds this bit to its rejection mask when submode bit 1 is clear. A populated level thing whose flags intersect that mask is freed. The corresponding multiplayer branch adds 0x8000 instead. Source multiplayer startup sets the submode bit, supporting the shorthand single-player placement exclusion. Original executable paths. [Evidence and target limits](https://richard-pemberton-research.darkahn.chatgpt.site/projects/jk-mots-cog-workshop/docs/FLAG_DISCOVERIES.html). | This is a loading condition. Setting it after loading does not by itself hide or destroy the thing. The precise original predicate is submode bit 1 clear; other rejection bits, including difficulty, still apply. |
| `0x00020000` | `SITH_TF_PULSESET` | **Engine-maintained thing pulse state.** P Cog/sithCogFunctionThing.c:598–617: SetThingPulse sets bit plus interval/next time, or clears all three for exactly zero. P Cog/sithCog.c:1544–1550: due pulse advances next time and sends the thing PULSE message.  | P World/sithThing.c:236–240 skips this processing while DISABLED. OR-ing the bit alone does not initialize the interval/deadline. This is the thing timer facility, not the COG instance's SetPulse state. |
| `0x00040000` | `SITH_TF_TIMERSET` | **Engine-maintained one-shot thing timer state.** P Cog/sithCogFunctionThing.c:619–636: SetThingTimer writes deadline/bit, or clears both for exactly zero. P Cog/sithCog.c:1551–1554 clears the bit before sending TIMER when due.  | Disabled things skip the update. Setting the flag alone does not establish a meaningful deadline. Both thing timer wrappers are registered outside the nearby MotS branch at P Cog/sithCogFunctionThing.c:2714–2723; this is source registration evidence, not retail proof. |
| `0x00080000` | `SITH_TF_DISABLED` | **Broad disable state/configuration, not just invisibility.** P World/sithThing.c:224–240 still handles lifetime before skipping subsequent timer/control/type updates; :470–477 rejects incoming damage and :732–733/:809–812 suppress sector notifications. C Engine/sithRender.c:2770–2772 excludes disabled ordinary things; C Engine/sithCollision.c:246–250 excludes them from the shown collision-candidate path.  | Lifetime/respawn work is not universally frozen. Dedicated C writers: World/sithItem.c:60–87 sets while awaiting respawn, :116–129 clears on respawn; Gameplay/sithPlayer.c:29–45 sets during slot startup, :471–483 clears when showing player. C Dss/sithDSSThing.c:244–259 has a MotS-specific disabled-to-enabled item/MP/client/respawn-factor/respawn-flag path sending RESPAWN before replacing flags. Not a guarantee that every engine subsystem ignores the thing. |
| `0x00100000` | `SITH_TF_INCAMFOV` | **Renderer-written sighting latch.** C Engine/sithRender.c:3013–3024 checks bit clear and noclip off, sends SIGHTED if CAPTURED, clears AI sleeping mode for an AI with actor data, then sets the bit.  | Happens before the later draw call. A preset bit suppresses this block; it does not force visibility. No dedicated clearing consumer was found in the named-bit searches; generic full-field changes can still clear it. P IsThingVisible at Cog/sithCogFunctionThing.c:1448–1455 instead compares renderFrame+1 against current tick; do not equate these contracts. |
| `0x00200000` | `SITH_TF_RENDERWEAPON` | **Extra-render callback opt-in, with engine initialization writers.** C Engine/sithRender.c:3048–3052 draws base renderData, then calls the registered extra renderer if this bit is set. C Main/jkMain.c:1677–1678 registers jkPlayer_renderSaberWeaponMesh. P World/jkPlayer.c:350–440 sets it for initialized players/boss actors; :2091–2112 sets it in the JKM_DSS info-allocation helper.  | P callback :989–1008 needs playerInfo and puppet data; :1034–1083 branches on persuasion/Seeing, geometry, weapon model and saber flags. Bit alone neither creates weapon resources nor guarantees a visible weapon. It is not a POV-only flag. The similarly named test in current Main/jkControl.c:63 reads actorParams.flags, a different domain, and was not used as Thing.flags evidence. |
| `0x00400000` | `SITH_TF_METAL` | **Material/sound configuration on the supporting or struck thing.** P Engine/sithPuppet.c:647–697 selects metal footstep modes from attachedThing; P World/sithThing.c:1320–1344 selects LANDMETAL. C Gameplay/sithPlayerActions.c:141–154 selects JUMPMETAL; C Engine/sithCollision.c:1032–1044 selects HITMETAL after qualifying collision.  | Sound classes, attachment/marker and movement/collision conditions still apply. Metal wins before Earth in the inspected paired tests. No claim of generic damage resistance, mass, or collision strength. |
| `0x00800000` | `SITH_TF_EARTH` | **Material/sound configuration.** Same pinned footstep/landing paths select earth modes after metal is absent; C Gameplay/sithPlayerActions.c:144–151 selects JUMPEARTH after metal.  | This does not change geometry, friction or physics in those paths. The inspected hit-sound branch has metal-versus-hard, not an Earth branch. |
| `0x01000000` | `SITH_TF_1000000` | **Unresolved.** P enum:1348; no named consumer found.  | Do not import Indy NOSOUND as a proven JK/MotS meaning. Literal/composite paths are not exhausted. |
| `0x02000000` | `SITH_TF_WATER` | **Engine-maintained water/submersion state.** P World/sithThing.c:796–807 compares underwater sector state with this bit and invokes transitions; EnterWater :822 sets it, ExitWater :882 clears it.  | Manually toggling the bit is not equivalent to running transition code: puppet mode, sounds, removal tests and slowdown are separate operations. C World/sithActor.c:226–248 uses it for underwater-death sound after other checks. C AI/sithAI.c:1067–1075 distinguishes observer/target water states, with a surface-physics exception; :823–844 uses it in a conditional movement path. C AI/sithAICmd.c:264–275 uses it for chosen destination height. |
| `0x04000000` | `SITH_TF_IGNOREGOURAUDDISTANCE` | **Rendering configuration bypass for one downgrade.** C Engine/sithRender.c:2946–2969: with ambient below 1, distance at/above gouradDistance and bit clear cap lighting to DIFFUSE; bit set routes to a cap at GOURAUD instead.  | It does not force Gouraud if desired mode is lower, nor override the earlier ambient>=1 branches. yval includes camera zoom adjustment at :2838–2843. No Main_bMotsCompat guard surrounds this consumer in the current function. |
| `0x08000000` | `SITH_TF_DROWNS` | **Destroy-on-water-entry configuration.** P World/sithThing.c:822–830 first sets WATER, then sets DESTROYED if DROWNS, optionally sending REMOVED if CAPTURED and the 0x100 gate is clear.  | Immediate destruction state in this path, not the player's timed breathing/drowning system. It bypasses the ordinary non-drowning splash/slowdown branch. No MotS-only guard around this bit test. |
| `0x10000000` | `SITH_TF_WATERCREATURE` | **Destroy-on-water-exit configuration.** P World/sithThing.c:880–905 clears WATER/changes puppet mode, processes exit sound, then sets DESTROYED and conditionally sends REMOVED.  | Not a general swim/AI-capability flag. Its destruction branch is not prevented by bNoSplash; no MotS-only guard surrounds the bit test. |
| `0x20000000` | `SITH_TF_SPLASHES` | **Splash-message opt-in.** P World/sithThing.c:831–856 entry: non-drowning/no-splash-suppression path, local player exists, this bit set, 0x100 clear; sends SPLASH to the local player's class/capture COG with entering thing as sender and source reference 1. Exit :899–920 excludes WATERCREATURE and applies suppression/local-player/0x100 conditions, using source reference 0.  | This is a message request, not direct creation of a splash effect. Ordinary water sound code is separate and not gated by SPLASHES. The recipient is the local player's COGs, even when another thing entered. No MotS-only guard surrounds these message tests. |
| `0x40000000` | `SITH_TF_40000000` | **Unresolved in bounded JK/MotS source audit.** P enum:1354; no named consumer found.  | Do not import Indy MOVABLE. No unused-bit guarantee; raw/composite masks and original implementations remain possible gaps. |
| `0x80000000` | `SITH_TF_80000000` | **One deferred eligible thing in the MotS renderer.** Original MotS stores an eligible flagged thing in one local pointer slot and skips its ordinary draw. Later eligible flagged things overwrite that slot. After normal traversal, the final stored thing is drawn once. The corresponding complete JK1.01 routine lacks this test and slot. Original executable paths. [Evidence and target limits](https://richard-pemberton-research.darkahn.chatgpt.site/projects/jk-mots-cog-workshop/docs/FLAG_DISCOVERIES.html). | This is not a queue for all flagged things, and it does not establish through-wall rendering. Earlier eligibility/clipping applies. The inspected JK difference is function-specific, not proof of game-wide bit nonuse. No visual runtime result is claimed. |

<a id="actor"></a>

## Actor flags

**Domain:** actor/player type parameters. **Kind:** `bitmask`.

Aliases conflict; Jones annotations are not stock-game support. Get/Set/ClearActorFlags alias type wrappers, which dispatch by actual thing type.

| Value | Exact symbol or descriptive catalog label | Evidence / interpretation |
| --- | --- | --- |
| `0x00000001` (1) | `SITH_AF_CANROTATEHEAD` | Declaration only; per-bit behavior not established in this audit. Legacy alias: `THING_TYPEFLAGS_1` (not resolved by name alone). P: src/types_enums.h:1396 |
| `0x00000002` (2) | `SITH_AF_VIEWCENTRING` | Declaration only; per-bit behavior not established in this audit. Legacy alias: `THING_TYPEFLAGS_FORCE` (not resolved by name alone). P: src/types_enums.h:1397 |
| `0x00000004` (4) | `SITH_AF_HEADLIGHT` | Declaration only; per-bit behavior not established in this audit. Legacy alias: `THING_TYPEFLAGS_DAMAGE` (not resolved by name alone). P: src/types_enums.h:1398 |
| `0x00000008` (8) | `SITH_AF_INVULNERABLE` | In the inspected actor damage path, rejects damage except when the whole damage type equals 0x40. This differs from ThingFlags 0x100; composites do not equal the single fall value. Legacy alias: `THING_TYPEFLAGS_8` (not resolved by name alone). C: World/sithActor.c:90-98 |
| `0x00000010` (16) | `SITH_AF_VIEWCENTRED` | Declaration only; per-bit behavior not established in this audit. Legacy alias: `THING_TYPEFLAGS_LIGHT` (not resolved by name alone). P: src/types_enums.h:1400 |
| `0x00000020` (32) | `SITH_AF_EXPLODE_WHEN_KILLED` | Declaration only; per-bit behavior not established in this audit. Legacy alias: `THING_TYPEFLAGS_20` (not resolved by name alone). P: src/types_enums.h:1401 |
| `0x00000040` (64) | `SITH_AF_BREATHEUNDERWATER` | Declaration only; per-bit behavior not established in this audit. Legacy alias: `THING_TYPEFLAGS_40` (not resolved by name alone). P: src/types_enums.h:1402 |
| `0x00000080` (128) | `SITH_AF_INVISIBLE` | One AI-awareness consumer multiplies awareness by0.05 unless the observer has SEEINVISIBLE, then clamps awareness to at least0.05. Not universal render hiding or undetectability. Legacy alias: `THING_TYPEFLAGS_80` (not resolved by name alone). C: AI/sithAI.c:1623-1655 |
| `0x00000100` (256) | `SITH_AF_DROID` | Declaration only; per-bit behavior not established in this audit. Legacy alias: `THING_TYPEFLAGS_DROID` (not resolved by name alone). P: src/types_enums.h:1404 |
| `0x00000200` (512) | `SITH_AF_BOSS` | Declaration only; per-bit behavior not established in this audit. Legacy alias: `THING_TYPEFLAGS_BOSS` (not resolved by name alone). P: src/types_enums.h:1405 |
| `0x00000400` (1024) | `SITH_AF_DEAF` | Declaration only; per-bit behavior not established in this audit. Legacy alias: `THING_TYPEFLAGS_DEAF` (not resolved by name alone). P: src/types_enums.h:1406 |
| `0x00000800` (2048) | `SITH_AF_BLIND` | Declaration only; per-bit behavior not established in this audit. Legacy alias: `THING_TYPEFLAGS_BLIND` (not resolved by name alone). P: src/types_enums.h:1407 |
| `0x00001000` (4096) | `SITH_AF_1000` | Unresolved numeric/source name; no safe-spare-bit inference. Legacy alias: `THING_TYPEFLAGS_1000` (not resolved by name alone). P: src/types_enums.h:1408 |
| `0x00002000` (8192) | `SITH_AF_BLEEDS` | **Eligibility for the saber-blocking branch.** The saber-hit path checks this bit on an actor/player being hit before admitting it to field-of-view and chance-based blocking checks, which also reject an invisible attacker. A clear bit goes to the damage path. Original executable paths. [Evidence and target limits](https://richard-pemberton-research.darkahn.chatgpt.site/projects/jk-mots-cog-workshop/docs/FLAG_DISCOVERIES.html). The bit permits this blocking path; it does not guarantee a block. BLEEDS is a misleading source alias for this inspected JK/MotS consumer. Other possible consumers have not been exhaustively excluded. |
| `0x00004000` (16384) | `SITH_AF_4000` | Unresolved numeric/source name; no safe-spare-bit inference. Legacy alias: `THING_TYPEFLAGS_4000` (not resolved by name alone). P: src/types_enums.h:1410 |
| `0x00008000` (32768) | `SITH_AF_SEEINVISIBLE` | Declaration only; per-bit behavior not established in this audit. Legacy alias: `THING_TYPEFLAGS_8000` (not resolved by name alone). P: src/types_enums.h:1411 |
| `0x00010000` (65536) | `SITH_AF_SCREAMING` | Declaration only; per-bit behavior not established in this audit. Legacy alias: `THING_TYPEFLAGS_SCREAMING` (not resolved by name alone). Source annotation: Jones: SLIP_SLOPE P: src/types_enums.h:1412 |
| `0x00020000` (131072) | `SITH_AF_DELAYFIRE` | Declaration only; per-bit behavior not established in this audit. Legacy alias: `THING_TYPEFLAGS_20000` (not resolved by name alone). P: src/types_enums.h:1413 |
| `0x00040000` (262144) | `SITH_AF_IMMOBILE` | Declaration only; per-bit behavior not established in this audit. Legacy alias: `THING_TYPEFLAGS_40000` (not resolved by name alone). P: src/types_enums.h:1414 |
| `0x00080000` (524288) | `SITH_AF_NOUNDERWATERFIRE` | Declaration only; per-bit behavior not established in this audit. Legacy alias: `THING_TYPEFLAGS_CANTSHOOTUNDERWATER` (not resolved by name alone). P: src/types_enums.h:1415 |
| `0x00100000` (1048576) | `SITH_AF_NOTARGET` | Declaration only; per-bit behavior not established in this audit. Legacy alias: `THING_TYPEFLAGS_100000` (not resolved by name alone). P: src/types_enums.h:1416 |
| `0x00200000` (2097152) | `SITH_AF_CONTROLSDISABLED` | **Gate player controls, with different look handling.** On the inspected player control path, a set bit skips movement, activation and map handling. Original JK calls its look handler before the flag gate. Original MotS places the look call after the gate, so the flagged path skips it as well. Original executable paths. [Evidence and target limits](https://richard-pemberton-research.darkahn.chatgpt.site/projects/jk-mots-cog-workshop/docs/FLAG_DISCOVERIES.html). This controls processing of these inputs; it does not freeze all physics or prevent every scripted move. Preserve the JK/MotS difference instead of reducing the meaning to immobile. |
| `0x00400000` (4194304) | `SITH_AF_FALLKILLED` | Declaration only; per-bit behavior not established in this audit. Legacy alias: `THING_TYPEFLAGS_400000` (not resolved by name alone). P: src/types_enums.h:1418 |
| `0x00800000` (8388608) | `SITH_AF_NOIDLECAMERA` | Declaration only; per-bit behavior not established in this audit. Legacy alias: `THING_TYPEFLAGS_800000` (not resolved by name alone). P: src/types_enums.h:1419 |
| `0x01000000` (16777216) | `SITH_AF_FULLDAMAGE` | Declaration only; per-bit behavior not established in this audit. Legacy alias: `THING_TYPEFLAGS_1000000` (not resolved by name alone). P: src/types_enums.h:1420 |
| `0x02000000` (33554432) | `SITH_AF_SEEINDARK` | **Skip the target-sector darkness penalty in an AI sight check.** The observer actor bit bypasses the target-sector ambient-light multiplier in the inspected sight-acceptance threshold calculation in both originals. The branch is reached inside the distance and target-light-state gates. Original executable paths. [Evidence and target limits](https://richard-pemberton-research.darkahn.chatgpt.site/projects/jk-mots-cog-workshop/docs/FLAG_DISCOVERIES.html). It does not guarantee detection or sight through geometry. Distance, movement, invisibility, observer blindness, threshold clamping and the final random comparison retain their own effects. Other visibility helpers and runtime probabilities were not established. |
| `0x04000000` (67108864) | `SITH_AF_4000000` | Unresolved numeric/source name; no safe-spare-bit inference. Legacy alias: `THING_TYPEFLAGS_4000000` (not resolved by name alone). P: src/types_enums.h:1422 |
| `0x08000000` (134217728) | `SITH_AF_FREEZE_MOVEMENT` | Declaration only; per-bit behavior not established in this audit. Legacy alias: `THING_TYPEFLAGS_8000000` (not resolved by name alone). P: src/types_enums.h:1423 |
| `0x10000000` (268435456) | `SITH_AF_FLYERMOVE` | Declaration only; per-bit behavior not established in this audit. Legacy alias: `THING_TYPEFLAGS_10000000` (not resolved by name alone). P: src/types_enums.h:1424 |
| `0x20000000` (536870912) | `SITH_AF_SCOPEHUD` | Declaration only; per-bit behavior not established in this audit. Legacy alias: `THING_TYPEFLAGS_20000000` (not resolved by name alone). P: src/types_enums.h:1425 |
| `0x40000000` (1073741824) | `SITH_AF_ELECTRICWHIP` | Declaration only; per-bit behavior not established in this audit. Legacy alias: `THING_TYPEFLAGS_40000000` (not resolved by name alone). Source annotation: Jones: ELECTRICT_WHIP P: src/types_enums.h:1426 |
| `0x80000000` (2147483648) | `SITH_AF_ARACHNID` | Declaration only; per-bit behavior not established in this audit. Legacy alias: `THING_TYPEFLAGS_80000000` (not resolved by name alone). Source annotation: Jones: ARACHNID P: src/types_enums.h:1427 |

<a id="ai"></a>

## AI mode flags

**Domain:** AI controller mode. **Kind:** `bitmask`.

AISetMode ORs and AIClearMode clears; it can cause an AI event. A mode bit does not initialize destinations, targets or instincts. Jones annotations remain separate.

| Value | Exact symbol or descriptive catalog label | Evidence / interpretation |
| --- | --- | --- |
| `0x00000001` (1) | `SITHAI_MODE_MOVING` | Declaration only; per-bit behavior not established in this audit. P: src/types_enums.h:862 |
| `0x00000002` (2) | `SITHAI_MODE_ATTACKING` | Declaration only; per-bit behavior not established in this audit. P: src/types_enums.h:863 |
| `0x00000004` (4) | `SITHAI_MODE_SEARCHING` | Declaration only; per-bit behavior not established in this audit. P: src/types_enums.h:864 |
| `0x00000008` (8) | `SITHAI_MODE_TURNING` | Declaration only; per-bit behavior not established in this audit. P: src/types_enums.h:865 |
| `0x00000010` (16) | `SITHAI_MODE_HASDEST` | Declaration only; per-bit behavior not established in this audit. P: src/types_enums.h:866 |
| `0x00000020` (32) | `SITHAI_MODE_TOUGHSKIN` | **Fire-target state maintained by AISetFireTarget.** AISetFireTarget stores the target pointer and timestamp, then sets this AI-control-block bit when the target is non-null and clears it otherwise. If the flags change, it emits the mode-change event. Both original wrappers agree. Original executable paths. [Evidence and target limits](https://richard-pemberton-research.darkahn.chatgpt.site/projects/jk-mots-cog-workshop/docs/FLAG_DISCOVERIES.html). TOUGHSKIN is not evidence of armor here. Setting the bit alone does not assign a target pointer or cause firing; use AISetFireTarget. Other possible consumers are not exhaustively excluded. |
| `0x00000040` (64) | `SITHAI_MODE_NOCHECKFORCLIFF` | Declaration only; per-bit behavior not established in this audit. P: src/types_enums.h:868 |
| `0x00000080` (128) | `SITHAI_MODE_UNK80` | Unresolved numeric/source name; no safe-spare-bit inference. P: src/types_enums.h:869 |
| `0x00000100` (256) | `SITHAI_MODE_UNK100` | **Mode-change event; persistent-mode meaning remains unproved.** The inspected use is an AI event identifier and instinct event-subscription mask. AISetMode emits event 0x100 whenever its OR changes the mode word. The event dispatcher sends COG AIEVENT with first extended parameter 256.0 in original JK, but current AI flags converted to float in original MotS. Original executable paths. [Evidence and target limits](https://richard-pemberton-research.darkahn.chatgpt.site/projects/jk-mots-cog-workshop/docs/FLAG_DISCOVERIES.html). This resolves an event role, not a special persistent AI mode. Generic mode setters can store 0x100; that does not prove a dedicated mode behavior. The dispatcher has living/class/disabled/sleeping gates. MotS float conversion need not preserve every high-bit integer exactly. |
| `0x00000200` (512) | `SITHAI_MODE_ACTIVE` | Declaration only; per-bit behavior not established in this audit. P: src/types_enums.h:871 |
| `0x00000400` (1024) | `SITHAI_MODE_TARGETVISIBLE` | Declaration only; per-bit behavior not established in this audit. P: src/types_enums.h:872 |
| `0x00000800` (2048) | `SITHAI_MODE_FLEEING` | Declaration only; per-bit behavior not established in this audit. P: src/types_enums.h:873 |
| `0x00001000` (4096) | `SITHAI_MODE_SLEEPING` | Declaration only; per-bit behavior not established in this audit. P: src/types_enums.h:874 |
| `0x00002000` (8192) | `SITHAI_MODE_DISABLED` | Declaration only; per-bit behavior not established in this audit. P: src/types_enums.h:875 |
| `0x00010000` (65536) | `SITHAI_MODE_WANTALLEVENTS` | Declaration only; per-bit behavior not established in this audit. Source annotation: Jones P: src/types_enums.h:876 |
| `0x00200000` (2097152) | `SITHAI_MODE_WALLCRAWLING` | Declaration only; per-bit behavior not established in this audit. Source annotation: Jones probably P: src/types_enums.h:877 |
| `0x02000000` (33554432) | `SITHAI_MODE_NOCHASING` | Declaration only; per-bit behavior not established in this audit. Source annotation: Jones P: src/types_enums.h:878 |
| `0x08000000` (134217728) | `SITHAI_MODE_ARMOREDSKIN` | Declaration only; per-bit behavior not established in this audit. Source annotation: Jones P: src/types_enums.h:879 |

Unlisted positions in this selected declaration inventory: `0x00004000`, `0x00008000`, `0x00020000`, `0x00040000`, `0x00080000`, `0x00100000`, `0x00400000`, `0x00800000`, `0x01000000`, `0x04000000`, `0x10000000`, `0x20000000`, `0x40000000`, `0x80000000`. These remain unresolved, not proved unused.

<a id="attach"></a>

## Attachment flags

**Domain:** thing attachment state. **Kind:** `bitmask`.

Zero means no bits set. A raw flag write does not construct surface/thing pointers or linked lists. Use the dedicated attachment operation; 0x10 has a MotS annotation.

| Value | Exact symbol or descriptive catalog label | Evidence / interpretation |
| --- | --- | --- |
| `0x00000001` (1) | `SITH_ATTACH_SURFACE` | **Surface attachment discriminator.** The native surface attachment setter records its target and writes discriminator 1 into attach_flags. Surface attachment stores a surface; Thing-face stores a parent and face; ordinary Thing attachment maintains parent/child links and a relative position. Detach selects its surface or shared Thing/face branch from this separate field. Original executable paths. [Evidence and target limits](https://richard-pemberton-research.darkahn.chatgpt.site/projects/jk-mots-cog-workshop/docs/FLAG_DISCOVERIES.html). Attachment flags are not Thing.flags: their offsets are JK +0x34 and MotS +0x3C. AttachThingToThingEx ORs its extra mask into this field after ordinary attachment. Arbitrary masks, movement propagation and complete message delivery are unproved; this does not resolve Thing 0x01000000 or attachment 0x8/0x10. |
| `0x00000002` (2) | `SITH_ATTACH_THINGFACE` | **Thing-face attachment discriminator.** The native Thing-face attachment setter records its target and writes discriminator 2 into attach_flags. Surface attachment stores a surface; Thing-face stores a parent and face; ordinary Thing attachment maintains parent/child links and a relative position. Detach selects its surface or shared Thing/face branch from this separate field. Original executable paths. [Evidence and target limits](https://richard-pemberton-research.darkahn.chatgpt.site/projects/jk-mots-cog-workshop/docs/FLAG_DISCOVERIES.html). Attachment flags are not Thing.flags: their offsets are JK +0x34 and MotS +0x3C. AttachThingToThingEx ORs its extra mask into this field after ordinary attachment. Arbitrary masks, movement propagation and complete message delivery are unproved; this does not resolve Thing 0x01000000 or attachment 0x8/0x10. |
| `0x00000004` (4) | `SITH_ATTACH_THING` | **Ordinary thing attachment discriminator.** The native ordinary Thing attachment setter records its target and writes discriminator 4 into attach_flags. Surface attachment stores a surface; Thing-face stores a parent and face; ordinary Thing attachment maintains parent/child links and a relative position. Detach selects its surface or shared Thing/face branch from this separate field. Original executable paths. [Evidence and target limits](https://richard-pemberton-research.darkahn.chatgpt.site/projects/jk-mots-cog-workshop/docs/FLAG_DISCOVERIES.html). Attachment flags are not Thing.flags: their offsets are JK +0x34 and MotS +0x3C. AttachThingToThingEx ORs its extra mask into this field after ordinary attachment. Arbitrary masks, movement propagation and complete message delivery are unproved; this does not resolve Thing 0x01000000 or attachment 0x8/0x10. |
| `0x00000008` (8) | `SITH_ATTACH_NOMOVE` | Declaration only; per-bit behavior not established in this audit. P: src/types_enums.h:1121 |
| `0x00000010` (16) | `SITH_ATTACH_FORCE_SERIALIZE` | Declaration only; per-bit behavior not established in this audit. Source annotation: MOTS added: Jail Key P: src/types_enums.h:1122 |

Unlisted positions in this selected declaration inventory: `0x00000020`, `0x00000040`, `0x00000080`, `0x00000100`, `0x00000200`, `0x00000400`, `0x00000800`, `0x00001000`, `0x00002000`, `0x00004000`, `0x00008000`, `0x00010000`, `0x00020000`, `0x00040000`, `0x00080000`, `0x00100000`, `0x00200000`, `0x00400000`, `0x00800000`, `0x01000000`, `0x02000000`, `0x04000000`, `0x08000000`, `0x10000000`, `0x20000000`, `0x40000000`, `0x80000000`. These remain unresolved, not proved unused.

<a id="collide"></a>

## Collide type values

**Domain:** thing collision type. **Kind:** `scalar`.

A scalar enumeration; never add values as though independent switches.

| Value | Exact symbol or descriptive catalog label | Evidence / interpretation |
| --- | --- | --- |
| `0x00000000` (0) | `SITH_COLLIDE_NONE` | Declaration only; per-bit behavior not established in this audit. P: src/types_enums.h:402 |
| `0x00000001` (1) | `SITH_COLLIDE_SPHERE` | Declaration only; per-bit behavior not established in this audit. P: src/types_enums.h:403 |
| `0x00000002` (2) | `SITH_COLLIDE_SPHERE_TREE` | Declaration only; per-bit behavior not established in this audit. P: src/types_enums.h:404 |
| `0x00000003` (3) | `SITH_COLLIDE_FACE` | Declaration only; per-bit behavior not established in this audit. P: src/types_enums.h:405 |

<a id="damage"></a>

## Damage types

**Domain:** damage operation / projectile damageclass. **Kind:** `integer-with-mixed-consumers`.

Some consumers compare exact values or switch on the whole integer. Combining bits does not guarantee additive behavior. FIRE is the source name for 0x4; FALL0x40 is separate.

| Value | Exact symbol or descriptive catalog label | Evidence / interpretation |
| --- | --- | --- |
| `0x00000001` (1) | `SITH_DAMAGE_IMPACT` | Declaration only; per-bit behavior not established in this audit. P: src/types_enums.h:1313 |
| `0x00000002` (2) | `SITH_DAMAGE_ENERGY` | Declaration only; per-bit behavior not established in this audit. P: src/types_enums.h:1314 |
| `0x00000004` (4) | `SITH_DAMAGE_FIRE` | Declaration only; per-bit behavior not established in this audit. P: src/types_enums.h:1315 |
| `0x00000008` (8) | `SITH_DAMAGE_FORCE` | Declaration only; per-bit behavior not established in this audit. P: src/types_enums.h:1316 |
| `0x00000010` (16) | `SITH_DAMAGE_SABER` | Declaration only; per-bit behavior not established in this audit. P: src/types_enums.h:1317 |
| `0x00000020` (32) | `SITH_DAMAGE_DROWN` | Declaration only; per-bit behavior not established in this audit. P: src/types_enums.h:1318 |
| `0x00000040` (64) | `SITH_DAMAGE_FALL` | Declaration only; per-bit behavior not established in this audit. P: src/types_enums.h:1319 |

<a id="explosion"></a>

## Explosion flags

**Domain:** explosion type parameters. **Kind:** `bitmask`.

Separate from thing.flags and damageclass. Resources, radius, times and child templates remain necessary. Some debris annotations are uncertain Jones ancestry.

| Value | Exact symbol or descriptive catalog label | Evidence / interpretation |
| --- | --- | --- |
| `0x00000001` (1) | `SITHEXPLOSION_FLAG_ANIMATED_SPRITE` | Only the sprite-render branch animates a multi-cel material here; missing/one-cel material clears this bit. C: World/sithExplosion.c:19 |
| `0x00000002` (2) | `SITHEXPLOSION_FLAG_HAS_BLAST_PHASE` | Pending blast phase: at its lifetime threshold, calls MakeBlast and clears this bit. C: World/sithExplosion.c:48 |
| `0x00000004` (4) | `SITHEXPLOSION_FLAG_DAMAGE_IN_BLAST_RADIUS` | Loader damage parameter sets this plus 0x2. The inspected MakeBlast gates use positive range and damage/force, not a test of 0x4; not a universal sole damage-enable gate. C: World/sithExplosion.c:146 |
| `0x00000008` (8) | `SITHEXPLOSION_FLAG_HAS_CHILD_EXPLOSION` | Declaration only; per-bit behavior not established in this audit. C: src/World/sithExplosion.h:16 |
| `0x00000010` (16) | `SITHEXPLOSION_FLAG_VARIABLE_LIGHT` | Variable-light update additionally requires ThingFlags EMITLIGHT and life/blast timing fields. C: World/sithExplosion.c:54 |
| `0x00000020` (32) | `RANDOM_Z_ORIENTATION` | Creation postrotates orientation about Z by a random value. The consumer borrows SITH_TF_20 but reads explosionParams.flags; no explosion enum declaration exists. Source annotation: Descriptive catalog label. C: src/World/sithExplosion.c:32 |
| `0x00000040` (64) | `SITHEXPLOSION_FLAG_NO_DAMAGE_TO_SHOOTER` | Excludes parent only when pointer AND signature match; the outer gate covers force as well as damage. C: World/sithExplosion.c:100 |
| `0x00000080` (128) | `SITHEXPLOSION_FLAG_RANDOM_DEBRIS` | Declaration names random debris, but the inspected debris loop creates nonnull debris templates without testing this bit. Not established as a universal prerequisite. C: World/sithExplosion.c:121 |
| `0x00000100` (256) | `SITHEXPLOSION_FLAG_FLASH_BLINDS_THINGS` | Inspected render path computes a local palette effect and clears this bit. General AI blindness is not established. C: Engine/sithRender.c:3061-3071 |
| `0x00000200` (512) | `SITHEXPLOSION_FLAG_ANIMATE_DEBRIS_MATERIAL` | Declaration only; per-bit behavior not established in this audit. Source annotation: maybe jones specific, makes the explosion debries mat to animate C: src/World/sithExplosion.h:21 |
| `0x00000400` (1024) | `SITHEXPLOSION_FLAG_UPDATE_DEBRIS_MATERIAL` | Declaration only; per-bit behavior not established in this audit. Source annotation: maybe jones specific, makes the explosion debries mat the same to the mat of the hit surface C: src/World/sithExplosion.h:22 |
| `0x00000800` (2048) | `SITHEXPLOSION_USE_EXPAND_TIME` | Declaration only; per-bit behavior not established in this audit. C: src/World/sithExplosion.h:23 |
| `0x00001000` (4096) | `SITHEXPLOSION_FLAG_USE_FADE_TIME` | Declaration only; per-bit behavior not established in this audit. C: src/World/sithExplosion.h:24 |

Unlisted positions in this selected declaration inventory: `0x00002000`, `0x00004000`, `0x00008000`, `0x00010000`, `0x00020000`, `0x00040000`, `0x00080000`, `0x00100000`, `0x00200000`, `0x00400000`, `0x00800000`, `0x01000000`, `0x02000000`, `0x04000000`, `0x08000000`, `0x10000000`, `0x20000000`, `0x40000000`, `0x80000000`. These remain unresolved, not proved unused.

<a id="geometry"></a>

## Geometry mode values

**Domain:** render geometry mode. **Kind:** `scalar`.

Value4 is named FULL in this source;5 is explicitly unknown. Do not promote the historical description of5 without a consumer/target test.

| Value | Exact symbol or descriptive catalog label | Evidence / interpretation |
| --- | --- | --- |
| `0x00000000` (0) | `RD_GEOMETRY_NONE` | Declaration only; per-bit behavior not established in this audit. P: src/types_enums.h:11 |
| `0x00000001` (1) | `RD_GEOMETRY_VERTEX` | Declaration only; per-bit behavior not established in this audit. P: src/types_enums.h:12 |
| `0x00000002` (2) | `RD_GEOMETRY_WIREFRAME` | Declaration only; per-bit behavior not established in this audit. P: src/types_enums.h:13 |
| `0x00000003` (3) | `RD_GEOMETRY_SOLID` | Declaration only; per-bit behavior not established in this audit. P: src/types_enums.h:14 |
| `0x00000004` (4) | `RD_GEOMETRY_FULL` | Declaration only; per-bit behavior not established in this audit. P: src/types_enums.h:15 |
| `0x00000005` (5) | `RD_GEOMODE_5_UNK` | Unresolved numeric/source name; no safe-spare-bit inference. P: src/types_enums.h:16 |

<a id="inventory"></a>

## Inventory definition flags

**Domain:** shared inventory-bin definitions. **Kind:** `bitmask`.

SetInvFlags ORs shared definition flags, not per-player availability. The source names 0x8 AUTOAIM; a bin being a Force power does not prove that is its universal effect.

| Value | Exact symbol or descriptive catalog label | Evidence / interpretation |
| --- | --- | --- |
| `0x00000001` (1) | `SITHINVENTORY_TYPE_REGISTERED` | Declaration only; per-bit behavior not established in this audit. P: src/types_enums.h:1096 |
| `0x00000002` (2) | `ITEMINFO_ITEM` | Declaration only; per-bit behavior not established in this audit. P: src/types_enums.h:1097 |
| `0x00000004` (4) | `ITEMINFO_WEAPON` | Declaration only; per-bit behavior not established in this audit. P: src/types_enums.h:1098 |
| `0x00000008` (8) | `SITHINVENTORY_TYPE_AUTOAIM` | Declaration only; per-bit behavior not established in this audit. P: src/types_enums.h:1099 |
| `0x00000010` (16) | `SITHINVENTORY_TYPE_DAMAGEABLE` | Declaration only; per-bit behavior not established in this audit. P: src/types_enums.h:1100 |
| `0x00000020` (32) | `ITEMINFO_DEFAULT` | Declaration only; per-bit behavior not established in this audit. P: src/types_enums.h:1101 |
| `0x00000040` (64) | `SITHINVENTORY_TYPE_NOT_CARRIED_BETWEEN_LEVELS` | Declaration only; per-bit behavior not established in this audit. P: src/types_enums.h:1102 |
| `0x00000080` (128) | `SITHINVENTORY_TYPE_BACKPACKITEM` | Declaration only; per-bit behavior not established in this audit. P: src/types_enums.h:1103 |
| `0x00000100` (256) | `ITEMINFO_HOTKEYABLE` | Declaration only; per-bit behavior not established in this audit. P: src/types_enums.h:1104 |

Unlisted positions in this selected declaration inventory: `0x00000200`, `0x00000400`, `0x00000800`, `0x00001000`, `0x00002000`, `0x00004000`, `0x00008000`, `0x00010000`, `0x00020000`, `0x00040000`, `0x00080000`, `0x00100000`, `0x00200000`, `0x00400000`, `0x00800000`, `0x01000000`, `0x02000000`, `0x04000000`, `0x08000000`, `0x10000000`, `0x20000000`, `0x40000000`, `0x80000000`. These remain unresolved, not proved unused.

<a id="goal"></a>

## Goal flags

**Domain:** goal+100 inventory bin amount. **Kind:** `bitmask-in-inventory-amount`.

SetGoalFlags/ClearGoalFlags modify an integer cast of inventory amount, then use the normal clamping/changed path. The secret bit is commented unused in the declaration.

| Value | Exact symbol or descriptive catalog label | Evidence / interpretation |
| --- | --- | --- |
| `0x00000001` (1) | `GOAL_EXISTS` | Declaration only; per-bit behavior not established in this audit. P: src/types_enums.h:1089 |
| `0x00000002` (2) | `GOAL_COMPLETE` | Declaration only; per-bit behavior not established in this audit. P: src/types_enums.h:1090 |
| `0x00000004` (4) | `GOAL_SECRET` | Declaration only; per-bit behavior not established in this audit. Source annotation: Unused, makes text show as yellow P: src/types_enums.h:1091 |

<a id="item"></a>

## World item flags

**Domain:** world pickup type parameters. **Kind:** `bitmask`.

Respawn needs stored position/sector and timing. BACKPACK does not initialize contents. The header carries an uncertain Jones annotation; reviewed consumers support narrower conclusions.

| Value | Exact symbol or descriptive catalog label | Evidence / interpretation |
| --- | --- | --- |
| `0x00000001` (1) | `SITH_ITEM_RESPAWN_MP` | Selects multiplayer item respawn path when the mode and associated respawn state qualify. The path also disables the item and schedules lifetime; the flag alone does not initialize the return location. C: World/sithItem.c:60-136 |
| `0x00000002` (2) | `SITH_ITEM_RESPAWN_SP` | Selects single-player item respawn path when the mode and associated respawn state qualify. The path also disables the item and schedules lifetime; the flag alone does not initialize the return location. C: World/sithItem.c:60-136 |
| `0x00000004` (4) | `SITH_ITEM_BACKPACK` | Backpack construction sets this bit and initializes contained-bin state. Bit-only mutation does not manufacture contents. P: Gameplay/sithInventory.c:952-980,1034-1065 |
| `0x00000008` (8) | `SITH_ITEM_8` | Unresolved numeric/source name; no safe-spare-bit inference. P: src/types_enums.h:175 |
| `0x00000010` (16) | `SITH_ITEM_10` | In original MotS, the registered weapon-versus-item collision handler checks whether the collided thing is type 5 (item). An item lacking item flag 0x10 immediately returns 0. A non-item or an item carrying the bit continues into subsequent weapon-collision processing. The compared original JK handler lacks this initial item-bit filter. Shootable powerup is a useful shorthand for this gate, not a complete damage or destruction contract. Later weapon/item settings and paths still matter. The JK result describes only the compared handler entry, not game-wide absence of all possible item-bit uses. No in-game collision or damage test was run. Original evidence: JK absent-in-compared-collision-handler; MotS bounded-original-static. See [master findings](https://richard-pemberton-research.darkahn.chatgpt.site/projects/jk-mots-cog-workshop/docs/FLAG_DISCOVERIES.html). |

Unlisted positions in this selected declaration inventory: `0x00000020`, `0x00000040`, `0x00000080`, `0x00000100`, `0x00000200`, `0x00000400`, `0x00000800`, `0x00001000`, `0x00002000`, `0x00004000`, `0x00008000`, `0x00010000`, `0x00020000`, `0x00040000`, `0x00080000`, `0x00100000`, `0x00200000`, `0x00400000`, `0x00800000`, `0x01000000`, `0x02000000`, `0x04000000`, `0x08000000`, `0x10000000`, `0x20000000`, `0x40000000`, `0x80000000`. These remain unresolved, not proved unused.

<a id="jk"></a>

## JK player/saber flags

**Domain:** thing.jkFlags. **Kind:** `bitmask`.

JKSetFlags ORs and JKClearFlags clears. JKGetFlags returns0 on invalid thing in the inspected source, unlike GetThingFlags. Model/puppet and saber state remain required.

| Value | Exact symbol or descriptive catalog label | Evidence / interpretation |
| --- | --- | --- |
| `0x00000001` (1) | `JKFLAG_SABERON` | Reviewed extra-render callback branches on saber-on/dual-sabers or persuasion/Seeing. It needs valid player/puppet/model state; the bit alone is not the complete feature. P: World/jkPlayer.c:989-1083 |
| `0x00000002` (2) | `JKFLAG_SABERDAMAGE` | Declaration only; per-bit behavior not established in this audit. C: src/World/jkPlayer.h:69 |
| `0x00000004` (4) | `JKFLAG_SABEREXTEND` | Declaration only; per-bit behavior not established in this audit. C: src/World/jkPlayer.h:70 |
| `0x00000008` (8) | `JKFLAG_SABERRETRACT` | Declaration only; per-bit behavior not established in this audit. C: src/World/jkPlayer.h:71 |
| `0x00000010` (16) | `JKFLAG_DUALSABERS` | Reviewed extra-render callback branches on saber-on/dual-sabers or persuasion/Seeing. It needs valid player/puppet/model state; the bit alone is not the complete feature. P: World/jkPlayer.c:989-1083 |
| `0x00000020` (32) | `JKFLAG_PERSUASION` | Reviewed extra-render callback branches on saber-on/dual-sabers or persuasion/Seeing. It needs valid player/puppet/model state; the bit alone is not the complete feature. P: World/jkPlayer.c:989-1083 |
| `0x00000040` (64) | `JKFLAG_40` | **Consume-once saber hit-history reset.** When the eligible saber collision path sees this bit, it clears the bit and resets the counts of already-hit things and surfaces. The stored histories are pointer lists. The separate saber-damage-enable bit is tested afterward. Original executable paths. [Evidence and target limits](https://richard-pemberton-research.darkahn.chatgpt.site/projects/jk-mots-cog-workshop/docs/FLAG_DISCOVERIES.html). This requests a reset when the path next consumes it; it does not itself enable damage or guarantee another hit. Source puppet marker 0xE sets it, but the original marker dispatch table was not independently reconstructed in this pass. |
| `0x00000080` (128) | `JKFLAG_SABERFORCEON` | **Force maximum saber length on the eligible update branch.** When neither the extend-4 nor retract-8 branch is selected, bit 0x80 sets saber length to its configured maximum, writes about 0.6 to the source-named timeLeftLengthChange field, clears 4/8 and sets 1. Both original branches retain 0x80; current enhanced source clears it. Original executable paths. [Evidence and target limits](https://richard-pemberton-research.darkahn.chatgpt.site/projects/jk-mots-cog-workshop/docs/FLAG_DISCOVERIES.html). The inspected callers already require JK flag 1 and the routine needs valid player information. Setting 0x80 alone does not enter that caller path. Extend/retract branches take priority. This is not a universal unretractable-saber or runtime appearance claim. |
| `0x00000100` (256) | `JKFLAG_100` | **MotS bubble-owner registration marker.** jkCreateBubble registers owner/radius-squared/type in the bubble array and sets this bit on the owner. jkDestroyBubble clears both the record and bit. A query removes a registered record if its owner no longer has the bit. Original executable paths. [Evidence and target limits](https://richard-pemberton-research.darkahn.chatgpt.site/projects/jk-mots-cog-workshop/docs/FLAG_DISCOVERIES.html). This marks the bubble owner, not a thing merely located inside a bubble. Setting the bit alone does not allocate a bubble record or specify radius/type. Use the bubble verbs. An original-JK meaning is not established; lack of bubble verbs is not proof that the bit is universally unused. |

Unlisted positions in this selected declaration inventory: `0x00000200`, `0x00000400`, `0x00000800`, `0x00001000`, `0x00002000`, `0x00004000`, `0x00008000`, `0x00010000`, `0x00020000`, `0x00040000`, `0x00080000`, `0x00100000`, `0x00200000`, `0x00400000`, `0x00800000`, `0x01000000`, `0x02000000`, `0x04000000`, `0x08000000`, `0x10000000`, `0x20000000`, `0x40000000`, `0x80000000`. These remain unresolved, not proved unused.

<a id="multiplayer"></a>

## Multiplayer mode flags

**Domain:** multiplayer game mode. **Kind:** `bitmask`.

Named bits do not establish every server/client effect. COOP is an enhanced-fork addition in the inspected spawn logic, not claimed original retail.

| Value | Exact symbol or descriptive catalog label | Evidence / interpretation |
| --- | --- | --- |
| `0x00000001` (1) | `MULTIMODEFLAG_TEAMS` | Enables team-score accumulation and team-score fields in the inspected send/receive branches. P: Dss/sithMulti.c:426-429,843-849,942-948 |
| `0x00000002` (2) | `MULTIMODEFLAG_2` | **Conditional same-team player health-damage suppression.** The original actor/player damage paths test multiplayer mode bit 0x2 and return zero before health subtraction when a same-team helper succeeds. That helper also requires teams bit 0x1, distinct player things, non-null player records, and equal nonzero team numbers. The source parent is compared with the victim. Original executable paths. [Evidence and target limits](https://richard-pemberton-research.darkahn.chatgpt.site/projects/jk-mots-cog-workshop/docs/FLAG_DISCOVERIES.html). Self-damage, team zero and unequal teams do not meet this predicate. Earlier COG/inventory callbacks may already have run. This does not promise suppression of all messages, every other damage path or network convergence. |
| `0x00000004` (4) | `MULTIMODEFLAG_4` | When SET, skips ordinary kills-minus-suicides score recomputation. Does not disable every scoring path. P: Dss/sithMulti.c:417-431 |
| `0x00000008` (8) | `MULTIMODEFLAG_TIMELIMIT` | Enables the inspected server time-limit path. P: Dss/sithMulti.c:1283-1310 |
| `0x00000010` (16) | `MULTIMODEFLAG_SCORELIMIT` | Enables the inspected server score-limit gate. P: Dss/sithMulti.c:433-441 |
| `0x00000020` (32) | `MULTIMODEFLAG_20` | Unresolved numeric/source name; no safe-spare-bit inference. P: src/types_enums.h:634 |
| `0x00000040` (64) | `MULTIMODEFLAG_40` | Unresolved numeric/source name; no safe-spare-bit inference. P: src/types_enums.h:635 |
| `0x00000080` (128) | `MULTIMODEFLAG_SINGLE_LEVEL` | Declaration only; per-bit behavior not established in this audit. P: src/types_enums.h:636 |
| `0x00000100` (256) | `MULTIMODEFLAG_100` | Host starts on team1; with teams 0x1, later initialization alternates team assignment. P: Dss/sithMulti.c:150-153,1425-1426 |
| `0x00000200` (512) | `MULTIMODEFLAG_200` | Unresolved numeric/source name; no safe-spare-bit inference. P: src/types_enums.h:638 |
| `0x00000400` (1024) | `MULTIMODEFLAG_400` | Unresolved numeric/source name; no safe-spare-bit inference. P: src/types_enums.h:639 |
| `0x00000800` (2048) | `MULTIMODEFLAG_800` | Unresolved numeric/source name; no safe-spare-bit inference. P: src/types_enums.h:640 |
| `0x00001000` (4096) | `MULTIMODEFLAG_1000` | Unresolved numeric/source name; no safe-spare-bit inference. Source annotation: MOTS uses P: src/types_enums.h:641 |
| `0x00002000` (8192) | `MULTIMODEFLAG_2000` | Unresolved numeric/source name; no safe-spare-bit inference. P: src/types_enums.h:642 |
| `0x00004000` (16384) | `MULTIMODEFLAG_4000` | Unresolved numeric/source name; no safe-spare-bit inference. P: src/types_enums.h:643 |
| `0x00008000` (32768) | `MULTIMODEFLAG_8000` | Unresolved numeric/source name; no safe-spare-bit inference. P: src/types_enums.h:644 |
| `0x00010000` (65536) | `MULTIMODEFLAG_COOP` | Declaration only; per-bit behavior not established in this audit. P: src/types_enums.h:645 |

Unlisted positions in this selected declaration inventory: `0x00020000`, `0x00040000`, `0x00080000`, `0x00100000`, `0x00200000`, `0x00400000`, `0x00800000`, `0x01000000`, `0x02000000`, `0x04000000`, `0x08000000`, `0x10000000`, `0x20000000`, `0x40000000`, `0x80000000`. These remain unresolved, not proved unused.

<a id="particle"></a>

## Particle flags

**Domain:** particle type parameters. **Kind:** `bitmask`.

EMIT_LIGHT selects a particle lighting mode in the inspected constructor, not demonstrated dynamic-world illumination. RANDOM_CEL_CHANGE0x20 is not a verified geometric flip.

| Value | Exact symbol or descriptive catalog label | Evidence / interpretation |
| --- | --- | --- |
| `0x00000001` (1) | `SITHPARTICLE_FLAG_OUTWARD_EXPANDING` | Updates scale using growthSpeed and stored scale. Sign and parameters still determine motion. C: Engine/sithParticle.c:178-225 |
| `0x00000002` (2) | `SITHPARTICLE_FLAG_ANIMATE_CEL` | Declaration only; per-bit behavior not established in this audit. P: src/types_enums.h:431 |
| `0x00000004` (4) | `SITHPARTICLE_FLAG_RANDOM_START_CEL` | Creation chooses starting cels using the material cel count. C: Engine/sithParticle.c:312-323 |
| `0x00000008` (8) | `SITHPARTICLE_FLAG_FADE_OUT_OVER_TIME` | Destruction path reduces particle count and gives a surviving cloud another short lifetime; not demonstrated smooth alpha fading. C: Engine/sithParticle.c:337-365 |
| `0x00000010` (16) | `SITHPARTICLE_FLAG_EMIT_LIGHT` | New-cloud creation selects lighting mode0 instead of2. This does not demonstrate dynamic illumination of nearby objects. Existing-cloud duplication is separate. C: Engine/sithParticle.c:260-278; Primitives/rdParticle.h:39-40; P: types_enums.h:22-25 |
| `0x00000020` (32) | `SITHPARTICLE_FLAG_RANDOM_CEL_CHANGE` | Conditionally increments particle cels toward later cels; does not prove a geometric flip or an arbitrary random new cel. C: Engine/sithParticle.c:178-225 |
| `0x00000040` (64) | `SITHPARTICLE_FLAG_TIMEOUT_RATE` | Declaration only; per-bit behavior not established in this audit. P: src/types_enums.h:436 |

Unlisted positions in this selected declaration inventory: `0x00000080`, `0x00000100`, `0x00000200`, `0x00000400`, `0x00000800`, `0x00001000`, `0x00002000`, `0x00004000`, `0x00008000`, `0x00010000`, `0x00020000`, `0x00040000`, `0x00080000`, `0x00100000`, `0x00200000`, `0x00400000`, `0x00800000`, `0x01000000`, `0x02000000`, `0x04000000`, `0x08000000`, `0x10000000`, `0x20000000`, `0x40000000`, `0x80000000`. These remain unresolved, not proved unused.

<a id="physics"></a>

## Physics flags

**Domain:** physics movement parameters. **Kind:** `bitmask`.

Requires physics movement. Set ORs; Clear clears and lacks Set’s explicit sync request in the inspected wrapper. Template physflags= replaces the field. Bit 30 is documented from original COG use and bounded runtime evidence; its dedicated meaning and the other undeclared high bits remain unresolved.

| Value | Exact symbol or descriptive catalog label | Evidence / interpretation |
| --- | --- | --- |
| `0x00000001` (1) | `SITH_PF_USEGRAVITY` | Gravity branch also requires nonzero mass and a sector without NOGRAVITY. P: Engine/sithPhysics.c:523-532 |
| `0x00000002` (2) | `SITH_PF_USETHRUST` | Declaration only; per-bit behavior not established in this audit. P: src/types_enums.h:1156 |
| `0x00000004` (4) | `SITH_PF_4` | Unresolved numeric/source name; no safe-spare-bit inference. P: src/types_enums.h:1157 |
| `0x00000008` (8) | `SITH_PF_8` | Unresolved numeric/source name; no safe-spare-bit inference. P: src/types_enums.h:1158 |
| `0x00000010` (16) | `SITH_PF_ALIGNSURFACE` | Declaration only; per-bit behavior not established in this audit. P: src/types_enums.h:1159 |
| `0x00000020` (32) | `SITH_PF_SURFACEBOUNCE` | Declaration only; per-bit behavior not established in this audit. P: src/types_enums.h:1160 |
| `0x00000040` (64) | `SITH_PF_FLOORSTICK` | Declaration only; per-bit behavior not established in this audit. P: src/types_enums.h:1161 |
| `0x00000080` (128) | `SITH_PF_WALLSTICK` | Declaration only; per-bit behavior not established in this audit. P: src/types_enums.h:1162 |
| `0x00000100` (256) | `SITH_PF_100` | Unresolved numeric/source name; no safe-spare-bit inference. P: src/types_enums.h:1163 |
| `0x00000200` (512) | `SITH_PF_USEROTATIONVELOCITY` | Declaration only; per-bit behavior not established in this audit. P: src/types_enums.h:1164 |
| `0x00000400` (1024) | `SITH_PF_BANKEDTURN` | Declaration only; per-bit behavior not established in this audit. P: src/types_enums.h:1165 |
| `0x00000800` (2048) | `SITH_PF_800` | Unresolved numeric/source name; no safe-spare-bit inference. P: src/types_enums.h:1166 |
| `0x00001000` (4096) | `SITH_PF_USEANGULARTHRUST` | Declaration only; per-bit behavior not established in this audit. P: src/types_enums.h:1167 |
| `0x00002000` (8192) | `SITH_PF_FLY` | Declaration only; per-bit behavior not established in this audit. P: src/types_enums.h:1168 |
| `0x00004000` (16384) | `SITH_PF_USEBLASTFORCE` | Declaration only; per-bit behavior not established in this audit. P: src/types_enums.h:1169 |
| `0x00008000` (32768) | `SITH_PF_8000` | **Pending bypass of the inspected attached-physics drag application.** The original ApplyForce implementation sets 0x8000 while adding force divided by positive mass to a physics mover velocity, even when the supplied force vector is zero. In the inspected surface/Thing-face attached-physics block, nonzero velocity and nonzero surface drag lead to a bit test: if set, the block clears 0x8000 and skips its linear drag call. Original executable paths. [Evidence and target limits](https://richard-pemberton-research.darkahn.chatgpt.site/projects/jk-mots-cog-workshop/docs/FLAG_DISCOVERIES.html). This is a conditional consumed marker, not permanent friction immunity. If those block prerequisites are not reached it is not consumed there, so a one-frame lifetime is unproved. Setting the bit alone does not apply an impulse. Other drag paths and runtime motion remain untested. |
| `0x00010000` (65536) | `SITH_PF_CROUCHING` | Declaration only; per-bit behavior not established in this audit. P: src/types_enums.h:1171 |
| `0x00020000` (131072) | `SITH_PF_20000` | **Skip initial velocity rotation on creation.** In CreateThingAtPos, a physics thing normally has its initial velocity transformed by its created orientation. A set 0x20000 skips that transform in both originals. Original executable paths. [Evidence and target limits](https://richard-pemberton-research.darkahn.chatgpt.site/projects/jk-mots-cog-workshop/docs/FLAG_DISCOVERIES.html). This is not a general angular-velocity, continuous-spin or object-orientation disable flag. MotS can apply the COG-supplied look vector after native creation; the transform uses the orientation at native creation time, not necessarily the final look-vector orientation. Setting the flag later does not undo the transform. |
| `0x00040000` (262144) | `SITH_PF_PARTIALGRAVITY` | Halves gravity contribution inside the USEGRAVITY branch; does not independently enable gravity. P: Engine/sithPhysics.c:523-532 |
| `0x00080000` (524288) | `SITH_PF_80000` | Unresolved numeric/source name; no safe-spare-bit inference. P: src/types_enums.h:1174 |
| `0x00100000` (1048576) | `SITH_PF_ONWATERSURFACE` | Engine sets/clears this water-surface state from collision results; not a safe user-owned latch. P: Engine/sithPhysics.c:45-63 |
| `0x00200000` (2097152) | `SITH_PF_200000` | Unresolved numeric/source name; no safe-spare-bit inference. P: src/types_enums.h:1176 |
| `0x00400000` (4194304) | `SITH_PF_NOTHRUST` | Declaration only; per-bit behavior not established in this audit. P: src/types_enums.h:1177 |
| `0x00800000` (8388608) | `SITH_PF_800000` | On the eligible original MotS water-entry path, the code ORs physics flags with 0x00800000, stores the result, and branches if that result is nonzero. The OR guarantees a nonzero result, so the following velocity.z *= 0.25 block is skipped whether the bit was previously clear or set. The corresponding original JK path performs that vertical-velocity multiplication and has no equivalent physics-bit OR/test there. The mapped MotS water-exit routine has no direct read or clear of this physics bit. The writer requires entry into an underwater sector, the separate Thing water-state bit previously absent, no drowning branch, the no-splash/no-notify argument zero, and physics movement. This is not a slow-once test. Broader intended meaning remains unresolved; no claim of all drag being disabled, game-wide permanent retention, or demonstrated sinking/fall-damage behavior. The current reconstruction reproduces this original branch anomaly. Runtime remains untested. Original evidence: JK bounded-original-static; MotS bounded-original-static. See [master findings](https://richard-pemberton-research.darkahn.chatgpt.site/projects/jk-mots-cog-workshop/docs/FLAG_DISCOVERIES.html). |
| `0x01000000` (16777216) | `SITH_PF_1000000` | Unresolved numeric/source name; no safe-spare-bit inference. P: src/types_enums.h:1179 |
| `0x02000000` (33554432) | `SITH_PF_2000000` | The normal fire/vire target-selection path in stock weap_carbgun_m.cog skips its targeting/freezing block when the target has PhysicsFlags 0x02000000. Original generic wrappers, copies and packet fields also preserve the word. The same script has a user0 forced-freeze handler without this test. This is a COG-defined contract, not universal freezing immunity or a verified dedicated native-engine consumer. No runtime carbonite test was performed. No equivalent JKDF2 or original-Indy contract was established. See [master findings](https://richard-pemberton-research.darkahn.chatgpt.site/projects/jk-mots-cog-workshop/docs/FLAG_DISCOVERIES.html). |
| `0x04000000` (67108864) | `SITH_PF_4000000` | Unresolved numeric/source name; no safe-spare-bit inference. P: src/types_enums.h:1181 |
| `0x08000000` (134217728) | `SITH_PF_8000000` | Unresolved numeric/source name; no safe-spare-bit inference. P: src/types_enums.h:1182 |
| `0x40000000` (1073741824) | `BIT_30_UNRESOLVED` (descriptive label) | Original purpose unresolved. No dedicated consumer established in inspected original JK 1.01/MotS paths. Original-executable tests showed identical hover and no reproducible free-impulse effect attributable to the bit; MotS retains a timing-sensitive numerical residual. Not a whole-program nonuse or vestigial proof. No native-Windows or connected-peer run. Controlled flight input, stock surface entry/exit, other actor types, water and save/load remain untested. Physics integration remainder was not recorded in the completed traces. See [master findings](https://richard-pemberton-research.darkahn.chatgpt.site/projects/jk-mots-cog-workshop/docs/FLAG_DISCOVERIES.html). |

Unlisted positions in this selected declaration inventory: `0x10000000`, `0x20000000`, `0x80000000`. These remain unresolved, not proved unused.

<a id="sector"></a>

## Sector flags

**Domain:** sector.flags. **Kind:** `bitmask`.

Water, thrust and collide-box fields have associated data/state; setting the bit does not create that data. Raw writes do not replay all transitions.

| Value | Exact symbol or descriptive catalog label | Evidence / interpretation |
| --- | --- | --- |
| `0x00000001` (1) | `SITH_SECTOR_NOGRAVITY` | Declaration only; per-bit behavior not established in this audit. P: src/types_enums.h:1127 |
| `0x00000002` (2) | `SITH_SECTOR_UNDERWATER` | Declaration only; per-bit behavior not established in this audit. P: src/types_enums.h:1128 |
| `0x00000004` (4) | `SITH_SECTOR_COGLINKED` | Declaration only; per-bit behavior not established in this audit. P: src/types_enums.h:1129 |
| `0x00000008` (8) | `SITH_SECTOR_USETHRUST` | Thrust setter maintains the vector as well as this flag. A raw flag write supplies no vector. P: Cog/sithCogFunctionSector.c:168-197 |
| `0x00000010` (16) | `SITH_SECTOR_HIDEONMAP` | Declaration only; per-bit behavior not established in this audit. P: src/types_enums.h:1131 |
| `0x00000020` (32) | `SITH_SECTOR_NOACTORENTER` | Declaration only; per-bit behavior not established in this audit. P: src/types_enums.h:1132 |
| `0x00000040` (64) | `SITH_SECTOR_FALLDEATH` | Declaration only; per-bit behavior not established in this audit. P: src/types_enums.h:1133 |
| `0x00000080` (128) | `SITH_SECTOR_ADJOINSOFF` | Boolean SetSectorAdjoins uses this as state and may short-circuit, then delegates per-adjoin show/hide. A raw bit write does not replay those transitions. P: Cog/sithCogFunctionSector.c:48-75; C: World/sithSector.c:220-237 |
| `0x00000100` (256) | `SITH_SECTOR_100` | Unresolved numeric/source name; no safe-spare-bit inference. P: src/types_enums.h:1135 |
| `0x00000200` (512) | `SITH_SECTOR_200` | Unresolved numeric/source name; no safe-spare-bit inference. P: src/types_enums.h:1136 |
| `0x00000400` (1024) | `SITH_SECTOR_400` | Unresolved numeric/source name; no safe-spare-bit inference. P: src/types_enums.h:1137 |
| `0x00000800` (2048) | `SITH_SECTOR_800` | Unresolved numeric/source name; no safe-spare-bit inference. P: src/types_enums.h:1138 |
| `0x00001000` (4096) | `SITH_SECTOR_HASCOLLIDEBOX` | Declaration only; per-bit behavior not established in this audit. P: src/types_enums.h:1139 |
| `0x00002000` (8192) | `SITH_SECTOR_2000` | Unresolved numeric/source name; no safe-spare-bit inference. P: src/types_enums.h:1140 |
| `0x00004000` (16384) | `SITH_SECTOR_SEEN` | Declaration only; per-bit behavior not established in this audit. P: src/types_enums.h:1141 |
| `0x00008000` (32768) | `SITH_SECTOR_SYNC` | Maintained during sector synchronization. Declaration is not a general authoring toggle. C: World/sithSector.c:320-338 |

Unlisted positions in this selected declaration inventory: `0x00010000`, `0x00020000`, `0x00040000`, `0x00080000`, `0x00100000`, `0x00200000`, `0x00400000`, `0x00800000`, `0x01000000`, `0x02000000`, `0x04000000`, `0x08000000`, `0x10000000`, `0x20000000`, `0x40000000`, `0x80000000`. These remain unresolved, not proved unused.

<a id="sound"></a>

## Sound flags

**Domain:** sound playback/channel state. **Kind:** `bitmask`.

Separate resource IDs from playback handles. This inventory contains engine-managed states as well as call options; do not pass every named bit as an authoring option.

| Value | Exact symbol or descriptive catalog label | Evidence / interpretation |
| --- | --- | --- |
| `0x00000001` (1) | `SITHSOUNDFLAG_LOOP` | **Looping attached sound cleanup when its owner is freed.** In the inspected owner-free cleanup, a matching sound with FOLLOWSTHING 0x80 and LOOP 0x1 takes the channel cleanup branch, conditionally stopping active playback and freeing its channel. The matching nonloop branch instead detaches the sound to the last position. Original executable paths. [Evidence and target limits](https://richard-pemberton-research.darkahn.chatgpt.site/projects/jk-mots-cog-workshop/docs/FLAG_DISCOVERIES.html). This adds an original lifecycle consumer of LOOP, not a complete looping/playback contract or an audibility test. The channel must pass owner and follow-state tests. Sound flags are separate from Thing.flags. |
| `0x00000002` (2) | `SITHSOUNDFLAG_FADING` | StopSound with positive fade duration marks fading; this is maintained playback state. P: Cog/sithCogFunctionSound.c:22-199,250-272; C: Devices/sithSoundMixer.c:217-248 |
| `0x00000004` (4) | `SITHSOUNDFLAG_AMBIENT_NOPAN` | Declaration only; per-bit behavior not established in this audit. P: src/types_enums.h:764 |
| `0x00000008` (8) | `SITHSOUNDFLAG_DOPPLER` | Declaration only; per-bit behavior not established in this audit. P: src/types_enums.h:765 |
| `0x00000010` (16) | `SITHSOUNDFLAG_FADE_IN` | Declaration only; per-bit behavior not established in this audit. P: src/types_enums.h:766 |
| `0x00000020` (32) | `SITHSOUNDFLAG_FADE_OUT` | Declaration only; per-bit behavior not established in this audit. P: src/types_enums.h:767 |
| `0x00000040` (64) | `SITHSOUNDFLAG_ABSOLUTE` | **Absolute stored position after a nonlooping sound loses its owner.** The matching nonlooping FOLLOWSTHING sound has bit 0x80 cleared and bit 0x40 set during owner-free cleanup, and receives the owner final position. The inspected position update selects the stored position when 0x40 is set. Original executable paths. [Evidence and target limits](https://richard-pemberton-research.darkahn.chatgpt.site/projects/jk-mots-cog-workshop/docs/FLAG_DISCOVERIES.html). This verifies the transition and position selection, not successful playback or audibility. Existing source-based PlaySoundPos behavior is retained in the prior interpretation. It establishes no Thing.flags 0x01000000 meaning. |
| `0x00000080` (128) | `SITHSOUNDFLAG_FOLLOWSTHING` | **Followed-owner sound lookup and detachment at owner cleanup.** Owner-free cleanup selects channels carrying 0x80 whose owner pointer matches the freed Thing. Looping channels enter channel cleanup; nonlooping channels clear 0x80, set absolute-position 0x40 and copy the owner final position. Original executable paths. [Evidence and target limits](https://richard-pemberton-research.darkahn.chatgpt.site/projects/jk-mots-cog-workshop/docs/FLAG_DISCOVERIES.html). This is sound-channel state, not Thing.flags. Other creation and playback gates, networking and runtime audio remain outside this original-static finding. |
| `0x00000100` (256) | `SITHSOUNDFLAG_HIGHPRIO` | Declaration only; per-bit behavior not established in this audit. P: src/types_enums.h:770 |
| `0x00000200` (512) | `SITHSOUNDFLAG_HIGHEST_PRIO` | Declaration only; per-bit behavior not established in this audit. P: src/types_enums.h:771 |
| `0x00000400` (1024) | `SITHSOUNDFLAG_MUTUALLY_EXCLUSIVE_PLAYBACK_ABOLUTE` | Declaration only; per-bit behavior not established in this audit. P: src/types_enums.h:772 |
| `0x00000800` (2048) | `SITHSOUNDFLAG_MUTUALLY_EXCLUSIVE_PLAYBACK_THING` | Declaration only; per-bit behavior not established in this audit. P: src/types_enums.h:773 |
| `0x00001000` (4096) | `SITHSOUNDFLAG_PER_VEL_PITCH_BEND` | Declaration only; per-bit behavior not established in this audit. P: src/types_enums.h:774 |
| `0x00002000` (8192) | `SITHSOUNDFLAG_UNDERWATER` | Declaration only; per-bit behavior not established in this audit. P: src/types_enums.h:775 |
| `0x00004000` (16384) | `SITHSOUNDFLAG_PRECACHED` | Declaration only; per-bit behavior not established in this audit. P: src/types_enums.h:776 |
| `0x00008000` (32768) | `SITHSOUNDFLAG_USE_DUP_BUF` | Declaration only; per-bit behavior not established in this audit. P: src/types_enums.h:777 |
| `0x00010000` (65536) | `SITHSOUNDFLAG_VOICE` | Declaration only; per-bit behavior not established in this audit. P: src/types_enums.h:778 |
| `0x00020000` (131072) | `SITHSOUNDFLAG_PLAYING` | StopAll reads this playback-state bit before setting PAUSED; that helper returns early in multiplayer. P: Cog/sithCogFunctionSound.c:22-199,250-272; C: Devices/sithSoundMixer.c:217-248 |
| `0x00040000` (262144) | `SITHSOUNDFLAG_PAUSED` | Set/cleared by stop-all/resume-all playback state transitions; not established as an ordinary caller option. P: Cog/sithCogFunctionSound.c:22-199,250-272; C: Devices/sithSoundMixer.c:217-248 |
| `0x00080000` (524288) | `SITHSOUNDFLAG_NO_3D` | Unresolved numeric/source name; no safe-spare-bit inference. P: src/types_enums.h:781 |

Unlisted positions in this selected declaration inventory: `0x00100000`, `0x00200000`, `0x00400000`, `0x00800000`, `0x01000000`, `0x02000000`, `0x04000000`, `0x08000000`, `0x10000000`, `0x20000000`, `0x40000000`, `0x80000000`. These remain unresolved, not proved unused.

<a id="surface"></a>

## Surface flags

**Domain:** world surface.flags. **Kind:** `bitmask`.

Not face typeFlags, adjoin flags or animation state. Source label AI_CAN_WALK_ON_FLOOR has a polarity problem: reviewed AI consumers reject 0x8.

| Value | Exact symbol or descriptive catalog label | Evidence / interpretation |
| --- | --- | --- |
| `0x00000001` (1) | `SITH_SURFACE_FLOOR` | Declaration only; per-bit behavior not established in this audit. C: src/World/sithSurface.h:53 |
| `0x00000002` (2) | `SITH_SURFACE_COG_LINKED` | Declaration only; per-bit behavior not established in this audit. C: src/World/sithSurface.h:54 |
| `0x00000004` (4) | `SITH_SURFACE_HAS_COLLISION` | Declaration only; per-bit behavior not established in this audit. C: src/World/sithSurface.h:55 |
| `0x00000008` (8) | `SITH_SURFACE_AI_CAN_WALK_ON_FLOOR` | SET rejects the tested surface in two AI walkability queries despite the affirmative enum name. This is not a universal prohibition of every AI/scripted movement route. Source annotation: from jkdf2 docs C: AI/sithAI.c:1224-1235,1279-1281,1307-1309 |
| `0x00000010` (16) | `SITH_SURFACE_DOUBLE_TEXTURE_SCALE` | Declaration only; per-bit behavior not established in this audit. C: src/World/sithSurface.h:57 |
| `0x00000020` (32) | `SITH_SURFACE_HALF_TEXTURE_SCALE` | Declaration only; per-bit behavior not established in this audit. C: src/World/sithSurface.h:58 |
| `0x00000040` (64) | `SITH_SURFACE_EIGHT_TEXTURE_SCALE` | Declaration only; per-bit behavior not established in this audit. C: src/World/sithSurface.h:59 |
| `0x00000080` (128) | `SITH_SURFACE_80` | **Suppress the inspected surface-collision impact damage.** For the inspected physics thing-versus-surface collision path, the surface bit becomes the fourth argument to the collision response. A set bit bypasses that response branch's impact-damage calculation, hit-damaged sound and FALL-class DamageThing call. Original executable paths. [Evidence and target limits](https://richard-pemberton-research.darkahn.chatgpt.site/projects/jk-mots-cog-workshop/docs/FLAG_DISCOVERIES.html). The collision response itself still runs. This is not universal damage immunity or a guarantee that every source of fall damage honors the bit. The flagged surface must be the actual contact surface in this path. |
| `0x00000200` (512) | `SITH_SURFACE_HORIZON_SKY` | Declaration only; per-bit behavior not established in this audit. C: src/World/sithSurface.h:61 |
| `0x00000400` (1024) | `SITH_SURFACE_CEILING_SKY` | Declaration only; per-bit behavior not established in this audit. C: src/World/sithSurface.h:62 |
| `0x00000800` (2048) | `SITH_SURFACE_SCROLLING` | Declaration only; per-bit behavior not established in this audit. C: src/World/sithSurface.h:63 |
| `0x00001000` (4096) | `SITH_SURFACE_ICY` | Declaration only; per-bit behavior not established in this audit. Source annotation: Jones specific: KILL_FLOOR C: src/World/sithSurface.h:64 |
| `0x00002000` (8192) | `SITH_SURFACE_VERYICY` | Declaration only; per-bit behavior not established in this audit. Source annotation: Jones specific: CLIMBABLE C: src/World/sithSurface.h:65 |
| `0x00004000` (16384) | `SITH_SURFACE_MAGSEALED` | Declaration only; per-bit behavior not established in this audit. Source annotation: Jones specific: TRACK C: src/World/sithSurface.h:66 |
| `0x00008000` (32768) | `SITH_SURFACE_CHANGED` | Declaration only; per-bit behavior not established in this audit. C: src/World/sithSurface.h:67 |
| `0x00010000` (65536) | `SITH_SURFACE_METAL` | Declaration only; per-bit behavior not established in this audit. C: src/World/sithSurface.h:68 |
| `0x00020000` (131072) | `SITH_SURFACE_WATER` | Declaration only; per-bit behavior not established in this audit. C: src/World/sithSurface.h:69 |
| `0x00040000` (262144) | `SITH_SURFACE_PUDDLE` | Declaration only; per-bit behavior not established in this audit. C: src/World/sithSurface.h:70 |
| `0x00080000` (524288) | `SITH_SURFACE_EARTH` | Declaration only; per-bit behavior not established in this audit. C: src/World/sithSurface.h:71 |
| `0x00100000` (1048576) | `SITH_SURFACE_VERYDEEPWATER` | Declaration only; per-bit behavior not established in this audit. C: src/World/sithSurface.h:72 |
| `0x00200000` (2097152) | `SITH_SURFACE_200000` | Unresolved numeric/source name; no safe-spare-bit inference. C: src/World/sithSurface.h:73 |
| `0x00400000` (4194304) | `SITH_SURFACE_400000` | Unresolved numeric/source name; no safe-spare-bit inference. C: src/World/sithSurface.h:74 |
| `0x00800000` (8388608) | `SITH_SURFACE_800000` | Unresolved numeric/source name; no safe-spare-bit inference. C: src/World/sithSurface.h:75 |
| `0x01000000` (16777216) | `SITH_SURFACE_1000000` | Loader sets/clears this state while reading four-component versus scalar per-vertex intensity data. Do not import the commented Indy LEDGE meaning. C: World/sithSurface.c:415-434 |

Unlisted positions in this selected declaration inventory: `0x00000100`, `0x02000000`, `0x04000000`, `0x08000000`, `0x10000000`, `0x20000000`, `0x40000000`, `0x80000000`. These remain unresolved, not proved unused.

<a id="adjoin"></a>

## Surface adjoin flags

**Domain:** adjoin.flags. **Kind:** `bitmask`.

The inspected collision queries condition passage tests on query flags, including bypasses with RAYCAST_4/100. MOVE and player/AI restriction names are not universal passability rules. They do not create a portal. Sound traversal is not established by the ALLOW_SOUND declaration alone. C: Engine/sithCollision.c:405-417.

| Value | Exact symbol or descriptive catalog label | Evidence / interpretation |
| --- | --- | --- |
| `0x00000001` (1) | `SITHSURF_ADJOIN_VISIBLE` | Declaration only; per-bit behavior not established in this audit. C: src/World/sithSurface.h:96 |
| `0x00000002` (2) | `SITHSURF_ADJOIN_ALLOW_MOVEMENT` | Declaration only; per-bit behavior not established in this audit. Source annotation: ADJOIN_MOVE C: src/World/sithSurface.h:97 |
| `0x00000004` (4) | `SITHSURF_ADJOIN_ALLOW_SOUND` | Declaration only; per-bit behavior not established in this audit. C: src/World/sithSurface.h:98 |
| `0x00000008` (8) | `SITHSURF_ADJOIN_ALLOW_PLAYER_ONLY` | Declaration only; per-bit behavior not established in this audit. C: src/World/sithSurface.h:99 |
| `0x00000010` (16) | `SITHSURF_ADJOIN_ALLOW_AI_ONLY` | Declaration only; per-bit behavior not established in this audit. C: src/World/sithSurface.h:100 |
| `0x00000020` (32) | `SITHSURF_ADJOIN_SET_BY_SECTOR` | Hide marks adjoins that were visible; Show restores those marks. Bookkeeping for sector hide/show, not an independent movement permission. C: World/sithSurface.c:551-569 |
| `0x00000040` (64) | `SITHSURF_ADJOIN_40` | Unresolved numeric/source name; no safe-spare-bit inference. C: src/World/sithSurface.h:102 |
| `0x00000080` (128) | `SITHSURF_ADJOIN_80` | Unresolved numeric/source name; no safe-spare-bit inference. C: src/World/sithSurface.h:103 |

Unlisted positions in this selected declaration inventory: `0x00000100`, `0x00000200`, `0x00000400`, `0x00000800`, `0x00001000`, `0x00002000`, `0x00004000`, `0x00008000`, `0x00010000`, `0x00020000`, `0x00040000`, `0x00080000`, `0x00100000`, `0x00200000`, `0x00400000`, `0x00800000`, `0x01000000`, `0x02000000`, `0x04000000`, `0x08000000`, `0x10000000`, `0x20000000`, `0x40000000`, `0x80000000`. These remain unresolved, not proved unused.

<a id="light"></a>

## Light mode values

**Domain:** render lighting mode. **Kind:** `scalar`.

Values4/5 remain unresolved. Mode6 has a bounded original-MotS inherited-lighting path, absent in the compared JK routines. Renderer caps and later processing still affect the result.

| Value | Exact symbol or descriptive catalog label | Evidence / interpretation |
| --- | --- | --- |
| `0x00000000` (0) | `RD_LIGHTMODE_FULLYLIT` | Declaration only; per-bit behavior not established in this audit. P: src/types_enums.h:22 |
| `0x00000001` (1) | `RD_LIGHTMODE_NOTLIT` | Declaration only; per-bit behavior not established in this audit. P: src/types_enums.h:23 |
| `0x00000002` (2) | `RD_LIGHTMODE_DIFFUSE` | Declaration only; per-bit behavior not established in this audit. P: src/types_enums.h:24 |
| `0x00000003` (3) | `RD_LIGHTMODE_GOURAUD` | Declaration only; per-bit behavior not established in this audit. P: src/types_enums.h:25 |
| `0x00000004` (4) | `RD_LIGHTMODE_4_UNK` | Unresolved numeric/source name; no safe-spare-bit inference. P: src/types_enums.h:26 |
| `0x00000005` (5) | `RD_LIGHTMODE_5_UNK` | Unresolved numeric/source name; no safe-spare-bit inference. P: src/types_enums.h:27 |
| `0x00000006` (6) | `RD_LIGHTMODE_6_UNK` | **Original MotS mode 6 propagates an inherited stored lighting scalar.** In original MotS, hierarchy node flag 2 selects mesh lighting mode 6 and copies the stored lighting scalar at mesh+0x44 from the first ancestor without node flag 2. Mode 6 bypasses the ordinary mesh lighting cap and copies that scalar into every face extraLight field at face+0x30. One original producer stores the mean of computed scalar vertex-light outputs in mesh+0x44. The compared JK1.01 hierarchy/mesh routines lack this inheritance, cap exception and mode-6 copy. Original executable paths. [Evidence and target limits](https://richard-pemberton-research.darkahn.chatgpt.site/projects/jk-mots-cog-workshop/docs/FLAG_DISCOVERIES.html). Scalar mode, not a flag bit. Requires a valid ancestor chain and mesh; the original walk lacks the modern null checks. Stored values can be stale, and culling, draw order, face-mode caps and later rendering still matter. It is not proof of fullbright, per-vertex RGB or a dynamic-only value. Current source instead transfers radius at this hierarchy step; original MotS parses and uses radius at mesh+0x48, distinct from the transferred +0x44. No runtime proof. |

<a id="face"></a>

## Face type / surface type flags

**Domain:** face.typeFlags. **Kind:** `bitmask`.

Historical surface type is a bitmask:0 has no options,1 and2 can coexist. Distinct from surface.flags. Jones annotations and unverified filtering/Z behavior are not retail support.

| Value | Exact symbol or descriptive catalog label | Evidence / interpretation |
| --- | --- | --- |
| `0x00000001` (1) | `RD_FF_DOUBLE_SIDED` | Declaration only; per-bit behavior not established in this audit. C: src/Raster/rdFace.h:18 |
| `0x00000002` (2) | `RD_FF_TEX_TRANSLUCENT` | Declaration only; per-bit behavior not established in this audit. C: src/Raster/rdFace.h:19 |
| `0x00000004` (4) | `RD_FF_TEX_CLAMP_X` | Declaration only; per-bit behavior not established in this audit. Source annotation: Didn't show correct results in JED port, assuming not used in JKDF2 C: src/Raster/rdFace.h:20 |
| `0x00000008` (8) | `RD_FF_TEX_CLAMP_Y` | Declaration only; per-bit behavior not established in this audit. Source annotation: Didn't show correct results in JED port, assuming not used in JKDF2 C: src/Raster/rdFace.h:21 |
| `0x00000010` (16) | `RD_FF_TEX_FILTER_NEAREST` | **Original MotS model-face path initially selects lighting mode 1.** Original MotS reads model face flags at face+4 and, when 0x10 is set, overrides the initially selected render-entry lighting mode with scalar 1 (source alias NOTLIT). The complete compared JK1.01 model-face routine lacks this override. This proves a lighting consumer beyond the nearest-filter enum label. Original executable paths. [Evidence and target limits](https://richard-pemberton-research.darkahn.chatgpt.site/projects/jk-mots-cog-workshop/docs/FLAG_DISCOVERIES.html). This is an initial mode override in the model-face path. Later ambient/colormap handling can replace mode 1 with mode 0 or 2; no final black/fullbright appearance is established. Nearest filtering, other surface/cache/backend consumers and global JK nonuse are not established. No runtime rendering test. |
| `0x00000020` (32) | `RD_FF_ZWRITE_DISABLED` | Declaration only; per-bit behavior not established in this audit. C: src/Raster/rdFace.h:23 |
| `0x00000040` (64) | `RD_FF_3DO_LEDGE` | Declaration only; per-bit behavior not established in this audit. Source annotation: Jones specific C: src/Raster/rdFace.h:24 |
| `0x00000080` (128) | `RD_FF_UNKNOWN_80` | Unresolved numeric/source name; no safe-spare-bit inference. C: src/Raster/rdFace.h:25 |
| `0x00000100` (256) | `RD_FF_FOG_ENABLED` | Declaration only; per-bit behavior not established in this audit. Source annotation: Jones specific C: src/Raster/rdFace.h:26 |
| `0x00000200` (512) | `RD_FF_3DO_WHIP_AIM` | Declaration only; per-bit behavior not established in this audit. Source annotation: Jones specific C: src/Raster/rdFace.h:27 |

Unlisted positions in this selected declaration inventory: `0x00000400`, `0x00000800`, `0x00001000`, `0x00002000`, `0x00004000`, `0x00008000`, `0x00010000`, `0x00020000`, `0x00040000`, `0x00080000`, `0x00100000`, `0x00200000`, `0x00400000`, `0x00800000`, `0x01000000`, `0x02000000`, `0x04000000`, `0x08000000`, `0x10000000`, `0x20000000`, `0x40000000`, `0x80000000`. These remain unresolved, not proved unused.

<a id="thing_type"></a>

## Thing types and masks

**Domain:** thing.type and type-selection mask. **Kind:** `scalar-with-derived-mask`.

Type IDs are ordinal. The inspected radius-message mask uses1 << (type &31); ghost8 therefore maps to 0x100. INVALID is a sentinel, not a spawnable thing type.

| Value | Exact symbol or descriptive catalog label | Evidence / interpretation |
| --- | --- | --- |
| `0x00000000` (0) | `SITH_THING_FREE` | Declaration only; per-bit behavior not established in this audit. Radius-selection mask: `0x00000001`. P: src/types_enums.h:1270 |
| `0x00000001` (1) | `SITH_THING_CAMERA` | Declaration only; per-bit behavior not established in this audit. Radius-selection mask: `0x00000002`. P: src/types_enums.h:1271 |
| `0x00000002` (2) | `SITH_THING_ACTOR` | Declaration only; per-bit behavior not established in this audit. Radius-selection mask: `0x00000004`. P: src/types_enums.h:1272 |
| `0x00000003` (3) | `SITH_THING_WEAPON` | Declaration only; per-bit behavior not established in this audit. Radius-selection mask: `0x00000008`. P: src/types_enums.h:1273 |
| `0x00000004` (4) | `SITH_THING_DEBRIS` | Declaration only; per-bit behavior not established in this audit. Radius-selection mask: `0x00000010`. P: src/types_enums.h:1274 |
| `0x00000005` (5) | `SITH_THING_ITEM` | Declaration only; per-bit behavior not established in this audit. Radius-selection mask: `0x00000020`. P: src/types_enums.h:1275 |
| `0x00000006` (6) | `SITH_THING_EXPLOSION` | Declaration only; per-bit behavior not established in this audit. Radius-selection mask: `0x00000040`. P: src/types_enums.h:1276 |
| `0x00000007` (7) | `SITH_THING_COG` | Declaration only; per-bit behavior not established in this audit. Radius-selection mask: `0x00000080`. P: src/types_enums.h:1277 |
| `0x00000008` (8) | `SITH_THING_GHOST` | Declaration only; per-bit behavior not established in this audit. Radius-selection mask: `0x00000100`. P: src/types_enums.h:1278 |
| `0x00000009` (9) | `SITH_THING_CORPSE` | Declaration only; per-bit behavior not established in this audit. Radius-selection mask: `0x00000200`. P: src/types_enums.h:1279 |
| `0x0000000A` (10) | `SITH_THING_PLAYER` | Declaration only; per-bit behavior not established in this audit. Radius-selection mask: `0x00000400`. P: src/types_enums.h:1280 |
| `0x0000000B` (11) | `SITH_THING_PARTICLE` | Declaration only; per-bit behavior not established in this audit. Radius-selection mask: `0x00000800`. P: src/types_enums.h:1281 |
| `0x0000000C` (12) | `SITH_THING_INVALID` | Declaration only; per-bit behavior not established in this audit. Radius-selection mask: `0x00001000` (sentinel; not a valid type assignment). P: src/types_enums.h:1282 |

<a id="weapon"></a>

## Weapon/projectile flags

**Domain:** weapon type parameters. **Kind:** `bitmask`.

These are not weapon bins or FireProjectile scale flags. Ordered branches and explosion/template bindings matter. The final MOPHIABOMB entry is explicitly Jones-labeled.

| Value | Exact symbol or descriptive catalog label | Evidence / interpretation |
| --- | --- | --- |
| `0x00000001` (1) | `SITH_WF_NOSHOOTERDAMAGE` | Declaration only; per-bit behavior not established in this audit. P: src/types_enums.h:134 |
| `0x00000002` (2) | `SITH_WF_2` | Unresolved numeric/source name; no safe-spare-bit inference. P: src/types_enums.h:135 |
| `0x00000004` (4) | `SITH_WF_FACEHITEXPLODE` | Declaration only; per-bit behavior not established in this audit. P: src/types_enums.h:136 |
| `0x00000008` (8) | `SITH_WF_THINGHITEXPLODE` | Declaration only; per-bit behavior not established in this audit. P: src/types_enums.h:137 |
| `0x00000010` (16) | `SITH_WF_10` | Unresolved numeric/source name; no safe-spare-bit inference. P: src/types_enums.h:138 |
| `0x00000020` (32) | `SITH_WF_20` | No verified weapon-specific 0x20 consumer was established in the reviewed original JK 1.01 or MotS update, firing, parser, collision and lifecycle region. The hexadecimal weaponflags parser stores the complete parsed word without a per-bit mask in both originals. The named current-source identifier remains a numeric declaration. Matching mask values in actor, explosion and physics code were rejected after checking their field and type context. This is not proof of game-wide nonuse or inertness. Computed masks, pointer aliases, indirect paths and uninspected code remain outside the bounded result. Template occurrence does not establish purpose, and no underwater-grenade behavior is assigned. Changing template input may affect checksum processing even without a demonstrated gameplay consumer. No retail runtime or multiplayer test was run. See [master findings](https://richard-pemberton-research.darkahn.chatgpt.site/projects/jk-mots-cog-workshop/docs/FLAG_DISCOVERIES.html). |
| `0x00000040` (64) | `SITH_WF_40` | Unresolved numeric/source name; no safe-spare-bit inference. P: src/types_enums.h:140 |
| `0x00000080` (128) | `SITH_WF_ATTACHFACE` | Declaration only; per-bit behavior not established in this audit. P: src/types_enums.h:141 |
| `0x00000100` (256) | `SITH_WF_EXPLODE` | Declaration only; per-bit behavior not established in this audit. P: src/types_enums.h:142 |
| `0x00000200` (512) | `SITH_WF_DAMAGEDESTROY` | Declaration only; per-bit behavior not established in this audit. P: src/types_enums.h:143 |
| `0x00000400` (1024) | `SITH_WF_IMPACTSOUND` | Declaration only; per-bit behavior not established in this audit. P: src/types_enums.h:144 |
| `0x00000800` (2048) | `SITH_WF_ATTACHTHING` | Declaration only; per-bit behavior not established in this audit. P: src/types_enums.h:145 |
| `0x00001000` (4096) | `SITH_WF_PROXIMITY` | Declaration only; per-bit behavior not established in this audit. Source annotation: "Weapon will explode when something touches its sphere." P: src/types_enums.h:146 |
| `0x00002000` (8192) | `SITH_WF_INSTANT_IMPACT` | Declaration only; per-bit behavior not established in this audit. P: src/types_enums.h:147 |
| `0x00004000` (16384) | `SITH_WF_DAMAGE_DECAY` | **Distance-based damage reduction in the 0x10000 handler.** At a terminal collision in the original 0x10000 handler, this bit subtracts rate times hit distance from the local damage amount and floors it at minDamage in both games. Original executable paths. [Evidence and target limits](https://richard-pemberton-research.darkahn.chatgpt.site/projects/jk-mots-cog-workshop/docs/FLAG_DISCOVERIES.html). This updates a local hit amount, not the persistent weapon damage field. Preserve the separately documented ordinary-update decay difference between current source and original JK. Finite-value control flow is established; downstream damage acceptance and runtime outcomes are not. |
| `0x00008000` (32768) | `SITH_WF_OBJECT_TRAIL` | **Conditional segmented object-trail creation in the 0x10000 handler.** The original handler attempts trail-template creation along a locally advanced orientation when bit 0x8000, a trail template and the step/projection versus hit-distance or range gates permit it. The no-hit tail can also create a trail up to range. Original executable paths. [Evidence and target limits](https://richard-pemberton-research.darkahn.chatgpt.site/projects/jk-mots-cog-workshop/docs/FLAG_DISCOVERIES.html). Creation return values are ignored, so allocation and visuals are not proved. The loops have no explicit iteration cap or general progress guard: termination depends on advancing configured step/orientation. This is not a reproduced hang, networking proof or a universal trail contract. |
| `0x00010000` (65536) | `SITH_WF_10000` | **Select the range-based collision-query and optional trail handler.** In both original dispatchers, weapon 0x10000 selects this handler after the higher-priority 0x2000 branch. The complete handler traverses returned adjoin crossings and handles the first returned non-adjoin result. Thing hits call damage and can independently attempt force; world hits call surface impact. A non-null impact template enables a creation attempt. The final callee marks the weapon Thing.flags 0x2 for destruction. Original executable paths. [Evidence and target limits](https://richard-pemberton-research.darkahn.chatgpt.site/projects/jk-mots-cog-workshop/docs/FLAG_DISCOVERIES.html). The query uses moveSize as well as range, so this is not established as a zero-width ray. One terminal result is handled; collision ordering and penetration are not established. Destruction is marked, not an immediate storage free. Full downstream helper behavior, successful allocation, visuals and networking remain untested. |
| `0x00020000` (131072) | `SITH_WF_20000` | Unresolved numeric/source name; no safe-spare-bit inference. P: src/types_enums.h:151 |
| `0x00040000` (262144) | `SITH_WF_DECAYEMITSOUNDAWARENESSEVENT` | Declaration only; per-bit behavior not established in this audit. P: src/types_enums.h:152 |
| `0x00080000` (524288) | `SITH_WF_SURFACERICOCHET` | Declaration only; per-bit behavior not established in this audit. P: src/types_enums.h:153 |
| `0x00100000` (1048576) | `SITH_WF_100000` | Unresolved numeric/source name; no safe-spare-bit inference. P: src/types_enums.h:154 |
| `0x00200000` (2097152) | `SITH_WF_EMITAITARGETEDEVENT` | Declaration only; per-bit behavior not established in this audit. P: src/types_enums.h:155 |
| `0x00400000` (4194304) | `SITH_WF_ACTORKILLDESTROY` | Declaration only; per-bit behavior not established in this audit. P: src/types_enums.h:156 |
| `0x00800000` (8388608) | `SITH_WF_MOPHIABOMB` | Declaration only; per-bit behavior not established in this audit. Source annotation: Jones specific P: src/types_enums.h:157 |

Unlisted positions in this selected declaration inventory: `0x01000000`, `0x02000000`, `0x04000000`, `0x08000000`, `0x10000000`, `0x20000000`, `0x40000000`, `0x80000000`. These remain unresolved, not proved unused.

<a id="inventory_status"></a>

## Per-player inventory status

**Domain:** player inventory state. **Kind:** `bitmask`.

Additional domain, not the historical Inventory Flags table. Dedicated available/activated/carries operations modify these states.

| Value | Exact symbol or descriptive catalog label | Evidence / interpretation |
| --- | --- | --- |
| `0x00000001` (1) | `SITHINVENTORY_ITEM_CHANGED` | Declaration only; per-bit behavior not established in this audit. P: src/types_enums.h:1109 |
| `0x00000002` (2) | `SITHINVENTORY_ITEM_ACTIVATED` | Declaration only; per-bit behavior not established in this audit. P: src/types_enums.h:1110 |
| `0x00000004` (4) | `SITHINVENTORY_ITEM_AVAILABLE` | Declaration only; per-bit behavior not established in this audit. P: src/types_enums.h:1111 |
| `0x00000008` (8) | `SITHINVENTORY_ITEM_FOUND` | Declaration only; per-bit behavior not established in this audit. P: src/types_enums.h:1112 |

Unlisted positions in this selected declaration inventory: `0x00000010`, `0x00000020`, `0x00000040`, `0x00000080`, `0x00000100`, `0x00000200`, `0x00000400`, `0x00000800`, `0x00001000`, `0x00002000`, `0x00004000`, `0x00008000`, `0x00010000`, `0x00020000`, `0x00040000`, `0x00080000`, `0x00100000`, `0x00200000`, `0x00400000`, `0x00800000`, `0x01000000`, `0x02000000`, `0x04000000`, `0x08000000`, `0x10000000`, `0x20000000`, `0x40000000`, `0x80000000`. These remain unresolved, not proved unused.

<a id="cog"></a>

## COG execution flags

**Domain:** COG instance flags. **Kind:** `bitmask`.

Additional domain. local 0x40 and no-sync 0x200 are not thing flags, checksum approval or universal isolation.

| Value | Exact symbol or descriptive catalog label | Evidence / interpretation |
| --- | --- | --- |
| `0x00000001` (1) | `SITH_COG_DEBUG` | Declaration only; per-bit behavior not established in this audit. P: src/types_enums.h:53 |
| `0x00000002` (2) | `SITH_COG_DISABLED` | Declaration only; per-bit behavior not established in this audit. P: src/types_enums.h:54 |
| `0x00000004` (4) | `SITH_COG_PULSE_SET` | Declaration only; per-bit behavior not established in this audit. P: src/types_enums.h:55 |
| `0x00000008` (8) | `SITH_COG_TIMER_SET` | Declaration only; per-bit behavior not established in this audit. P: src/types_enums.h:56 |
| `0x00000010` (16) | `SITH_COG_PAUSED` | Declaration only; per-bit behavior not established in this audit. P: src/types_enums.h:57 |
| `0x00000020` (32) | `SITH_COG_CLASS` | Declaration only; per-bit behavior not established in this audit. P: src/types_enums.h:58 |
| `0x00000040` (64) | `SITH_COG_LOCAL` | Declaration only; per-bit behavior not established in this audit. P: src/types_enums.h:59 |
| `0x00000080` (128) | `SITH_COG_SERVER` | Declaration only; per-bit behavior not established in this audit. P: src/types_enums.h:60 |
| `0x00000100` (256) | `SITH_COG_GLOBAL` | Declaration only; per-bit behavior not established in this audit. P: src/types_enums.h:61 |
| `0x00000200` (512) | `SITHCOG_NOSYNC` | Declaration only; per-bit behavior not established in this audit. P: src/types_enums.h:62 |

Unlisted positions in this selected declaration inventory: `0x00000400`, `0x00000800`, `0x00001000`, `0x00002000`, `0x00004000`, `0x00008000`, `0x00010000`, `0x00020000`, `0x00040000`, `0x00080000`, `0x00100000`, `0x00200000`, `0x00400000`, `0x00800000`, `0x01000000`, `0x02000000`, `0x04000000`, `0x08000000`, `0x10000000`, `0x20000000`, `0x40000000`, `0x80000000`. These remain unresolved, not proved unused.

<a id="legacy_type_aliases"></a>

## Legacy type aliases

**Domain:** legacy names overlapping type parameters. **Kind:** `alias-inventory`.

Additional name inventory preserved for source reading. These names conflict with actor declarations; do not treat them as a universal type-flags semantic map.

| Value | Exact symbol or descriptive catalog label | Evidence / interpretation |
| --- | --- | --- |
| `0x00000001` (1) | `THING_TYPEFLAGS_1` | Unresolved numeric/source name; no safe-spare-bit inference. P: src/types_enums.h:1360 |
| `0x00000002` (2) | `THING_TYPEFLAGS_FORCE` | Declaration only; per-bit behavior not established in this audit. P: src/types_enums.h:1361 |
| `0x00000004` (4) | `THING_TYPEFLAGS_DAMAGE` | Declaration only; per-bit behavior not established in this audit. P: src/types_enums.h:1362 |
| `0x00000008` (8) | `THING_TYPEFLAGS_8` | Unresolved numeric/source name; no safe-spare-bit inference. P: src/types_enums.h:1363 |
| `0x00000010` (16) | `THING_TYPEFLAGS_LIGHT` | Declaration only; per-bit behavior not established in this audit. P: src/types_enums.h:1364 |
| `0x00000020` (32) | `THING_TYPEFLAGS_20` | Unresolved numeric/source name; no safe-spare-bit inference. P: src/types_enums.h:1365 |
| `0x00000040` (64) | `THING_TYPEFLAGS_40` | Unresolved numeric/source name; no safe-spare-bit inference. P: src/types_enums.h:1366 |
| `0x00000080` (128) | `THING_TYPEFLAGS_80` | Unresolved numeric/source name; no safe-spare-bit inference. P: src/types_enums.h:1367 |
| `0x00000100` (256) | `THING_TYPEFLAGS_DROID` | Declaration only; per-bit behavior not established in this audit. P: src/types_enums.h:1368 |
| `0x00000200` (512) | `THING_TYPEFLAGS_BOSS` | Declaration only; per-bit behavior not established in this audit. P: src/types_enums.h:1369 |
| `0x00000400` (1024) | `THING_TYPEFLAGS_DEAF` | Declaration only; per-bit behavior not established in this audit. P: src/types_enums.h:1370 |
| `0x00000800` (2048) | `THING_TYPEFLAGS_BLIND` | Declaration only; per-bit behavior not established in this audit. P: src/types_enums.h:1371 |
| `0x00001000` (4096) | `THING_TYPEFLAGS_1000` | Unresolved numeric/source name; no safe-spare-bit inference. P: src/types_enums.h:1372 |
| `0x00002000` (8192) | `THING_TYPEFLAGS_ISBLOCKING` | **Eligibility for the saber-blocking branch.** The saber-hit path checks this bit on an actor/player being hit before admitting it to field-of-view and chance-based blocking checks, which also reject an invisible attacker. A clear bit goes to the damage path. Original executable paths. [Evidence and target limits](https://richard-pemberton-research.darkahn.chatgpt.site/projects/jk-mots-cog-workshop/docs/FLAG_DISCOVERIES.html). The bit permits this blocking path; it does not guarantee a block. BLEEDS is a misleading source alias for this inspected JK/MotS consumer. Other possible consumers have not been exhaustively excluded. |
| `0x00004000` (16384) | `THING_TYPEFLAGS_4000` | Unresolved numeric/source name; no safe-spare-bit inference. P: src/types_enums.h:1374 |
| `0x00008000` (32768) | `THING_TYPEFLAGS_8000` | Unresolved numeric/source name; no safe-spare-bit inference. P: src/types_enums.h:1375 |
| `0x00010000` (65536) | `THING_TYPEFLAGS_SCREAMING` | Declaration only; per-bit behavior not established in this audit. P: src/types_enums.h:1376 |
| `0x00020000` (131072) | `THING_TYPEFLAGS_20000` | Unresolved numeric/source name; no safe-spare-bit inference. P: src/types_enums.h:1377 |
| `0x00040000` (262144) | `THING_TYPEFLAGS_40000` | Unresolved numeric/source name; no safe-spare-bit inference. P: src/types_enums.h:1378 |
| `0x00080000` (524288) | `THING_TYPEFLAGS_CANTSHOOTUNDERWATER` | Declaration only; per-bit behavior not established in this audit. P: src/types_enums.h:1379 |
| `0x00100000` (1048576) | `THING_TYPEFLAGS_100000` | Unresolved numeric/source name; no safe-spare-bit inference. P: src/types_enums.h:1380 |
| `0x00200000` (2097152) | `THING_TYPEFLAGS_IMMOBILE` | **Gate player controls, with different look handling.** On the inspected player control path, a set bit skips movement, activation and map handling. Original JK calls its look handler before the flag gate. Original MotS places the look call after the gate, so the flagged path skips it as well. Original executable paths. [Evidence and target limits](https://richard-pemberton-research.darkahn.chatgpt.site/projects/jk-mots-cog-workshop/docs/FLAG_DISCOVERIES.html). This controls processing of these inputs; it does not freeze all physics or prevent every scripted move. Preserve the JK/MotS difference instead of reducing the meaning to immobile. |
| `0x00400000` (4194304) | `THING_TYPEFLAGS_400000` | Unresolved numeric/source name; no safe-spare-bit inference. P: src/types_enums.h:1382 |
| `0x00800000` (8388608) | `THING_TYPEFLAGS_800000` | Unresolved numeric/source name; no safe-spare-bit inference. P: src/types_enums.h:1383 |
| `0x01000000` (16777216) | `THING_TYPEFLAGS_1000000` | Unresolved numeric/source name; no safe-spare-bit inference. P: src/types_enums.h:1384 |
| `0x02000000` (33554432) | `THING_TYPEFLAGS_2000000` | **Skip the target-sector darkness penalty in an AI sight check.** The observer actor bit bypasses the target-sector ambient-light multiplier in the inspected sight-acceptance threshold calculation in both originals. The branch is reached inside the distance and target-light-state gates. Original executable paths. [Evidence and target limits](https://richard-pemberton-research.darkahn.chatgpt.site/projects/jk-mots-cog-workshop/docs/FLAG_DISCOVERIES.html). It does not guarantee detection or sight through geometry. Distance, movement, invisibility, observer blindness, threshold clamping and the final random comparison retain their own effects. Other visibility helpers and runtime probabilities were not established. |
| `0x04000000` (67108864) | `THING_TYPEFLAGS_4000000` | Unresolved numeric/source name; no safe-spare-bit inference. P: src/types_enums.h:1386 |
| `0x08000000` (134217728) | `THING_TYPEFLAGS_8000000` | Unresolved numeric/source name; no safe-spare-bit inference. P: src/types_enums.h:1387 |
| `0x10000000` (268435456) | `THING_TYPEFLAGS_10000000` | Unresolved numeric/source name; no safe-spare-bit inference. P: src/types_enums.h:1388 |
| `0x20000000` (536870912) | `THING_TYPEFLAGS_20000000` | Unresolved numeric/source name; no safe-spare-bit inference. P: src/types_enums.h:1389 |
| `0x40000000` (1073741824) | `THING_TYPEFLAGS_40000000` | Unresolved numeric/source name; no safe-spare-bit inference. P: src/types_enums.h:1390 |
| `0x80000000` (2147483648) | `THING_TYPEFLAGS_80000000` | Unresolved numeric/source name; no safe-spare-bit inference. P: src/types_enums.h:1391 |

<a id="keyframe"></a>

## Keyframe playback flags

**Domain:** PlayKey caller options. **Kind:** `bitmask`.

Additional domain. Distinct from material animation and internal puppet-track status. Low option precedence is conditional, not independent.

| Value | Exact symbol or descriptive catalog label | Evidence / interpretation |
| --- | --- | --- |
| `0x00000001` (1) | `RDKEYFRAME_PUPPET_CONTROLLED` | Sets track speed to0.0. This is an immediate source mapping, not full downstream pose/timing proof. P: Engine/sithPuppet.c:222-279 |
| `0x00000002` (2) | `RDKEYFRAME_NOLOOP` | Maps to internal track status 0x20, taking precedence over options 0x20 and 0x4. This is an immediate source mapping, not full downstream pose/timing proof. P: Engine/sithPuppet.c:222-279 |
| `0x00000004` (4) | `RDKEYFRAME_PAUSE_ON_LAST_FRAME` | Maps to internal status 0x40 only when 0x2 and 0x20 are absent. This is an immediate source mapping, not full downstream pose/timing proof. P: Engine/sithPuppet.c:222-279 |
| `0x00000008` (8) | `RDKEYFRAME_RESTART_ACTIVE` | Searches four tracks for the same keyframe and resets a match; otherwise adds a track. This is an immediate source mapping, not full downstream pose/timing proof. P: Engine/sithPuppet.c:222-279 |
| `0x00000010` (16) | `RDKEYFRAME_DISABLE_FADEIN` | Immediately plays the track instead of the default0.1 fade-in. This is an immediate source mapping, not full downstream pose/timing proof. P: Engine/sithPuppet.c:222-279 |
| `0x00000020` (32) | `RDKEYFRAME_FADEOUT_NOLOOP` | Maps to internal status 0x80 only when option 0x2 is absent. This is an immediate source mapping, not full downstream pose/timing proof. P: Engine/sithPuppet.c:222-279 |
| `0x00000040` (64) | `RDKEYFRAME_FORCEMOVE` | Declaration only; per-bit behavior not established in this audit. P: src/types_enums.h:447 |

Unlisted positions in this selected declaration inventory: `0x00000080`, `0x00000100`, `0x00000200`, `0x00000400`, `0x00000800`, `0x00001000`, `0x00002000`, `0x00004000`, `0x00008000`, `0x00010000`, `0x00020000`, `0x00040000`, `0x00080000`, `0x00100000`, `0x00200000`, `0x00400000`, `0x00800000`, `0x01000000`, `0x02000000`, `0x04000000`, `0x08000000`, `0x10000000`, `0x20000000`, `0x40000000`, `0x80000000`. These remain unresolved, not proved unused.

<a id="texture"></a>

## Texture mode values

**Domain:** face or thing desired texture mode. **Kind:** `scalar`.

Additional domain.0 affine,1 perspective are source labels;2/3/4 remain unknown. A desired mode is not a verified final rendering result.

| Value | Exact symbol or descriptive catalog label | Evidence / interpretation |
| --- | --- | --- |
| `0x00000000` (0) | `RD_TEXTUREMODE_AFFINE` | Declaration only; per-bit behavior not established in this audit. P: src/types_enums.h:34 |
| `0x00000001` (1) | `RD_TEXTUREMODE_PERSPECTIVE` | Declaration only; per-bit behavior not established in this audit. P: src/types_enums.h:35 |
| `0x00000002` (2) | `RD_TEXTUREMODE_2_UNK` | Unresolved numeric/source name; no safe-spare-bit inference. P: src/types_enums.h:36 |
| `0x00000003` (3) | `RD_TEXTUREMODE_3_UNK` | Unresolved numeric/source name; no safe-spare-bit inference. P: src/types_enums.h:37 |
| `0x00000004` (4) | `RD_TEXTUREMODE_4_UNK` | Unresolved numeric/source name; no safe-spare-bit inference. P: src/types_enums.h:38 |

<a id="super"></a>

## Player superflags

**Domain:** local selected player super-weapon/shield/Force state. **Kind:** `bitmask-over-three-booleans`.

No player argument; unknown bits are ignored in the inspected source. Does not itself implement every powerup effect.

| Value | Exact symbol or descriptive catalog label | Evidence / interpretation |
| --- | --- | --- |
| `0x00000001` (1) | `WEAPON_SUPER` | Weapon-super boolean. Source annotation: Descriptive catalog label; not necessarily a native C symbol. P: src/Cog/jkCog.c:388 |
| `0x00000002` (2) | `SHIELD_SUPER` | Shield-super boolean. Source annotation: Descriptive catalog label; not necessarily a native C symbol. P: src/Cog/jkCog.c:390 |
| `0x00000004` (4) | `FORCE_BOOST` | Force-boost boolean. Source annotation: Descriptive catalog label; not necessarily a native C symbol. P: src/Cog/jkCog.c:392 |

<a id="respawn"></a>

## Multiplayer respawn masks

**Domain:** multiplayer spawn-index selection. **Kind:** `32-bit-index-exclusion-mask`.

SetRespawnMask replaces the word. In the inspected selector, set bits exclude candidate indices; the historical examples are exclusion complements, not individual flags. Current fork/co-op changes prevent a retail claim.

| Value | Exact symbol or descriptive catalog label | Evidence / interpretation |
| --- | --- | --- |
| `0x0000FFFF` (65535) | `EXCLUDE_0_THROUGH_15` | Leaves indices16..31 available to this mask; other selection rules still apply. Source annotation: Descriptive catalog label; not necessarily a native C symbol. P: src/Dss/sithMulti.c:301 |
| `0xFFFF00FF` (4294902015) | `EXCLUDE_EXCEPT_8_THROUGH_15` | Leaves indices8..15 available to this mask. Source annotation: Descriptive catalog label; not necessarily a native C symbol. P: src/Dss/sithMulti.c:301 |
| `0xFFFFFF00` (4294967040) | `EXCLUDE_EXCEPT_0_THROUGH_7` | Leaves indices0..7 available to this mask. Source annotation: Descriptive catalog label; not necessarily a native C symbol. P: src/Dss/sithMulti.c:301 |

<a id="animation"></a>

## Surface/material animation flags

**Domain:** runtime material/surface animation. **Kind:** `bitmask`.

These low options are animation flags, not world-surface flags despite reused source constants. Use the animation API, not SetSurfaceFlags.

| Value | Exact symbol or descriptive catalog label | Evidence / interpretation |
| --- | --- | --- |
| `0x00000001` (1) | `LOOP` | Wrap after the final cel instead of finishing. Source annotation: Descriptive catalog label; not necessarily a native C symbol. C: src/World/sithSurface.c:846 |
| `0x00000002` (2) | `START_AND_LOOP_BASE_1` | Start at cel1 unless 0x4 is set; loop base1 takes precedence over 0x4. This is not skip-last-frame. Source annotation: Descriptive catalog label; not necessarily a native C symbol. C: src/World/sithSurface.c:849 |
| `0x00000004` (4) | `START_AND_LOOP_BASE_2` | Start at cel2; loop base2 only when 0x2 is absent. Both bits together start2 then loopbase1. Cel count matters. Source annotation: Descriptive catalog label; not necessarily a native C symbol. C: src/World/sithSurface.c:853 |
| `0x00000008` (8) | `RESET_ON_COMPLETION` | On nonloop completion reset to cel0 instead of holding final cel, then release animation. Source annotation: Descriptive catalog label; not necessarily a native C symbol. C: src/World/sithSurface.c:875 |

Unlisted positions in this selected declaration inventory: `0x00000010`, `0x00000020`, `0x00000040`, `0x00000080`, `0x00000100`, `0x00000200`, `0x00000400`, `0x00000800`, `0x00001000`, `0x00002000`, `0x00004000`, `0x00008000`, `0x00010000`, `0x00020000`, `0x00040000`, `0x00080000`, `0x00100000`, `0x00200000`, `0x00400000`, `0x00800000`, `0x01000000`, `0x02000000`, `0x04000000`, `0x08000000`, `0x10000000`, `0x20000000`, `0x40000000`, `0x80000000`. These remain unresolved, not proved unused.

## Contract details that prevent common mistakes

Get/Set/ClearActorFlags, WeaponFlags, ItemFlags, ExplosionFlags and ParticleFlags alias the type-flags functions in P `Cog/sithCogFunctionThing.c:2139-2217,2801-2818`. The thing's actual type selects the union field. A misleadingly named call does not coerce that type. Template `typeflags=` is interpreted by the template's type parser.

P `Cog/sithCogFunctionThing.c:1796-1827` gives SetPhysicsFlags an explicit conditional sync request but not ClearPhysicsFlags. This asymmetry needs a multiplayer test; it is not proof that clearing can never reach a peer by another path.

SetInvFlags changes shared inventory definitions (P `Cog/sithCogFunction.c:783-797`, `Gameplay/sithInventory.c:688-707`). Per-player activated/available/found flags are separate. Goal masks use the integer-cast amount of bin goal+100, then normal amount clamping and changed delivery (P `Cog/sithCogFunctionPlayer.c:78-100`, `Gameplay/sithInventory.c:548-573`). Do not invent callable GetInvFlags/ClearInvFlags from internal C helper names: those exact native spellings are absent from the measured original registries.

Sector/surface flag setters OR and clearers AND-complement, with conditional synchronization (P `Cog/sithCogFunctionSector.c:199-237`, `Cog/sithCogFunctionSurface.c:243-281`). Adjoin verbs take a surface reference, resolve its one adjoin, and do not explicitly mutate the mirror in their wrappers (`283-338`). FaceType is a mask; FaceGeoMode/LightMode/TexMode are scalar assignments. Geometry assignment can be reduced when required material/UV data is absent (`340-466`). Desired thing modes need not equal the final renderer mode.

PlaySoundThing/Pos return playback references, not sound resource IDs. Local playback clears position/follow flags; attached playback sets up the corresponding channel state (P `Cog/sithCogFunctionSound.c:22-199`). StopSound consumes that playback reference. Runtime PLAYING/PAUSED/FADING names are not a promise that all are valid caller options.

SurfaceAnim(surface, rate, flags) and MaterialAnim(material, rate, flags) return animation indices (P `Cog/sithCogFunction.c:153-210`). Creation and loop precedence differ when 0x2 and 0x4 are combined. Keyframe PlayKey options are a different domain: the low options map into puppet-track states through an else-if chain (P `Engine/sithPuppet.c:222-279`). StopKey uses the returned track, not the original keyframe resource.

The pinned source explicitly guards some MotS behaviors, including PlayKey rejecting a dead local player and actor freeze/blind combinations. Other comments say “MoTS added” without guarding the surrounding branch. The absence of a compatibility guard is source structure, not proof that stock JK supports a feature. Current MotS-mode item collision 0x10 is also a separate type-flags case, not a ThingFlags bit.

## Indy comparison: useful evidence, separate meanings

[OpenJones3D](https://github.com/smlu/OpenJones3D) reconstructs Indiana Jones and the Infernal Machine. This comparison pins revision `a675b395a5d5cff238332d091368483b849e8b5b`; it does not claim an original Indy runtime test. Its ThingFlags wrappers also OR/AND-complement nonzero masks (Libs/sith/Cog/sithCogFunctionThing.c:2935-2988). Matching operations do not make the bit meanings portable.

| Bit | JK/MotS enhanced-source label | Separate Jones declaration |
| --- | --- | --- |
| `0x00000001` | `SITH_TF_EMITLIGHT` | `SITH_TF_EMITLIGHT` |
| `0x00000002` | `SITH_TF_DESTROYED` | `SITH_TF_DESTROYED` |
| `0x00000004` | `SITH_TF_4` | `SITH_TF_NOWEAPONCOLLIDE` |
| `0x00000008` | `SITH_TF_LEVELGEO` | `SITH_TF_WHIPCLIMB` |
| `0x00000010` | `SITH_TF_10` | `SITH_TF_INVISIBLE` |
| `0x00000020` | `SITH_TF_20` | `SITH_TF_UNKNOWN_20` |
| `0x00000040` | `SITH_TF_STANDABLE` | `SITH_TF_STANDON` |
| `0x00000080` | `SITH_TF_80` | `SITH_TF_MOUNTABLE` |
| `0x00000100` | `SITH_TF_INVULN` | `SITH_TF_REMOTE` |
| `0x00000200` | `SITH_TF_DEAD` | `SITH_TF_DYING` |
| `0x00000400` | `SITH_TF_CAPTURED` | `SITH_TF_COGLINKED` |
| `0x00000800` | `SITH_TF_NOIMPACTDAMAGE` | `SITH_TF_NOCRUSH` |
| `0x00001000` | `SITH_TF_NOEASY` | `SITH_TF_UNKNOWN_1000` |
| `0x00002000` | `SITH_TF_NOMEDIUM` | `SITH_TF_WOOD` |
| `0x00004000` | `SITH_TF_NOHARD` | `SITH_TF_SHADOW` |
| `0x00008000` | `SITH_TF_8000` | `SITH_TF_JEEPSTOP` |
| `0x00010000` | `SITH_TF_10000` | `SITH_TF_SNOW` |
| `0x00020000` | `SITH_TF_PULSESET` | `SITH_TF_PULSESET` |
| `0x00040000` | `SITH_TF_TIMERSET` | `SITH_TF_TIMERSET` |
| `0x00080000` | `SITH_TF_DISABLED` | `SITH_TF_DISABLED` |
| `0x00100000` | `SITH_TF_INCAMFOV` | `SITH_TF_SEEN` |
| `0x00200000` | `SITH_TF_RENDERWEAPON` | `SITH_TF_UNKNOWN_200000` |
| `0x00400000` | `SITH_TF_METAL` | `SITH_TF_METAL` |
| `0x00800000` | `SITH_TF_EARTH` | `SITH_TF_EARTH` |
| `0x01000000` | `SITH_TF_1000000` | `SITH_TF_NOSOUND` |
| `0x02000000` | `SITH_TF_WATER` | `SITH_TF_SUBMERGED` |
| `0x04000000` | `SITH_TF_IGNOREGOURAUDDISTANCE` | `SITH_TF_CLIMBCRATE` |
| `0x08000000` | `SITH_TF_DROWNS` | `SITH_TF_WATERDESTROYED` |
| `0x10000000` | `SITH_TF_WATERCREATURE` | `SITH_TF_AIRDESTROYED` |
| `0x20000000` | `SITH_TF_SPLASHES` | `SITH_TF_SPLASH` |
| `0x40000000` | `SITH_TF_40000000` | `SITH_TF_MOVABLE` |
| `0x80000000` | `SITH_TF_80000000` | `SITH_TF_WHIPSWING` |

Jones SNOW0x10000 has material-sound consumers at Libs/sith/World/sithThing.c:1524-1555 and2800-2816. That does not establish a JK/MotS meaning; the independently traced original JK/MotS loader uses 0x10000 for the conditional placement exclusion described above. Jones NOSOUND0x1000000, CLIMBCRATE0x4000000, MOVABLE0x40000000 and WHIPSWING0x80000000 are separate declarations, not replacements for JK/MotS meanings. In particular, the current enhanced renderer and the inspected original MotS routine use 0x80000000 for a single deferred-draw slot; the compared original JK1.01 routine lacks that path.

Even similar water/splash names conceal different contracts. The inspected JK path sends splash messages to the local player's class/capture COG; the Jones entry path sends to the entering thing's own COGs (Jones World/sithThing.c:1981-1990). Jones destroy-on-water/air paths call DestroyThing and return; the inspected JK paths directly set destruction state and conditionally notify. Copy the integration contract only after comparing consumers.

## Corrections and remaining work

The historical AI0x29 entry is a composite value, not an individual bit. The source's 0x20 TOUGHSKIN label is not sufficient to replace it with a verified fire-target description. Duplicate physics 0x10000 descriptions are not accepted as two independent flags. The ghost type8 radius mask is 0x100, not 0x80. Thing DESTROYED0x2 and DEAD0x200 are distinct. Geometry5 and light4/5 remain unknown here. Source aliases for actor 0x2000 and 0x200000 conflict and stay visible.

This release replaces reliance on unqualified tables; it does not erase the historical reference or claim its every entry was wrong. The next evidentiary step for unresolved meanings is target-specific consumer tracing followed by a small original-game reproduction. Record exact executable/hash, caller, owning field, mask, parameters, authority, before/after state and cleanup. For network claims use separate connected clients. Do not use spare-looking bits for custom state based on a negative text search.

The optional Workshop discovery reporter remains off by default. A genuinely new or contradictory result can be prepared as a local draft with provenance and credit; sending still requires the user's explicit instruction. No report is sent by reading this reference.

## Source identities

The accompanying machine catalog is the full hash/line manifest. Pinned snapshot repository: [OpenJKDF2-MotS-Fixes](https://github.com/CodaLilitari/OpenJKDF2-MotS-Fixes/tree/b1be98bd4f170b5384cb71b15443a274aac6a937). Current-only paths below are hash identities without a claimed public revision; repository availability is separate from these byte identities. The [historical Massassi page](https://www.massassi.net/jkspecs/cog_flags.htm) defines the replacement's coverage, not its per-bit authority.

| Evidence | File | SHA-256 |
| --- | --- | --- |
| pinned | `src/types_enums.h` | `2233bb6cd6a43d73e2d0da808cebbdf4c65487953764ceebfce2fe0263c58f21` |
| current | `src/World/sithExplosion.h` | `12ac8d1a46157333f91bf6c77e7384cec2b4454bbbd4721b63693b71a961c500` |
| current | `src/World/jkPlayer.h` | `8fefbe121bd0edaf6fdc4d31d8344ee6bea75b3cebcbf09c663c02e99b95d797` |
| current | `src/World/sithSurface.h` | `037371f6d7c687526b38fc51e93138b367ec3d46131b3795d96d362947f2696d` |
| current | `src/Raster/rdFace.h` | `bf4b0e791bace1d4e04068533bacf836c560fa2718776d895c5d53b1cefeb1fb` |
| pinned | `src/Cog/jkCog.c` | `aa75ff29753a92eac8b162d9e9ab80fb155a6cb3650d885e6f0006b7bc80fe47` |
| pinned | `src/Dss/sithMulti.c` | `9844cf21ea11690ed8ae2704d7c8ef9eb754836b34096b29d5801fcf32ff3a37` |
| current | `src/World/sithSurface.c` | `004068c76d8e8bd32d42f85b7b326e88b46e7a90589f408ed474d223253946a1` |
| current | `src/World/sithExplosion.c` | `e155d0bbcd7090c35fca7b7e8f3b3c8448c1d307b028035fd7c353df07c556da` |
| indy-reconstruction | `Libs/sith/types.h` | `dec16badcb63c8ed3be066e8faeb916da3250eb69fd117f78dd6a58b3d9058ef` |
| indy-reconstruction | `Libs/sith/Cog/sithCogFunctionThing.c` | `00b35f268041fa137eb8c753bf7feb2bda8255304123cbeddd37c62c9b428c45` |
| indy-reconstruction | `Libs/sith/World/sithThing.c` | `e9a6e13ee77073f1080e839e452615bc4eb2a51cc875814248431d83b47183fa` |
| current | `src/AI/sithAI.c` | `07b1ef306818ede5766bcb34cd31fbfe59f5d65d4b8cb6aae683901b65bf4a45` |
| pinned | `src/Cog/sithCog.c` | `520df53e12f096eb2783c0ef42afecdf5826c747ac72e9c3c16aab2f239e8d1b` |
| pinned | `src/Cog/sithCogFunctionThing.c` | `34e86325b8d0b2b30a56aa3e93fae02337bba566759c92233bf9192005acfcf4` |
| current | `src/Engine/sithCollision.c` | `5f78e078e825e17e05ccaceda35be700d357f3e745d0edbcc5724229f2adf3ce` |
| pinned | `src/Engine/sithPhysics.c` | `bfd0e3e70ad7e88162805617744f1536d7bae0a4ee08b18c58bc673755d4796b` |
| current | `src/Engine/sithRender.c` | `528391a9a20522cf9f695686c5095efde7934b91f290118c9bc53e3d4654769f` |
| current | `src/World/sithActor.c` | `f14a561c84c24b4d8d06452fc959b6f6fe0b2344a7877951a9b13e5b2f0d0f3a` |
| pinned | `src/World/sithThing.c` | `24bb370e91397dadc510900d3f66aac237eec44ccb881d497e21233692422249` |
| current | `src/World/sithWeapon.c` | `a976486f897c10c74bf9da63d68ef969513368136ccd2ce903bbd2b8bd8273c1` |
| current | `src/AI/sithAICmd.c` | `9adba1a5646d4cde5f418975a9cf43a12c97387becff882b3efe5f849aa00b72` |
| current | `src/Dss/sithDSSThing.c` | `277c5ea276b8b6f0e2835e434eb3e3325159edb19e67e086890b9748f2f34b29` |
| pinned | `src/Engine/sithPuppet.c` | `b536f0b6e274d1b8acb585785f83dcc968f26e2bdd4ca83478a5e1918ffe0b41` |
| current | `src/Gameplay/sithPlayer.c` | `a2ea82d4654e3bf7e08f87c81653117b316550e4fb9025877c07383fcf2dea75` |
| current | `src/Gameplay/sithPlayerActions.c` | `8c7b5b9da3ba8cdb8909a07d728d436f9483048f974387925115fe0321fbc188` |
| current | `src/Main/jkControl.c` | `5a74def33a188deb1f029f725eb83cbfc0ea9f9b662365ac8f00eadcedf5904c` |
| current | `src/Main/jkMain.c` | `1771ec5d80fc6ee1cbea764a138a838a259f460a8896562104813697df61c42b` |
| pinned | `src/World/jkPlayer.c` | `fad78733b6ea6ae03e7323a728a82701d75892c12ef79e0f614f40c6223b90a9` |
| current | `src/World/sithItem.c` | `20b2056fb8b85b592e21d679654bed17d901dfacf14eda2f532634be107de1ee` |
| pinned | `src/Cog/sithCog.h` | `f75e6fb7173b789bd3f01b9b797c587344bdf55db12342c0199d349a8cd45a23` |
| pinned | `src/Cog/sithCogFunction.c` | `dd11848d3197f5d44ad54da67aa7d72e085286b96b7c715d44dfe3e34b0312c2` |
| pinned | `src/Gameplay/sithInventory.c` | `13df52993ff024324a6a27729c3be2686abd6a17f5eebbd3436b513d4acc5810` |
| pinned | `src/Cog/sithCogFunctionSector.c` | `258d6f7b31c4f27961cc66b9f3655948855f68854eba711d45f0b0806d5a9e3c` |
| pinned | `src/Cog/sithCogFunctionSound.c` | `c916f654ebe8fe893e04e7e91d0905ee1cfe4cb0608f6372e044ffb918a12471` |
| pinned | `src/Cog/sithCogFunctionSurface.c` | `19dcf541413e4ae14d6fe3859bd291e3b5e01037d9c3a5612a6db1dcedaf2337` |
| current | `src/Devices/sithSoundMixer.c` | `3fa0f801c60a8e46cd29b4956f263c046a8c1122fbc36d40bb3630a141280719` |
| current | `src/Engine/sithParticle.c` | `239ddc3aa5fdc6baaf24e50b81ffc8cd5068c1c95bbb38f0549887a927ad98b5` |
| current | `src/Primitives/rdParticle.h` | `3747a8e1e10197aefd1a2f7574ad45776fef897d970f5682b6a426ac98ea0d0f` |
| current | `src/World/sithSector.c` | `7a12f2c539fc4bdfc5707e7ed9b6267ceba8efdd7e62b4e0a326faecce41a7c1` |
| pinned | `src/Cog/sithCogFunctionAI.c` | `72ede103fb12a0078e9d7b3cd886147faf09770880dc2b120143088ca862c4e8` |
| pinned | `src/Cog/sithCogFunctionPlayer.c` | `6fd72f9c130655b38b82ca22063b321ecc9ac9b12e61be5ea4e28d7783038d46` |
