# Audit a level's progression rules

Run `red-agent query level.sed progression.json`, or call MCP `sed_query` with the workspace-relative
`path` and the same `request` object. The query reads a saved level. Use it to ask whether a player
with specified abilities can reach a goal, whether a successful route can lead back to a safe sector,
and whether a different choice can strand the player before the goal.

## A key behind its own door

Suppose sector 0 has only one exit, its surface 3 leads to sector 1, and the only red key is in
sector 1. With the following request, an actor starting without the key cannot reach sector 1.
The starting state itself is a softlock witness. Move the grant to sector 0 and repeat to see
whether the same saved topology admits the intended route.

```json
{
  "schemaVersion": 1,
  "collection": "progressionRoutes",
  "startSector": 0,
  "targetSector": 1,
  "abilities": ["redKey"],
  "grants": [{"sector": 1, "provides": ["redKey"]}],
  "gates": [{"portal": {"sector": 0, "surface": 3}, "requires": ["redKey"]}]
}
```

Replace the example addresses with zero-based sector and local surface indices from your level.
Include its `inputSha256` or `sourceRevision` from `inspect` if those addresses came from a previous
inspection. The receipt returns both saved-level identities. Its separate `requestSha256` binds
the complete parsed request serialized by RED, including field values, list order and any revision
guards. Changing only a grant produces a different request identity for the same level bytes.

## Declare what the actor can do

| Request field | Meaning |
|---|---|
| `startSector`, `targetSector` | Required indices in this level. Success needs the target sector and every `requiredAbilities` entry. |
| `returnSector` | Desired destination after success. Defaults to the start sector. |
| `abilities` | Required array of 0–12 unique, nonblank UTF-8 names, each at most 64 bytes. Names compare exactly, including case. |
| `initialAbilities`, `requiredAbilities` | Optional arrays of distinct declared names. Both default to empty. |
| `grants` | Up to 128 entries. Each has a `sector`, optional `requires` and nonempty `provides`. Both lists contain distinct declared names. |
| `gates` | Up to 256 entries. Each has a `portal` endpoint and a nonempty `requires` list of distinct declared names. Naming the same pair twice, even by its other face, refuses the request. |
| `excludePortals` | Up to 256 endpoints permanently close their reciprocal pairs. Either face names the pair. |
| `policy` | Default `movementBit` follows a face when its departing adjoin flags include `0x2`. `paired` follows both directions of every reciprocal pair. |
| `maxStates`, `maxWorkUnits` | Search limits from 1 to 65,536 distinct states and 1 to 2,000,000 outgoing arc probes respectively. Defaults are the maxima. |

Every portal endpoint has exactly `sector` and `surface`. The graph uses the same reciprocal pairs
as [portalRoutes](agent_design.md#portal-route-report), with the shared limits of 4,096 sectors and
262,144 surfaces. Invalid references retained by the loader refuse the audit, including references
in disconnected sectors. Use `portalRoutes.invalidReferences` to inspect them. Earlier import may
already have removed a malformed on-disk reference. Unsupported fields, invalid indices and undeclared
ability names also refuse. Omitted grant, gate and exclusion lists are empty.

Each state stores both the sector and the abilities held on that particular route. On arrival,
including at the start, all eligible grants fire repeatedly until no additional ability is gained.
All requirements on a gate must be held before crossing from either face. Abilities persist.
The model supports a permanently latched switch. Consumable keys, a switch that toggles back,
time-dependent triggers, death, save/reload and scripted teleports require different evidence.

| Design question | Model and result to inspect |
|---|---|
| Can a player choose a one-way pit before finishing? | Give the pit an incoming movement direction with no way back. A reachable goal and a reachable pit can yield both `targetReachable: true` and `possibleSoftlock: true`. Follow `softlockPath`. |
| Can two mutually exclusive key branches unlock a two-key exit? | Put one grant on each irreversible branch and require both keys at the gate. A key acquired on one route cannot combine with a key acquired only on the other. |
| Can the companion operate the switch? | Declare `canUseSwitch` and `doorOpen`. At the switch sector grant `doorOpen` only when `canUseSwitch` is held. Gate the door on `doorOpen`. Run once with the player's initial abilities and again with the NPC's. The difference measures your stated capability assumption. Actual NPC navigation and switch activation need observation. |

## Read the result

- `targetReachable` answers whether **some** route reaches a satisfying goal state. `targetPath`
  gives a witness when true.
- `returnPossible` asks whether **some** satisfying state can then reach `returnSector`.
  `roundTripPath` includes the route from the start through the goal and on to the return sector.
  A one-way completion can have `targetReachable: true` and `returnPossible: false`. Without a
  discovered satisfying state, `returnPossible` is null.
- `possibleSoftlock` asks whether a state can be reached **before passing a satisfying goal** from
  which success is impossible. `softlockPath` shows how to reach such a state. It may consist solely
  of the start. Post-goal excursions do not count. Returnability and softlock are separate questions.
- `stateCount` counts discovered sector/ability combinations. `workUnits` counts attempted outgoing
  arcs, including gated or excluded arcs. `transitionCount` counts admitted transitions. When
  `complete` is false, `stoppedBy` identifies `stateBudget` or `workBudget`. Positive target and return
  witnesses survive exhaustion. Unproved answers, including the softlock verdict, are null. A complete
  search has a null `stoppedBy`.
- Each non-null path has a total `stepCount` and up to 128 ordered `steps`. A path's own `complete`
  flag tells you whether you can see its entire witness, independently of search completeness. Each
  step records its `sector` and all `abilities` held after arrival, in declaration order. The first
  step's `via` is null. Later steps carry `from` and `to` face addresses and their unsigned `fromFlags`
  and `toFlags`. The separate `endState` always identifies the terminal state and its incoming hop,
  including a trap beyond the displayed prefix of a long path.

The scope is `declaredMonotonePortalModel`. `unmodeledPlacedCogs` counts placed COG rows in the saved
level. The audit does not execute their code or infer grant and gate rules from COGs, Things or
templates. `scriptsEvaluated`, `geometryVerified` and `runtimeVerified` are false. Collision volumes,
jumps and timed events can change what happens in play. Campaign progression and whether a COG
eventually finishes also need separate checks.

Test the critical route on the intended packaged episode and executable. In an ordinary gameplay
recipe, `wait` checks sector arrival within a deadline and `assert` checks native state at a step.
Capture runners supporting the `observe` step can also check a sampled height or sector interval
for a fall or detour. Choose a state actually reported by the engine. A screenshot or a Use key
alone cannot establish a door's internal state. [Engine gameplay recipe and evidence](../tools/engine-capture/README.md#ordinary-gameplay-capture)
