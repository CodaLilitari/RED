# Filesystem path contracts and qualification

This guide records the implemented native-path boundary and the remaining work under REL-01 in the
[product roadmap](product_roadmap.md). It is a developer work queue and acceptance contract. A source-audited
narrow conversion identifies a risk to test; it is not proof that every listed command fails at runtime.

## Implemented boundary

The level commands `inspect`, `new`, `query`, `select`, `validate`, `plan` and `apply`, and their MCP routes,
use the [selected level workflow contract](agent_workflow.md). MCP validation is named `sed_diagnose`; path-based
`sed_discover` shares the inspection boundary. The three `model-native-*` commands have their separate
[native authoring contract](native_model_authoring_design.md#filesystem-paths).

Their coverage establishes the selected top-level input, request, staging and output paths. It does not qualify
nested resources or unrelated asset, archive, package and launch-planning commands. The full Release suite,
packaged-installation checks and target-game observations remain distinct gates.

The Windows regressions exercise native process arguments and filesystem entries under code page 1252, including
distinct intended/mojibake source files, accented and supplementary Unicode names, occupied destinations,
Unicode workspaces and temporary directories, UTF-8 receipts, and original-argument journal replay. A completed
historical operation returns its recorded receipt; an upgrade must not repeat that write using a newly interpreted name.

## Shared seams to inspect

| Boundary | Existing owner | Required distinction |
| --- | --- | --- |
| CLI argument capture | [`main.cpp`](../tools/sed-agent/main.cpp), `isLevelPathCommand` and native-model classification | Native Windows arguments for host paths; explicit command dispatch and native executable-relative data discovery. |
| MCP policy and staging | [`main.cpp`](../tools/sed-mcp/main.cpp): `checkReadable`, `checkWritable`, `workspaceRelativeArg`, `normalizeRequestPaths` and `ScratchFile` | Decode host paths before policy and I/O. Normalize only documented filesystem fields on a copy. Preserve original journal arguments. |
| Domain input and publication | [`agent_workspace.cpp`](../tools/sed-agent/agent_workspace.cpp): request readers, document readers, captured-file helpers and output writers | Use one native entry for admission, read/write, source recheck and receipt. Keep legacy defaults for consumers outside an explicitly migrated batch. |
| Loose resource policy | MCP's resource-read callback and the core resource locator | Avoid round-tripping an already-native path through a narrow string. Keep resource identifiers separate from the host file selected by the locator. |

Do not fix this by converting every string or changing a shared helper before its consumers agree on the encoding.
The existing `model-native-*` routes and the legacy `model-lighting-*` routes are separate implementations.

## Remaining command families

The [command registry](../tools/sed-agent/agent_schema.cpp) and MCP catalog supply the entry points below.
Each batch needs its own regression-first reproduction and qualification. The ordering favors bounded inspections
before publication, while leaving independent asset families available for parallel work.

### 1. Level inspection and dependencies

CLI/MCP pairs: `statistics` / `sed_statistics`; `workbench-query` / `sed_workbench_query`;
`arch-validate` / `sed_arch_validate`; `bindings` / `sed_bindings`; `dependencies` / `sed_dependencies`.

Inspect the input level, request file, ARCH `models[].path`, captured archives and template sources. Give intended
and mojibake-named inputs different content and verify the captured source. These inspections must create no
publication or journal writes. Preserve the dependency receipt's reversible
[`ascii-byte-escape-v1` encoding](dependency_inspection_design.md).

### 2. Archive and asset inspection

CLI/MCP pairs: `archive` / `sed_archive`; `dxf-inspect` / `sed_dxf_inspect`;
`cog-check` / `sed_cog_check`; `cog-compile` / `sed_cog_compile`;
`mat-inspect` / `sed_mat_inspect`; `model-hierarchy` / `sed_model_hierarchy`;
`key-inspect` / `sed_key_inspect`; `jo-ja-inspect` / `sed_jo_ja_inspect`.

Inspect host input filenames and KEY inspection's optional model filename. Distinguish source files by their
inventory or hash. Preserve archive member names and JO/JA target-entry selectors while changing only host file
resolution. JO/JA has its own capture path in [`agent_jo_ja.cpp`](../tools/sed-agent/agent_jo_ja.cpp).

### 3. Model and KEY workflows

CLI/MCP pairs: `model-hierarchy-edit` / `sed_model_hierarchy_edit`; `key-from-model` / `sed_key_from_model`;
`key-plan` / `sed_key_plan`; `key-apply` / `sed_key_apply`;
`model-lighting-inspect` / `sed_model_lighting_inspect`;
`model-lighting-plan` / `sed_model_lighting_plan`;
`model-lighting-apply` / `sed_model_lighting_apply`;
`model-import` / `sed_model_import`; `terrain-3do` / `sed_terrain_3do`.

Inspect source, request and destination files, import `source.path`, `materials[].matFile.path`, and the optional
`exportBaseColor` destination. Reopen the exact native output and compare reviewed bytes. When an import produces
both a model and a base-color image, check both destinations and their publication behavior.
[Model lighting](../tools/sed-agent/agent_model_lighting.cpp) requires its own migration; native-model tests do not cover it.

### 4. Material and palette authoring

CLI/MCP pairs: `mat-create` / `sed_mat_create`; `cmp-create` / `sed_cmp_create`;
`cmp-master-create` / `sed_cmp_master_create`; `mat-export-png` / `sed_mat_export_png`.

Inspect request/output paths, `cels[].path`, `palette.path`, `master.path`, `protectMaterials[].path`,
`protectArchives[].path`, and export's optional CMP filename. Distinct input images and CMPs must select the
intended bytes. Verify native output identity and UTF-8 receipts while retaining palette indices, material identifiers
and color-key semantics.

### 5. Packaging, conversion and export

CLI/MCP pairs: `convert` / `sed_convert`; `extract` / `sed_archive` with `entry`;
`archive-create` / `sed_archive_create`; `package-plan` / `sed_package_plan`;
`package-apply` / `sed_package_apply`; `prefab-capture` / `sed_prefab_capture`;
`export-dxf` / `sed_export_dxf`; `dxf-import` / `sed_dxf_import`;
`jo-ja-plan` / `sed_jo_ja_plan`; `jo-ja-apply` / `sed_jo_ja_apply`.

Inspect top-level files, package archives and stock archives, loose archive-entry sources, and JO/JA captured
source/palette/material/reimport inputs. Use ASCII logical archive entries under Unicode host directories so the
test distinguishes the two namespaces. Require exact native output entries and the existing output/manifest hashes.
A fresh occupied destination must refuse without creating a differently named sibling.

The legacy aliases in [package authoring](package_authoring.md) have different roles:
`archives[].fileName` is a host path, while `extraResources[].fileName` is a logical resource name.
Preserve their existing precedence and admission rules.

### 6. Snapshots and overview output

CLI/MCP pairs: `snapshot` / `sed_snapshot`; `overview` / `sed_overview`.

Inspect level/request files, the explicit snapshot output, overview's `request.output`, and captured archives.
Reopen the intended PNG and verify bytes and dimensions along with the source/resource identities.
Exercise overview with and without an output; its read-only form must remain read-only.

### 7. COG project and simulation workflows

CLI/MCP pairs: `assistant-script` / `sed_assistant_script`; `cog-run` / `sed_cog_run`;
`assistant-check` / `sed_assistant_check`.
Additional CLI entry points are `addCog`, `rebindCogReference`, `cog-templates` and `cog-template-write`.

Those four CLI commands have no standalone MCP aliases. The first two also exist as patch operations, with the
patch workflow's separate boundaries.

Inspect request/output files, nested `level.path`, `script.path`, `cogs[].script.path`, explicit project/library
directories and `RED_DATA_DIR`. Preserve script and set identifiers. Use a neutral working directory for installed
library discovery so a source-tree or current-directory fallback cannot hide a missing executable path.
Template-copy conflicts must leave every destination unchanged.

### 8. Launch planning and inspection worker

CLI/MCP pairs: `launch-plan` / `sed_launch_plan`; `native-inspect` / `sed_native_inspect`.

Inspect the launch request file, `gameDirectory`, `projectDirectory`, `executable`, worker input and
`workerConfig`, including the verified adjacent worker executable. Preserve existing executable-hash checks,
argument construction and Windows worker restrictions. `launch-plan` must continue to launch nothing.

## Nested resource paths

These can still affect an otherwise-corrected level route. Start with explicit captured-file fields, then qualify
loose-resource resolution separately:

- `resourceArchives[].path` and `templateSource.path`, at patch root or operation level.
- `operations[].prefabFile`, baking `resourceRoots[]` and `gobs[]`.
- ARCH `models[].path`, dependency/package `archives[].path`, package `archives[].fileName` and `stockArchives[]`.
- Bound-file inputs consumed by `readBoundFile`: image cels, palettes, master CMPs, protected materials,
  model sources/MAT files and loose archive-entry sources.
- JO/JA `source.path`, `palette.path`, `materials[].source.path`, `reimport.authoredArchive.path`, and captured
  fields within `reimport.baselineRecipe`.

Relevant domain seams include `captureResourceArchive`, template capture, bake resource roots, prefab loading and
`readBoundFile`, plus JO/JA's separate capture helper. Preserve the existing rule: normalize documented host fields
on a staging copy, retain original journal arguments and apply policy to the actual resource read.

## Identities that must stay separate

| Value | Interpretation |
| --- | --- |
| Archive container `path`, archive creation `entries[].path` | Host filesystem path. |
| Archive `entry`, archive creation `entries[].name` | Archive member identifier. |
| Package `archives[].fileName` | Legacy host-path alias. |
| Package `extraResources[].fileName` | Legacy logical-resource alias. |
| Package `episode.levelFileName` | Packaged level identifier, separate from the host source filename. |
| Model material names, template names, COG symbols, game aliases and JO/JA target entries | Engine or schema identifiers. |
| Dependency diagnostic identifiers | Reversible byte representation under `ascii-byte-escape-v1`. |

UTF-8 request validation, byte limits and valid JSON output are not evidence that Windows opened the intended
native filename. Keep the [dependency encoding specification](dependency_inspection_design.md) and actual host-path
qualification separate.

## Acceptance protocol for every batch

1. Establish ASCII controls and record code page 1252 for every Windows regression. Include accented, CJK and
   supplementary names with spaces; a UTF-8 system code page alone cannot establish this qualification.
2. Give intended and mojibake-named sources different bytes. Verify the actual captured content or hash.
3. For writers, start with an ASCII source and reviewed hashes where supported. Enumerate and reopen the exact native
   destination, compare its bytes/hash and validate the entire receipt as UTF-8.
4. Test fresh occupied destinations, wrong-hash refusal, Unicode workspace/TEMP/read-only roots, outside-workspace
   refusal and missing-file diagnostics. Keep occupied-output cases separate from earlier successful writes.
5. Preserve original-argument journal identity and historical replay without repeating completed writes.
6. Keep existing archive/game-identifier byte fixtures unchanged. A host-path correction must not silently normalize
   a resource name, palette index or reversible diagnostic identifier.
7. Run the focused regressions, the complete configured Release suite and the packaged-installation checks required
   for the claimed delivery. Record source identity, exact commands, skipped/failed checks and remaining target gates.

Update the owning workflow documentation and this work queue when a family lands. Qualification for one family
must not silently expand the claim for another.
