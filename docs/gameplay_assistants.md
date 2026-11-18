# Gameplay assistants

RED has five assistants for the scripted parts of a level: doors, elevators, switches, objectives and cutscenes.
Each one takes its targets from the selection, writes the path frames and one placed COG in a single undo step, and
can be tried in the [COG runtime](cog_runtime.md) before anything changes.

- In the editor: **Thing > Assistants**. Select the targets first; the dialog lists what it took from the selection.
  **Try it** runs the result on a copy of the level and shows each check and the timeline.
- From the agent: the patch operations `doorAssistant`, `elevatorAssistant`, `switchAssistant`,
  `objectivesAssistant` and `cutsceneAssistant` (through `plan` and `apply`), then `assistant-check` to run the
  placed COG in the runtime. MCP: `sed_plan`/`sed_apply` and `sed_assistant_check`.

Doors, elevators and switches use stock scripts every JK install has, so nothing is added to the level's GOB. The
editor reads them from the project resources (add the game's `Res2.gob`), and the agent from the patch's
`resourceArchives`. Objectives and cutscenes have no generic stock script, so RED writes its own:
`red_goals.cog` and `red_cutscene.cog`. The editor saves them in the project's `cog` folder. From the agent,
`assistant-script <name> <new.cog>` (MCP `sed_assistant_script`) writes them. Both compile cleanly for JK and MotS.

## The five assistants

| Assistant | Select | Script | What it builds |
|---|---|---|---|
| Door | one to four Things | `00_door.cog` | Each piece gets two frames: where it is, and moved by the opening offset. Using any piece opens them all; they close after the wait. |
| Elevator | the lift (a Thing), and its button (a surface) or up to three adjoins | `00_elev_switch.cog` | The lift rises by the travel offset when its button is pressed or a call adjoin is crossed, waits at the top, and comes down. |
| Switch | the door's pieces, and one or two switch surfaces | `00_doorswitch.cog` | A locked door: using it plays the lock sound. A switch opens it, and shows cel 1 until it closes. |
| Objectives | sectors to reach and Things to use or kill (up to eight goals), and an adjoin as the exit | `red_goals.cog` | All goals are shown at startup. Each completes when its sector is entered, or its Thing is used or killed. Crossing the exit ends the level once every goal is done; without an exit, the last goal ends it. |
| Cutscene | the camera (a Thing with path frames) and, optionally, a sector | `red_cutscene.cog` | Entering the sector (or startup, without one) holds the player and moves the view along the camera's frames. After the hold, control returns. It plays once unless told otherwise. |

The settings are speed (JKU per second), how long a door stays open or a lift waits, the light in a door's sector,
the goal text base, and the camera's speed and hold.

Things that move must use a template with `move=path` and have no frames yet. An assistant gives them
`thingflags` 0x408 (part of the world, COG-linked) if their template doesn't already, as stock doors have.

## What the assistants refuse, and what they warn about

Refusals leave the level unchanged:

- a target that doesn't exist, or one listed twice;
- a Thing that doesn't move on a path, or already has frames;
- a call adjoin or exit that isn't an adjoin;
- a camera with fewer than two frames;
- a second objectives COG in the level;
- a script without the symbols the assistant fills;
- settings out of range.

Warnings are written to the log:

- `00_door.cog` and `00_doorswitch.cog` hide the door sector's adjoins while the door is shut. The door sector is
  the first piece's sector, so it should be a small sector that holds only the door. The assistant warns when it
  holds other Things, or when a piece is in another sector.
- Buttons and switches change cel, so their materials need two cels.
- Objective text goes in `cogstrings.uni` as `GOAL_<base>` onward. Edit it in **Episode > Goals**. Retail uses
  1000 for level 1, 2000 for level 2, and so on.
- Objectives are single-player.

## What Try it checks

| Assistant | Scenario |
|---|---|
| Door | The door's sector hides its adjoins at startup. Using the first piece opens every piece and shows the adjoins. The door closes by itself, and the adjoins are hidden again. |
| Elevator | The button, then the call adjoin, each send the lift up. The button shows cel 1 while the lift is away. The lift comes back down, and the button returns to cel 0. |
| Switch | Using the door does nothing. The switch opens every piece and shows cel 1. The door closes by itself. |
| Objectives | The goal text base is set. Every goal is shown. The exit does nothing until the goals are done. Each goal completes from its trigger. Then the exit, or the last goal, ends the level. |
| Cutscene | The trigger sends the camera to its last frame. The view follows the camera and the player is held. The view returns to the player, who can move again. Entering a second time doesn't play it again. |

The stock `00_door.cog`, `00_elev_switch.cog` and `00_doorswitch.cog` pass these checks in the runtime on a test
level. The repository's tests use stand-ins with the same symbols, since the stock scripts are LucasArts'.

The runtime is a stand-in for the game (see [the COG runtime](cog_runtime.md)): it has no collision, so a door
that a player blocks, or a lift that carries one, still needs a test in the game, in both renderers.
