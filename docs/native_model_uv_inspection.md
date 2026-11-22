# Model Studio UV inspection

## Intent and scope

Extend the numeric Model Studio with a read-only, material-backed 2D view of its existing UV edits. A mapper
must be able to see which source face corners reference the selected texture-vertex row, inspect the proposed
coordinates, cancel or undo exactly, and publish through the existing reviewed variant path. This design does
not change the native document, operation schemas, source-preserving writer, model history or target-game claims.

The approved numerical workflow and the user's instruction to continue establish the scope. The choices below
are implementation decisions: a 2D panel, explicit material capture, cel zero/mip zero, and a bounded display.
The design continues [Model Studio](native_model_studio.md) and its [qualification plan](native_model_studio_plan.md).

## Approach

A material-backed UV panel uses raw source LOD/mesh/face/corner identities and the currently displayed immutable
document. It shares the existing numerical editing controls. Reusing the level UV editor would couple native
model addresses to Level surfaces and its history. A complete textured 3D model renderer would add lighting,
camera and runtime appearance contracts that are unnecessary for inspecting this edit. The 2D panel is the
bounded first implementation; the level UV diagnostic ruler remains an independent feature.

## Display contract

- Select a material slot used by the selected source mesh. Display both slot ordinal and authored name. A row
  can be used by several materials with different dimensions; report the selected material's corners and the
  total uses separately. Never merge UV rows merely because their coordinates or geometric positions match.
- U increases right and V increases down, in native texture pixels. Show explicit axes. Show the origin material
  tile once, with its real aspect ratio; coordinates outside it remain unwrapped. This is an unlit cel-zero,
  mip-zero material swatch, not an engine image or a light-table simulation.
- Draw face UV outlines for the chosen material and highlight exactly the corners referencing the selected row.
  Give each highlighted corner its source face and corner ordinals in an inspectable readout. A coincident seam
  using another UV row stays unselected. An unused selected UV row remains inspectable as an isolated point.
- Keep the frame fixed for changes to the displayed coordinates on the same model/LOD/mesh/material target.
  Explicit Reframe fits the origin tile, valid face UVs and selected unused row. A new target or newly captured
  material may reframe. Large finite coordinates must never overflow painter coordinates or imply normalized UVs.
- Bound overlay work to 262,144 visited face corners. An over-budget model shows an explicit unavailable overlay;
  it must not silently show a partial inspection. Clip drawn segments to the viewport before converting to Qt
  painter coordinates. Do not allocate or draw one background tile per UV repetition.
- Preview, Cancel, Apply, Undo and Redo all display the same immutable snapshot used by Studio. Editing text
  revokes an older preview as it does today. The panel adds no authored state and no Undo item.

## Material capture and decoding

Choose the selected slot's project resource or an explicit loose MAT for inspection. A loose override is visibly
identified and does not rewrite the model's material name. Indexed swatches use an explicitly selected CMP or
the captured project master CMP. No guessed palette is presented as the material's actual colors.

Capture at most 16 MiB of MAT bytes and 1 MiB of CMP bytes. Compute identities and SHA-256 from those same captured
bytes. Resolve project resources through the captured ResourceLocator, including its current winning origin.
Keep at most one decoded material snapshot; source and selection changes invalidate stale asynchronous results.
Decode in the existing bounded preview worker pool, with no QWidget or QPixmap accessed off the GUI thread.
No material I/O or decoding occurs in a paint event or on every numerical UV preview.

The new portable `MatFile::inspectPreviewCel(span<const byte>, size_t maxPixels)` extends the existing strict
cel-zero snapshot walk. The default maximum is 4,194,304 selected pixels. Its result `MatPreviewCel` distinguishes
`SolidIndexed`, `Indexed` and `TrueColor`; includes source `width`/`height` (zero for a solid color), the solid
palette index, selected-texture key convention, raw indexed texels or RGBA8 texels. It follows cel zero's texture
selector, validates all declared cels/textures/mips, and decodes only selected mip zero after checking its budget.
The ordinary `MatFile::parse` behavior and the indexed software snapshot contract remain unchanged.

The unlit indexed swatch uses direct captured CMP palette entries, without light-table translation or tint.
Its color-key display follows the selected texture's key flag or the MAT alpha marker, never another cel's key.
Solid colors remain opaque, including index zero, and supply no invented texture dimensions. Truecolor uses the
existing supported packed-channel decoder and its stored alpha. Missing CMP retains indexed texture dimensions
and the UV overlay, but clearly marks colors unavailable. Missing, malformed or oversized MATs do not supply
dimensions; numerical authoring remains available. A failed capture must not display a previous material as new.

## Lifecycle and ownership

The panel owns immutable documents and decoded image snapshots. ModelStudioDialog passes the displayed model,
selection and a source-generation identity; source replacement clears captured materials. Material capture is
independent of the session mutation worker and has one active request with generation-checked completion.
Changes to material/CMP inputs, target or source invalidate pending work. Closing the dialog prevents publication
of a late image. The project-context callback is invoked only on the GUI thread and a changed context is visible.
Inspection never modifies Level, source files, resources, model history or published variants.

## Acceptance

Use an owned nonsquare material with distinct corner colors and a two-LOD model. Two faces share one UV row;
a coincident geometric seam uses a separate UV row. Verify highlighted face/corner addresses, material-specific
dimensions, U/V orientation, negative/tiled values, an unused row, and stable framing through Preview/Cancel/
Apply/Undo/Redo. Publish twice to different destinations and independently reopen each result; preserve original
bytes and unrelated model fields. Exercise missing/changed sources, missing CMP, multiple material slots,
asynchronous stale results, palette-only changes, malformed later mips and declared selectors, and budget refusal.

Core pixels must cover a 3x2 indexed texture, a nonzero first texture selector, multiple cels sharing one texture,
selected versus unselected keys, RGB565 primaries and solid color without dimensions. GUI checks inspect actual
pixels and source corner identities, including 200% display scale. Run the complete configured Release suite and
retain native Windows/Linux evidence. Edited-variant game rendering, animation and collision remain separate.
