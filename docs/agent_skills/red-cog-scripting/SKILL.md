---
name: red-cog-scripting
description: Write, compile and test Jedi Knight and MotS COG scripts with RED, and build doors, lifts, switches, objectives, cutscenes and vehicles from its assistants and template library.
---

# COG scripts with RED

For what a verb or flag means, how JK and MotS differ, multiplayer protocols and worked examples, use Coda
Lilitari's JK/MotS COG Workshop and its `jk-mots-cog-workflow` skill when you have it; its guide is at
https://richard-pemberton-research.darkahn.chatgpt.site/projects/jk-mots-cog-workshop/docs/GUIDE.html and its flag
findings at .../docs/FLAG_DISCOVERIES.html. RED is the checking end: it compiles what you write the way the game
loads it, runs it, and builds the common scripted parts for you.

Choose the target first: retail JK 1.01, retail MotS, or a named enhanced engine. A familiar verb can belong to the
other game or behave differently there (`CreateThingAtPos`'s last vector is rotation angles in JK and a direction
in MotS; `GetCurInvWeapon` is JK, `GetCurInvWeapon2` MotS).

## Writing COG

- Every message declared in `symbols` needs a label, and every handler path ends with `return;`. Labels aren't
  function boundaries.
- No `switch`, `break`, `continue`, `++`, compound assignment, ternaries or block-scoped variables: write
  `i = i + 1;`. Vectors are written in single quotes: `'0 0 1'`.
- `&&` and `||` evaluate both sides. Guard a call that could be invalid with nested `if`s.
- `base[index]` reads neighbouring symbols with no bounds check; keep the index in range yourself.
- A vector's default in `symbols` is ignored: every vector starts at (0/0/0). Set it in code with `VectorSet`, or
  pass it from the JKL. An int default written in hex reads as 0.
- Don't name a label after a message you don't declare (`fire`, `trigger`): it never runs for that message, and its
  address lands in the global name's slot.
- `SetTimer` replaces the instance's one simple timer; `Sleep` lets other events run in the same instance meanwhile.
- Flag setters OR bits in. Give every flag you set a way to be cleared.
- In multiplayer, `local` on a symbol doesn't stop network traffic, and the retail checksum compares each script's
  compiled size, so even a small edit can break a multiplayer match between versions.

## Check a script

`red-agent cog-compile <file.cog> <jk|mots> --strict` reads the script the way the retail game loads it. `loads` is
the engine's verdict. The diagnostics list what the game accepts silently but gets wrong: unknown verbs and
messages, wrong argument counts, missing labels, ignored defaults, full engine tables. Fix every error; `--strict`
exits 5 while any remain. `--listing` shows the compiled code.

The engine's tables per script: 256 symbols, 128 linked symbols, 32 handlers, and label slots (one per symbol and per
`if`, `else`, `while` and `for`): 512 in JK 1.01, 1024 in MotS. Past the label limit the engine writes beyond its
table. Split a big script into helpers that talk with `SendMessageEx`. A script that runs in OpenJKDF2 can still
overrun retail: compile for the retail target.

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
logged and return 0.

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

The stock-script assistants need the game's archives in `resourceArchives`. Write RED's own two scripts into the
project with `red-agent assistant-script <name> <new.cog>`. Then prove the result with
`red-agent assistant-check {level, kind, cog, script?, strict:true}`: it runs the scenario for that kind and lists
each expectation. Surfaces are `[sector, surface]`. Moving Things need a `move=path` template and no frames yet.

## Vehicles, enemies and items

Start from the COG template library. `red-agent cog-templates` lists the sets: the KnightCop swoops and Kells, the
Demolition vehicles and arena hazards, and SOTE's space fighter, space gunner, jetpack and enemy classes.
`red-agent cog-templates <set>` gives the exact placed-COG values, and the template rows or, for sets without rows,
`namedTemplates`. `red-agent cog-template-write <set> <project>` copies the scripts and rows in, never over a
different file. The user supplies the templates, models, keys and sounds the set lists.

## Test in the game

The runtime has no collision, AI or physics: blocked doors, riding lifts and enemies are untested until you play
them, in both renderers. For multiplayer, use two clients in separate processes; one hosted player proves nothing
about sync.
