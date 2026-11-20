# Classic COGs: appearance, state changes and real bindings

> Copied into RED from [CodaLilitari/jk-mots-cog-workshop@b4b43f6](https://github.com/CodaLilitari/jk-mots-cog-workshop/blob/b4b43f6ab1976f673598c90751ec6160da70c506/corpus/notes/classic-cogs-and-binding-lessons.md) (MIT; see [LICENSE](../LICENSE.md)). Regenerate with `tools/knowledge/import_knowledge.py` rather than editing this copy.

This reference draws practical authoring lessons from preserved JK packages, measured original-game name registries and inspected enhanced-engine source. It contains original analysis and source identities, not redistributed community scripts or retail assets. A historical package's intent, its actual statements, an engine interpretation and observed game behavior are separate evidence grades. None of these case studies establishes an original-retail multiplayer join or remote effect.

## The classic God saber's bolt hilt

The remembered bowcaster-bolt hilt has concrete source support. Three recovered JK weapon scripts select `bow0.3do` as the external saber mesh: LOTJ_Shockwave's Godkill Extended Lightsaber in [Clint Eastwood Weapons Pack](https://www.massassi.net/media/levels/files/jkmod/cewp.zip), EAH_TRISCUIT's enhanced saber recovered in [AWP](https://jkdf2.net/JKBot/JKArchive/JKDF2/JKDF2/Game%20Files/jkmod/AWP.zip), and [EAH Christmas 2](https://jkdf2.net/JKBot/JKArchive/JKDF2/JKDF2/Game%20Files/jkmod/eah_xmas2.zip). CEWP also selects that model for the first-person view; the other two retain `sabv.3do` there. These are model assignments inside the selected-weapon handlers, beyond suggestive filenames.

The AWP readme identifies its weapon as a God Saber derived from Triscuit's Build3beta and describes the bullet-shaped replacement handle. That attribution does not establish byte identity with a separately recovered original Build3beta. A preserved 30 November 1998 post by Acrissime, *The Final Definitive Word on JK Sabers*, describes using the projectile model for a green lower extension and a bulge near the handle. Its source is the public [Massassi mboard13 archive](https://www.massassi.com/etc/archived_msgboards/massassi/mboard13.zip). Contemporary testimony and independently inspected scripts therefore support the visual explanation. They do not establish an inventor or statistical prevalence.

Keep four contracts separate: the visible model, saber collision/damage settings, explicit changes to a victim, and network/checksum behavior. Selecting `bow0.3do` alone supplies no proof of enormous damage, a second damaging blade or protection removal. AWP binds bin 10 to its saber COG but supplies no replacement `bow0.3do`, `sabv.3do` or `kyle.cog`; the effective models and class script depend on the remaining mounts. These examples target JK. Original MotS rendering and integration remain untested.

## AWP: inspect the protection-removal attempt separately

AWP's saber contains damage settings of 99 and 350 alongside a `USER5` handler with a different responsibility. The handler takes the first returned candidate from a view query and checks whether its position is within 0.5 of the player. On that branch it sets the candidate's collision type to 1, clears actor mask `0x2008`, and sets actor bit `0x2000` on the local player. It neither iterates to a second candidate nor damages the candidate directly. It also does not preserve and later restore the candidate's former collision or flags.

The inspected enhanced source names actor bit `0x8` invulnerable. Its enum calls `0x2000` BLEEDS, while its saber code uses the same bit in an actor/player blocking branch. Read both the wrapper and the consumer before assigning meaning from an enum label or COG comment. Setting this bit is not demonstrated universal invulnerability.

Several attack branches request `USER5` and then evaluate nested calls named `x`. The measured original registries contain no native `x` verb. Separately, assignments from `SendMessage` lack a justified returned value in the inspected wrapper. These expressions should not be described as harmless padding, guaranteed syntax rejection or ordinary zero-return functions. A source-level message side effect can precede later stack problems. Exact VM execution and original-game behavior remain open.

## What the recovered Fade operation means

Triscuit's inspected generator source emits an actor-flag operation with mask `0x400000` when its FADE option is selected. A distributed rail-detonator script in [TTNS Mod](https://www.massassi.net/levels/files/1911.shtml) contains the same operation; Emon's sector script in [Space Noxx II](https://www.massassi.net/levels/files/3113.shtml) independently uses it for low-gravity pits. This establishes a concrete historical Fade component. TTNS combines it with damage, explosions and other state changes, so a successful combined attack would not identify which component caused death.

In the inspected enhanced source, `0x400000` is FALLKILLED. The local-player update reduces the screen palette's fade while that bit is set and invokes a player-kill routine when the fade reaches zero. That routine is separate from the ordinary actor-damage path and its protection checks. Directly setting the flag does not require the physical pit detector's speed and sector conditions, nor does it perform all that detector's separate camera and dead-flag work.

This explains a mechanism to test; it does not prove that a remote retail client accepts the state change or that every protection script loses. Set/ClearActorFlags reach wrappers that can request synchronization under specific conditions. A queued synchronization request is not verified remote delivery. The fade consumer runs for each client's local player. Reapplication, respawn and palette restoration therefore require separate observations; clearing the flag alone is not evidence that a partially faded palette has reset.

Both the TTNS and Emon code sections passed the captured parser/code-generation adapter. That adapter omits complete headers, mounted resources and the VM. It provides no retail checksum-acceptance result.

## JKDF2 and MotS contracts that must remain distinct

| Inspected contract | Original JK 1.01 | Original MotS |
| --- | --- | --- |
| Rail-detonator inventory binding | Bin 7, `weap_raildet.cog` | Bin 127, `weap_raildet_m.cog` |
| Rail ammunition inventory | Bin 15, maximum 30 | Bin 15, maximum 30 |
| `activate` / `activated` message names | Both registered as ID 1 | Both registered as ID 1 |
| `timmer` message spelling | Absent from measured registry | Absent from measured registry |
| `jkSyncForcePowers` verb spelling | Absent from measured registry | Present in measured registry |

The inventory rows come from inspected retail `ITEMS.DAT` members. The name rows come from measured executable registries. Do not mechanically replace every 7 with 127: a weapon-selection index, an inventory bin and another integer argument can have different domains. Likewise, a registered name establishes availability, not matching semantics, resource resolution, argument correctness or runtime success. An enhanced engine may expose names unavailable in the original target.

## Sphere: level bindings reveal what standalone scripts hide

[Sphere by LowD and Huck](https://www.massassi.net/levels/files/310.shtml) is particularly useful because its small package contains eight COGs and its JKL instantiates all eight. Its script-resource table has 24 names; the `World cogs 32` declaration is not a count of 32 actual instance rows. Reading the directory, resource table and concrete bindings answers different questions.

Both `elev.cog` and `elev_switch.cog` are bound to thing 7. Reviewing either alone misses their shared movement target. The switch controller declares `switch0` and `movespeed` but uses `switch` and `speed`; its declared message is `timmer`, although its code label is `timer`. Activation also falls through into the `arrived` body because no return separates them. It schedules a timer on that immediate path, without waiting for an arrived event.

The startup COG has the same structural lesson: after scheduling a 15-second timer, execution reaches the timer label immediately. A label does not itself stop sequential execution. That body also calls the MotS-registered `jkSyncForcePowers` despite the package's JK target. These findings identify source and target mismatches; a complete original header/VM run has not established their precise failure behavior.

The generators' `activate` spelling is valid in both measured registries. Their actual problems include undeclared local-use names and, in the larger variants, mismatched generator-reference names. The short-lived `active` guard is cleared in the same handler; it does not implement persistent one-shot behavior. Repeated creation has no corresponding existing-item tracking or removal in these sources.

The platform controller uses undeclared names including `active`, `door0` and `doorstatus`. One path sets its activity latch before a movement check, then returns without clearing it. Treat that as a source-level latch risk, not an observed soft lock. The bound platform has six frames, while this controller requests only frames 0 and 1.

Finally, the forcefield instance supplies `-1` for its second switch surface, yet the script uses that reference without an optional-reference guard. Its shared touch-damage throttle applies across both bound damage surfaces. The package readme reports host-only button messages and occasional elevator trouble, but those are author observations; these static findings do not establish the cause of either report.

## Reusable inspection sequence

For a new script, identify the exact package and target, then map its real JKL/template/inventory bindings. Trace every declared event through labels, calls, sleeps, returns and cleanup. Compare names against the intended retail registry before interpreting argument values. Inspect the engine's consumer of each changed flag, not only the setter. Record missing resources, optional references, shared controllers and stale target IDs. Then design bounded runtime observations for the unresolved mechanism, including original-game and separate-client checks where claimed.

## Selected SHA-256 identities

These identify the inspected inputs; raw originals are not included here.

| Input | SHA-256 |
| --- | --- |
| CEWP saber COG | `77ae183cd3b19be6cc838e606885fb4fdcbe206348759b8f296a680bbd0ee731` |
| AWP ZIP | `a98162882725bac46e7e3c035427b492db49728c20acbc7386b66935db7e98ff` |
| AWP saber COG | `d2fdfa2e7b40a11bff1f04145fee937a1a4c3aff8a43439d5e892e299fa9bc18` |
| EAH Christmas 2 saber COG | `408650c5688aa453525daf468dc3d9adff8b420d014ad5e2bda8febaa25950b9` |
| TTNS ZIP | `9ef02c608d754ee57163c0d50dabe85688e5507e1b71176d8d7bd2cbd3405b6e` |
| TTNS rail-detonator COG | `6dd49f51f4b70a38f83cc7b552b26c73bf2da8238f6ee3346e0b4440abcc5693` |
| Emon sector Fade COG | `9d89ce22d5c8a8c8d402f2368f01c0703dd3e2e62166f2ed47365ac43ecb5e92` |
| Sphere ZIP | `4a80e10839d6d91e7fbeac02a570a898aa20cb99c65fbe4c5d47369596c4f1b4` |
| Sphere JKL | `79fed4fedeae70cd4dc1df1e7d334e6aaa70257f8dcd6c6167022d41459327d2` |
| Sphere startup COG | `01a41bb676b9d9bb20b562a7584cb2243ee53a90c720ec0b0e0bf483676ee107` |
| Sphere elevator-switch COG | `d2aa4a10e81bba4f376ea7648cf59d4cef5287b068d9c015bfcbc9ebc01dc798` |
| Inspected enhanced `sithPlayer.c` | `a2ea82d4654e3bf7e08f87c81653117b316550e4fb9025877c07383fcf2dea75` |
| Pinned enhanced `sithCogFunctionThing.c` | `34e86325b8d0b2b30a56aa3e93fae02337bba566759c92233bf9192005acfcf4` |

The two engine hashes pin the analyzed bytes, without asserting that current upstream code or an original executable is identical. Preserve that distinction when extending these findings.
