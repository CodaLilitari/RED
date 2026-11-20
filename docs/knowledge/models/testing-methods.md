# Testing models in original JK/MotS and OpenJKDF2/OpenMotS

> Copied into RED from [CodaLilitari/JK-MotS-Model-Workflow@ca011b8](https://github.com/CodaLilitari/JK-MotS-Model-Workflow/blob/ca011b84c4fa5e1f3755e1543be2c1c1d25063c9/skills/jk-mots-model-workflow/references/testing-methods.md) (MIT; see [LICENSE](LICENSE.md)). Regenerate with `tools/knowledge/import_knowledge.py` rather than editing this copy.

Choose the test that answers the next practical question. A new skin usually needs a quick source check, a controlled appearance check, and ordinary character selection/gameplay. A rig repair benefits from numerical animation tests before visual comparison. A claim about multiplayer appearance needs another connected client that actually sees the model.

This reference covers character, creature, player-skin, and weapon-model testing. Existing stock content or a private diagnostic copy supplies the scene; creating levels is outside the workflow.

## Pick a route

| Question | Recommended method | What it establishes | Main tradeoff |
| --- | --- | --- | --- |
| Are the exported resources internally consistent? | Native parsers, contract checks, decoded MAT inspection | Counts, indices, hierarchy, KEY allocation, materials, LOD structure | Fast and repeatable; no rendered or gameplay conclusion |
| Does the intended rig respond to the actual animation tracks? | Source-port headless animation harness | KEY starts, advancing time, finite node matrices, motion/attachment relationships | Efficient corpus coverage; no texture or silhouette judgment |
| Does original retail display the native assets? | Original executable in an isolated Wine/Xvfb case, or a separate native Windows installation | Original game logic and renderer output on the recorded compatibility stack | Wine/Xvfb can remain off the desktop; native Windows is needed for its driver/window behavior |
| Does an enhanced texture/effect look right? | OpenMotS/OpenJKDF2 hidden graphical `-renderprobe` | Actual GL output, effective settings, material-upload evidence and screenshots | Requires a build with the feature; not the same as `-headless` |
| Do I need a fixed engine camera for repeatable close-ups? | The maintained source-port `-capture` path | Explicit camera, lossless image and actual world/player-resource metadata | Does not freeze simulation; first-person body visibility still applies |
| Can the player choose and use this character normally? | Load Character → preview → save/select → hosted game through ordinary menus | Registration, preview, profile, selected player and sampled actions | Slower than autostart; catches integration failures hidden by substitutions |
| Do other players see the intended model and animation? | Two separately running clients, with local and remote views | Connection plus observed replication, attachments and transitions | More setup; a scoreboard alone proves neither appearance nor animation |

For a first retail release, start with indexed materials and the original software renderer to obtain a useful baseline, then test RGB565 with real 3D acceleration. For an enhanced-only release, start with the source port's normal material settings and add effects after the model looks right. Neither choice requires spending time on every possible renderer before the first useful result.

## Three practical test plans

**New MP humanoid using stock animation.** Parse every LOD, resolve the intended KEY/PUP set, and check decorative node reservations. Render a neutral/idle view and a moving view. Exercise the real selector, enter a hosted game, then check movement, crouch, jump, aim, representative weapon holds, hit/death/respawn, and near/far LODs. Add a second client when claiming remote visibility. Preserve first-person stock hands as an explicit option if custom view models are outside scope.

**Mechanical joint or hand repair.** Keep materials, model hierarchy, animation resources, and scene fixed while changing the relevant geometry. Check the actual range of joint transforms numerically, then compare matched views. For a hand, verify the selected weapon from pixels before judging contact or concealment. Survey firing and alternate fire separately from passive holds. The stock idle camera is useful for intact-retail third-person views; it is not a deterministic turntable.

**Texture or high-detail upgrade.** Keep model, pose, camera, lighting, and output size fixed. Compare native indexed MAT, native RGB565 MAT, and replacement PNG as separate cases where supported. Check mips/distance, filtering, seams, thin hair, and the darkest/lightest regions before adding bloom or SSAO. Confirm that the renderer loaded and used the intended path; a high-resolution PNG existing on disk proves little by itself.

## Prepare a reproducible case

Create a new writable case directory with copied, owned game resources, its own profile, model overlay, logs, and output directory. Retain the baseline separately. Record the executable hash as well as its displayed version: original JK 1.00, official JK 1.01, a digital-store executable, and a patched executable are different inputs. For a source port, record source commit, build options, executable hash and dependency build identity.

Keep these fields with every result:

```json
{
  "game": "MotS",
  "engine": "original executable or exact source-port build",
  "executable_sha256": "...",
  "model_sha256": "...",
  "material_profile": "indexed-mots | rgb565 | replacement-png",
  "animation_manifest_sha256": "...",
  "entry_route": "normal-menu | autostart | diagnostic-substitution",
  "environment": "Windows or Linux/WSL + Wine version",
  "render_path": "game software | game Direct3D | source-port OpenGL",
  "render_backend": "physical GPU and driver, or Mesa llvmpipe",
  "display_settings": "actual effective values and menu capture",
  "controls_and_observations": [],
  "captures": [],
  "stop_reason": "normal-quit | planned-stop | deadline | crash",
  "limitations": []
}
```

Use fresh output names so an old screenshot cannot become apparent evidence for a failed new run. Hash source and staged files before and after. Capture the actual scene-ready state and identity of the loaded model. A diagnostic `SetThingModel` is useful for showing a specimen, but it bypasses the very selection route a selectable-skin test must exercise. A read-only `GetThingModel` check can verify normal selection without changing it.

## What “headless” means here

There are three distinct methods:

1. **No graphical rendering:** source-port `-headless` runs loading/simulation without the ordinary GL window/context. This is useful for KEY/PUP and numerical tests. It cannot validate UV appearance, missing textures, transparency, shaders, or screen capture.
2. **Off-desktop graphical retail:** the original 32-bit game runs normally under Wine, with an Xvfb virtual display. It still has menus, windows and drawing. A controller can inspect and operate that private display without using the Windows desktop.
3. **Hidden native graphical source port:** `-renderprobe` or `-capture` keeps an actual GL context while suppressing its engine's window/input paths. These are opt-in features of the maintained fork, not switches that can be passed to an original retail executable or assumed in every upstream release.

The [OpenMotS render/capture integration](https://gitlab.com/codalilitari/OpenJKDF2-MotS-Fixes/-/blob/main/docs/RENDER_CAPTURE_INTEGRATION.md) documents their combined behavior; the [GitHub copy](https://github.com/CodaLilitari/OpenJKDF2-MotS-Fixes/blob/main/docs/RENDER_CAPTURE_INTEGRATION.md) is an alternative source. Original retail `-windowgui` is not a headless switch and does not guarantee a non-focusable game window.

### Use a numerical animation harness well

Load the exact model, resolve its reachable KEYs, start each relevant action, and sample more than the initial frame. Record start result, track IDs, advancing time, and finite transforms. Compare a rigid detail node to its intended parent's transform after accounting for the detail's neutral local transform. Remove global/root motion when asking whether a limb actually articulates; movement through the world can otherwise hide a rigid body branch.

A useful harness has a negative control: intentionally attach synthetic detail inside a known animation-addressed slot and require the transform check to fail. This isolates the rule more effectively than comparing two production models that also differ in geometry. TDIR Broly supplied the practical counterexample: stock KEYs started successfully while its visible large-body branch remained rigid. The maintained [KEY-domain guard](https://gitlab.com/codalilitari/OpenJKDF2-MotS-Fixes/-/tree/main/scripts/model-workarounds/key-domain-guard) preserves the reservation method and synthetic cases; the workflow's small `inspect_contract.py` only checks its documented subset.

Parser success and a few surviving animation starts do not establish allocation safety across all reachable KEYs. Preserve the complete original declared KEY domain when adding detail, then ensure applicable KEY storage covers the complete exported hierarchy. See [compatibility](compatibility.md).

## Linux on Windows: choose WSLg or a private X display

WSL provides the Linux environment; Wine runs a Windows executable inside it. A native Linux build of OpenJKDF2 does not need Wine. A Windows x64 source-port binary needs a suitable 64-bit/WoW64 Wine environment, while the retained original-retail tests used a 32-bit Wine 8 environment. Match the runtime to the executable rather than treating every Wine prefix as interchangeable.

WSLg is convenient for interactive Linux GUI applications integrated into the Windows desktop. It can use a vGPU with a supported driver, but it is not a hidden-test guarantee. A private Xvfb display is the appropriate option when the requirement is to keep menus, input and captures off that desktop. [Microsoft WSL GUI documentation](https://learn.microsoft.com/en-us/windows/wsl/tutorials/gui-apps), [X.Org Xvfb manual](https://xorg.freedesktop.org/archive/X11R7.0/doc/html/Xvfb.1.html).

Inventory first, without updating or restarting a working environment:

```powershell
wsl.exe --status
wsl.exe --list --verbose
wsl.exe -d <DistroName> -- sh -lc 'uname -m; wine --version; command -v Xvfb; command -v xdotool; command -v xwd'
```

The retained retail environment was Debian 12 i386 with Wine 8, hosted inside WSL Ubuntu. It used `wine32`, Xvfb, xdotool, X11 utilities, and Mesa llvmpipe. A private Debian i386 chroot was chosen to obtain that known combination without replacing the host distro. This is a reproducible option when the installed Wine architecture is unsuitable, not a mandatory WSL layout. Keep the package manifest and version output; a newer Wine/WoW64 route is a new test environment.

Use a fresh prefix per case or copy a known-good prefix only after its Wine processes have stopped. `WINEPREFIX` separates registry/configuration/server state. Prefix architecture is selected at creation; do not point `WINEARCH=win32` at an existing 64-bit prefix and expect conversion. Check the installed Wine build's supported architecture modes. [Wine environment reference](https://github.com/wine-mirror/wine/blob/master/tools/wine/wine.man.in).

### Portable retail launch pattern

The following Linux shell example illustrates the recorded original-JK route with portable placeholders. It assumes the copied game, a compatible dedicated prefix, and required media/audio/dependencies have already been prepared. Replace the paths and executable with the actual case. It is a launcher, not an automatic model test: use a controller within the same private display for menu input and timed capture.

```bash
case_root='/absolute/new-case'
game_dir="$case_root/game"
prefix="$case_root/wine-prefix"
mkdir -p "$case_root/framebuffer"
cd "$game_dir"

xvfb-run --auto-servernum \
  --server-args="-screen 0 1280x960x24 -nolisten tcp -fbdir $case_root/framebuffer" \
  env -u WAYLAND_DISPLAY \
  WINEPREFIX="$prefix" WINEARCH=win32 \
  LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe \
  WINEDEBUG=err+all,warn+module \
  timeout --signal=TERM --kill-after=10s 180s \
  wine ./jk.exe -windowgui -devmode -debug log -verbose 2 -path modeltest
```

Use `jkm.exe` for an actual original MotS case; original MotS does not need the source-port `-motsCompat` switch. Use a simple local path without spaces for this particular `--server-args` example, or construct the Xvfb argv directly in a runner. Preserve the launcher's exit status. A timeout is an intentional bounded stop, not a normal game exit.

For a production runner, reproduce the useful parts of the retained scripts:

- Start a new private display and record its number/PID; never inherit WSLg's display accidentally. All `xdotool`, window queries and captures use that display explicitly. The `xvfb-run` wrapper can provide X authentication automatically.
- Capture only its virtual framebuffer or the selected game window. `-fbdir` exposes `Xvfb_screen0` in XWD format; copy it at each named checkpoint while the display is running. Decode using the XWD header, pixel masks, byte order and row stride, rather than assuming every file is the historical BGRX layout.
- Inspect the current private frame before choosing controls. Do not treat blindly replayed screen coordinates as proof that a menu, weapon or renderer selection succeeded.
- Add an outer deadline, release held keys/buttons, stop only this prefix's Wine server and the display process the runner created, and await completion. Do not use a host-wide Wine kill or remove another X server's lock file.
- Recheck inputs and save the exact action queue, logs, captures and stop reason. If startup fails, preserve the failed case and create a fresh rerun after the fix.

A 24-bit Xvfb desktop does not mean the game's software renderer is true color. The original game can render its 8-bit paletted world into that display. Conversely, the “3D acceleration” checkbox means the game uses its Direct3D API path; when WineD3D runs on llvmpipe, the physical CPU still performs the rendering. Record both levels. `direct rendering: Yes` in a GLX report alone is not evidence of a physical GPU.

### Resolve environment failures before blaming the model

First run stock Kyle or a known working creature with the same executable/settings. If the stock control cannot reach a scene, fix the environment before changing model geometry.

| Failure | Investigate | Recorded lesson |
| --- | --- | --- |
| Original MotS stops at first-run/media checks | Owned installation/media, drive mapping and actual CD-ROM drive type | Media acceptance and model compatibility are separate. Use owned sources; do not invent a verification marker or present a patched executable as unchanged retail |
| Original game stops because DirectSound has no device | Wine audio backend and availability of an audio endpoint | A private PulseAudio null sink let the retained MotS test proceed; this did not test audible sound or alter Windows audio |
| Host Error / cannot connect to network service | DirectPlay provider and module-load diagnostics | Wine 8 builtin DirectPlay returned `DPERR_UNSUPPORTED` before a model test began |
| Native DirectPlay libraries register, but hosting stalls | Actual server helper process and loaded DLLs | The retained JK host needed a native `dplaysvr.exe` as well as `dplayx.dll` and `dpwsockx.dll` |
| Join dialog is missing with newer Windows components | Embedded dialog resources versus external language MUI resources | Wine 8 found the modern provider DLL but did not resolve its external MUI dialog; an older official provider with an embedded dialog completed the recorded join |
| Only an old screenshot is visible | Capture freshness, display identity, crashed helper, process state | A stale-frame MotS attempt was excluded and rerun without the interfering console helper |

For a fresh Wine prefix that needs legacy DirectPlay, the maintained [Winetricks `directplay` recipe](https://github.com/Winetricks/winetricks/blob/master/src/winetricks) supplies a documented native-component route. Inspect the recipe/version and its source checksums; run it only in the intended prefix. Registration is not a hosting or joining result. Avoid distributing Microsoft components with a model package or copying arbitrary system DLLs into shared Wine installations. The earlier manual staging needed atomic replacement because Wine builtin paths can be symlinks.

The [publisher's MotS installation instructions](https://support.starwars.com/hc/en-us/articles/360000721246-How-do-I-install-Jedi-Knight-Mysteries-of-the-Sith) explain the original media prerequisite. A separate no-CD or executable-compatibility patch, if used, belongs in the test identity; it does not solve a 3DO, KEY, or texture problem.

## Use the stock idle camera for original-retail model views

This method is practical when custom camera code would weaken an original-engine comparison. In the observed MotS runs the normal idle camera began after roughly 30 seconds without recognized input, then orbited the player. The corresponding source-port logic uses a 30,000 ms threshold and an 8-degree-per-second orbit. Treat those values as the studied behavior, not a universal timer for every mod: flags, death, scope and other camera modes can prevent it. [Input idle logic](https://gitlab.com/codalilitari/OpenJKDF2-MotS-Fixes/-/blob/main/src/Devices/sithControl.c), [orbit implementation](https://gitlab.com/codalilitari/OpenJKDF2-MotS-Fixes/-/blob/main/src/Engine/sithCamera.c).

1. Enter a suitable existing scene, stop in a clear space and verify the actual player model. Keep lighting and the start position consistent for comparisons.
2. Center the pointer before the game gains input during loading. In the retained private 1280×960 tests this was `(640,480)`; the resolution is part of that recipe. A late recenter could already have turned or tilted the player. Use the bound center-view control (`Home` in those profiles) to reset pitch.
3. Wait for pickups and automatic weapon changes to settle. Select the desired weapon and confirm it from the first-person image. Numeric keys can cycle related weapons/modes; “key 3 was sent” is not a rifle-identification result.
4. Release movement, turning, crouch, modifier, fire and mouse buttons. Make a deliberate bounded input/reset and verify the immediate view. A very short synthetic tap did not always reset an already active orbit in the retained controller.
5. Leave the private display untouched. A useful observed sampling schedule was 1, 35, 42, 49, 56, 63, 70 and 77 seconds after reset. Inspect the frames for real orbit progression and useful front/side/rear angles; time labels alone do not establish camera phase.
6. Save passive holds separately from movement/firing/alternate-fire sequences. Inspect mechanical contacts in their actual animated poses as well as the orbit views.

The scoped rifle in the retained MotS test forced first person and intercepted the usual camera toggle, so it supplied no ordinary third-person scoped-grip evidence. A passive orbit also cannot reveal every animation mixture or weapon state. For exact matched-frame comparisons use the source port's fixed-camera path, while retaining the original-retail orbit as separate runtime evidence.

## Hidden graphical tests with OpenMotS/OpenJKDF2

### Three-frame effects and texture probe

Use a build that includes the maintained [`-renderprobe` implementation](https://gitlab.com/codalilitari/OpenJKDF2-MotS-Fixes/-/blob/main/docs/render-probe.md) ([GitHub alternative](https://github.com/CodaLilitari/OpenJKDF2-MotS-Fixes/blob/main/docs/render-probe.md)). Stage the game/episode/model and a case-specific profile first. The output directory must already exist and be new/empty. The example is one command with placeholders:

```text
openjkdf2-64.exe -renderprobe -autostart -mp -episode <episode> -map <map.jkl> -path modeltest -renderout C:/model-tests/case-001/captures -renderwidth 1920 -renderheight 1080 -renderframes 180 -renderdelayms 4000 -renderbloom 0 -renderssao 0
```

Add `-motsCompat` for the appropriate MotS source-port case. Supply the intended character-selection mechanism; an autostart default can be Kyle. Do not combine this mode with `-headless`, `-capture`, or `OPENJKDF2_CAPTURE_REQUEST`.

The probe captures three post-composite back buffers, including the HUD, before swap. Compare PNGs with their `.png.txt` metadata and logs: effective drawable dimensions, scene-FBO dimensions, filtering, SSAA, effects and actual effect draws, renderer/vendor/version, software-world state, and both pre-readback and readback GL errors. Check `RENDERPROBE_COMPLETE`, successful exit and three valid images. The process still needs an outer deadline for startup/driver hangs outside its event loop.

Named `RENDERPROBE_JKGM_UPLOAD` receipts report decoded and GPU texture dimensions/formats and source identity. They establish upload, including precaching, rather than proving every visible surface sampled that material. Confirm appearance or use an instrumented diagnostic material when actual usage is the question. Native MAT cases need their own archive identity and visible result; the PNG upload log is not a universal material validator.

### Fixed-camera inspection

The separate [`-capture` request format](https://gitlab.com/codalilitari/OpenJKDF2-MotS-Fixes/-/blob/main/docs/HIDDEN_RENDERED_CAPTURE.md) ([GitHub alternative](https://github.com/CodaLilitari/OpenJKDF2-MotS-Fixes/blob/main/docs/HIDDEN_RENDERED_CAPTURE.md)) specifies output size, sector, camera position/orientation, horizontal FOV, capture frame, timeout, and an absolute new PPM output. It requires a deliberately staged `.openjkdf2-capture-session` directory and an absolute `OPENJKDF2_CAPTURE_REQUEST` path. Use the maintained request validator and launcher rather than adding the marker to a normal personal installation.

Its reviewed MP route accepts an existing staged MPC basename through `-character`. Check requested and actual local-player model names in the sidecar; ordinary game rules can select a different team/uniform model. A valid image needs exit zero, a valid sidecar, and exact PPM dimensions/data length. The fixed camera suppresses the view-model/HUD and retains ordinary first-person body visibility rules. It may verify resource selection without showing the complete player body.

Neither capture route freezes AI, animation, light pulses, scripts, or randomness. A fixed camera is only one part of a controlled comparison. Use a deliberately stable subject/pose when evaluating geometry or effects, record any diagnostic modification, and then test the normal input-controlled player separately.

## Build a graphics-setting matrix that answers a question

Start with a small matrix and expand where the result will change the release or an artistic decision. Keep output resolution separate from scene render resolution and from texture resolution.

| Case | Asset and game path | Useful starting settings | Inspect |
| --- | --- | --- | --- |
| Retail indexed | Correct JK or MotS indexed MATs; original software renderer | Confirm actual 8bpp game mode and 3D acceleration unchecked | Palette remapping, banding, transparency, mips, normal/dark light |
| Retail RGB565 | Native 16-bit MATs; original Direct3D path | Confirm real 3D acceleration and selected 16bpp game mode | Texture/mip completeness, clipping, depth/transparency, renderer-specific corruption |
| Source-port native MAT | JKGM replacements disabled | Gamma 1, filtering on for the first clear comparison, SSAA 1, bloom/SSAO off | Native material decoding/UV scale and geometry without a replacement hiding defects |
| Source-port PNG replacement | JKGM/replacements enabled with recorded files | Same camera/settings as MAT case; precache state recorded | Actual loaded image identity, island alignment, RGB/alpha, fine-detail aliasing |
| Effect comparison | Same source-port scene and material path | Off/off, bloom only, SSAO only, both | Intended effect actually drawn, no GL errors, no detail hidden by glare/shading |
| Native display review | Exact chosen runtime on the physical GPU | Record driver, output dimensions, scaling, HDR/SDR state | Human-visible brightness, color, pacing, input and window behavior |

For the retained Wine retail RGB565 runs, the working menu showed 1280×960 16bpp, acceleration enabled, minimum texture size 1 and system-memory backbuffer enabled. The last setting was a Wine compatibility choice, not a universal native-Windows performance recommendation. A MotS script's initial registry `b3DAccel=0` was superseded by a real menu change; its stale post-stop excerpt did not describe the active rendered case. Prefer actual menu/effective-state evidence over an initialization script.

For source-port detail, add a filtering-off comparison only to diagnose aliasing or honor the desired pixel style. In the inspected JKGM path, nearest sampling of a detailed 1024 image on a small on-screen surface caused distracting stipple; filtering selected linear sampling, not a new mip chain or anisotropic filter. SSAA 2 multiplies both scene dimensions, producing four times as many scene pixels. Measure its cost on the target GPU/resolution; do not transfer a low-resolution software-rendering preset blindly to 4K.

Use a neutral albedo baseline before effects. A warm/noisy color already encoded in a raw framebuffer capture may come from texture art, palette/light remapping, renderer, or capture decoding. Windows HDR did not produce the pixel values in a private Xvfb capture. HDR, SDR white level, monitor processing and driver output can still change how that image looks when displayed. Compare the stored RGB data separately from the native display presentation.

“8-bit” can refer to a MAT's palette indices, a software-rendered game surface, the menu/preview buffer, or a Windows compatibility setting. These are not equivalent. Likewise a wrapper's displayed “16-bit” game menu label need not describe its final desktop output depth. Use the specific renderer's documented behavior.

## Original retail on modern Windows: wrappers and targeted fixes

Use a separate portable copy for each wrapper. Begin with a stock control, configure the actual game Display menu, confirm the wrapper loaded, then install the model. Retain the exact executable/renderer combination in the result. Do not stack wrappers merely because several guides each recommend one.

| Option | Mechanism and useful role | Setup and boundaries |
| --- | --- | --- |
| Original executable with system graphics | Baseline original game path | Useful when it works on the target OS; a startup failure does not yet implicate the model |
| Jonathan Clark's JkGfxMod | Purpose-built OpenGL replacement for JK's old accelerated rendering, with optional replacement materials/effects | Follow exact executable requirements; launch its injector and enable game acceleration. It is distinct from source-port embedded JKGM support |
| Dege's dgVoodoo2 | Implements legacy graphics APIs through D3D11/D3D12 | For original 32-bit JK/MotS, stage the appropriate x86 DirectDraw/Direct3D components and local config. Record the selected output API; driver/backend differences matter |
| narzoul's DDrawCompat | Compatibility/performance work around DirectX 1–7, largely retaining native legacy API rendering | Separate `ddraw.dll` lane; confirm the author log. Current author support excludes Wine/VM environments and combining other wrappers/hooks |
| A documented executable patch/mod stack | Changes the specific game executable or resources required by that mod | Record complete stack and exact supported input binary; rendering fixes do not automatically expand KEY/model budgets |

Author sources and available release pages were checked on 30 September 2026:

- [JkGfxMod 1.0.0](https://github.com/jdmclark/jkgfxmod/releases/tag/v1.0.0): its release ZIP endpoint responded successfully. The [installation guide](https://github.com/jdmclark/jkgfxmod/blob/v1.0.0/doc/install.md) requires the original compatible JK 1.00 executable for the described Steam/GOG integration; the store executable is not interchangeable. The guide covers MotS separately. Its [configuration](https://github.com/jdmclark/jkgfxmod/blob/v1.0.0/doc/config.md) controls output resolution/window mode. The author explains that the game's hard-coded 16-bit label does not describe JkGfxMod's desktop-depth output.
- [dgVoodoo2 2.87.5](https://github.com/dege-diosg/dgVoodoo2/releases/tag/v2.87.5): its release ZIP endpoint responded successfully. [Author documentation](https://dege.freeweb.hu/dgVoodoo2/ReadmeGeneral/) and [project notes](https://github.com/dege-diosg/dgVoodoo2) describe the API/backend choices. The author does not target Wine/Proton. The earlier local staging used 2.87.4 with explicit D3D11 FL11.0; staging was not a native game test, and the newer release is not retroactively tested by it.
- [DDrawCompat 0.7.1](https://github.com/narzoul/DDrawCompat/releases/tag/v0.7.1): the selected ZIP endpoint responded successfully; the release is marked prerelease. The [author README](https://github.com/narzoul/DDrawCompat) explains its mechanism, loading log and current support scope. No DDrawCompat model runtime pass is claimed by this workflow.

Availability checks are not execution or safety certifications. Download from the identified author, retain the chosen version/hash, and follow current platform protections. A historical security-product detection or author response should not be rewritten as an independently established false positive. This workflow does not bundle wrappers or change system drivers/security settings.

JkGfxMod's [author README](https://github.com/jdmclark/jkgfxmod/blob/v1.0.0/README.md) recommends disabling other compatibility fixes and DirectDraw wrappers for its lane. With dgVoodoo, compare one backend at a time; the recorded NVIDIA-specific D3D12 concern was version/driver-dependent. Neither forced windowed mode nor disabling mouse confinement guarantees that an original game's dialogs stay hidden or cannot take focus. Use the private Wine display when desktop isolation is the requirement, or use a verified source-port hidden capture build.

### The misleading “at least 256 colors” startup error

Treat this as startup/display-compatibility diagnosis, not a request to quantize the model textures or reduce the entire Windows desktop to 256 colors. First identify whether the message occurs before a model loads. Test the same executable without the mod, record its launch path, and inspect per-executable compatibility settings and installed custom compatibility databases.

There is a direct historical report: on 28 March 2019, JkGfxMod author Jon`C documented removal of a conflicting Windows application-compatibility database named `Jedi Knight: Dark Forces II`; on 29 March he connected an apparent minimum-color error to a possibly incompatible shim. Wookie06 reported that the injector could run while direct startup showed the color error. This supports a specific diagnostic route, not a diagnosis of every later machine. [Original repair report](https://forums.massassi.net/Main_Massassi_Forums/Discussion_Forum/thread_67886_page_33.html), [color-error discussion](https://forums.massassi.net/Main_Massassi_Forums/Discussion_Forum/thread_67886_page_34.html).

If inspection confirms that exact conflicting custom database, preserve its identity/source and restoration route, then an administrator can remove that specific database using the historically reported command:

```text
sdbinst.exe -u -n "Jedi Knight: Dark Forces II"
```

This is a system-level database change, not a portable game-folder edit. Do not execute it automatically as part of model installation, guess that a similarly named entry is the same, or delete unrelated compatibility records. Microsoft's [database-management instructions](https://learn.microsoft.com/en-us/previous-versions/windows/it-pro/windows-10/deployment/compatibility/installing-and-uninstalling-custom-compatibility-databases-in-compatibility-administrator) and [`sdbinst` documentation](https://learn.microsoft.com/en-us/previous-versions/windows/it-pro/windows-10/deployment/compatibility/using-the-sdbinstexe-command-line-tool) describe inspection/removal and exact-name/GUID targeting. Recheck stock launch, actual Display settings, then the model after the targeted change.

This is a historical diagnosis and a conditional repair procedure; it does not establish the cause of every later color error. A separate appearance lesson also matters: speckled high-resolution OpenJK textures were not proof of a global 256-color framebuffer. Filtering, source-frequency content, native palette surfaces and replacement-upload format needed separate checks.

## Multiplayer: selection, connection and remote appearance

Use separate cases for these three milestones:

1. **Ordinary selection and one local host:** verify model label, animated preview, saved MPC, actual selected local model, and representative gameplay. Do not silently substitute the model after spawn.
2. **A second client joins:** use separate processes and profiles; record versions, payload hashes, provider/transport, session discovery, join, and both scoreboards. A loopback case isolates the software path; it does not test internet/NAT behavior.
3. **Remote visual behavior:** have each side observe the other moving, turning, crouching, jumping, aiming, switching/firing weapons, dying and respawning. Check attachments, team overrides and LODs. This is the evidence needed for a claim that peers see the intended character.

The earlier T-800 JK 1.01 study illustrates the distinction. Native DirectPlay components allowed a host and an older-provider client to connect locally; both displayed two T-800 scoreboard rows, and local movement/body views worked. Further peer inspection stopped before the remote actor's appearance was checked. Later v1.0.1 leg-repair hashes had their own bounded single-host checks. Neither result should be inflated into a full connected-client validation of those later assets. The [published T-800 validation](https://gitlab.com/codalilitari/T800-Hybrid-JK-MotS/-/blob/main/docs/VALIDATION.md) describes the release-specific scope ([GitHub alternative](https://github.com/CodaLilitari/T800-Hybrid-JK-MotS/blob/main/docs/VALIDATION.md)).

For SP actors, use the same discipline with the intended AI action set, weapon/aim roles, death and save/load instead of MP profile/network checks. A campaign player replacement and an MP selectable skin still need their different resource-registration routes.

## Read the results at the scope actually tested

| Retained example | Useful evidence | Does not establish |
| --- | --- | --- |
| Six Starweird designs × five original-executable/renderer cases | 240 sampled stock KEY starts across JK 1.00 indexed, JK 1.01 indexed/RGB565, and unchanged MotS indexed/RGB565; private Wine/Xvfb graphical output | Complete animation allocation safety, normal MP selection, remote appearance, or native GPU performance |
| Starweird native graphical matrix | 64 settings and 192 selected captures on the recorded NVIDIA OpenGL path, with fixed comparison scenes and settings/GL/upload receipts | Every player action, every visible surface's texture use, or retail fidelity |
| Integrated OpenMotS capture build | Eight stock effect cases, two posed captures, three invalid-input/error cases; 13 intended cases across 14 launches, after a harness wording correction | A full mod test, connected multiplayer match, deterministic replay or broad retail parity |
| T-800 final leg-repair release | Exact-hash JK/MotS indexed single-host samples, native source checks and separately verified installer combinations | Final-hash RGB565 runtime, every weapon transition or a second client's view |

The [model-workaround validation](https://gitlab.com/codalilitari/OpenJKDF2-MotS-Fixes/-/blob/main/docs/MODEL_WORKAROUNDS_VALIDATION.md), [render/capture integration record](https://gitlab.com/codalilitari/OpenJKDF2-MotS-Fixes/-/blob/main/docs/RENDER_CAPTURE_INTEGRATION.md), and [T-800 release validation](https://gitlab.com/codalilitari/T800-Hybrid-JK-MotS/-/blob/main/docs/VALIDATION.md) preserve the public technical scope. These are reusable precedents, not fresh game runs performed by reading this skill.

A useful final report identifies the chosen route, exact candidate, observed success, visible remaining defects, and the next test that would resolve a meaningful uncertainty. If the user wants a playable stock-animation version now and a custom rig later, report each as its own option rather than making the first release depend on every possible future test.
