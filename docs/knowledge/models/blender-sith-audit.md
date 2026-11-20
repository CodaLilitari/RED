# Blender-Sith: source audit and reproducible export probes

> Copied into RED from [CodaLilitari/JK-MotS-Model-Workflow@ca011b8](https://github.com/CodaLilitari/JK-MotS-Model-Workflow/blob/ca011b84c4fa5e1f3755e1543be2c1c1d25063c9/skills/jk-mots-model-workflow/references/blender-sith-audit.md) (MIT; see [LICENSE](LICENSE.md)). Regenerate with `tools/knowledge/import_knowledge.py` rather than editing this copy.

Audit date: 2026-09-30. Scope: Blender/Sith model tooling for JKDF2 and MotS single-player/multiplayer models. No level authoring is included.

## What was actually changed

The inspected Blender-Sith copies are unmodified upstream sources. Three local Git checkouts are clean at `ecc90131bcfdeabd400225e8a72ce94cbff1ec5c`, with `https://github.com/smlu/blender-sith.git` as their origin. A fresh remote HEAD check on the audit date returned the same commit. A fourth copy of the rc4 extension matched all 26 original archive members. A separate editor research snapshot contains selected upstream files, not a custom implementation.

No significant custom Blender/Blender-Sith fork or Blender MCP implementation was found in the historical project sources audited for version 1.0.0. Those copies must not be relabeled as authored fixes. After this audit, version 1.1.0 added a new [focused fork](blender-sith-fork.md) for normal preservation and hexadecimal metadata. The evidence below describes the original upstream baseline; its broader patch criteria remain relevant to unresolved modifier/mesh-reuse work.

| Component | Provenance and result |
|---|---|
| [Blender-Sith rc4](https://github.com/smlu/blender-sith/releases/tag/v1.0.0-rc4) | Original release ZIP SHA-256 `d1ab51c59ea14297408d0a68d0835c871c83b9ffb797f6f3bde6005b36859e47`; extracted extension matched all 26 members in the fresh audit. |
| [Upstream ecc90131](https://github.com/smlu/blender-sith/commit/ecc90131bcfdeabd400225e8a72ce94cbff1ec5c) | One commit after rc4. The source diff changes only `sith/material/mat.py`: 28 added and 13 removed lines. It is the upstream MAT transparency repair, not our patch. |
| Extension manifest | Blender minimum `5.0.0`, visible version `1.0.0-rc4`, maintainer Crt Vavros, SPDX `GPL-3.0-or-later`. The commit identifies newer code even when the visible version stays the same. |
| Local installable source-commit ZIP | Repackaged upstream blobs with root-level extension layout and license/docs. Historical SHA-256 `8927e1ead757feeae1ef46089ffdadc66398d9c4cd4b079fbb4deca7ccc7a9b1`. Repackaging is not an authored exporter fix. |
| Background runtime | Historical checks and the new synthetic probe used Blender `bpy` 5.1.2. An isolated Python import is separate from installation into desktop Blender preferences. |

Blender-Sith is by **Crt Vavros (smlu)**. Retain its license and notices in any redistribution or derivative; this workflow links to the upstream project rather than changing its license. [Upstream source and license](https://github.com/smlu/blender-sith)

## Fresh synthetic observations

The portable [probe script](https://github.com/CodaLilitari/JK-MotS-Model-Workflow/blob/ca011b84c4fa5e1f3755e1543be2c1c1d25063c9/skills/jk-mots-model-workflow/scripts/probe_blender_sith.py) was executed on the audit date against unchanged `ecc90131` in the existing isolated `bpy` 5.1.2 runtime. It completed all four probes, and the hashed add-on Python/manifest files remained unchanged. These results also reproduce the earlier September 12 study.

| Probe | Observed output | Consequence |
|---|---|---|
| Cube with unapplied SIMPLE subdivision | Blender base: 8 vertices / 6 faces; evaluated: 26 / 24; export: 8 / 6. | Export ignores the enabled modifier result. Bake/apply it to an export duplicate or use a tested evaluated-geometry path. |
| Two perpendicular triangles with separate coincident vertices | 6 input vertices become 4; 2 face corners receive the wrong normal. | A geometric vertex split alone does not survive this merge policy. Check normals after actual 3DO serialization/reparse. |
| Two objects sharing one Mesh datablock | One source mesh, two objects; two exported meshes. | Mesh instances are duplicated. This is a size/representation limitation, not by itself a rendering failure. |
| KEY type metadata `0xFFFF` | Assignment returns with `FFFF` readback while the callback reports a length error; canonical `FFFF` works. | Metadata warning has a project-side workaround; it is not a reproduced animation-interpolation failure. |

The compact machine-readable record is [probe-results evidence](https://github.com/CodaLilitari/JK-MotS-Model-Workflow/blob/ca011b84c4fa5e1f3755e1543be2c1c1d25063c9/skills/jk-mots-model-workflow/references/evidence/blender-sith-probe-2026-09-30.json). The probe exits zero when observations complete; zero does **not** mean no defects. `--require-fixed` changes the exit policy for a future patched implementation.

From the `skills/jk-mots-model-workflow` directory, run with an installed Blender executable in a fresh background process:

```powershell
blender --background --factory-startup --python .\scripts\probe_blender_sith.py -- --addon C:\tools\blender-sith\sith --output .\probe-output
```

Or use an existing Python environment that supplies `bpy`:

```powershell
python .\scripts\probe_blender_sith.py --addon C:\tools\blender-sith\sith --output .\probe-output
```

For a separately staged dependency directory, add `--bpy-site C:\tools\bpy-dependencies`. Paths above are examples, not required machine paths. The output directory must be new. The script produces a synthetic `splitprobe.3do` and a JSON report; it reads no commercial game files and changes no add-on source. It deliberately refuses an interactive Blender process.

The implementation reads original `obj.data`, merges vertices using position/color rather than normal identity, and emits a mesh per object. The normal and reuse findings agree with **SMLiberator's** original [issue #13](https://github.com/smlu/blender-sith/issues/13) and [issue #12](https://github.com/smlu/blender-sith/issues/12). The project's synthetic experiments are independent local reproductions, not authorship of those issue reports. [Pinned exporter source](https://github.com/smlu/blender-sith/blob/ecc90131bcfdeabd400225e8a72ce94cbff1ec5c/sith/model/model3doExporter.py)

## Project-side improvements that are real

Two further exporter details were identified during Lily's bounded Pro advisory and checked directly against the pinned local source. `_uv_add_image_size` selects the first image-texture node that has an image, so an unrelated texture node can determine absolute-UV conversion. `_get_mat_name` lowercases the material name and truncates it at the first period before adding `.mat`, so distinct Blender names can collapse to the same output name. Prepare unambiguous export materials and check names after conversion. In `model3doWriter.py`, format 2.1 averages RGB to a scalar while 2.2 writes RGB; preserving vertex color means different things in the two formats. These are source-inspected behaviors, not new rendered test results.

- **KEY metadata adapter:** clear animation through the add-on helper, assign canonical `FFFF`, and call the unchanged KEY importer with `clearScene=False`. The adapter does not change positions, rotations, slopes, timing or interpolation.
- **Preview normal preservation:** a project helper assigns supplied normals after import for inspection. This does not repair the third-party exporter's vertex merge behavior.
- **Independent writer correction:** the project's own 3DO writer defaults newly authored mesh and face texture modes to perspective `1` while retaining explicit affine `0`. Round-trip cases exercised default 1, explicit 0 and explicit 1, and final export verification reparsed all LODs. This was a custom writer data correction, not a Blender-Sith change or proof of improved rendering. The audited native OpenGL shader interpolates independently of these legacy fields, so flipping them alone has no source-supported expected quality gain there.
- **Background validation:** explicit source/ZIP hashes, independent parsing, selected KEY pose comparisons, packed texture save/reopen checks and all-LOD structural validation. These are command-line workflows built around the add-on.
- **Reproducible packaging:** the source-commit ZIP is assembled from committed Git blob bytes to avoid Windows newline conversion, with fixed ZIP timestamps, exact member checks and retained notices. This is packaging infrastructure.
- **Deterministic color baking:** the source model's accepted face/skin texture workflow retained original inputs, UV-only caches, unchanged geometry/normals, named temporary bake visibility filters and repeat output hashes. See [methods and recipes](blender-export-and-baking.md).

## What a useful narrow fork would implement

Offer an explicit evaluated-geometry export mode that leaves the authored object intact. Carry or deliberately rebuild per-face Sith properties, material indices, UVs and light values after topology changes. Preserve evaluated corner-normal identity in the game vertex split/merge policy. Reuse geometry only when evaluated geometry, transforms, material bindings and face data agree; preserve the hierarchy independently of that reuse.

Validate those changes against the four synthetic fixtures, plus negative/nonuniform transforms, multiple materials, per-face metadata, vertex lighting, UV seams, hierarchy/pivots and near-budget meshes. Compare the actual serialized and reparsed 3DO, then animate/render the result in the intended JK/MotS lane. This is a proposed validation scope, not a completed fork release.

## Limits that prevent false conclusions

Blender-Sith's parser reads every geoset; its inspected scene importer creates an editable scene from geoset 0. Preserve the original complete native 3DO; one imported `.blend` is not a round-trip of the lower LODs. An empty mesh node can be deliberately retained as a hierarchy slot with mesh index `-1` when its game contract calls for it.

The Starweird V6 output used a separate deterministic writer. Therefore the reproduced add-on exporter defects do not, by themselves, explain the earlier V6 face appearance. Reference quality, geometry decisions and exporter defects require separate diagnosis.

The public upstream [KEY issue #6](https://github.com/smlu/blender-sith/issues/6) concerns particular Indiana Jones animation behavior; that behavior was not reproduced by these tests and is outside this JK/MotS probe. Isolated KEY pose equality does not reproduce PUP blending, weapon attachment, collision, lifecycle or connected multiplayer.

No fresh game compatibility test was performed for this audit. The newly executed result is specifically four synthetic tool probes, clean source checks and upstream identity verification.
