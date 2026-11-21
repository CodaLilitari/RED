# Workbench

**Tools > Workbench** exposes the portable query, UV projection, and placement
services through ordinary controls. Each creation or UV command prepares its
entire result before the application applies one document Undo transaction.
Refused preparation or stale application leaves the document unchanged.

Querying remains available in Game Preview, but projection and array creation
require Editor mode. At application time the dialog rechecks its captured
document, project and resource context and the exact prepared targets. UV edits
refuse hidden or locked source-sector layers; Thing arrays refuse hidden or
locked destination or containing-sector layers; light arrays refuse hidden or
locked destination layers. A changed context or protected target leaves authored
data, Undo history and the saved marker unchanged. The refusal text points to
Preview, layer protection and context changes without asserting which one was
observed; return to Editor, check layers and reopen Workbench when needed.

## Query

Choose Things, Sectors, Lights, or Surfaces in one explicit sector. Results use
the original collection indices, with surface indices local to that sector.
The total is the number of matching records, independently of the current page.
Set 1–200 rows per page and use Previous/Next to traverse all matching indices.
Changing any query input invalidates the displayed result and page cursor; Run
query starts again at the first match.

Check each exact predicate to enable it. Enabled fields combine with AND. Name
and template comparisons are ASCII case-insensitive and preserve significant
whitespace; Thing substring search combines with those exact predicates. Each
collection enables only its applicable fields:

| Collection | Exact predicates |
|---|---|
| Things | Name, template, layer |
| Sectors and Lights | Layer; flags any/all/none |
| Surfaces | Material index, layer; surface, face and adjoin flags any/all/none |

Flag masks accept decimal or explicitly `0x`-prefixed hexadecimal unsigned
32-bit values. A leading zero alone does not select octal. Any zero matches
nothing; all zero and none zero impose no condition. Invalid input displays a
refusal and clears old results. An unmatched valid query is an empty success.
This view lists indices; it does not assert resource closure or gameplay state.

## Saved count requirements

Keep the counts a map needs in a separate JSON file. For example, this request checks for one relay on layer 2,
no Things with the placeholder template, and at least three sentries:

```json
{
  "schemaVersion": 1,
  "checks": [
    {"name": "Relay beacon", "query": {"collection": "things", "filter": {"name": "relay", "layer": 2}}, "expect": {"exactly": 1}},
    {"name": "Remove placeholders", "query": {"collection": "things", "filter": {"templateName": "placeholder"}}, "expect": {"max": 0}},
    {"name": "Sentries", "query": {"collection": "things", "filter": {"templateName": "sentry"}}, "expect": {"min": 3}}
  ]
}
```

Run `red-agent map-requirements mission.sed checks.json` after each revision. Use `-` for a request on stdin.
On MCP, call `sed_map_requirements` with `path` and the same object in `request`. Both accept ordinary level inputs
and count the authored records the ordinary level importer exposes from that format. `red-agent schema map-requirements` describes the request.

Each check uses one of the four Workbench collections above. For Things, `name`, `templateName` and `layer` go
inside `filter`, and optional `search` goes directly in `query`. Use `collection: "surfaces"` with an explicit
zero-based `sector`. Surface `layer` matches its owning sector. Masks in JSON use numeric unsigned 32-bit
values under `any`, `all` and `none`. Omit `filter` to count every row. Each expectation takes either
`exactly` or an inclusive `min`, `max` or both (0–2,147,483,647). The count always covers the complete match set.

The report binds the evaluated level bytes with `inputSha256` and returns `checks` in file order with each
`name`, `expect`, `actual` count and `passed` flag. A count mismatch still evaluates every check. Like
`validate`, a completed report exits 0. **CI should require both exit 0 and top-level `passed: true`.** A malformed
request, refused query, unreadable level or exceeded work budget returns an error without check rows. A count of
zero therefore requires a successfully evaluated query. These checks cover stored records. Playtesting establishes
whether actors spawn, a beacon activates, or a route can be traversed.

Use 1–64 checks per file. Their nonblank names must be unique by exact UTF-8 bytes, up to 128 bytes each.
The ordinary 1 MiB request limit applies. The optional `maxWorkUnits` (0–4,000,000, default 4,000,000) bounds work
across all checks. Each check charges one unit per candidate row, before filtering. A surface material predicate
also reserves one unit per material-table slot for every surface in that sector. This ceiling covers collection
scans, independently of level loading and text comparison cost.

## Placement arrays

Select a layout and review its positions before creation:

| Layout | Parameters and ordering |
|---|---|
| Linear | Origin, count, XYZ step |
| Grid | Origin, U/V counts and XYZ steps; U varies fastest |
| Radial | Origin, axis, radius, count, start and sweep angles; endpoint excluded |

The shared core refuses more than 256 outputs, duplicate positions, degenerate
axes and out-of-bound coordinates. The preview reports positions only; actual
creation also validates the destination level, layer and entity requirements.
Thing arrays expose template, generated-name prefix, fixed pitch/yaw/roll,
layer and optional explicit containing sector. Radial placement does not rotate
each Thing toward its center. Without an explicit sector every Thing must have
one unambiguous interior owner; pinning an overlapping sector does not permit
placing outside it or on its boundary. Existing placed COGs retain the shared
schema-aware append guard.

Light arrays expose RGB color, white intensity, range, flags and layer, and have
no containment requirement. For MotS, **RGB follows white** preserves inherited
intensity. Clear it to author an independent RGB intensity in `0..1000`; explicit
zero stays zero even when white intensity is nonzero. Non-MotS arrays keep this
control disabled. The agent's `placeLightArray` uses the same optional
`rgbIntensity`: omitted or `null` inherits, while a number creates a MotS override.
Native projects preserve the metadata; engine exports require the source-light
extension to retain that editor-only distinction. Every array is prepared completely before
its single Undo step; failures never append a prefix of the requested array.

## Batch UV projection

Select surfaces before opening the dialog. Planar projection offers the six
directed world axes and an explicit origin. Cylindrical and spherical projection
use the entered origin/center and axis/pole. All use the entered texels per unit.
The shared core validates geometry and bounds, preserves corner lighting, and
rechecks the prior geometry/UV state before applying one Undo transaction.
This is projection at a density, distinct from explicit texture-dimension Fit
or shared-edge Align operations in other tools.

## Implementation and verification

`src/core/include/sed/level/workbench.hpp` owns layout, query and prepared-edit
contracts. `WorkbenchDialog` translates widgets into those typed requests;
`MainWindow::showWorkbenchDialog` owns document transactions and refreshes.
Successful UV edits publish only their affected surface-UV cache ranges; Thing
arrays invalidate the Thing render cache and light arrays notify the viewport.
Each transaction records its dirty domain so Undo and Redo also refresh the
affected render state. Successful transactions update the document UI and
schedule the relevant Checks refresh.

The focused `sed_app_workbench_dialog` test exercises the real Qt controls,
pagination and filtered original indices, flag parsing, explicit light values,
radial/grid output and whole-array refusal. The MainWindow integration regression
drives the Tools action and real controls, then checks exact serialized Undo/Redo,
saved markers, selection, CPU render-cache publication, resource diagnostics and
the refusal paths. Hidden/offscreen CPU-cache tests do not certify native OpenGL
rendering or visual usability. Native visual usability and the complete configured
Release suite remain separate acceptance gates.
