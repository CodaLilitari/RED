# Place a switch on a 3DO panel

Put a small sphere-collision Thing just in front of the painted panel. The building keeps its original 3DO
and face collision. The tiny Thing receives the native Use event. The linked COG sees that button as
`GetSenderRef()` and the activating player as `GetSourceRef()`.

1. Place a stationary architecture Thing with a 3DO and `collide=3` in a JK or MotS sector.
2. Place the action COG. Give it an `activated` handler and leave one player-linked `thing` parameter at `-1`.
   For a working example, add [paneluse.cog](../tools/engine-capture/fixtures/panel-switch/paneluse.cog) to the project's
   `cog` folder, place it, bind `mover` to a Thing with closed/open frames, and leave `button` unset.
3. Select the architecture Thing and open **Thing > Assistants > 3DO Panel Switch**. Choose the collision mesh
   and face from physical geoset zero. The planar outline shows the face from the front. Its circle is the button's
   footprint. Move the center in JKU along the first face edge and perpendicular to that edge. Set the radius and
   select the free parameter on the desired placed COG.
4. Confirm the preview. One Undo removes the button and restores the COG parameter. Try using the panel in the
   target game. Aim at the neighboring plain wall from the same distance to check the usable area. The game sweeps
   the player's Use ray with its own width, so the exact activation extent can exceed the drawn button footprint.

The button is invisible so the 3DO supplies its appearance. RED adds or reuses the plain `red_panelbutton`
template, with sphere collision and the COG-linked flag, and writes the radius as the instance's `size` and
`movesize`. The sphere sits 0.001 JKU in front of the physical face at its nearest point. It must fit within
the face edges and the owner's sector. The supported radius range is 0.005 through 0.1 JKU. A moving building,
nonplanar or concave face, missing resource, occupied symbol, or conflicting template prevents placement.
Direct, typed references are validated across the placed COGs before the Thing is appended. An activated
handler can still have its own script conditions. Verify the action from both aim points in the engine.

For CLI or MCP, put the 3DO and the level's COGs in ordered, hash-bound `resourceArchives` for `plan` and
`apply`. World offsets follow the native collision face after applying the building Thing's pose. Visual model
node transforms and model insertion offsets do not move that physical face.

```json
{"op":"panelSwitch","owner":0,"mesh":0,"face":2,"cog":0,"symbol":"button","u":0,"v":-0.123,"radius":0.022}
```

[Generate the two-room sample and the neighboring-wall control](../tools/engine-capture/fixtures/panel-switch/make_project.py)
with `python make_project.py <new-directory> --agent <red-agent>`. Put one generated GOB at a time in the
episode folder of a local Jedi Knight test installation and start a new single-player game. Both episodes
include the original fixture player, wall, materials, and action COG.

The two generated episodes were tried in the original Windows disc executable with dgVoodoo. From the
marked panel start, pressing Space printed `PANEL sender=3 player=2`. From the neighboring plain wall
start, 0.24 JKU to the side at the same distance, two Space presses produced no activation message.
The built-in `coords` command showed the player move from Y −0.25 to −0.17 against that wall and stay
there during another 1.5 seconds of forward input. The front face lies at Y −0.11 and the fixture
player's movement radius is 0.065 JKU. These observations cover the supplied JK level and control.
