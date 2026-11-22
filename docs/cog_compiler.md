# The COG compiler

RED compiles COG scripts the way retail Jedi Knight 1.01 and Mysteries of the Sith load them. It tells you whether the
game will load a script at all, and it reports the mistakes the game accepts without a word but that break the script
when it runs. Nothing is run.

- In the app: select a placed COG and choose **Tools > COG Preflight (selected COG)**. The dialog shows the compiler's
  verdict and findings for the chosen game, then the preflight inventory. Tick **Show compiled code** for a listing.
- From the agent: `red-agent cog-compile <file.cog> <jk|mots> [--strict] [--listing]`. MCP: `sed_cog_compile`.

With `--strict`, a loader rejection or any error exits 5 with `ok: false` and `errorCode: "cogCompileErrors"`.
The JSON keeps the source hash, diagnostics, line numbers, handlers, table limits and requested listing. On MCP
the same report appears in an error tool result. Warning-only compilations still succeed.
[Headless level validation](headless_validation.md#placed-cog-scripts) also compiles placed JK/MotS scripts from captured archives.

## How it reads a script

- **The header** is read like the engine's config reader:
  - a line starting with `;` or `#` is a comment, and so is anything after a `#`;
  - lines are lowercased;
  - a line ending in `\` continues on the next;
  - words are separated by spaces, tabs or commas, and `key=value` is split at the first `=`.

  `//` is not a comment in the header: the engine skips a line that starts with it only because the first word
  isn't a type.
- **The code** is lexed and parsed with the retail grammar, and compiled to the engine's opcodes with its jump
  addresses. The grammar and code generation follow OpenJKDF2's reconstruction of the retail compiler (see
  `THIRD_PARTY_NOTICES.md`). In practice:
  - keywords and names are case-insensitive, and `stop` is `return`;
  - `end` ends the code wherever it appears;
  - a number that starts with 0 is octal, so `010` is 8;
  - there is no `break`, `continue` or `switch`;
  - a `for` loop needs all three parts;
  - the code needs at least one statement.

## What it reports

`loads` is the engine's own verdict. The game refuses a script only when the header is malformed (no `symbols`, no
`end`, no `code`) or the code has a syntax error. For a syntax error RED gives the file line and the line the engine's
console would print, which counts from the line after `code`.

Everything else loads and misbehaves:

| Finding | What the engine does |
|---|---|
| `unknownVerb`, `notAVerb`, `verbShadowed` | Calls nothing. A verb from the other game, a misspelling, or a symbol with a verb's name all end up here. |
| `argumentCount` | Doesn't check. Missing arguments are taken from whatever is on the stack. With extra arguments, the verb takes the last ones and the rest stay on the stack. |
| `noResult` | A verb that returns nothing, used as a value, gives whatever is on the stack. |
| `verbRunsAnother` | In both retail games `ModifyColorEffect` is registered to `FreeColorEffect`'s code: it frees the effect. |
| `assignToVerb`, `assignToMessage` | Overwrites the global entry for every script until the game restarts. |
| `undefinedLabel` | `goto` or `call` to a missing label jumps to address -1. |
| `unknownMessage`, `handlerTableFull` | Skips the declaration, so the handler never runs. |
| `labelOnGlobalName` | A label named after a message the script doesn't declare never runs. Its address is also kept in a slot that belongs to the global name. |
| `symbolTableFull` | A script holds 256 symbols, counting every string literal. Past that, names compile to the wrong symbol. |
| `linkTableFull`, `linkTableOverrun` | 128 linked symbols. Past that, resources are skipped, and variables are written past the table. |
| `labelTableOverrun` | JK has 512 label slots and MotS 1024, one per symbol and per `if`, `else`, `while` and `for`. Past that, the engine writes past the table. |
| `forWithoutCondition` | `for (a; ; c)` tests whatever is left on the stack. An empty stack reads as -1, which is true, so the loop runs until a stray 0 ends it, and without a sleep the game freezes. |
| `undeclaredNeverSet` | An undeclared name that is never assigned is always 0. |

Warnings and notes cover the rest:

- an int default in hex reads as 0;
- a vector default is ignored;
- vectors the engine can't read;
- statements with no effect (`x == 1;`);
- unclosed strings;
- characters the engine ignores;
- duplicate symbols and labels;
- messages without a handler.

## Verb signatures

A verb's arguments and result come from OpenJKDF2's reimplementation of that verb. The verb pops its arguments last
first, so the order of its pops, reversed, is its argument list. The measured retail registries decide which code
each name runs. Names that share one function in a retail executable share a signature, and that is how the
`ModifyColorEffect` bug shows up.

`tools/cog-registry/generate_cog_verb_signatures.py` regenerates the table, and the core tests check its digest.
Verbs whose argument count depends on a run-time setting have no fixed count, and RED doesn't check their arguments.
The only one is MotS's `jkPrintUNIVoice`.

## Checked against real scripts

Of the stock scripts:

- **JK:** all 543 load.
- **MotS:** 784 load.

Six MotS scripts don't load, and each is really broken:

- five have a syntax error (missing parentheses, a missing semicolon or a `break`);
- one has a stray byte before its header.

The stock scripts also contain bugs of the kinds in the table above, for example:

- `VectorSub(GetThingPos(GetLocalPlayerThing(), senderref))`;
- `jkEndLevel()` with no argument;
- `StopKey` with two arguments instead of three;
- a script that assigns to `randvec`;
- a misspelled `GetLocalPPlayerThing`.

A private research corpus of about 25,000 distinct community scripts compiles in about 11 seconds. Of these, 2% don't
load, and the cases sampled were all real errors.

## Not covered

- Running scripts: see [the COG runtime](cog_runtime.md).
- Type checks beyond literals passed where a verb wants a different kind.
- Infernal Machine scripts, for which there is no measured registry.
- Global symbol numbers in the listing are RED's own numbering, not the engine's.
