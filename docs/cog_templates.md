# COG template library

RED ships reusable controller scripts for vehicles, enemies and items in `data/cog_templates`, with the template
rows the KnightCop sets need. They are original work:

- the swoop, Kell and enemy scripts Lily (Lilitari) wrote for Andrei and KnightCop's level in November 2026, on top
  of the accepted swoop work of Lily, Coda and Andrei (the player swoop's boost mod is Red's);
- the vehicle and hazard scripts of Lily's Star Wars Demolition port (October 2026), which re-create Demolition's
  vehicles and arena hazards in original COG;
- space flight, the jetpack and enemy classes from the Shadows of the Empire port (SOTE DR1 R2, November 2026), by
  Coda, Lily, Andrei and Red.

All 43 scripts compile for JK with no errors or warnings. A set's notes give each placed script's values in order,
from hand-written notes or straight from its symbols. The Demolition and SOTE sets have no template rows: their
notes list the templates the scripts name, and the original levels' JKLs show complete templates and bindings.

RED ships no models, animations or sounds. Each set lists the files its templates and scripts name, so you know what
to supply: stock files come from the game, and the rest from the original kit or your own work.

| Set | Scripts | For |
|---|---|---|
| `swoop-player`: a swoop the player mounts and drives | `swp_fleet` (one per level), and per bike `swp_ride`, `swp_aux`, `swp_ram` | single player and multiplayer |
| `swoop-enemy-pursuit`: a mounted Tusken that wakes, follows an exit route and chases | `kcswoop` (one per bike) | single player |
| `kell-bowcaster-enemy`: a Grave Tusken riding a Kell | `kc_bow`, `kckbody` (template class COGs) | single player |
| `kell-rideable`: a Kell the player rides | `swp_fleet` (shared with the swoops), and per Kell `kc_geom`, `kc_mount` | single player |
| `demolition-vehicle`: the snowspeeder, speeders and walkers of the Demolition arenas | `dm_fleet` (one per level), and per vehicle `dm_ride`, `dm_move`, `dm_aux`, with `dm_anim` for gaits | single player and multiplayer |
| `demolition-arena-hazards`: ion cannon, AT-AT gun, wind fans, boost gates, bat hive, swamp creature, sarlacc, gun towers, junk engines, Atlas statues, Sith crushers | one script per hazard | single player and multiplayer |
| `sote-space-fighter`: free flight in space, with a cockpit view, instruments, twin lasers and homing missiles | `sg30_flight`, `sfp_view`, `sfi_state`, `sg30_missile`, and `sf_weapon` as the primary weapon's COG | single player |
| `sote-space-gunner`: the gunner's seat while a route flies the ship | `sg_flight`, `sg_missile`, `sfp_view`, and `sf_weapon` | single player |
| `sote-jetpack`: an inventory jetpack with fuel | `jp_item` (from items.dat), and per level `jp_level`, `jp_pickup`, `jp_body` | single player |
| `sote-enemies`: AT-ST, troopers, wampa, probe droid or turret, IG-88 | `sote_atst`, `sote_troop`, `sote_wampa`, `sote_droid`, `om_ig88` (template class COGs) | single player |

- **From the agent:** `red-agent cog-templates` lists the sets. `red-agent cog-templates <set>` gives the scripts,
  their roles, the placed-COG values in order, the template rows and the files to supply.
  `red-agent cog-template-write <set> <project>` copies a set into a project. Bind the placed COGs with `addCog`,
  or use the Pursuit assistant for enemy swoops.
- **In the editor:** **Tools > COG Templates…** shows each set and copies it into the level's project.
  **Thing > Assistants > Pursuit (enemy AI)…** places an enemy swoop's controller from the selection (see below).

A copy writes the scripts into the project's `cog/` folder, and the set's rows and notes into
`templates/<set>.tpl` and `templates/<set>.md`. Merge the rows into the project's `master.tpl`, parents before
children. A copy never overwrites a different file: if one is in the way, nothing is written.

## Using the sets

- **Player swoop.** Place `swp_classic`. Add one `swp_fleet.cog` for the level, then for each bike a `swp_ride.cog`
  (bike, fleet, its aux, protocol base 62000 then +100 per bike, keys and sounds), a `swp_aux.cog` (its ride, the
  bike, the geometry values, `swpovcl.3do`, its ram) and a `swp_ram.cog` (its aux). The aux and ram name each other.
- **Enemy swoop pursuit.** Place `kcswoop`, and `+kcsway` markers on walkable ground along its way out. Each bike
  gets its own `kcswoop.cog`, with these values:
  - **Waking:** `wakeDistance`, and an optional `wakeSector`.
  - **Chase:** speed, ram damage and fire interval.
  - **Route:** `exit0`–`exit3` name the markers in order.
  - **Rider:** `survivalChance`.

  The rider's template uses `kcs_tusk.cog`, a modified retail script that comes with the original kit, not RED.
- **Bowcaster Tusken on a Kell.** Place `kc_kbow`. Its templates bind both class COGs; there is nothing to place.
- **Rideable Kell.** Place `+kc_kell`. Add `kc_geom.cog`, and `kc_mount.cog` (the Kell, the shared fleet COG, its
  geometry COG). Keep one `swp_fleet.cog` even without a swoop.

- **SOTE space fighter.** Place the visual ship and a camera Thing. Add `sg30_flight.cog` (ship, camera, -1, 0,
  the view COG, the missile COG), `sfp_view.cog` (the flight COG, camera, ship, 1, the instruments COG) and,
  optionally, `sfi_state.cog` and `sg30_missile.cog`. Make `sf_weapon.cog` the primary weapon's COG in items.dat,
  or give your weapon COG the same protocol: while the master COG answers `user7(player)` with 1, send it
  `user0(player)` to fire, `user4` on release and `user1` for a missile. The missiles lock on only to Things your
  encounter or objective COG captures, with the templates their symbols name.
- **SOTE space gunner.** As the fighter, with `sg_flight.cog` and `sg_missile.cog`, and `sfp_view.cog` with kind 0.
  Write your own route COG: it moves the pilot and the ship, then sends the flight COG
  `user3(pilot, signature, route COG, mission)`, and the flight COG moves the view to match.
- **SOTE jetpack.** Add the two items.dat rows from the set's notes, one `jp_level.cog` per level, a `jp_pickup.cog`
  per pickup, and `jp_body.cog` if the player's model should show the pack (its flight bodies are local symbols).
- **SOTE enemies.** Put each script in its template's `cog=` field. The AT-ST ignores Force powers and creates
  `+soteatst_exp` when it dies; troopers drop `+dstrifle`.

Raise **World cogs** for every placed COG and every template `cog=` field, and start a fresh game after changing
templates or bindings.

## The Pursuit assistant

**Thing > Assistants > Pursuit (enemy AI)…**, or the agent's `pursuitAssistant{enemy, route?, wakeSector?, …}`,
places `kcswoop.cog` for one enemy. Select the enemy (a `kcswoop` Thing), its route markers (taken in number
order, up to four) and, optionally, a wake sector. The editor copies the `swoop-enemy-pursuit` set into the project
the first time.

The assistant refuses:

- a second controller for the same enemy;
- a marker listed twice, or the enemy as its own marker;
- settings out of range.

It warns when the enemy or a marker isn't the expected template. The COG runtime doesn't simulate AI, so **Try it**
only checks that the controller starts parked and runs without errors. The wake, route and chase need a test in the
game.

## What isn't in the library

- **SOTE's Hoth snowspeeder.** Its mission COG `cr_bh_mission.cog` needs 622 label slots and its wingman COG
  `crh_wing.cog` 570, more than JK 1.01's 512, so they overrun the engine's label table on retail JK. They wait for a
  split. The Demolition set has a snowspeeder that fits.
- **Mission scripts.** SOTE's route tables, encounters, radio and Info state machines carry the original game's
  mission data and only make sense in those levels.
- **Retail-derived scripts** (LucasArts' copyright header) stay out of RED, wherever they were found.
