# Save JKL and Test

**File > Save JKL & Test...** exports the current Jedi Knight or Mysteries of the Sith map into a new isolated test folder and starts the selected engine. The editable `.sed` or imported `.jed` path, dirty state, undo history and ordinary repeat-export destination remain unchanged. Save the editable project separately when you want to checkpoint it.

The dialog remembers the game installation, test-output folder and engine choice separately for JK and MotS, and remembers a separate executable for each engine profile. Cancel in the setup dialog changes no settings and creates no test files. The filename is limited to 27 ASCII letters, digits, underscores or hyphens followed by `.jkl`, matching the engine episode sequence field.

Save modified COG scripts and resource-editor files before testing. RED refuses when those drafts are unsaved because the resource resolver reads saved files and archives. The command leaves those drafts unchanged and does not save unrelated resources automatically.

| Profile | Behavior |
|---|---|
| OpenJKDF2 | Uses the command protocol verified in OpenJKDF2 v0.9.9: `-devmode -debug log`, MotS's `-motsCompat` when appropriate, `-path <stage> -autostart -singleplayer -episode redtest -map <filename.jkl>`. It requests the current map directly. Select a compatible executable explicitly; RED does not infer feature support from the executable's name. |
| Retail JK / MotS | Uses `-devmode -debug log -path <stage>`. After the game starts, choose **New Game** and the **RED Test** episode. Retail has no supported direct-map command-line option. A renamed executable, including Steam's `JediKnight.EXE`, is valid when Retail is selected explicitly. Direct native Retail launch requires Windows. |

The working directory is the selected game installation. OpenJKDF2 also requires `resource/jk_.cd` there so its install helper does not choose another working directory. This marker check is a launch prerequisite, not proof of a complete or working installation.

The engine splits its command line at whitespace without interpreting quotes. RED therefore chooses a valid relative or absolute resource-path token, or an actual Windows short-path alias when available. The token must use printable ASCII without spaces or quotation marks, and the token plus each required resource filename must fit the engine's 127-byte path budget. A path that cannot satisfy this constraint is refused with a diagnostic; choose a shorter test-output folder without spaces. The executable path itself remains a separate operating-system argument and may contain spaces.

Each run allocates a new `redtest-*` directory under the selected test-output folder, which must be outside the game installation. RED never installs an episode into the game's `episode` directory. Retail enumerates its installed episode entries and reads the test's overriding `episode.jk` through the developer resource path. OpenJKDF2 receives the synthetic episode identifier and explicit map directly. The current export and the generated `episode.jk` are written as loose files in the owned stage.

Resource staging reuses the existing package dependency graph. It resolves the original authored resource name against the editor's selected resource winners, then the selected game's resources when the editor has no winner. A referenced resource is omitted from the stage only when its complete bytes match the selected game's winner. Other referenced resources are copied into the owned stage. Bare names and canonical prefixes such as `stone.mat` and `mat/stone.mat` may share one output path only when their captured bytes agree. A conflicting alias or unsupported nested spelling such as `variants/stone.mat` refuses instead of substituting another resource. The graph and byte budgets remain bounded; each distinct alias read consumes budget, and an oversized selected winner never falls back to a shadowed resource.

Use **Additional test resources** for files that scripts or mission conventions load without a discoverable JKL reference. Add a logical **Folder** and **Resource name**, such as `misc` and `cogstrings.uni`; these select the current resource winner. **Add resource** and **Remove selected** edit only this test list. Names retain their authored spelling while physical staged paths use the shared lowercase runtime convention. Invalid paths, duplicate additional destinations and more than 1024 rows are refused. An empty list adds nothing automatically.

The list is remembered for this source file and game. Another project has its own list; Untitled projects do not retain it. Canceling setup saves no changes. Setup fields scroll while **Run** and **Cancel** stay accessible in a small window.

Missing named resources and failed package checks stop the test. Some AI, COG and other resource dependencies cannot be resolved completely by the current static graph. When it reports an unverified scope, or when additional resources are selected, RED first prepares the known files, then presents **Review test resources** with **Test prepared episode** and **Cancel**. Its inventory shows canonical physical paths, captured source categories, byte counts and SHA-256 values, distinguishing copied resources from exact matches already supplied by the selected game. Source categories do not identify a physical archive row. Unknown dependency scopes remain visible and the inventory does not certify complete runtime closure. Cancel removes the unlaunched owned stage; the accepted setup settings remain remembered. Accepting the review does not turn an unverified dependency into a verified one.

Package-generated physical resource names use lowercase ASCII and engine separators, matching the
native GOB lookup contract. Authored logical references and resource payload bytes retain their
original spelling and contents. Invalid relative paths, names beyond the 127-byte engine lookup
budget, and conflicting aliases refuse publication. Dependency inspection continues to show the
original archive row spelling; its case-insensitive lookup does not prove native runtime reachability.

The ordinary export preflight and immutable snapshot remain authoritative, including UV dimensions and MotS architecture. The command checks the document generation, authored Level, COG and resource-editor state, and resource configuration after export and again before launch. Cancellation, failed export, failed resource staging or a changed document cannot launch an older map. A process-creation failure is reported and leaves the fully prepared stage available for diagnosis. Successful stages remain available while the game uses them; remove obsolete test folders when no game is using them.

The launch workflow's setup/staging dialogs and caller callback boundaries cancel if their owner is destroyed, remove unlaunched owned output, and invoke no further callbacks. This is a scoped launch-workflow guarantee; it does not claim new destruction coverage for every pre-existing export-pipeline operation.

Starting a process establishes only that the operating system accepted the launch. It does not establish successful loading, gameplay, multiplayer behavior, lighting correctness or resource closure at runtime. Those remain actual-game acceptance checks.

## Reproducible engine capture

For an isolated posed capture or an ordinary-control gameplay run, use **Tools > Engine Capture…**. It is an explicit
request editor for the [capture runner](../tools/engine-capture/README.md), separate from the
interactive Save JKL & Test workflow. Select the engine profile and graphics backend, episode,
map, exact camera position and rotation, sector, field of view, output dimensions, frame, and
time/log limits. Add each input file with its staging destination and role, then calculate its
SHA-256. Import and export preserve the request format used by automation; editing the request
does not change the open level or export it implicitly.

Enable **Use a packaged episode (GOB/GOO)** to test an existing package without extracting it.
**Choose episode archive…** declares its source and exact `episode/<episode>.gob` or `.goo`
destination. **Add required rows** supplies the matching engine/configuration placeholders and
one scenario archive row; the loose route remains the default when packaged mode is disabled.
Declare stock/runtime inputs explicitly. In packaged mode every scenario input must be a direct
`episode/` GOB/GOO container; remove loose episode rows before preparing. The runner checks the
selected staged archive's directory before launch. This checks package admission, not resource closure.

Enable **Run an ordinary-control gameplay recipe** and choose/load its JSON file for the Linux
agent engine route. The bounded loader retains the parsed inline object and shows the exact file
SHA-256 alongside the hash of a compact serialization of the retained inline object. Imported inline requests cannot
recover original recipe-file bytes and are labelled accordingly. The loaded snapshot is the input;
editing a recipe file later requires loading it again. No shell or command-harness text is executed
by this dialog.

Recipes contain 1–256 bounded steps for ordinary key/mouse/button/text/release input and
wait/assert/checkpoint/sleep observations. Prepare delegates the full schema and launch-identity
checks to the runner. The recipe's game, episode and map must match the request, and gameplay needs
an explicit `resource/jk_.cd` input. Windows gameplay is refused. Camera pose, resolution, field of view and
capture-frame controls are disabled and ignored in gameplay mode; checkpoints capture the observed
player view. A camera-less gameplay request imports with disabled default camera controls. Gameplay
defaults to 180,000 ms engine time and 210,000 ms total time, bounded at 600,000/630,000 ms; returning
to posed capture restores its existing limits.

**Prepare / validate inputs** captures SHA-256 values for the explicit source rows and checks the
request without staging game files or launching an engine. **Import request** preserves its declared
hashes; validation alone does not certify that those imported hashes match current files. **Run capture**
revalidates the complete request, verifies input hashes and uses a new output directory. **Cancel work**,
or closing an active dialog, asks the runner to stop its
owned processes and record incomplete identity evidence. Inspect the resulting launcher receipt
and its hash-checked PPM using the dialog; an old completed receipt cannot make a refused run
appear successful. Posed completion reports a frame. Gameplay receipts retain the controller's
scenario/journal identities and allow selection among up to 32 hash-checked observed frames. Frame
paths stay within the receipt's session, and each displayed image must match its reported hash and
dimensions. The full original receipt preserves the distinction between allowed staged configuration
writes and unchanged immutable inputs. Neither mode grants general game acceptance or resource closure;
gameplay evidence proves only the explicitly observed steps and assertions.

The package includes the runner beside the editor under `tools/engine-capture`. Choose an existing
Python 3 interpreter and a capture-capable OpenJKDF2 build explicitly; RED does not install them.
Windows and Linux run their matching engine profiles. macOS can prepare requests and inspect
receipts, but the runner has no macOS engine profile. Retail remains available through Save JKL &
Test and does not implement this capture protocol.

## Read-only launch planning

`red-agent launch-plan request.json` uses the same portable planner and never exports, writes settings, creates a stage or starts a process. The request keeps the existing fields:

```json
{
  "schemaVersion": 1,
  "game": "MysteriesOfTheSith",
  "engine": "OpenJKDF2",
  "gameDirectory": "C:/Games/MotS",
  "projectDirectory": "C:/RED-tests/redtest-example",
  "levelFileName": "current.jkl",
  "executable": "openjkdf2-64.exe"
}
```

`game` is `JediKnight` or `MysteriesOfTheSith`. `engine` is `OpenJKDF2`, `Retail`, or the compatible `Auto` mode. Auto recognizes known OpenJKDF2 executable names and the appropriate `jk.exe`/`jkm.exe`; a renamed executable requires an explicit profile. `executable` can be absolute or relative to `gameDirectory`.

The result contains the resolved executable and working directory, exact argument array, `projectPathToken`, required loose files, individual prerequisite checks, `ready`, `directMapStart`, and `launches: false`. Missing prerequisites produce a reviewable `ready: false` plan; unsupported profile, game, map filename or resource path syntax is a usage error. The expected loose map is `<projectDirectory>/jkl/<levelFileName>`, beside `<projectDirectory>/episode.jk` at the stage root. A plan does not inspect these files' semantic content or prove that the selected binary supports its declared profile.

## Ownership and source evidence

`src/core/include/sed/level/game_launch.hpp` owns the portable plan. `src/app/src/ui/project/game_test_workflow.*` owns the dialog, settings, staging and process boundary. `main_window/menus/file_menu.cpp` supplies the existing export and authored-snapshot guards. `PackageRequest::resolveResource` adapts an immutable application source into the existing package graph without changing archive-only callers.

The argument parser and direct-start route are verified against [OpenJKDF2 v0.9.9 Main.c](https://github.com/shinyquagsire23/OpenJKDF2/blob/v0.9.9/src/Main/Main.c), with the current implementation pinned at [ae6ed8a Main.c](https://github.com/shinyquagsire23/OpenJKDF2/blob/ae6ed8a81b316421f08a46719b12a4fa9e2fbed9/src/Main/Main.c). [jkMain.c](https://github.com/shinyquagsire23/OpenJKDF2/blob/ae6ed8a81b316421f08a46719b12a4fa9e2fbed9/src/Main/jkMain.c), [jkRes.c](https://github.com/shinyquagsire23/OpenJKDF2/blob/ae6ed8a81b316421f08a46719b12a4fa9e2fbed9/src/Main/jkRes.c) and [jkEpisode.c](https://github.com/shinyquagsire23/OpenJKDF2/blob/ae6ed8a81b316421f08a46719b12a4fa9e2fbed9/src/Main/jkEpisode.c) establish the episode, map and developer override paths. The retail argument route also matches `TJedMain.SaveJKLGOBandTest1Click` in the original [Pascal SED source](https://github.com/smlu/SED/blob/master/src/JED_MAIN.PAS).
