# Importing OBJ and GLB models

**Tools > Import Model…** converts a Wavefront OBJ or binary glTF 2.0 (GLB) file into a new Jedi Knight 3DO 2.1
model. The dialog uses the same portable importer as the agent's `model-import` command. It is separate from
**Edit > Import Sector**, which imports geometry into the current level.

## Prepare and inspect

1. Choose the source OBJ or GLB file. No external MTL, texture image or glTF buffer is opened implicitly.
2. Add an explicit material mapping for every used textured material. **Source material** is the exact,
   case-sensitive OBJ `usemtl` name or GLB material name. A GLB material without a name uses `#<index>`.
   **MAT basename** is the name the 3DO will reference, such as `crate.mat`, without an archive or directory prefix.
3. Enter each MAT's width and height in texels, or enter/select a **MAT file**. A selected MAT supplies its first
   cel's dimensions and takes precedence over the width/height cells. A color MAT has no texture dimensions;
   give explicit dimensions instead. The report identifies every captured MAT file and its SHA-256.
4. Set placement, normal handling and mesh splitting, then select **Prepare**.
5. Review the read-only report. It identifies source and output hashes, effective material dimensions, bounds,
   mesh parts, normal splits, unused mappings and ignored source constructs. Preparation writes no output file.

Unmapped materials, malformed input and unsupported constructs produce explicit importer diagnostics. Material
names are not guessed from local filenames. OBJ `mtllib` statements are reported as ignored. Select mappings in
the table and use **Remove selected** to remove only that row.

## Placement and normals

The default positive scale is `1`. Scientific notation is accepted; a nonfinite value, zero, a negative scale,
or a value above `1000000` is refused instead of clamped. The scale field follows the application's decimal separator
preference and rejects group separators. A modified draft retains its original input locale across a live preference
change; after a successful scale commit, its presentation adopts the current preference. **Y up → Z up** maps `(x, y, z)` to `(x, -z, y)`;
**Already Z up** retains the axes. **Bottom center**, **Center**, and **Keep transformed coordinates** choose
the model origin after axis conversion and scaling. An optional mesh name prefix configures generated names.

**Preserve authored normals / hard edges** is the default. **Recalculate smooth normals** explicitly replaces
authored normals and smoothing boundaries with geometric smoothing. See [model lighting and import normals](model_lighting.md#import-normals)
for the shared OBJ/GLB normal contract. OBJ UVs become `(u × width, (1 − v) × height)` texels; GLB UVs become
`(u × width, v × height)`.

**Split meshes to JK limits** is enabled by default: at most 512 faces, 512 geometric vertices and 768 texture
vertices per mesh, and 64 meshes per model. Clearing it requests refusal when a source mesh exceeds a limit.
Normal discontinuities can require additional vertices before these limits are checked. Each emitted mesh has
an identity hierarchy node under one root. This conversion does not create an animated skeleton.

**Allow untextured faces** is an explicit opt-in. Those faces have material `-1` and geometry mode `0`; they are
invisible and only collide. This option is not a substitute for providing a MAT mapping for visible geometry.

## Create new files

After preparation, choose a new `.3do` filename and select **Create new 3DO**. The serialized output is read back
through the ordinary core 3DO parser before publication. The model and selected MAT inputs are re-read and hash
checked during preparation and staging. A changed input requires preparation again. Every output is staged as a
complete sibling file and published with an atomic create-only operation; an existing file, directory, dangling
symlink, protected input alias, or concurrent creator is refused without replacement.

When used GLB materials share exactly one available embedded PNG or JPEG base-color image, **Save base-color
image** can write those captured image bytes unchanged to a new matching `.png`, `.jpg`, or `.jpeg` filename.
The report includes that image's SHA-256. Multiple images, external/unavailable images and unsupported image
types are reported and leave this action unavailable. Saving the image and creating the 3DO are independent
publications: either can succeed without the other. They do not form a two-file transaction, and the image is
not automatically converted to a MAT. Use [Material Authoring](material_authoring.md) for that conversion.

Publication does not insert a Thing, assign a resource, add a resource root, change the level's dirty state or
create a level undo entry. Add the generated model and its MATs to the intended project resources explicitly.
For an engine package, the model belongs under `3do\`, with its materials under the appropriate `mat\` or
`3do\mat\` resource paths. Resource closure and actual game behavior require their normal validation gates.

## Cancellation and limits

Preparation and staging run in the editor's bounded preview worker pool using immutable input/options snapshots.
Changing conversion inputs invalidates the prepared report. Editing an output filename retains a prepared
conversion; if staging is active, it cancels that publication and requires an explicit retry. **Cancel work**,
closing the dialog, or losing its host context revokes publication. The portable importer is not cooperatively
interruptible inside parsing, so an already-running bounded conversion may finish in the background; its stale
result cannot publish. The dialog does not block closure waiting for that conversion.

The existing parser limits remain authoritative: OBJ files are at most 16 MiB, GLB files 64 MiB, GLB JSON 8 MiB,
and each embedded data URI 4 MiB after decoding. The GUI captures at most 256 mapping rows, at most 64 MiB per
selected MAT, and at most 128 MiB of source plus mapping input bytes in one preparation. Each mapping-file read
counts toward that total, including repeated selections. Explicit dimensions must be integers from 1 to 4096.
The source is admitted by its bytes; changing a filename extension does not change the parser contract.

The input hashes establish the bytes used for conversion and detect changes at the stated recheck boundaries.
They do not lock external files against another process. A publication notification failure is reported separately
from the already-created file. Compact windows keep the action buttons outside the scrollable controls and report.
