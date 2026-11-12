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

Linux containment: the engine starts in its own process group on a
private Xvfb display allocated via `-displayfd` with TCP disabled;
`HOME`/`XDG_RUNTIME_DIR` point inside the output session, SDL video
uses X11, and SDL/OpenAL audio use dummy/null backends. Xvfb is required
for this actual Linux capture path. Startup, session setup and execution
failures release the owned display process, readiness pipes and stderr
handle. A descendant that starts a new
session can escape the process group; the receipt records that limit
instead of claiming complete containment.

## Acceptance status: unverified

A `completed` receipt proves only that the staged engine wrote a
well-formed PPM of the requested size. Receipts always carry
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
installation. Full-capture fixtures substitute both the display and the
engine; they retain real request staging, command/environment construction,
hashes, PPM validation and receipts, including failed/timeout cases.
Separate POSIX tests exercise real pipe ownership with controlled process
responses. A Linux staging test executes a real tiny synthetic child from
the staged engine path and checks permissions, bytes and process output.
Platform-specific tests report skips on other operating systems.

These are distinct evidence layers:

| Layer | Requirements | What it establishes |
|---|---|---|
| Synthetic runner suite / `sed_engine_capture_runner` CTest | Python standard library | Runner orchestration, refusal and resource ownership; no real rendering |
| Actual Linux capture | Xvfb and a compatible capture-v1 engine with declared inputs | Exact PPM and unchanged-input receipt; acceptance fields remain unverified |
| Game acceptance | Verified engine, owned assets and camera/resource/gameplay observations | Separate F-09/F-11 acceptance evidence, currently missing |

The Qt-free CI jobs run the synthetic suite without installing Xvfb.
Offscreen EGL/SDL integration would require a separately verified engine
backend; the runner does not silently substitute one. See the
[headless testing design](../../docs/engine_capture_headless_testing.md).
