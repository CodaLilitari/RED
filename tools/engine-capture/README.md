# Engine capture runner (D-14)

Pattern port of Coda's `sed-engine-capture-v1` capture runner,
including the Linux engine profile (delta row D-14 in
[DELTA_INVENTORY.md](../../docs/port/DELTA_INVENTORY.md)). The runner
is stdlib-only Python: it stages a hash-bound file set into a fresh
output directory, writes `capture-request.txt`, launches the staged
engine, validates the returned `capture.ppm` (P6 RGB8, exact
dimensions), and records a `launcher-receipt.json` with before/after
manifests. An explicit archive mode stages packaged episodes without loose
overrides. An optional Linux gameplay recipe uses `openjkdf2-agent-v1` ordinary
controls and native observations, with the same staging and process supervision.
Ship `gameplay_capture.py` beside `invoke_engine_capture.py`; no extra Python
package, engine checkout or external movement harness is required.

## Legacy archive triage

Inspect the direct GOB/GOO children of a collection before choosing which levels to restore.
Run with Python 3 and the companion `red-agent` from this source build. For example:

```sh
python3 -B tools/engine-capture/triage_legacy_archives.py legacy-episodes --agent ./red-agent --stock resources/Res2.gob
```

The JSON report on stdout lists each archive by basename, captured byte length and SHA-256, then
each JKL by normalized entry name, size and payload SHA-256. The archive inventory also records whether
`episode.jk` exists. It does not validate episode progression. Supply `--stock` repeatedly in search
order. Each level uses its own episode first and the listed stock archives next within RED's lookup
tiers. Other episodes and loose files never become implicit mounts. Missing and ambiguous resource
paths refer **only to the supplied set**. The ordered `stock` receipts identify those source bytes.
A refused input can lack a digest if its bytes were never admitted.

Read the map's `validation` and `dependencies` independently. Strict validation checks raw JKL
framing, then loads and reserializes the level, inspects geometry (including the 24-corner surface
limit), and checks placed COG parameter references against mounted script schemas. The
reported `rawStructureScope` distinguishes agents that also checked TEMPLATES, MODELS and
THINGS capacities from those that checked MATERIALS and GEORESOURCE alone. A raw rejection
leaves `loaded` false and skips dependency inspection. If validation times out or fails to
return a usable receipt, the separately budgeted dependency inspection still runs and the map
remains incomplete. Counts by issue code and sample addresses show
what to examine. The dependency scan follows supported transitive references and reports observed
missing/ambiguous counts, sampled paths, incomplete traversal and unverified scopes. A code-only
COG resource reference is an example of an unverified scope. The two checks do not compile COGs,
lint all embedded 3DOs, run a game or establish runtime closure. Archive-wide MAT mip advisories from
validation are outside this per-map report.
[Validation contract](../../docs/headless_validation.md) · [Dependency coverage](../../docs/dependency_inspection_design.md)

| Status | Meaning |
| --- | --- |
| `checked` | The supported map checks completed with no reported findings or uncovered dependency scopes. |
| `findings` | A completed check reported an issue, including warnings, missing supplied resources or failed strict framing. |
| `incomplete` | A check refused, timed out, exceeded its budget or disclosed an unsupported dependency scope. Other checks may still have findings. |
| `no-maps` | The archive directory was readable and contained no JKL. |
| `refused` | The archive or its inputs exceeded admission limits, were unreadable or failed archive checks. |

An archive containing multiple maps takes the most conservative map status, with `incomplete` first.
A malformed episode leaves its siblings available. Exit 0 means all selected episodes were checked,
1 means at least one has another status, and 2 means the collection or shared invocation was refused,
including an empty folder or inadmissible stock mount.

The helper admits JK/MotS version 20 containers with a bounded directory at the beginning or end,
NUL-terminated relative ASCII names and disjoint nonempty payloads. It rejects links, duplicate normalized
entries, traversal and nonregular inputs. It reads from private captured copies, stages each JKL under
a fixed name independent of the entry path, and removes its temporary directory on return. It never
rewrites source assets or publishes migrated levels. The receipts describe the captured bytes.

| Budget | Limit |
| --- | ---: |
| Direct directory entries examined / archive candidates / ordered stock mounts | 4,096 / 64 / 4 |
| Source bytes per archive / cumulative captures including stock | 256 MiB / 1 GiB |
| Directory entries per archive | 16,384 |
| JKLs per archive / total admitted JKLs | 32 / 128 |
| Bytes per JKL | 8 MiB |
| Cumulative agent inputs (two map-plus-mount captures reserved per admitted map) | 8 GiB |
| Each agent invocation | 30 seconds / 2 MiB stdout |
| Retained validation examples / unresolved resource samples per map | 16 / 16 |

Validation uses up to 1,000,000 sector pairs, 250,000 surface pairs and 1,000,000 edge work units;
the fan scan allows 1,000,000 triangles and 4,096 findings. Full validation and dependency inspection
also retain the agent's own limits, including 512 MiB of aggregate mounted archives per command.
Each check runs inside a private POSIX process group or Windows kill-on-close job. The deadline covers
waiting for the reply pipe, including ordinary descendants which inherit it. Cleanup allows two more
seconds for the owned worker to exit. Malformed or incompatible receipts leave that map incomplete.
The dependency receipt must use schema version 1. Visible issue, missing and ambiguous
rows must agree with their respective counts, allowing omitted rows only for incomplete detail.
The directory-child limit takes precedence if both directory and archive limits are exceeded.
Validation samples one example per code and preserves counts. Dependency counts are lower bounds
when `scanComplete` is false. `unresolvedTruncated` signals missing sample rows; displayed strings
are shortened to 240 characters. These are static review receipts for selecting a level and check
to follow up in RED.

## Compare two episode releases

From the source checkout, or the directory containing the installed editor executable, run:

```sh
python3 -B tools/engine-capture/compare_episode_releases.py previous/Arena.gob candidate/Arena.zip
```

Requires Python 3.9 or later. Each input can be a bare GOB/GOO or a ZIP with exactly one such member.
The JSON names the input files separately from the installed archive basenames. It reports byte size and
SHA-256 of each inner archive. Sorted `added`, `removed` and `changed` rows identify normalized resource
names and the size and SHA-256 on each available side. `unchanged` counts common resources with equal bytes.
Resource names use lowercase ASCII and forward slashes. Installed basenames retain their spelling in the
report and compare case-insensitively, including the extension.

| Classification | Meaning | Exit |
| --- | --- | --- |
| `byteIdentical` | Inner archive hashes match. | 0 |
| `repacked` | Inner archive bytes differ and every normalized resource name and payload matches. | 0 |
| `payloadDifferences` | A resource was added, removed or changed. | 1 |

An installed basename change sets `installNameChanged` and returns 1 even when the archives are byte-identical.
Invalid input or usage returns 2 with a single JSON `error` on stdout. Both inputs are opened read-only. Each
archive is copied to temporary storage before indexing so its byte hash and its resource hashes describe the
same captured bytes. For a ZIP, the wrapper is also copied before its directory is read. Allow temporary
disk space for the wrapper and the inner episode together. The two inputs are captured sequentially.

The profile admits up to 512 MiB per episode, 16,384 GOB entries, a 1 GiB outer ZIP, 4,096 ZIP members and a
1 MiB classic single-disk ZIP directory. The episode ZIP member must be stored or deflated and unencrypted.
Use ASCII or flagged UTF-8 ZIP names. ZIP64, multidisk and alternate path aliases are unsupported. Unsafe paths, duplicate normalized entries, and overlapping nonempty
GOB payloads are refused. Unrelated ZIP payloads are never decompressed. ZIP compression, wrapper metadata,
documentation and other companion payloads are outside the comparison. It does not inspect file semantics,
prove episode completeness, or establish runtime or multiplayer equivalence. Use the paired texture check
below when comparing intentionally different 8-bit and 16-bit editions.

## Paired retail texture editions

Run the static check on the files players will install. It accepts two ZIPs, bare GOB/GOO
archives, or one of each. Each ZIP should contain one episode archive and can include install notes.

```sh
python tools/engine-capture/verify_episode_pair.py Arena-8-bit.zip Arena-16-bit.zip
```

The command checks the installed archive name, inventory, episode definition, levels, scripts,
models and remaining shared files. Opaque indexed textures may be replaced by 16-bit cels of
matching dimensions. The hardware edition must carry the same bytes for any indexed assets it keeps,
including keyed glass and foliage. Results include the declared, listed and free material slots
per level. Supply `--min-free-materials N` to enforce a project-specific reserve.

For a multiplayer result, host one edition and join with the other, then observe a shared action
from each player. [Retail conversion notes](../../docs/jo_ja_import.md)

## Reproduce the Nar glass repair from source

The source checkout includes an input-specific [Nar glass recipe](recipes/nar_glass/README.md).
It accepts a user-supplied parent archive with the recorded SHA-256, generates the reviewed material,
splits seven fixed visual glass owners and assigns the verified opaque architecture to its earlier draw pass.
It can publish the 16 changed/new resources for inspection, or use an explicit RED agent to validate and
rebuild the exact accepted GOB. Destinations must be new; original game assets are not included.

This case-study recipe is source-only and is not part of the packaged capture runner. It does not add an
automatic importer repair. Read the [lessons and evidence boundaries](../../docs/native_glass_reliability.md)
before applying the approach to another level, particularly a mixed or moving glass owner.

## GUI and inspection callers

RED's **Engine Capture** dialog provides typed camera, profile/backend, timing,
log-budget and explicit-file controls for this runner. It remains separate from
**Save JKL & Test**, which launches an interactive game. Select an installed
Python interpreter and this runner script; the dialog does not install either
Python or an engine. Prepare captures source SHA-256 identities in a background
worker and asks the runner to validate the request. Run uses that exact prepared
request, and the runner rechecks file hashes before executing. Editing request
controls invalidates the prepared request. Request export creates a new file
without replacing an existing one. Import preserves declared hashes and rejects
relative source paths because their meaning depends on the original caller's
working directory. The GUI supports at most 512 explicit files and 2 MiB request
JSON. The CLI also bounds request JSON to 2 MiB, rejects duplicate JSON keys,
and limits archive/gameplay requests to 512 explicit files.
The request form scrolls on smaller displays while the preparation, import/export,
run, cancellation and close actions remain outside the scrolling form.

`python invoke_engine_capture.py request.json --validate-only` validates schema
and source-file metadata without creating output or launching a process. It can
inspect either profile on any host and returns the normalized `request`,
`canRunHere`, and `sourceHashesVerified: false`. Hash verification still belongs
to execution. `--output` is optional in this mode and, if supplied, is not used.
Ordinary invocation still requires `--output` and refuses unsupported hosts.

GUI launch opts into `--cancel-on-stdin`: any input byte or EOF cancels the
supervised operation. Without this flag, stdin does not control the caller.
Importing Python callers may pass an optional `cancel_event` to `run_capture`.
Cancellation before staging refuses without output. Cancellation after admission
releases the owned Job/process group, records `status: failed`, `cancelled: true`,
and incomplete identity evidence (`inputsUnchanged: null`), and retains the same
bounded log and cleanup diagnostics. Cancellation is distinct from timeout.
The GUI closes this ownership channel on Cancel or owner loss instead of killing
the supervisor before it can release its separate POSIX worker group.

Receipt inspection is available without Python. It bounds receipt and image
reads, checks a displayed PPM against the receipt's image hash, and keeps game
acceptance explicitly unverified. A refused new run never adopts an old receipt
already present at the selected output path. macOS can prepare and inspect
requests, but cannot run either capture profile.

## Profiles

The staged executable names select the profile; a profile only runs
on its own operating system.

| Profile | Staged binaries | Runs on |
|---|---|---|
| windows | `openjkdf2-64.exe`, `OpenAL32.dll`, `libGameNetworkingSockets.dll` | Windows |
| linux | `openjkdf2` (system OpenAL) | Linux |

In posed mode the runner exports `OPENJKDF2_CAPTURE_REQUEST` (absolute path of the
staged request) and writes the `.openjkdf2-capture-session` marker the
engine requires before it will run isolated. Requests accept an
optional `motsCompat` boolean (default `true`); set it `false` for
JKDF2 episodes so the engine does not demand MotS UI assets.

Both profiles also stage `registry.json` and `openjkdf2_cvars.json`
(role `config`), the episode file and the requested map (role
`scenario`), and any explicit `resource/` files. Every file carries a
SHA-256 identity; sources and staged copies are re-hashed after the
run. Legacy loose posed captures fail on any change (`inputsUnchanged`).
The new archive/gameplay workflows allow only the engine's local staged
configuration writes, with separate immutable-input evidence described below.
Staging grants owner execute permission only to the Linux engine;
ordinary staged data and Windows-profile files gain no execute bit.
This changes permission metadata, not the hash-bound file bytes.

## Packaged episodes without loose files

Add `"episodeArchive": "episode/moonwell.gob"` for an episode named `moonwell`,
and declare that file with role `scenario` and its SHA-256. The selected path
must be exactly `episode/<episode>.gob` or `episode/<episode>.goo`. In this mode
every scenario file is a direct `episode/*.gob` or `episode/*.goo` container;
additional explicitly declared base-game containers such as `episode/jk1.gob`
are allowed. Loose selected-episode files are refused. Nothing is extracted.
Omitting `episodeArchive` preserves the existing loose episode/map requirement.

After copying and hashing the selected container, the worker validates its
native JK GOB v0x14 index: exact magic/version/index layout, 1..65,536 entries,
16-byte to 2-GiB archive size, bounded payload ranges, NUL-terminated ASCII
names, lowercase backslash spelling, no traversal and no duplicate names.
It requires `episode.jk` and `jkl\\<lowercase-map>` entries. Unsupported archives
fail before starting an engine. Metadata-only `--validate-only` does not read
or qualify the archive directory; the full check belongs to the supervised run.

`episodeArchive` in the launcher receipt records the container hash, entry
count, selected map entry and `looseEpisodeFilesStaged: false`. This is structural
package evidence. It does not prove which archive supplied every runtime asset.
The existing package planner remains responsible for dependencies and exclusions.
Packaged posed captures use the same explicit staged-configuration policy as
gameplay: normal settings writes are listed, while original sources and all
staged non-config files must retain their hashes. PPM, graphics-report and
exit-status validation are unchanged. Omitting both new fields preserves the
legacy loose capture's strict all-staged-files policy.

## Ordinary gameplay capture

Add a `gameplay` object containing the existing OpenJKDF2 scenario-v1 recipe.
Its `launch` must exactly agree with the outer request: `game` is `jk` when
`motsCompat` is false and `mots` when true; `episode` and `map` match verbatim.
Declare `resource/jk_.cd` with role `resource` and the remaining owned inputs
explicitly. The staged four-byte install marker must match the selected game.
This mode requires a native Linux engine built with agent hooks and the
`openjkdf2-agent-v1` protocol. Windows gameplay requests are refused; macOS may
inspect Linux requests but cannot execute them. There is no fallback to posed
capture, no paid service, and no shell-command field.

The recipe is inline, bounded to 256 KiB, and can be saved in a separate JSON
file for the GUI's ordinary **Load gameplay recipe** control. For example:

```json
{
  "schemaVersion": 1,
  "name": "walk-and-use",
  "launch": {"game": "jk", "episode": "example", "map": "example.jkl"},
  "steps": [
    {"op": "wait", "expect": {"levelActive": true, "player": {"notNull": true}},
     "timeout": 25, "retryTap": {"key": "return", "seconds": 0.08, "every": 1}},
    {"op": "checkpoint", "name": "spawn"},
    {"op": "tap", "key": "up", "seconds": 0.2},
    {"op": "assert", "distanceFrom": {"checkpoint": "spawn", "min": 0.01}},
    {"op": "tap", "key": "space", "seconds": 0.07},
    {"op": "checkpoint", "name": "after-use"}
  ]
}
```

The example tests movement and retains evidence after Use. It does **not** assert
an interaction succeeded. Add expectations against the actual level's native
state or inspect its visible consequence. The engine's normal bindings decide
what a key does. Save and Load use ordinary keys and menus; this runner neither
imports saves nor sends COG messages.

| Step | Contract |
|---|---|
| `wait` | 1..32 dot-path comparisons; timeout up to 120 s; optional stable interval and bounded ordinary retry tap |
| `assert` | Literal/null/bool/string/array comparisons, finite min/max ranges, `notNull: true`, or distance from an earlier checkpoint |
| `observe` | Check the same expectations on every native state reply over 0.05–30 seconds with an explicit maximum sampling gap of 0.1–2 seconds |
| `checkpoint` | Named native state, plus current-view capture by default; `capture: false` records state only |
| `tap` | Named key or SDL scancode 1..255; hold above zero and at most 1.5 s, followed by key-up |
| `command` | Only `key CODE 0|1`, relative `mouse DX DY` (each ±2048), `button 1..5 0|1`, printable ASCII `text` (1..127 characters), or `release` |
| `sleep` | Ordinary wall-clock wait above zero and at most 30 s |

Recipes have 1..256 steps with unique simple names and at most 31 requested
capture checkpoints. One additional capture slot is reserved for failure
evidence. Field paths are at most 128 characters; comparison arrays have at most
256 elements and depth 4. Unsupported operations, extra fields, non-finite values
and forward checkpoint references are refused. Input changes normal real-time
gameplay; it never advances a synthetic clock or writes player/mission state.

Gameplay launches `-agent -autostart -sp -windowGUI`, preserving the HUD and
current player camera. `camera` may be omitted. If supplied by the GUI it is
validated for compatibility but ignored; camera/frame controls are disabled and
the receipt contains no `requestedCamera` or `requestedFrame` claim. The controller
creates a private agent session, clears posed-capture intent, verifies the native
protocol handshake, and requires acknowledged `release`/`quit` plus process exit 0.

Every request and exact ID-matched response is retained in `agent/commands.jsonl`.
`scenario-input.json` records the admitted recipe; `scenario.json` retains each
step's status, response IDs, native state, comparisons and captures. A failed
assertion, mismatched/missing response, exhausted budget, cancellation or failed
shutdown cannot become a successful receipt. An unconsumed command is never
overwritten. The controller permits at most 4,096 commands, 1 MiB per response,
16 MiB journal/receipt data and 192 MiB total capture bytes. Images require exact
P6 payloads, dimensions no larger than 2048 per side and at most 2,097,152 pixels.
Source data is streamed during staging, so archive size is not a whole-file
allocation requirement.

The compact launcher `gameplay` summary binds the scenario, controller and
journal hashes and lists each native image's path, hash, observed dimensions and
step. Receipt inspection offers these frames individually and verifies their
hashes. Imported receipts are reported evidence; the GUI does not claim to have
replayed them. The normal agent protocol does not report SDL/GL identity, so
`graphicsBackendObserved` remains false even when SDL offscreen was requested.
Posed-mode sidecar requirements below remain unchanged.

The engine may rewrite **staged** `config` files during ordinary shutdown.
`manifest-after.json`, `inputsUnchanged` and `stagedConfigChanges` report that
honestly. Archive/gameplay capture succeeds only when `immutableInputsUnchanged` is true: every
original source and every staged non-config input still matches its hash.
`mutableConfigDestinations` names the explicit exception. Original configuration
files are never edited. A timeout/cancellation leaves identity evidence incomplete.

Destinations use portable POSIX-style relative paths for both profiles. Admission
rejects empty or dot components, backslashes, control characters, Windows drive
or stream syntax, reserved device names, trailing dots/spaces, and characters
invalid in Windows filenames. It refuses case-insensitive duplicate files and
file/directory prefix collisions instead of normalizing them. Runner-owned file
and session-directory names are reserved case-insensitively at the output root;
unrelated nested paths such as `episode/session/episode.jk` remain valid. Ordinary spaces
inside a name and nested resource paths remain supported.

Staging rechecks raw destinations and resolved containment before creating
directories and before exclusive file creation, so an existing parent symlink
or Windows junction cannot redirect writes outside the output session. Post-run
staged identity checks also require containment. These checks protect the owned
staging workflow; they do not sandbox a hostile same-user process racing to
replace directories while filesystem operations are in progress.

PPM validation reads and checks the exact requested header first, then reads at
most the requested RGB payload plus one byte. A malformed or growing capture
cannot cause a whole-file allocation beyond that known pixel budget. Oversized
payload diagnostics report the bounded observed size, not an invented total.

### Check a gameplay interval

Use `wait` to reach a sector within a deadline and `assert` to check a single state. Use `observe`
to catch a brief violation that could disappear before the next checkpoint. For example, after
the relay bay lift reaches its upper stop, add this step to the scenario to check the standing
height and sector for a second:

```json
{"op": "observe", "name": "standing-on-upper-stop", "seconds": 1, "maxGapSeconds": 0.25,
 "expect": {"levelActive": true, "player.sector": 1, "player.position.2": {"min": 0.56, "max": 0.58}}}
```

For a moving check, issue an ordinary key-down with a `command` step, observe across the
hold, then issue its key-up. Choose the sector or height limits for the intended route.
Crossing a doorway after Use observes player passage. A specific door's internal state needs
a field from the engine that actually identifies and reports it.

In `scenario.json`, the step's `observation.samples` entries point to acknowledged `state`
requests in `agent/commands.jsonl`. Each entry gives the request ID and elapsed controller
times bracketing that sample. The final request starts at least `seconds` after the first
reply. `observation.maxGapSeconds` is the largest conservative gap: the first round trip
or the time from the previous request's start to this reply, including both request latencies.
The interval uses wall time. These samples can miss events between polls or while gameplay is paused.

| Observation outcome | Meaning |
|---|---|
| `passed` | Every sampled comparison matched, including a terminal sample, with all gaps within the requested limit. |
| `failed` | A comparable value violated an expectation within the sampling-gap limit. |
| `inconclusive` | A required field was missing or had an incompatible type, coverage was too sparse, or the controller could not finish sampling. |

Each mismatch identifies the field, expected value, native JSON type and request ID.
Small scalar actual values appear inline. The identified journal reply contains the full value.
A sample received after an excessive gap retains its mismatch details and timing, and the
interval is inconclusive even when that sample violates the bound.

A failed or inconclusive interval stops the recipe and fails the launcher even if a frame was
captured. A hard interruption can leave a running step without a final report. Only a completed
launcher receipt with `immutableInputsUnchanged: true` qualifies the recorded observations.
Its `gameplayInputs` and the scenario's `inputs` identify the exact admitted request bytes,
staged engine, packaged episode (null for loose gameplay), and normalized recipe by SHA-256.
Before staging, the worker checks the handoff against the exact request digest supplied
by the supervisor through its admission pipe. The gameplay summary binds the final report and command journal; completed runs recheck
the retained request, recipe, report and journal after engine exit. These hashes identify the evidence
from this run. General game acceptance remains unverified.

## Linux graphics backend selection

Linux requests accept an optional `graphicsBackend` with exactly one of
these values:

| Request value | Child video driver | Display requirement |
|---|---|---|
| Omitted, or `"xvfb"` | X11 | A private Xvfb server allocated via `-displayfd`, with TCP disabled |
| `"sdl-offscreen"` | SDL offscreen | A compatible engine and SDL offscreen OpenGL backend; no display server is started |

For example, add `"graphicsBackend": "sdl-offscreen"` to an otherwise
complete Linux request. Windows requests reject this field. Omission
keeps the existing normalized request shape and selects Xvfb on Linux.
The Windows child clears inherited Linux capture-backend intent, preserving
the engine's native selection without changing the parent environment.
The four-line `sed-engine-capture-v1` request file is unchanged; the
backend is passed separately through `OPENJKDF2_CAPTURE_BACKEND`.
There is no automatic retry with a different backend.

Both Linux paths place the worker, engine and display in one owned process group, place
`HOME` and `XDG_RUNTIME_DIR` inside the output session, and set SDL
audio to `dummy` and OpenAL to `null`. Both the SDL3 environment names
(`SDL_VIDEO_DRIVER`, `SDL_AUDIO_DRIVER`) and their legacy names
(`SDL_VIDEODRIVER`, `SDL_AUDIODRIVER`) are set consistently.

Offscreen removes `DISPLAY` and `WAYLAND_DISPLAY` from the child
environment and requests Mesa software rendering with
`LIBGL_ALWAYS_SOFTWARE=1`, `GALLIUM_DRIVER=llvmpipe`, `LP_NUM_THREADS=1`
and `MESA_SHADER_CACHE_DISABLE=true`. The parent environment is unchanged.
These settings describe what the runner requested; observed graphics
information must come from the engine report below.

Xvfb startup, session setup and execution failures release the owned
display process, readiness pipes and stderr handle. A descendant that
starts a new session can escape the engine process group; the receipt
records that limit instead of claiming complete containment.

## Posed-mode engine graphics report

Offscreen completion requires `capture.ppm.json` as well as a valid PPM.
The engine sidecar must be a JSON object of at most 16 KiB with this
`graphics` object (the GL values below are illustrative placeholders):

```json
{
  "graphics": {
    "schemaVersion": 1,
    "requestedBackend": "sdl-offscreen",
    "sdlVideoDriver": "offscreen",
    "sdlVersion": "3.4.12",
    "glVendor": "engine-reported vendor",
    "glRenderer": "engine-reported renderer",
    "glVersion": "engine-reported GL version"
  }
}
```

The `graphics` fields are exact: missing or additional fields fail
validation. `schemaVersion` must be the integer `1`, `requestedBackend`
must be `sdl-offscreen`, and the observed `sdlVideoDriver` must be
`offscreen`. All strings must be nonempty printable ASCII. Driver and
SDL version strings are limited to 31 characters; each GL string is
limited to 255. `sdlVersion` must contain three decimal components in
`major.minor.micro` form. If the sidecar also reports `width` or `height`,
each must match the requested dimension.

The launcher records its Linux selection as `requestedGraphicsBackend`
and copies accepted offscreen graphics observations as
`engineReportedGraphics`. The complete sidecar is retained as
`engineReportedReceipt`, with its hash in `engineReceiptSha256`.
These are engine reports, not independent renderer attestation.
Missing, malformed or mismatched offscreen reports fail the capture
even when its image is valid.

Legacy Xvfb and Windows sidecars remain optional and retain their
existing validation. Their contents, including any unknown graphics
fields, remain in `engineReportedReceipt`; they do not gain the
separately validated `engineReportedGraphics` field.

## Acceptance status and observed scope

A posed `completed` receipt proves that the staged engine wrote a
well-formed PPM of the requested size. Offscreen completion also requires
the matching engine graphics report described above. Receipts always carry
`effectiveCameraObserved: false`,
`runtimeResourceResolutionObserved: false` and
`acceptance: "unverified"`. A gameplay completion establishes only the explicit
recipe assertions and a clean native protocol shutdown. It is not automatic
mission completion, serializer fidelity or F-09/F-11 closure. An ordinary retail
executable cannot substitute for a build supporting the selected protocol.

An actual Linux smoke run through this runner used engine source
`10902f0230f25f7d010fa1bbb0acd5de736e629d`, binary SHA-256
`b928df1da12ee4ce8852765f09d9b002a12670acc5807e839ec9294ef3c6364b`, and the unchanged
Moonwell v3 custom archive SHA-256
`ab19b044e834cc54c5584415b0f1c50ad0b1bfd5dd31929ba66f4df9b44e8734`.
It loaded with no loose episode files, observed movement, native power-object
activation and repeated activation with the phase unchanged, and retained three
current-view images plus 167 command records. This qualifies that bounded
ordinary-gameplay route on that engine and package. Full mission completion,
Windows/MotS gameplay, and general effective-camera/resource-resolution
qualification remain separate. The original journal and package/binary identities
are preserved in the accompanying qualification checkpoint; no stock payload is
part of this repository.

## Tests

```
python -m unittest discover -s tools/engine-capture/tests
```

The standard-library suite requires no Xvfb, display socket, GPU or game
installation. Synthetic display/engine fixtures retain real request
staging, command/environment construction, hashes, PPM validation and
receipts, including failed/timeout cases. Offscreen execution tests run
real staged synthetic children and inspect their actual environment,
process-group identity, output and receipts. They cover missing or mismatched
graphics reports, child failure and timeout without backend fallback.
The pixels and graphics reports in those fixtures remain synthetic.
Separate POSIX tests exercise real pipe ownership with controlled process
responses. A Linux staging test executes a real tiny synthetic child from
the staged engine path and checks permissions, bytes and process output.
Platform-specific tests report skips on other operating systems.
The host-independent orchestration and budget fixtures explicitly replace only
engine-profile admission when they already use a synthetic engine or Python
child. They therefore run on macOS without claiming a supported macOS engine
profile. Separate CLI checks preserve rejection of both engine profiles on
Darwin before any output directory is created. On other hosts those rejection
checks inject Darwin platform facts into the real CLI admission boundary;
native macOS CI invokes the unmodified command directly.

These are distinct evidence layers:

| Layer | Requirements | What it establishes |
|---|---|---|
| Synthetic runner suite / `sed_engine_capture_runner` CTest | Python standard library | Runner orchestration, refusal and resource ownership; no real rendering |
| Actual Linux Xvfb capture | Xvfb and a compatible capture-v1 engine with declared inputs | Exact PPM and unchanged-input receipt; acceptance fields remain unverified |
| Actual Linux offscreen capture | A compatible capture-v1 engine with SDL offscreen OpenGL support and declared inputs | Exact PPM, unchanged inputs and matching engine-reported graphics; acceptance fields remain unverified |
| Actual Linux ordinary gameplay | Agent-enabled engine, explicit recipe and owned inputs | Native journal, explicit assertions, current-view images and immutable input identities; no automatic mission verdict |
| Game acceptance | Verified engine, owned assets and camera/resource/gameplay observations | Separate F-09/F-11 acceptance evidence, currently missing |

The Qt-free CI jobs run the synthetic suite without installing Xvfb.
Real SDL offscreen drawing/readback is a separate engine integration check;
synthetic runner tests do not establish renderer support. See the
[headless testing design](../../docs/engine_capture_headless_testing.md) and
[explicit offscreen contract](../../docs/engine_capture_offscreen_plan.md).

## Time and log budgets

`engineTimeoutMs` bounds the engine process wait. `runnerTimeoutMs` supplies
one wall-clock budget for the admitted capture: output preparation, staging,
private-display setup, engine execution, image/sidecar validation and post-run
source/staged identity checks all consume the same budget. It starts after
request JSON/schema and source-metadata admission and before creating the output
directory. The supervisor runs this work in a separate Python process, so a
stalled staging or collection operation can be stopped on Windows as well as
Linux. Calling `run_capture` from an importing Python program uses the same
supervision as the CLI; no multiprocessing main-module guard is required.
Posed mode keeps its 30,000/45,000 ms defaults and 60,000/90,000 ms maxima.
Gameplay uses 180,000/210,000 ms defaults and 600,000/630,000 ms maxima;
both budgets have a 1,000 ms minimum and the runner budget must cover the engine
budget. Startup, commands, ordinary waits and acknowledged shutdown share the
engine budget. The outer supervisor still bounds staging and post-run collection.

Windows uses an owned kill-on-close Job Object; the worker waits for admission
until it has been assigned to that job. Linux uses an owned process group.
Cleanup targets only that job/group, including descendants left by a normally
exited engine. A POSIX descendant that deliberately starts a new session can
escape the group; this remains a containment limitation. Normal completion,
failure and timeout all release the owned scope. A separate two-second grace
bounds waiting for the terminated worker. Request admission, operating-system
termination latency, and the supervisor's small final receipt/log-metadata writes
are outside the work deadline; it is not a hard real-time promise for a blocked
filesystem or operating system.

On expiry, `runnerTimedOut` and `timedOut` are true, status is `failed`, and
`failureStage` names the last atomic worker checkpoint. `identityCheckComplete`
is false and `inputsUnchanged` is null: the supervisor does not perform an
unbounded second input-hash pass or claim unchanged inputs after interrupting
the first one. `runnerDeadlineScope`, `runnerDurationMs` and `cleanupGraceMs`
describe the boundary; `cleanupComplete`/`cleanupError` separately report owned
cleanup failures. The private `.admitted-request.json` and
`.capture-progress.json` files preserve the admitted request and latest complete
checkpoint inside the output session.

`logLimitBytes` is one **aggregate retained-byte budget** shared by
`engine-stdout.log` and `engine-stderr.log`. Both pipes are continuously drained;
bytes beyond the budget are discarded without blocking a noisy child. The
distribution between streams depends on their arrival order. Each receipt entry
records retained `bytes`, their `sha256`, total `bytesObserved`, and
`truncatedForReceipt`. Reaching the limit exactly does not imply truncation.
Truncated logs alone do not invalidate an otherwise verified capture.

Failed engines retain the same bounded log evidence. If the worker is stopped
before its drain counters finish, the supervisor hashes only the retained files;
`bytesObserved` and `truncatedForReceipt` are null with
`observationComplete: false`. It does not invent how much output was discarded.
Log limits cover the runner's stdout/stderr capture, not arbitrary files an
engine itself may write into its session.

The synthetic suite also executes real supervised Python children for stalled
preparation, stalled post-run collection, combined preparation/process deadlines,
successful and failing noisy engines, exact-limit logs, timeout receipts, and
owned-descendant cleanup while an unrelated process remains alive. These tests
use no game data or graphics backend and remain orchestration evidence only.

## Qualify two multiplayer endpoints

For multiplayer, use `python3 -B peer_qualification.py profile.json`. It checks independently recorded host and guest input identities, late join, authored starts, route replication, weapon and thrown-object contacts, pickup and lift state, death/respawn, and a deliberate synchronization failure. Its JSON reports the target, transport, native frame intervals, allocated and remaining Thing slots and the outcome of each observation.

Each movement check reports two results. `endpointAgreement` means both observers saw the player start and stop within 0.05 JKU of each other after moving at least 0.05 JKU. `routeAgreement` follows the whole walk: every position one observer recorded must lie within 0.05 JKU of the other observer's route, and the remote observer must not fall back along the route by more than that. It compares paths rather than timestamps, so replication lag between independent clients is allowed. Each observer needs at least three timed positions, no more than 250 ms and 0.25 JKU apart; otherwise the route is `unverified`. A movement passes only when the endpoints agree and the route `agrees`. Matching endpoints alone could hide a detour: a remote route that wandered 4 JKU and came back to the same end once passed. In the retained loopback recording, the routes stay within 0.012 JKU of each other. A public numerical example, bound recipes and the exact Linux target procedure are available in the [two-client scenario](examples/peer-profile/README.md).

## Profile a stationary view and an ordinary route

Use `route_performance.py PROFILE.json --html comparison.html` with Python 3.10 or later to import an experiment. The CLI prints the structured result and creates a new standalone HTML file. Expand a repeat to inspect the player path, visibility and collision overlays, counter maxima and exact input hashes. The importer runs locally on all three editor platforms and never starts an engine.

To collect the 2×2 matrix on Linux, prepare two admitted gameplay launch requests for the same source map and stock resources, one exported with model-scene geometry and one with hybrid sectors. Give them the same render-mesh collision, input, camera and physics settings. Supply one stationary and one ordinary forward recipe, each with ordered `warmup`, `begin` and `end` checkpoints and a gameplay assertion. Set checkpoint capture to false within the timing window. Three interleaved repeats yield 24 trials. The output directory must be new and large enough for 24 private staged sessions, including their game data.

```sh
python3 -B collect_route_profile.py model-request.json hybrid-request.json stationary.json walk.json runs \
  --cpu "observed CPU model" --class software --physics "stock JK single-player"
python3 -B route_performance.py runs/profile.json --html comparison.html
```

The collection adapter expects an opted-in native observer, with one CSV row per active pre-swap and monotonic nanosecond timestamps, observed GL renderer, driver, drawable, swap interval and cap, and level-open confirmation that the optional cache was actually allocated. Stock game executables do not emit this evidence. The source examples include the observer delta used on OpenJKDF2-MotS-Fixes `f554833fd008eee0ba9c1cd09918a815441b56d8` with the code changes from its optional [floor-cache PR](https://github.com/CodaLilitari/OpenJKDF2-MotS-Fixes/pull/8). Apply it to a separate consented engine build, with agent hooks enabled, when the format and version match. All three changed engine translation units must see the cache-enabled headers. Keep the stock data in private launch storage. [Original observer, recipes and measured 24-run report](examples/route-profile/observed-two-room.md).

Each run in `profile.json` identifies the route, geometry, cache setting, repeat index, three input SHA-256s and the common target. Its relative path and SHA-256 evidence pairs identify the native CSV, observed cache mode and target, launcher receipt, staged manifest and scenario report. The importer checks start and end player positions and view angles, the resource set, declared controls, hashes and consecutive native frames. A walking endpoint separation above 0.075 JKU or start difference above 0.005 JKU rejects the paired comparison. Paired runs must also follow the same route: every recorded position of one run must lie within 0.075 JKU of the other run's path (`routeAgreement`, `routeDeviation`). Two runs that share their endpoints but pass through different places may meet different geometry, so their timings are not compared. Routes are thinned to points at least 0.02 JKU apart before comparison; a route too long to resolve at a third of the tolerance is `unverified`. A timing improvement requires both median and p95 to fall below the entire baseline repeat range. Per-repeat p99 remains available for inspection.

Record authored changes in `semanticChanges`. Optional `optimizationReviews` entries reference a JSON file by relative path and SHA-256. Bind it to `baselinePackageSha256` and `candidatePackageSha256`. Under `checks`, provide `silhouettes`, `transparentOpenings`, `sourceCoverage` and `gameplayCollision` with `result` set to `passed`, `failed` or `unobserved`. Every observed check includes its own `evidence` path and SHA-256. A failed check rejects the optimization even when it is faster. The report attributes these judgments to the supplied review. Hardware, retail, and unsupported counters need their own observations.

### Reuse a current packaged preview frame

For the packaged posed OpenJKDF2 route only, check a previous local capture
before spending time on another launch:

```sh
python invoke_engine_capture.py request.json --check-preview session-directory
```

A current result (exit 0) reports the exported request revision plus package,
executable and frame SHA-256 values. The check rehashes all current source
files and revalidates the saved PPM and any engine sidecar; it starts no
process and writes no files. Use an ordinary capture with a **new** output
directory if the check refuses (exit 2). Refresh declared file hashes when
exported inputs change. Reuse requires a successful final receipt atomically published by the supervisor after
cleanup. Older receipts without that confirmation require a new run.

The revision identifies the normalized capture request, including file rows,
pose and limits, not unsaved changes in the editor. A moved source or reordered
row conservatively invalidates the check. Before returning, the checker rereads
the normalized request and rechecks file identity, size and timestamps for sources,
the final receipt, the frame and its sidecar. A current result describes this
optimistic observation. A subsequent edit requires a fresh check before reuse. This checks evidence from a trusted
local launcher, not a signed attestation. A fresh run requests the same pose;
it does not restore a prior player state or observe the effective camera. There
is no in-process or retail reload, and frame identity is not game acceptance.
