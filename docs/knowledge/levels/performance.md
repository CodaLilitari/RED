# Frame rate in large one-sector levels

These are the lessons of the Sith Council V2 work (20 November 2026). Lilivane made the initial conversion and Lily
(Lilitari) refined it. This work started from her refinement, Candidate03, for OpenJKDF2. That build is:

- one sector with 590 Things;
- 150 static world models, holding 1,453 meshes, 185,329 triangles and 398,659 vertices;
- 272 COG instances.

Red measured it in OpenJKDF2 and made it about a fifth faster at 1920×1080 without a visible change. The figures
below come from that work. Read them as one level's evidence, not as engine constants.

## Where a frame goes

- **Models cost CPU per visible mesh.** For every mesh whose bounding sphere touches the view, the renderer:
  - transforms every vertex;
  - lights each vertex from nearby dynamic lights, for Gouraud meshes;
  - backface-culls, clips and submits each face.

  The cost grows with the faces and vertices in view, not with the triangles the GPU finally draws.
- **One sector culls nothing.** Sector visibility only removes Things in sectors the view doesn't reach through
  adjoins. With everything in one sector, every model in the field of view is drawn, including those behind walls.
- **High resolutions pay for that overdraw.** At 3840×2160 on an RTX 5070 Ti, the original level and the
  optimized ones took 23–34 ms a frame. The clean-up's gain there was small and inconsistent: a few percent in one
  round, 17% in another. At 1920×1080 and below the frame was CPU-bound and the clean-up mattered.
- **Model LOD doesn't help room-sized models.** The engine picks a geoset from the depth of the Thing's origin and
  the level's LOD distances. Council's median model radius is 3.7 JKU, so one origin can't speak for the whole
  model.

## COGs that scan every Thing

In a one-sector level, `FirstThingInSector(0)` visits every Thing in the level. Council had 47 trigger COGs that
did this on every pulse: 24 hurt volumes at 20 Hz, plus sensors, lifts and a teleporter. That came to about
380,000 Thing visits a second, most of them on scenery. A pulse runs at most once per frame.

```text
pulse:
victim = FirstThingInSector(0);
while (victim >= 0) {
    next = NextThingInSector(victim);
    if (GetThingType(victim) == 10) { ...tests on the player... }
    victim = next;
}
return;
```

When only players matter, walk the player slots instead:

```text
pslot = 0;
pmax = GetMaxPlayers();
while (pslot < pmax) {
    victim = GetPlayerThing(pslot);
    pslot = pslot + 1;
    if (GetThingSector(victim) == 0) { ...the same tests... }
}
```

- **Why it tests the same Things.** Every placed player-type Thing becomes a player slot, up to 32, so the slots
  cover the Things the original loop's type test kept. The sector test restores the original's
  "in this sector" condition.
- **Use `GetMaxPlayers`, not `GetNumPlayers`.** `GetNumPlayers` counts active players, and active slots can have gaps.
- **Look for other work to drop.** Stop pulses whose fired actions only reset their own cooldowns. Have a lift
  skip the walk unless it is in a phase the walk can act on.
- **Check the result.** Run `red-agent cog-compile file.cog jk --strict` and the same with `mots`.

## Duplicate vertices and safe merges

- **Weld vertices whose normals barely differ.** Council had 398,659 vertices for about 151,000 distinct
  positions. Most duplicates differed only in normals that a per-face export left a fraction of a degree apart.
  Welding vertices that share a position, the 3DO light and the MotS ARCH row, and whose normals are within 0.5°,
  gave 326,631 vertices. Only dynamic light reads vertex normals.
- **Merge coplanar faces only where light and UVs stay linear.** The engine draws a polygon as a fan of triangles.
  A merged face draws like the triangles it replaced only if its per-corner light and texture coordinates are
  affine across it, so check that before merging.

  An earlier merge that only checked shared edges, material, normal and convexity changed about 6,200 faces.
  In those faces light left a plane by more than 1/255. In hundreds, the texture slid by up to 149 texels.
  The change was visible as a lost highlight.
- **Merge totals.**

  | Merge | Light tolerance | Texture tolerance | Faces |
  |---|---|---|---|
  | Strict | ½ of an 8-bit step | 1/32 texel | 185,329 → 140,492 |
  | Loose | 4/255 | ½ texel | 185,329 → 124,468 |

- **Faces mattered more than vertices.** Welding normals within 30° gave 226,770 vertices but no extra speed.

RED's JO/JA importer applies the same weld and merge through `meshOptimization`, a change on the
`red/importer-mesh-lessons` branch under review.

## Results

Each figure is the geometric mean over seven spawn views, in milliseconds per frame:

| Build | RTX 5070 Ti, 1920×1080 | Software GL, 640×480 |
|---|---:|---:|
| Before (Candidate 03) | 22.3 | 104.8 |
| Strict weld and merge, COG fix (Candidate 08) | 17.7 (−21%) | 84.3 (−20%) |
| Loose merge, COG fix (Candidate 09) | 16.6 (−26%) | 83.0 (−21%) |

- **How the RTX figures were taken.** Each view's figure is the faster of two runs, pinned to the performance
  cores, while other work ran on the PC.
- **How the software figures were taken.** These came from an otherwise idle cloud VM, with a median run-to-run
  spread of 6.6%.
- **No visible change.** Final frames matched the original to within 0.003% of pixels differing by more than 16/255.
- **The COG fix alone.** It was worth about 8 points: −14% for the merge alone, −22% with the COG fix, in an
  earlier round.

## Measuring

1. Use the OpenJKDF2 fork's hidden render probe on a staged copy of the game data:

   ```text
   openjkdf2-64.exe -renderprobe -autostart -sp -episode <gob> -map <level>.jkl -renderout <new absolute dir>
       -renderwidth 1920 -renderheight 1080 -renderframes 480 -renderdelayms 4000
   ```

   - It prints `RENDERPROBE_COMPLETE frames=N captures=3 elapsed_ms=E`. The frame time is (E − delay) / N.
   - Vsync is off and no window appears.
   - The run must finish within 120 seconds, load time included.
   - On Windows the process exited with code 5 after a completed measurement. The printed line is what counts.
2. Choose views by moving the single-player start in benchmark-only copies of the archive.
3. Interleave the candidates and rotate their order. Run each view at least twice, and use the median or the faster
   of the repeats.
4. On a hybrid CPU, pin the game to the performance cores at high priority, and keep other work off the machine.
5. Compare each candidate's captured frames with the baseline pixel by pixel.
6. Run the resolution you actually play at. A 4K result can hide a CPU-side gain.
7. For walking routes and frame-time distributions, use
   [route profiles](../../../tools/engine-capture/README.md#profile-a-stationary-view-and-an-ordinary-route).

## What helps next

Only culling helps at high resolution. Splitting a one-sector conversion into sectors with adjoins lets the engine
skip whole rooms. That is the purpose of the importer's
[hybrid sectors](../../jo_ja_import.md#agent-workflow) and of Lilitari's 32-sector retrofit plan for Council.
Patch sampling in the importer is still fixed per quadratic block.

Credits: Lilivane (the initial conversion), Lily / Lilitari (the Candidate03 refinement and the retrofit research),
Red (analysis and candidates), Andrei (testing on his hardware), Coda Lilitari (the engine-capture and route-profile
tools).
