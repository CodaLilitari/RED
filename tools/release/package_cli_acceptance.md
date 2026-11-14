# Extracted package CLI acceptance

`accept_package_cli.py` checks an exact release archive on its matching native
host with Python 3.11 or newer and the standard library. Run it from a complete
source checkout: its supervisor reuses the owned-process implementation in
[`../engine-capture/invoke_engine_capture.py`](../engine-capture/invoke_engine_capture.py).
It does not install Python, Qt, game assets, or other dependencies.

This gate establishes **package CLI acceptance**. It does not launch the editor,
claim a rendered frame, test an engine, or replace the complete Release CTest
gate and extracted-package native GUI check. Every receipt explicitly records
`acceptanceKind: "package-cli"` and `guiRenderAccepted: false`.

## Invocation and trust

After staging, signing, inventory generation, and archive creation, pass the
exact archive hash and source commit associated with that build:

```sh
python3 -B tools/release/accept_package_cli.py /delivery/RED-package.tar.gz \
  --archive-sha256 "$ARCHIVE_SHA256" --version 0.6.0 \
  --platform linux-x86_64 --commit "$SOURCE_COMMIT" \
  --receipt /evidence/package-cli.json --scratch-root /external-scratch
```

`--platform` accepts `windows-x86_64`, `linux-x86_64`, or `macos-arm64`; it must
match the current operating system and machine architecture. The supplied hash
and commit must be full lowercase SHA-256 and Git SHA-1 hex strings respectively.
The version is an exact numeric `major.minor.patch`. These values come from the
release build's recorded identity, not from an untrusted archive's own claims.
This tool executes the accepted package binaries with the caller's privileges;
it is not a sandbox for arbitrary downloaded software.

The source archive is copied and hashed into a private snapshot before parsing.
The snapshot must match the expected SHA-256. The complete manifest's version,
platform, file sizes, hashes, and relative symlink targets are checked before
execution and again afterward. All regular files and symlinks must be declared,
and the manifest itself is excluded from its own inventory. Empty directories,
timestamps, and non-executable permission bits are not inventory claims.

The Unix manifest must contain the expected `sourceCommit`. The existing Windows
format may omit it; such a receipt records `manifestSourceCommitVerified: false`.
Its source attribution therefore relies on the caller's archive hash/build
record. A Windows manifest that supplies `sourceCommit` must match exactly.
The Unix platform cannot use the older Windows format to bypass that check.
The original archive is hashed again after execution, and both archive and
manifest bytes must remain unchanged.

## Extraction and limits

Only ordinary ZIP and gzip-compressed TAR archives with a root manifest, or one
named top-level package directory containing the manifest, are admitted. ZIP64,
split ZIP archives, sparse files, hardlinks, and special devices are refused.
Paths must be portable relative names. Absolute paths, traversal, backslashes,
drive/alternate-stream separators, Windows device names, trailing dot/space
aliases, duplicate entries, case/Unicode-normalization aliases, and conflicting
file/directory prefixes are rejected before payload extraction. Symlinks are
created last; both their lexical targets and resolved chains must stay inside
the package. Chain containment uses the canonical package root, including when
the extraction parent contains an operating-system directory alias. Strict
resolution continues to refuse broken links, cycles, and resolved escapes.
No archive extraction API writes arbitrary paths on the tool's
behalf. Regular files use exclusive creation, and privileged permission bits are
not restored.

Budgets are 20,000 archive entries, 512 MiB per file, 4 GiB per archive snapshot
and expanded TAR/payload, 8 MiB per manifest/worker receipt, and 16 MiB of archive
metadata. Individual TAR extension records are limited to 64 KiB. ZIP directory
metadata is bounded before `ZipFile` parses it. TAR is decompressed into a
bounded private file, then its raw headers and extension sizes are checked before
the general TAR parser sees them. Normal executable permission bits are retained.
The snapshot, uncompressed TAR, and extracted files coexist, so scratch storage
can exceed a single 4 GiB budget.

`--timeout-seconds` defaults to 120 and accepts 1 through 600 seconds. The
supervisor's deadline begins before loading/starting the owned worker; it covers
archive snapshotting, hashing, parser work, extraction, signatures, commands,
and final verification. Each command additionally has a 20-second maximum within
the remaining overall budget. Termination/owned-tree cleanup has a bounded
two-second wait, with short bounded pipe-reader joins. Initial argument checking,
scratch/request creation, and final receipt I/O happen outside this worker
deadline. Filesystem/system-call availability is still an operating-system
constraint; this is not a guarantee against an uninterruptible OS call.

The worker and descendants use a Windows kill-on-close Job or a dedicated POSIX
process group. The worker waits for an admission token before starting work.
Timeout or failure closes only that owned tree, preserving unrelated processes.
Command stdout and stderr share a 2 MiB limit; the receipt retains counts, hashes,
and a 4 KiB preview per stream. Excess output is a failure, not a truncated pass.

## Executed checks

The native editor executable, both companion commands, capture runner and README,
runtime template/list assets, and root license/notice files must exist. On macOS,
`/usr/bin/codesign --verify --strict` must succeed for the extracted app bundle
(also `--deep`) and each companion before execution. Ad-hoc signature validity
does not establish notarization or Developer ID signing.

Commands use absolute executable paths, argument vectors, a fresh working
directory, system-only `PATH`, and no inherited Qt/QML/library-loader/Python SDK
overrides. They exercise:

- `red-agent --help` identity and command schema;
- creation of a fresh synthetic MotS `.sed`, then exact-hash inspection and
  validation of its single sector;
- an explicit invalid-command refusal with the documented exit status;
- MCP initialize, initialized notification, ping, tool catalog, and malformed
  JSON refusal, checking response IDs and protocol identity;
- packaged capture-runner help and metadata-only refusal of an invalid request.

No proprietary data is needed. The fixture, requests, and receipts are outside
the package. Bytecode writing is disabled, and final inventory verification
detects added or modified package files. The input `.sed` hash is checked after
read-only operations.

## Receipts, retention, and CI integration

`--receipt` must be a new file in an existing directory. Exit 0 means every
package CLI check passed; exit 1 means acceptance failed with a JSON receipt;
exit 2 covers argument/receipt-destination failures. An existing receipt is never
overwritten. A terminal JSON summary is printed to stdout when the receipt was
written. Failures retain the last recorded phase and command context when the
worker could write them.

Each invocation creates a unique `red-package-cli-*` scratch directory beneath
`--scratch-root`, or the OS temporary directory when omitted. The scratch
directory is retained on success and failure, including the private archive,
extraction, fresh fixture, and worker state. Retention keeps failure evidence and
avoids an unbounded recursive deletion after the timeout. CI may discard its
ephemeral workspace after preserving the final receipt. Local cleanup is an
explicit operation against the receipt's exact owned directory.

Run this gate after archive creation and before publishing/uploading that
candidate, retaining the receipt even on failure. macOS archives must preserve
framework symlinks without adding undeclared AppleDouble/resource-fork payloads.
No unexpected `__MACOSX` directory is exempted from manifest coverage. Package
creation and this acceptance run must use the same exact source/hash record.

Run the synthetic helper tests without a game installation:

```sh
python3 -B -m unittest discover -s tools/release/tests -v
```

The Unix symlink-extraction cases (single file, framework directory chains, and
aliased extraction parents with valid and refused chains) require symlink
creation privileges. They are skipped only for Windows error 1314;
Linux/macOS execute them normally.
Negative symlink/path admission, actual process deadlines, owned descendant
cleanup, unrelated-peer preservation, output limits, manifest mutations, failure
receipts, and protocol semantics remain exercised on Windows. Synthetic protocol
fixtures validate this helper; they do not stand in for running the actual
packaged native binaries.
