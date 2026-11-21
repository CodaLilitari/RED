"""End-to-end revision checks with a synthetic engine boundary, not engine acceptance."""
import json
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from test_archive_gameplay import archive_document
from test_invoke_engine_capture import synthetic_graphics
import invoke_engine_capture as runner


class RevisionPreviewTests(unittest.TestCase):
    def test_check_current_then_refuse_superseded_input_pose_and_frame(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            doc = archive_document(root)
            doc["graphicsBackend"] = "sdl-offscreen"
            path = root / "input.json"
            path.write_text(json.dumps(doc))
            session = root / "session"

            def execute(command, stage, *args, **kwargs):
                (stage / "capture.ppm").write_bytes(b"P6\n64 64\n255\n" + bytes(64 * 64 * 3))
                (stage / "capture.ppm.json").write_text(json.dumps({"graphics": synthetic_graphics()}))
                kwargs["on_start"](123)
                return {"processId": 123, "processExitCode": 0, "timedOut": False}

            with mock.patch.object(runner, "execute_engine", side_effect=execute), \
                    mock.patch.object(runner, "profile_runs_here", return_value=True):
                receipt = runner._run_capture(path, session)
            self.assertEqual(receipt["status"], "completed", receipt)
            check = [str(path), "--check-preview", str(session)]
            self.assertEqual(runner.main(check), 0)
            self.assertFalse(receipt["effectiveCameraObserved"])
            doc["camera"]["sector"] += 1
            path.write_text(json.dumps(doc))
            self.assertEqual(runner.main(check), 2)
            doc["camera"]["sector"] -= 1
            path.write_text(json.dumps(doc))
            for row in [doc["files"][0], doc["files"][-1]]:
                src = Path(row["source"])
                original = src.read_bytes()
                src.write_bytes(original + b"new revision")
                self.assertEqual(runner.main(check), 2)
                src.write_bytes(original)
            frame = session / "capture.ppm"
            original = frame.read_bytes()
            frame.write_bytes(original[:-1] + b"x")
            self.assertEqual(runner.main(check), 2)
