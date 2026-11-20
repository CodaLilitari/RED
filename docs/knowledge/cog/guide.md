# Writing COGs for Jedi Knight and Mysteries of the Sith

> Copied into RED from [CodaLilitari/jk-mots-cog-workshop@b4b43f6](https://github.com/CodaLilitari/jk-mots-cog-workshop/blob/b4b43f6ab1976f673598c90751ec6160da70c506/docs/GUIDE.md) (MIT; see [LICENSE](LICENSE.md)). Regenerate with `tools/knowledge/import_knowledge.py` rather than editing this copy.

For flag-based behavior, start with the [master flag findings](flag-discoveries.md) and the [flags and numeric values reference](flags.md). The master page and [machine-readable JSON](https://github.com/CodaLilitari/jk-mots-cog-workshop/blob/b4b43f6ab1976f673598c90751ec6160da70c506/corpus/reference/flag-discoveries.json) distinguish JKDF2, MotS and Indy evidence explicitly. It separates ThingFlags, actor/type, physics, JK, inventory, surface, rendering and animation domains; covers all 26 historical table families; and marks unresolved meanings and target limits. The original JK/MotS setter contract is separate from enhanced-source behavior. Retrieve one relevant family or bit at a time rather than loading the entire research library.

This guide takes you from a linked switch to a maintainable gameplay system. It is written for humans; the bundled skill supplies the corresponding agent workflow. The examples are original teaching code, and the explanations draw on measured original-game registrations, inspected engine source, owned retail examples and historical community research. No proprietary game scripts or private conversations are reproduced here.

Start by choosing **stock JK 1.01**, **stock Mysteries of the Sith**, or an **explicitly identified enhanced engine or extension**. A script that uses a familiar name may still belong to the wrong game. Keep [COMPATIBILITY.md](compatibility.md) beside this guide. It lists the target differences and labels which claims come from original binaries, source interpretation or bounded tests.

## 1. What a COG actually controls

A COG is an event-driven script. The engine supplies messages, and the script calls native functions, traditionally called *verbs*, to inspect or change game state. The script does not run independently of the world. It needs a route by which events reach it and valid resources on which its verbs operate.

One file can supply several instances. Two doors can use the same COG file while maintaining separate symbols, links and timer state. Within one instance, however, the handlers share their variables. A `local` symbol is not a C function's private stack variable.

Choose the role before choosing the code:

| Role | Integration you must inspect |
| --- | --- |
| Level COG | Instance in the JKL, ordered parameters, linked things/surfaces/sectors |
| Thing class or capture COG | Template binding, inherited fields, thing lifetime and event routing |
| Weapon, inventory or Force COG | Same-game `ITEMS.DAT` bin, selection/activation lifecycle, player, assets and templates |
| Static/global COG | Static-world loading and callers; its index is not a placed level-instance index |
| Local presentation | Intended listener/viewer, camera/audio/UI cleanup, dispatch locality |
| Multiplayer system | Gameplay authority, replication path, peer identity and recovery of state |

A loose file that has never been referenced is not an installed feature. A parsed file with incorrect level parameters is not a working feature. Think of source, binding, resources and lifecycle as one deliverable.

## 2. Build a small test episode

Use a separate test project for the chosen game. Keep the original installation intact. Make one room, one player start and one usable switch surface in your editor. Put [cw_counter.cog](https://github.com/CodaLilitari/jk-mots-cog-workshop/blob/b4b43f6ab1976f673598c90751ec6160da70c506/examples/shared/cw_counter.cog) in the project's `cog` resource directory, add a level instance, and bind its `switchSurface` parameter to that surface. Build and load the episode in the intended engine.

The useful observation is simple: activating the surface prints successive integers. If nothing happens, first establish whether the file loaded and whether the surface can send activation. Adding more script logic will not fix a missing instance or a non-usable surface.

This release's examples have offline spelling and parser checks; they have not been run in original retail games. The [example instructions](https://github.com/CodaLilitari/jk-mots-cog-workshop/blob/b4b43f6ab1976f673598c90751ec6160da70c506/examples/README.md) state the required setup, and the [validation record](https://github.com/CodaLilitari/jk-mots-cog-workshop/blob/b4b43f6ab1976f673598c90751ec6160da70c506/docs/EXAMPLE_VALIDATION.md) separates those checks from work still needed in your level.

## 3. Read a COG from the outside inward

A conventional file has an optional `flags=...` line, a `symbols` section ending with `end`, and a `code` section ending with `end`. Use that structure in new work even where a particular parser tolerates an omission.

The counter declares an exposed surface, an internal integer and two messages:

```cog
symbols
surface switchSurface linkid=1
int count=0 local
message startup
message activated
end
```

The exposed surface is a level parameter. The `local` count retains its internal value rather than consuming a level argument. For an authored level COG, document the externally supplied parameters in declaration order. Inserting another nonlocal parameter into the middle changes what later values mean in existing instances.

`linkid=1` is a label chosen by this script's author for an event link. It is not surface number 1. `nolink` suppresses that ordinary link when a resource is needed as data but should not be another event source. A linked-reference mask selects source thing types; it is not a list of message numbers.

Use the appropriate declaration type: `thing`, `surface` and `sector` identify different domains; `sound`, `model`, `keyframe`, `template`, `material`, `ai` and `cog` reference other resource types. Ordinary values include `int`, `flex` and `vector`. Do not pass a template index where a verb expects a live thing just because both print as integers.

In the code section, labels identify entry points:

```cog
startup:
    count = 0;
    return;

activated:
    if (GetSenderID() != 1) return;
    count = count + 1;
    PrintInt(count);
    return;
```

Declare the message **and** supply its label. End each intended handler path explicitly with `return;`; labels do not create automatic function boundaries. Use `call helperLabel;` for a COG subroutine and provide its return path. A helper label does not itself need a message declaration.

## 4. Use COG syntax, not remembered C syntax

COG resembles a reduced C language. Write assignments, braces, `if`/`else`, `while`, `for`, `goto`, `call` and explicit returns. Declare variables in `symbols`. The inspected grammar does not provide C functions with argument declarations, block-scoped variables, `switch`, working `break`/`continue` statements, `++`, `--`, compound assignment or a ternary expression. Write `i = i + 1;`.

Vectors use single quotes, such as `'0 0 1'`; strings use double quotes. Use `#` or `//` line comments. Use ordinary decimal or explicit hexadecimal constants; leading zeroes can invoke integer scanning behavior you did not intend. Use parentheses to show the intended grouping rather than relying on a visual resemblance to mathematics.

The inspected compiler/VM evaluates both sides of `&&` and `||`. **Do not use a logical expression as a guard against a dangerous second call.** Prefer:

```cog
if (actor >= 0)
{
    if (GetThingHealth(actor) > 0.0)
    {
        call handleLivingActor;
    }
}
```

This is a syntax fragment, not a complete script; the example assumes `actor` and `handleLivingActor` are declared/defined in their proper places. An index above `-1` also does not prove that a destroyed and reused thing is still the same actor.

The VM uses tagged stack values. A declaration is useful documentation and initialization, but it does not grant all C type semantics. Use explicit comparisons for fractional conditions, and do not use a void verb inside arithmetic. A missing or misspelled native call can survive grammar parsing without becoming a functioning verb. This is why an offline parser and a target verb check answer different questions. The pinned grammar and VM are linked in [SOURCES.md](sources.md).

The traditional `base[index]` technique indexes adjacent symbols. It does not allocate an ordinary bounded array. Keep declarations adjacent and enforce the index bounds yourself. For a dynamically sized store, inspect the target's heap calls and own its allocation/free lifetime explicitly; do not infer a return value or safe repeated freeing from a function's name.

## 5. Know who sent the event

For a linked surface's activation, the surface is the sender and the activating thing is the source. Other messages have different producers. Record the contract for each message you actually handle.

| Query | Question it answers |
| --- | --- |
| `GetSenderRef()` | Which reference sent this event? |
| `GetSenderType()` | What domain does that reference belong to? |
| `GetSourceRef()` / `GetSourceType()` | What originating source did this producer supply? |
| `GetSenderID()` | Which link ID, or extended-timer ID, reached this handler? |
| `GetParam(0)` through `GetParam(3)` | What event-specific payload values were supplied? |

Do not treat all five identities as “the player.” A surface reference cannot be used as a thing reference. A link ID cannot be used as a network player index. A number received as an ordinary parameter is not automatically converted into a valid local thing reference on another peer.

`SendMessage(targetCog, message)` and `SendMessageEx(targetCog, message, p0, p1, p2, p3)` are useful for cooperating components. Define the payload fields and legal callers in writing. `ReturnEx(value)` sets a handler's response in the inspected implementation; follow it with `return;` when execution should stop. A synchronous local response is not a promise of a synchronous remote network reply.

## 6. Turn an effect into a state machine

[cw_timed_light.cog](https://github.com/CodaLilitari/jk-mots-cog-workshop/blob/b4b43f6ab1976f673598c90751ec6160da70c506/examples/shared/cw_timed_light.cog) has two states: idle and busy. Startup initializes the extra light. Activation changes state before setting the on value and timer. The timer restores the off value and clears busy. Repeated activations during the hold are ignored by design.

Writing that policy first prevents accidental timer behavior. Other valid policies include restarting the timeout, toggling off immediately, or queuing a request. Choose one; do not leave the answer to whatever repeated `SetTimer` happens to do.

| Facility | Useful role | Constraint to remember |
| --- | --- | --- |
| `Sleep(seconds)` | A suspended sequence | Other events can reenter the instance and change shared symbols/context |
| `SetTimer(seconds)` | One delayed state transition | Replaces the instance's simple timer; nonpositive values cancel it in the inspected implementation |
| `SetPulse(seconds)` | Repeating work while enabled | Disable it when its owning state ends |
| `SetTimerEx(seconds, id, p0, p1)` | Identified delayed events | Use positive IDs when later cancellation matters; validate payload and stale work |

Capture necessary event data before a suspension, then protect that capture from another handler overwriting it. For one outstanding task, a busy guard may be enough. For concurrent requests, use per-request storage, IDs and explicit ownership. When an object can be destroyed or replaced, validate its identity again before acting later.

The timed-light example owns one sector's **extra light**, not every component of lighting in the room. Another COG changing the same field is another writer and may conflict. An off value of zero does not mean the room is black. Both points are integration requirements, not syntax errors.

## 7. Move doors and platforms through their real lifecycle

[cw_two_stop.cog](https://github.com/CodaLilitari/jk-mots-cog-workshop/blob/b4b43f6ab1976f673598c90751ec6160da70c506/examples/shared/cw_two_stop.cog) alternates between frames 0 and 1. It requires a correctly configured path mover already stopped at frame 0. A switch begins movement, `arrived` changes the next destination, and `blocked` stops movement while retaining the pending destination for retry.

This teaches three separate events: a request to move, successful arrival and obstruction. A timer equal to estimated travel time does not prove arrival. `WaitForStop` in the inspected source waits on particular path-movement state; it is not a general promise that physics velocity is zero.

A finished door system also needs a deliberate obstruction policy, sounds, lock state, permitted activators, automatic-return rules and multiplayer behavior if applicable. A lift may need rider behavior and platform-specific collision testing. Use the closest same-game stock system from your own installation to inspect those contracts, then preserve its binding and resource dependencies while modifying the behavior.

## 8. Build weapons, inventory items and Force powers as whole systems

Start with the same-game bin definition and a complete weapon or item lifecycle. A new projectile template alone does not make a usable weapon. Track what owns the current player reference, whether the item is available, how selection starts/ends, when firing is allowed, when ammunition or Force energy changes, and how effects are stopped on deselection or death.

Use a small behavior table before editing:

| Transition | Questions |
| --- | --- |
| Selected | What POV model, animation and state are installed? |
| Activated/fire | What authorizes a shot, how is repeat fire scheduled, and when is cost charged? |
| Deactivated | Which timer, pulse, sound and animation must stop? |
| Deselected | What shared player/camera/weapon state is restored? |
| Killed, respawned or unavailable | Can delayed work still fire? Are saved references and temporary effects reset? |

The source-reviewed base projectile call is:

```text
FireProjectile(shooter, template, fireSound, mode, fireOffset, aimError,
               scale, scaleFlags, autoaimFov, autoaimMaxDistance) -> thing or -1
```

The MotS registry additionally includes `FireProjectileData`, whose extra data argument follows those ten arguments in the inspected implementation. Check the selected template and the same-game call sites before assigning meanings to an opaque mode/flag combination. A syntactically accepted call with one omitted argument can corrupt the intended stack contract rather than produce a helpful compiler error.

**JK and MotS use different weapon query spellings and selection/bin conventions.** JK's original table exposes `GetCurInvWeapon`; MotS's exposes `GetCurInvWeapon2`. The inspected original MotS getter returns the raw inventory bin, while the pinned enhanced binding returns an ordinal in MotS mode. Do not pass the original getter's already-raw result through `GetWeaponBin` again.

For a candidate spanning the inspected original MotS and enhanced MotS implementations, validate `GetCurWeapon`'s ordinal, map it once with `GetWeaponBin`, then retain the resulting bin. Stock MotS saber ordinal 11 maps to bin 131. Original `SelectWeapon` treats values above 10 as bins, so passing 11 directly requests bin 11. `SetInvAvailable` also requires bins; do not carry a JK 1–10 availability loop over unchanged. These are static binary/source findings, not a completed playable port. The [weapon diagnostics](https://github.com/CodaLilitari/jk-mots-cog-workshop/blob/b4b43f6ab1976f673598c90751ec6160da70c506/examples/README.md) print results without imposing a conversion; see [the detailed contract correction](notes/target-contracts-and-lifecycle.md).

`SetInv` has no pushed result in the inspected wrapper; `ChangeInv` does. Inventory setters may trigger a `changed` event, which can reenter logic. Do not put the same state mutation unconditionally in both the setter's caller and the resulting notification handler. In MotS, some low weapon-related arguments are converted through selection order; there is no universal numeric offset that makes a JK inventory script a MotS script.

## 9. Cooperate with AI instead of assuming it has stopped thinking

[cw_ai_destination.cog](https://github.com/CodaLilitari/jk-mots-cog-workshop/blob/b4b43f6ab1976f673598c90751ec6160da70c506/examples/shared/cw_ai_destination.cog) reads a marker's position and asks an actor to look and move there. That is a request to the AI system. Navigation, current goals, the actor's `.ai` class and surrounding geometry still determine what happens.

For an escort or cutscene, define who controls movement and facing during each phase. Do not let an AI goal, path motion and a COG teleport loop all compete without a transition rule. Test the actor before it sees an enemy, while fighting, after obstruction, and after the scripted phase ends.

Flag-setting verbs frequently OR bits into existing state; they do not necessarily replace the whole state. Pair changes with the appropriate clearing/restoration route. MotS-only AI verbs need an explicit MotS target. The inspected `AISetAlignment` writes through an AI class pointer, so shared-class effects require review before using it as though it were always a private per-actor field.

## 10. Treat multiplayer as a protocol design problem

The supplied examples are single-player teaching components. To make a feature multiplayer-ready, decide which peer owns each authoritative change and which peers should only present the result. Damage, spawning, inventory, moving things, sounds and UI can use different network paths.

For each effect, write down:

1. Who may request it, and who validates the request?
2. Which instance changes the authoritative state exactly once?
3. Which native verb or explicit message carries the result?
4. How are repeated, stale or invalid requests handled?
5. How does a joining player obtain current state?
6. How do death, departure and level transition clean up?

A script execution flag and a verb's network implementation are separate mechanisms. In particular, `local` on a symbol says nothing about network silence. A locally executing COG can still call a verb that transmits. The inspected `SendTrigger` path does not use the ordinary COG synchronization gate, so do not treat a no-sync flag as a prohibition on every message.

For explicit triggers, document the protocol number and every payload field. In the inspected implementation, the trigger ID becomes the event's source reference and the local player thing supplies the sender. Choose stable identifiers meaningful to the protocol; a numeric thing index in a flex payload is not automatically translated through a thing-GUID path. Late joins need an actual state-recovery route, not merely another broadcast of a start effect.

Test with separate clients and identify their exact executable and content versions. Trigger from each relevant side, observe remote effects and test joining after the state has changed. A single hosted player does not establish synchronized behavior. Historical cheat COGs are useful case studies of local effects, altered gameplay state and protocol assumptions; their advertised names do not establish what they actually do or which peers they affect.

## 11. Debug in layers

When a feature fails, collect the earliest concrete failure rather than changing several systems at once.

| Symptom | First useful check |
| --- | --- |
| No effect and no diagnostic | Did the intended file and instance load? Does its event route exist? |
| Parser complaint | Reduce to the failing statement; check supported grammar and exact preceding line |
| A native call does nothing | Target spelling, argument order, return contract, reference domain and validity |
| Wrong object responds | JKL parameter order, sender/source domains, template inheritance, duplicate basename |
| First use works; later uses fail | State reset, timer ownership, repeated events and cleanup |
| Failure after a delay | Shared state overwritten, stale object identity, reentry or changed event context |
| Only one peer sees it | Authority, event dispatch and verb-specific replication |
| Join checksum rejected | Effective loaded data and exact retail/patch target; see the next section |

Run the bundled inspector on each intended target from the release directory:

```text
python cog_workshop.py --offline inspect candidate.cog --target jk
python cog_workshop.py --offline inspect candidate.cog --target mots
```

The `--offline` option makes these authoring checks local. The skill's underlying `scripts/inspect_cog.py` also accepts `--strict` to make absent native/message spellings produce a failing exit status. Read warnings rather than automatically renaming everything the tool dislikes.

The inspector is a lexical preflight, not a compiler or type checker. A parser test is stronger for grammar, but does not prove resource resolution, event delivery, native-call semantics or game behavior. Keep a short test record: intended target, source hash, binding/resource changes, steps, actual observations and remaining limits.

## 12. Understand retail checksum failures without folklore

The inspected original JK 1.01 and MotS executables compare loaded-world checksums during joining. They do not simply compare the number of characters in a COG or hash every source file byte. Static analysis found a particularly consequential unit mismatch: each script's compiled instruction **word count** is passed to a helper that reads that many **bytes**. For a stored count of `N` int32 words, that step reads the first `N` bytes of a `4N`-byte instruction span.

This explains why a particular compiled tail change can leave that contribution unchanged. It does **not** mean “anything added at the end is safe.” An edit can change the word count, symbol numbering, branch destinations or bytes earlier in the prefix. Source position and compiled position are different. Other inputs include world/static script order, vertex data, selected template fields and, for MotS, personality data.

| Observation | Valid conclusion |
| --- | --- |
| Only comments changed | Unchanged compiled data and other inputs would preserve the checksum; verify that premise |
| Source character count stayed equal | No general checksum conclusion |
| A COG is local/no-sync | No blanket exclusion from the inspected script checksum loops |
| Two COG files have identical SHA-256 | These bytes match; the complete loaded world may still differ |
| A join succeeds in an enhanced build | That build accepted it; retail acceptance is not established |
| Loaded checksum values match | This equality check can pass; behavioral correctness is a separate question |

Use a clean joining baseline. Change one cause at a time and compare edited/unedited peers, then matching edited peers, in the target executable. Record which archives and loose overrides really loaded. The inspected enhanced-engine revision compiles out a rejection block and has a different MotS personality-reading loop; its successful join must not be reported as an original-retail test. The detailed compatibility notes state the evidence level without distributing the original binaries.

## 13. Port deliberately between JK and MotS

First inventory the complete feature: source files, events, resources, bins, templates, level arguments and network assumptions. Run the availability check for the destination game. Then inspect each unavailable call and each same-named call whose implementation is mode-sensitive.

`CreateThingAtPos` is a useful warning: the inspected JK implementation interprets its final vector as rotation angles, while the MotS implementation treats a nonzero final vector as a direction to normalize. A port can pass name checks yet face the wrong way. MotS's `CreateThingAtPosOld` provides the older-style implementation in the measured registry/source combination. Verify the desired target and result; do not mechanically swap names.

After native contracts, reconcile inventory and assets. A valid MotS event or resource name is not automatically available in JK. Rebuild the feature in a destination-game test episode and repeat behavior, lifecycle and multiplayer checks. Keep separate variants when the target distinction is clearer than a complex shared implementation.

## 14. Extend the workshop responsibly

Use the searchable public research metadata to locate relevant evidence, then use your own installation and legitimately obtained source packages when full examples are needed. Keep original credits and distinguish an author's description from behavior independently established by inspection or testing. The public release can summarize lessons learned from private material without publishing its text, identities, message links or attachments.

For a new system, finish with four things: the COG, the integration instructions, the declared target and a test record. Add the closest successful and failed cases to your project notes. This makes the next change easier to reason about and keeps “parses,” “loads,” “works in one engine” and “works in original two-client multiplayer” from collapsing into one unsupported claim.

The [source guide](sources.md), [target matrix](compatibility.md) and [original example set](https://github.com/CodaLilitari/jk-mots-cog-workshop/blob/b4b43f6ab1976f673598c90751ec6160da70c506/examples/README.md) provide the next steps without requiring the private research archive.

## 15. Learn from complete historical systems

Start with the actual package, then trace the script through its inventory or level binding. A file's presence establishes neither that it is loaded nor that the surrounding mod supplies all of its resources. In Sphere, all eight embedded COGs have explicit JKL instance rows, and two different controllers reference elevator thing 7. That shared ownership matters even if each controller appears reasonable in isolation. A `World cogs` capacity is not the number of explicit instances.

Check declarations against every use, and follow execution past each event label. Scheduling a timer does not end the current handler. Sphere's startup source requests a timer and falls directly into its timer body; a switch source similarly falls into its arrived body. Missing symbol declarations, an optional surface passed as -1, and inconsistent spellings need separate investigation. The name inspector does not validate those contracts. Conversely, `activate` is a valid alias of `activated` in both measured original games.

Classic cheat scripts illustrate why a feature name is an inadequate specification. Recovered God sabers select `bow0.3do`, the bowcaster projectile model, for their visible weapon mesh. A contemporary account describes its green lower-blade effect and handle bulge. That visual choice is independent of damage values and any attempt to alter another actor's protection. A Triscuit-derived saber selects only one candidate and conditionally changes its collision and flags; it does not thereby prove a successful attack against every protected player.

The recovered Fade operation sets actor flag `0x400000`. The inspected enhanced engine associates it with a local-player palette fade and a later self-kill path, separate from ordinary weapon damage. A combined weapon may also inflict damage, change health, or spawn an explosion. Isolate those effects before attributing a result to Fade. Original retail behavior and remote delivery require their own observations.

For a JK-to-MotS rail-weapon port, the inspected inventory bindings use JK bin 7 with `weap_raildet.cog`, and MotS bin 127 with `weap_raildet_m.cog`; both use ammo bin 15. Review each verb's numbering contract before changing constants. Read the [full historical case studies](notes/classic-cogs-and-binding-lessons.md) for attributed sources, exact identities, and the limits of these findings.

## 16. Own delayed work, shared state and recovery

Decide what repeat activation means before choosing a timer: replace the deadline, extend it, queue independent work or reject the repeat. Changing a stage variable does not reschedule a timer. In the reviewed enhanced event manager, repeated extended IDs coexist; a positive-ID cancellation operates across the owning COG instance's matching queued events, not one player encoded in a parameter. A callback already selected for dispatch still needs a stale-state check. This manager is separate current-local source evidence, not original retail runtime proof.

Save only state you actually own and define how another system can supersede it. A fixed normal-gravity reset does not restore prior physics flags. A fixed fallback skin does not restore the previous model. A model swap can also occur while a disguise effect retains its own saved model and cleanup timer. Define which owner restores the final state, validate object identity after delays, and make cleanup idempotent where the target permits it.

For a multiplayer movement protocol, identify the operation and expected participants. Counting arrival messages alone does not distinguish duplicates, late feedback or changing player counts. The reviewed SM HQ door pair exposes that limitation in source. Specify timeout and late-join recovery as well as the happy path; verify separate connected clients before claiming synchronization.

A negative count is a convention only when the code implements it. Two SM HQ generator instances bind -1, but their source tests the total against that value unconditionally after incrementing. Their first qualifying attempt therefore requests pulse shutdown. Check successful creation before capture/counting, identify owned actors before decrementing, and assign an explicit restart target.

Finally, name the question you are testing. Visibility, line of sight, point containment and collision require different evidence. Validate references before native queries and compare documented success values explicitly. Extension-only presentation calls need their actual build features, model/key resources and cleanup contract; they are not substitutes for original JK or MotS verbs. The [target and lifecycle study](notes/target-contracts-and-lifecycle.md) develops these cases and labels what remains untested.
