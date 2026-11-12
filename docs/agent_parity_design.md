# Agent parity design

RED agents need to discover the exact request accepted by the current executable, make a bounded edit, and read back
the saved result. This design closes a first group of gaps against the older .NET SED-MotS agent while preserving the
portable C++ model and the existing RED protocol. The [capability matrix](capability_matrix.md) remains the broader
editor comparison; an exposed command is structural capability, not visual or game-runtime acceptance.

## Shared discovery

The operation registry in `tools/sed-agent/agent_schema.cpp` supplies the compatible 63-operation inventory and focused
Draft 2020-12 schemas. The same module supplies 41 command descriptors, including positional usage. CLI help and
discovery use that registry, and MCP embeds its patch schema rather than maintaining another mutation grammar.

`schema commands`, `schema patch` and `schema <operation>` work without a level. The command schema validates a
command name and publishes usage metadata; domain-specific command requests retain their existing contracts.
Unknown topics and extra topic arguments are usage errors. An agent should follow the executable's discovery result
instead of transferring request spellings from another editor.

Compatibility is deliberate. Existing optional source hashes, request fields and family-specific permissiveness remain
accepted. The six added operations reject unknown fields. JSON Schema expresses types, alternatives and local bounds;
runtime validation owns source identity, current indices, cross-field arithmetic, geometry, resource hashes and model
compatibility. These distinctions are visible in `x-runtimeConstraints`.

## Checked authoring

| Operation family | Shared behavior | Preservation boundary |
| --- | --- | --- |
| Absolute vertex position | Follow exact reciprocal reverse-cyclic portal copies and validate all affected closed convex sectors. | Unconnected coincident vertices stay independent; IDs, corner order, references, UVs and light channels remain intact. |
| Fit and align textures | Fit explicit pixel dimensions or continue a representable affine mapping across one exact shared edge. | Preserve target light channels, materials and unrelated flags; refuse unsupported UV space or geometry. |
| MotS ARCH bind and reset | Resolve hash-bound model resources, check all LODs and append private sample storage for one selected owner. | Other owners retain samples and bindings; reset requires explicit policy. |
| MotS ARCH bake | Evaluate explicit point-source or world-occluded recipes in model hierarchy and Thing space. | Retain source mesh addressing and provenance; bounded work; no retail-baker equivalence claim. |

Patch execution edits an isolated Level, prepares all output and rechecks captured sources before create-only
publication. A later failure discards the candidate. This is the existing headless transaction model; it does not add
an interactive editor session or a multi-file transaction. Native opaque workspace/history rules still apply.

The ARCH v1 bake-input digest remains compatible with the shared authoring service and omits world blocker geometry.
Plan/apply source hashes bind the level being edited, and world changes require a new plan and a rebake. The digest
alone must not be used as proof that occlusion remains current.

## Readback as evidence

Six richer collections expose stored sectors, template declarations, placed COG records, ARCH objects and samples,
and point containment. Pagination happens before expensive row construction. Duplicate parameters and their order
remain visible. Mono/RGB values, explicit zero and inherited/null values stay distinct.

Queries describe their limits: COG bindings and ARCH model compatibility remain unverified until the corresponding
resource-aware command runs. Containment distinguishes inside, boundary, outside, ambiguous and unknown; it does not
certify collision-volume clearance or gameplay. Source hashes bind every page to the document being examined.

Output and work admission cover nested records, authored text, sample hashing, owner scans, filtering and containment.
The [agent design guide](agent_design.md#read-back-the-edited-records) owns exact limits and refusal semantics.

## MCP transport

The 39 named MCP tools cover discovery, editing, readback, archives, packages, rendering, COGs, prefabs, DXF, MAT, model
imports/lighting, KEY files, launch planning, native inspection and outcome recovery. Each wrapper translates explicit
arguments into the existing agent command. Domain logic remains in the agent/core implementation.

Read paths are admitted under the workspace or explicit resource roots; write paths remain under the workspace.
Command-aware normalization covers nested resource captures and outputs without rewriting ordinary level strings.
Lighting additionally checks the actual resolved loose MAT/CMP file before reading it and during publication recheck.
Unused descendants of a resource directory do not require recursive scanning. These are application-level admission
checks, not an OS sandbox or a filesystem lock.

Publication journals fingerprint the caller's original arguments. Normalized staged paths are an execution detail and
cannot silently change replay identity. File-producing wrappers use the existing started/completed/failed outcome
protocol, including snapshots and an overview with an output file. Camera-only overview remains read-only.

## Deliberate remaining differences

This pass does not claim byte-for-byte .NET protocol compatibility, mandatory legacy envelopes, uniform rejection of
all historical fields or every old array alias. It does not close unknown-record/text-SED migration, dependency-complete
prefabs, atomic multi-file authoring, a COG compiler, richer model/sprite snapshots, interactive GUI automation or a
causal game acceptance runner. Those remain named work in the [gap register](gap_register.md).

The highest-value follow-on agent work is stronger observation and verification: richer render evidence, reviewable
semantic diffs, explicit resource closure and controlled game scenarios. Broadening authoring should keep the same
source bindings, failure-atomic publication and readable refusal contracts.

## Verification

Focused tests exercise actual domain commands and synthetic assets: schema contracts, texture/vertex operations,
ARCH authoring, rich queries, MCP routes and journals. Independent Draft 2020-12 validation checks emitted schemas and
request fixtures. The [review record](reviews/agent_parity.md) records the exact build/test evidence and unresolved
platform gates. Windows/Qt/native rendering and real games require their own acceptance evidence.
