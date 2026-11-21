# Episode release comparison

A designer comparing two releases needs the installed episode filename and the exact resources that changed.
Accept a direct JK/MotS v0x14 archive or a single-episode ZIP on each side. Compare canonical member names,
byte lengths and SHA-256 digests. Report the inner archive name and byte hash independently of the resource
comparison. Keep the specialized texture-edition verifier available with its existing rules.

1. Exercise the command on owned binary fixtures before implementing it. Compare one changed COG, additions
   and removals, reordered archives, renamed downloads, ZIP wrappers and malformed inputs.
2. Reuse the existing GOB directory parser. Admit bounded regular files and classic single-disk ZIP directories,
   stream the chosen archive into temporary storage, validate the complete index, then hash member ranges.
   Inspect ZIP member names without opening unrelated payloads. Bound both memory and total work.
3. Return deterministic JSON. Exit 0 for equal payload inventories and the same installed basename, including
   a repack. Exit 1 for a changed inventory, changed payload or renamed installed archive. Exit 2 for usage
   or input errors without a partial comparison.
4. Include the command and its reader in the release layouts. Run the engine-capture and packaging suites and
   exercise packaged paths on synthetic inputs. The ordinary full Release CTest gate remains a separate check.

The comparison covers the inner episode only. ZIP packaging, companion files, asset semantics, dependency
closure and runtime behavior require separate checks. Test admission boundaries and refuse ambiguous names or
overlapping nonempty payloads before producing a result.
