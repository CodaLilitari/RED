# red-build: the declarative level builder (prototype)

`cellgen.py` builds Jedi Knight level geometry from a plan. A plan is a set of named 2D points, plus
convex cells laid over them.

- A cell is a stack of layers:
  - open air,
  - water with its own surface,
  - a bridge deck over a passage.
- Shared edges become adjoins automatically.
- Height differences become risers and headers.

It also generates:

- sky ceilings and water surfaces;
- world-aligned UVs and fitted decals (one painted panel, placed exactly once);
- strata bands and cap rock;
- baked vertex light: sun, sky dome and point lights, with ray-cast shadows through the sector graph.

It checks:

- convexity;
- planarity, triangulating non-planar cells;
- adjoin mirrors;
- sealed seams (an edge lying along another cell's edge without sharing its points).

It was written for Kell Riders: Canyon Reworked (238 sectors, 1,028 visible surfaces). That level loads
in OpenJKDF2, loads in retail JK 1.01 in both renderers, and passed the ridden tours.

## Next: `red-agent build`

Port this to C++ in tools/sed-agent as `red-agent build <plan.json> <new-output.jkl>`, using the same
plan model as JSON, then let `apply` take plan patches. Agents would describe rooms, not vertices. That
is the point of RED.

## Findings from using RED on the canyon (to fix)

1. **`overview` framing** fills only about half the frame on an elongated level. The orthographic height
   seems halved.
2. **`snapshot` with `cullBackFaces: true`** drops the visible side of JK surfaces. Retail normals point
   into the sector, so the renderer's culling test is inverted.
3. **`snapshot` misses level-colormap materials.** MATs drawn for a level CMP (15maw.cmp, cliffs from
   levels 14 to 16) come back missing and render grey. Resolve the palette from the level's COLORMAP.
4. **`validate` passed a level whose east-wash mouth was sealed.** The seam lay on another cell's edge
   but did not share its points (cellgen's seam check now catches it). Add the same check to
   `validate`.
