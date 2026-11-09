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

Missing named resources and failed package checks stop the test. Some AI, COG and other resource dependencies cannot be resolved completely by the current static graph. When it reports an unverified scope, RED first prepares the known files, then presents **Review test resources** with **Test prepared episode** and **Cancel**. This keeps ordinary stock AI usable while explicitly leaving dynamic resource closure for the game test. Cancel removes the unlaunched owned stage; the valid setup settings remain remembered. Accepting the review does not turn an unverified dependency into a verified one.

The ordinary export preflight and immutable snapshot remain authoritative, including UV dimensions and MotS architecture. The command checks the document generation, authored Level, COG and resource-editor state, and resource configuration after export and again before launch. Cancellation, failed export, failed resource staging or a changed document cannot launch an older map. A process-creation failure is reported and leaves the fully prepared stage available for diagnosis. Successful stages remain available while the game uses them; remove obsolete test folders when no game is using them.

The launch workflow's setup/staging dialogs and caller callback boundaries cancel if their owner is destroyed, remove unlaunched owned output, and invoke no further callbacks. This is a scoped launch-workflow guarantee; it does not claim new destruction coverage for every pre-existing export-pipeline operation.

Starting a process establishes only that the operating system accepted the launch. It does not establish successful loading, gameplay, multiplayer behavior, lighting correctness or resource closure at runtime. Those remain actual-game acceptance checks.

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
