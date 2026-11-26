"""Compare two posed OpenJKDF2 capture sessions and write a full-frame HTML review."""
import argparse
import base64
from dataclasses import dataclass
import hashlib
import html
import io
import json
import math
from pathlib import Path
import re
import struct
import sys
import tempfile
import zlib


CAMERA = ("width", "height", "sector", "position", "pitchYawRoll", "horizontalFov")
MAX_PIXELS = 2_097_152


def check(ok, message):
    if not ok:
        raise ValueError(message)


def read(path, limit):
    with path.open("rb") as stream:
        data = stream.read(limit + 1)
    check(len(data) <= limit, f"{path.name} exceeds the input size limit")
    return data


def load_json(data):
    def unique(pairs):
        result = {}
        for key, value in pairs:
            check(key not in result, f"Duplicate JSON field {key}")
            result[key] = value
        return result

    def reject_constant(value):
        raise ValueError(f"Non-finite JSON number {value}")

    return json.loads(data.decode("utf-8"), object_pairs_hook=unique, parse_constant=reject_constant)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def digest(value):
    check(isinstance(value, str) and re.fullmatch(r"[0-9a-f]{64}", value),
          "Expected a lowercase SHA-256 identity")
    return value


def finite(value):
    return type(value) in (int, float) and math.isfinite(value)


@dataclass
class Frame:
    width: int
    height: int
    rgb: bytes

    def png(self):
        def chunk(kind, body):
            return (struct.pack(">I", len(body)) + kind + body
                    + struct.pack(">I", zlib.crc32(kind + body)))

        stride = self.width * 3
        rows = b"".join(b"\x00" + self.rgb[i:i + stride] for i in range(0, len(self.rgb), stride))
        header = struct.pack(">IIBBBBB", self.width, self.height, 8, 2, 0, 0, 0)
        return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header)
                + chunk(b"IDAT", zlib.compress(rows, 5)) + chunk(b"IEND", b""))


def image(path, *, capture=True):
    limit = MAX_PIXELS if capture else 16_000_000
    raw = read(path, 3 * limit + 65536)
    if raw.startswith(b"P6\n"):
        m = re.match(rb"P6\n([0-9]{1,5}) ([0-9]{1,5})\n255\n", raw)
        check(m, "Expected a native RGB8 P6 header")
        w, h = int(m[1]), int(m[2])
        check(0 < w * h <= limit and len(raw) - m.end() == w * h * 3,
              "Invalid P6 dimensions or pixel payload")
        return Frame(w, h, raw[m.end():]), sha(raw)
    try:
        from PIL import Image
    except ImportError as exc:
        raise ValueError("Reading a PNG/JPEG needs Pillow; native P6 frames use standard Python") from exc
    try:
        with Image.open(io.BytesIO(raw), formats=("PNG", "JPEG")) as im:
            check(0 < im.width * im.height <= limit, "Image exceeds the pixel budget")
            if capture:
                check(im.format == "PNG" and im.mode == "RGB", "Retained capture needs a lossless RGB PNG")
            check(getattr(im, "n_frames", 1) == 1, "Expected a single still image")
            return Frame(im.width, im.height, im.convert("RGB").tobytes()), sha(raw)
    except Image.DecompressionBombError as exc:
        raise ValueError("Image exceeds the pixel budget") from exc


def camera(record):
    check(isinstance(record, dict) and type(record.get("schemaVersion")) is int
          and record["schemaVersion"] == 1 and record.get("mode") == "hidden-rendered-inspection"
          and record.get("format") == "ppm-rgb8", "Expected a posed native RGB8 engine sidecar")
    pose = {k: record[k] for k in (*CAMERA, "renderedFrame", "world", "isMultiplayer", "motsCompat")}
    for name in ("width", "height", "renderedFrame"):
        check(type(pose[name]) is int and pose[name] > 0, f"Invalid {name}")
    check(pose["width"] * pose["height"] <= MAX_PIXELS and max(pose["width"], pose["height"]) <= 2048,
          "Viewport exceeds the native capture budget")
    check(type(pose["sector"]) is int and pose["sector"] >= 0, "Invalid sector")
    for name in ("position", "pitchYawRoll"):
        v = pose[name]
        check(isinstance(v, list) and len(v) == 3 and all(finite(n) for n in v), f"Invalid {name}")
    check(finite(pose["horizontalFov"]) and 0 < pose["horizontalFov"] < 180, "Invalid horizontalFov")
    check(record.get("engineWorldStateAvailable") is True and isinstance(pose["world"], str)
          and bool(pose["world"].strip()), "Missing loaded world in engine sidecar")
    check(all(type(pose[k]) is bool for k in ("isMultiplayer", "motsCompat")), "Missing game or multiplayer mode")
    return pose


def read_session(folder):
    side_bytes = read(folder / "capture.ppm.json", 16384)
    side = load_json(side_bytes)
    pose = camera(side)
    if (folder / "launcher-receipt.json").exists():
        wrapper = load_json(read(folder / "launcher-receipt.json", 2 << 20))
        for k in ("supervisorFinalized", "cleanupComplete", "engineStarted", "identityCheckComplete", "validPpm"):
            check(wrapper.get(k) is True, f"RED launcher did not confirm {k}")
        check(wrapper.get("status") == "completed" and wrapper.get("processExitCode") == 0
              and wrapper.get("immutableInputsUnchanged", wrapper.get("inputsUnchanged")) is True,
              "Expected a completed RED capture with stable input identities")
        check(sha(side_bytes) == wrapper.get("engineReceiptSha256")
              and side == wrapper.get("engineReportedReceipt"), "Native sidecar differs from RED launcher receipt")
        req = load_json(read(folder / "request.json", 2 << 20))
        revision = sha(json.dumps(req, sort_keys=True, separators=(",", ":"), ensure_ascii=True).encode("ascii"))
        if "previewRevisionSha256" in wrapper:
            check(revision == wrapper["previewRevisionSha256"], "Stored request differs from RED launcher revision")
        check(wrapper.get("requestedCamera") == req.get("camera") and wrapper.get("requestedFrame") == req["frame"],
              "Stored request and launcher disagree on camera or frame")
        check(req["map"].casefold() == pose["world"].casefold() and req["motsCompat"] is pose["motsCompat"]
              and pose["isMultiplayer"] is False, "Native world or game mode differs from posed RED request")
        for k in CAMERA:
            # The engine serializes camera floats at its own precision.
            observed, requested = pose[k], req["camera"][k]
            obs = observed if isinstance(observed, list) else [observed]
            ask = requested if isinstance(requested, list) else [requested]
            check(len(obs) == len(ask) and all(finite(b) and math.isclose(a,b,rel_tol=1e-7,abs_tol=1e-6)
                                               for a,b in zip(obs,ask)), f"Native {k} differs from RED request")
        check(req["frame"] == pose["renderedFrame"], "Native frame differs from RED request")
        engines = [r for r in req["files"] if r["role"] == "engine"]
        check(len(engines) == 1, "Expected one capture executable")
        engine = digest(engines[0]["sha256"])
        if "previewRevisionSha256" not in wrapper:
            # Historical sessions without a canonical request hash need corroboration
            # for every staged identity used in the before/after resource comparison.
            manifest = load_json(read(folder / "manifest-after.json", 2 << 20))
            check(isinstance(manifest, list), "Expected a post-run input manifest")
            requested = {r["destination"]: (r["role"], digest(r["sha256"])) for r in req["files"]}
            observed = {r["destination"]: (r["role"], digest(r["sha256"])) for r in manifest}
            check(len(requested) == len(req["files"]) and len(observed) == len(manifest)
                  and requested == observed, "Post-run staged input identities differ from the request")
            # Only packaged captures allow a staged config to change. Loose
            # captures are completed by the runner only when every input is stable.
            packaged = "episodeArchive" in req and "episodeArchive" in wrapper
            allowed_config = wrapper.get("stagedConfigChanges", []) if packaged else []
            for row in manifest:
                changed_config = (row["role"] == "config" and row["destination"] in allowed_config
                                  and wrapper.get("immutableInputsUnchanged") is True)
                check(row["sourceUnchanged"] is True
                      and (row["stagedUnchanged"] is True or (row["stagedUnchanged"] is False and changed_config)),
                      "Post-run staged input was changed")
        package = wrapper.get("episodeArchive", {}).get("archiveSha256")
        if package is not None:
            package = digest(package)
            check(any(r["destination"] == req["episodeArchive"] and r["sha256"] == package for r in req["files"]),
                  "Episode identity differs between launcher and request")
        file = folder / "capture.ppm"
        frame, h = image(file)
        check(h == digest(wrapper.get("imageSha256")), "Native frame differs from RED launcher SHA-256")
        staged = {r["destination"]: digest(r["sha256"]) for r in req["files"]}
        backend = wrapper.get("requestedGraphicsBackend")
        source = "RED launcher"
        binding = "launcher SHA-256 verified"
    else:
        wrapper = load_json(read(folder / "receipt.json", 2 << 20))
        check(wrapper.get("capture.ppm.json") == side and wrapper.get("exit") == 0 and wrapper.get("openGLInit") is True,
              "Hidden-capture receipt differs from its sidecar or the capture failed")
        engine, package = digest(wrapper.get("engineSha256")), digest(wrapper.get("sha256"))
        file = folder / "capture.ppm"
        if not file.exists():
            file = folder / "frame.png"
        frame, h = image(file)
        staged = None
        backend = None
        source = "hidden capture"
        binding = "image hashed at review; capture-time image hash unavailable"
    check((frame.width, frame.height) == (pose["width"], pose["height"]), "Viewport and image dimensions differ")
    return frame, {"source": source, "engineSha256": engine, "packageSha256": package, "pose": pose,
                   "graphics": side.get("graphics"), "frameFile": file.name, "frameSha256": h,
                   "frameBinding": binding, "engineSidecarSha256": sha(side_bytes), "requestedGraphicsBackend": backend,
                   "stagedInputs": staged, "runtimeResourceResolutionObserved": False}


def named(rows, label):
    check(isinstance(rows, list) and len(rows) <= 32, f"Expected at most 32 {label}")
    names = set()
    for r in rows:
        name = r["name"]
        check(isinstance(name, str) and 0 < len(name.strip()) <= 80 and name not in names,
              f"Expected a unique name of 1..80 characters in {label}")
        names.add(name)
        yield r


def metrics(a, b, xywh, threshold):
    x, y, w, h = xywh
    light = dark = total = 0
    cutoff = threshold * 10000
    for row in range(y, y+h):
        start = (row*a.width+x)*3
        for i in range(start, start+w*3, 3):
            delta = (2126*(b.rgb[i]-a.rgb[i]) + 7152*(b.rgb[i+1]-a.rgb[i+1])
                     + 722*(b.rgb[i+2]-a.rgb[i+2]))
            light += delta > cutoff
            dark += delta < -cutoff
            total += delta
    return {"xywh": xywh, "brightened": light, "darkened": dark, "withinThreshold": w*h-light-dark,
            "meanDelta": round(total / (10000*w*h), 4)}


def prepare_review(spec, a, b, reference, threshold):
    check(isinstance(spec, dict) and set(spec) <= {"regions", "landmarks"}, "Unknown review field")
    regions = [{"name": "Full frame", "xywh": [0, 0, a.width, a.height]}]
    pixels = a.width*a.height
    for row in named(spec.get("regions", []), "regions"):
        box = row["xywh"]
        check(set(row) == {"name","xywh"} and row["name"] != "Full frame"
              and isinstance(box, list) and len(box) == 4 and all(type(n) is int for n in box),
              "Each region needs a name and an integer xywh rectangle")
        x, y, w, h = box
        check(w > 0 and h > 0 and 0 <= x <= a.width-w and 0 <= y <= a.height-h, "Region lies outside the image")
        pixels += w*h
        check(pixels <= 12_000_000, "Review regions exceed the combined pixel budget")
        regions.append(row)
    landmarks = []
    for row in named(spec.get("landmarks", []), "landmarks"):
        check(reference and set(row) == {"name","reference","before","after"}, "Landmarks need a reference and three points")
        for key in ("reference","before","after"):
            pt = row[key]
            check(isinstance(pt, list) and len(pt)==2 and all(finite(n) and 0<=n<=1 for n in pt),
                  f"Invalid normalized {key} landmark")
        displacement = [round(100*(actual-want), 3) for actual,want in zip(row["after"],row["reference"])]
        landmarks.append(dict(row, afterMinusReferencePercentagePoints=displacement))
    return [dict(name=r["name"], **metrics(a,b,r["xywh"],threshold)) for r in regions], landmarks


def render(frames, report):
    figures = []
    for label, frame, key in frames:
        url = base64.b64encode(frame.png()).decode("ascii")
        dots = []
        for n, point in enumerate(report["landmarks"], 1):
            x, y = point[key]
            dots.append(f'<span class="dot" style="left:{x*100}%;top:{y*100}%">{n}</span>')
        figures.append(f'<figure><figcaption>{label}</figcaption><div class="frame">'
                       f'<img alt="{label}" src="data:image/png;base64,{url}">{"".join(dots)}</div></figure>')
    region_rows = "".join(f"<tr><th>{html.escape(r['name'])}</th><td>{r['brightened']:,}</td>"
                          f"<td>{r['darkened']:,}</td><td>{r['withinThreshold']:,}</td><td>{r['meanDelta']:+.4f}</td></tr>"
                          for r in report["regions"])
    landmark_rows = "".join(f"<tr><th>{i}. {html.escape(row['name'])}</th>"
                            f"<td>{row['afterMinusReferencePercentagePoints'][0]:+.3f}</td>"
                            f"<td>{row['afterMinusReferencePercentagePoints'][1]:+.3f}</td></tr>"
                            for i,row in enumerate(report["landmarks"], 1))
    landmark_table = ""
    if landmark_rows:
        landmark_table = ("<h2>Landmarks</h2><p>After minus reference in percentage points. Positive values point right and down.</p>"
                          "<table><tr><th>Landmark</th><th>Horizontal</th><th>Vertical</th></tr>" + landmark_rows + "</table>")
    notes = "".join(f"<li>{html.escape(report[l]['frameBinding'])} ({l})</li>" for l in ("before","after"))
    toggle = ('<label><input type="checkbox" id="marks" checked> Show landmarks</label>'
              if report["landmarks"] else "")
    changes = report["changedStagedInputs"]
    inputs_note = ("A complete staged-input comparison is unavailable for this pair." if changes is None
                   else f"{len(changes)} staged input(s) differ; comparison.json lists their names and hashes.")
    return ("""<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Capture comparison</title><style>body{font:1rem system-ui,sans-serif;background:#17191b;color:#eee;margin:2%}
.figures{display:flex;gap:1%;align-items:start}figure{margin:0;flex:1;min-width:0}figcaption{padding:.45em 0}
.frame{position:relative}img{display:block;width:100%;height:auto}.dot{position:absolute;transform:translate(-50%,-50%);
border-radius:50%;background:#111a;border:1px solid white;color:#fff;padding:0 .28em;pointer-events:none}
table{border-collapse:collapse}td,th{text-align:left;padding:.4em;border-bottom:1px solid #666}
@media(max-width:650px){.figures{display:block}figure{margin-bottom:1rem}}
body:has(#marks:checked) .dot{visibility:visible}body:has(#marks:not(:checked)) .dot{visibility:hidden}
</style><h1>Capture comparison</h1>""" + toggle + '<div class="figures">'  + "".join(figures) + "</div>"
            + f"<h2>Brightness change</h2><p>Threshold ±{report['threshold']:g} in display-space luma (0–255)."
            + " Positive values indicate a brighter after frame.</p>"
            + "<table><tr><th>Region</th><th>Brighter</th><th>Darker</th><th>Within threshold</th><th>Mean change</th></tr>"
            + region_rows + "</table>" + landmark_table + "<h2>Capture evidence</h2><p>Both engine sidecars report the same "
            + "requested pose, loaded world, mode and frame. The effective camera matrix is unobserved. "
            + "Animation and rendering can still vary. " + inputs_note + "</p><ul>" + notes + "</ul></html>")


def compare(before, after, output, review=None, reference=None, threshold=3.0):
    check(finite(threshold) and 0 <= threshold <= 255, "Threshold must be in [0, 255]")
    check(not output.exists(), "Choose a new output directory")
    a, ra = read_session(before)
    b, rb = read_session(after)
    for key in ra["pose"]:
        check(ra["pose"][key] == rb["pose"][key], f"Engine sidecar mismatch: {key}")
    check(ra["engineSha256"] == rb["engineSha256"], "Engine executable SHA-256 differs")
    check(ra["graphics"] == rb["graphics"], "Reported graphics settings differ")
    if ra["source"] == rb["source"] == "RED launcher":
        check(ra["requestedGraphicsBackend"] == rb["requestedGraphicsBackend"], "Requested graphics backends differ")
    input_changes = None
    if ra["stagedInputs"] is not None and rb["stagedInputs"] is not None:
        sa, sb = ra["stagedInputs"], rb["stagedInputs"]
        input_changes = [{"destination": name, "beforeSha256": sa.get(name), "afterSha256": sb.get(name)}
                         for name in sorted(sa.keys() | sb.keys()) if sa.get(name) != sb.get(name)]
    spec = load_json(read(review, 128 << 10)) if review else {}
    frames = [("A · Before", a, "before"), ("B · After", b, "after")]
    ref_info = None
    if reference:
        ref, h = image(reference, capture=False)
        frames.insert(0, ("Reference", ref, "reference"))
        ref_info = {"sha256": h, "width": ref.width, "height": ref.height}
    regions, landmarks = prepare_review(spec,a,b,reference,threshold)
    report = {"schemaVersion": 1, "before": ra, "after": rb, "threshold": threshold,
              "reference": ref_info, "landmarks": landmarks, "regions": regions, "effectiveCameraObserved": False,
              "changedStagedInputs": input_changes}
    page = render(frames, report)
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".visual-review-", dir=output.parent) as tmp:
        product = Path(tmp) / "review"
        product.mkdir()
        (product / "comparison.html").write_text(page, encoding="utf-8")
        (product / "comparison.json").write_text(json.dumps(report, indent=2, allow_nan=False)+"\n", encoding="utf-8")
        check(not output.exists(), "Choose a new output directory")
        product.rename(output)
    return report


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__)
    for name in ("before","after","output"):
        p.add_argument(name, type=Path)
    p.add_argument("--review", type=Path, help="JSON with named pixel rectangles and optional landmarks")
    p.add_argument("--reference", type=Path, help="Full-frame reference art (P6, PNG or JPEG)")
    p.add_argument("--threshold", type=float, default=3.0, help="Display-space luma threshold, default 3")
    args = p.parse_args(argv)
    try:
        compare(**vars(args))
    except (ValueError, KeyError, TypeError, OSError, OverflowError, RecursionError) as exc:
        print(f"Capture comparison failed: {exc}", file=sys.stderr)
        return 2
    print(args.output / "comparison.html")
    return 0


if __name__ == "__main__":
    sys.exit(main())
