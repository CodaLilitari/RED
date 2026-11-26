# Requests from the JK editing community

Ideas from the community's #editing discussion (17 November 2026) for what a modern JK editor should do, and how
they would fit RED. None of these is built yet.

| Request | What it would take in RED |
|---|---|
| **Terrain with optimized adjoins.** | RED builds heightmap terrain as a grid of sectors, or as one 3DO. Optimizing would merge neighbouring cells that are coplanar or nearly so into larger sectors. That leaves fewer sectors and adjoins for the renderer to walk, while keeping the walkable surface. |
| **Flowing water.** Mark surfaces or points as inflow and outflow, simulate, and watch channels fill and overflow. | A fill simulation over the sector graph: water spreads from inflows through adjoins, lower floors first, up to each sector's height, until it reaches an outflow or spills. The result would be water levels, so the editor could set the underwater sectors (Make Underwater already does one sector) and their water surfaces. |
| **Sector thrust flow.** Pick points that blow or suck air, and let a simulation set the thrust of the sectors around them, with 3DO blockers such as doors taken into account. | A flow solve over sectors and their adjoins: sources and sinks, with each sector's thrust set from the flow through it. Closed doors would block their adjoins; a door's sector could be marked open or shut for each run. JK and MotS levels don't store sector thrust (a COG sets it with `SectorThrust` at run time), so the output would be a generated script that sets each sector's thrust at startup. That stays retail-compatible, and the COG runtime can check it. |
| **Sculpted and painted terrain split into a 3DO per sector.** Shred has built this as a separate tool. | RED's heightmap terrain makes sectors or one 3DO. Splitting the model per sector, so each 3DO is drawn only when its sector is visible, would make large terrain cheaper; sculpting and painting would be new editor tools. |

The [community authoring priorities](product_roadmap.md#community-authoring-priorities) cover twelve additional
requests and authoring problems from the editing communities, beginning with opposing double doors and multi-face extrusion.
The [editor comparison](editor_comparison.md) covers GeoSith.
