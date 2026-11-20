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

- **Check shared triggers.** Council FFA has 13 player starts and zero placed NPCs. A slot walk can include an
  unoccupied start. It can also visit two live players in a different order from the sector list. A hurt volume
  that damages its first eligible occupant and then starts a shared cooldown can therefore hit a different player.
  Preserve the original selection rule whenever the first eligible occupant matters.
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
  affine across it, so check that before merging. Both the old triangles and the new fan interpolate corners that
  sit within the tolerance of the fitted plane, so a pixel can move by up to twice the tolerance.

  An earlier merge that only checked shared edges, material, normal and convexity changed about 6,200 faces.
  In those faces light left a plane by more than 1/255. In hundreds, the texture slid by up to 149 texels.
  The change was visible as a lost highlight.
- **Merge totals.**

  | Merge | Light tolerance | Texture tolerance | Faces |
  |---|---|---|---|
  | Strict | ½ of an 8-bit step | 1/32 texel | 185,329 → 140,492 |
  | Loose | 4/255 | ½ texel | 185,329 → 124,468 |

- **Faces mattered more than vertices.** Welding normals within 30° gave 226,770 vertices but no extra speed.

- **Remove straight corners only where every face agrees.** A merged face keeps corners in the middle of straight
  edges, where the face on the other side turns. Each costs a fan triangle. Coda removed 18,509 of them from C09,
  face by face, and measured about 4% more FPS in retail under Wine. About half sat where another face of the same
  model kept a corner at that position. Dropping a corner from one face while its neighbour keeps it leaves a
  T-junction, which can open single-pixel cracks. Removing a position only when every face there drops it removes
  8,481 corners from C09, 4.6% of the drawn triangles, and adds no T-junction. Removed corners also take their
  dynamic-light samples with them.

RED's JO/JA importer applies the weld and merge through the opt-in `meshOptimization` setting, in the import dialog
and the agent. Its `loose` level also removes straight corners by the every-face rule.

## OpenJKDF2 results

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
- **Sampled image differences.** In the saved comparison frames, at most 0.003% of pixels differed from the
  original by more than 16/255.
- **COG timings.** An earlier round combined a different geometry revision with script changes. Its average
  gain cannot be assigned to the final C09 scripts at every view.

## Original Windows retail results

The LucasArts 1.01 executable hosted the unchanged C09 through DirectPlay and ran stock Nar Shaddaa on Windows
with dgVoodoo 2.87.4. The game selected 640×480×16 with 3D acceleration, dgVoodoo rendered at 960×720, and
the JK process used the RTX 5070 Ti 3D engine. On the 60 Hz display, the game's own counter read 59.76–60.04 FPS
in the Council hall and 59.94–60.04 FPS in Nar Shaddaa. Earlier original-executable results around 15–16 FPS at
1280×960 used Wine and Mesa llvmpipe.

The following native tests used separate fixed-start FFA copies. The `-dispStats` game overlay reported the drawn
polygons. The hall visibility diagnostic used the original Academy BSP's potentially visible set (PVS) for that
single position. The two-sector build used normal JK adjoins that follow the moving player.

| View | Original C09 drawn polygons | Fixed hall PVS | Two native sectors |
|---|---:|---:|---:|
| Hall, facing away from the paired door | 14,221–14,234 | 8,055–8,058 | 14,207–14,216 |
| Facing the paired door | 7,727 | — | 7,732 |

All these views ran near 60 FPS. In one eight-second hall comparison, JK consumed 8.38 CPU-seconds with C09
and 3.98 with the fixed PVS. That is process CPU time across threads, including waits that spin. The fixed mask
belongs only at its sampled viewpoint. The playable two-sector build retained the paired door, and the original
game tracked the player from sector 1 to 0 and back to 1. This split provided no meaningful polygon saving at
either stationary view. Use the original PVS to identify architecture a proposed partition should reject, then
measure actual drawn work from both sides of the opening.

### Texture depth and the software renderer

The authored C09 has 331 opaque RGB565 MATs and 13 indexed cutouts. An 8-bit edition converted only the opaque
textures, retaining their mips and using the level's own CMP. Four runs used the same hall camera. Retail's
software display selected 640×480×8 and wrote an indexed PCX. Its accelerated mode selected 640×480×16 and
wrote an RGB screenshot. The GPU also presents the software-rendered image, so seeing a graphics driver loaded
does not identify the game's rasterizer.

| Material edition | Software, median of three FPS readings | Accelerated, median of three |
|---|---:|---:|
| Authored RGB565 with indexed cutouts | 48.76, colored streaks and corrupt textures | 60.04, clean original colors |
| All indexed, same geometry | 49.21, textures readable, rear console visible through ramp | 60.04, intact depth, some palette banding |

All 150 static world models have exactly one geoset. Switching render passes while holding their geometry,
textures and camera fixed restored the front ramp and hid the rear console. The flag involved is
`SITH_TF_LEVELGEO` (0x8). Clearing it moves a model from the level-geometry pass into the ordinary Thing pass,
which reads and writes depth. In the all-indexed hall, that change raised the software median from 49.21 to
57.65 FPS. The ordinary pass also uses distance-dependent shading and texture mapping. Test distant balconies,
dark surfaces and silhouettes in both renderers before choosing the setting for a level.

The fidelity-preserving treatment also sets `SITH_TF_IGNOREGOURAUDDISTANCE` (`0x04000000`) on those static
models. This retains their per-vertex shading at distance. It delivered **54.08 FPS in software**, about 9.9%
above the 49.21 FPS indexed baseline at this view. Against the original indexed hardware picture, the proportion
of world-crop pixels differing by more than 16/255 in any RGB channel fell from 15.12% to 7.08% in software.
On hardware the same measure was 0.0092%, compared with 2.73% for moving the models without preserving
Gouraud lighting. Some dark foreground shapes still differed in software. RED exposes the combined treatment
through the opt-in JK `worldModelRenderPass: ordinaryThing` import setting.

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

## Tried and not adopted

- **Tighter mesh bounds.** Shrinking 942 mesh radii without moving a vertex looked promising in one retail run.
  In the controlled forward-and-reverse comparison it was even with C09 or slightly slower.
- **Recentring mesh origins.** It would let more meshes fail the view test: an estimated 17% fewer vertices across
  the seven saved views. JK tests face collision against the raw mesh vertices in the Thing's frame, so a
  compensating node offset would leave a wall drawn in place while its collision moved, by up to 4.91 JKU here.
  Origins also decide sphere-only hit distances and which dynamic lights a model picks up. Not built.
- **Regrouping faces into tighter meshes.** Moving faces between the meshes of 71 models, without moving any
  corner, gave an estimated 2% fewer vertices in view. Its one retail run gained 2.5% to 11.6%, depending on which
  C09 control it is compared with, so the benefit is unproven.
- **Two native sectors at the paired door.** The split traversed correctly in retail, but drew almost the same
  number of polygons at both stationary views.

These are Coda's retail and source checks from 20 November 2026.

## What helps next

Useful sector boundaries let the renderer skip rooms that would otherwise survive its view checks. Count drawn
faces as well as sectors and measure the open doorway as well as the closed view. A source surface drawn on one
side of a wall can support a partition plane while leaving the reverse view open. Closing a shared portal needs
opaque material and solid coverage at that exact interface. RED's source visibility report can check emitted
meshes and native surfaces against a chosen Raven cluster. Lilitari's broader 32-sector Council proposal remains
a design estimate. [JO/JA import and visibility reports](../../jo_ja_import.md)

Credits: Lilivane (the initial conversion), Lily / Lilitari (the Candidate03 refinement and the retrofit research),
Red (analysis and candidates), Andrei (testing on his hardware), Coda Lilitari (the engine-capture and route-profile
tools).
