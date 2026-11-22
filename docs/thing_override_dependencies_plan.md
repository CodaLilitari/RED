# Placed Thing resource overrides

## Scope

A placed Thing can override a template's resource, for example with `soundclass=alarm.snd`.
Expose that occurrence in dependency inspection and follow it during package preparation, including
when an external JKL omits the corresponding resource table row. A normal RED export also collects
Thing resource values for its tables. The change belongs in the shared JKL root visitor.

## Implementation and acceptance

1. Exercise the public inspector and package planner with synthetic JK and MotS rows. The inspector
   must retain both incoming edges for two instances selecting the same sound class. Reopen the package
   and check that it contains exactly one SND and one child WAV. Remove the WAV and check refusal.
2. Admit only rows inside a declared `World things` table with a numbered prefix, template and instance
   names, six numeric pose fields and an integer sector. Recognize the optional MotS ARCH index.
   Ignore JKL hash comments, empty and `none` values and unrecognized parameter names. Retain authored spelling
   and use the existing template resource-key vocabulary with distinct Thing relations.
3. Feed tokens and references through the existing stop-capable callbacks. Test distinct keys, malformed
   prefixes, table boundaries, repeated occurrences, edge limits and a token-work stop.
4. Document occurrence ordering, limits, and evidence in the dependency and package authoring guides.

The existing request shapes, archive lookup order and public entry points remain applicable.
Edges originate at the level node. They do not identify a Thing by index and do not establish runtime closure.

## Implemented coverage

The shared visitor feeds level-root relations to both existing consumers. The focused
[dependency tests](../src/core/tests/level/dependency_tests.cpp) exercise the graph and reopen a physical
package produced from a Thing-only reference. The [reference coverage](dependency_inspection_design.md#reference-coverage)
defines the admitted keys and lexical framing, including work limits and reused destinations.
