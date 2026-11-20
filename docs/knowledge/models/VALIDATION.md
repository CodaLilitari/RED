# Release validation — 1.7.0

> Copied into RED from [CodaLilitari/JK-MotS-Model-Workflow@ca011b8](https://github.com/CodaLilitari/JK-MotS-Model-Workflow/blob/ca011b84c4fa5e1f3755e1543be2c1c1d25063c9/VALIDATION.md) (MIT; see [LICENSE](LICENSE.md)). Regenerate with `tools/knowledge/import_knowledge.py` rather than editing this copy.

## Workflow 1.7.0, November 20, 2026

The workflow targets Blender Sith (Coda) 1.2.0, tagged at
[`34d256c30f66`](https://github.com/CodaLilitari/blender-sith/commit/34d256c30f66fe9826e5a3318f2fd4397257f24c).
The [GitHub](https://github.com/CodaLilitari/blender-sith/releases/tag/v1.2.0-coda.1)
and [GitLab](https://gitlab.com/codalilitari/blender-sith/-/releases/v1.2.0-coda.1)
distributions yielded identical bytes for all fourteen assets. The add-on's
validation report records Blender application tests, native-format tests and
background regression results for each platform.

The Windows x64 and Ubuntu/WSL x64 installations each loaded the enabled
extension from saved preferences in a fresh process, matched all 38 package
files and passed all 29 background regression cases. Windows used Python 3.13
with bpy 5.1.2 and Ubuntu used native Blender 5.1.2.

The workflow tests ran 46 cases and the install resolver tests ran 21. Each
suite passed with one Windows symlink-capability skip. The skill validator
accepted the package source. Research revision `2026112001` requires workflow
1.7.0. The [joint and face guide](rigid-joint-and-face-diagnostics.md)
identifies the native file counts that were checked independently and the pose
and paint observations reported by Red. No new game session was run for this
workflow release.

## Version 1.6.0 evidence review

Prepared November 13, 2026. Workflow 1.6.0 / research revision `2026111301`
updates APIs, shared installation resolution and release guidance while retaining
all previous Red/Wren/Dot research. It does not run a new game, generate an asset
or certify multiplayer.

- Blender-Sith integration source `25955b7976d966a12feff273087b58cb7c82d4e3`
  combines upstream `01ff4bd85498074e6f936d9adbbcc93817ca712d` and the reviewed
  Coda hex/pivot changes. [Native CI run 43385708162](https://github.com/CodaLilitari/blender-sith/actions/runs/43385708162)
  completed successfully for Linux x64 and ARM64 macOS. Final release source is
  pinned at `0b30d90c5529ef6f11e822f6812c5332b7362c6e`; exact release ZIP/hash
  readback and installation remain separate finalization records.
- The Linux run reports 264 upstream core cases, 768 Blender application cases
  and 29 Coda checks. The macOS artifacts independently report 264 core and 768
  application cases with zero skips, plus 29 Coda checks. The Windows core run
  completed 264 cases with two host-capability skips. Retain each platform's
  own detailed artifacts. These are add-on/codec tests, not retail game tests.
- The workflow probe first failed on the removed root-level `export3do` API,
  then exposed its multiple-root fixture. After explicit-module and single-root
  fixture migration, the observed Blender 5.1.2 run exited 0 with completed
  reporting and unchanged add-on source. Shared-mesh duplication remains its
  one reported limitation; visible-modifier rejection is recorded separately.
- Independent read-only application review exercised a procedural armor plus
  two-cel indexed MotS material/KEY case and a malformed textured 3DO plus old
  Blender-scene case. It confirmed actionable routes and prompted explicit
  clarification of ordinary flat normals and authored collision radius. This
  was instruction review, not a newly executed asset pipeline.
- The shared resolver and focused tests retain their reviewed bytes; they bind
  exact package payload, runtime, launcher and harness without running Blender.
  Private manifests and installation paths are excluded from the public package.
- Research revision `2026111301` requires complete skill 1.6.0 because the
  reference instructions depend on new scripts/entrypoint. The updater logic is
  unchanged; a historical compatible-feed fixture remains pinned separately
  from the new full-upgrade requirement.
- Fresh offline workflow tests completed 46 cases: 45 passed and one Windows
  symlink-capability case was skipped. The resolver suite completed 21 cases:
  20 passed and one symlink-capability case was skipped. Skill-creator validation
  passed with Python's explicit UTF-8 mode; its initial locale-default read failed
  on valid UTF-8 and is retained as a tool/environment receipt, not a skill defect.
- Final allowlist/readback, archive/feed hashes, publication and installed-copy
  receipts are separate records for the new 1.6.0 bytes. Historical suite totals
  below are not substituted for current validation, and no immutable older
  release is replaced.

## Version 1.5.0 evidence review

Checked November 12, 2026. This update combines fresh artifact measurements, independently reproduced Blender-Sith regressions and attributed saved project reports. It does not certify a newly played character or level.

- Fresh Red source reads distinguish requested quads, mixed OBJ polygons and GLB triangles. The 5,336-polygon OBJ contains 4,421 quads and 915 triangles, equivalent to 9,757 triangles; its paired GLB agrees. The 2,851-polygon OBJ has no paired GLB inspection in this review. Saved Smart Mesh settings and engine-limit experiments retain their original dates and evidence scope.
- Fresh Wren artifact reads measured the selected Votary, Warden and brazier native files, their alternate RED exports, dimensions and material choices. Shared-atlas comparison found 102 changed pixels restricted to the Warden half; the later brazier repaint changes 2,696 pixels. Source FBX/GLB counts and planarity results are saved reports, not newly reproduced here. The supplied bundles do not establish final level placement or gameplay acceptance.
- Dot Lilitari's Kestrel Relay was recovered from her distinct OpenAI Dot conversation and local project. A fresh read of its 14-entry GOB matches the saved release manifest; ten RGB565 MATs have complete 256-square/four-mip payloads, including two multi-cel materials. The source ZIP passes CRC and hashing. This establishes artifact contents and the attributed level/resource lessons; it does not establish Tripo or Blender-Sith use by Dot or repeat her gameplay checks.
- Red's saved radius report was independently reproduced on the supplied Blender scene. The native automatic bound, hierarchy/pivot preservation and imported mesh-radius correction have 29 synthetic regression cases and separate exact-package platform receipts. The reviewed native model retains all 15 nonzero pivots through two import/export cycles. Release validation records the tested platforms, runtimes, source hashes and limits; synthetic checks are not retail or connected-multiplayer tests.
- An independent agent applied the earlier workflow and updated workflow to two concrete conversion/handoff scenarios. The updated instructions passed after correcting the unpaired OBJ/GLB claim and separating the later HD quality verifier from the earlier triangle-merge experiment.
- The existing Python suite ran 46 tests in 1.524 seconds: 45 passed and one native Windows symlink test was skipped because this process lacks that privilege. The earlier F-drive timestamp-fixture failure was rerun successfully with external C-drive temporary storage. Generated data stayed outside the checkout.
- Final package/readback, allowlist, local-link, privacy and research-feed checks are recorded in the release receipt. Older immutable releases remain preserved. The research-only updater changes references; the complete 1.5.0 package is needed for the new SKILL routing.

## Version 1.4.1 evidence review

Checked November 9, 2026. This documentation update adds a bounded historical package comparison and source synthesis. It does not execute a game or certify a current character's multiplayer compatibility.

- Compared the two hash-pinned Guardians of the Galaxy editions. All 171 shared 3DOs, 27 COGs, 91 KEYs, eight PUPs, three DATs and the static JKL are byte-identical; material paths retain local edition differences. The public ledger records exceptions, exact hashes and the concrete shared-model/material example.
- Re-read TIE's January 6, 2002 compatibility statement on the original 16-bit release page. Treat it as author testimony, not a freshly reproduced network result. The 8-bit page was inaccessible through the web tool in this pass; its identification is retained from the exact owned archive evidence.
- Checked the model-name sender/receiver and material-loading explanation against pinned source and existing original-executable byte readbacks. No automatic renderer fallback is established by those inspected paths; the finding is bounded to that evidence.
- Read Lilitari's completed independent advisory on material loading and multiplayer identity. Her proposed experiments are retained as tests to perform; no external adviser is credited with a local runtime result.
- The existing Python suite ran 46 tests: 45 passed and one native Windows symlink test was skipped because the process lacks that privilege. Temporary files and bytecode policy kept generated output outside the checkout.
- Parsed all 22 allowlisted JSON files and checked 253 relative file-link targets: none were missing. These checks do not claim every Markdown anchor or remote URL was verified. The two new public files passed a focused private-path and private-project-term scan.
- The release builder syntax passed; only its version/research-revision constants changed. Final archive/feed hashes and publication readbacks are recorded separately. No historical release is replaced.

## Version 1.4.0 evidence review

Checked November 8, 2026. The update reviews existing source, artifacts and saved collaboration; it does not regenerate models/audio or rerun Blender or gameplay.

- Verified the public Blender-Sith GitHub fork relationship and v1.0.3-coda.1 metadata. Downloaded all three extension ZIPs anonymously, matched published SHA-256 values and inspected their manifests. Its 16-case platform regressions are published historical evidence, not newly executed here.
- Inspected Mando OC source scripts/native files and saved runtime frames. Current hierarchy matches the recorded Kyle contract; a saved all-KEY report has an older model hash and is not presented as final-byte validation.
- Inspected the separate Mandalorian Red archive, native Tripo provenance, three model counts and empty right-hand meshes. All listed GOB hashes matched. Later connected-play claims remain attributed to Red’s saved report.
- Reviewed the Darth Lilitari voice collaboration and packaged v1.2 sound class, preserving the initial cut/mapping corrections and unfinished water coverage.
- Re-read the shipped voice GOB and all nine custom WAV headers: mono PCM, 16-bit, 22,050 Hz. The 86 sound-class rows reference 41 distinct WAV names, including stock dependencies. These are artifact checks, not a new listening or gameplay test.
- The existing Python suite ran 46 tests: 45 passed and the Windows native symlink privilege case was skipped. No converter or gameplay implementation changed in this documentation release.
- A fresh agent applied the revised skill to Tripo conversion, textured Blender-Sith inspection and an existing Suno voice bank before consulting the evidence reports. All 64 local links/anchors in its seven-file exercise resolved. Review caught and corrected two ambiguous phrases; remote links were outside that offline exercise.


## Version 1.3.0 research checks

- The existing Python suite ran 46 tests: 45 passed and the Windows native symlink privilege case was skipped. No new converter, game or Blender runtime test was implied.

- Rehashed all 2,308 current nonempty JK/MotS Massassi packages against acquisition records: zero mismatches. Passive indexing recorded 158,245 native member occurrences and 3,158 documents, with 76 explicitly listed errors/exclusions. Index counts include stock assets, duplicates and scenery; only identified character/weapon/material cases received detailed review.
- Selected MotS package checks covered seven ZIPs, six GOO directories, 187 3DOs, nine KEYs and 1,931 MATs. Three additional skin packages supplied 110 MAT payloads, parsed to exact end of file. No appearance, animation playback or runtime certification follows from these structural checks.
- Reviewed retained model/animation and converter ZIPs, selected source files, included manuals and prior original regression receipts. Historical binaries were not executed. New source-only concerns and earlier test results remain labeled separately.
- Publication checks verify relative links, JSON parsing, privacy markers, package manifests and the unchanged updater's per-file/total size limits. Full release bytes and hosted readbacks are recorded separately from runtime evidence.


Checked 2026-09-30. This record concerns the workflow, its small tools and the sources used to write it. It is not a newly built character or a new game-compatibility certification.

## Version 1.2.0 additions

- Skill-creator validation accepted the updated skill frontmatter and structure in UTF-8 mode.
- The full Python suite ran **46 tests: 45 passed, 1 skipped**. The native Windows symlink test still requires a privilege unavailable to this process; simulated reparse-file and ancestor checks passed.
- A new integration test builds and extracts the actual package builder's synthetic release, verifies that its initial research state refuses a stale revision before replacement downloads, and checks changed/new references on a historical 1.1 baseline while preserving installed scripts, `SKILL.md` and the package baseline.
- Independent reviews checked source attribution and the distinction between author testimony, reported fixes, hypotheses and independent reproduction. Source ledgers retain retrieval sizes/hashes; exact coverage belongs in each chapter. The new forum pass did not execute historical converters, inspect linked artwork, or run a game.
- Three recovered Mortal Kombat ZIPs were compared byte-for-byte across Massassi and JKDF2 mirrors. Independent archive reads checked their GOB contents and the fourteen MK2 model headers. These establish package contents, not a working Cyrax/Sektor skin or a new retail test.
- TDIR source review checked the inspected GOB hashes, entry counts and selected members, corrected the distinction between the older embedded readme and later character-conversion credits, and verified the early DBZ 4.0 alias. The beta binary was not recovered.
- The 1.2 reference feed retains a minimum compatible skill version of 1.1.0. The updater executable is unchanged. Full installs receive initial revision metadata as well as the original package baseline.
- An independent audit applied the complete 33-file research feed to the exact published 1.1 ZIP: 19 references changed, including 12 new files. The upgraded library's 117 relative targets resolved; installed scripts, `SKILL.md` and the original package baseline stayed unchanged. Read-only checks wrote nothing, a repeated update was a no-op, local reference edits refused the entire batch without changing the tree, and a fresh 1.2 install rejected the older 1.1 feed before replacement downloads.
- The final historical Discord review checked all 26 selected source claims against retained own-message evidence and independently recomputed 23 result pages, 472 message occurrences and 459 unique message IDs. This was evidence review, not a second live reading or runtime test.
- Eight offline archive-index fixtures passed, including preservation of all 93 earlier records and merge idempotence. The expanded index contains 170 external endpoints and 28 confirmed Wayback replays; unverified recovery leads remain explicitly marked.

## Version 1.1.0 additions

- The full Python suite ran **45 tests: 44 passed, 1 skipped**. The existing native Windows symlink privilege limitation remains; metadata-based file/ancestor reparse cases pass.
- Twelve Comfy bridge cases use a local synthetic HTTP fixture: native-node preflight, exact image mapping, 3D/latent output records, path containment, explicit execution success, error/timeout capture, and no duplicate submission after an uncertain result. Current local Comfy sources and interpreter versions were inspected; no responsive live Comfy endpoint was found and no new inference was run.
- Eleven research-updater cases cover read-only checks, valid updates, digest corruption, local edits/deletions, version requirements, URL/path boundaries, races and handled rollback. An independent adversarial review caught a rollback overwrite of a concurrent edit and Windows filename aliases; both were corrected and independently rechecked.
- Three reporting-helper tests check exact message digest inputs, header injection refusal and decoded `.eml` readback without overwrite. The helper has no email sender/network implementation. No report was emailed; explicit user consent and an available email route remain necessary before a send.
- A single-distribution test confirms the complete ZIP contains the skill's local baseline, notices and research hashes. Copying the nested skill folder supports later research updates.
- The focused Blender-Sith fork's ten synthetic checks return **4 pass / 6 fail on unchanged upstream** and **10 pass / 0 fail on the fork**, using `bpy` 5.1.2. Root independently reviewed the patch, reran the suite, then extracted the exact installable extension ZIP and ran all ten checks again successfully. See [packaged fork evidence](https://github.com/CodaLilitari/JK-MotS-Model-Workflow/blob/ca011b84c4fa5e1f3755e1543be2c1c1d25063c9/skills/jk-mots-model-workflow/references/evidence/blender-sith-packaged-fork-2026-09-30.json).
- The new testing guide is a source-verified procedure collection. Existing retail, wrapper, camera and source-port results retain their original scope; publishing this guide did not execute a new retail or connected-multiplayer test.

The Blender-Sith fork changes corner-normal preservation and hexadecimal-property handling. It does not fix modifier evaluation, shared-mesh reuse, arbitrary transform semantics or every format version. That 1.0.1 historical release did not add an MCP companion or change Blender core; the later optional MCP companion is described in the current fork guide.

## Retained version 1.0.0 checks

- Skill-creator validation accepted the skill frontmatter and structure, using Python UTF-8 mode and PyYAML 6.0.3 in an isolated validation dependency directory. The skill itself has no PyYAML runtime dependency.
- Python unit suite: **18 tests run, 17 passed, 1 skipped**. Tests cover hierarchy/KEY allocation failures, reserved domains, extra but valid KEY tracks, malformed negative tracks, allowlist boundaries, deterministic ZIP contents and readback, preservation of existing archives, and reparse metadata. The native Windows symlink-creation case was skipped because this process lacked that privilege; simulated file and ancestor reparse rejection passed.
- Fresh synthetic Blender-Sith probe on `bpy` 5.1.2 against upstream `ecc90131`: all four observations completed and source hashes stayed unchanged. It reproduced ignored unapplied modifiers, lost sharp-normal identities, duplicated mesh instances and a KEY metadata callback issue. `--require-fixed` correctly failed against this unfixed upstream. See the [probe record](https://github.com/CodaLilitari/JK-MotS-Model-Workflow/blob/ca011b84c4fa5e1f3755e1543be2c1c1d25063c9/skills/jk-mots-model-workflow/references/evidence/blender-sith-probe-2026-09-30.json).
- Read-only parser corpus: **278 3DO hierarchies and 603 KEY files** parsed without errors. These are separate parser reads, not 881 gameplay tests or model/animation pairings. Specific T80024/custom-TDIR52 pairs passed the narrow checks.
- Two independent scenario exercises confirmed that the skill offers useful alternatives and recommends a bounded route: a stock-motion creature with a successful skull and fused teeth; and an unusual alien for original MotS with optional custom movement/first-person hands.
- A saved ChatGPT Pro advisory contributed independently checked exporter/KEY source findings. That consultation did not execute local game tests.

## Corrections made during review

An inherited recommendation to scale native UVs universally by the texture-size ratio was removed. Current guidance calibrates the actual renderer/export mapping and explains the observed 256-unit normalization counterexample.

The inspector originally ignored malformed negative `NODE` records and falsely rejected valid extra KEY tracks absent from a smaller stock model. Adversarial and original-file cases exposed both; targeted regressions now distinguish them correctly.

The historical Blender-Sith copies were upstream, with external project tools. The new focused fork was developed after that audit and is recorded separately above. Historical source/renderer/runtime findings retain their dates and specific scope in the references.

## Tool scope

`inspect_contract.py` checks declared hierarchy indices, parent cycles, KEY record/allocation consistency and optional detail-domain overlap. It does not inspect full geometry, every child/sibling traversal invariant, PUP inheritance, key timing/interpolation, material content, performance or runtime behavior. Select applicable original-rig reference KEYs before allocation padding when using `--detail-start`.

`build_release.py` packages only the exact `RELEASE_FILES.txt` allowlist, includes per-member sizes/hashes, refuses linked sources and existing archive replacement, and reads back every ZIP member. ZIP integrity is separate from technical correctness and public download availability.

The skill leaves original game data, private project histories, model weights and third-party artwork outside its package. Research measurements and original analysis are portable; other users supply their own permitted inputs and choose their target runtime.
