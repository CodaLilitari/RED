# Checking an installation

Use **Help > Check Installation… > Run Check** to check the current RED executable
and its packaged Qt deployment. The dialog offers Cancel Check, a readable result,
the inspected synthetic frame, and Save JSON Report. The check opens its own native
editor window with synthetic geometry. It does not use the open level or invoke a
game engine. The action is available to people and automation through the same
controller and frame-admission rules.

The command-line equivalent is:

```text
red-editor --check-installation --report-directory /absolute/new-report-directory
```

On Windows, use the executable's actual filename and an absolute Windows path.
The report directory must not exist, and its parent must already exist. The
operation never replaces an earlier report directory. Exit 0 means the complete
native check passed, exit 1 means an admitted check failed or was cancelled, and
exit 64 means the request was refused. Normal startup without the explicit
installation-check option keeps its existing behavior.

The GUI creates a uniquely named report directory under the system temporary
directory. Its exact path appears in the result. Save JSON Report creates a new
JSON file using an exclusive staged write; an existing destination or a write,
flush, or publication failure leaves existing files unchanged. Frame files remain
in the original report directory. Changing or closing the result dialog does not
replace the user's level, resource roots, selection, or Undo history.

## What a successful check establishes

The check requires the platform's native Qt backend: Windows, Cocoa on macOS, or
X11/Wayland on Linux. Qt `offscreen` and `minimal` are refused. The child loads
plug-ins only from the deployment's platform-specific plug-in directories; an
undeployed development build can therefore fail even when its ordinary launch
finds Qt through an SDK environment.

It constructs the actual MainWindow, docks, menus and GlView, then checks an
exposed native shell, a valid OpenGL context and a linked scene shader. The native
window is labelled as an installation check and disabled for editing while the
measurement runs. A white untextured face is rendered at two different positions
through the ordinary document and viewport path. Predicted interior regions must
change from bright to dark and dark to bright, while a separate background
region stays dark. A blank, uniformly white, stale, malformed or oversized frame
cannot pass. The predicates use normalized coordinates projected by the actual
camera, with seven-pixel interior neighborhoods and physical framebuffer bounds
of 64–4096 pixels on each axis.

All eleven required runtime data files from `cmake/runtime_data.cmake` are read
from the installed executable's `data` directory. The existing core readers parse
the template, conversion and category data. Required tables must contain parsed
entries; category hierarchies must resolve actual list entries. The receipt
records the size and SHA-256 of the exact captured bytes. Template/conversion
files are limited to 4 MiB each, category files to 64 KiB each, and hierarchy files
to 256 newline separators. Missing, linked, empty, oversized or unparseable data
fails the check. These are deployment checks, not a new strict grammar for every
legacy file accepted by the editor.

The final `installation-report.json` records the executable path, run identifier,
outcome, elapsed time, last phase, native platform, Qt/application versions,
OpenGL identity, parsed runtime files, and hashes of both inspected PNG files.
The parent independently captures and decodes the bounded child artifacts before
accepting them. Changed frame bytes are not shown in the GUI preview.

This establishes native shell, basic geometry rendering and deployment-data
availability for the recorded environment. It does not establish every editing
feature, all GPU drivers, game-engine compatibility, lighting equivalence, or the
complete Release test gate. Xvfb/software Mesa results must retain that renderer
identity; they are not physical-GPU observations. Package CLI/MCP acceptance and
native installation acceptance are separate release receipts.

## Isolation, ownership and limits

The controller runs outside the rendering process and enforces a 30-second
deadline, followed by at most two seconds waiting for its owned process to finish
after termination. Windows uses an owned kill-on-close Job; Unix uses the owned
process group. It never searches for or terminates an unrelated editor process.
On Qt 6.7 and newer, Unix session creation uses the native QProcess flags.
Earlier Qt 6 uses its child-process modifier, and Qt 5 uses the forked-child
setup hook. Both compatibility paths establish the session and reset inherited
signal dispositions/masks before exec, then exit without admission if setup
fails. The parent verifies the exact child owns its session and process group
before sending startup admission or retaining a group identifier for cleanup.
Combined stdout/stderr is limited to 256 KiB, progress records to 1 KiB, the child
JSON receipt to 64 KiB and each PNG to 8 MiB. Process-tree cleanup precedes artifact
admission. Missing executable, startup failure, crash, malformed output, timeout,
invalid evidence and cancellation produce an honest failure/cancellation receipt
when the report destination remains writable.

The internal rendering child waits for the controller's exact pipe admission
before starting Qt GUI work. A separate small watchdog exits only that child on
controller EOF even if its GUI thread is blocked in a driver. Internal child
arguments are an implementation detail, not another automation interface. There
is no dependency installer or engine invocation in this workflow. Filesystem and
kernel operations that themselves block cannot be made transactional by a GUI
timer; release orchestration retains its outer process deadline as well.

Before the child first reads application storage, it exclusively creates an
isolated storage directory inside its report directory. Settings, theme roots
and recovery destinations/searches stay there; legacy portable/native settings
are not imported. Storage redirection is startup-only and is refused after the
first application-storage path is used. The typed MainWindow installation
purpose also suppresses automatic game-install discovery, external resource
mounts, global-template discovery, plug-in discovery and recovery timers. It
does not enter the ordinary startup/recovery chooser. The two-argument normal
MainWindow constructor retains its original startup purpose.

The purpose is represented by one private, startup-only QObject property behind
a typed accessor. It is never serialized or changed after construction. This
keeps the existing MainWindow member layout stable while making the narrow
synthetic-frame adapter require both the explicit purpose and active isolated
storage. A normal editor window cannot accept diagnostic geometry.

## Verification

Focused tests cover argument/report admission, negative pixel controls, real
owned child timeout/cancellation/descendant cleanup, unrelated-process survival,
receipt identity and output limits, owner lifetime during callbacks, immutable
report selection, failure-atomic export, small-window controls, isolated storage,
bundled-data parsing and ownership-pipe EOF. MainWindow integration tests exercise
the actual Help action and document/history preservation. The standalone
integration harness additionally exercises the fresh-process isolated startup
and synthetic document adapter; a shared-process test cannot reset the
startup-only storage policy.

Release validation must also execute the public command from the actual extracted
native package, inspect its successful report, and verify package/user-storage
sentinels remain unchanged. Helper tests and an object-only compilation do not
replace that native run or the full configured Release CTest suite.
