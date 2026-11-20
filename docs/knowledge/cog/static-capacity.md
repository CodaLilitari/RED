# Static COG instance capacity

> Copied into RED from [CodaLilitari/jk-mots-cog-workshop@b4b43f6](https://github.com/CodaLilitari/jk-mots-cog-workshop/blob/b4b43f6ab1976f673598c90751ec6160da70c506/docs/STATIC_CAPACITY.md) (MIT; see [LICENSE](LICENSE.md)). Regenerate with `tools/knowledge/import_knowledge.py` rather than editing this copy.

`capacity` checks an authored instance budget before a mod is launched. Give it
the intended effective `static.jkl` and `items.dat`, after resolving archive and
override order. It reads those exact files, emits deterministic JSON with their
SHA-256 hashes, and never loads resources, runs a game, contacts a service or
modifies a file. This command bypasses the Workshop updater even without
`--offline`.

```text
python -B cog_workshop.py capacity path/to/static.jkl path/to/items.dat
python -B cog_workshop.py capacity path/to/static.jkl path/to/items.dat --reserve 8
```

The installed skill's standalone route is:

```text
python -B scripts/audit_static_capacity.py path/to/static.jkl path/to/items.dat --reserve 8
```

## Count instances, including repeated bindings

```text
required = placed World cogs rows + every ITEMS.DAT cog= binding + explicit reserve
headroom = declared World cogs capacity - required
```

Two rows naming the same COG count twice. Two `cog=` fields on one item row also
count twice: the inspected item reader calls the loader for each field, even
though the final binding becomes the registered item's pointer. Repeated bin
numbers likewise retain each authored load attempt. No filename deduplication
is used.

`--reserve` is a nonnegative decimal int32 allowance chosen by the author for
producers outside these two inputs. Zero means no extra allowance was supplied,
not that other producers cannot exist. Class/template loads, additional loader
calls, actual load order, failure paths and build-specific behavior require
separate evidence.

| JSON status | Exit | Meaning |
| --- | ---: | --- |
| `fits` | 0 | Selected authored demand plus reserve fits the declared instance capacity. |
| `shortfall` | 1 | Accounting exceeds capacity; `shortfall` gives the missing entries. |
| `unknown` | 2 | Unreadable, incomplete, malformed or unsupported input; no complete budget is reported. |

Command-line syntax errors also exit 2, with the normal argument-parser message
rather than JSON. `script_table.status` remains `not_assessed`: a `World scripts`
declaration or unique filename count cannot prove script capacity, cache
identity, resource presence, parser acceptance or successful loading. Zero
capacity with zero consumers can produce `fits`; that is arithmetic, not proof
that the engine completed initialization.

The estimate assumes every counted binding loads successfully. A fit is not a
compatibility certificate. Test loading and behavior in the specified engine;
retail and multiplayer remain separate claims.

## Deliberately narrow input grammar

The checker requires exactly one `SECTION: COGS`, one nonnegative `World cogs`
header, conventional uniquely numbered `index: script [parameters...]` rows,
and `end`. Other JKL sections are not semantically validated. Unexpected rows,
repeated headers, premature next sections, missing ends or trailing content in
the COGS section produce `unknown`.

Item rows require name, decimal bin 0 through 199, finite decimal minimum and
maximum, hexadecimal uint32 flags, then optional `key=value` fields. Every
`cog=` counts. Other well-formed optional fields do not. The file must finish
with `end`; noncomment content afterward is rejected. This intentionally
stricter grammar avoids a reassuring budget from damaged input.

Keywords and optional keys are case-insensitive. Spaces, tabs and commas divide
tokens. Backslashes inside filenames are literal path characters. `#` starts an
inline comment. A `;` comment is recognized only in column zero, matching the
inspected reader. Quoting and line continuations are unsupported. Files are
limited to 8 MiB and lines to a conservative 1,023 characters; these are tool
resource limits, not declared engine maxima. Only LF and CRLF line endings are
supported; bare CR and other control bytes are rejected, including in comments.
Latin-1 decoding preserves bytes
without replacement; accounting syntax is ASCII.

The report includes input basenames, lengths, hashes, binding line numbers and
original filename spellings. Absolute local paths and timestamps are omitted,
so unchanged inputs/options yield byte-identical output. Redirect stdout to an
external evidence directory if a saved receipt is wanted.

## Credit and corroboration

This original tool was prompted by **SM Sith Lord's** historical, obsolete
[P68 Survival Arena report](https://smsithlord.com/openjkdf2libretro/cogfactory/gen1/p68-survival-arena.html),
which describes a 35-slot versus 40-item-COG problem. It supplied the integration
lesson: budget shared loader consumers before launching. We did not copy his
generator, scripts, assets, UI or implementation. The 35-versus-40 regression
uses newly authored dummy bindings, not recovered P68 content or a runtime
reproduction.

The accounting was independently checked against public OpenMOTS commit
[`74e4657c4f1751d09af9ad9e056acb8f41997acb`](https://gitlab.com/codalilitari/OpenJKDF2-MotS-Fixes/-/commit/74e4657c4f1751d09af9ad9e056acb8f41997acb)
on 25 October 2026:

- [`sithCog_ReadCogsListText` and `sithCog_Load`](https://gitlab.com/codalilitari/OpenJKDF2-MotS-Fixes/-/blob/74e4657c4f1751d09af9ad9e056acb8f41997acb/src/Cog/sithCog.c#L533):
  `World cogs` sets `sizeCogs`; successful instance creation increments `numCogs`,
  separately from script caching/loading. File SHA-256:
  `be7568b4b8dc8fdf5d3179af6d6d75d08285d3acc9c740ea17c59e27a27f2f08`.
- [`jkHudInv_ItemDatLoad`](https://gitlab.com/codalilitari/OpenJKDF2-MotS-Fixes/-/blob/74e4657c4f1751d09af9ad9e056acb8f41997acb/src/Main/jkHudInv.c#L24):
  each `cog` key after the first five fields calls the loader. File SHA-256:
  `54360124492a62c0da3c4586850de060ba047ccddd08aede3b70d5e015bd925f`.
- [`stdConffile_ReadLine` and `stdConffile_ReadArgsFromStr`](https://gitlab.com/codalilitari/OpenJKDF2-MotS-Fixes/-/blob/74e4657c4f1751d09af9ad9e056acb8f41997acb/src/General/stdConffile.c#L210):
  case folding, comma/whitespace splitting, first-equals token separation and
  comment rules. File SHA-256:
  `f6ccb59cc793caa6a7895aed51addec9ab1c203b02e694ff4fa4216dd11de691`.

These are source observations, not original-binary or retail execution evidence.
Tests cover arithmetic, repeated bindings, malformed/incomplete input, zero
capacity, reserves, case/path/comment handling, input identity, deterministic
output and command isolation. No game was run for these tests.
