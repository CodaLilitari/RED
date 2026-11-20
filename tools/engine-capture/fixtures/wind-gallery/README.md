# Wind Gallery

Build an editable, original conversion project and its controls in a new directory.

```sh
python -B make_project.py /tmp/wind-gallery --agent /path/to/red-agent
python -B verify.py /path/to/red-agent
python -B qualification_controls.py /tmp/wind-gallery/gallery.gob /tmp/gallery-controls
```

The generator sends an original Raven BSP through RED's ordinary import plan and apply commands. Two convex native sectors meet at a portal. The residual models include a curved patch and a striped grate. A separate invisible brush has player-clip and shot-clip filters. Inspect brush 0 in the source and Thing 0 in target sector 0 to compare the constraints. The accompanying section viewer can overlay the native exterior and portal, the placed model, player Thing 2 and the `@+windstone` projectile. The player uses a .065-unit movement sphere. The stock actor model is an external resource.

The authored JK project supplies a sky, color-key grate, palette, sector wind, player start, thrown object and placed COG. Ordinary forward movement crosses the portal and fires a gravity-driven stone. The script records the entrance with player health 98 and uses health 86 to identify a damage callback from that same live stone on the grate. The player stops at the grate, then returns through the portal. Run `scenario.json` with the reviewed or optimized episode, and `pass-through.json` with `gallery-pass-through.gob`. The latter changes the placed grate to `collide=0`; its draw mesh and native spatial geometry are identical.

The original recipe emits 36 triangles. Reducing patch subdivisions and chunk size emits 12 triangles. Reapplying the old opaque script and gameplay overrides produces named conflicts before an output is written. The example records the mapper's decision to bind the sensor to the unique new owner of source surface 2, reviews that baseline, and publishes `gallery-optimized.gob`. `mapping-decision.json` identifies both hashes, model names, triangle counts and source. The original and optimized plans and the conflict report stay in the generated project.

The compatible OpenJKDF2 build loaded both episodes, reported projectile contact, blocked the player at the grate and allowed reverse traversal. A native color-key control revealed the wall behind the open stripes. The sky and paired wind/silence controls also produced independent changes in the engine's RGB and stereo PCM outputs. Numeric observations, exact engine and package hashes, and control conditions are in [qualification.json](qualification.json).

Source sky and shader-driven alpha require the declared target replacements in this example. The hybrid importer rejects direct transparent MAT input. RED reports the original source filter flags; the mapper supplies the target collider. The sector wind is authored in JK. The example covers this JK target, collision shape, projectile and gameplay route. Raven shader animation, water behavior, per-actor source-filter equivalence, source contact audio, retail maps and network play each require separate evidence.
