# Delta inventory — Coda post-snapshot work vs the September Qt port

Status: synthesis of the 2026-11-02 read-only delta investigation. No port
code is changed by this document. It inventories what landed on Coda's line
after the snapshot the port was built from, classifies each item against the
existing F-rows in [CODA_FEATURES.md](CODA_FEATURES.md) and
[PORT_LEDGER.md](PORT_LEDGER.md), and proposes where each item would live in
`SED-Qt-Port` and in what slice order. Behaviour/contracts only — no C# is
copied; fixtures must be synthetic/original.

Evidence rule for this file: every delta cites an inspected Coda commit (hash
+ paths from `git show`/`git diff` in the read-only `SED-MotS-Net` repo) or a
port file path. Commit subjects alone were not accepted as evidence in the
source investigation. Test totals quoted from commit messages or work logs
are labelled as such and were not re-executed in the read-only pass.

## 1. Provenance

| Item | Value |
|---|---|
| Port snapshot (Coda) | `f15639e` (2026-09-30) — inventoried in CODA_FEATURES.md / PORT_LEDGER.md |
| Coda tip investigated | `codex/macos-consolidated-20261101` |
| `f15639e..origin/claude` | 52 commits (tip `6fcc36c`) |
| `f15639e..codex/linux-consolidated-20261101` | 70 commits |
| `f15639e..codex/macos-consolidated-20261101` | 73 commits (linux 70 + 3 macOS-only: `d20e1b7`, `a7ab730`, `ec1119d`) |
| Native bridge upstream | `b822559` (`CppCoreInspector.UpstreamCommit`) — matches the Qt port's `smlu/SED` upstream base per PORT_LEDGER.md / CODA_FEATURES.md headers |
| Port branch | `codalilitari/features-port` in `SED-Qt-Port` |

Topology (independently count-verified): linux first-parent = 31, second-
parent-only via merge `ea0b351` = 39 (32 path-sliced replays from
`origin/claude` tip `6fcc36c` + 7 originals `0950f12..5baf577`). Replay
commits `c8610e9` / `1342194` / `f596622` / `a100ed9` are byte-identical in
effect to their originals `0a99e4a` / `c0a133d` / `1dc0001` per diff
inspection — they are the consolidation mechanism, not separate features.

Commit-class accounting for all 73 (from the full `git log`/`show --stat`
classification pass): feature-new = 3, feature-extension = 6, platform = 12,
tooling-external = 5, docs-skills = 14, replay-merge = 33 (32 path-sliced
replays + merge `ea0b351`). `3+6+12+5+14+33 = 73`. No commit is unaccounted.

## 2. Delta table

Classifications: `new-feature` (no September equivalent),
`extension` (extends an inventoried F-row contract),
`invalidates-ported-assumption` (a ported contract is now incomplete or
divergent), `platform-not-applicable`, `external-tool`, `docs-only`.
`Slice` is the proposed implementation order within a future delta programme;
it does not reorder the existing ledger sequence in PORT_LEDGER.md §4.

| ID | Delta | Coda evidence (commit + paths) | Affects | Classification | Proposed C++ home in SED-Qt-Port | Slice |
|---|---|---|---|---|---|---|
| D-01 | Native core inspection command + C++ bridge worker (`native-inspect <input> <jk\|mots\|ijim> <jkl\|native-sed> <worker-config.json>`; exit 0 Inspected / 3 DocumentRejected / 5 ProcessFailed; absolute hash-bound exe + source-manifest SHA-256, reparse-point rejection, 16 MiB in / 64 KiB out, 1–60 s timeout) | `8e93310` — new `dotnet/src/Sed.Formats/NativeCore/CppCoreInspection.cs` (`CppCoreInspector.InspectAsync`), `CppCoreProcess.cs`, `CppCoreProtocol.cs`, new `native/bridge/` (`sed-cpp-bridge`, `protocol-v1.md`, `upstream.lock.json`), new `dotnet/tools/Sed.Agent/NativeInspectionRunner.cs`; tests `dotnet/tests/Sed.Core.Tests/CppCoreInspectionTests.cs` (580 lines) | F-01 (consumer), F-02 (exposure) | new-feature | New `native/bridge/` worker + `tools/sed-agent` `native-inspect` command; SED-Qt-Port currently has no `native/` dir | 5 |
| D-02 | Deadline-test hardening for D-01 (injectable `TimeProvider`; no worker started when deadline expires before identity read) | `7642d52` — `CppCoreInspection.cs` `TimeProvider` param + `CppCoreInspectionTests.cs` `ManualDeadlineTimeProvider`; no port contract change beyond injectable deadline | F-01 | extension | Follow D-01; keep deadline semantics injectable in the bridge host | 5 (with D-01) |
| D-03 | Reviewed dependency packages: `package-plan` / `package-apply` agent commands and `sed_package_plan` / `sed_package_apply` MCP tools (MCP 11→13 tools; `expectedOutputSha256` + `expectedManifestSha256`, `runtimeClosureVerified` stays false, JKL-only inputs, 128 MiB output cap) | `4b630ce` — `AgentCommandCatalog.cs`, `Sed.Mcp ToolCatalog.cs`, `AgentPackages.cs` + `Resources/{JklResourceTableReader,LevelPackageArchive,LevelPackageOptions,LevelPackagePlanner,ResourceDependencyReader}.cs`; verified by diff | F-01, F-02, F-05/F-10 (diagnostics semantics) | new-feature | `tools/sed-agent` + `tools/sed-mcp` over `src/core` (no second edit path) | 4 |
| D-04 | True-colour MAT decoding + rendering (8/16/32 bpp; `colorMode` gate, RGBA8 `(v*255+max/2)/max`, legacy RGB565/ARGB1555 fallback, free-bit15 mask, partial/overlap/other-bpp/truncated refused; renderer `TrueColorBit=16`, alpha<0.5 discard when masked/translucent, modulation only, no palette/ramp) | `be8aeef` — `dotnet/src/Sed.Formats/Material/MatFile.cs` (`TrueColorLayout`), `RenderScene.cs` (`TrueColorBit=16`), `SceneRenderer.cs`, `mesh.frag`; tests `TrueColorMatTests.cs` (278 lines) | F-04, F-03(a) | extension | `src/core/src/assets/mat.cpp` (port `decodePackedImage8` at `mat.cpp:174` lacks the gate/fallback/mask/refusal and zero-fills) + `src/app/src/render/` | 3 |
| D-05 | Master-palette resolution (World Colormap 0 else `dflt.cmp` with Problems, never a later sector CMP; textured = master palette + sector ramp, solid = sector palette + ramp, unresolved sector = master + default ramp + diagnostic) | `d3f1376` — new `dotnet/src/Sed.Rendering/MasterColormap.cs` (`Resolve`/`ResolveFromArchives`), `MaterialLibrary.FindColormap`; tests `MasterColormapTests.cs` (161 lines); GUI replay `12b82c7` (`Sed.App/MainWindow.cs`) | F-03 | extension | `src/app/src/render/mesh/render_lighting_utils.cpp` (must invert `sectorRenderColormap` at `:600`; port today returns sector CMP else `Level::getMasterCMP` at `src/core/src/level.cpp:142`) | 3 |
| D-06 | Adjoined-surface facing discard (single-sided bit 32; discard when `vFacing<0`, zero normal stays visible, non-adjoined stays double-sided) | `8e74556` — `RenderScene.cs` (`SingleSidedBit=32`), `SceneRenderer.cs`, `mesh.vert`/`mesh.frag` | F-04, F-03(a) | extension | `src/app/src/render/` (port has no world-surface single-sided shader discard; fragment inspected at `gl_view_render.cpp:740-1155` has no facing discard) | 3 |
| D-07 | Retail mip fallback (complete mip 0 + short later mip → decode mip 0 + `assetNotices`; short mip 0 / earlier texture / impossible count still throw) | `1dc0001` — `MatFile.cs` `MipFallbackNote`; tests `MatMipFallbackTests.cs` (219 lines) | F-04, F-01 (receipt plumbing) | extension | `src/core/src/assets/mat.cpp` (port parse at `mat.cpp:386-416` throws on truncation, no fallback/note) + agent receipt | 3 (core) / 6 (receipt) |
| D-08 | Snapshot contract growth: orthographic projection (`orthographicHeight` 0.001..1e6, mutually exclusive with FOV), `fullbright`, `cullBackFaces` (world surfaces only, models never); additive receipt, perspective `cameraSha256` unchanged | `1dc0001` (+ `27f3a00` credit/docs) — `AgentSnapshotRequest.cs`, `Mat4.Orthographic` (`66704c3`, `Sed.Core/Math/Mat4.cs`), `RenderScene.CullWorldBackFaces`; tests `CameraProjectionTests` / `AgentSnapshotRequestTests` | F-01, F-02 | extension | Headless render path (see §3); no current home — the Qt-free agent has no renderer | 6 |
| D-09 | Wren-style `overview` command (view `overhead`/`threequarter`, framing `wren`/`contain`, margin 0..0.25 default 0.025, `cullBackFaces` default true; `OverviewFraming` with golden values shared with `tools/blender`; embedded `snapshotRequest` for byte-exact replay) | `1dc0001` — new `dotnet/src/Sed.Formats/Automation/AgentOverviewRequest.cs` (136 lines), `OverviewFraming.WorldSurfaceBounds/Compute`; tests `OverviewFramingTests` / `AgentOverviewRequestTests` | F-01, F-02 | new-feature (new command within the F-01 class, not a new F-row) | Headless render path (see §3) | 6 |
| D-10 | Caesar's Palace stray-`)` frame kept verbatim + line-named preservation warnings + native-import parity (three legacy spellings) | `c0a133d` (replayed `1342194` Jkl + `f596622` Jed) — `JklReader.cs` (`IsLegacyTrailingFrameToken`), `JklParser.cs` (`LegacyParameterNotes`), `JedImporter.cs`; tests `JklMotsPreservationTests` / `JedImporterTests`; rule in `docs/audit/NET_MOTS_FORMAT_AUDIT.md` | F-00 | invalidates-ported-assumption (port row incomplete; no ported test contradicted) | `src/core` JKL/JED readers (port `src/core/include/sed/level.hpp:464-468` explicitly does not retain record-internal THINGS/TEMPLATES extras; no `LegacyParameterNotes` equivalent) | 1 |
| D-11 | JKL writer guard: a preserved unterminated final frame must remain last; save refuses if a follower is appended (original spelling and parameter order preserved) | `8e93310` — `dotnet/src/Sed.Formats/Jkl/JklWriter.cs` (`ParametersForWrite`); tests `LegacyParameterCompatibilityTests.cs` (155 lines) | F-06 | invalidates-ported-assumption | `src/core` `jkl.hpp`/`jkl.cpp` `buildJklExport`/`commitJklExport` (no equivalent check today) | 1 (with D-10) |
| D-12 | Non-GOB resource archive is a request error, not a crash (exit 2 `invalid_request` naming the file; was exit 134 abort) | `0a99e4a` (replay `c8610e9`) — `dotnet/src/Sed.Formats/Resources/ExplicitArchiveResources.cs`, `dotnet/tools/Sed.Agent/Program.cs`; test `AgentResourceTests.ArchiveThatIsNotAGobIsARequestErrorNotACrash` | F-05, F-01 (exit-code contract) | extension + invalidates-ported-assumption (exit semantics diverge) | Port `agent_workspace.cpp:215-217` currently returns exit 3 (`kExitInput`) with a different message — align code+message or document the divergence | 2 |
| D-13 | Retail JK capture tool under Wine (hash-checked retail `JK.EXE` 1.01, private Xvfb, inert Smacker stub, frame-hash receipts; 49 stdlib tests) | `0141e1d` — new `tools/retail-capture/` (`jkcapture.py` 1961 lines, `tests/test_jkcapture.py`) | F-09 (non-substitute), F-11 | external-tool | Engine-side only; does not unblock F-09/F-11 (see §4) | — (do not port as a capture substitute) |
| D-14 | Linux engine profile for the capture runner (profile selection by staged exe names, POSIX process-group cleanup, private Xvfb displays 91–199; unchanged P6 RGB8 `capture.ppm` receipts) | `0950f12` (docs), `c931f57` (runner), `fbda203` (tests), merged `ae278d6` — `tools/invoke_engine_capture.py`, `tools/tests/test_invoke_engine_capture_linux.py`, `docs/ENGINE_CAPTURE_LINUX.md` (real OpenJKDF2 verified only to the missing-assets guard) | F-09, F-11 | external-tool | New `tools/engine-capture/` (Python harness pattern is language-agnostic) | 7 |
| D-15 | Wren's JKL importer + preview renderer for Blender (shares `framing.py` golden values with D-09) | `78eb348` — `tools/blender/` | none (separate external tool) | external-tool | Out of scope for the Qt port; no F-row | — |
| D-16 | Native Linux/macOS platform programme (editor startup, MCP worker resolution, atomic create-only promotion via `renameat2`, Vulkan push-constant gate, source-archive blob preservation, macOS distribution/qualification pipeline) | `7273f7a`, `ec1e6ac`, `30f9c80`, `c40ae10`, `de6a278`, `1c6c3b7`, `063b1f0`, `514c3f9`, `ff97205`, `d20e1b7`, `a7ab730`, `ec1119d` (12 platform commits in the classification pass) | none directly | platform-not-applicable | Port already targets Qt/C++ on its own platform matrix; track only where a Coda contract (not a .NET mechanism) is implied | — |
| D-17 | Docs, skills and validation guidance (work log, Wren credit, `.claude/skills/jk-level-workshop`, SM Sith Lord application notes — handler-message test ≠ player Use-reach, separate movement/lighting/sky verdicts, bounded direct scan ≠ recursive closure) | `2f957c0` (work log), `27f3a00` (credit/docs dominant), `013ea4a`, `9106bf0`, `ff88182` (`docs/SM_SITH_LORD_APPLICATION_NOTES.md`, 2 files, no code/schema/exit-code change), `419637c`, `3816446`, `c9f3ca8`, `8b1f056`, `5baf577..633b95d` series | F-10 (guidance), F-11 (acceptance discipline) | docs-only | Cite as validation procedure; do not treat MotS behaviour notes as asserted contracts | — (adopt as guidance) |

Notes on the table:

- D-04/D-05/D-06/D-07 are contract-tightenings of the September F-03/F-04
  renderer slices, not new rows. The port renderer slices are already marked
  `Partial` + runtime-unverified in PORT_LEDGER.md (`F-03` line 40, `F-04`
  line 41); this delta widens that remainder rather than contradicting a
  ported core assumption.
- Port shader closure was inspected directly for D-04
  (`gl_view_render.cpp:740-1155`, `gl_view_mesh.cpp:660-849`, `mat.cpp:1-423`,
  `render_lighting_utils.cpp:600`, `level.cpp:142`): the port shader has no
  `TrueColorBit`/palette/ramp branch (palette is pre-baked CPU-side at
  `gl_view_mesh.cpp:725`), and its alpha discard is driven by header
  classification, not the .NET per-draw masked/translucent rule — so the
  port is not equivalent today, and the ledger's runtime-unverified marking
  stands.
- D-16/D-17 commit counts are taken from the inspected classification pass
  (prior result 2); the macOS pipeline content (`d20e1b7`/`ff97205`) outside
  the assigned scopes was count-verified but not independently diff-inspected
  — see §5.

Proposed slice order (delta programme only): 1 = D-10+D-11 (preservation
correctness, cheapest and load-bearing), 2 = D-12 (exit-code alignment),
3 = D-04–D-07 (MAT/renderer contract tightening), 4 = D-03 (package
commands), 5 = D-01+D-02 (native inspection), 6 = D-08+D-09 (headless
snapshot/overview, needs the render path in §3), 7 = D-14 (capture-runner
pattern; acceptance stays unverified per §4).

## 2b. Port status

- **D-10 + D-11: ported (2026-11-02).** `classifyLegacyParamValue` /
  `legacyParamSpellingNote` in `src/core/include/sed/values.hpp` +
  `src/core/src/values.cpp` implement the three-spelling rule natively;
  `deserializeTplValue` keeps a legacy spelling byte-for-byte instead of
  letting the typed frame parser silently close it. Loaders enforce the
  tokenizer discipline: `checkLegacyParamTokens` (JKL THINGS/TEMPLATES,
  `src/core/src/assets/jkl.cpp`) and the mirrored native-row check (JED,
  `src/core/src/assets/jed.cpp`) keep the stray-`)` frame with a
  row-named preservation warning (`THINGS row N:` / `TEMPLATES row
  'name':` / `Native thing N:`), refuse unmatched/unclosed near-misses,
  and refuse an unterminated frame that is not row-final. The D-11
  writer guard (`validateLegacyFrameOrder` in `saveToJklImpl`) refuses
  export before any byte is written when a follower was appended after
  a preserved unterminated frame; the stray-`)` frame may gain
  followers. Tests: new `jkl-legacy-frame` group in
  `src/core/tests/assets/jkl_legacy_frame_tests.cpp` (Caesar keep/warn/
  round-trip, 10 near-miss refusals, writer-guard matrix, JED parity).
  Gates: every build target built except the `sed.exe` relink, blocked
  only by the running editor process holding the file (LNK1168);
  `sed_core_tests`
  exit 0; agent/MCP ctests 18/18; updater 5/5; `gui_wiring_utils`
  failures=0. Remaining difference vs Coda: JKL warnings name the row,
  not the absolute source line (the port loader does not retain
  absolute line numbers per section).

- **D-12: ported (2026-11-02).** Non-GOB resource archive handling is
  aligned to Coda's exit-2 contract. In `tools/sed-agent/
  agent_workspace.cpp`, `loadCogArchives` now maps a `GobArchive::open`
  failure to `failCoded(kExitUsage=2, "invalid_request", "Resource
  archive is not a readable GOB/GOO archive: " + path + ". " +
  e.what())`; a missing file (`readFileBytes` failure) still exits 3
  (`kExitInput`) and a SHA-256 mismatch still exits 4
  (`kExitConflict`). The second open site `openGobOrFail` (used by
  the `archive`/`extract` commands) applies the same exit-2 mapping;
  its exists-check still exits 3. `tools/sed-agent/main.cpp`
  `printError` already serialises `AgentError.errorCode` to both the
  `code` and `errorCode` JSON fields. Test: new `invalid-archive`
  scenario in `tools/sed-agent/tests/agent_cli_tests.cpp` (text-file
  `JKMRES.GOO`, bytes `not a gob`, SHA-256
  `29342e2fbb5571d45a1f08050774858ba943254983fd0a249a03746fdd5be9b6`)
  asserts exit 2 + `invalid_request` with the message prefix and
  filename, and additionally asserts missing-file to 3, stale-hash
  to 4, and valid GOB to 0; the pre-existing `archive-extract-launch`
  non-GOB expectation was updated from 3 to 2 `invalid_request`.
  Wired in `tools/sed-agent/CMakeLists.txt` (`invalid-archive` in
  the `agent_scenario` foreach). Gates (verify pass, fresh):
  build of `sed_core_tests sed_agent sed_mcp sed_update` exit 0;
  `sed_core_tests.exe` from repo root exit 0; `ctest -R agent`
  19/19 (incl. `sed_agent_invalid-archive`,
  `sed_agent_cog-bindings`, `sed_agent_archive-extract-launch` + 4
  MCP tests); `ctest -R sed_update` 5/5. No remainder for D-12.

- **D-04: ported (2026-11-02), renderer runtime-unverified.**
  Renderer wiring landed after the core decode below: texture
  records carry `trueColor` (`TextureInfo`/`PreparedTextureSource`
  in `src/app/src/gl_view.hpp`, set from `MatFile::isTrueColor()`
  in `gl_view_mesh.cpp`, propagated in `gl_view_resources.cpp`),
  and the three batch paths in `gl_view_render.cpp` now enable the
  existing 0.5 alpha cutout for true-colour textures as well
  (masked/translucent texels below 0.5 alpha are discarded). The
  port textured path is already modulation-only (`tex * lit`, no
  palette or ramp stage in the shader; palettes are applied at CPU
  decode for indexed MATs only), so Coda's `TrueColorBit`
  modulation-only rule holds by construction rather than by a
  shader bit. Compile-verified via `sed_app_tests`; no pixel
  comparison against the .NET renderer was run, so the row keeps
  its runtime-unverified marking. Core decode detail: True-colour MAT decoding is implemented in core:
  `MatFile::isTrueColor()` / `bitsPerPixel()` in
  `src/core/include/sed/assets/mat.hpp` (file-level, following the
  `isColor`/`isAnimated` shape; private `isTrueColor_`), and in
  `src/core/src/assets/mat.cpp` the texture-data field `pad0` is
  renamed `alphaFlags`, with `TrueColorChannel`/`TrueColorLayout`,
  `channelMask`/`channelIsValid`/`expandChannel`
  (`(v*255+max/2)/max`, absent alpha = 255),
  `resolveTrueColorLayout` and `decodeTrueColorImage8` (always
  `Rgba8`). Legacy all-zero RGB 16 bpp falls back to RGB565
  (alpha flag clear) or ARGB1555 (set); a declared layout must be
  1-8 bits/channel and disjoint across RGB+alpha; a free bit 15
  (bpp 16, alpha enabled, `rgbMask & 0x8000 == 0`, no declared
  alpha) synthesises a 1-bit mask and a declared alpha takes
  precedence. Parse gate: bpp not in {8,16,32} is refused
  (`Unsupported MAT bpp`), 16/32 bpp with mode other than RGB(1)/
  RGBA(2) is refused, `isTrueColor_` is set for 16/32 bpp, the
  per-cel layout derives from `texData.alphaFlags & 1`, true-colour
  mips decode via `decodeTrueColorImage8`, and the 8 bpp indexed
  path is unchanged. Tests in
  `src/core/tests/assets/mat_cmp_tests.cpp`: existing 16 bpp
  fixture updated to `Rgba8`/16 bytes + `isTrueColor`, 24 bpp
  fixture converted to a refusal, plus synthetic declared-16
  (rounding 132/130/132), legacy RGB565 and ARGB1555 fallbacks,
  free-bit15 mask (set/clear + masked RGB565 opaque), declared-32
  (alpha default 255), declared-alpha precedence + ARGB4444, and
  refusals for partial/overlapping RGB, shift past depth, >8-bit
  channel, overlapping alpha, 32 bpp no layout, 16 bpp `Indexed`
  / 32 bpp `IndexedCols` mode, 15 bpp, and truncated 16/32 bpp
  pixels. Gates: build of `sed_core_tests` exit 0;
  `sed_core_tests.exe --mat-cmp` exit 0 and full
  `sed_core_tests.exe` from repo root exit 0. Renderer wiring is
  recorded at the head of this entry; no renderer equivalence is
  claimed.

- **D-07: ported (2026-11-02).** Core retail mip fallback plus
  agent receipt plumbing: `validate` and `bindings` outcomes now
  carry `assetNotices` (`matAssetNotices` in
  `tools/sed-agent/agent_workspace.cpp`), one row per archive MAT
  whose parse recorded a retail mip fallback (`archive` index,
  `entry`, `note`); empty array when no archive MAT fell back, and
  unparsable MATs never produce a notice. Scenario
  `sed_agent_asset-notices` (synthetic short-mip 4x4/3-mip MAT in a
  GOB fixture) passes; agent suites 22/22, MCP 4/4, updater 5/5,
  `sed_core_tests` exit 0. Core detail: Retail mip fallback is implemented in core:
  `MatFile::mipFallbackNote()` (`const std::string&`, empty when
  no fallback) in `mat.hpp`, backed by private
  `mipFallbackNote_`. In `MatFile::parse`
  (`src/core/src/assets/mat.cpp`), if `rawBytes >
  r.remaining()` the loader throws
  `std::runtime_error("MAT pixel data truncated")` when the short
  mip is mip 0 or the cel is not the final cel; when the short
  mip is `mip > 0` on the final cel it records the exact note
  `Retail mip fallback: mip level <m> of <count> levels (<w>x<h>)
  needs <bytes> bytes but <remain> remain; showing complete mip
  level 0, as retail JK does on faces nearer than the level's
  MipMap Distances; retail faces far enough to use mip level <m>
  or later show data the file never filled.` and breaks, keeping
  every complete mip already decoded. An impossible mip count
  still throws earlier via the `bit_width(min(sizeX,sizeY))`
  gate; complete chains are unchanged with an empty note.
  Tests in `mat_cmp_tests.cpp` (`MAT.mipFallback`): short mip 1
  (4x4/3 mips, 2 of 4 mip-1 bytes: exact note,
  `mipmaps.size()==1`, mip-0 pixels and `decodeCelRgba8`
  present mip 0), short mip 2 (keeps mips 0+1, note names
  level 2), complete chain (empty note, 3 mips), short mip 0
  throws, impossible counts throw (4x4/4 mips and 1x1/2 mips),
  short earlier texture in a 2-cel file throws, and fallback
  applies to the final texture of a 2-cel file. The fallback
  therefore keeps all complete mips before the short one (the
  short-mip-1 case keeps only mip 0) and `decodeCelRgba8` still
  presents mip 0, matching the note wording. Gates: build of
  `sed_core_tests` exit 0; full `sed_core_tests.exe` from repo
  root exit 0 (all groups incl. `MAT.mipFallback`). Agent receipt
  plumbing of `mipFallbackNote` (`assetNotices` in agent receipts)
  landed with the D-07 completion above; the MCP surface reuses the
  agent outcomes unchanged.

- **D-05 + D-06: ported (2026-11-02), renderer runtime-unverified.**
  D-05: `sectorRenderColormap` (`src/app/src/render/mesh/render_lighting_utils.cpp`)
  now always resolves the level master palette for MAT texture decoding
  (World Colormap 0 via `Level::getMasterCMP`, else `dflt.cmp`); a sector
  CMP never selects the texture palette. Unit-covered by
  `runMasterPaletteColormapTests` in the `render_preview` app group
  (failures=0). The port has no indexed sector-ramp shading path, so the
  "sector ramp only" half has no counterpart to invert; sector light
  remains multiplier/vertex-colour based, as before. D-06: the main
  fragment shader gained `u_single_sided` — for Level batches with
  `FF_RD_Adjoin` in Game Preview only, fragments behind the surface
  plane (`dot(v_face_normal, u_camera_pos - v_world_pos) < 0`) are
  discarded, zero normals stay visible; editor inspection keeps both
  sides (matching the existing backface-inspection behaviour). Set at
  all three batch uniform sites in `gl_view_render.cpp`. Compile-verified
  via `sed_app_tests`; the `render_preview` group passes, but no pixel
  comparison against the .NET renderer was run, so both rows keep the
  F-03/F-04 runtime-unverified marking. Coda's `!flat` submesh condition
  has no port equivalent and is not modelled.

- **D-03: ported (2026-11-02), dependency-reader scope narrower than Coda.**
  Core planner `sed::level::planLevelPackage` (`src/core/include/sed/level/package.hpp`,
  `src/core/src/level/package.cpp`): deterministic plan over the JKL
  resource tables (SOUNDS/MODELS/SPRITES/KEYFRAMES/ANIMCLASS/SOUNDCLASS/
  AICLASS/COGSCRIPTS with Coda's folder mapping), MATERIALS and
  GEORESOURCE colormaps, template resource parameters, placed COG
  scripts, extra roots, and 3DO material dependencies; episode.jk via
  the port's `saveToEpisodeJkText`; deterministic dir-first GOB bytes
  (read back through `GobArchive`); canonical-manifest SHA-256 binding
  plan to apply. Agent `package-plan` (blocked plan = whole plan JSON,
  exit 3) and `package-apply` (expected-output + expected-manifest
  revalidation, exit 4 stale, exit 5 existing, never overwrites); MCP
  `sed_package_plan` (read) and `sed_package_apply` (journaled write),
  13 tools total. `runtimeClosureVerified` is always false. Covered by
  core `package` group + agent `sed_agent_package` scenario + MCP
  protocol tool assertions (agent 20/20, MCP 4/4, core exit 0).
  Narrower than Coda, disclosed in every plan's `unverifiedScopes`:
  COG resource defaults/default heels, KEY/PUP/SPR/SND/AI/CMP
  subresource readers, sound-class sound lists, and stock dependency
  closure are not followed; Coda's retained-provenance table reader
  (indexed/duplicate/null rules) is approximated from raw section rows.

- **D-01 + D-02: ported (2026-11-02), Windows host.** New worker
  executable `tools/sed-cpp-bridge/` (`sed-cpp-bridge.exe`) implements
  native inspection worker protocol 1 (`capabilities`, `inspect-jkl`,
  `inspect-native-sed`) over the port's upstream-C++ core: strict argv,
  16 MiB stdin cap, JKL admission budgets, HEADER VERSION 1/3 envelope,
  kind-mismatch refusals, bounded diagnostics, SED optional-section and
  integrity reporting; `upstream.lock.json` pins `b822559` and its
  SHA-256 is the worker's embedded source-manifest identity. Agent
  `native-inspect <input> <jk|mots|ijim> <jkl|native-sed>
  <worker-config.json>` validates the config (schemaVersion 1, both
  hashes, timeoutSeconds 1-60), requires the worker beside the config,
  verifies the executable hash (exit 4 stale), runs it with a hard
  deadline (worker is terminated; `deadlineExceeded`, exit 5), and maps
  outcomes Inspected 0 / DocumentRejected 3 / ProcessFailed 5. D-02 is
  covered by that process deadline plus the pre-spawn input-size check;
  Coda's per-read stream deadline has no separate port counterpart.
  Covered by `sed_agent_native-inspect` (agent 21/21, core exit 0).
  Known limits: worker hosting is Windows-only; jk/mots discrimination
  for VERSION-1 JKL trusts the declared kind (only IJIM mismatches are
  detectable); the worker's JKL admission is the protocol budget subset,
  not the upstream bridge's full surface-expansion accounting.

- **D-14: pattern ported (2026-11-02), acceptance unverified.** New
  `tools/engine-capture/` holds a stdlib-Python reimplementation of
  the capture-runner pattern (`invoke_engine_capture.py`, own code;
  contract only, no Coda text copied): hash-bound staging into a
  fresh output directory, `capture-request.txt` in
  `sed-engine-capture-v1` format, profile selection by staged
  executable names (windows `openjkdf2-64.exe` + two runtime DLLs;
  linux single `openjkdf2`), POSIX process-group cleanup, private
  Xvfb display allocated via `-displayfd` with TCP disabled (the
  inventory row's "displays 91-199" phrasing is superseded by the
  runner code's display-fd allocation), P6 RGB8 `capture.ppm`
  validation, and `launcher-receipt.json` with before/after
  manifests. Receipts always carry
  `effectiveCameraObserved: false`,
  `runtimeResourceResolutionObserved: false` and
  `acceptance: "unverified"`; the F-09 engine gate and F-11 oracle
  stay exactly as §4 states. Not ported: the gameplay-telemetry
  extension. Tests: `tests/test_invoke_engine_capture.py` (9 tests,
  1 POSIX-only skip on Windows) and ctest
  `sed_engine_capture_runner` pass; the full-capture test uses a
  synthetic fake engine and proves orchestration only.

- **D-08 + D-09: ported (2026-11-02) with a software render
  path; scope narrower than Coda's Vulkan renderer.** Rendering
  landed last: core `sed/render/software_snapshot.hpp` is a CPU
  rasterizer (z-buffered, perspective-correct UVs, near-plane
  clipping, stored-block PNG encoder) drawing world surfaces only
  with static sector ambient (or fullbright); things, models,
  sprites and dynamic lights are out of scope and the outcome
  reports `renderer: "software-v1"`, `renderScope:
  "world-surfaces"`. Agent `snapshot` renders to a create-new PNG
  (explicit camera or overview auto-frame); `overview` renders
  when the request names an `output`. Materials resolve from the
  hash-bound archives by basename with the master palette.
  Pixel-level core tests (`software-snapshot`: texel at centre,
  background at corners, cull direction) and agent scenarios
  (`sed_agent_snapshot-refusal` now asserting real PNGs,
  `sed_agent_overview`) pass. No equivalence with Coda's Vulkan
  output is claimed. Core gained the Wren framing rule
  (`src/core/include/sed/level/overview.hpp`,
  `src/core/src/level/overview.cpp`): the drawn-world-surface
  predicate (`geo != 0`, >= 3 corners, named material),
  `worldSurfaceBounds`, and `computeOverviewCamera` for overhead
  (orthographic, north up) and threequarter (perspective from the
  south-east) with wren/contain fits, matching the shared golden
  values to 1e-9 (core group `overview`, synthetic fixtures).
  Agent `overview` returns the computed camera plus the embedded
  snapshot request (`renderStatus: snapshotRequiresRenderer` and
  `imageCreated: false` when no output is requested; `rendered`
  with an output). Agent `snapshot` validates the D-08 request
  contract (width/height 64..2048, 2097152 pixel budget, projection
  exclusivity of `fieldOfViewDegrees` / `orthographicHeight`
  0.001..1e6, `fullbright`/`cullBackFaces` booleans) before
  rendering. Gates: core exit 0 (incl. `overview`,
  `software-snapshot`), agent 23/23, MCP 4/4.

## 3. Snapshot question — is the F-01/F-02 snapshot refusal superseded?

**No** — as written against the delta alone. The refusal was the
correct state until a render path existed. It has since been
retired by the port's own software renderer (see the D-08 + D-09
entry in §2b): `snapshot` and `overview` now produce real PNGs of
world surfaces, with the narrower scope reported in every outcome.
The evidence below records the state when this question was asked.

Evidence:

- Port: `tools/sed-agent/agent_workspace.cpp:1299-1305` — `cmdSnapshot`
  refuses with exit 5 / `snapshotRequiresRenderer` ("Qt-free and cannot
  render a faithful PNG snapshot without the renderer; no image was
  created"), repeated at `:1628`, dispatched at `:1708`. The agent links
  `sed_core` only (PORT_LEDGER.md F-01 row); `sed-mcp` delegates the same
  refusal (F-02 row). No overview/package-plan/native-inspect strings exist
  in the agent file (grep empty in the source investigation).
- Coda delta: snapshot/overview require an offscreen Vulkan `SceneRenderer`
  (`AgentSnapshotRunner` render path). The port renderer is Qt/OpenGL under
  `src/app` and is not linked into the Qt-free agent.
- The delta adds orthographic/fullbright/cull-back-faces snapshot options
  (D-08) and the genuinely new `overview` command (D-09) on top of the same
  Vulkan path. Overview is a new command inside the F-01 extension surface,
  not a new F-row, and it makes a headless render path *more* necessary, not
  less.
- The earlier speculation in PORT_STATUS_FINAL.md Addendum 5 (that a
  Wren-style overview "may supersede" the refusal) does not survive as
  supersession; it survives only as an extension. Ledger claims of protocol
  parity (F-01 "Present (full command set)", F-02 11-tool completeness in
  PORT_LEDGER.md:38-39) are stale against the tip and must be re-scoped to
  `f15639e`.

Correct port action: keep the refusal; implement D-08/D-09 only via a new
headless render path, then retire the refusal for the commands that path
actually serves.

## 4. F-09 capture question

**The F-09 engine gate stays.** No capture-capable engine binary is
delivered or verified end-to-end in this delta.

Evidence:

- A capture-capable *runner contract* now exists on both OSes that the Qt
  port could target unchanged: `tools/invoke_engine_capture.py` is Python
  (language-agnostic), and the Linux profile (D-14) needs only a native
  `openjkdf2` implementing `sed-engine-capture-v1`. Staging/hashing and the
  P6 RGB8 `capture.ppm` + `effectiveCameraObserved` /
  `runtimeResourceResolutionObserved = false` receipts are unchanged.
- But the Linux real build (OpenJKDF2-MotS-Fixes `2c6008db`) is verified in
  `docs/ENGINE_CAPTURE_LINUX.md` only to the missing-assets guard; a complete
  capture with owned game files is "Not yet established".
- The retail-capture tool (D-13) is a different, non-capture-v1 path (retail
  executable under Wine, no `capture.ppm`/P6 contract) and is explicitly
  excluded by F-09: "an ordinary retail executable is not a substitute"
  (PORT_LEDGER.md F-09 row).

Therefore: Qt port F-09 stays `Absent`/engine-gated and the F-11 oracle
stays unverified. Correct port action is to port the runner + harness
pattern (including the Linux profile) as `tools/engine-capture/`, and keep
acceptance unverified until a capture-v1 engine build and owned
`JKMRES.GOO`/`JKMsndLO.goo`/`jk_.cd` exist.

## 5. Unresolved items (carried verbatim from the source investigation)

From prior result 1 (`agent-mcp-capture-tools`):

- "Whether OpenJKDF2-MotS-Fixes capture-v1 Linux build completes a real capture with owned game files — docs/ENGINE_CAPTURE_LINUX.md states not yet established; no engine binary inspected in either repo."
- "Sed.UiProbe/LinuxDesktopProbe.cs and macOS pipeline commits (d20e1b7/ff97205) touched in delta but outside the four assigned items; not inspected beyond --stat."

From prior result 3 (`renderer-mat-snapshot`):

- "Port GL shader handling of pre-decoded packed (16/32b) textures under scalar/RGB/fullbright lighting not inspected in shader sources — only CPU decode (mat.cpp) verified; renderer runtime-unverified per PORT_LEDGER F-03/F-04." — partially closed by prior result 6's direct shader inspection (see §2 note on D-04); the runtime-unverified marking itself stands.
- ".NET suite totals (Core 1943, LightingProbe 328, Test-AgentLitSnapshot 252/93) taken from 1dc0001 commit message and WORK_LOG, not re-executed in this read-only pass."

From prior result 4 (`formats-preservation-core`), verbatim flag texts:

- "FLAG INVALIDATES/EXTENDS F-00 (port contract now incomplete, not silently broken): Coda c0a133d/1342194+f596622 require verbatim keep + line-named preservation warning + native-import parity for the stray-')' frame; the port has no legacy frame-token rule and per level.hpp:464-468 does not retain record-internal THING/TEMPLATE extras — a Caesar's Palace import fixture would not reproduce Coda's keep-and-warn behavior. New F-00 fixture needed (verbatim line, round-trip with/without edit, template + native rows, 10 near-miss refusals). No ported F-00 test is contradicted; the row's 'refuse-don't-repair matrix' must gain this accept-verbatim exception or the port will refuse/mangle a level Coda now opens."
- "FLAG INVALIDATES F-06 writer assumption: 8e93310 JklWriter.ParametersForWrite refuses save when a preserved unterminated final frame gains a follower (protecting engine parameter-order semantics); the port's buildJklExport/commitJklExport (jkl.hpp/jkl.cpp) has no equivalent check, so an edit appending a parameter after such a frame would silently produce output Coda refuses. Add to F-06 refusal matrix with LegacyParameterCompatibilityTests as derivation source."
- "FLAG DIVERGES F-01/F-05 exit semantics: Coda 0a99e4a makes a non-GOB resourceArchives entry exit 2 invalid_request naming the file ('Resource archive is not a readable GOB/GOO archive: {path}'); port agent_workspace.cpp:215-217 returns exit 3 (kExitInput) with a different message. Port does not crash, but scripted callers keying on Coda's exit 2 will misclassify. Align code+message or document the deliberate divergence."
- "EXTENDS (does not invalidate) F-01/F-02: Coda snapshot gains orthographic/fullbright/cullBackFaces with backward-compatible perspective receipts, plus overview, package-plan/package-apply (MCP 11→13 tools, sed_package_*) and native-inspect. The port's honest snapshotRequiresRenderer refusal remains valid but now covers a larger surface; ledger claims of protocol parity (F-01 'Present (full command set)', F-02 11-tool completeness) are stale against tip and must be re-scoped to f15639e. Package planner (runtimeClosureVerified=false, dual-hash apply) is a new F-01/F-02-class feature with no port row."
- "NO INVALIDATION from (4): ff88182 is docs-only guidance; it strengthens F-11-style witness discipline (reach vs handler, separate verdicts, direct-scan ≠ closure) and adds no F-10 code contract. Port COG Slice Spec A-cases unaffected; cite the notes as validation procedure, do not treat P54/P77 MotS behavior as asserted contracts (the doc itself disclaims this)."
- "Mat4.Orthographic (66704c3) and MainWindow master-palette change (12b82c7) are additive/GUI-preview only; they extend the F-03/F-04 renderer-slice backlog (master-palette + true-colour texels in previews) but invalidate no ported core assumption."

From prior result 5 (`critic`):

- "Port OpenGL shader-level true-colour modulation equivalence not inspected (carried from prior-result-3); port renderer slices remain runtime-unverified per PORT_LEDGER F-03/F-04 — does not affect falsification verdicts above" — see the partial closure note above.
- ".NET test totals (Core 1943/1944, LightingProbe 328, snapshot 252/93) are commit-message/WORK_LOG reported in priors, not re-executed in this read-only critic pass"
- "macOS pipeline commits d20e1b7/ff97205 and Sed.UiProbe/LinuxDesktopProbe touched in delta were outside the four assigned scopes in prior-result-1 and not independently diff-inspected here; classification (prior-result-2) covers them as platform, count-verified but content taken from that inspected classification"

From prior result 6 (`gap-followup`):

- ".NET suite totals (Core 1943, LightingProbe 328, Test-AgentLitSnapshot 252 assertions/93 calls from 1dc0001 message/WORK_LOG) not re-executed — read-only pass per hard rules; test files inventoried via git show --stat only."

Prior result 2 (`commit-classification`) reported no unresolved items.
