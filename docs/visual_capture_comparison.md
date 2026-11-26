# Comparing a level from the same camera

Use this tool when an edit changes the look of a level and you want to see where it changed. It produces a local, standalone HTML page with labeled full frames and a JSON record of the comparison.

Capture both revisions through [Engine Capture](../tools/engine-capture/README.md) using the same executable, loaded map name, sector, position, pitch/yaw/roll, horizontal field of view, viewport and frame number. Keep the same renderer. Run the comparison from the repository root.

```sh
python tools/engine-capture/compare_visual_captures.py runs/before runs/after reviews/bridge
```

Open `reviews/bridge/comparison.html`. The output directory must be new. A recorded mismatch exits with status 2 and identifies the field to fix before capturing again. Matching the pose lets you inspect design differences such as a mast that is too tall or a translucent band that hides the gallery rails. A short lateral movement check in the intended runtime can reveal overlap or shimmer that is invisible in a still.

## Reference art and areas of interest

Supply reference art and your own landmarks to measure alignment. Keep the original uncropped images. Coordinates in the example below describe the images, so adjust them to your own subject and viewport.

```sh
python tools/engine-capture/compare_visual_captures.py runs/before runs/after reviews/bridge-art --reference art/bridge.png --review bridge-review.json --threshold 3
```

```json
{
  "regions": [
    {"name": "Light crossing the upper rail", "xywh": [240, 90, 120, 75]}
  ],
  "landmarks": [
    {"name": "Bridge meets the tower", "reference": [0.55, 0.66], "before": [0.59, 0.60], "after": [0.55, 0.63]}
  ]
}
```

Mark a corresponding point yourself in each image. For landmarks, divide the point’s x and y coordinates by that image’s width and height, starting at the upper left. The report shows the after point’s displacement from the reference in percentage points. Positive x points right and positive y points down. The HTML checkbox hides the numbered marks for clean inspection. Each image keeps its original aspect ratio.

Regions use integer pixel coordinates in the native capture, in the order left, top, width, height. The comparison always includes the full frame. For each region it counts pixels that brightened or darkened beyond your threshold and reports the mean signed change over **all** its pixels. The display-space measure uses `0.2126 R + 0.7152 G + 0.0722 B` with RGB channels in the range 0–255. A center that darkens where you painted a shaft of light is worth inspecting at native size. Animation, moving water and dithering can also change pixels.

## Which captures qualify

- **RED posed sessions.** Supply the session directory containing a successful, supervisor-finalized `launcher-receipt.json`, `request.json`, native `capture.ppm` and `capture.ppm.json`. The tool checks the frame and sidecar hashes against the launcher. Packaged captures with a preview revision also bind the request to that revision. For loose sessions it checks every declared input against `manifest-after.json`, including its destination, role, and SHA-256, and verifies the original and staged files stayed intact. This review uses the saved record; use [the preview freshness check](../tools/engine-capture/README.md#reuse-a-current-packaged-preview-frame) separately when you need to verify current source files.
- **Earlier hidden-capture sessions.** A successful `receipt.json` with `exit: 0`, `openGLInit: true`, package and engine hashes, and an embedded `capture.ppm.json` can accompany a matching native sidecar. Use the original P6 frame or the retained lossless RGB `frame.png`. These receipts have no capture-time image hash. The report labels that limitation and records the SHA-256 of the supplied image.
- **Engine evidence.** Both sessions need a native sidecar containing the capture pose, frame number, dimensions, loaded world, game and multiplayer mode. Older sidecars lacking world readback cannot qualify. The executable and reported graphics settings must match. Where the engine omitted graphics information, the JSON records `null`. The sidecar echoes the pose parameters applied by the capture feature. The engine supplies the framebuffer and loaded world state; the tool has no independent observation of the final camera matrix or deterministic simulation.
- **Asset evidence.** Package hashes may differ by design. For RED captures the JSON includes destination names and staged SHA-256 values, plus a list of changed inputs. An episode digest covers its embedded textures and palettes; individual archive members and the resources actually selected by the running engine require separate inspection. For loose assets their hashes are visible when supplied separately to RED.
- **Images.** Standard Python handles native RGB8 P6 files for sessions and references. For PNG captures or PNG/JPEG reference art, install Pillow in your Python environment. The report embeds the frames and optional reference image, so keep that output with the project’s other local review material.
