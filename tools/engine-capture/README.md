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
