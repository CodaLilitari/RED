# Updater (F-14, adapted)

The adapted updater re-derives Coda's portable-updater contract for the Qt
layout on top of the rewrite's existing release-package machinery
(`sed_release_package` in `src/app/CMakeLists.txt`, staged by
`cmake/stage_release_package.cmake`, which writes `RELEASE_MANIFEST.json`
with per-file `bytes` and `sha256` receipts). It never performs an
in-place replacement.

**The update feed is not independently signed.** Trust is HTTPS plus
control of the feed host only (the same non-claim Coda states for its
feed). Per-file SHA-256 receipts detect corruption and tampering relative
to the manifest, but a party controlling the feed controls both the
manifest and the packages. Do not present receipts as signatures.

## Layout

- Core logic: `src/core/include/sed/update/updater.hpp` and
  `src/core/src/update/updater.cpp` (`sed::update`, part of `sed_core`,
  Qt-free, covered by `sed_core_tests`).
- Tool: `tools/sed-update` (`sed_update.exe` on Windows, links `sed_core`
  only). Feed fetch is injectable through `sed::update::IUpdateSource`;
  the tool wires `FileUpdateSource`, and tests use local file feeds only
  (no network in tests).
- GUI: Help → "Check for Updates (sed-update)..." only states the
  contract and points here. The editor downloads nothing itself.
  (Compile-verified only in this sandbox; GUI runtime is unverified —
  see `docs/port/BASELINE.md`.)

## Feed and package contract

A feed is a bounded JSON document (≤ 256 KiB):

```json
{"format": 1, "releases": [
  {"version": "0.5.0", "channel": "stable", "package": "SED-0.5.0"}
]}
```

`package` is a safe relative path to an unpacked release-package
directory (as staged by `sed_release_package`) containing
`RELEASE_MANIFEST.json` whose `version` must match the release. Package
paths that are absolute or contain `..` are refused, duplicate or
unlisted files are refused, and each file must match its manifest size
and SHA-256 receipt. The highest release on the requested channel wins.

Responses encode dynamic strings as JSON strings, including native paths,
quotes, backslashes and control characters. Decode the complete JSON document
before consuming a returned staging/activation path or diagnostic.

Refusals (stable `code` in the tool's JSON output; exit 3 for version
refusals, 4 for conflicts, 2 for malformed input, 5 for I/O or receipt
failures):

- Equal versions and downgrades are **refused** (`equal-version`,
  `downgrade-refused`); an update is offered only for a strictly newer
  version (dotted numeric compare, prerelease suffixes order before the
  release).
- A corrupt file hash (`hash-mismatch`) aborts staging, removes the
  staging directory the updater created, and leaves the old install
  untouched and runnable.
- A staging directory left by an interrupted run
  (`interrupted-staging`) blocks a new stage; inspect or remove it
  manually. The updater never silently reuses or deletes it.

## Staging and activation (two explicit steps)

```powershell
.\sed_update.exe check    --feed <feed.json> --current 0.4.0
.\sed_update.exe stage    --feed <feed.json> --install <install-dir> --current 0.4.0
.\sed_update.exe verify   --dir <staging-dir> --staged
.\sed_update.exe activate --staging <staging-dir>
```

`stage` copies the verified package **beside** the running install into
`<install>.staging-<version>` (never inside or over the install tree),
rechecks every file receipt after copying, and writes `STAGED.json`
holding the same per-file receipts. `activate` is an explicit separate
step: it rechecks every receipt again, then renames the staging
directory to `<install>-<version>`. The old directory is never modified
and remains the manual rollback; run the new install from its own
directory. `verify` repeats the launch-time receipt recheck at any
later point. Any failure leaves the old editor running.

Tests: `src/core/tests/update/updater_tests.cpp` (`sed_core_tests`) and
`tools/sed-update/tests/update_cli_tests.cpp` (ctest scenarios
`sed_update_equal`, `sed_update_downgrade`, `sed_update_corrupt-hash`,
`sed_update_interrupted-staging`, `sed_update_stage-activate`,
`sed_update_json-output`, driving
the real binary against local feed fixtures).
