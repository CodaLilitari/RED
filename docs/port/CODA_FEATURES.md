# Coda feature inventory — SED-MotS-Net (`main`) for the Qt/C++ port

Status: definitive inventory for port planning. Compiled 2026-11-01 from direct
inspection of the Coda repository; no Coda file was modified and no C# code is
copied here. This document describes **behavior and contracts** to reimplement
originally in C++/Qt, informed by Coda's behavior. Use synthetic/original
fixtures only; do not port retail assets.

## Sources and method

| Item | Value |
| --- | --- |
| Coda repo (read-only) | `C:\Users\Andrei\Documents\ChatGPT\SED-MotS-Net`, branch `main` |
| Range | `dotnet-source/main..main` = **46 commits** |
| Base | `908d53d70d6b95784a92e9f1746ab5b13afcfdf0` — 2026-08-12 "Monsters and NPCs" |
| Head | `f15639e1260c574ef5f3a061fac6ff3fc7e26924` — 2026-09-30 "docs: prepare reviewed PLAY-01 release milestone" |
| Port target | This repo (`SED-Qt-Port`, branch `codalilitari/features-port`, `smlu/SED` develop lineage: `src/core`, `src/app`, CMake, tests) |
| Evidence rule | Every commit's diffstat was sampled (`git show --stat`); feature claims below were checked against HEAD docs and HEAD source headers, not titles alone. |

Method notes:

- "Defining commits" lists the commits that introduce or materially change the
  behavior. "Record" commits (acceptance/handoff notes, usually `docs/` only)
  are listed separately in the commit index (§4) and are not repeated as
  defining commits.
- Source paths are Coda `dotnet\` paths at HEAD. `A` = file added in the range,
  `M` = pre-existing file modified in the range (verified with
  `git diff --name-status dotnet-source/main..main -- dotnet/`).
- Test counts are `[Fact]`/`[Theory]` **method** counts in files added in the
  range, counted at HEAD. A `[Theory]` expands to multiple cases, so executed
  cases exceed method counts. Checkpoint suite totals quoted from Coda docs are
  labeled as such and were not re-run for this inventory.
- Where Coda states a contract, the citation gives the doc path and section.
  Section titles are HEAD titles.

## Range snapshot (measured)

| Measure | Value |
| --- | --- |
| Commits in range | 46 (2026-09-08 … 2026-09-30) |
| `dotnet/` files at base → HEAD | 217 → 404 |
| `dotnet/` entries changed in range | 263 (33 677 insertions, 938 deletions) |
| New test files in range | 77 (71 `Sed.Core.Tests` + 6 `Sed.Mcp.Tests`) |
| Test methods in those new files | 806 (688 Core + 118 MCP) |
| Solution | `dotnet/Sed.slnx`: 6 `src` projects, 2 `tests` projects, 19 `tools` projects at HEAD |
| Final checkpoint suite (Coda-reported, `.16`) | 1 594 Core + 271 MCP tests passing; see `docs/PROJECT_STATE.md` |

Coda layout referenced below: `dotnet/src/Sed.App` (Avalonia GUI),
`Sed.Core` (model/editing/lighting/query), `Sed.Formats` (JKL/JED/GOB/3DO/COG +
`Automation`), `Sed.Rendering` + `Sed.Rendering.Vulkan`, `Sed.Plugins`;
`dotnet/tests/Sed.Core.Tests`, `Sed.Mcp.Tests`; `dotnet/tools/Sed.Agent`,
`Sed.Mcp`, plus probe tools. Port equivalents: portable logic belongs in
`src/core` (`sed_core`, no Qt/OpenGL — see `docs/architecture.md`), GUI in
`src/app`, and headless tools as new executables; the port currently has only
`src/core` and `src/app`, so agent/MCP/engine harnesses are greenfield in the
port, not translations.

## Feature map

| ID | Feature | Defining commits (short) | Primary Coda area |
| --- | --- | --- | --- |
| F-00 | Preservation foundation & format fidelity | `5febeb3`, `85fa15e` | `Sed.Formats`, `Sed.Core` |
| F-01 | Headless agent CLI (`sed-agent`) | `5febeb3`\*, `a48b696`, `9d72f40` | `Sed.Formats/Automation`, `tools/Sed.Agent` |
| F-02 | Local MCP server (`sed-mcp`) | `ef53c33` (design), `1b784ae`, `ce093bd` | `tools/Sed.Mcp`, `tests/Sed.Mcp.Tests` |
| F-03 | MotS lighting: renderer + independent source lights (LIGHT-01) | `10177b8`, `f6100fe` | `Sed.Core/Lighting`, `Sed.Rendering*`, `SourceLightMetadataFormat` |
| F-04 | Transparency, cutouts & 3DO hierarchy | `e7507b2` | `Material`, `ThreeDo`, `Sed.Rendering*` |
| F-05 | Resource-aware ARCH validation (all LODs) | `9d1d316` | `Resources`, `ThreeDo`, `GuiResourceExport` |
| F-06 | JKL export integrity (GEO-01) | `9a1f2ce`, `8642887` | `Jkl` writer/parser, `Sector` colormap |
| F-07 | Geometry-bound spatial selection (SPATIAL-01) | `1fe296a` | `Sed.Core/Query`, `Automation/AgentSelections` |
| F-08 | Architectural-lighting authoring & baking (ARCH-01) | `adb0cae`, `8d04786` (profile) | `ArchitecturalLightingAuthoring`, `ArchitectureAuthoring*` |
| F-09 | Isolated engine capture (ENGINE-01) | `ed17c14`, `ea4d6f4` | `tools/*.py` (no `dotnet/` change) |
| F-10 | Guarded COG authoring, diagnostics & editing foundations (EDIT-01) | `8d04786` | `Cogs`, `LevelAssetDiagnostics`, `LightingDiagnostics` |
| F-11 | Playable-mission authoring & gameplay acceptance (PLAY-01) | `370a881`, `6533ff8`, `00e7741`, `c935187` | `SetThingFramesCommand`, `tools/*play*` |
| F-12 | Shared workbench: exact queries, UV batch, bounded arrays (TOOL-01) | `026b0fa` | `AgentQueryFilters`, `BatchSurfaceUvCommand`, `PlacementArray*` |
| F-13 | Dynamic vertex-color readiness | `5e9d137` (part) | `DynamicVertexColorReadiness`, `JklDynamicVertexColors` |
| F-14 | Portable updater & release feed | `5e9d137` (part), `062df81`, `49cfb59` | `Sed.Core/Updating`, `Sed.App` update windows |

\* `5febeb3` is the founding mega-commit for F-00 and also seeds F-01/F-03;
it is attributed per-file in F-00/F-01 below, not double-counted as a whole.

Record-only, infrastructure and release commits (no new user-visible behavior)
are grouped in §4.3 so that all 46 commits are accounted for exactly once as
primary, with secondary links noted where a commit serves two features.

---

## F-00 — Preservation foundation & format fidelity

- **Defining commits:** `5febeb351710730dd86ced1cc787bb07778cfffc`
  "Modernize MotS preservation, Windows editing and agent workflows" (198 files,
  75 619 insertions; includes the `docs/audit/` corpus manifests);
  `85fa15e9cfdcd25751e051a216ccf0d455a4064e` "Preserve signed UV zeros and
  expand historical MotS corpus coverage" (signed-zero UV fix in
  `GeoResourceWriter.cs` + corpus follow-ups).
- **Source:** `dotnet/src/Sed.Formats/Jkl/` (`JklParser.cs` M, `JklReader.cs` M,
  `JklWriter.cs` M, `JklDocument.cs` M, `GeoResourceWriter.cs` M,
  `ArchitecturalLightingFormat.cs` A, `ParameterRecords.cs` A);
  `dotnet/src/Sed.Formats/Jed/JedImporter.cs` (A, native `.jed` import);
  `dotnet/src/Sed.Formats/Gob/GobArchive.cs` (M);
  `dotnet/src/Sed.Formats/Game/` (`PreparedLevelOpen.cs` A,
  `DocumentRecovery.cs` A, `GameInstall.cs` M);
  `dotnet/src/Sed.Formats/Launching/` (`GameLauncher.cs` A, `TestEpisode.cs` A);
  `dotnet/src/Sed.Core/Editing/` (`LevelClipboard.cs` M with dependency closure,
  `CleaveSectorCommand.cs` M, `VertexLightComponentCommand.cs` A);
  `dotnet/src/Sed.Core/Validation/ConsistencyChecker.cs` (M),
  `CogReferenceRules.cs` (A); audit tooling
  `dotnet/tools/Sed.CommunityAudit/`, `Sed.MotsFormatAudit/` (both A).
- **Tests:** new files include `JklMotsPreservationTests.cs` (21 methods),
  `JklNumericRoundTripTests.cs` (3), `JedImporterTests.cs` (10),
  `MotsEditingPreservationTests.cs` (11), `ClipboardDependencyTests.cs` (10),
  `CleaveSectorSafetyTests.cs` (9), `EngineConsistencyTests.cs` (11),
  `GobBoundaryTests.cs` (4), `LaunchContractTests.cs` (21),
  `PreparedOpenTests.cs` (6), `AgentWorkspaceTests.cs` (11).
- **Docs:** `docs/audit/NET_MOTS_FORMAT_AUDIT.md`, `NET_NATIVE_JED_IMPORT.md`,
  `NET_ENGINE_CONTRACTS.md`, `SED_FORK_DELTA_AUDIT.md` (all added by `5febeb3`);
  `docs/AGENT_PROTOCOL.md` § "Bounded readback" (preservation warnings on native
  conversion).
- **Contract:** Opening, editing and re-saving a supported MotS/JK level —
  including native `.jed`/`.sed`/`.ned` projects via `JedImporter` — preserves
  data the editor does not model (unknown sections, signed-zero UVs, original
  numeric spellings where the format requires them) and refuses, rather than
  silently repairing, input it cannot represent safely. Clipboard, cleave and
  lighting edits operate through undoable Core commands shared by GUI and
  agents, so the GUI never has a private edit path.
- **Port note:** This is the substrate for every other feature. In the port it
  maps to `src/core` parsers/writers plus the existing document/undo model;
  port the *preservation invariants and refusal behavior* with original
  fixtures, not Coda's audit corpora (which reference historical files).

## F-01 — Headless agent CLI (`sed-agent`)

- **Defining commits:** `5febeb3` (seeds `AgentWorkspace.cs`,
  `AgentSchema.cs`, `tools/Sed.Agent/Program.cs`);
  `a48b696c66f68da3ae330dc7a8f716f0ef368030` "Improve agent readback, edit
  receipts and native source guards" (adds `AgentQueries.cs`,
  `AgentCommandCatalog.cs`, `AgentPlanGuard.cs`, `AgentRequestReader.cs`);
  `9d72f4053683e4e556e9c6247a31051fb0fd25e4` "Enable connected headless level
  design and diagnostic snapshots" (adds `AgentGeometryOperations.cs`,
  `AgentMaterialOperations.cs`, `AgentSnapshotRequest.cs`,
  `tools/Sed.Agent/AgentSnapshotRunner.cs`). Later features extend the same
  protocol; their operations are attributed to those features.
- **Source:** `dotnet/src/Sed.Formats/Automation/` (all A in range:
  `AgentWorkspace.cs`, `AgentSchema.cs`, `AgentCommandCatalog.cs`,
  `AgentQueries.cs`, `AgentPlanGuard.cs`, `AgentRequestReader.cs`, plus
  per-domain `Agent*Operations.cs` files added by F-03/F-07/F-08/F-10/F-12);
  executable `dotnet/tools/Sed.Agent/` (`Program.cs`, `AgentSnapshotRunner.cs`,
  `SnapshotResources.cs`, all A). Errors are typed:
  `AgentRequestException` / `AgentConflictException` /
  `AgentValidationException` in `AgentWorkspace.cs`, mapped to exit codes.
- **Tests:** `AgentWorkspaceTests.cs` (11), `AgentQueryTests.cs` (14),
  `AgentSafetyTests.cs` (16), `AgentTransportTests.cs` (5),
  `AgentSnapshotRequestTests.cs` (11); PowerShell harnesses
  `tools/Test-AgentWorkflow.ps1`, `tools/Test-AgentDesign.ps1` drive the real
  executable.
- **Docs:** `docs/AGENT_PROTOCOL.md` § "Commands" (full command table),
  § "Bounded readback", § "Example edit"; `docs/AGENT_WORKFLOWS.md`
  § "Read, plan, review, apply, read back". The protocol states explicitly:
  "This is a tool-neutral CLI and does not claim to implement MCP."
- **Contract:** A local JSON CLI (`help`, `schema`, `inspect`, `query`,
  `select`, `validate`, `plan`, `apply`, `new`, `convert`, `archive`,
  `extract`, `snapshot`, `launch-plan`, plus diagnostic commands added later)
  edits an isolated parsed document with the same Core commands as the GUI.
  `plan` never writes and returns the exact `outputSha256`; `apply` optionally
  requires that hash, rechecks inputs and atomically creates a **new** file —
  existing outputs are never overwritten, and numeric selectors are valid only
  for the exact `inputSha256`/`sourceRevision` they were read from.
- **Port note:** Highest-leverage port item: a `sed-agent`-equivalent tool over
  `sed_core` gives every later feature a headless test surface. Preserve the
  exit-code contract (0 success, 2 invalid request, 3 validation failure,
  4 changed input/stale target/existing output, 5 I/O failure) and the
  1 MiB request / bounded-page limits (`AgentWorkspace.MaxRequestBytes`).

---

## F-02 — Local MCP server (`sed-mcp`)

- **Defining commits:** `ef53c332fb7801ca0598d9522195fef0562128b2` "Refine
  MCP adapter boundaries" (docs-only design gate);
  `1b784ae6df3b941e7decd25d863eee3e54666f0e` "Add local MCP level design tools
  with guarded writes and verified previews" (53 files, 11 913 insertions;
  adds the whole `Sed.Mcp` tool + `Sed.Mcp.Tests` project);
  `ce093bdeb2c568b3717a1649775af9721e0a0ddc` "Correct MCP preview interruption
  guidance" (cancellation regression + guidance). Maintenance:
  `f85aec0deda71bff59435b061aed8a7fad4ff53c` (packaged-fixture capacity) and
  `e3379f233e98f3c5ed88c039ed8011bd5114d108` (interrupted-write journal
  observation in `tools/test_mcp_workflow.py`).
- **Source:** `dotnet/tools/Sed.Mcp/` (all A): `SedTools.cs` (transport
  adaptation only — "Domain parsing, geometry and writes remain shared with
  the CLI"), `ToolCatalog.cs`, `Program.cs`, `HostOptions.cs`,
  `BoundedInputStream.cs`, `Infrastructure/McpFilePolicy.cs`,
  `Infrastructure/OperationStore.cs`, `Infrastructure/SnapshotEvidence.cs`,
  `Infrastructure/SnapshotWorker.cs`. Registered in `dotnet/Sed.slnx` by
  `1b784ae`. Domain behavior stays in F-01's `AgentWorkspace`.
- **Tests:** `dotnet/tests/Sed.Mcp.Tests/` (all A, 118 methods):
  `SedToolsTests.cs` (28), `SnapshotWorkerTests.cs` (28),
  `OperationStoreTests.cs` (18), `SnapshotEvidenceTests.cs` (18),
  `McpFilePolicyTests.cs` (16), `BoundedInputStreamTests.cs` (10); plus
  `tools/test_mcp_workflow.py` (real stdio client). Coda-reported MCP-01 gate:
  1 329 Core + 265 MCP tests, 65 completed stdio calls + 2 interrupted-write
  probes (`docs/MCP_INTEGRATION.md`, "Final Windows package checkpoint").
- **Docs:** `docs/MCP_INTEGRATION.md` § "Tools and editing loop" (11-tool
  table), § "Run locally or configure Codex"; `docs/AGENT_DESIGN_ROADMAP.md`
  (adapter-boundary decision from `ef53c33`).
- **Contract:** A local stdio MCP server exposes eleven tools
  (`sed_discover`, `sed_query`, `sed_select`, `sed_diagnose`, `sed_plan`,
  `sed_apply`, `sed_new`, `sed_convert`, `sed_archive`, `sed_snapshot`,
  `sed_outcome`) over the F-01 domain layer, confined to a configured
  `--workspace` (writes only create new files there) and read-only
  `--resource-root` paths. Commits require the reviewed plan's
  `outputSha256` (and `resourceRevision` when resources were read) plus a
  one-intent `operationId`; after any interruption the caller must query
  `sed_outcome` instead of replaying blindly, and snapshots — which have no
  durable outcome record — must be inspected at their PNG path before retry.
- **Port note:** Port only after F-01 exists; the MCP layer itself is thin
  (file policy, operation journal, snapshot worker with PNG validation:
  chunk/CRC, pixel budget 2 097 152, dimension and pixel-hash checks in
  `SedTools.cs`). Do not let the port's MCP tools grow a second edit path.

## F-03 — MotS lighting: renderer + independent source lights (LIGHT-01)

- **Defining commits:** `10177b80cf8668f0e27db2ed230dec42779c5cee` "Add
  source-aligned MotS lighting and offscreen agent previews" (49 files;
  renderer, shaders, `LightCalculator` changes, `Sed.LightingProbe`);
  `f6100fe6f5cad1b9e7b57d3518bf2406996f4eaf` "Preserve independent MotS
  source-light authoring through GUI and agent workflows" (38 files; adds
  `SourceLightMetadataFormat.cs`, `AgentLightOperations.cs`,
  `LightFieldCommands.cs`, GUI `LightInspector` support).
- **Source:** `dotnet/src/Sed.Core/Lighting/LightCalculator.cs` (M — quadratic
  falloff `intensity · ((range − dist) / range)²`, facing/range/shadow rules
  documented in its header); `dotnet/src/Sed.Rendering/RenderScene.cs` (M),
  `SceneAssembler.cs` (M), `Mesh.cs` (M);
  `dotnet/src/Sed.Rendering.Vulkan/SceneRenderer.cs` (M) with checked-in GLSL +
  SPIR-V (`Shaders/mesh.vert`/`mesh.frag`, `ShaderHashes.props` A — build
  verifies SHA-256, see `docs/RENDERER_LIGHTING.md`);
  `dotnet/src/Sed.Formats/Jkl/SourceLightMetadataFormat.cs` (A — versioned
  `SED_SOURCE_LIGHTS` section); `dotnet/src/Sed.Core/Model/Light.cs` (M —
  nullable `RgbIntensity`, `LegacyRgbRange`); GUI
  `dotnet/src/Sed.App/Inspectors/LightInspector.cs` (M).
- **Tests:** `SceneLightingTests.cs` (22), `LightCalculatorTests.cs` (M,
  expanded), `SourceLightFormatTests.cs` (21),
  `IndependentSourceLightTests.cs` (11), `AgentSourceLightTests.cs` (11),
  `RendererLightingLayoutTests.cs` (7), `LegacyEditorLightTests.cs` (2);
  probes `Sed.LightingProbe`, `Sed.UiProbe/LightInspectorProbe.cs`.
  LIGHT-01 acceptance: 908-test suite + 71 headless UI checks
  (`docs/LIGHT-01.md`, § "Acceptance record").
- **Docs:** `docs/LIGHT-01.md` § "Field compatibility contract" (per-field
  read/edit/write rules); `docs/SOURCE_LIGHT_METADATA.md` § "Extension grammar
  and validation" and § "Canonical checksum bytes" (independent fixed vector,
  SHA-256 `63d045cfe6c98062d8cbd2daf5a37676af67ddab6698f2d5186f76bbad9bc1d0`);
  `docs/MOTS_LIGHTING_STATUS.md`; `docs/RENDERER_LIGHTING.md`.
- **Contract:** MotS lights carry an independent white intensity and RGB
  intensity (plus primary range and a retained legacy RGB range) through
  native import, JKL save/reopen, GUI and agent editing, undo/redo and copy —
  `null` RGB inherits white, while explicit zero or explicit equal values stay
  explicit, and omission vs. `null` in `setLight` mean preserve vs. remove.
  The additive `SED_SOURCE_LIGHTS` section is checksum-bound to the ordinary
  `LIGHTS` rows, so stale or edited-behind-its-back metadata is rejected
  rather than applied; other editors may discard the extension, which the
  docs state plainly is indistinguishable from a legacy file.
- **Port note:** Two separable slices: (a) renderer channel semantics
  (mono/RGB per-corner, CMP profiles) and (b) the metadata round-trip. Slice
  (b) is pure `src/core` text-format work and ports first; do not simplify
  the null/inherit distinction — it is the feature.

---

## F-04 — Transparency, cutouts & 3DO hierarchy preservation

- **Defining commits:** `e7507b2d7d8c724155222dfdd1360a616a0bb626` "Correct
  MotS cutouts and transparency and preserve 3DO hierarchy metadata" (30
  files, 1 212 insertions; adds `RenderDrawOrder.cs`,
  `Sed.TransparencyProbe`, `docs/audit/MOTS_TRANSPARENCY_CONTRACT.md`).
- **Source:** `dotnet/src/Sed.Formats/Material/MatFile.cs` (M — masking from
  `alpha_en & 1`, first-cel texture ID), `MaterialLibrary.cs` (M);
  `dotnet/src/Sed.Formats/ThreeDo/ThreeDoModel.cs` (M), `ThreeDoParser.cs` (M),
  `ThreeDoWriter.cs` (M — hierarchy metadata round-trip);
  `dotnet/src/Sed.Rendering/RenderDrawOrder.cs` (A),
  `RenderScene.cs` (M), `SceneAssembler.cs` (M);
  `dotnet/src/Sed.Rendering.Vulkan/SceneRenderer.cs` (M).
- **Tests:** `MatTransparencyTests.cs` (9), `ThreeDoHierarchyTests.cs` (5),
  `RenderDrawOrderTests.cs` (3); `dotnet/tools/Sed.TransparencyProbe/` renders
  independent framebuffer expectations offscreen (no window, no game assets).
- **Docs:** `docs/audit/MOTS_TRANSPARENCY_CONTRACT.md`;
  `docs/RENDERER_LIGHTING.md` (effective opacity 165/255 derived from stored
  90/255; masked/translucent index-zero discard; adjoined surfaces disable
  depth writes; per-frame deferred-triangle ordering).
- **Contract:** Masked/translucent textures discard palette index zero before
  colormap mapping while opaque index zero and solid color zero stay visible,
  and translucent draw order, blend and depth-write state follow the inspected
  engine behavior rather than material order. 3DO hierarchy metadata survives
  parse/write so a model that round-trips through the editor keeps its node
  structure. These are preview-fidelity rules; the docs explicitly do not
  claim transparency-table fidelity or pixel-identical clipping.
- **Port note:** Small, well-fixtured slice. Port the MAT flag rules and draw
  ordering into `src/core` material code + `src/app` renderer with original
  synthetic MAT/CMP fixtures; keep the stated non-claims.

## F-05 — Resource-aware ARCH validation (all LODs) & GUI export

- **Defining commits:** `9d1d3162a718423c864863b114a0f5b0b2fbd155` "Validate
  MotS architectural resources across LODs and editor exports" (34 files,
  2 178 insertions; adds `ArchitectureBindingValidator.cs`,
  `ThreeDoTopologyReader.cs`, `ExplicitArchiveResources.cs`,
  `ResourceManifest.cs`, `ResourceValidatedExport.cs`,
  `GuiResourceExport.cs`, `AgentResources.cs`).
- **Source:** `dotnet/src/Sed.Formats/ThreeDo/ArchitectureBindingValidator.cs`
  (A), `ThreeDoTopologyReader.cs` (A — bounded all-LOD topology),
  `ArchitectureModelFingerprint.cs` (A, from `10177b8`);
  `dotnet/src/Sed.Formats/Resources/` (`ExplicitArchiveResources.cs` A,
  `ResourceManifest.cs` A, `ResourceValidatedExport.cs` A);
  `dotnet/src/Sed.App/GuiResourceExport.cs` (A — GUI uses the same validated
  export as agents); `dotnet/src/Sed.Formats/Automation/AgentResources.cs` (A).
- **Tests:** `ArchitectureBindingValidatorTests.cs` (13),
  `GuiResourceExportTests.cs` (16), `AgentResourceTests.cs` (11),
  `ThreeDoTopologyTests.cs` (6), `ArchitectureModelFingerprintTests.cs` (4).
- **Docs:** `docs/ARCH_RESOURCE_VALIDATION_DESIGN.md`;
  `docs/AGENT_PROTOCOL.md` § "Architectural resources and reviewed exports"
  (1–16 explicit hash-bound GOB/GOO archives, first-archive precedence).
- **Contract:** When the caller supplies explicit hash-bound archives,
  validation checks each ARCH thing's model across **every** LOD — geoset-zero
  mesh/vertex counts exactly, lower LODs as array prefixes — plus thingflags
  `0x8`, ARCH indices, model names and the stored native fingerprint, and a
  lower LOD needing more slots is refused even if the preview LOD fits. GUI
  export and agent export share this validation, so a level cannot be
  exportable from one surface and invalid from the other; without explicit
  archives, no resource closure is claimed.
- **Port note:** Pure `src/core` validation + archive reading; needs the
  port's GOB reader to expose entry hashes. Keep "explicit archives only"
  semantics — no implicit game-directory discovery.

## F-06 — JKL export integrity: reference preservation, transactions, colormap identity (GEO-01)

- **Defining commits:** `9a1f2ce3ac6eebef1be970fc7cd65b67e9f248bc` "Preserve
  imported geometry references during JKL export" (adds
  `JklSourceGeometry.cs`, `JklGeometrySectionPolicy.cs`);
  `86428871e5b30cdbe4806c86efbfa77d7b01a6b2` "Make JKL export transactional
  and preserve colormap identities" (adds `SectorColormapProbe`, sector
  colormap slot retention in `Sector.cs`).
- **Source:** `dotnet/src/Sed.Formats/Jkl/JklSourceGeometry.cs` (A),
  `JklGeometrySectionPolicy.cs` (A), `JklWriter.cs` (M — non-mutating `Build`,
  `Save` commits registrations only after successful file replacement),
  `GeoResourceWriter.cs` (M), `JklParser.cs` (M), `JklDocument.cs` (M);
  `dotnet/src/Sed.Core/Model/Sector.cs` (M), `Surface.cs` (M);
  GUI `dotnet/src/Sed.App/Inspectors/SectorInspector.cs` (M — exact
  registered-slot selector).
- **Tests:** `JklExportReferenceTests.cs` (19),
  `AgentGeometryPreservationTests.cs` (9), `JklGeometryImportEdgeTests.cs` (8),
  `JklWriterTransactionTests.cs` (5), `SectorColormapIdentityTests.cs` (8).
  GEO-01 gate: 981-case suite, 91 headless UI checks
  (`docs/GEOMETRY_REFERENCE_PRESERVATION.md`, § "Acceptance evidence").
- **Docs:** `docs/GEOMETRY_REFERENCE_PRESERVATION.md` § "Reproduced failures"
  (sector-range/geometry mismatch reproduction), § "Required contract",
  § "Implemented boundaries" (adjoin distance field still emitted as `0.00` —
  byte-for-byte adjoin preservation explicitly not claimed).
- **Contract:** Regenerating GEORESOURCE after an edit preserves the imported
  file's original surface, vertex and adjoin slot identities — derived from
  parsed source labels, not sector traversal order — and appends new records
  without retargeting existing slots; anything that cannot fit that contract
  refuses with a specific diagnostic before any file is written. A failed
  generation or failed file promotion leaves material registrations and the
  previous file untouched, so a clean retry produces identical output;
  sector colormap choice is stored as an exact slot (with `-1` = name-only
  authoring state), never collapsed by ambiguous name matching.
- **Port note:** Directly relevant to the port's JKL writer. Port the
  identity-capture + refuse-don't-remap rule and the transactional
  build/save split; the writer must check retained source text against its
  captured hash before replacing a section.

## F-07 — Geometry-bound spatial selection (SPATIAL-01)

- **Defining commits:** `1fe296a65f331161e13a4dac20f3306d322d3b53` "Add
  geometry-bound spatial selection and outpost authoring workflow" (17 files,
  2 046 insertions; adds `SpatialSelection.cs`, `AgentSelections.cs`;
  acceptance fixture is the original eight-sector "Relay Outpost").
- **Source:** `dotnet/src/Sed.Core/Query/SpatialSelection.cs` (A — pure
  queries over closed, inward-wound convex sectors; bounds are broad-phase
  only, no mutation of cached normals/geometry);
  `dotnet/src/Sed.Formats/Automation/AgentSelections.cs` (A — token minting
  and per-operation token checks); protocol surface in
  `AgentWorkspace.cs`/`AgentSchema.cs` (M).
- **Tests:** `SpatialSelectionTests.cs` (18), `AgentSelectionTests.cs` (18);
  `tools/Test-AgentSpatial.ps1` (570 lines; stale/ambiguous controls, 52
  calls / 458 checks reported in `docs/PROJECT_STATE.md`).
- **Docs:** `docs/SPATIAL_SELECTION.md` § "Selection contract" and
  § "Bound targets and editing" (token contents, supported operations,
  invalidation rules); `docs/AGENT_PROTOCOL.md` § "Spatial targets".
- **Contract:** `select` finds a sector by an interior world point, or a
  surface by outward direction (defaults: distance tolerance `1e-7`, angular
  `0.1°`), and returns a token **only** for a unique match — `none` and
  `ambiguous` are successful read-only results with a null token, never a
  guessed nearest match. The token binds source revision plus the full
  containing sector's geometry, so any geometry change invalidates it (stale
  tokens fail with exit 4 conflict), while material/UV/light metadata edits
  may reuse it within one batch; resolved addresses are stored in the plan
  receipt and predicates are never rerun after editing.
- **Port note:** Self-contained `src/core` geometry (`SpatialSelection` +
  token digest) with no renderer dependency. Requires the port's sector model
  to guarantee the closed/convex/planar preconditions or to report the same
  "cannot classify safely" diagnostic instead of a bounding-box match.

---

## F-08 — Architectural-lighting authoring & baking (ARCH-01)

- **Defining commits:** `adb0caeab1f6a4f4f089cd1df94bd7dcc2ce9441` "Add
  resource-bound architectural lighting authoring" (45 files, 2 277
  insertions; adds `ArchitecturalLightingAuthoring.cs`,
  `ArchitectureAuthoringMetadataFormat.cs`, `ArchitectureAuthoringGeometry.cs`,
  `ArchitectureAuthoringService.cs`, `ReplaceThingArchitectureCommand.cs`,
  GUI `ArchitectureInspector`);
  `8d047860c576021ea3da22743b97f5e27e15c9cc` (EDIT-01) adds the opt-in
  `source-points-world-occluded-v1` profile to the same service.
- **Source:** `dotnet/src/Sed.Core/Lighting/ArchitecturalLightingAuthoring.cs`
  (A), `dotnet/src/Sed.Core/Model/ArchitecturalLighting.cs` (A in `5febeb3`,
  extended here); `dotnet/src/Sed.Formats/Jkl/ArchitectureAuthoringMetadataFormat.cs`
  (A — versioned `SED_ARCH_AUTHORING` extension), `ArchitecturalLightingFormat.cs`
  (A in `5febeb3`); `dotnet/src/Sed.Formats/ThreeDo/ArchitectureAuthoringGeometry.cs`
  (A); `dotnet/src/Sed.Formats/Resources/ArchitectureAuthoringService.cs` (A);
  `dotnet/src/Sed.Core/Editing/ReplaceThingArchitectureCommand.cs` (A),
  `ThingArchitectureCommand.cs` (A in `5febeb3`); GUI
  `dotnet/src/Sed.App/Inspectors/ArchitectureInspector.cs` (A); agent
  `AgentArchitectureOperations.cs` (A).
- **Tests:** `ArchitectureAuthoringTests.cs` (14),
  `ArchitectureAuthoringFormatTests.cs` (17),
  `AgentArchitectureAuthoringTests.cs` (7), `SceneLightingTests.cs` (M,
  +81 lines in `adb0cae`); world-occlusion: `WorldOccludedArchitectureTests.cs`
  (10), `WorldOccludedArchitectureFormatTests.cs` (6).
- **Docs:** `docs/ARCHITECTURE_AUTHORING.md` § "Actions" (bind / reset-rebind /
  bake / unbind), § "Sampling profile and boundaries", § "Provenance and
  status" (computed status, strict versioned extension).
- **Contract:** Each ARCH thing can bind a private copy of existing samples,
  reset to a zeroed allocation for the current model, bake from editor source
  lights, or unbind — every replacement appends a new object and touches only
  the selected thing, with undo restoring the exact prior binding and redo
  replaying captured samples. Baking is an explicit editor recipe
  (`source-points-v1`: per-vertex `(1 − d/r)²` falloff over the primary range,
  independent mono/RGB channels, no occlusion/normals/ambient), and EDIT-01's
  `source-points-world-occluded-v1` additionally rejects light/sample pairs
  blocked by world polygons via the shared ray predicate (open adjoins pass
  unless `BlockLight` is set); neither profile is presented as a recovered
  retail baker.
- **Port note:** Depends on F-05 validation and F-03 lights. Note the bake
  limits (single geoset, valid acyclic hierarchy, ≤4 096 nodes, depth ≤128)
  and that provenance ("origin remains unknown" for bound imported samples)
  must survive — do not upgrade bound samples to "verified bakes".

## F-09 — Isolated engine capture & lighting acceptance (ENGINE-01)

- **Defining commits:** `ed17c14a6750e397f8ee6f6b8cc057bd2a74f6b6` "Add
  isolated engine capture and architectural lighting acceptance tools" (16
  files, 2 742 insertions — all Python `tools/`, no `dotnet/` change);
  `ea4d6f4a8c39501198064a52264317b16e5741e0` "Validate integrated engine
  world-state capture receipts" (launcher + receipt validation).
- **Source:** `tools/invoke_engine_capture.py` (612 lines), 
  `new_engine_lighting_fixture.py`, `compare_engine_lighting.py`,
  `engine_resource_closure.py`, `test_engine_lighting.py`; tests under
  `tools/tests/` and `tools/test_engine_lighting*.py`. Requires an engine
  implementing `sed-engine-capture-v1` (OpenMotS capture build); "an ordinary
  retail executable is not a substitute" (`docs/ENGINE_CAPTURE.md`).
- **Tests:** Python `unittest` files `test_invoke_engine_capture.py`,
  `test_compare_engine_lighting.py`, `test_engine_lighting*.py` (fixture +
  workflow). No `Sed.Core.Tests` files.
- **Docs:** `docs/ENGINE_CAPTURE.md` § "Tools and prerequisites",
  § "Isolation and receipts", § "Controls and interpretation" (claim limited
  to "an observable RGB differential in this static fixture").
- **Contract:** The harness stages one explicit, hash-manifested scenario
  into a new private directory, launches the capture engine hidden/muted with
  a fixed camera and finite deadline, and accepts only on exit zero plus an
  exact P6 RGB8 image, exclusive new outputs and unchanged inputs — a visible
  or foreground owned window terminates the run. Capture-v1's camera fields
  echo the request, so `effectiveCameraObserved` and
  `runtimeResourceResolutionObserved` remain `false` in receipts; the
  integrated world-state extension (map, mode, player model/soundclass) is
  type/bounds-checked when present and refused when partial.
- **Port note:** Language-agnostic harness pattern (manifest + staging hashes
  + receipt discipline), reusable from C++/Python in the port. It validates
  the port only if the same capture-capable engine build is available;
  otherwise mark engine acceptance unverified rather than substituting
  screenshots.

## F-10 — Guarded COG authoring, diagnostics & editing foundations (EDIT-01)

- **Defining commits:** `8d047860c576021ea3da22743b97f5e27e15c9cc` "Add
  guarded COG authoring, diagnostics and world-occluded MotS baking" (70
  files, 4 899 insertions). This single commit also carries the F-08
  world-occluded profile and the KEY/3DO-offset corrections; its COG,
  diagnostics and alignment parts are F-10.
- **Source:** `dotnet/src/Sed.Formats/Cogs/CogBindings.cs` (A — typed binding
  model), `CogScript.cs` (M), `CogValidation.cs` (A in `5febeb3`, M here);
  `dotnet/src/Sed.Formats/Automation/AgentCogOperations.cs` (A),
  `AgentAssetDiagnostics.cs` (A), `AgentLightingDiagnostics.cs` (A);
  `dotnet/src/Sed.Formats/Resources/LevelAssetDiagnostics.cs` (A);
  `dotnet/src/Sed.Core/Lighting/LightingDiagnostics.cs` (A — per-light
  facing/range/shadow contribution counts), `dotnet/src/Sed.Core/Query/ThingContainment.cs`
  (A), `dotnet/src/Sed.Core/Editing/AlignTextureCommand.cs` (M — neighbor
  texture alignment); GUI `dotnet/src/Sed.App/CogEditorWindow.cs` (M).
- **Tests:** `CogBindingsTests.cs` (13), `CogSchemaTests.cs` (5),
  `CogSchemaResolutionTests.cs` (8), `AgentCogBindingTests.cs` (6),
  `LevelAssetDiagnosticsTests.cs` (13), `LightingDiagnosticsTests.cs` (20),
  `ThingContainmentTests.cs` (12), `AgentTextureAlignmentTests.cs` (5),
  `AlignTextureEdgeIdentityTests.cs` (7), `ThreeDoInsertOffsetTests.cs` (4);
  harness `tools/Test-AgentEditing.ps1`.
- **Docs:** `docs/EDITING_FOUNDATIONS.md` § "Delivery phases" and
  § "Agent contracts" (referenced from `docs/AGENT_PROTOCOL.md`:
  "`cogs` exposes raw placed script names and values; use `bindings` to
  obtain types, never infer them from integers").
- **Contract:** COG references are edited only through typed bindings
  resolved against the actual script schema and hash-bound archives — no
  guessed defaults, no COG execution in the editor, and appends are refused
  when an existing placed COG lacks a complete schema. Read-only diagnostics
  (asset/dependency scan with exact entry receipts, per-light bake
  explanations, thing containment preserving overlap/boundary ambiguity,
  declared-capacity checks) report unknown/unverified states explicitly
  instead of inferring engine limits or rewriting data.
- **Port note:** Split into (a) `src/core` COG schema/binding model, (b)
  diagnostics (pure queries, easy wins), (c) GUI cog editor. The append
  guard (complete-schema requirement, canonical `-1` nulls) is a correctness
  rule, not UI polish — port it with the model.

## F-11 — Playable-mission authoring & hidden gameplay acceptance (PLAY-01)

- **Defining commits:** `370a881380e4ecaa4609d48a5cbdd0f18dcd071b` "Add
  reviewed MotS mission authoring and hidden gameplay acceptance" (adds
  `SetThingFramesCommand.cs`, `AgentThingFrameOperations.cs`,
  `tools/new_playable_fixture.py`, `tools/engine_play_contract.py`);
  `6533ff826b95f6fece1425595ea4de105aaba001` "Bind gameplay motion checks to
  observed wall time"; `00e7741990aa2b3e48e938136da0a383c5241782` "Require
  observed causal witnesses for playable mission acceptance";
  `c935187e4ff32b3e0fd79f07de17b84e22dd0160` "Bind PLAY objective completion
  to its observed phase transition" (oracle correction; tests only).
- **Source:** `dotnet/src/Sed.Core/Editing/SetThingFramesCommand.cs` (A),
  `CogCommands.cs` (M); `dotnet/src/Sed.Formats/Automation/AgentThingFrameOperations.cs`
  (A), `AgentCogOperations.cs` (M — `addCog` with reviewed script digest);
  acceptance in Python: `tools/engine_play_contract.py`,
  `tools/test_playable_workflow.py`, `tools/tests/test_engine_play_contract.py`.
  Runtime is the separately built OpenMotS executable via F-09's launch path.
- **Tests:** `PlayableAuthoringTests.cs` (12),
  `PlayableAuthoringCorrectionTests.cs` (12),
  `PlayableSurfaceBindingTests.cs` (2); Python contract tests (57 primary +
  32 launcher + 9 topology reported in `docs/PROJECT_STATE.md` after
  `c935187`). Fixture: original four-room corridor mission (gate + path mover
  + two goals), authored entirely through MCP tool operations.
- **Docs:** `docs/PLAYABLE_AUTHORING.md` § "Shared authoring scope"
  (`addCog`, `setThingFrames`), § "Runtime contract", § "Acceptance to run"
  (seven gates including blocked-without-activation and wrong-objective
  negative controls).
- **Contract:** A mission is accepted only on observed causal evidence from
  real play — player activation through ordinary controls, real path-mover
  motion bound to wall-clock time, portal crossing tied to the mover's
  ARRIVED dispatch, and goal inventory transitions (`1 -> 3` in the same
  adjacent sample pair as phase `2 -> 3`, dispatch inside that pair's time
  bounds) — never on echoed requests, scheduled-but-unconsumed inputs, a COG
  latch alone, or a screenshot. Controls must never directly send a COG
  message, move the gate, set inventory or teleport; negative controls
  (no activation, wrong objective, barrier behind gate, pre-completed goal)
  must all fail acceptance.
- **Port note:** Authoring ops (`addCog` guard, `setThingFrames`) port with
  F-10; the acceptance oracle is a separate Python checker and should be
  ported as-is conceptually. Do not weaken witnesses to make runs pass —
  `c935187` exists because delayed/reverted completion previously passed.

---

## F-12 — Shared workbench: exact queries, UV batch, bounded arrays (TOOL-01)

- **Defining commits:** `026b0fa7670e653de80fa4990b81f549ac04badf` "Add
  shared UV workbench, exact queries and bounded placement arrays" (39 files,
  5 616 insertions; adds `AgentQueryFilters.cs`, `BatchSurfaceUvCommand.cs`,
  `PlacementArrayCommands.cs`/`PlacementArrayLayout.cs`, GUI
  `TextureWorkbenchWindow.cs`/`ArrayPlacementWindow.cs`). Research input:
  `docs/audit/JED_SED_PLUGIN_AUDIT_20260930.md` (45 legacy plug-in families —
  behavior survey, not code to copy).
- **Source:** `dotnet/src/Sed.Formats/Automation/AgentQueryFilters.cs` (A),
  `AgentBatchUvOperations.cs` (A), `AgentArrayOperations.cs` (A);
  `dotnet/src/Sed.Core/Editing/BatchSurfaceUvCommand.cs` (A),
  `PlacementArrayCommands.cs` (A), `PlacementArrayLayout.cs` (A),
  `dotnet/src/Sed.Formats/Cogs/CogThingAppendGuard.cs` (A);
  GUI `dotnet/src/Sed.App/TextureWorkbenchWindow.cs` (A),
  `ArrayPlacementWindow.cs` (A); probes `TextureWorkbenchProbe.cs`,
  `ArrayPlacementProbe.cs` (A).
- **Tests:** `BatchSurfaceUvTests.cs` (15), `AgentBatchUvTests.cs` (6),
  `PlacementArrayTests.cs` (13), `AgentPlacementArrayTests.cs` (5),
  `AgentArrayReferenceTests.cs` (2), `PlacementArrayCapacityFormatTests.cs` (2),
  `AgentQueryFilterTests.cs` (11), `CogThingAppendGuardTests.cs` (12);
  `tools/test_tool_workbench.py` (real CLI, no GUI/game). TOOL-01 gate
  (Coda-reported): 1 517 Core + 270 MCP tests, 149 headless UI checks,
  132 CLI calls / 1 099 checks / 14 expected refusals (`docs/PROJECT_STATE.md`).
- **Docs:** `docs/TOOL_WORKBENCH.md` § "Exact query filters" (per-collection
  filter fields and mask semantics), § "Shared UV operations" (six projection
  bases, transform order, 256-face / 4 096-corner limits), § "New thing and
  light arrays" (linear/grid/radial layouts, 256-per-array / 1 024-per-patch
  limits). Explicit non-goals there: no historical DLL loading, no
  reference-bearing fragment cloning, no stairs/cutscene generation.
- **Contract:** One shared command serves GUI dialog, CLI and MCP for each
  operation: exact-match query filters (AND-combined with substring search,
  ordinal case-insensitive, paginating *matching* rows while retaining
  original indices), multi-surface UV projection/transform as a single
  undoable preflighted step, and deterministic arrays of **new** things or
  lights with declared-capacity and single-sector containment checks.
  New things never copy an existing object's COG values or ARCH binding, name
  collisions (`namePrefix_000`…`_255`) refuse, and radial layout does not
  implicitly rotate things toward the center.
- **Port note:** Clean `src/core` commands + thin `src/app` dialogs; the GUI
  must call the same commands (Coda's dialogs capture the current selection
  and apply as one undo step). Keep the numeric bounds in the schema — they
  are refusal contracts, not tuning knobs.

## F-13 — Dynamic vertex-color readiness

- **Defining commits:** `5e9d13746f28705a102a36cf0c912886415005b4` "Add
  portable updates, dynamic color readiness and a research-driven Codex
  workflow" (dynamic-color part: adds `DynamicVertexColorReadiness.cs`,
  `JklDynamicVertexColors.cs`, `AgentDynamicVertexColors.cs`, GUI
  `DynamicVertexColorsWindow.cs`).
- **Source:** `dotnet/src/Sed.Core/Lighting/DynamicVertexColorReadiness.cs`
  (A — pure evaluator over final `float32` samples: `Retained` / `Compacted` /
  `Scalar` / `Unknown` / `Invalid`); `dotnet/src/Sed.Formats/Jkl/JklDynamicVertexColors.cs`
  (A — evaluates the exact text the editor would save);
  `dotnet/src/Sed.Formats/Automation/AgentDynamicVertexColors.cs` (A);
  surfaces: CLI `vertex-colors` command, MCP `sed_diagnose kind:"vertexColors"`,
  GUI Tools → Check Consistency entry.
- **Tests:** `DynamicVertexColorTests.cs` (16); MCP coverage via
  `SedToolsTests.cs` (M in `5e9d137`).
- **Docs:** `docs/DYNAMIC_VERTEX_COLORS.md` ("What the result means" table:
  `4N` rows compact in the inspected retail loader iff every corner has
  `W == R == G`; blue never affects compaction; scalar `N` rows are retail
  `Unknown`).
- **Contract:** A read-only, paginated diagnostic reports, per surface,
  whether the exact serialized bytes the editor would save retain RGB storage
  in the inspected retail loader and in current OpenMotS — using finite
  `float32` equality with no epsilon or clamping, and keeping `Unknown` where
  decimal→`float32` conversion paths disagree. It changes no values, sets no
  flags and inserts no COGs; normal patch plans carry the same aggregate
  result bound to the plan's `outputSha256` so the reviewed bytes are the
  assessed bytes.
- **Port note:** Tiny pure function + serialization hook; port early as a
  `src/core` diagnostic. The subtle part is assessing *proposed serialized*
  rows (including retained `N`/`4N`/mixed layouts in unchanged sections),
  not in-memory colors.

## F-14 — Portable updater & release feed

- **Defining commits:** `5e9d137` (updater part: adds
  `dotnet/src/Sed.Core/Updating/PortableUpdateService.cs`,
  `UpdateManifest.cs`, GUI `UpdateWindow.cs`/`MainWindow.Updates.cs`/
  `UpdateHandoff.cs`/`EditorUpdater.cs`, `tools/build_update_feed.py`);
  `062df817832012a49484b75ae2b20810f3b01d70` "Make source release archives
  independent of Windows Git defaults" (`Package-Windows.ps1`);
  `49cfb59fc5507c6920aaa2257aa664214d105239` "Publish the verified SED .16
  preview update feed" (`updates/sed-releases.json`).
- **Source:** `dotnet/src/Sed.Core/Updating/` (`PortableUpdateService.cs` —
  "Never edits the running installation", `UpdateManifest.cs` — version/
  channel selection for the `0.4.0-mots.N-dev` family); GUI in
  `dotnet/src/Sed.App/`; headless verifier `dotnet/tools/Sed.UpdateProbe/`
  (references only `Sed.Core`); feed builder `tools/build_update_feed.py`.
- **Tests:** `PortableUpdateTests.cs` (16 — version/channel selection,
  malformed manifests, redirect/path policy, hash/size failures, archive
  attacks, staged tampering); `Sed.UiProbe/UpdateRecoveryProbe.cs`.
- **Docs:** `docs/UPDATING.md` § "Installation and recovery" and § "Feed and
  package contract" (feed ≤256 KiB / 100 releases, package ≤1 GiB / 20 000
  entries, per-file receipts, URL allowlist, "The feed is not independently
  signed" stated explicitly).
- **Contract:** The editor checks an allowlisted HTTPS feed in the background
  (24 h cache), never offers equal versions or downgrades, and on user action
  stages a verified **new portable directory beside** the running copy —
  never overwriting it — with per-file hash receipts rechecked at launch.
  "Save recovery and restart" hands off only after the new process
  acknowledges the expected version and the exact recovery-file SHA-256 with
  the recovery document open; any failure leaves the old editor running, and
  the old directory remains the manual rollback.
- **Port note:** Lowest priority for the port (Windows-x64 portable layout,
  Qt deployment differs). If ported, keep the refuse-equal-version,
  no-overwrite and receipt-recheck rules; do not add in-place replacement.

---

## Cross-cutting invariants (apply to every feature above)

These recur in Coda docs and code headers; treat them as port requirements,
not per-feature options:

1. **Plan/apply separation.** Planning mutates only an isolated document and
   writes nothing; committing recomputes, optionally binds to the reviewed
   `outputSha256` (+ `resourceRevision`/`sourceRevision` where applicable),
   and creates a new file atomically. No overwrite flag exists anywhere.
2. **Revision binding.** Numeric selectors and spatial tokens are valid only
   for the exact `inputSha256` (JKL) or `sourceRevision` (native projects,
   which also binds companion template files) they were issued from.
3. **Bounded everything.** Requests (1 MiB), pages (default 50, max 200,
   1 MiB response cap), operations (≤1 000 per patch in the CLI; ≤128 via
   MCP), archives, feeds and bakes all have explicit limits; exceeding them
   refuses rather than truncating silently.
4. **Refuse, don't guess.** Ambiguous selection, stale tokens, incomplete
   COG schemas, unsupported geometry, unknown sections and unrepresentable
   exports all fail with a specific diagnostic (exit 2/3/4 discipline in
   F-01). Successful-looking fallback values are treated as defects.
5. **Unknown stays unknown.** Diagnostics preserve `Unknown`/`unavailable`/
   ambiguous states (F-10, F-13) instead of collapsing them to zero/false.
6. **Shared commands.** GUI, `sed-agent` and `sed-mcp` call the same Core
   edit commands; transport layers contain no domain logic. File-based agent
   edits do not synchronize an unsaved GUI document (stated in
   `docs/TOOL_WORKBENCH.md` and `docs/MCP_INTEGRATION.md`).
7. **Synthetic fixtures.** Acceptance fixtures (Relay Outpost, four-room
   mission, lighting/UV fixtures) are original/synthetic. Coda's historical
   corpus manifests under `docs/audit/` are evidence records, not port inputs.

## What Coda does *not* claim (preserve these limits in the port)

- No retail/gameplay parity: editor snapshots and captures are static or
  bounded-fixture evidence (`docs/ENGINE_CAPTURE.md`, `docs/PLAYABLE_AUTHORING.md`).
- No recovered retail baker: ARCH bake profiles are named editor recipes
  (`docs/ARCHITECTURE_AUTHORING.md`).
- Extension sections (`SED_SOURCE_LIGHTS`, `SED_ARCH_AUTHORING`) may be
  stripped by other editors; absence is indistinguishable from legacy files
  (`docs/SOURCE_LIGHT_METADATA.md`, § "Evidence and loss boundaries").
- The update feed is HTTPS + host-control trust only, not signed
  (`docs/UPDATING.md`).
- MCP/CLI results do not prove a model-driven design session occurred
  (`docs/MCP_INTEGRATION.md`: fixtures are "a reproducible client, not
  evidence that a native Codex MCP session designed the level" — see also
  `docs/PLAYABLE_AUTHORING.md`).

---

## Commit index — all 46 commits accounted for

Chronological order. "Primary" is the feature the commit defines;
"Record/infra" commits change no user-visible behavior. Full hashes are in the
feature sections for defining commits; record commits are identified by short
hash + title (verify with `git log --oneline dotnet-source/main..main`).

| # | Commit | Primary | Note |
| --- | --- | --- | --- |
| 1 | `5febeb3` Modernize MotS preservation… | F-00 (+F-01/F-03 seed) | Founding mega-commit; `docs/audit/` corpora |
| 2 | `85fa15e` Preserve signed UV zeros… | F-00 | `GeoResourceWriter` fix + corpus |
| 3 | `d5de2b7` Document private GitHub hosting… | Infra | `docs/GITHUB_HANDOFF.md` only |
| 4 | `a48b696` Improve agent readback… | F-01 | Queries, receipts, guards |
| 5 | `9d72f40` Enable connected headless level design… | F-01 | Geometry/material ops, snapshots |
| 6 | `10177b8` Add source-aligned MotS lighting… | F-03 | Renderer + offscreen previews |
| 7 | `9de27d3` Update Windows guide… | F-03 record | `WINDOWS_QUICKSTART.md` only |
| 8 | `e7507b2` Correct MotS cutouts… | F-04 | Transparency + 3DO hierarchy |
| 9 | `9d1d316` Validate MotS architectural resources… | F-05 | All-LOD ARCH validation |
| 10 | `f6100fe` Preserve independent MotS source-light… | F-03 | LIGHT-01 authoring |
| 11 | `11c9ebc` Record LIGHT-01 acceptance… | F-03 record | Docs only |
| 12 | `2a1461a` Normalize checkpoint line endings | Infra | `PROJECT_STATE.md` only |
| 13 | `65e94af` Update public project location… | Infra | Repo-location docs only |
| 14 | `9a1f2ce` Preserve imported geometry references… | F-06 | GEO-01 part 1 |
| 15 | `8642887` Make JKL export transactional… | F-06 | GEO-01 part 2 + colormap |
| 16 | `34a8cd3` Record verified GEO-01 package… | F-06 record | Docs only |
| 17 | `1fe296a` Add geometry-bound spatial selection… | F-07 | Relay Outpost fixture |
| 18 | `896c808` Record completed spatial authoring… | F-07 record | Docs only |
| 19 | `adb0cae` Add resource-bound architectural lighting… | F-08 | ARCH-01 authoring |
| 20 | `bcba14a` Record verified ARCH-01 package… | F-08 record | Docs only |
| 21 | `ed17c14` Add isolated engine capture… | F-09 | Python harness, no `dotnet/` |
| 22 | `ea4d6f4` Validate integrated engine world-state… | F-09 | Receipt validation |
| 23 | `c6aabe6` Record verified ENGINE-01 runtime… | F-09 record | Docs only |
| 24 | `8d04786` Add guarded COG authoring… | F-10 (+F-08 profile) | EDIT-01; 70 files |
| 25 | `11550ee` Record verified EDIT-01 delivery… | F-10 record | Docs + MCP direction |
| 26 | `ef53c33` Refine MCP adapter boundaries… | F-02 design | Docs only |
| 27 | `1b784ae` Add local MCP level design tools… | F-02 | `Sed.Mcp` + `Sed.Mcp.Tests` |
| 28 | `ce093bd` Correct MCP preview interruption… | F-02 | Cancellation regression |
| 29 | `b0278c2` Record completed MCP milestone… | F-02 record | Docs only |
| 30 | `dfd7d24` Publish Windows prerelease links… | Release | Links/record only |
| 31 | `370a881` Add reviewed MotS mission authoring… | F-11 | PLAY-01 authoring |
| 32 | `6533ff8` Bind gameplay motion checks… | F-11 | Wall-time oracle fix |
| 33 | `00e7741` Require observed causal witnesses… | F-11 | Oracle hardening |
| 34 | `83b158c` Record paused PLAY-01 handoff… | F-11 record | Docs only |
| 35 | `026b0fa` Add shared UV workbench… | F-12 | TOOL-01 first slice |
| 36 | `4ccabe0` Preserve original test fixture line endings | Infra | `.gitattributes` |
| 37 | `f85aec0` Preserve declared capacity… | F-02 maint. | Packaged MCP fixture fix |
| 38 | `4a9eb3c` Record independently audited TOOL-01… | F-12 record | Docs only |
| 39 | `5e9d137` Add portable updates, dynamic color… | F-13 + F-14 | Also adds Codex research skill (not an editor feature) |
| 40 | `062df81` Make source release archives independent… | F-14 | Packaging fix |
| 41 | `e3379f2` Observe interrupted MCP writes… | F-02 maint. | Journal-API probe fix |
| 42 | `49cfb59` Publish the verified SED .16 preview feed | F-14 | `updates/sed-releases.json` |
| 43 | `9c735bf` Record published .16 milestone… | F-14 record | Docs + release notes |
| 44 | `c935187` Bind PLAY objective completion… | F-11 | Oracle correction (tests) |
| 45 | `a8adf5f` Record resumed PLAY review… | F-11 record | Docs only |
| 46 | `f15639e` docs: prepare reviewed PLAY-01 milestone | F-11 record | `.17` notes prep; HEAD |

Excluded from features by design: `docs/audit/` historical manifests, license
inventories (`licenses/`), and the `.agents/skills/` research workflow in
`5e9d137` are evidence/tooling, not editor behavior. `tools/Package-Windows.ps1`,
`Build-Net.ps1`, `Verify-Distribution.ps1` changes ride along with their
features as release plumbing.

## Suggested port order (dependency-driven, not milestone order)

1. **F-00 + F-06** — preservation and transactional export in `src/core`;
   everything else writes through this writer.
2. **F-01** — headless CLI over the port's document model; unlocks headless
   tests for all later work.
3. **F-07, F-12, F-13** — self-contained Core queries/commands/diagnostics.
4. **F-03(b), F-05, F-08, F-10** — lighting metadata, ARCH validation/authoring,
   COG bindings (need F-01 surfaces for acceptance).
5. **F-03(a), F-04** — renderer channel/transparency semantics in `src/app`.
6. **F-02** — MCP adapter over F-01 (transport only).
7. **F-09, F-11** — engine harnesses (need the capture-capable engine build).
8. **F-14** — updater last; Qt deployment model differs from Coda's portable ZIP.

For each ported feature, recreate Coda's *negative controls* with original
fixtures (ambiguous selection, stale token, incomplete COG schema,
wrong-objective mission, equal-version update refusal, etc.). A port that
only reproduces happy paths has not reproduced the feature.

## Appendix — how to re-verify this inventory

```powershell
# Commit list and per-commit diffstats (read-only against the Coda repo)
git -C C:\Users\Andrei\Documents\ChatGPT\SED-MotS-Net log --oneline dotnet-source/main..main
git -C C:\Users\Andrei\Documents\ChatGPT\SED-MotS-Net show --stat <hash>
# Added-vs-modified attribution and HEAD trees
git -C C:\Users\Andrei\Documents\ChatGPT\SED-MotS-Net diff --name-status dotnet-source/main..main -- dotnet/
git -C C:\Users\Andrei\Documents\ChatGPT\SED-MotS-Net ls-tree -r --name-only HEAD -- dotnet/src/ dotnet/tests/ dotnet/tools/
```

Coda checkpoint totals (suite sizes, workflow call/check counts) are quoted
from `docs/PROJECT_STATE.md`, `docs/MCP_INTEGRATION.md`, `docs/LIGHT-01.md`,
`docs/GEOMETRY_REFERENCE_PRESERVATION.md` and `docs/TOOL_WORKBENCH.md` at HEAD
`f15639e`; they describe Coda's verified state and are not port acceptance
evidence. Re-run the equivalent port tests before claiming any feature ported.

