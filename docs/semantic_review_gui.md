# Semantic change review for humans

`SemanticReviewDialog` is a read-only Qt surface over the portable
[`level-model-v1` comparison](semantic_preview_design.md). It reviews explicit
saved files or an existing generic `plan`/`apply` receipt. It has no reference to
the live document, no apply callback, and no operation that saves a level.

The standalone dialog and its tests are registered with the application build.
MainWindow menu placement belongs to the host integration; this component does
not add a menu action or shortcut itself.

## Compare saved snapshots

Choose **Compare saved native snapshots (.sed)** and supply three files using
the ordinary path fields and **Browse…** buttons:

| Input | Comparison role |
| --- | --- |
| Baseline | The loaded source model used by both views. |
| Candidate | The loaded saved candidate shown in the **Authored** tab. |
| Output | The loaded saved output shown in the **Persisted** tab. |

Press **Compare saved snapshots** to capture the selected files and run the
comparison. Each identity is the SHA-256 of the exact bytes captured for that
file. The captures occur separately. A file can change on disk after it has
been read; the displayed identity continues to describe the captured bytes.

This mode explicitly compares **loaded saved snapshots**. The candidate is
already a saved file. It does not establish the in-memory candidate of a patch,
infer that a serializer produced the output, or assign differences to writer
effects. For the original in-memory authored versus reopened-output distinction,
inspect the corresponding agent receipt.

Native loading uses `loadSedFileBytes`, followed by
`applySedLightRgbSection` and `applySedArchAuthoringSection`. Optional RGB
absence remains distinct from explicit zero, and IEEE bits survive the shared
comparison. Native corruption or repair is reported alongside the original
captured-file identity. The compared model is the model produced by that load.
The viewer does not rewrite repaired input bytes.

Direct snapshot loading admits **native `.sed` only**. JKL, NDY, JED, NED,
archives, and other formats receive an explicit unsupported-format message.
The current text loaders do not expose the native decoder's equivalent
preallocation bounds; extending those parsers is a separate task. Existing
successful plan/apply receipts from text-level workflows remain inspectable.

## Inspect an existing receipt

Choose **Inspect an existing plan/apply receipt (.json)**, select the saved JSON
file, and press **Inspect recorded receipt**.

The input must be one complete successful generic `plan` or `apply` command
receipt containing `semanticPreview`. A saved JSON-RPC envelope must first be
represented by its complete command receipt from `result.content[0].text`.
The viewer does not unwrap arbitrary envelopes, read paths named by the
receipt, replay a patch, or open the claimed input/output files.

The **Authored** and **Persisted** tabs show the comparisons recorded in the
receipt. Their scope labels explicitly say **not recomputed**. The identity
pane distinguishes the hash calculated for the captured receipt file from
the input and output hashes asserted by its contents.

Admission checks the supported version, coverage, exclusions, addressing,
view labels, bounds, descriptor shapes, exact float bits/classification,
optional absence, string encodings, counts, and completeness invariants.
These checks establish that the recorded result is structurally supported.
They do not authenticate a receipt or verify the claimed computation.
Malformed, inconsistent, unsupported, or over-budget input is refused without
publishing a review.

## Read changes and incompleteness

The two comparison tabs retain the core's deterministic model-field and
array-slot order. Each row shows its path, change kind, and before/after stored
types and values. Float rows include their exact hexadecimal IEEE bits and
classification. Absent values are explicitly labelled `absent`; omitted string
values retain their byte count and an omission label. A selected row's detail
pane and tooltips retain its complete admitted descriptors.

Paths address **snapshot storage slots**. They do not identify an entity across
snapshots or infer moves. Counts refer to **change records**, including array
length and optional-presence records, rather than logical entity counts.

The summary always shows comparison completeness, detail completeness,
observed changes, emitted records, omitted values, reason codes, and work
counters:

| Result | Visible meaning |
| --- | --- |
| Comparison complete | The total change-record count is exact, even if detail rows were omitted. |
| Comparison incomplete | Observed changes are a lower bound; the exact total is unknown. |
| Incomplete with zero observations | Whether covered fields changed is unknown. |
| `changeLimit` | More changes were counted than could be emitted as detail rows. |
| `inlineStringLength` / `inlineStringBytes` | Some values were omitted explicitly because of inline budgets. |
| `comparisonSlots` / `comparisonStringBytes` | The corresponding view stopped comparing at its work limit. |

The path filter is a case-sensitive substring search. The stored-type filter
matches either the before or after descriptor, including `absent`. Both filters
apply to **emitted rows only** and reset the page. Pages contain at most 32 rows;
Previous and Next preserve their recorded order. Filtering never changes the
global exact total/lower bound or recovers omitted or unscanned changes.

The **Scope and hash identity** tab lists file identities, integrity warnings,
the effective limits, every excluded field and excluded scope, and the model
coverage boundary. A complete zero-change result establishes equality only
for the fields covered by this comparison. It is not a preservation,
validation, or gameplay certificate.

## Export a new report

Choose a new destination in the report path field or with **Browse…**, then
press **Export new report**. Export uses exclusive file creation. An existing
file, directory, or symbolic link is refused; it is never replaced.

- For an imported receipt, export writes the **exact captured original bytes**,
  preserving whitespace, numeric spelling, field order, and the receipt hash.
- For a saved-file comparison, export creates a JSON report with
  `reportVersion: 1` and `reportKind: "saved-snapshot-comparison"`. Its separate
  provenance object identifies the three captured files and any native load
  warnings. Its comparison descriptions explicitly refer to loaded saved
  snapshots. It does not claim to be an agent plan/apply receipt.

The snapshot-report wrapper is an export format for independent review; the
receipt input mode currently accepts generic plan/apply receipts only.

An interrupted or failed new write attempts to remove its incomplete output
and reports failure if that cleanup cannot complete. Cancellation racing a
completed file close reports the actual successful creation. Export does not
modify an input or the live document.

## Admission and worker ownership

| Boundary | Limit or policy |
| --- | --- |
| Snapshot bytes | 64 MiB per native `.sed` file, captured with bounded reads. |
| Native decoding | Existing codec limits: 256 MiB expanded payload, 128 MiB logical decoded objects, and 2,000,000 decoded items per model. |
| Native ARCH extension | Declared entry count and conservative extra decoded footprint are admitted before the unchanged extension loader can reserve storage. |
| Retained models | Baseline plus one after-model; candidate storage is released before output loading. |
| Receipt bytes | 4 MiB, before JSON tree construction. |
| Receipt grammar | At most 32 nested levels and 65,536 admitted values/keys; object keys at most 1,024 raw bytes. Duplicate decoded keys and malformed Unicode are refused. |
| Selected paths | At most 32,768 characters; embedded NUL is refused. |
| Comparison | Existing independent defaults per view: 1,000,000 visited slots, 4 MiB compared strings, 128 rows, 512 bytes per inline string, 32 KiB aggregate inline strings. |
| Display pages | At most 32 emitted rows. |

There is one active job per dialog. Requests own their selected path strings;
workers capture each file once, hash those bytes, and decode immutable inputs.
Workers never access widgets, a resource locator, or a mutable document.
Export workers own a shared immutable settled review and a copied destination.
No subprocess, shell command, resource lookup, or new runtime dependency is
used by this component.

Changing review inputs requests cancellation, advances the generation, clears
the previous result, and disables export. Late results from the old generation
are discarded. Starting another job is disabled until the current one returns,
so repeated clicks cannot queue model copies. Closing or destroying the dialog
requests cancellation; workers do not capture the dialog's lifetime.

Cancellation is checked between capture chunks, during receipt admission and
conversion, at core comparison admission, and during export. Native codec and
extension calls remain internally synchronous and bounded; cancellation is
checked before and after them. A blocked operating-system file operation also
has to return before cooperative cancellation can take effect. This is not a
hard process timeout or a fixed total-process memory guarantee.

Worker launch, worker execution, result retrieval, and presentation have
exception guards. Allocation/programming failures are reported as errors and
do not become misleading comparison-budget truncation.

## Shared implementation and host integration

The portable API is declared in
[`semantic_preview.hpp`](../src/core/include/sed/level/semantic_preview.hpp):

```cpp
sed::semantic_preview::Comparison result =
    sed::semantic_preview::compareLevels(before, after, {}, stopToken);
```

The result contains typed descriptors, exact counters, optional changed/total
values, ordered changes/reasons, and raw float bits. The existing agent adapter
retains its public `buildSemanticPreview` and `SemanticPreviewLimits` interface
and serializes the shared result through the original JSON representation.
There is one Level walker. Core has no Qt dependency.

The host may open the dialog with an ordinary Qt lifetime:

```cpp
#include "ui/semantic/semantic_review_dialog.hpp"

auto* review = new sed::ui::semantic_review::SemanticReviewDialog(this);
review->setAttribute(Qt::WA_DeleteOnClose);
review->show();
```

The default constructor uses the bounded service. The additional `Loader`
constructor separates job orchestration from file-review acquisition for
isolated hosts and deterministic lifecycle tests. A supplied loader must obey
the same immutable-result and bounded-admission contract.

No MainWindow declaration, menu action, shortcut, settings key, undo entry,
selection transition, recovery record, or dirty flag is added by this patch.
The integrating host owns menu placement and native-platform acceptance.

## Focused verification

Build the dedicated targets against the selected compatible Qt kit:

```sh
cmake --build build --config Release --target \
  sed_semantic_preview_core_tests \
  sed_app_semantic_review_io_tests \
  sed_app_semantic_review_dialog_tests
QT_QPA_PLATFORM=offscreen ctest --test-dir build -C Release \
  -R '^(sed_semantic_preview_core|sed_app_semantic_review_io|sed_app_semantic_review_dialog)$' \
  --output-on-failure
```

These tests use synthetic or project-owned fixtures and ordinary Qt widget
actions. They need no game data, OpenGL context, Xvfb, or foreground desktop.
Core/agent model fixtures and exact legacy response captures protect the
unchanged agent contract; the service tests cover malformed and oversized
inputs, native metadata, partial-result admission, exact original receipt
export and create-only conflicts. Dialog tests cover presentation, filters,
paging, late generations, cancellation, exceptions and pending-worker lifetime.

Offscreen widget checks do not qualify native file choosers, platform window
behavior, host menu wiring, or the complete editor Release gate. Follow the
repository's [build and release instructions](README.md#build-and-test) for
those separate checks.
