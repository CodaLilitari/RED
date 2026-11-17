# The COG runtime

RED runs COG scripts against a stand-in for the game, so you can see what a door, elevator or switch script does
before you load the level in JK. Each script is compiled first, the way the game loads it (see
[`cog_compiler.md`](cog_compiler.md)). A script the game wouldn't load is reported and doesn't run.

- From the agent: `red-agent cog-run <request.json|->`. MCP: `sed_cog_run`. `red-agent schema cog-run` prints the
  request schema.
- There is no app UI yet.

## A request

```json
{
  "schemaVersion": 1,
  "target": "jk",
  "level": { "path": "lumen.jkl", "sha256": "…" },
  "cogs": [
    { "script": { "path": "cog/00_door.cog", "sha256": "…" }, "placedCog": 3 },
    { "script": { "path": "cog/lift.cog", "sha256": "…" }, "bindings": { "lift": 41, "speed": 4.0 } }
  ],
  "events": [
    { "at": 1.0, "activate": { "thing": 12 } },
    { "at": 6.0, "enter": { "sector": 88 } },
    { "at": 9.0, "message": "user0", "cog": 1, "params": [2, 0] }
  ],
  "duration": 20
}
```

- `level` is optional. With it, the world's things, path frames, sectors and surfaces come from the level, and
  `placedCog` takes a placed COG's values from it. The hash must match the file.
- `bindings` set symbol values by name and override `placedCog`'s. Things and sectors are indices. Surfaces are
  numbered sector by sector, as the engine numbers them. Numbers fill int and flex symbols, `"(x/y/z)"` fills a
  vector, and a name fills a resource symbol.
- A `cog` symbol's value is a script's position in `cogs`, not a placed COG number.
- `step` sets the clock step (0.01 s by default), and `instructionBudget` sets how many instructions a script may
  run without sleeping before it is stopped as a runaway.

Each event happens at its time on the clock and needs exactly one of these:

| Event | What the scripts get |
|---|---|
| `activate: {thing}` or `{surface}` | `activated`, sent to every script linked to it, as when the player presses the use key |
| `enter: {sector}` or `{surface}` | `entered` |
| `cross: {surface}` | `crossed` |
| `damage: {thing, amount, type?}` | `damaged`, with the amount and type as parameters, then `killed` if its health runs out |
| `message`, `cog`, `params?` | that message, sent to one script |

A script is linked to an object when one of its thing, sector or surface symbols holds it and isn't `local` or
`nolink`. `GetSenderId()` gives that symbol's `linkid`. Every script gets `startup` at time 0, in order.

The output lists each script (whether it loads, its errors, its position) and a timed log. Then comes the world at
the end: every thing with path frames or a changed position, health or flags; the surfaces and sectors that changed;
the inventory and goals; and whether the level ended. Log entries are:

- `message` — a handler ran;
- `verb` — a simulated verb with a visible effect;
- `print` — `Print`, `PrintInt` and the like;
- `arrived` — a thing reached its frame;
- `unsimulated` — a verb RED doesn't simulate;
- `error` — a runaway loop, a jump outside the code, or a stack problem.

## The machine

The virtual machine follows the retail engine (OpenJKDF2's `sithCogExec`; see `THIRD_PARTY_NOTICES.md`), quirks
included:

- the stack holds 64 values, and the oldest is dropped when it overflows;
- arithmetic always gives a flex, and dividing by zero gives 0;
- `!=`, `&&`, `||` and the bit operators work on ints, so `0.5 != 0.7` is false;
- popping an empty stack reads -1 as an int and 0 as a flex, which is why `for (a; ; c)` never ends;
- `call` goes four deep, and a fifth nested call is skipped;
- a message arriving while a script sleeps runs its handler, then the sleep carries on;
- handlers are found by message id, so a handler named `activate` runs for `activated`, and the first handler wins;
- `Sleep` is at least 0.1 seconds.

Scripts sleep, set timers (`SetTimer`, `SetTimerEx`, `KillTimerEx`), pulse (`SetPulse`), wait for things to stop
(`WaitForStop`), and send each other messages (`SendMessage`, `SendMessageEx`, `ReturnEx`).

## The world

The world is a stand-in, not the engine:

- **Things** have a position, a sector, health, flags, light and user data. A thing with path frames moves along
  them at the speed a script asks (`MoveToFrame`, `SkipToFrame`, `JumpToFrame`), passing the frames in between, and
  sends `arrived` when it gets there.
- **Surfaces** have a cel, flags, adjoin flags, light and a centre.
- **Sectors** have light, flags, a tint, a colormap and whether their adjoins are shown.
- **The player** is the first thing whose template name contains "player", if any. It has inventory and goals, and
  `jkEndLevel` is recorded.

Over a hundred common verbs are simulated: script state and messages, printing, numbers and vectors, thing movement
and health, surfaces, sectors, sounds (each gets a channel), keyframes and surface animations (each gets a track or
handle), inventory, goals and difficulty (medium). Every other verb takes its arguments off the stack, leaves 0 if it
has a result, and is logged as `unsimulated`, so the script carries on.

## Checked

- The stock two-piece door (`00_door.cog`), placed on a level with two door halves, does what it does in the game:
  `startup` closes the sector's adjoins and lights it; activating either half opens the adjoins and moves both
  halves; a second press while they move is ignored; the timer closes them two seconds after they arrive; and the
  adjoins close when both are shut.
- The core tests cover the machine's quirks, doors, timers, pulses, sleeps interrupted by messages, `WaitForStop`,
  `SendMessageEx`, symbol arrays (`door0[i]`), placed-COG values and building the world from a level.

## Not covered

- Physics and collision: no gravity, no `blocked`, no `touched`, and a moving thing doesn't change sector.
- AI, weapons, the camera, music, force powers and multiplayer. Their verbs are logged as not simulated.
- Template class scripts (`cog=` on a template) and link masks: an event reaches every script linked to the object.
- Keyframe and surface animation run only as log entries.
- Infernal Machine scripts.
