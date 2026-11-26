# Walkway clearance

Use this check after adding an observatory, railing, or other scenery beside a flat route. It reads the actual exported JKL and collision models, then marks a sampled path between selected starts and pickups. A separate option checks whether the connected walkway surrounds an opening.

## Run a check

Export the playable JKL from RED, or extract the JKL and its 3DOs from the built episode. Use the **Thing instance names** shown in RED. Choose the sector and floor elevation, then take the player's collision radius and desired body height from the target setup. All distances are in Jedi Knight units (JKU).

The script's help works with standard Python 3. The analysis uses NumPy, SciPy and Pillow. In the Python environment you use for RED tools, install any missing packages with `python -m pip install numpy scipy Pillow`.

```sh
python tools/engine-capture/walkway_clearance.py level.jkl --models extracted/3do --sector 12 --floor-z 1.5 --height .19 --radius .065 --anchor weststart --anchor eastshield --out review/upper-gallery
```

Use the names and sector from your own level. Repeat `--anchor` for up to 64 named destinations in the same sector. Use `--around X Y` when players must be able to circle a pit or an atrium. Select a point inside the plan that has no walkable floor. A U-shaped path can connect both destinations and still fail the enclosure check. `--step` controls the spacing of the samples, and `--foot-offset` sets the lower end of the vertical slice; the defaults are 0.005 and 0.002 JKU.

## Read the plan

### Checking a step between sectors

For an existing route through adjoining sectors, give the sector indices in walking order and choose
the actor radius, height and maximum step rise in JKU. This mode uses the Python standard library.

```sh
python3 -B tools/engine-capture/walkway_clearance.py route gallery.jkl --sectors 3 5 7 --radius .065 --height .13 --max-rise .035
```

Each transition lists both floor heights, the sill position, available headroom, portal width and
source surface. `firstBlocked` is the zero-based index of the earliest failing transition. Its status
distinguishes a high riser, low lintel, narrow opening or movement-blocking adjoin. It measures
the midpoint of one reciprocal upright rectangular opening between horizontal floors at each crossing.
The original planar check covers obstacles along the rest of a walkway. Sloped treads and multiple
openings between the same two sectors require a separate review.

The [two-room fixture generator](../tools/engine-capture/fixtures/clearance-stair/make_portal_trial.py)
builds three playable JK episodes. On the capture-enabled Windows OpenJKDF2 engine
(`72170044bcd9`), four seconds of normal forward movement carried the .065-radius actor from
sector 0 into sector 1 over a .005-JKU step. A .25-JKU riser and a .070-JKU clear opening each
stopped it about .065 JKU before the portal plane. The static report identified the riser and
lintel separately. These runs used the fixture actor and JK physics profile.

Open `overlay.png`. Numbered white points follow the order of the anchors on the command line. Teal marks the component reachable from the first anchor at the requested radius. Gray marks sampled floor with too little room for the player. Purple marks other components with sufficient room. The gold lines trace sample paths between the anchors, and the red circle marks the narrowest sample on those illustrated paths. An optional cyan ring marks the point selected with `--around`.

`report.json` includes the JKL and model hashes, named anchors, obstacle instances, projected sampling resolution and coordinates of that red circle. `sampledConnected` describes the anchors. `surroundsAtRadius` describes the optional ring. `passes` combines the requested conditions. `widestSampledRadius` is the largest tested radius satisfying those conditions to within one quarter of one grid step. `anchorPathPinch` belongs to the illustrated paths; the optional loop may have a narrower point elsewhere. Exit code 0 means the requested conditions passed, 1 means the analysis ran and found a blocked route or open ring, and 2 means the tool could not evaluate the inputs. Use a new output directory for each run.

## Scope of the measurement

- The selected floor consists of flat, floor-flagged JKL surfaces at the chosen height. The tool clips solid sector surfaces and adjoins that block player movement to the body slab. It includes low ceilings and internal walls. The selected sector's footprint bounds the analysis.
- Collidable spheres use the larger of authored size and movement size. Mesh obstacles use faces from native 3DOs with a single GEOSET, identity hierarchy transforms, and placement by translation and yaw. Projected closed outlines within an obstacle are filled conservatively. Model hash lookup uses case-insensitive native names. Unsupported collision shapes, tilted models, transformed model hierarchies and missing assets stop the check.
- The tool refuses physics-driven scenery and authored path frames. Some permanent architectural Things are tagged `move=path` without any waypoints. After checking their intended placement and scripts, pass `--stationary-paths` to evaluate those frameless models in their stored poses. Their names appear under `stationaryPathPoses`. Players, pickups and other transient actors are listed separately and excluded as obstacles.
- Four-neighbour connectivity rules out corner-only movement. Boundary-to-centre distances include a two-diagonal-sample margin for rounding. Shapes smaller than the grid resolution, solid geometry belonging to adjacent sectors, sloped surfaces, dynamic COGs, jumping and step-up physics require separate inspection. Test a close call at finer resolution, then walk through the marked location in the chosen game and renderer.
