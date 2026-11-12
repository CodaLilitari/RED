# Explicit offscreen capture verification

The standalone capture runner adds an explicit Linux `sdl-offscreen` backend.
Xvfb remains the default. The companion OpenJKDF2-MotS-Fixes change uses SDL3's
offscreen/EGL driver, checks the initialized driver/current context and writes
bounded observed graphics metadata. The four-line capture-v1 wire is unchanged.
See the [contract and plan](../engine_capture_offscreen_plan.md).

## Verified scope

- The runner's 39 focused tests pass, including real staged child environment,
  process-group, timeout and no-fallback cases. The configured
  `sed_engine_capture_runner` CTest entry passes. The Windows profile clears
  inherited Linux backend intent without changing its parent environment.
- Nine engine capture groups pass with ASan and UBSan, plus the expected unsafe
  UI negative control. LeakSanitizer is unavailable under this environment's
  tracing and was explicitly disabled; address/undefined checks remain enabled.
- The normal Linux Release engine and its relevant dependencies build with
  GNU C/C++ 14.2.0. A final incremental build includes the reviewed loader fix.
  Final executable SHA256:
  `027e7fadf08420a1838d2ad3084dbc2efc0beafc9acbca4a59fe67c2dd30c843`.
- The shared production graphics boundary passes a real SDL3 3.4.12 offscreen
  test with Mesa llvmpipe and OpenGL 4.5. It compiles GLSL 3.30, verifies all
  4096 rendered RGB pixels, checks top-first PPM orientation and writes the
  production graphics receipt. The final run uses the exact SDL and GLEW static
  libraries linked into the built engine.
- Removing the draw, removing the `glCreateShader` loader lookup, and creating
  an actual OpenGL 3.2 context each fail their intended assertion. RED accepts
  the successful real test's PPM/sidecar through its final offscreen validator.

## Review corrections

Independent review reproduced a loader gap with real contexts: GLEW's 3.3 flag
can remain true when a required earlier shader entry point is missing or the
actual context is only 3.2. The correction checks the actual context version,
the required earlier core groups, and the two older texture/blending functions.
Both reproductions became persistent negative controls. The capture-only
backend environment variable is also cleared before profile selection so a
Windows child cannot inherit a Linux backend request.

## Remaining acceptance

These results establish source compilation, runner behavior and the tested
graphics boundary. They do not launch an owned map, observe the effective game
camera/resources, certify deterministic simulation, run the native Windows or
Qt interface, or prove retail/editor parity. `acceptance` remains `unverified`;
both camera/resource observation flags remain false. The earlier complete
143-test headless gate belongs to the preceding test-isolation checkpoint;
this pass does not relabel it as a fresh whole-editor run.

The Linux executable uses system graphics/audio/UI/runtime dependencies; a
single executable hash does not bind every dynamically loaded library. This
work also does not add enforcement of the runner's recorded overall deadline
and log-size limits.
