"""Compare synthetic native sidecars and images produced in isolated directories."""
import copy
import json
import sys
import tempfile
import unittest
from unittest.mock import patch
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import compare_visual_captures as visual


def write_json(path, data):
    raw = json.dumps(data, indent=2).encode()
    path.write_bytes(raw)
    return visual.sha(raw)


class VisualComparisonTests(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        self.root = Path(temp.name)
        self.before = self.root / "before"
        self.after = self.root / "after"
        self.out = self.root / "out"
        self.pose = {"width": 64, "height": 64, "sector": 1, "position": [1,-2,3],
                     "pitchYawRoll": [4,-80,0], "horizontalFov": 110}
        self.side = dict(self.pose, schemaVersion=1, mode="hidden-rendered-inspection",
                         format="ppm-rgb8", renderedFrame=3, world="example.jkl",
                         engineWorldStateAvailable=True, isMultiplayer=False, motsCompat=False)
        self.pixels = bytes([40,40,40]) * (64*64)
        self.capture(self.before, self.side, self.pixels)
        self.capture(self.after, self.side, bytes([160]*3 + [10]*3) + self.pixels[6:])

    def capture(self, folder, side, pixels, *, native=True, engine="7"*64):
        folder.mkdir(exist_ok=True)
        raw = b"P6\n64 64\n255\n" + pixels
        (folder/"capture.ppm").write_bytes(raw)
        sh = write_json(folder/"capture.ppm.json", side)
        if not native:
            write_json(folder/"receipt.json", {"engineSha256": engine, "sha256": "8"*64, "openGLInit": True,
                                              "exit": 0, "capture.ppm.json": side})
            return
        pose = {k:side[k] for k in visual.CAMERA}
        req = {"camera": pose, "frame": side["renderedFrame"], "map": side["world"], "motsCompat": False,
               "episodeArchive": "episode/fixture.gob",
               "files": [{"role":"engine","destination":"openjkdf2", "sha256":engine},
                         {"role":"resource","destination":"episode/fixture.gob","sha256":"8"*64}]}
        write_json(folder/"request.json", req)
        revision = visual.sha(json.dumps(req, sort_keys=True,separators=(",",":")).encode())
        write_json(folder/"launcher-receipt.json", dict(
            requestedFrame=side["renderedFrame"], requestedCamera=pose, status="completed", processExitCode=0,
            cleanupComplete=True, identityCheckComplete=True, validPpm=True, immutableInputsUnchanged=True,
            engineStarted=True, supervisorFinalized=True, engineReportedReceipt=side, engineReceiptSha256=sh,
            episodeArchive={"archiveSha256":"8"*64}, imageSha256=visual.sha(raw), previewRevisionSha256=revision))

    def test_native_pair_and_markers(self):
        spec = self.root/"review.json"
        write_json(spec, {"landmarks":[{"name":"& <cap>", "before":[.25,.5], "after":[.50,.60],
                                        "reference":[.45,.65]}], "regions":[{"name":"Top","xywh":[0,0,2,1]}]})
        result = visual.compare(self.before,self.after,self.out,spec,self.before/"capture.ppm")
        region = result["regions"][1]
        self.assertEqual((region["brightened"],region["darkened"],region["withinThreshold"],region["meanDelta"]),
                         (1,1,0,45.0))
        self.assertEqual(result["landmarks"][0]["afterMinusReferencePercentagePoints"],[5.0,-5.0])
        self.assertEqual(json.loads((self.out/"comparison.json").read_text()),result)
        html = (self.out/"comparison.html").read_text(encoding="utf-8")
        self.assertIn("A · Before",html)
        self.assertIn("B · After",html)
        self.assertIn("&amp; &lt;cap&gt;",html)
        self.assertEqual(html.count("data:image/png;base64,"),3)
        with self.assertRaisesRegex(ValueError,"new output directory"):
            visual.compare(self.before,self.after,self.out)

    def test_mismatched_inputs_and_no_publication(self):
        differences = {"position":[2,-2,3], "sector":2, "pitchYawRoll":[0,0,0], "horizontalFov":90,
                       "world":"other.jkl", "renderedFrame":4}
        for field, changed in differences.items():
            with self.subTest(field=field):
                side = copy.deepcopy(self.side)
                side[field] = changed
                self.capture(self.after,side,self.pixels)
                with self.assertRaisesRegex(ValueError,field):
                    visual.compare(self.before,self.after,self.out)
                self.assertFalse(self.out.exists())
        self.capture(self.after,self.side,self.pixels,engine="3"*64)
        with self.assertRaisesRegex(ValueError,"executable"):
            visual.compare(self.before,self.after,self.out)

    def test_changed_image_and_mismatched_viewport(self):
        (self.after/"capture.ppm").write_bytes((self.before/"capture.ppm").read_bytes())
        with self.assertRaisesRegex(ValueError,"SHA-256"):
            visual.compare(self.before,self.after,self.out)
        self.capture(self.after,dict(self.side,width=128),self.pixels)
        with self.assertRaisesRegex(ValueError,"Viewport"):
            visual.compare(self.before,self.after,self.out)

    def test_legacy_pose_pair_and_unbound_image_label(self):
        self.before = self.root/"legacy"
        self.capture(self.before,self.side,self.pixels,native=False)
        result = visual.compare(self.before,self.after,self.out)
        self.assertIn("capture-time image hash unavailable",result["before"]["frameBinding"])
        self.assertIn("verified",result["after"]["frameBinding"])

    def test_unverified_pose_and_incomplete_receipt(self):
        wrapperfile = self.before/"launcher-receipt.json"
        wrapper = json.loads(wrapperfile.read_text())
        wrapper["supervisorFinalized"] = False
        write_json(wrapperfile,wrapper)
        with self.assertRaisesRegex(ValueError,"supervisorFinalized"):
            visual.compare(self.before,self.after,self.out)
        self.capture(self.before,dict(self.side,engineWorldStateAvailable=False),self.pixels)
        with self.assertRaisesRegex(ValueError,"loaded world"):
            visual.compare(self.before,self.after,self.out)

    def test_request_tamper_and_graphics_mismatch(self):
        reqfile = self.after/"request.json"
        req = json.loads(reqfile.read_text())
        req["files"][0]["sha256"] = "b"*64
        write_json(reqfile,req)
        with self.assertRaisesRegex(ValueError,"revision"):
            visual.compare(self.before,self.after,self.out)
        self.capture(self.after,dict(self.side,graphics={"sdlVideoDriver":"offscreen"}),self.pixels)
        with self.assertRaisesRegex(ValueError,"graphics"):
            visual.compare(self.before,self.after,self.out)

    def set_loose(self, folder, material_hash):
        wr = folder / "launcher-receipt.json"
        wrapper = json.loads(wr.read_text())
        del wrapper["previewRevisionSha256"]
        del wrapper["episodeArchive"]
        del wrapper["immutableInputsUnchanged"]
        wrapper["inputsUnchanged"] = True
        write_json(wr, wrapper)
        rq = folder / "request.json"
        req = json.loads(rq.read_text())
        del req["episodeArchive"]
        req["files"][1] = {"role": "scenario", "destination": "episode/fixture/mat/panel.mat",
                           "sha256": material_hash}
        write_json(rq, req)
        rows = [dict(r, sourceUnchanged=True, stagedUnchanged=True) for r in req["files"]]
        write_json(folder / "manifest-after.json", rows)
        return rows

    def test_loose_manifest_and_changed_staged_inputs(self):
        self.set_loose(self.before, "1"*64)
        self.set_loose(self.after, "2"*64)
        result = visual.compare(self.before, self.after, self.out)
        self.assertEqual(result["changedStagedInputs"], [{"destination": "episode/fixture/mat/panel.mat",
                                                         "beforeSha256": "1"*64, "afterSha256": "2"*64}])

    def test_loose_manifest_binds_each_resource(self):
        rows = self.set_loose(self.after, "3"*64)
        self.set_loose(self.before, "2"*64)
        req_file = self.after / "request.json"
        req = json.loads(req_file.read_text())
        # Only the resource is altered; the engine identity is intact.
        req["files"][1]["sha256"] = "2"*64
        write_json(req_file, req)
        with self.assertRaisesRegex(ValueError, "input identities"):
            visual.compare(self.before, self.after, self.out)
        req["files"][1]["sha256"] = "3"*64
        write_json(req_file, req)
        manifest = self.after / "manifest-after.json"
        for altered in (rows[:-1], rows + rows[-1:], [rows[0],dict(rows[1], role="config")]):
            write_json(manifest, altered)
            with self.assertRaisesRegex(ValueError, "input identities"):
                visual.compare(self.before, self.after, self.out)
        for field in ("sourceUnchanged", "stagedUnchanged"):
            write_json(manifest, [rows[0],dict(rows[1], **{field: False})])
            with self.assertRaisesRegex(ValueError, "input was changed"):
                visual.compare(self.before, self.after, self.out)
        self.assertFalse(self.out.exists())

    def test_older_packaged_manifest_allows_only_reported_staged_config_writes(self):
        req_file = self.after / "request.json"
        wrapper_file = self.after / "launcher-receipt.json"
        req, wrapper = json.loads(req_file.read_text()), json.loads(wrapper_file.read_text())
        del wrapper["previewRevisionSha256"]
        wrapper["stagedConfigChanges"] = ["settings.cfg"]
        wrapper["inputsUnchanged"] = False
        write_json(wrapper_file, wrapper)
        req["files"].append({"role": "config", "destination": "settings.cfg", "sha256": "9"*64})
        write_json(req_file, req)
        rows = [dict(r, sourceUnchanged=True, stagedUnchanged=r["role"] != "config") for r in req["files"]]
        manifest = self.after / "manifest-after.json"
        write_json(manifest, rows)
        visual.read_session(self.after)
        rows[-1]["sourceUnchanged"] = False
        write_json(manifest, rows)
        with self.assertRaisesRegex(ValueError, "input was changed"):
            visual.read_session(self.after)
        rows[-1]["sourceUnchanged"] = True
        write_json(manifest, rows)
        wrapper["stagedConfigChanges"] = []
        write_json(wrapper_file, wrapper)
        with self.assertRaisesRegex(ValueError, "input was changed"):
            visual.read_session(self.after)

    def test_publication_failure_removes_unpublished_files(self):
        original = Path.write_text
        def failed_json(path, *args, **kwargs):
            if path.name == "comparison.json":
                raise OSError("Simulated disk write error")
            return original(path, *args, **kwargs)
        with patch.object(Path, "write_text", failed_json):
            with self.assertRaises(OSError):
                visual.compare(self.before, self.after, self.out)
        self.assertFalse(self.out.exists())
        self.assertFalse(list(self.root.glob(".visual-review-*")))

    def test_bad_config(self):
        spec = self.root/"bad.json"
        write_json(spec,{"regions":[{"name":"outside","xywh":[20,30,60,30]}]})
        with self.assertRaisesRegex(ValueError,"outside"):
            visual.compare(self.before,self.after,self.out,spec)
        write_json(spec,{"landmarks":[{"name":"floating"}]})
        with self.assertRaisesRegex(ValueError,"reference"):
            visual.compare(self.before,self.after,self.out,spec)
        self.assertFalse(self.out.exists())


if __name__ == "__main__":
    unittest.main()
