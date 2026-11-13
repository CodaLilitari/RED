# Engine capture runner (D-14)

Pattern port of Coda's `sed-engine-capture-v1` capture runner,
including the Linux engine profile (delta row D-14 in
[DELTA_INVENTORY.md](../../docs/port/DELTA_INVENTORY.md)). The runner
is stdlib-only Python: it stages a hash-bound file set into a fresh
output directory, writes `capture-request.txt`, launches the staged
engine, validates the returned `capture.ppm` (P6 RGB8, exact
dimensions), and records a `launcher-receipt.json` with before/after
manifests.

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
JSON; those UI limits do not change the command-line schema.
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

The runner exports `OPENJKDF2_CAPTURE_REQUEST` (absolute path of the
staged request) and writes the `.openjkdf2-capture-session` marker the
engine requires before it will run isolated. Requests accept an
optional `motsCompat` boolean (default `true`); set it `false` for
JKDF2 episodes so the engine does not demand MotS UI assets.

Both profiles also stage `registry.json` and `openjkdf2_cvars.json`
(role `config`), the episode file and the requested map (role
`scenario`), and any explicit `resource/` files. Every file carries a
SHA-256 identity; sources and staged copies are re-hashed after the
run and any change fails the receipt (`inputsUnchanged`).
Staging grants owner execute permission only to the Linux engine;
ordinary staged data and Windows-profile files gain no execute bit.
This changes permission metadata, not the hash-bound file bytes.

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

## Engine graphics report

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

## Acceptance status: unverified

A `completed` receipt proves that the staged engine wrote a
well-formed PPM of the requested size. Offscreen completion also requires
the matching engine graphics report described above. Receipts always carry
`effectiveCameraObserved: false`,
`runtimeResourceResolutionObserved: false` and
`acceptance: "unverified"`. No capture-capable
`sed-engine-capture-v1` engine build has been verified end-to-end
with owned game assets (`JKMRES.GOO`/`JKMsndLO.goo`/`jk_.cd`), so
this runner is **not** acceptance evidence for the F-09 engine gate
or the F-11 oracle, and an ordinary retail executable is not a
substitute. The gameplay-telemetry extension of the upstream runner
is not ported.

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
