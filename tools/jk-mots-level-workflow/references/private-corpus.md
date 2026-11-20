# Optional private corpus

`update-lead` replaces one explicit finding by stable ID while retaining the prior entry in the private ledger history. Supply `id`, `capability`, `disposition` and `next_acceptance_case`, plus exact source/evidence fields relevant to the finding. It does not promote evidence stages or approve sharing automatically.

The distributable research pack works alone. An authorized local research corpus adds exact source recovery; it is never included in the pack. `scripts/corpus.py` uses Python's standard-library SQLite FTS5, with explicit `--home <private-directory>`. That directory contains `manifest.json`, `corpus.sqlite`, `evidence-events.json` and `leads.json`.

```text
python scripts/corpus.py --home PRIVATE_HOME build
python scripts/corpus.py --home PRIVATE_HOME search "vertex lighting" --limit 10
python scripts/corpus.py --home PRIVATE_HOME show MESSAGE_ID --context 3 --raw
python scripts/corpus.py --home PRIVATE_HOME leads --id qt-sed-rewrite
python scripts/corpus.py --home PRIVATE_HOME update-lead ORIGINAL_FINDING.json
```

The manifest allowlists exact paths, SHA256 values, formats and coverage notes. Existing downloads stay in place; no new network acquisition occurs. Supported input adapters are message JSONL, message-array JSON, text and HTML documents. HTML is indexed as page text, not falsely parsed into individual forum posts. UI-only extracts with ambiguous main text should be registered as documents with that qualification.

`build` refuses changed files and replaces the derived index only after a successful build. To knowingly refresh one existing input use `refresh-source EXACT_PATH`, then rebuild. Search is literal word matching with bounded output and optional exact author/channel filters. It returns exact record keys; duplicate IDs with different captured bodies remain separate versions. `show --raw` re-hashes an original source before returning its exact JSON record. Context is **nearest acquired messages in the same channel/group**, not guaranteed uninterrupted conversation. Inspect gap/coverage notes and original occurrences. Reported UI and ID-derived timestamps remain separate.

Acquisition is established by indexed bytes. Other states require explicit content-bound evidence events:

```text
python scripts/corpus.py --home PRIVATE_HOME evidence EXACT_RECORD_KEY reviewed --receipt REVIEW_RECEIPT --note "Main body read; attachments not inspected"
```

Valid stages are `reviewed`, `source_inspected`, `runtime_verified`, and `reused`. They do not imply one another. Each event retains the exact record key and receipt hash. Edited content does not inherit review merely because its message ID is unchanged. Bulk imported reviews must bind the exact reviewed capture hashes; keyword matches or source filenames are not sufficient. The ledger can retain announced/media/acquired source/runtime/reuse status separately for each finding, along with current counterpart, disposition and next acceptance case.

Raw logs, copied forum text, attachments and local paths stay private. `export-findings` only emits separately authored `shareable` fields on selected ledger entries with `originalAnalysis` and `reviewedForSharing` attestations. It never copies indexed bodies or private source references. It checks obvious private locators but cannot determine authorship or disclosure suitability for you: read the exact packet before sending under the user's authorization. Producing a packet is not uploading it.

Keep personal annotations and corpus state outside the distributable skill. An installed `local-config.json` is private and excluded by the release builder; copy it forward locally when intentionally replacing the full skill. Do not add this config, SQLite database or evidence receipts to a public release.
