# Check campaign state contracts

Use `red-agent campaign-check <request.json|->` or MCP `sed_campaign_check` to compare declared
exit guarantees with the entry requirements of each successor in an existing `episode.jk`.
The checker visits both branches of each `LEVEL`, `CUT` and `DECIDE` sequence, including cycles.
A terminal `-1` branch has no successor to check. Use the episode's line identifiers for `line`.

## Declare a handoff

For an episode where line 10 leads to line 20 through branch A, the following request asks whether
the producer supplies the relay required by the second level. Both levels declare the shared script
as a dependency for comparing revisions.

```json
{
  "schemaVersion": 1,
  "episode": {"path": "episode.jk"},
  "inputs": [
    {"id": "arrival", "path": "jkl/arrival.jkl"},
    {"id": "vault", "path": "jkl/vault.jkl"},
    {"id": "relay", "path": "cog/relay.cog"}
  ],
  "sequences": [
    {"line": 10, "input": "arrival", "dependsOn": ["relay"],
     "entry": {}, "exits": {"a": {"relayOnline": true}}},
    {"line": 20, "input": "vault", "dependsOn": ["relay"],
     "entry": {"relayOnline": true}, "exits": {}}
  ]
}
```

Give MCP the same object inside its single `request` argument. CLI relative paths resolve from
the working directory. MCP resolves paths from its workspace and admits reads in the workspace
or its configured resource roots. The portable project profile can supply the filenames when
preparing this separate request. The checker reads its declared inputs without modifying the project.

Facts are booleans, integers from −9007199254740991 through 9007199254740991, or UTF-8 strings up to
128 bytes. Keys are case-sensitive and limited to 64 bytes, excluding ASCII whitespace and controls.
Integer checks retain the exact decimal token's integrality before binary floating-point rounding,
including through MCP request staging. Integral decimal or exponent spellings such as `12.0` and
`120e-1` are accepted; a fraction that merely rounds to an integer is refused.
An empty entry declares no requirements. An omitted exit leaves the branch's guarantees unknown;
an explicit empty exit supplies no facts. Each edge compares only the stated exit with the next
entry. A cutscene needs its own exit declaration to establish a fact for its successor.

| Receipt field | Interpretation |
| --- | --- |
| `transitions[].status` | `matches` for a complete matching handoff, `mismatch` for a known contradiction, or `incomplete` for missing evidence. |
| `declaredStateCompatible` | `false` if any compared handoff contradicts a requirement, `null` if declarations are incomplete without a known contradiction, otherwise `true`. |
| `inputsComplete` | Whether all declared files were captured and every sequence has its required primary input binding and available dependencies. |
| `sequences[].inputStatus` | A bound level or cutscene must match the episode's logical filename. A `DECIDE` row needs no primary input. |
| `sequences[].freshness` | `uncompared` on an initial audit, `current` or `stale` against a baseline, `untracked` for a new line, or `unavailable` when the current revision cannot be computed. |

The receipt always reports the full-campaign verdict and sequence revisions. Page the edge details
with `offset` and `limit` (default 16, maximum 32), following `nextOffset`. Each edge reports its
full fact counts and at most eight detailed differences, with a truncation flag.

## Compare revisions

Save the command's JSON receipt, calculate its SHA-256 and add
`"baseline":{"path":"prior-audit.json","sha256":"<64 hex digits>"}` to the next request. The checker
verifies the baseline bytes and the internal consistency of its sequence revision evidence.
When the episode digest matches, the baseline must contain one row for every episode line,
with the type and filename the checker emitted for that line, including on an empty transition page.
Available source evidence must agree with revision presence. Genuine incomplete and unmodeled
rows retain null revisions. These checks do not authenticate who produced or edited the receipt.
The optional `sha256` field on the episode and each input pins those files too. A changed pinned
file produces a conflict. To audit an intended edit, update its pin or omit the pin.

The logical input name defaults to the source basename. Set `resource` to declare another name.
Names compare after ASCII case folding and slash normalization. A sequence revision binds the
episode bytes, the row's declarations, successor entry assumptions, and each assigned input's
identity, logical name and content digest. Editing a shared file or changing its logical resource
name marks each dependent row stale. Moving identical bytes while retaining their logical name
and ID preserves the binding. An episode byte change affects every sequence revision. Inspect
`changedInputs`, `rulesChanged` and `episodeChanged` for the recorded causes.

The command admits up to 128 episode sequences, 256 declared inputs, 64 facts per state,
4096 facts in total and 1024 sequence/input associations. A row can name a primary input and up
to 32 additional dependency IDs. Declared input files are capped at 64 MiB each and 256 MiB
combined. The request, episode file, baseline and receipt each have a 1 MiB limit.
Episode resource filenames must be representable as UTF-8 for the JSON receipt. Legacy title and
comment bytes are retained in the episode hash without being emitted as JSON text.

Resource receipts identify captured bytes. The checker does not decode level content, infer script
dependencies, execute triggers, select the game's branch, verify initial state, or establish
persistence in a running engine. A missing file can leave declared rules compatible while its
sequence's source evidence is unavailable. For the complete machine-readable request shape,
use `red-agent schema campaign-check`.
