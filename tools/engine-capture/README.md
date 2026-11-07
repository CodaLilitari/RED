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

Linux containment: the engine starts in its own process group on a
private Xvfb display allocated via `-displayfd` with TCP disabled;
`HOME`/`XDG_RUNTIME_DIR` point inside the output session and SDL/audio
are held to dummy/null backends. A descendant that starts a new
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

Synthetic fixtures only; the full-capture test substitutes a fake
engine that writes a well-formed PPM. It proves runner
orchestration, not engine behaviour.
