# Image-to-MAT authoring

**Tools > Material Authoring** creates a new opaque indexed 8-bit or RGB565 16-bit MAT from 1–16 PNG or
baseline/extended-sequential JPEG images. The portable `buildIndexedMat` and `buildRgb565Mat` implementations
also serve `red-agent mat-create` and the MCP `sed_mat_create` route. Indexed output remains the default.

## Designer workflow

1. Add image paths or use **Browse images**. Move cels up/down to set animation order; Remove deletes a list entry.
2. Choose the output format. For **Indexed 8-bit**, use the captured project CMP or browse to an explicit CMP
   palette. **RGB565 16-bit** needs no palette and disables the palette, dither and palette-index controls.
3. Optionally resize every cel to a common width and height, each a power of two from 8 through 1024. Without resize,
   sources must already have compatible MAT dimensions.
4. Select 1–4 mipmaps. Indexed output additionally offers dithering, the inclusive allowed palette-index range
   and whether to reserve index zero.
5. Choose **Preview conversion**. Select a cel to compare decoded source pixels with independently decoded MAT output.
   The report shows the stored format, actual dimensions, cel/mip counts, colours used, quantization error and
   source hashes. Indexed output also reports its captured CMP identity.
6. Choose a new `.mat` path and **Create new MAT**. Add it to the desired resource root/archive and assign it with the
   existing material picker when ready.

Small dimensions can reduce the actual mip count; the report shows what was written. An empty allowed palette range
refuses indexed conversion. Both formats ignore input alpha and write opaque textures; reserving index zero does
not add transparency. RGB565 stores red/blue with 5 bits and green with 6 bits, rounds to the nearest encoded value
(halves round up), and writes little-endian pixels with an explicit channel layout. Each mip averages the original
unquantized colours before independent quantization. Colours used counts distinct stored values across all cels
and mips; mean error compares source and decoded RGB channels at full size, ignoring alpha.

RGB565 output is independently decoded by RED before publication. The OpenJKDF2 GL renderer has an explicit RGB565
path, but generating and decoding a MAT does not qualify its appearance in a particular engine build. Verify in
the intended runtime. This workflow makes no retail JKDF2 or cross-game RGB565 compatibility claim. Other
true-colour encodings and alpha authoring are outside this writer's scope.

## Agent and MCP requests

`red-agent mat-create <request.json|-> <new-output.mat>` accepts `format: "indexed8"` (also the default) or
`format: "rgb565"`. Existing indexed requests and their receipt fields remain unchanged. Both formats use captured
image entries, optional `resize: [width, height]`, and `mipmaps` from 1 through 4. For example:

```json
{
  "schemaVersion": 1,
  "format": "rgb565",
  "cels": [{"path": "wall.png", "sha256": "<captured source SHA-256>"}],
  "resize": [256, 256],
  "mipmaps": 4
}
```

RGB565 refuses `palette`, `dither` and `reserveIndexZero` before reading source files, including when those options
are false or empty. It never opens a CMP. Its receipt adds `format: "rgb565"` and `bitsPerPixel: 16`, and omits
indexed-only `dither` and `paletteSha256`. Output bytes, output hash, source hashes, dimensions, cels, mipmaps,
colour counts and error remain explicit. Unknown formats refuse without publication.

For MCP, place the same request object in `sed_mat_create`'s `request`, choose a fresh workspace-relative
`output`, and optionally supply `operationId` for journaled replay. The transport uses the same portable command
implementation and captured-path policy; no external image converter or shell command is involved.

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

Core writer tests check literal RGB565 header/pixel bytes, quantization boundaries, decoded colours, opaque alpha,
mip averaging, ordered cels and size/count bounds. CLI and MCP tests cover palette-free publication, source hashes,
create-only output and indexed compatibility. `sed_app_material_authoring` covers ordinary format controls,
ordered cels, decoding, bounds, source drift, stale context, format changes during queued conversion/publication,
cancellation, owner destruction, racing output creation and temporary cleanup. Offscreen tests establish widget
and file behavior; native layout/input checks and runtime appearance remain separate release evidence.
