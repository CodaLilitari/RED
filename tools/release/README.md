# Unix release package notices and inventory

`stage_notices.py` is the Linux/macOS release-workflow companion to the explicit
Windows `sed_release_package` target. It uses Python 3.11 or newer and the standard
library. It does not download dependencies or infer redistribution permissions.

The workflow installs one exact Qt SDK version, its module documentation, and the
matching `qt5` source archive for the supplied license texts. The `stage` command
requires the selected kit's `qtbase`, `qtmultimedia`, and `qtsvg` SPDX documents;
nonempty attribution HTML from `qtcore`, `qtgui`, `qtnetwork`, `qtmultimedia`, and
`qtsvg`; and the source archive's GPL 2, GPL 3, and LGPL 3 license texts. It copies
the complete source `LICENSES/*.txt` set and the selected attributions/SBOMs without
rewriting them. A missing or empty input or malformed SPDX document rejects the
operation before creating the notices directory. An existing notice destination
is refused rather than merged with stale files.

```sh
python3 tools/release/stage_notices.py stage --package RED-package \
  --qt-kit "$QT_ROOT_DIR" --qt-version 6.8.3
python3 tools/release/stage_notices.py manifest --package RED-package \
  --version 0.6.0 --qt-version 6.8.3 --platform linux-x86_64 --commit "$GITHUB_SHA"
```

Run `manifest` after deployment, signing, and adding the package README. It
revalidates notices and records the exact source commit, Qt version, platform,
relative paths, regular-file sizes and SHA-256 hashes. Framework symlinks are
recorded with their relative targets; broken links, absolute targets, and targets
outside the package are refused. The manifest excludes itself and is replaced
atomically after inventory succeeds. The notice set is an evidence-bearing
redistribution input, not an assertion that every distribution obligation has
been discharged. Review the actual deployed libraries, their source-delivery
requirements, and [third-party notices](../../THIRD_PARTY_NOTICES.md) separately.

The release workflow builds every configured target and runs the complete CTest
suite before staging or uploading a candidate. Linux uses a private Xvfb desktop
with Openbox and Mesa software rendering; this does not establish physical-GPU
acceptance. macOS uses the native Cocoa desktop, retaining test-specific offscreen
settings where explicitly registered by CMake. A missing native desktop or a
failed test blocks the package; no subset substitutes for the complete gate.
Diagnostic artifacts are retained separately even after failure. macOS bundle
and companion CLI signatures must verify; ad-hoc signing is not notarization.

Run the helper's synthetic fixtures on Linux or macOS:

```sh
python3 -B -m unittest discover -s tools/release/tests -v
```

The symlink cases require permission to create symlinks. These Unix package tests
are run directly by both release jobs; Windows packaging retains its CMake test
suite. Successful staging and CI tests do not replace launching the extracted
package on each supported platform or the complete Windows Release gate.
