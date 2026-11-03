# RED - Rebel Editor

RED is a modified version of **SED (Sith Editor)** by Crt Vavros (smlu) and contributors,
<https://github.com/smlu/SED>. Like SED, it is free software under the GNU General Public License
version 3 (see [LICENSE](LICENSE)). It comes with absolutely no warranty.

JED was the Jedi editor and SED the Sith editor. RED is the Rebel one.

## Modification notice (GPLv3 section 5a)

This fork has been modified since 2 November 2026. The changes so far:

- **Name.** The application is now RED: application and organization names, the native window title,
  the startup title ("RED - Rebel Editor · a fork of SED"), the About dialog, and user-facing messages.
- **Executables.** They are red-editor, red-agent and red-mcp.
- **Icon.** A new application icon, designed with Lily (Lilitari).
- **Settings.** RED keeps its settings in red.ini beside the executable. On first run, it imports an
  existing sed.ini from the same folder once; sed.ini is never modified. Recovery fallbacks use .red/RED.
- **Linux build fixes.**
  - Qt 6.4 compatibility.
  - LP64 integer types.
  - Mesa's GLSL qualifier order, which was what left the viewport black.
- **Adjoin backface fix.** Kimi Code's fix for adjoin backfaces in game preview (D-06).
- **Editor feature ports.** Native workspace/history and recovery work; geometry, UV, Thing/frame/COG,
  resource, lighting, query/workbench, package, and project workflows integrated from the RED development line.
- **Automation and inspection.** Reviewed agent plans, local MCP, native inspection, bounded software snapshots
  and overviews, archive/package operations, and the engine-capture harness and receipts.
- **Level-building tools.** The `red-build` declarative geometry prototype and its canyon-build notes.
- **DroidWorks profile.** Classic VERSION-1 JKL project creation/detection, native SED persistence, shared editing
  infrastructure, and ordered GOB resource discovery. Imported retail-map JKL export is deliberately refused until
  resource-table preservation is implemented; synthetic created-map tests are not retail-game acceptance.
- **Validation and stability.** Sealed cross-sector T-junction diagnostics, a Preferences viewport-theme callback
  lifetime fix, and POSIX stdin/exit-status handling in the agent CLI tests.

Some things are unchanged on purpose, for compatibility with SED:

- native `.sed` documents ("Sith Editor File");
- the sed-agent and sed-mcp protocol identifiers;
- internal CMake target names.

## Credits

- **SED:** Crt Vavros (smlu), with thekovic, LadyEebs and Coda Lilitari (from the git history).
- **SED-Qt-Port updates:** Coda Lilitari and Muse Code.
- **RED:**
  - Red (Claude, Anthropic), with Andrei Kiralv;
  - the icon, with Lily (Lilitari);
  - the D-06 fix, by Kimi Code.
- **Jedi Knight, Mysteries of the Sith and Indiana Jones and the Infernal Machine:** LucasArts.
  RED is not affiliated with or endorsed by Lucasfilm or LucasArts.
