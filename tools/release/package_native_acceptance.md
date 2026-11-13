# Extracted package native installation acceptance

`accept_package_native.py` runs the installed editor's public
[`--check-installation` workflow](../../docs/installation_check.md) from a fresh,
hash-verified package extraction. It uses the same product operation available
through **Help > Check Installation**. It records
`nativeInstallationAccepted: true` only after verifying native rendering evidence,
installed data identities and unchanged package/storage sentinels.

This gate complements [package CLI acceptance](package_cli_acceptance.md). It does
not exercise an ordinary user's editing, file-picker, docking, selection, or
save/reopen workflow. `guiRenderAccepted` and `guiWorkflowAccepted` remain `false`;
the separate ordinary GUI checkpoint is still required. The synthetic Python
tests validate this harness, not an actual editor build or GPU.

## Invocation

Run from a trusted source checkout with standard Python 3.10 or newer. No Python
dependencies are installed. Supply the exact archive SHA-256, release version,
platform and source commit from the candidate's trusted build record:

```text
python -B tools/release/accept_package_native.py /absolute/RED.zip \
  --archive-sha256 <64-lowercase-hex-digits> --version 0.6.0 \
  --platform windows-x86_64 --commit <40-lowercase-hex-digits> \
  --receipt /absolute/native-acceptance.json \
  --scratch-root /absolute/external-scratch --timeout-seconds 180
```

Use the shell's own continuation syntax or enter this as one command. Supported
native targets are `windows-x86_64`, `linux-x86_64` and `macos-arm64`; the running
host and architecture must match. The native Qt platform must report `windows`,
`xcb`/`wayland`, or `cocoa`, respectively. A Linux Xvfb display is permitted when
it hosts the actual XCB plugin and an OpenGL context; an offscreen Qt plugin is
refused. Windows and macOS need an available native graphical session. The check
opens only its own diagnostic shell; coordinate it with other foreground tests.

The receipt destination must be new. Complete JSON is flushed into an owned
sibling temporary file and atomically linked to the destination without replacing
an existing file. A filesystem that cannot provide this create-only publication
fails explicitly. Exit codes are `0` for accepted, `1` for a rejected check with a
published failure receipt, and `2` for argument or receipt publication errors.

## Admission and ownership

The helper reuses the CLI gate's archive metadata bounds, portable path admission,
exclusive extraction, symlink resolution, manifest coverage, layout checks,
macOS signature checks and owned worker supervisor. It does not implement a
second archive parser. The archive is copied into private scratch under a
4-GiB limit while checking its expected hash. Every extracted payload must match
the complete manifest before a command runs and again afterward. The source
archive and manifest hashes must remain unchanged.

The Unix manifest binds the expected `sourceCommit`. The legacy Windows manifest
may omit that field, as documented by the CLI gate; in that case
`manifestSourceCommitVerified: false` is retained and source provenance still
requires the trusted build record associated with the exact archive hash. A
present commit must match. The native child must identify the same installed
executable, app version and exact Qt version recorded by its package. macOS app
and companion signatures must verify after extraction before editor execution.

One overall supervisor deadline covers worker startup, extraction, validation,
native execution and final verification. Its default is 180 seconds, configurable
from 1 to 600. The native editor command has a further 35-second bound around the
product's own 30-second controller deadline and owned cleanup. The supervisor
terminates only its Windows Job or POSIX process group, including descendants;
it never searches for or kills another editor. Combined stdout/stderr is bounded
by the shared command runner. Failed and timed-out runs retain an honest failure
receipt and any available progress.

The helper strips inherited Qt, SDK, Python and `SED_` overrides, uses system
tooling paths, and redirects home/config/data/cache/temp environment locations
into its owned profile. The native report directory deliberately contains ordinary
Unicode (`native-report-漢字-Δ`) on every platform. Scratch, extracted files,
native request/report, frame PNGs and command records remain outside the checkout
for inspection. The helper does not remove this evidence automatically.

## Independent native evidence

The public command's stdout must agree with its bounded persisted report. Parent,
request and child UUIDs and executable paths must agree. Native success requires
the full shell to be exposed, a valid context, a linked scene shader and all
bundled runtime data to have passed their authoritative product readers. The
receipt must include nonempty OpenGL vendor, renderer and version identities.

The gate reads the exact two named frame files, verifies their sizes and hashes,
and independently decodes the bytes it hashed. Its deliberately limited PNG
reader accepts noninterlaced 8-bit RGB/RGBA, validates chunk framing and CRCs,
requires contiguous image data and exact compressed-stream termination, and
supports all five [PNG row filters](https://www.w3.org/TR/png-3/#9Filters).
Images are bounded to 8 MiB and dimensions of 64 through 4096; inflated data is
limited to the exact expected scanline count plus one byte. Unsupported critical
chunks, palette/transparent-color formats, animation and trailing data refuse
acceptance rather than receiving a partial interpretation.

Each decoded frame must contain three nonoverlapping, interior 7-by-7 probe
neighborhoods. The first face changes from opaque bright to opaque dark, the
second from dark to bright, and a background region stays dark. Their dimensions
must agree and their hashes must differ. A child that reports success with a
blank, stale or corrupt frame is rejected. The independent decoder classifies
the actual pixels rather than trusting a child-supplied pixel summary. It also
matches the exact eleven parsed runtime-data names and their byte hashes against
the extracted package. This gate trusts the designated build's executable; it is
an installation acceptance harness, not a sandbox for malicious binaries.

## Storage evidence and limits

Owned synthetic preference/recovery files and an absent-file sentinel must retain
their exact bytes or absence. Additional explicit absolute regular-file paths
may be supplied using repeatable `--user-storage-sentinel`; at most 32 are
accepted, each bounded to 8 MiB and never followed through a file symlink. Missing
sentinels must remain missing. The helper neither discovers nor enumerates the
user's real home, settings or recovery directories.

The receipt reports `sentinelScope: owned-profile-and-explicit-files` and
`registryStateVerified: false`. Redirected environment variables and these
sentinels do not prove that all native registry or operating-system preferences
were untouched. The product's early isolated-storage contract is checked by its
separate startup tests; the package receipt must not overstate this scope.

## Focused tests

```text
python -B -m unittest discover -s tools/release/tests -p test_package_native_acceptance.py -v
```

Fixtures contain generated pixels and synthetic data only. Tests exercise every
supported filter/color combination, hostile chunks and inflation bounds, native
identity and report mismatch, explicit sentinels, changed archive/package bytes,
atomic publication, actual subprocess Unicode arguments, deadlines and failure
receipts. Native release acceptance requires running the helper against each
actual delivered archive on its matching host, then retaining its report and
frame evidence alongside the separate CLI and ordinary GUI receipts.
