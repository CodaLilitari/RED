# RED clipboard failures: classification and repair (Red, 2026-11-09)

This continues item 1 of [Coda's handoff](2026-11-09-red-handoff.md): classify the remaining failures in
`sed_app_entity_clipboard_routing`, `sed_app_paste_in_place` and `sed_app_mots_arch_lighting`.

## The failing assertions all read the system clipboard

On the Windows coordinator machine, these assertions failed:

| Test | Failing assertions | Passing neighbours in the same run |
| --- | --- | --- |
| `entity_clipboard_routing` | Light/Thing Ctrl+C publishes one Thing payload; Light/Thing Ctrl+V pastes it | Sector Copy, both Sector pastes, Undo/Redo, cross-document paste, Baked-light Copy and Paste |
| `paste_in_place` | Thing Paste in Place keeps the exact pose; it records one Undo step | Sector Paste in Place; "Thing copy publishes the entity clipboard" |
| `mots_arch_lighting` | ARCHLIGHTING Paste promotes the compatible Thing; its Undo restores flags and binding | ARCHLIGHTING Copy |

Coda's diagnostic in the routing test showed:
- the codec control, the mode, the selection and the Copy binding were all correct, and Copy was consumed;
- immediately after Copy, the clipboard listed no formats;
- after up to 500 ms of event processing, it listed `application/x-sed-entities-v1` and `text/plain`, but both read
  back empty: zero payload bytes and empty text.

## Evidence

- **Linux (this review).** Built from `37ead3d` with Qt 6.4.2. All three tests' clipboard assertions pass, both under
  `xvfb` (X11) and with `QT_QPA_PLATFORM=offscreen`. Only four or five unrelated ARCHLIGHTING picking and wireframe
  assertions fail, and only under software GL.
- **Qt 6.8.3, `qwindowsclipboard.cpp`.** `setMimeData` deletes the previous entry's data first. It then calls
  `OleSetClipboard`, retrying only on `CLIPBRD_E_CANT_OPEN`: three attempts with `QThread::msleep(100)`, which renders
  nothing while it waits. If every attempt fails, Qt drops the new data and only logs a warning. The old OLE object
  stays registered on the clipboard: its formats are still listed, but there is no data behind them.
- **The suite hides that warning.** The main-window suite's prior message handler drops every message except its
  sentinel, so the `OleSetClipboard` warning never reaches a log.
- **The order of operations in each failing test is the same.** A first publication succeeds. The test then does
  synchronous work that processes no events, and the next Copy fails.
  - On Windows, a program that reads clipboard updates (clipboard history, a remote-desktop or VNC clipboard bridge,
    a clipboard manager) opens the clipboard and asks for delayed rendering of the first entry. That request is a
    message sent to this thread, and the reader keeps the clipboard open until the thread answers it.
  - While the test thread does not process events, it does not answer. The next `OleSetClipboard` then gets
    `CLIPBRD_E_CANT_OPEN`, and Qt's sleeping retry cannot clear it.
  - This matches every observation, including two easy-to-miss ones. The format-only wait in Paste in Place passes on
    the stale registration and the paste then fails. The first publication in a test always succeeds.

**Classification: the most consistent explanation is clipboard contention on Windows, not a codec or routing
defect.** It is made deterministic in the tests by a second publication made while a clipboard reader is still waiting
for the first entry to be rendered. It is a product defect as well: Copy reported "Copied" when nothing was published.

The passive observer `clipspy.ps1` (in Red's companion kit) logs the clipboard sequence number, owner and open-window
process during a run. That Windows run is still pending, and it should name the reading process.

## Repair (`red/clipboard-publication`)

- **`ui::publishClipboard`** (`src/app/src/ui/components/clipboard_publication.*`):
  - before writing, it answers render requests while another program's window holds the clipboard, within a budget;
  - it writes, then verifies every private format by reading it back;
  - after a failed attempt it waits again (still answering requests) and retries, three attempts by default;
  - it reports `changedElsewhere` instead of overwriting an entry another program published in between (Windows
    sequence number);
  - on Windows, the wait uses `PeekMessage(PM_QS_SENDMESSAGE)` and `MsgWaitForMultipleObjectsEx(QS_SENDMESSAGE)`. These
    process only messages other threads send; posted messages and user input stay queued.
- **Entity Copy and ARCHLIGHTING Copy** publish through it.
  - On failure they show **Copy failed** with the reason, leave Paste state unchanged, and do not record the editor
    clipboard copy.
  - ARCHLIGHTING Copy returns false.
- **Tests (red first, then green):**
  - `sed_app_clipboard_publication` covers the publication contract with a scripted clipboard: first-write success,
    answering a holder, retry after a lost write, the bounded budget and bounded holder wait, never overwriting newer
    content, a missing clipboard, and the failure messages. It also covers entity Copy's failure report and its retry,
    through `setClipboardPublicationBackendOverrideForTest`.
  - The ARCHLIGHTING dialog tests gained "ARCHLIGHTING Copy reports a clipboard it could not write and changes
    nothing".
  - Paste in Place now waits for a decodable payload of the expected kind, not just an advertised format.

## Validation run

Linux, Qt 6.4.2, GCC, Release (the 59k-line native test TU at `-O1` to fit the 6 GB sandbox), `xvfb` and offscreen:

| Selector | Before the repair | After |
| --- | --- | --- |
| `--clipboard-publication` | 8 failures (red, as designed) | pass |
| `--entity-clipboard-routing` | pass | pass |
| `--paste-in-place` | pass | pass |
| `--mots-arch-lighting` (xvfb) | new ARCH Copy check fails (red) | it passes; only the pre-existing software-GL picking and wireframe failures remain |

Not yet run:
- the Windows native lane for these selectors with the repair (Red's kit builds the branch on Windows and runs them);
- the complete Release gate.

The DPR3 and idle-desktop conditions in the handoff still apply.
