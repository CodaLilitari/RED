# Explicit SDL offscreen capture

## Goal and scope

Allow the standalone engine-capture runner to request SDL3's offscreen desktop
OpenGL backend without a display server. Keep omitted backend selection on the
existing private-Xvfb path. Preserve the capture-v1 wire format, staged input
identities, isolated profile, process timeout and unverified acceptance status.

This work is authorized as the next capture improvement. It does not add a
gameplay controller, change retail mod formats, or certify an effective camera,
runtime resource resolution, deterministic simulation or editor/engine parity.

## Contract

- Request JSON accepts optional `graphicsBackend`: exactly `xvfb` or
  `sdl-offscreen`, for the Linux engine profile only. Omission retains the
  existing normalized request and selects `xvfb` on Linux.
- Offscreen skips display-server discovery and creation, removes inherited
  display addresses from the child environment, selects SDL `offscreen`, and
  requests Mesa software rendering. SDL3 and legacy SDL2 environment names are
  set consistently; only the child environment changes.
- The engine receives `OPENJKDF2_CAPTURE_BACKEND` separately from the unchanged
  four-line capture request. Its capture parser accepts only `native`, `xvfb`
  and `sdl-offscreen`; the two Linux modes are rejected on other platforms.
  Omission selects `native` for direct legacy engine invocation.
- During capture the engine selects the requested SDL driver before video
  initialization and checks the initialized driver. It records graphics only
  after a context has successfully become current. A backend, initialization,
  current-context or observation failure exits unsuccessfully, without trying
  another video driver.
- The engine sidecar adds `graphics` with `schemaVersion: 1`,
  `requestedBackend`, `sdlVideoDriver`, `sdlVersion`, `glVendor`, `glRenderer`,
  and `glVersion`. Strings are nonempty bounded printable ASCII, with JSON
  escaping. SDL version uses `major.minor.micro`; driver is at most 31 bytes,
  version at most 31 bytes, and each GL string at most 255 bytes.
- Offscreen completion requires that report, with requested backend
  `sdl-offscreen` and observed driver `offscreen`. A valid PPM alone is
  insufficient. The launcher copies accepted observations as
  `engineReportedGraphics`; these remain engine reports, not independent
  renderer attestation. Legacy Xvfb and Windows sidecars remain optional.
- Reports remain at most 16 KiB. Acceptance stays `unverified`, with both
  effective camera and runtime resource resolution observations false.

## Implementation sequence

1. [x] Pin RED and capture-engine parents; inspect SDL, capture and build paths.
2. [x] Record design and execution plan without adding another approval round.
3. [x] Add failing runner contract/environment/receipt tests; implement selection.
4. [x] Add failing engine backend and graphics-observation tests; implement the
   production SDL boundary and receipt writer.
5. [x] Run real SDL offscreen GL drawing/readback if a verified SDL runtime can
   be obtained; keep graphics-only evidence distinct from full game captures.
6. [x] Run scoped regressions, obtain independent review, and resolve findings.
7. [x] Publish separate draft changes on pinned real remote parents, verify
   published contents, and save the evidence and continuation checkpoint.

## Verification and known limits

The runner's existing 23 synthetic tests pass at the pinned starting source.
They require no Xvfb. New tests must establish no offscreen display startup, a
real staged child with the correct isolated environment, exact driver receipt
requirements, and no fallback on failures. Existing private-display lifecycle,
image validation and input-preservation checks remain relevant.

Engine tests must cover ordinary-launch behavior, invalid backend intent,
observed-driver mismatch, initialization/current-context failures, null or
oversized graphics strings, receipt JSON escaping and stale-report rejection.
The production SDL boundary should be shared with a small asset-free real-GL
test. That test proves only the tested context/draw/readback path.

The pinned engine source was completely recovered through connected repository
access and exact matching local Git objects after direct checkout's network
approval was cancelled. A verified existing SDL3 3.4.12 dependency was copied
privately for a real offscreen test: shared production initialization, context
observation and loader policy, GLSL 3.30 drawing, all 4,096 RGB pixels, PPM row
orientation and receipt writing passed. Removing the draw failed the expected
pixel assertion. This evidence does not launch the game or validate its camera.
Full engine compilation and owned-map acceptance are separate gates.
The full normal Linux Release engine now builds, including the reviewed loader
fix. The [verification record](reviews/engine_capture_offscreen.md) records the
39 runner tests, nine sanitized engine groups and real graphics controls.
Owned-map/camera/resource acceptance remains unverified.
The [runner](../tools/engine-capture/README.md) enforces `runnerTimeoutMs`
across admitted staging, execution, capture validation, and post-run identity
collection through an owned worker process. JSON/schema/source-metadata
admission and small final supervisor receipt writes are outside that work
budget; process termination has a separate two-second grace. Timeout receipts
mark identity checks incomplete instead of rehashing all inputs after expiry.
`logLimitBytes` bounds the aggregate retained engine stdout and stderr while
both pipes continue draining. These process controls are separate from real
engine rendering and gameplay acceptance.
