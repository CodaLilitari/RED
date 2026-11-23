# Authored JK and MotS sound preloads

RED retains authored JKDF2 and MotS `WORLD SOUNDS` rows through JKL import, native `.sed` Save/Open,
checkpoint recovery, persisted Undo/Redo, and ordinary or compact JKL export. A WAV does not need a Sector,
COG, or template reference to retain its authored preload action.

## Ordered loader actions

These original synthetic rows deliberately contain a duplicate:

```text
SECTION: SOUNDS
WORLD SOUNDS 10
zeta.wav
alpha.wav 0
zeta.wav 1
END
```

The runtime processes rows in order. The presence of a second argument requests preload; its numeric truth
value is not tested, so the literal `0` is significant. A later marked cache lookup can behave differently
from the first registration. The marker does not by itself establish waveform residency, successful loading,
or playback. RED preserves filename case, duplicates, marker spelling, additional arguments, and the native
comma and `key=value` argument forms.

The retained unit is a logical row. Physical line continuations are joined without adding whitespace;
column-one semicolon comments and hash comments are removed. Comments and original physical line layout are
not retained. A literal terminal backslash is protected by an empty hash comment on export so the emitted
newline does not turn it into a continuation. Compact output retains complete sound action rows instead
of rewriting numeric-looking marker text.

The native reader removes an inline hash comment before testing the penultimate byte for a backslash.
For example, `Bell\ # joined` followed by `Tone.wav 0` becomes the single action `BellTone.wav 0`.
The space after the backslash is significant. An immediate hash after the backslash in `marker\#`
retains the literal backslash.

The source evidence is the pinned OpenJKDF2 implementation of
[`sithSound_ReadSoundsListText` and sound registration](https://github.com/shinyquagsire23/OpenJKDF2/blob/545eace7ec5b0b35e18768adc6cd22653fc440ca/src/Devices/sithSound.c#L71-L209)
and the shared [argument/line reader](https://github.com/shinyquagsire23/OpenJKDF2/blob/545eace7ec5b0b35e18768adc6cd22653fc440ca/src/General/stdConffile.c#L210-L299).
These static contracts do not replace testing the exported episode with its intended runtime and resources.

## Authored prefix and inferred sounds

Export writes the retained actions first. It then appends the deterministic, case-insensitive, unique list
of newly inferred sounds that the authored effective filenames do not already name. Existing static-resource
filtering applies to that inferred tail; it does not remove an explicitly authored action.

Committing an export does not convert its generated tail into permanent authored rows. Removing the last
reference to a generated sound can therefore remove it on a later export from the same document. Opening an
exported JKL as a new document treats the rows in that file as authored input. Native projects retain the
distinction, independently of the transient whole-JKL source cache.

Dependency inspection and packaging interpret the same sound arguments, continuations, comments, and effective
`end` terminator. A zero-capacity table contributes no active preload dependencies. Missing active WAVs remain
subject to the existing dependency and package checks.

The first possible JK/MotS SOUNDS header triggers a dialect check in the dependency and package readers.
These readers charge one work unit before examining each physical row for a project signature, which can
follow the sound table. Exhausting the work budget yields an incomplete scan. A map without a possible
SOUNDS header retains its single-pass cost. IJIM and DroidWorks retain their lexical sound-table references.

## Capacity and Extra Buffers

`WORLD SOUNDS` is an allocation capacity, not a physical row count. On import, RED records a capacity base equal
to the smaller of the declared capacity and the number of distinct effective nonstatic names, excluding `none`.
The remaining capacity belongs to the existing editable **Sounds Extra** setting.

For the example above, the base is 2 and Extra is 8. An unchanged export writes capacity 10 and all three rows.
Likewise, a capacity of 1 with two actions naming the same WAV remains 1. An unchanged underdeclared positive
table also retains its declared capacity. Appending new sounds reserves space for the distinct authored prefix,
the new tail, and Extra, so the prefix cannot consume a new dependency's slot. These counts are conservative
static accounting; actual allocation also depends on available resources and the runtime's shared sound cache.

A source capacity of zero makes its body dormant. RED retains that zero on an unchanged export. If an inferred
sound would require using a populated dormant list, export refuses and explains how to set **Level Settings >
Extra Buffers > Sounds Extra** to a positive value to activate it deliberately. A matching authored filename
does not satisfy a new reference while its table is dormant. Read-only buffer inspection remains available to
make that edit. Once deliberately activated for inferred sounds, the full authored prefix is accounted for.

The capacity is read like the game's `atoi()`: `64;`, `64.0` and `+64x` all declare 64, and a value without leading
digits declares a dormant table of 0. A carriage return inside a row separates arguments, as it does in the game,
and is retained as a space.

The importer admits at most 65,536 rows, 65,536 bytes per logical row, and 8 MiB of retained row text. Capacities
must be nonnegative and fit the existing signed 32-bit text-world range. These are RED admission limits, not
measured retail resource maxima. An active table may finish at EOF after a complete row. A dormant table can yield
directly to the next section. Write the outer `SECTION: SOUNDS` or `SECTION:SOUNDS` header on one physical line.
Other sections retain the editor's existing section-name grammar.

Some tables the game loads cannot be preserved row for row: duplicate SOUNDS sections, a negative or out-of-range
capacity, unfinished continuations, an active table crossing another section before its terminator, a continued
SOUNDS section header, and a continuation that absorbs a physical section boundary. Opening such a level still
succeeds. RED reports a warning, keeps no authored rows, and rebuilds SOUNDS on export from the sounds the level
uses, as it did before it preserved authored rows. Strict saved-count inspection, dependency inspection and
package planning refuse these tables, because they cannot vouch for the preload set the game would build.

## Native state and format boundaries

The Level owns `jklSoundPreloads`: the capacity base and ordered logical rows. Protected native files carry it
in the optional [`sndp` section](formats/sed_binary_format.md#sndp). Empty state adds no section and retains
legacy file bytes. Known malformed data is rejected by strict Open; permissive corruption recovery reports
loss of `sndp` while retaining independently verified Level data. Older editors may read the optional section
and discard it when resaving.

The required eight native domains and their nested history bytes remain unchanged. A versioned
[`jklSoundPreloadsV1` HIST field](formats/recovery_format.md) carries the current list and explicit historical
overrides, including an empty list. Marker-only, order-only, and capacity changes contribute to authored-state
and history bindings. As with other optional history, an invalid or over-budget timeline can be omitted with
a warning while the current native Level remains available.

Agent semantic previews include the capacity base and each row in both authored and persisted comparisons.
The native format is the editable master for this state. Legacy JED has no corresponding preload representation;
the agent reports the authored row count in its existing loss receipt and requires the reviewed output hash before
publishing that conversion. JK/MotS JKL and native `.sed` are the preservation paths. IJIM and DroidWorks use
their legacy second-word section names, including when VERSION or the DroidWorks copyright signature follows
the geometry. Only JK and MotS recognize the compact `SECTION:SOUNDS` spelling as a preload table. Strict
saved-count inspection continues to validate the width of other section headers.
