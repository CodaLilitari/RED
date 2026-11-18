---
name: red-cog-scripting
description: Write, compile and test COG scripts for Jedi Knight and MotS with RED, and build doors, elevators, switches, objectives and cutscenes with RED's assistants. Use when a level needs scripted behaviour or a .cog file needs checking.
---

# COG scripts with RED

For what a verb or flag means, how JK and MotS differ, multiplayer protocols and worked examples, use Coda
Lilitari's JK/MotS COG Workshop and its `jk-mots-cog-workflow` skill when you have it. RED is the checking end:
it compiles what you write the way the game loads it, runs it, and builds the common scripted parts for you. The
engine's logical operators don't short-circuit, so guard an operation that could be invalid with nested `if`s
rather than `&&`.

## Check a script

`red-agent cog-compile <file.cog> <jk|mots> --strict` reads the script the way the retail game loads it. `loads` is
the engine's verdict. The diagnostics list what the game accepts silently but gets wrong: unknown verbs and
messages, wrong argument counts, missing labels, full engine tables. Fix every error; `--strict` exits 5 while any
remain. `--listing` shows the compiled code.

## Run a script

`red-agent cog-run <request.json>` runs scripts on a simulated clock against a stand-in world built from a level:

```json
{"schemaVersion":1, "target":"jk", "level":{"path":"lvl.jkl","sha256":"…"},
 "cogs":[{"script":{"path":"cog/door.cog","sha256":"…"}, "placedCog":3}],
 "events":[{"at":1, "activate":{"thing":12}}], "duration":10}
```

Events are `activate`, `enter`, `cross`, `damage` and `message`. Read the timed log: `message`, `verb`, `print`,
`arrived`, `unsimulated` and `error` entries. The runtime keeps the engine's quirks (arithmetic always gives a flex,
`!=` compares ints, an empty stack reads -1). It has no physics, AI or weapons, so verbs it doesn't simulate are
logged and return 0. See [the COG runtime](../../cog_runtime.md).

## Build scripted parts with the assistants

Prefer the assistants to hand-written scripts for the common cases. They are patch operations, so plan and apply
them like any edit:

| Operation | Builds | Script |
|---|---|---|
| `doorAssistant{pieces, openOffset}` | a door of one to four pieces | stock `00_door.cog` |
| `elevatorAssistant{elevator, travel, button?, callAdjoins?}` | a lift called by a button or adjoins | stock `00_elev_switch.cog` |
| `switchAssistant{pieces, openOffset, switches}` | a locked door its switches open | stock `00_doorswitch.cog` |
| `objectivesAssistant{goals:[{trigger:reach\|use\|kill, target}], exit?}` | goals and the level's end | `red_goals.cog` |
| `cutsceneAssistant{camera, trigger?}` | a camera move along a Thing's frames | `red_cutscene.cog` |
| `pursuitAssistant{enemy, route?, wakeSector?}` | an enemy swoop that wakes, follows its route and chases | Lily's `kcswoop.cog` |

For vehicles and enemies, start from the COG template library: `red-agent cog-templates` lists the player swoop, the
rideable Kell, enemy swoop pursuit and the bowcaster Tusken on a Kell; `red-agent cog-templates <set>` gives the
exact placed-COG values; `red-agent cog-template-write <set> <project>` copies the scripts and template rows in. The
user supplies the models, keys and sounds the set lists.

The stock-script assistants need the game's archives in `resourceArchives`. Write RED's own two scripts into the
project with `red-agent assistant-script <name> <new.cog>`. Then prove the result with
`red-agent assistant-check {level, kind, cog, script?, strict:true}`: it runs the scenario for that kind (open and
close the door, call the lift, unlock with the switch, complete every goal, play the cutscene) and lists each
expectation. Surfaces are `[sector, surface]`. Moving Things need a `move=path` template and no frames yet.

See [the gameplay assistants](../../gameplay_assistants.md) for what each refuses and warns about. Test in the game:
the runtime has no collision, so blocked doors and riding lifts are untested until then.
