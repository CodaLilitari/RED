# Bake output limits and diagnostics

The agent `bakeLighting` operation accepts optional `lightFloor` and `lightCap`
fields. These change stored surface-corner RGB after the existing bake, not the
runtime renderer or the bake's source lights.

## Policy

For JKDF2, `baked` is the double-precision mean of fresh baked RGB. The scalar
result is stored in all RGB channels and in the corner's `a` field, which JK
VERSION 1 JKL uses as its exported mono intensity. The old `a` value is not an
input, even when it is stale or nonfinite. For MotS and IJIM, the formula applies
independently to each fresh RGB component and alpha is preserved:

```text
final = min(lightCap, lightFloor + baked)
```

Values must be finite and satisfy `0 <= lightFloor <= lightCap <= 1`. When
either field is supplied, the omitted field defaults to `lightFloor = 0` or
`lightCap = 1`. When both are omitted, the policy is bypassed and legacy baked
output bytes are preserved; adding the diagnostic report does not change the
level's bytes. Explicitly supplying the defaults still opts into the policy.
Explicit limits are supported only for JKDF2, MotS and IJIM. DroidWorks limits
are refused; its default bake without these options is unchanged.

The existing bake clears old corner RGB before calculating new contributions.
The policy uses that fresh result, not the previous policy output, so repeating
the bake does not accumulate the floor. Floor is an additive baseline, not a
lower clamp: a finite negative input can remain below `lightFloor`.

Arithmetic uses double precision before storing float components. The stored
representation of a decimal cap may lie slightly above or below the requested
double value. This is not a mathematical guarantee that the stored float is
less than or equal to the exact decimal cap.

The postpass preserves MotS/IJIM alpha, geometry and sector ambient. For JKDF2 it
synchronizes the exported mono field as described above. The existing bake
has already performed its own ambient derivation; the postpass does not repeat
it or undo it. Surface/sector extra light, materials, runtime lights, tint and
rendering modes still affect the displayed result. Neither the floor nor the
cap guarantees an in-game brightness bound, fixes missing geometry or detects
rendered black holes.

Nonfinite stored RGB is refused before the postpass mutates any corner. A
failed patch does not publish a partially modified destination.

The existing `intensity` and `range` fields configure FULLYLIT material-emission
resolution. They are not global multipliers for authored point lights. Point
lights retain their own intensity and range; bounce and sky controls retain
their separate behavior. See [lighting](lighting.md).

## Patch example

The following is a patch template. Replace both hash placeholders with the
values obtained for the source and choose resource roots containing the intended
MAT/CMP resources.

```json
{
  "schemaVersion": 1,
  "inputSha256": "<source SHA-256>",
  "sourceRevision": "<source revision>",
  "operations": [
    {
      "op": "bakeLighting",
      "resourceRoots": ["materials"],
      "intensity": 0.5,
      "range": 6,
      "lightFloor": 0.25,
      "lightCap": 0.8
    }
  ]
}
```

Run `red-agent plan source.jkl patch.json`, review the reports and proposed
changes, and retain the returned `outputSha256`. Then run
`red-agent apply source.jkl patch.json next.jkl <outputSha256>` with that exact
reviewed hash and a new destination. Apply is create-only: it does not overwrite
the source or an existing destination. Supply the source hash and revision to
detect stale inputs. See [agent workflow](agent_workflow.md) for resource
consistency and publication checks.

## Reports

Plan and apply results include `bakeLightingReports`, one entry for each bake
operation. Each report is captured immediately after that operation's bake and
optional policy. Later patch operations and serialization may change the final
representation; these are not reports of the serialized output or engine frame.

Each entry contains:

- `operation`: zero-based patch-operation index.
- `scope`: always `stored-corner-rgb`.
- `policyApplied`, `lightFloor`, `lightCap`: whether the postpass ran and the
  effective policy values. With no policy, the report uses floor zero and cap one.
- `corners`: number of stored texture-vertex corner records inspected.
- `minimum`, `maximum`: extrema across all stored RGB components; both zero
  for an empty level. Alpha does not participate.
- `nearBlackCorners`: corners whose **all** RGB components are at most
  `min(0.01, lightCap)`.
- `cappedCorners`: corners with **any** component at or above the float
  representation of `lightCap`. This matches the quantized stored cap, including
  downward-rounded values such as 0.7. It does not prove that clipping occurred.
- `overUnitCorners`: corners with **any** component strictly above 1.
- `diagnostics`: detail rows for corners matching any of those three conditions.
- `diagnosticsTruncated`: true when matching detail rows were omitted.

Counts and extrema cover every corner even when details are truncated. At most
256 detail rows are retained across the entire patch, shared by all bake
operations. Earlier operations consume that budget first. A corner matching
several conditions produces one row. Rows follow sector, surface and corner
storage order.

Each detail row contains zero-based `sector`, `surface` (within that sector)
and `corner` indices, plus the model's `sectorId` and `surfaceNum`. The latter
are retained model identifiers, not inferred screenshot pixel mappings. Boolean
fields `nearBlack`, `atOrAboveCap` and `overUnit` identify the matching conditions.

For example, a matching corner may be represented as:

```json
{
  "sector": 0,
  "sectorId": 42,
  "surface": 1,
  "surfaceNum": 77,
  "corner": 2,
  "nearBlack": false,
  "atOrAboveCap": true,
  "overUnit": false
}
```

These numeric diagnostics are advisory. An over-unit stored component is not
proof of visible saturation, and a near-black corner is not proof of a missing
face. Actual visual acceptance requires separately identified engine captures
and appropriate runtime testing.
