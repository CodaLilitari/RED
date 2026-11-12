# Engine capture runner (D-14)

Pattern port of Coda's `sed-engine-capture-v1` capture runner,
including the Linux engine profile (delta row D-14 in
[DELTA_INVENTORY.md](../../docs/port/DELTA_INVENTORY.md)). The runner
is stdlib-only Python: it stages a hash-bound file set into a fresh
output directory, writes `capture-request.txt`, launches the staged
engine, validates the returned `capture.ppm` (P6 RGB8, exact
dimensions), and records a `launcher-receipt.json` with before/after
manifests.

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

Both Linux paths start the engine in its own process group, place
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

`engineTimeoutMs` bounds the child process wait. `runnerTimeoutMs` and
`logLimitBytes` are currently recorded request limits, not enforced
end-to-end limits on the runner or its log files.
