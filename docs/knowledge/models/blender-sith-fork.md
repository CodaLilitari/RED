# Using the Blender-Sith Agentic fork

> Copied into RED from [CodaLilitari/JK-MotS-Model-Workflow@ca011b8](https://github.com/CodaLilitari/JK-MotS-Model-Workflow/blob/ca011b84c4fa5e1f3755e1543be2c1c1d25063c9/skills/jk-mots-model-workflow/references/blender-sith-fork.md) (MIT; see [LICENSE](LICENSE.md)). Regenerate with `tools/knowledge/import_knowledge.py` rather than editing this copy.

Use Blender-Sith Agentic by default for importing existing JK/MotS 3DOs into Blender for inspection and screenshots. Its focused fixes also preserve corner normals, make scripted metadata predictable, repair indexed MAT cel reading, and correct native KEY export. Keep a prepared export duplicate for modifiers and transform cleanup. An independent writer remains useful for precise multi-geoset preservation and file-level model surgery.

The earlier [Blender-Sith audit](blender-sith-audit.md) found no substantive locally modified fork in the historical projects. A new focused fork was subsequently developed at Andrei's request, starting from the actual audited upstream commit [`ecc90131bcfdeabd400225e8a72ce94cbff1ec5c`](https://github.com/smlu/blender-sith/commit/ecc90131bcfdeabd400225e8a72ce94cbff1ec5c). These are two different points in the project's history. The original work is by Crt Vavros / smlu; upstream history and GPL-3.0-or-later licensing are preserved.

The public [GitHub repository](https://github.com/CodaLilitari/blender-sith) now belongs to [smlu/blender-sith](https://github.com/smlu/blender-sith)'s fork network; the former standalone arrangement is historical. [GitLab](https://gitlab.com/codalilitari/blender-sith) remains a source-distribution link, but verify each host's actual revision before treating copies as equivalent. A release is an exact source snapshot: later upstream refactors and pull requests require a separate comparison and are not implicitly included.

## Public release and installation

Workflow 1.7.0 targets **v1.2.0-coda.1**, extension version **1.2.0**, named
**Blender Sith (Coda)**. Release source is pinned at
[`34d256c30f66fe9826e5a3318f2fd4397257f24c`](https://github.com/CodaLilitari/blender-sith/commit/34d256c30f66fe9826e5a3318f2fd4397257f24c).
Use the [GitHub release](https://github.com/CodaLilitari/blender-sith/releases/tag/v1.2.0-coda.1)
or [GitLab mirror](https://gitlab.com/codalilitari/blender-sith/-/releases/v1.2.0-coda.1)
and their exact package receipts. Published release, installed selection and
game acceptance remain distinct: verify the actual archive and payload before
using the new APIs. Keep platform, archive hash and measured runtime in the job.

1. Use Blender **5.0 or newer** and choose [Windows x64](https://github.com/CodaLilitari/blender-sith/releases/download/v1.2.0-coda.1/sith-1.2.0-windows-x64.zip), [Linux x64](https://github.com/CodaLilitari/blender-sith/releases/download/v1.2.0-coda.1/sith-1.2.0-linux-x64.zip), [macOS Apple Silicon](https://github.com/CodaLilitari/blender-sith/releases/download/v1.2.0-coda.1/sith-1.2.0-macos-arm64.zip), or [universal](https://github.com/CodaLilitari/blender-sith/releases/download/v1.2.0-coda.1/sith-1.2.0-universal.zip).
2. Compare the download with the release's [SHA-256 checksums](https://github.com/CodaLilitari/blender-sith/releases/download/v1.2.0-coda.1/SHA256SUMS.txt). Per-package receipts record source and normalized file hashes.
3. In Blender, open **Edit > Preferences > Add-ons > Install from Disk**, select the ZIP and enable the extension. The ID remains `sith`; use one edition in a given extension repository.

These are Python extensions, not Blender executables. The optional MCP companion
is separate. Background inspection can load a verified snapshot without changing
desktop preferences. The universal package removes a manifest restriction; it
does not expand the validated architecture/runtime matrix. macOS means Apple
Silicon: this Blender-5.0-minimum release does not claim Intel Mac support.

The preceding [v1.0.3-coda.2 release](https://github.com/CodaLilitari/blender-sith/releases/tag/v1.0.3-coda.2)
already supplied a separately validated Apple Silicon package. Earlier statements
that no Mac 1.0.3 asset existed described v1.0.3-coda.1 and are historical.

## 1.2.0 native pivots and binding audit

The [1.2.0 release](https://github.com/CodaLilitari/blender-sith/releases/tag/v1.2.0-coda.1)
is pinned to source `34d256c30f66fe9826e5a3318f2fd4397257f24c`. Keep its
[package checksums](https://github.com/CodaLilitari/blender-sith/releases/download/v1.2.0-coda.1/SHA256SUMS.txt)
and [validation record](https://github.com/CodaLilitari/blender-sith/releases/download/v1.2.0-coda.1/VALIDATION.md)
with the selected job. The extension ZIP is the installable artifact; there is no
separate fork installer. The optional MCP companion comes from matching source
and is not required for ordinary Blender work or the native binding API.

**Choose the native frame deliberately.** The Native Pivot XYZ control stores
the static local mesh/attachment offset in `3do_pivot`, including on Empty
nodes. Changing it preserves the editable surface and joint channels while
changing the native frame exported around that surface. The default export
restores that separate pivot and compensates temporary mesh coordinates. Object
and inherited scale apply to both. This is not an automatic repair of arbitrary
origin changes, Apply Transform operations, animation rigs or an old scene that
already lost its source pivots.

Old `3do_imported_pivot` data remains readable. The canonical property takes
precedence even when zero; editing the numeric control removes the legacy key.
A supported active Blender Pivot constraint takes precedence over stored native
metadata, even with zero offset. Muted or zero-influence constraints leave the
stored value available. Keep KEY's separate supported live-pivot semantics.

**Bake Pivots is an intentional loss of the native frame.** The optional
`export3do(..., bakePivots=True)` mode writes zero native pivots, including on
Empty nodes, while preserving exported surfaces and descendant joint poses.
It uses current editable vertices and discards stored offsets; supported live
Pivot constraints are still validated. This is neither a KEY bake nor a remedy
for a wrong mesh-to-joint assignment. Leave it off when preserving a native
attachment contract. Compare serialized joint and mesh frames as well as posed
surfaces before deciding that a result is equivalent. See the versioned
[pivot contract](https://github.com/CodaLilitari/blender-sith/blob/v1.2.0-coda.1/docs/blender-pipeline.md).

**Check what each KEY index addresses.** With the verified add-on's parent on
`sys.path`, the resource API can run without Blender:

```python
from sith.model.model3doLoader import load3do
from sith.key.keyLoader import loadKey
from sith.key.modelBinding import auditModelKey

model, model_version = load3do(model_path)
key = loadKey(key_path)
binding = auditModelKey(model, key, key_target="MOTS")  # or "JKDF2"
```

The audit checks indexed slots, names, declared allocation, basic numeric and
track-timing integrity, and the selected engine's KEY limits. Names are useful
diagnostics: the engine binds by index, so a label mismatch is review evidence,
not proof of misbinding. Results are `consistent`, `review`, `incompatible` or
`no_tracks`. Only 32 detailed findings are returned; `issue_counts` and
`findings_truncated` preserve the wider diagnostic scope. Unknown parsed marker
types retain their declared/skipped counts, so a dropped marker cannot hide a
source marker-limit violation. The audit does not evaluate animation, PUP
blending, first-frame policy, limb reach, weapon grip or attachment appearance.
Follow it with actual pose and game checks; do not rename tracks to hide a
mismatch or replace the existing allocation inspector's simpler use case.

**Use geometry-only import for a bounded diagnostic.**
`import3do(..., loadExternalMaterials=False)` explicitly avoids neighboring
MAT/CMP discovery. Ordinary import still loads materials by default. A
geometry-only `.blend` is not a textured inspection or a completed conversion.
Count native triangles, quads and n-gons by geoset before comparing budgets;
the scene importer still represents geoset 0 only.

## 1.1.0 integration

Release target **v1.1.0-coda.1**, extension **1.1.0**, integrates upstream
core at [`01ff4bd85498074e6f936d9adbbcc93817ca712d`](https://github.com/smlu/blender-sith/tree/01ff4bd85498074e6f936d9adbbcc93817ca712d)
with the reviewed [hexadecimal-property change (PR16)](https://github.com/smlu/blender-sith/pull/16)
and [native-pivot change (PR23)](https://github.com/smlu/blender-sith/pull/23),
while retaining the fork's automation. Source/native-CI evidence supports the
integration; a particular local installation must still resolve and verify its
own archive, payload and runtime. A deliberate development comparison keeps
its exact source receipt rather than silently changing that installation.

The integrated root package registers Blender lazily and does not expose the old
`sith.export3do` or `sith.exportKey` helpers. Use explicit modules:

```python
from sith.model import model3doImporter, model3doExporter, model3doLoader
from sith.key import keyExporter
from sith.material import matExporter
```

Enable the selected module through `addon_utils.enable("sith", default_set=False)` in a disposable Blender process before scene/property work; use Blender's restricted registration path.
Resource codecs can be imported without Blender; scene adapters and `exportMat`
still require it. Confirm the selected package version and module paths before
using the new keyword arguments below. A resolver result for 1.0.4 remains 1.0.4;
these new recipes do not upgrade it.

| Decision | 1.1.0 behavior | Application rule |
|---|---|---|
| Textured native import | Strict parsing by default; optional recovery and native-normal import | Supply exact MAT/CMP dependencies; retain diagnostics and distinguish missing textures from malformed geometry |
| Native material output | `exportMat` or `export3do(..., exportTextures=True)` encodes baked image pixels, bound cels and mips | Select `INDEXED` with the real CMP, or an explicitly tested encoding such as `RGB565`; the API default `RGBA32` is not a JK/MotS compatibility choice |
| KEY output | Explicit targets plus sparse/fractional and adaptive curve sampling | Pass `target='JKDF2'` or `'MOTS'`; the API default is `IJIM`. Evaluate serialized tracks at their timestamps, including between samples |
| Geometry export | Handles supported reflected winding, scaled normals and UV-unit metadata; rejects unsupported evaluated state | Prepare/apply intended modifiers on a duplicate; preserve the failure instead of disabling validation or dropping deformation |
| Numeric output | Float-preserving text replaces six fixed decimal places; 2.2 RGB formatting is repaired | Compare parsed values and bounds with justified tolerances, then record new hashes. JK/MotS still uses the 2.1 lane |

### Strict import and deliberate recovery

Start with `model3doLoader.load3do(path)` and the normal importer. When a user
needs available geometry from a broken text file, preserve the original and its
strict failure, then opt into `allowErrors=True` in a separate recovery job:

```python
diagnostics = []
root = model3doImporter.import3do(
    str(model_path), mat_dirs=[str(mat_dir)], cmp_file=str(cmp_path),
    clearScene=False, allowErrors=True, warnings=diagnostics,
    importVertexNormals=True,
)
```

Use this only with the reviewed 1.1.0 API, in a checkpointed or disposable scene.
Record `diagnostics`, the import log, and the report stored in the Blender file.
Recovery can retain completed records, create placeholders and omit invalid faces;
text parsing stops at the first syntax error. It does not guess the missing rest
of the model. Binary parsing remains strict. A recovered view is an incomplete
inspection artifact until the omissions are independently repaired and checked.

For valid sources, keep `allowErrors=False`; `importVertexNormals=True` is an
independent choice for retaining supplied custom normals. The importer parses
before clearing the scene and cleans up newly created datablocks on later failure,
but this is not an undo of `clearScene=True`. A missing MAT/CMP can still leave
placeholders in an otherwise successful strict model import. Neither recovery
nor a normal-preservation option fixes absent pixels.

### Older scripts and saved Blender scenes

- Re-import an intact native source when an old Blender file has already lost
  pivots. The retained native-pivot fix can preserve metadata that exists; it
  cannot infer erased attachment coordinates. Compare weapon/joint matrices as
  well as visible surfaces, and retain explicitly authored PivotConstraints.
- New imports record the UV coordinate domain. An older scene without that
  metadata needs its native source and texture dimensions checked before export;
  do not guess whether UVs are already pixel-scaled. Use an asymmetric diagnostic
  grid and avoid multiplying by image size twice. Import still exposes geoset 0.
- Vertex color export uses Blender's linear RNA color values, including byte
  color layers. Recheck an older scene that was manually brightened/darkened to
  compensate for the old sRGB path. Preserve source color attributes and compare
  native values before another correction.
- Ordinary flat-shading flags retain geometric vertex normals plus separate
  face normals. Custom normals and explicit sharp edges use corner normals and
  split where necessary. The old flat-cube count of 24 below belongs to 1.0.4;
  it is not the new representation's universal correctness test. Check native
  face/vertex normals and intended lighting, then count actual exported vertices.
- Supported negative scale now has explicit winding/normal handling. Non-uniform
  rotated parents, shear, unsupported parent inverses/delta transforms, visible
  modifiers, active shape-key deformation and unsupported constraints can fail
  early instead of producing a plausible but incorrect file. Bake the intended
  result on a duplicate and recheck hierarchy/animation; do not blindly apply
  transforms to the animation master.
- Adaptive KEY export preserves explicit zeroes and authored Euler winding and
  samples supported curve behavior. It is not a general NLA/driver/constraint
  bake. Quaternion orientations cannot encode an omitted full revolution. The
  exact target and animation contract remain deliberate choices; see
  [KEY target and sampling](compatibility.md#select-the-key-export-target).

The 1.0.4 radius example below remains an exact historical measurement. New float
serialization can produce a different decimal spelling and file hash. Check
finite values, mesh culling bounds around emitted local vertices, pivots and
attachment transforms on the actual new output. An automatic model radius is a
bound on assembled rest geometry; an authored model `RADIUS` is preserved as a
collision-default choice and need not enclose every visible point. Do not enlarge
an intentional collision radius merely to pass a visual-containment check. These
checks are not proof of all-animation bounds or original-game rendering.

## Resolve the shared current installation

Default Windows and Linux jobs use one private manifest per host:
`Path.home() / ".config" / "blender-sith" / "current-install.json"`.
Run `python scripts/resolve_install.py` from the model skill with Python 3.11 or
newer; `--manifest <absolute-manifest-path>` selects another explicit manifest.
The resolver checks files and prints JSON. It never launches Blender, imports
`bpy` or the add-on, installs anything, or falls back to PATH or a checkout.
The explicit installation step chooses the current release; the resolver verifies that declared
selection, not whether a newer public release exists. Keep private manifests,
runtime paths and resolved job receipts out of portable research packages.

The version-1 manifest records `schema_version: 1`, `release_tag`,
`extension_version`, the host's `platform_system` (`Windows` or `Linux`) and
`platform_machine`, and these installation bindings:

- `addon_dir`: an absolute directory named `sith` containing `__init__.py`.
- `archive_path` and `archive_sha256`: the exact reviewed extension ZIP.
- `payload_sha256`: every regular ZIP member's relative POSIX path and SHA-256,
  including metadata and licenses. The ZIP, map and installed directory must
  agree; only source-paired CPython `.pyc` caches under `__pycache__` are ignored
  in the installed tree. Unlisted sourceless or legacy bytecode is rejected.
- `runtime`: `route` (`blender` or `python`), absolute `executable`, recorded
  `version`, optional `executable_sha256` (verified when supplied), and optional
  absolute `bpy_site` for the Python route.
- `launcher_path`, `launcher_sha256`, `harness_path`, `harness_sha256`: the
  reviewed regression tools. The harness must be the exact
  `tests/background_regression.py` derived from the launcher's distribution root.

Resolve the manifest in the host that will run the job; Windows and WSL/Linux
paths are separate installations. Package ID/version, platform, full payload
hashes, executable existence and launcher/harness identity must pass before
using the result. The manifest is a private selection record, not a signature
or independent proof of upstream provenance. Runtime `version` is recorded
metadata: actual jobs must still report `bpy.app.version_string`, module paths
and hashes. The returned `ignored_generated_bytecode` lists excluded caches;
source attestation does not attest their contents. Start verification jobs with
a clean cache or an isolated empty cache location: `-B` prevents cache writes,
not cache reads. Resolver success does not prove desktop enablement or game behavior.

Use the returned `addon_dir`, `source_root` (its parent) and `runtime` for
default jobs. An explicit upstream, historical or development comparison can
retain its own exact source/runtime and receipt; label it as a comparison and
do not silently replace the shared current selection. If resolution fails,
repair or provision the intended installation instead of selecting another
copy automatically. The optional MCP companion also needs these exact paths
configured and verified before it can be called the current installation.

## Choose the useful route

| Need | Method | Practical cost or limit |
|---|---|---|
| Inspect or render an existing textured JK/MotS model | Direct fork `sith.model.import3do` call below | Supply resolved MATs and CMP; scene import exposes only geoset 0 |
| Direct export of flat faces, sharp edges or existing custom normals | Focused fork | Normal discontinuities create necessary additional 3DO vertices; check the exported budget |
| Bake subdivision, bevels or a weighted-normal modifier into the export | Duplicate the source, apply selected modifiers, then use the selected version | 1.0.4 reads base geometry; 1.1.0 deliberately rejects unsupported visible modifiers rather than silently omitting them |
| Preserve all existing LOD/geoset structures exactly | Format-aware writer plus structured validation | Blender-Sith's scene importer still exposes only geoset 0 |
| Script KEY priority or node flags without prefix callback errors | Fork's registered properties, followed by readback assertions | Raw ID-property dictionary writes bypass validation |
| Export keyed returns to zero or quaternion turns through the wrap boundary | KEY corrections introduced in 1.0.3; broader upstream sampling in 1.1.0 | Explicitly select the JK/MotS target with 1.1.0; key enough quaternion samples to express the turn |
| Inspect an interactive scene or correct pivots | Blender UI, optionally through a verified general Blender connection | The optional format-specific companion is not an interactive scene editor |
| Summarize a native file or make a geometry-only `.blend` | Optional local MCP companion | Conversion omits MAT/CMP lookup and imports only geoset 0; it is not the textured inspection route |
| Repeat synthetic checks or compare two versions | Bounded background launcher and regression suite | Serialized fixtures do not certify retail or multiplayer runtime behavior |

## Direct native import for inspection and screenshots

For the default job, resolve the shared installation above and retain its JSON as `resolved_install`; its `source_root` contains the verified installed `sith/__init__.py`. An extension label or repository URL alone does not identify the code that ran. For an explicitly requested source comparison, select a verified checkout or snapshot and record its revision and local changes. When another task is editing that checkout, copy the required add-on source into an external job evidence directory and hash that snapshot; do not pull, reset or modify the shared checkout. Serialize any necessary Git inspection with the checkout's coordinator. A release ZIP places `__init__.py` at its root; an extracted installation must put it inside a directory named `sith` for this import layout.

Use a fresh background Blender process or an isolated Python environment with compatible `bpy`. The direct-import workflow observed on 2026-10-23 used `bpy` 5.1.2 with `bpy.app.background == True`; that is historical runtime evidence, not a required personal installation path. Record `sys.executable`, `bpy.__file__`, and `bpy.app.version_string` from the actual import process. Keep runtimes, dependencies, copied source, assets and outputs physically outside Git checkouts, with no junctions or symlinks back into them.

In the job script, use the resolver's source root and resolve exact absolute asset paths for `model_path`, `mat_dir` and `cmp_path`, then invoke the fork itself:

```python
import hashlib
import sys
from pathlib import Path

# Successful JSON from resolve_install.py, supplied to this job explicitly.
fork_root = Path(resolved_install["source_root"])
model_path = Path(model_path).resolve()
mat_dir = Path(mat_dir).resolve()
cmp_path = Path(cmp_path).resolve()
sys.dont_write_bytecode = True
sys.path.insert(0, str(fork_root))
import bpy
import addon_utils
import sith
from sith.model import model3doImporter, model3doLoader

assert bpy.app.background, "Use a disposable process; clearScene clears scenes"
assert Path(sith.__file__).resolve() == fork_root / "sith" / "__init__.py"
assert Path(model3doImporter.__file__).resolve() == (
    fork_root / "sith" / "model" / "model3doImporter.py"
)
assert model_path.is_file() and mat_dir.is_dir() and cmp_path.is_file()
source_hashes = {
    p.relative_to(fork_root).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
    for p in sorted((fork_root / "sith").rglob("*"))
    if p.is_file() and p.suffix in (".py", ".toml")
}
assert addon_utils.enable("sith", default_set=False) is not None
native, file_version = model3doLoader.load3do(str(model_path))
import_options = dict(
    mat_dirs=[str(mat_dir)],
    cmp_file=str(cmp_path),
    uvAbsolute_2_1=True,
    importVertexColors=True,
    importRadiusObj=False,
    preserveOrder=True,
    clearScene=True,
)
root = model3doImporter.import3do(str(model_path), **import_options)
assert root is not None, "The importer produced no model root"
import_receipt = {
    "installation": resolved_install,
    "python": sys.executable,
    "bpy_module": bpy.__file__,
    "blender": bpy.app.version_string,
    "sith_module": sith.__file__,
    "importer_module": model3doImporter.__file__,
    "source_sha256": source_hashes,
    "model": str(model_path),
    "model_sha256": hashlib.sha256(model_path.read_bytes()).hexdigest(),
    "cmp": str(cmp_path),
    "cmp_sha256": hashlib.sha256(cmp_path.read_bytes()).hexdigest(),
    "mat_directory": str(mat_dir),
    "import_options": import_options,
    "native_geoset_count": len(native.geosets),
    "scene_geoset": 0,
}
# Save this receipt with the job's asset manifest, import log and render settings.
```

For the resolver's `blender` route, use its absolute `runtime.executable` followed by `--background --factory-startup --offline-mode --threads 2 --python-exit-code 1 --python job.py -- <job arguments>` under an actual process deadline that can terminate the owned process tree. Use isolated Blender user configuration. For its `python` route, add `runtime.bpy_site` when supplied before `import bpy`, preserve the selected interpreter path (including a virtual environment's symlink), and use a bounded supervisor. These settings do not constitute an OS sandbox. Do not install or enable the add-on in desktop preferences merely to run a background import.

The importer searches its default model-relative material folders **before** supplied `mat_dirs`. Extract only the intended package and explicitly resolved dependencies into an isolated job tree, inspect the ordered search folders, and resolve collisions before import. A missing MAT can produce a placeholder while import still succeeds. Capture the import log; verify every referenced MAT, decoded image, UV layer and material assignment. Hash the actual MAT files used. Supply the intended CMP for indexed materials; a guessed palette can make an intact model look wrong. `uvAbsolute_2_1=True` uses image-size conversion; compare it with the native renderer contract before diagnosing a source-model defect.

This call imports geoset 0, native hierarchy and material data. Check all native geosets with structured diagnostics and keep source defects separate from importer limitations. Inspect the actual final images. Record any task-local material adapter or display conversion and its affected files. Save the unaltered imported scene before optional presentation changes that could hide diagnostic evidence. A reconstructed mesh or the companion's geometry-only conversion can supplement diagnosis; neither substitutes for a requested textured fork import.

## Historical changes through 1.0.4

**Normal preservation.** The upstream exporter merged coincident vertices using position and color and wrote a BMesh vertex normal. The fork reads Blender's per-corner normals and includes the normal in the merge key. Flat-face, sharp-edge and custom normal splits survive. Texture coordinates keep their separate indexing. A smooth cube stays at eight vertices; a flat or all-sharp cube exports 24. Blender's [corner-normal API](https://docs.blender.org/api/5.0/bpy.types.Mesh.html#bpy.types.Mesh.corner_normals) defines the source data used by the fix.

Credit SMLiberator's [upstream issue 13](https://github.com/smlu/blender-sith/issues/13) for the earlier report of sharp-edge loss, automatic merging and the resulting vertex-budget tradeoff. This patch supplies normal-aware merging and synthetic regression coverage; it does not implement the report's suggested optional merge-control UI.

**Hexadecimal metadata.** Four-digit fields accept bare or `0x`-prefixed hexadecimal input, normalize it, and reject negative or overflowing values while retaining the previous value. The registered getter returns declared defaults when unset: `FFFF` for KEY high-priority types and `01` for hierarchy node type. This corrects the previous empty-string getter behavior; it is a deliberate default change. Saved explicit values remain explicit.

Use readback assertions in scripts:

```python
bpy.context.scene.sith_key_types = "0xFFFF"
assert bpy.context.scene.sith_key_types == "FFFF"
model_root.sith_model3do_hnode_type = "0x0001"
assert model_root.sith_model3do_hnode_type == "01"
```

Do not infer a KEY animation failure merely from the historical callback error. The earlier probe showed the assignment could still read back correctly. The patch eliminates the callback problem and fixes range/default behavior; animation playback remains a separate test.

**Material fixes added in 1.0.2.** Darth Martyr's [material-table separator repair](https://github.com/sithmartyr/blender-sith/commit/a25543f5ba45c5ad203a65ba11193c640b37e255) retains its original author and source-commit attribution. Long MAT filenames remain separated from their numeric index. A separate Coda fix consumes indexed MAT mip payloads even without a CMP palette, keeping later cel headers aligned and rejecting truncated skipped data. Missing palettes still yield placeholders; this is not recovery of the intended colors. See the [dated source audit](https://github.com/CodaLilitari/blender-sith/blob/v1.0.4-coda.1/docs/fork-audit-2026-11-01.md).

**KEY export corrected in 1.0.3.** A keyed location or rotation component can now return to explicit zero without retaining its earlier nonzero value. Quaternion samples choose equivalent continuous pitch/yaw/roll values before native differentials are written, avoiding an unintended long reverse turn across the wrap boundary. Authored Euler winding remains intact, including sparse multi-turn Euler keys. See the [correction notes](https://github.com/CodaLilitari/blender-sith/blob/v1.0.4-coda.1/docs/key-export-corrections.md) and [exporter source](https://github.com/CodaLilitari/blender-sith/blob/v1.0.4-coda.1/sith/key/keyExporter.py).

Keep quaternion samples close enough to express the intended direction: orientation samples alone cannot distinguish an omitted revolution or a turn exceeding 180 degrees between samples. Bake or key an adequate sampling density when needed. The 1.0.3/1.0.4 correction did not bake unkeyed constraints, modifiers or Blender interpolation curves. Version 1.1.0 adds supported adaptive curve sampling as described above. Historical tests inspect serialized KEYs and synthetic interpolation; actual game playback is still a separate check.

## Radius and native pivots corrected in 1.0.4

Red's saved build used a conservative radius of **0.133282** after the old
exporter produced **0.081336**. Re-export of that unchanged Blender file
reproduced the problem. Its assembled native rest geometry requires about
**0.120722308**, so the repaired exporter writes **0.120722** at six-decimal
format precision. The fix measures exported hierarchy geometry about the native
origin, including ancestor translations/rotations and each node's pivot; it
does not apply scale or INSERT OFFSET twice. An explicitly authored larger
radius remains authoritative. Rest-pose containment is distinct from bounds
for every animation pose.

A separate archived check found native pivots lost through import/export.
Baking a pivot into vertices can preserve visible surfaces while changing the
joint matrix used to attach a weapon. The cited hand pivot explains an
approximately **0.01096 JKU** attachment shift. New imports preserve pivot
metadata; export restores native vertices and pivots, with explicit authored
PivotConstraint behavior kept separate. Check attachment matrices as well as
surface positions. The fix cannot infer pivots already erased from older saved
Blender files: re-import a source 3DO retaining its hierarchy, or perform a
separately verified rig repair. Existing KEY location semantics are preserved.

Credit Red's report, workaround and archived verifier leads; Coda reproduced
the defects, checked native engine semantics and implemented the add-on repair.
This update ships synthetic tests, not the private character source. It does not
turn geoset-zero Blender import into a complete LOD-preserving round trip.

## Optional local MCP companion

The fork now offers a separate [local stdio companion](https://github.com/CodaLilitari/blender-sith/blob/v1.1.0-coda.1/docs/MCP.md). The normal Blender extension does not start it or require its dependencies. Its five tools report configuration (`server_info`), summarize an allowlisted native file (`inspect_3do`), create a geometry-only scene (`convert_3do_to_blend`), run synthetic checks (`run_regression`), and package the companion checkout's add-on (`build_extension`).

`inspect_3do` summarizes all parsed geosets. Conversion imports **geoset zero with placeholders**, disables neighboring MAT/CMP lookup, and cannot normalize JK 2.1 UVs using absent texture dimensions. Use the direct recipe above for textured inspection. `build_extension` does not publish or install; when configured with a Blender executable it also validates the exact ZIP. A Python-plus-`bpy` configuration cannot substitute for Blender CLI extension validation.

Configure trusted runtime/source paths, exact authorized read roots, and an external job directory. The companion serializes bounded jobs and rejects paths outside its configured scope; it is a trusted-user local tool, not an OS security sandbox. Preserve its limits and receipts. Its SDK environment should be separate from a general Blender MCP server's environment. The companion documentation's example pairing with Blender Lab is not evidence of a tested joint session or official endorsement.

## Repeat the checks

Prefer the fork's [bounded launcher](https://github.com/CodaLilitari/blender-sith/blob/v1.1.0-coda.1/docs/platform-builds.md). From this skill, run `python scripts/resolve_install.py --regression-output <absolute-new-external-directory>`, substituting an actual absolute path. The returned `regression_argv` is a JSON argument list to execute for the authorized check, without shell interpolation. It explicitly binds `--addon` to the installed `sith` directory, selects `--blender` or `--python` and optional `--bpy-site`, and sets `--threads 2 --timeout 300`. The resolver itself does not execute that list or create the output directory.

The launcher isolates preferences, bounds the subprocess lifetime and thread count, verifies the completed report and source hashes, and refuses existing output directories or output inside its source, the selected add-on or any Git checkout. Keep dependency environments, build/capture trees and generated fixtures physically external too, with no junctions or symlinks into source checkouts. The launcher does not install dependencies.

For a deliberate upstream/historical/development comparison, invoke the reviewed launcher with an explicit absolute `--addon`, explicit `--blender` or `--python` (and `--bpy-site` when needed), separate new external `--output`, `--threads 2 --timeout 300`, and retain the comparison identity. Do not omit `--addon`: its default selects the launcher's checkout, which need not be the current installed package. Direct harness entry points remain available for established comparison automation with equivalent process supervision and an explicit add-on selection.

The suite creates synthetic geometry, animations and material fixtures in a disposable process, checks serialized native data and writes `regression.json`. Preserve the launcher receipt and detailed results, including failures and skips. A successful report establishes the tested contracts, not game compatibility.

### Historical release evidence: 1.0.4

The exact runtime matrix, native-platform receipts and check totals are retained
in the [release validation record](https://github.com/CodaLilitari/blender-sith/releases/download/v1.0.4-coda.1/VALIDATION.md).
Each new release must be checked against its own package hashes; the earlier
16-case 1.0.3 results cannot validate changed 1.0.4 bytes. The new synthetic
coverage includes origin-centered radius, accumulated transforms and pivots,
import/export attachment preservation and unchanged source data. Actual game
rendering, animation blending and connected multiplayer remain separate checks.

### 1.1.0 core and application checks

Retain three separate results: the integrated upstream `core` suite, its real
Blender `app` suite, and the fork's bounded regression/package checks. No older
1.0.4 receipt validates changed source or archives. In a reviewed full-source
snapshot, upstream's ordinary-Python command is `python tests/run.py --suite core`
with Python 3.11+ and the declared NumPy dependency. It checks actual codecs without
fake `bpy` modules; it does not run Blender adapters or games. Use a bounded
supervisor and the exact selected interpreter rather than the illustrative bare
`python` command. Keep dependencies, cache and output outside source/Git trees.

For `--suite app` or `--suite all`, use real background Blender 5.0+ with
`--python-exit-code 1`, put test arguments after `--`, and retain process limits
and isolated preferences. The installed add-on's `source_root` is not assumed to
contain these development tests; use the matching reviewed source/test snapshot.
Record source/package hashes, measured runtime, test totals, failures and skips.
The workflow's small probe is a diagnostic, not a substitute for these suites.
The verified integration run completed both native Linux and ARM64 macOS jobs.
Linux reports 264 core cases, 768 Blender application cases and 29 fork checks.
The macOS artifacts independently report 264 core and 768 application cases
with zero skips, plus 29 fork checks. The Windows core run completed 264 cases
with two host-capability skips. Retain the detailed receipts: package schemas,
native execution and desktop enablement remain separate checks. See the pinned
[upstream test boundary](https://github.com/smlu/blender-sith/blob/01ff4bd85498074e6f936d9adbbcc93817ca712d/tests/README.md).

### Historical baseline: September 2026 / 1.0.1

The original identical ten-check comparison on `bpy` 5.1.2 gave upstream four passes and six failures, and the focused candidate ten passes. Its fixes covered disconnected normal seams, flat/sharp/custom normals, mixed faces and hexadecimal metadata. Controls covered smooth geometry, UV/material/color channels, hierarchy and a simple animated KEY roundtrip. Those totals describe that historical harness, not today's full matrix.

The original installable extension ZIP was extracted and independently passed those same ten checks. Its [packaged-artifact evidence dated 2026-09-30](https://github.com/CodaLilitari/JK-MotS-Model-Workflow/blob/ca011b84c4fa5e1f3755e1543be2c1c1d25063c9/skills/jk-mots-model-workflow/references/evidence/blender-sith-packaged-fork-2026-09-30.json) records the archive hash and source commit `0f401e672f9e28818bc7304407bf1cb781e65b70`. Source archives use exact Git blobs; working-tree hashes can differ with line-ending normalization. Installing through the desktop extension UI was not part of that process-local test.

## 1.0.4 limits and model release checks

- Apply non-uniform or negative scales on a prepared export duplicate, then inspect winding, normals, bounds and hierarchy offsets. The radius/pivot repairs follow exported native coordinates; they do not establish arbitrary shear, modifier or animation-transform fidelity.
- Shared mesh datablocks still produce separate mesh records for separate hierarchy objects. Safe reuse would have to account for scale and object-specific properties.
- The additional 1.0.4 3DO 2.2 experiment found an RGB face-extra-light writer/reader disagreement. The 1.1.0 upstream integration repairs that serialization, but the old passing JK/MotS 2.1 matrix is not its proof. 2.2 remains an IJIM format choice, not a JK/MotS upgrade.
- Version 1.0.4 exported byte-color layers through the old sRGB path; 1.1.0 uses linear RNA colors. Recheck compensated old scenes as described above.
- Modifier evaluation, retail rendering, animation blending, multiplayer interoperability and material resolution remain independent checks. Follow [runtime and packaging](runtime-and-packaging.md) and [compatibility](compatibility.md).

See [Blender export and baking methods](blender-export-and-baking.md) for prepared duplicates, native atlases, analytical chrome baking and high-to-low alternatives. The fork is the default native import route; export and preservation methods still depend on the task.
