# Skills for agents that use RED

These are skills for AI agents that build levels, scripts and models with RED's command-line agent (`red-agent`)
or its MCP server (`red-mcp`). They are in the Agent Skills format: a folder with a `SKILL.md` whose front matter
says when to use it. Copy a folder into your agent's skills directory (for Claude Code, `~/.claude/skills/`; for
Codex, its skills folder) or point the agent at it.

| Skill | Use it for |
|---|---|
| [`red-level-editing`](red-level-editing/SKILL.md) | Inspecting, editing, validating, previewing and packaging JK and MotS levels |
| [`red-cog-scripting`](red-cog-scripting/SKILL.md) | Compiling and running COG scripts, and doors, lifts, switches, objectives and cutscenes |
| [`red-model-authoring`](red-model-authoring/SKILL.md) | Native 3DO UV/mesh edits, Model Studio inspection, import, lighting, hierarchy and KEY authoring/playback |

They work alongside Coda Lilitari's JK/MotS COG Workshop and JK/MotS Model Workflow skills, which carry the
research and the Blender pipeline; these cover what RED itself does. A copy of that research ships with RED in the
[knowledge library](../knowledge/README.md), with level performance notes. The skills summarize [the agent
workflow](../agent_workflow.md), which stays the full reference. When the two disagree,
the workflow document and `red-agent schema` win.

The retail playtesting guidance in these three skills is adapted from Red's November 2026 authoring notes.
