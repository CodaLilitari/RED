# KEY animation editing contract

The portable codec in `src/core/src/assets/key.cpp` supports the classic JK/MotS KEY grammar used by
`rdKeyframe_LoadEntry` in OpenJKDF2's `src/Engine/rdKeyframe.c`. KEY animation tracks are separate from
Thing path-mover frames. They do not change a level's Thing trajectory when edited.

## Retained data and output grammar

The writer emits the ordered header fields `FLAGS`, `TYPE`, `FRAMES`, `FPS`, and `JOINTS`, followed by an optional
`MARKERS` section and the `KEYFRAME NODES` section. Markers must precede keyframe nodes because the engine reads
these sections in sequence. Synthetic `END` separators are not emitted.

| Field | Contract |
|---|---|
| `FLAGS` | Stored as 32 bits; emitted as signed decimal, matching the engine reader's `%d`. Historical `0x` input is also accepted. |
| `TYPE`, entry flags | Retained as 32 bits and emitted in hexadecimal. |
| `FRAMES` | Nonnegative integer frame count. |
| `FPS` | Positive finite floating-point rate, including fractional rates. |
| `JOINTS` | Retained declared skeleton size, including joints without animation tracks. |
| `NODE` | Unique nonnegative index into the `JOINTS` table, retained independently of vector position. |
| `MARKERS` | At most eight ordered events, each with a fractional frame position and an integer event ID. |
| Entry | Fractional frame position, flags, position, rotation, and both derivative vectors. |

Every emitted entry begins with its sequential numeric label and a colon (`0:`, `1:`, and so on). Labels do not
identify skeleton nodes. A sparse animation with `JOINTS 12`, `NODE 3`, and `NODE 9` keeps those node identities
after editing; it is not rewritten as nodes zero and one.

Numeric text uses locale-independent round-trip spelling for the authored floating-point value. Values must be
finite and within the engine's single-precision range. Entry and marker positions are nonnegative and cannot exceed
a positive declared frame count. Mesh names must be nonempty, shorter than the engine's 32-byte buffer, and contain
no whitespace, hash comment delimiter, or NUL. Comments, whitespace, entry-label spelling, and an empty optional
marker section are not retained as textual formatting.

Newly authored `KeyNode` objects may leave `nodeIndex` at `-1` to use their vector position. A newly authored
`KeyFile` may leave `joints` at `-1` to derive the required count from its nodes. Imported files retain explicit
values. Callers addressing the editor's node vector must distinguish its position from the stored skeleton index.

## Preview recovery and editing refusal

Both stream and path readers admit at most 128 MiB of source bytes, 64 KiB per physical line (excluding LF), and
1,048,576 physical lines. CR bytes, comments and blank lines count toward these operational bounds, and an
unterminated final line counts as one line. A trailing LF does not create an additional empty line. Byte admission
consumes at most the byte limit plus one excess probe byte; line bounds are checked on that bounded capture before
the grammar parser retains section records. An exceeded bound or stream failure throws instead of returning a
partial animation. These are editor resource limits, not limits claimed for the engine format.

The reader can recover modeled fields for preview from incomplete or extended input. The returned
`serializationSafe` flag is false when required fields are absent, known records are malformed or truncated,
counts or references are invalid, duplicate fields or nodes occur, or an unknown field or section would otherwise
be discarded. Recovered defaults are not permission to overwrite the source.

Timeline mutations refuse an unsafe import. Both serialization entry points throw an actionable error for it,
and path saving validates the complete output before creating a staging file. Canonical bytes are written to an
owned sibling, flushed and closed before atomic replacement. Serialization, write, flush, close and publication
failures retain the previous complete destination; failure cleanup removes only this writer's staging file.
Path saves write the same bytes as `saveToKEYText`, without platform newline translation.

Supported imports preserve the modeled animation semantics through parse, edit, and write. Unknown extensions
must receive an explicit preservation or migration contract before editing can be enabled for them; copying an
unknown record verbatim is not sufficient when it may refer to edited entries or nodes.

The original synthetic fixtures in `src/core/tests/assets/key_tests.cpp` exercise sparse nodes, marker ordering,
fractional timing, decimal flags, semantic round trips, malformed-input refusal, and destination preservation.
Codec conformance does not substitute for playback validation of a particular animation and model in its target
game engine.
