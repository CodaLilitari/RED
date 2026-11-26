# Image-to-MAT authoring

**Tools > Material Authoring** creates an indexed 8-bit MAT (opaque or color-keyed) or an opaque RGB565 16-bit MAT from 1–16 PNG or
baseline/extended-sequential JPEG images. The portable `buildIndexedMat` and `buildRgb565Mat` implementations
also serve `red-agent mat-create` and the MCP `sed_mat_create` route. Indexed output remains the default.

## Designer workflow

1. Add image paths or use **Browse images**. Move cels up/down to set animation order; Remove deletes a list entry.
2. Choose the output format. For **Indexed 8-bit**, use the captured project CMP or browse to an explicit CMP
   palette. **RGB565 16-bit** needs no palette and disables the palette, dither and palette-index controls.
3. Optionally resize every cel to a common width and height, each a power of two from 8 through 1024. Without resize,
   sources must already have compatible MAT dimensions.
4. Select 1–4 mipmaps. Indexed output additionally offers dithering, the inclusive allowed palette-index range
   and whether to reserve index zero. For foliage, grilles or other cutouts, select **Use source alpha for cutout**.
   Enable **Left**, **Right**, **Top** or **Bottom** only where a repeating texture edge needs a transparent seam.
5. Choose **Preview conversion**. Select a cel to compare decoded source pixels with independently decoded MAT output.
   The report shows the stored format, actual dimensions, cel/mip counts, colours used, quantization error and
   source hashes. Indexed output also reports its captured CMP identity.
6. Choose a new `.mat` path and **Create new MAT**. Add it to the desired resource root/archive and assign it with the
   existing material picker when ready.

Small dimensions can reduce the actual mip count; the report shows what was written. An empty allowed palette range
refuses indexed conversion. Opaque output ignores input alpha. Reserving index zero only restricts the palette.
For indexed cutouts the writer enables the texture color key on each cel. Source alpha below 128 becomes index zero.
Visible colors use the remaining permitted indices, even when the source RGB equals palette entry zero. Resize and
mip filters weight RGB by source alpha, so a transparent texel's hidden color does not bleed into the artwork.
Each selected guard clears one outer row or column at every mip. This explicitly removes that edge of the artwork.
The color error for a cutout counts only visible full-size texels. RGB565 stores red/blue with 5 bits and green with 6 bits, rounds to the nearest encoded value
(halves round up), and writes little-endian pixels with an explicit channel layout. Each mip averages the original
unquantized colours before independent quantization. Colours used counts distinct stored values across all cels
and mips; mean error compares source and decoded RGB channels at full size, ignoring alpha.

RGB565 output is independently decoded by RED before publication. The OpenJKDF2 GL renderer has an explicit RGB565
path, but generating and decoding a MAT does not qualify its appearance in a particular engine build. Verify in
the intended runtime. This workflow makes no retail JKDF2 or cross-game RGB565 compatibility claim. Other
true-colour encodings and continuously blended transparency are outside this writer's scope.

## Agent and MCP requests

Discover the standalone Draft 2020-12 request contract with `red-agent schema mat-create` (no level or image
read required), or MCP `sed_schema` / `sed_discover` with `topic: "mat-create"`. The `sed_mat_create` tool catalog
embeds that same schema in its `request` property. It describes defaults, exact resize dimensions, captured
source objects and format-dependent palette/options without changing existing conversion behavior.

`schemaVersion` remains optional. Every cel and palette object requires nonempty `path` and `sha256` strings;
uppercase hexadecimal digests are accepted. Optional `name` metadata is ignored at any JSON type. Other nested
fields and unknown root fields are refused. The schema describes potentially valid hashes; actual equality is a
runtime check (the unchanged CLI reports a nonempty malformed digest as a hash conflict after reading the source).
A schema match does not prove file readability, image validity, source identity, fresh-output availability or
engine compatibility. The output path and MCP `operationId` are wrapper arguments, not request fields.

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

For a keyed indexed material add `"colorKey": true` to an indexed request with its CMP. Optionally add
`"guardEdges": ["right"]` to clear the last column at every mip. The array takes unique entries from
`left`, `right`, `top` and `bottom`. A guard requires keying. To preserve a cutout across two JO/JA
editions, map the same indexed MAT and its matching palette in both **Model scene** recipes. RGB565
can still supply opaque art in the hardware edition. Hybrid sector mode rejects keyed materials.

RGB565 refuses `palette`, `dither`, `reserveIndexZero`, `colorKey` and `guardEdges` before reading source files, including when those options
are false or empty. It never opens a CMP. Its receipt adds `format: "rgb565"` and `bitsPerPixel: 16`, and omits
indexed-only `dither` and `paletteSha256`. Output bytes, output hash, source hashes, dimensions, cels, mipmaps,
colour counts and error remain explicit. Unknown formats refuse without publication.

**Neutral greys in RGB565.** Green has six bits to red and blue's five, and each channel rounds on its own, so some
neutral greys are stored with a cast: grey 87 becomes (90, 85, 90), a pink that shows on a large grey area. Lumen
Cistern's dark grating, a palette-exact tile whose bar highlights are grey 87, came out that way. The receipt counts
such greys in `tintedGreys` and gives the first in `tintedGreyExample` (`source` and `stored`). With
`"neutralGreys": true` (RGB565 only, default false), every source pixel with R = G = B is stored as the nearest value
that decodes neutral: red and blue share their 5-bit value and green takes whichever of its two matching 6-bit values
decodes closest, so 87 becomes (90, 89, 90). Other pixels and every default request keep their bytes.

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

## Painted lamp glows

`mat-create` takes an optional `paint` object, applied to every cel after `resize`, in texels of the final cel:

```json
"paint": { "glows": [ { "center": [44, 172.5], "radius": [30, 19], "rgb": [255, 158, 56], "strength": 0.6 } ],
           "fills": [ { "min": [38, 171], "max": [51, 175], "rgb": [255, 171, 0] } ],
           "wrap": true }
```

Each glow screens its colour over its ellipse, weighted `strength * (1 - d^2)^2` (d the elliptical distance), the
strongest glow winning where two overlap; each fill then sets its texels (a lamp lens). Outside them the cel is
unchanged, and with `wrap` (the default) both continue across the cel's edges as the texture repeats. This is how the
Moonwell amber indicators were made: the same request made once with `format: "rgb565"` for the hardware renderer
and once as indexed8 with dither for the software renderer gives a 16-bit MAT and its 8-bit twin of one name, which go
in separate GOBs. Pair it with `carveSurfaceRect` to cut the lens out as its own fully lit face. Pick lens colours
from the level's palette (for dflt.cmp, amber index 21 and pale warm 162) so the 8-bit twin draws the same lens.

## Tinted colormaps

A sector can give its room a colour of its own through its colormap, the way the stock red maps colour a lava
room. `red-agent cmp-create <request.json|-> <new-output.cmp>` (MCP `sed_cmp_create`, schema topic `cmp-create`)
makes one from the level's master CMP and an RGB tint:

```json
{ "schemaVersion": 1,
  "master": { "path": "dflt.cmp", "sha256": "<captured SHA-256>" },
  "tint": [0.45, 0.62, 1.0] }
```

Each of the master's 64 light levels is multiplied by the tint and mapped to the nearest master palette colour; the
palette is kept and the tint is stored in the header. The two renderers use different halves of the file: the 8-bit
renderer draws the sector through the tinted light tables, and the hardware renderer multiplies the sector by the
header tint. Made this way from the stock `01narsh.cmp` with the tint `[1, 0.2, 0.2]`, 76.5% of the light-table
entries match the stock `01redmap.cmp` exactly, and the colours they map to differ by 4.5 on average (the sum of the
three channel differences, out of 765). The stock tinted
maps have no transparency table; the output carries the master's when the master has one, because the table blends
two palette entries and the palette is the master's. A translucent face in a tinted sector, water in a coloured cistern
say, then has a table to blend through whichever colormap the 8-bit renderer reads it from. Each channel is from 0 to 4; a master without its light tables is
refused.

Pack the file in the level's GOB under `misc\cmp`, add it with `registerColormap`, and give the sector its index
with `setSectorColormap`. A snapshot with `"lighting": "baked"` and the GOB in `resourceArchives` shows the hardware
tint before the game is started; compare both renderers in the game.

## Level master colormaps

dflt.cmp has no teal or violet ramps, so an 8-bit texture in those colours dithers into noise. The stock levels solve
this with their own masters: every stock level master shares palette slots 0 to 191 (except 95) with the others, and
puts its own colours in 192 to 254, which dflt.cmp leaves as magenta placeholders. `red-agent cmp-master-create
<request.json|-> <new-output.cmp>` (MCP `sed_cmp_master_create`, schema topic `cmp-master-create`) builds a master the
same way:

```json
{ "schemaVersion": 1,
  "master": { "path": "dflt.cmp", "sha256": "<captured SHA-256>" },
  "ramps": [ { "rgb": [70, 200, 210], "steps": 12 }, { "rgb": [170, 120, 220], "steps": 12 } ],
  "colors": [ { "rgb": [255, 171, 0], "fullbright": true } ],
  "protectArchives": [ { "path": "mylevel.gob", "sha256": "<captured SHA-256>" } ] }
```

Each colour takes the next slot from `slots` (default 192 to 254) that no protected MAT draws and that the master does
not keep fullbright; the receipt lists where each colour went and every slot passed over, with the MAT that uses it.
A ramp adds `steps` colours from `rgb/steps` to `rgb`. A `fullbright` colour stays at full brightness at every light
level, like the stock emissive entries 0 to 31: texels in it glow in the 8-bit renderer, the per-texel counterpart of
a fully lit face.

The tables are made by the rules measured on the stock dflt.cmp. Light level `l` maps a colour scaled by `l/63` to the
nearest palette entry under a distance weighting red, green and blue 0.30, 0.59 and 0.11 (98.7% of dflt.cmp's light
entries reproduced exactly, against 67% for plain RGB distance); level 63 is the colour itself. A transparency entry
blends 0.35 of the front colour with 0.65 of the back (96.6% of dflt.cmp's table exactly). Only the new colours'
rows and columns, and entries that pointed at a replaced slot, are computed; every other byte is the master's, so a
texture that does not use the replaced slots looks exactly as it did.

What to protect: the stock `static.jkl` resources (weapons, Kyle, effects) draw none of 192 to 254, so a level using
dflt.cmp needs to protect only its own 8-bit MATs, plus any stock textures it borrows from a stock level (those were
made for that level's own master). Then make the level's 8-bit MATs against the new master (`mat-create` with it as
`palette`) so they use the new colours, and make tinted sector colormaps from it with `cmp-create`. Compare both
renderers in the game.

## Quake cloud skies

A Quake III or Quake Live shader with `skyparms - 384 -` can still show a complete sky. The first dash omits the outer skybox. Look at the shader stages for the scrolling cloud textures. For an opaque base and additive layers, bake a phase-zero set of six PNGs with the optional Python/Pillow helper. Supply the shader's cloud height and each stage's two texture scales in draw order.

```text
python tools/engine-capture/quake_cloud_sky.py sky-png --height 384 --size 256 --layer base.tga 2 2 --layer glow.tga 3 2
```

Use a new output folder. The five cloud faces share a spherical projection and wrapped bilinear sampling at their edges. The lower face gives the native renderer an opaque black background. The six names are `rt`, `lf`, `bk`, `ft`, `up` and `dn`. Use the same face assignments and palette across the two material editions. The helper captures still RGB layers. A shader with moving clouds, a sun effect, a different blend operator or a separate outer skybox needs additional authoring. See the [id Tech 3 sky renderer](https://github.com/ioquake/ioq3/blob/main/code/renderergl1/tr_sky.c) for the cloud projection.
