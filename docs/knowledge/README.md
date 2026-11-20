# JK and MotS knowledge library

This is what the JK/MotS research has learned about COG scripting, models and level performance. It is kept here so
that an agent working in RED can read it without a network connection or another repository. RED's own documents say
what RED does. These say how the games behave, what the community has done and what has been measured.

## How to use it

- **Read one topic at a time.** The flag reference alone is about 136 KB. Start from the tables below or search
  for the verb, flag or model term, and load the part you need.
- **Name the target first.** Retail JK 1.01, retail MotS, a patched build and an enhanced engine such as OpenJKDF2
  have different contracts. The same spelling doesn't guarantee the same behaviour, and each document says which
  target its evidence comes from.
- **Keep levels of evidence apart.** Structural checks, editor views, engine captures and gameplay prove different
  things. Treat a claim as established only for the target where it was observed.
- **When the library and RED disagree about RED, RED's documents, `red-agent schema` and the code win.** Both
  `workflow.md` files are the standalone workshops' agent skills. Their `scripts/` commands aren't part of RED; in
  RED, use `red-agent` and the [agent skills](../agent_skills/README.md).

## COG scripting

From Coda Lilitari's [JK/MotS COG Workshop](https://github.com/CodaLilitari/jk-mots-cog-workshop) 0.1.7.
RED's [COG compiler](../cog_compiler.md), [COG runtime](../cog_runtime.md) and
[gameplay assistants](../gameplay_assistants.md) are the checking end.

| Document | Covers |
|---|---|
| [guide](cog/guide.md) | Writing COGs for Jedi Knight and Mysteries of the Sith |
| [flags](cog/flags.md) | COG flags and numeric values: an evidence-backed reference |
| [flag-discoveries](cog/flag-discoveries.md) | Flag findings: JKDF2, MotS, and Indy |
| [compatibility](cog/compatibility.md) | Target compatibility: JK 1.01, MotS and extensions |
| [static-capacity](cog/static-capacity.md) | Static COG instance capacity |
| [workflow](cog/workflow.md) | JK / MotS COG workshop |
| [sources](cog/sources.md) | Sources, attribution and evidence boundaries |
| [corpus](cog/corpus.md) | Public COG research corpus |

### Research notes

| Document | Covers |
|---|---|
| [authoring-and-testing](cog/notes/authoring-and-testing.md) | Authoring and testing COG behavior |
| [authoring-decisions](cog/notes/authoring-decisions.md) | Choose the next COG check |
| [beefcaike-cog-study](cog/notes/beefcaike-cog-study.md) | BeefCaike / Dralloc: practical COG patterns and integration evidence |
| [case-lessons](cog/notes/case-lessons.md) | Advanced COG lessons from recovered systems |
| [checksums-and-retail](cog/notes/checksums-and-retail.md) | Retail checksums: measured mechanism and practical limits |
| [classic-cogs-and-binding-lessons](cog/notes/classic-cogs-and-binding-lessons.md) | Classic COGs: appearance, state changes and real bindings |
| [engine-findings](cog/notes/engine-findings.md) | Engine evidence for JKDF2 and Mysteries of the Sith COG |
| [flag-reference](cog/notes/flag-reference.md) | Using the replacement flag reference |
| [historical-sources](cog/notes/historical-sources.md) | Historical archives, authors and generator research |
| [language-and-execution](cog/notes/language-and-execution.md) | Writing and reasoning about COG |
| [pattern-atlas](cog/notes/pattern-atlas.md) | Retail COG pattern atlas: JKDF2 and Mysteries of the Sith |
| [research-and-retrieval](cog/notes/research-and-retrieval.md) | Retrieval, local source import and evidence grades |
| [smsithlord-website-lessons](cog/notes/smsithlord-website-lessons.md) | Lessons from SM Sith Lord's Jedi Knight work |
| [target-contracts-and-lifecycle](cog/notes/target-contracts-and-lifecycle.md) | Target contracts and lifecycle: authoring findings for 0.1.3 |
| [triscuit-generator-engine](cog/notes/triscuit-generator-engine.md) | Triscuit generator: parser, code-size and native-call findings |
| [verb-families](cog/notes/verb-families.md) | Practical COG verb families |

## Models and animation

From the [JK/MotS Model Workshop](https://github.com/CodaLilitari/JK-MotS-Model-Workflow) 1.7.0. Its
[credits](models/CREDITS.md) name the community authors whose methods it studies, and its
[validation record](models/VALIDATION.md) lists what was tested. RED's [model import](../model_import.md),
[model lighting](../model_lighting.md) and [hierarchy and KEY](../model_hierarchy.md) guides cover RED's part.

| Document | Covers |
|---|---|
| [workflow](models/workflow.md) | JK / MotS model workflow |
| [methods-and-options](models/methods-and-options.md) | Methods and options |
| [compatibility](models/compatibility.md) | Model compatibility: rig, native formats, and engine targets |
| [runtime-and-packaging](models/runtime-and-packaging.md) | Runtime validation and model packaging |
| [testing-methods](models/testing-methods.md) | Testing models in original JK/MotS and OpenJKDF2/OpenMotS |
| [blender-export-and-baking](models/blender-export-and-baking.md) | Blender, export and baking: choose the method that fits the job |
| [blender-sith-fork](models/blender-sith-fork.md) | Using the Blender-Sith Agentic fork |
| [blender-sith-audit](models/blender-sith-audit.md) | Blender-Sith: source audit and reproducible export probes |
| [rigid-joint-and-face-diagnostics](models/rigid-joint-and-face-diagnostics.md) | Diagnose rigid joints and face registration |
| [textures-and-imagegen](models/textures-and-imagegen.md) | Textures, image generation and mapped review |
| [same-name-material-editions](models/same-name-material-editions.md) | Same-name material editions for original Jedi Knight |

### Generated models, props and voices

| Document | Covers |
|---|---|
| [tripo-to-jk](models/tripo-to-jk.md) | Hosted Tripo sources to rigid JK character models |
| [partial-generation](models/partial-generation.md) | Partial generation and donor assembly |
| [local-generation-automation](models/local-generation-automation.md) | Codex → local generation → Blender |
| [environment-props-to-jk](models/environment-props-to-jk.md) | Generated environment props to native JK models |
| [generated-character-voice](models/generated-character-voice.md) | Generated character voices: Suno takes to a JK sound class |
| [advisory-and-delegation](models/advisory-and-delegation.md) | ChatGPT Pro advisory and practical delegation |

### Community, corpus and case studies

| Document | Covers |
|---|---|
| [additional-discord-model-methods](models/additional-discord-model-methods.md) | Additional Discord model-method findings from the downloaded corpus |
| [additional-geometry-animation](models/additional-geometry-animation.md) | Additional geometry and animation research from the downloaded corpus |
| [additional-surface-methods](models/additional-surface-methods.md) | Additional surface research from the downloaded corpus |
| [community-discord-model-methods](models/community-discord-model-methods.md) | Historical Discord model methods: options, authors and limits |
| [community-discord-research](models/community-discord-research.md) | Community Discord research: model choices and observed limits |
| [community-modeling-research](models/community-modeling-research.md) | Community methods for character models, animation and player integration |
| [community-surface-research](models/community-surface-research.md) | Community research: model surfaces, materials and export |
| [downloaded-corpus](models/downloaded-corpus.md) | Research from the downloaded JK / MotS material collection |
| [downloaded-material-tools](models/downloaded-material-tools.md) | Downloaded tools and material bytes: additional surface findings |
| [downloaded-model-integration](models/downloaded-model-integration.md) | MotS model and personality integration: seven inspected packages |
| [downloaded-tools-geometry](models/downloaded-tools-geometry.md) | Retained modeling tools and native examples |
| [jke-tdir-shred-research](models/jke-tdir-shred-research.md) | TDIR, JKE, and Shred: reusable modeling methods |
| [mortal-kombat-research](models/mortal-kombat-research.md) | Historical Mortal Kombat skins for Jedi Knight |
| [research-index](models/research-index.md) | Reusable research library |
| [smsithlord-validation-lessons](models/smsithlord-validation-lessons.md) | Animated skins, sound capacity and target-engine review |
| [source-archive](models/source-archive.md) | Source archive and offline research |
| [tdir-beta-research](models/tdir-beta-research.md) | TDIR's leaked beta: identity, roster evidence and recovery leads |
| [terminator-case-study](models/terminator-case-study.md) | Terminator case study: combining conversion, proportion work, baking, and retail integration |

## Levels

- [Frame rate in large one-sector levels](levels/performance.md): where the frame goes in OpenJKDF2, COGs that
  scan every Thing, safe vertex welds, face merges and straight-corner removal, what was tried and not adopted, and
  how to measure. These are the Sith Council lessons.
- [Qualifying a playable JO/JA conversion](../jo_ja_import.md#qualifying-a-playable-conversion) covers Bespin
  rooftop barriers, Council's multiplayer material capacity and RJA's indexed leaf edges in accelerated rendering.
- The [JK/MotS level workflow skill](../../tools/jk-mots-level-workflow/SKILL.md) and its
  [references](../../tools/jk-mots-level-workflow/references/research-index.md): editor, compatibility and testing
  research for levels.
- RED's [lighting](../lighting.md), [game testing](../game_testing.md) and [JO/JA import](../jo_ja_import.md)
  guides.

## Where it comes from

| Folder | Source | Commit | Licence |
|---|---|---|---|
| `cog/` | [CodaLilitari/jk-mots-cog-workshop](https://github.com/CodaLilitari/jk-mots-cog-workshop/tree/b4b43f6ab1976f673598c90751ec6160da70c506) | `b4b43f6` | MIT ([licence](cog/LICENSE.md), [notice](cog/NOTICE.md)) |
| `models/` | [CodaLilitari/JK-MotS-Model-Workflow](https://github.com/CodaLilitari/JK-MotS-Model-Workflow/tree/ca011b84c4fa5e1f3755e1543be2c1c1d25063c9) | `ca011b8` | MIT ([licence](models/LICENSE.md)) |
| `levels/` | Written for RED from the Sith Council measurements, November 2026 | — | GPLv3, like the rest of RED |

Authorship is recorded in the COG Workshop's [notice](cog/NOTICE.md) and the Model Workshop's
[credits](models/CREDITS.md). Each document also credits the community work it draws on.

Each copied file names its source file and commit. Links between copied files point within this library. Links
to anything not copied point to the source repository at the same commit.

**Left out:** everything that isn't Markdown, and the workshops' own update and reporting instructions. That
means the JSON data (the flag catalogues in `corpus/reference/`, the model research's evidence inventories and its
runtime test plan), the scripts and the examples. The documents link to these where they cite them. Raw evidence
the workshops kept local was never published, so it isn't here either.

To refresh the copy, clone both repositories and run, from RED's root:

```sh
python3 -B tools/knowledge/import_knowledge.py <jk-mots-cog-workshop> <JK-MotS-Model-Workflow>
```

Then check the diff, update the commits and versions in this file, and commit. The script rewrites `cog/` and
`models/`, removes files the sources no longer have, and leaves this index and `levels/` alone.
