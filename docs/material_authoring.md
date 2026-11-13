# Image-to-MAT authoring

**Tools > Material Authoring** creates a new opaque 8-bit indexed MAT from 1–16 PNG or baseline/extended-sequential
JPEG images. The portable `buildIndexedMat` implementation also serves `red-agent mat-create`.

## Designer workflow

1. Add image paths or use **Browse images**. Move cels up/down to set animation order; Remove deletes a list entry.
2. Use the captured project CMP or browse to an explicit CMP palette.
3. Optionally resize every cel to a common width and height, each a power of two from 8 through 1024. Without resize,
   sources must already have compatible MAT dimensions.
4. Select 1–4 mipmaps, dithering, the inclusive allowed palette-index range and whether to reserve index zero.
5. Choose **Preview conversion**. Select a cel to compare decoded source pixels with independently decoded MAT output.
   The report shows actual dimensions, cel/mip counts, colours used, quantization error and source/CMP hashes.
6. Choose a new `.mat` path and **Create new MAT**. Add it to the desired resource root/archive and assign it with the
   existing material picker when ready.

Small dimensions can reduce the actual mip count; the report shows what was written. An empty allowed palette range
refuses conversion. Input alpha is ignored: reserving index zero does not add transparency. True-colour MAT writing
is not supported by this workflow.

## Publication and cancellation

Conversion runs on immutable captured bytes in the bounded preview worker pool. The dialog checks source hashes
again before staging and after writing the temporary file. Document/resource context and option generation must
still match before publishing. These checks detect observed changes; they are not filesystem locks. The written
content is always the previewed snapshot.

Creation uses the shared create-only atomic writer. Existing or concurrently created outputs are refused. It does
not change the open level, template assignments or resource roots. Editing sources/options invalidates the preview
and cancels pending publication. Changing only the output path preserves a running conversion but cancels a pending
write to the old path; press Create again after it finishes. Cancel/close prevents subsequent publication. A running
core decode or quantization call completes its current stage before cancellation is observed.

## Input limits

The GUI admits up to 32 MiB per source image, 128 MiB aggregate image bytes, 16,777,216 aggregate source pixels,
8192 pixels per source side and a 1 MiB CMP. PNG must be noninterlaced with a complete 13-byte IHDR as its first
chunk. Header admission precedes the authoritative CRC/chunk/pixel decoder. Unsupported JPEG variants, including
progressive JPEG, retain decoder diagnostics. Limits describe this authoring operation, not engine maxima.

## Implementation and checks

[`MaterialAuthoringDialog`](../src/app/src/ui/resources/material_authoring_dialog.hpp) owns widgets and asynchronous
orchestration. [`mat_write.hpp`](../src/core/include/sed/assets/mat_write.hpp) owns portable conversion. The proposed
bytes are read through `MatFile` and decoded again before Create is enabled.

`sed_app_material_authoring` covers ordinary controls, ordered cels, decoding, bounds, source drift, stale context,
queued cancellation, owner destruction, racing output creation and temporary cleanup. Offscreen tests establish
widget and file behavior; native layout/input checks and runtime appearance remain separate release evidence.
