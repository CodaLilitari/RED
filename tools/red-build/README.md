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

## Findings from using RED on the canyon

Fixed (the canyon's overviews now render whole, textured, and framed):

1. **`snapshot` drew portals.** An adjoin surface with no material is a portal: the engine draws nothing
   there. The software renderer drew each one as a flat grey quad and counted it as a missing material,
   so every view of a real level was walled off. It now skips them (`portalsSkipped` in the receipt).
   Test: a portal over a floor leaves the image unchanged.
2. **`overview` framing.** Its embedded `snapshotRequest` put the projection, the lens and the clip planes
   inside `camera` under names `snapshot` does not read, so a replayed request rendered a different camera
   (perspective, default lens: the level filled part of the frame). The request now follows the snapshot
   contract. Test: the embedded request, replayed through `snapshot`, renders the same image bytes as
   `overview` itself.

Withdrawn (misread before the portal fix):

- `cullBackFaces` is not inverted: JK normals point into the sector and the test keeps faces that point
  at the camera. The "missing" level-colormap materials were portals; with the level's CMP in the
  archives, 15maw.cmp cliffs resolve.

Resolved in RED:

- **`validate` now rejects sealed T-junctions between sectors.** A solid surface edge that collinearly
  overlaps another sector's coplanar edge without matching endpoints produces a `sealed-seam` error naming both
  surfaces. Exact matching sealed walls and non-coplanar partial edge overlaps remain valid for this check. The
  regressions use two synthetic box rooms, split only one side of their shared wall, and angle a separate copy.
- The two `overview` CLI checks (unknown view and margin above 0.25) and the core `vertex-colors` check
  pass on the current source without further changes.
