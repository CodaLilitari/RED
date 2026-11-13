# Archive and Package Authoring

**Tools > Archive and Package Authoring…** creates a new GOB or GOO from explicit
files. Its two tabs expose the same portable authoring operations as
`red-agent archive-create` and `package-plan` / `package-apply`. The dialog does
not save or modify the open level, change its filename, mount its output, or
launch a game. Use [Save JKL and Test](game_testing.md) for an editor export and
private test installation.

## Resource Archive

1. Add one row per source file. Choose the file for the selected row and enter its
   logical archive destination, such as `mat/custom.mat` or `3do/prop.3do`.
2. Arrange rows with **Up** and **Down**. Physical entry order follows this list.
3. Select GOB or GOO, then **Prepare**.
4. Inspect **Contents**, **Issues**, and **Full report**. The review includes the
   canonical physical destinations, byte counts, payload hashes, parser readback
   results, and layout warnings.
5. Choose a new output filename matching the reviewed format and **Create new
   archive**. An existing destination is refused without replacement.

Runtime destinations use the existing engine-facing authoring rules: lowercase
ASCII names and backslash separators, at most 127 bytes, no absolute paths or
traversal. Case-colliding destinations are refused. This normalization applies
only to authoring destinations; raw GOB reader/writer fidelity, resource references
inside files, and captured payload bytes are unchanged.

Known MAT, 3DO, COG-symbol, and TPL payloads must pass their existing readers.
Other file types remain byte-preserved and are reported as having no typed
readback. This is structural admission, not proof that an asset works in a game.
The inherited CLI extension classification uses the platform's lexical
`std::filesystem::path` behavior; unusual nested dotfile names are not a portable
typed-file convention. Use explicit ordinary extensions for portable readback.

There may be 1–4096 entries, at most 256 MiB per source, and at most 512 MiB total
including the GOB header and index. A failed entry poisons the captured builder;
no partial archive can subsequently be finished. A completed archive is reopened
with the GOB reader before it becomes a publishable review.

## Exported Level

1. Choose an explicit exported `.jkl` file. Native `.red`, `.sed`, and `.jed`
   documents must be exported first.
2. Set the level filename inside the package, episode title and type, and GOB/GOO
   format. These are ordinary controls; no request JSON is required.
3. Add the GOB/GOO archives used to resolve references. Mark stock archives in the
   **Stock** column. Resource archives are searched before stock archives, with
   list order retained inside each group. **Full report** records that effective
   order and the source files' raw hashes.
4. Optionally add extra referenced asset names and resource folders. These are
   roots for dependency resolution, such as a COG and its `cog` folder; they are
   not loose filesystem search directories.
5. Set node, edge, depth and byte budgets and the explicit option to allow
   unverified references. Prepare and inspect the complete plan before creating
   the new archive.

The dialog calls `planLevelPackage` with captured bytes and its existing options.
The [package node budget contract](package_node_budget_design.md) counts the
episode, level, and distinct resolved resources, including stock omissions. A
zero depth limit means no extra depth restriction. The byte limit is an actual
maximum output size, so zero does not produce a publishable package. Stock
resources can satisfy references while their bytes are omitted from the output.
The report distinguishes included entries from stock omissions, shows each
source category and hash, and lists every issue and unverified dependency scope.

The displayed output SHA-256 binds the exact prepared archive bytes. The manifest
SHA-256 identifies the planner's normalized inputs and decisions. Archive hashes
inside that manifest describe normalized entry data; they are different from the
raw archive file hashes listed separately in the review. Historical CLI package
materialization resolves each physical archive row by its name, including aliases;
the GUI preserves that rule and charges the selected payload before allocating it.

Capture is limited to 128 MiB of JKL, 16 archives totaling 512 MiB of raw archive
bytes, 100,000 physical archive entries, 512 MiB of expanded payloads, and 1024
extra roots. The index is admitted before parsing or materialization. Planner
controls allow up to 100,000 nodes, 1,000,000 edges, 100,000 depth, and 512 MiB
of package bytes. These are dialog admission bounds, not new file-format limits.

## Review, cancellation, and publication

**Export full report** writes the complete captured review as UTF-8 plain text to
a new filename, including a blocked package plan. The tables and report are not
truncated. A blocked plan cannot create an archive. A package report always says
that runtime closure is unverified; successful packaging does not prove retail,
OpenJKDF2, palette, COG, multiplayer, or platform behavior.

Input edits invalidate the review. Preparation and staging run on immutable
worker snapshots, with cancellation checks during bounded file reads and archive
expansion. Core parsing/planning has no interrupt callback, so cancellation there
discards the eventual result; it does not claim to stop work instantly. Closing
the dialog cancels publication without waiting for the worker on the GUI thread.

Before publication, sources are recaptured and compared by canonical path, size,
and SHA-256; the output bytes must retain their reviewed identity. The adapter
checks the owning document/resource context at command entry and worker completion.
A changed context, changed source, cancellation, or owner-check exception prevents
publication and asks for a fresh review. These checks detect drift; they are not
a filesystem lock against an external writer after the final check.

The adapter uses the existing staged create-only file protocol. It checks a new
destination, writes and closes a sibling temporary file, rechecks sources, then
publishes only while the owner and generation remain current. Existing files and
symbolic-link destinations are refused. Filesystems that cannot provide the
required create-only publication primitive fail with the operation's error;
choose a local supported destination. If a post-publication host notification
fails, the status still reports the file as created and includes that error.

The portable helper in `sed/assets/runtime_archive.hpp` owns deterministic archive
admission, canonical destinations, typed readback, hashes and output bytes. The Qt
adapter owns file capture, workers, review widgets and publication. The agent
retains its request/hash checks and exact error categories while sharing the
same captured-entry helper. Neither adapter adds Qt or filesystem discovery to
the portable authoring operation.
