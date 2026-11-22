"""End-to-end revision checks with a synthetic engine boundary, not engine acceptance."""
import json
import sys
import tempfile
import threading
import unittest
from pathlib import Path
from unittest import mock

from test_archive_gameplay import archive_document
from test_invoke_engine_capture import synthetic_graphics
import invoke_engine_capture as runner


def synthetic_worker(admitted, session):
    def execute(command, stage, *args, **kwargs):
        (stage / "capture.ppm").write_bytes(b"P6\n64 64\n255\n" + bytes(64 * 64 * 3))
        (stage / "capture.ppm.json").write_text(json.dumps({"graphics": synthetic_graphics()}))
        kwargs["on_start"](123)
        return {"processId": 123, "processExitCode": 0, "timedOut": False}

    with mock.patch.object(runner, "execute_engine", side_effect=execute), \
            mock.patch.object(runner, "profile_runs_here", return_value=True):
        return runner._run_capture(Path(admitted), Path(session), supervised=True)


class RevisionPreviewTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        root = Path(temporary.name)
        self.doc = archive_document(root)
        self.doc["graphicsBackend"] = "sdl-offscreen"
        self.path = root / "input.json"
        self.path.write_text(json.dumps(self.doc))
        self.session = root / "session"

        def command(admitted, output):
            script = ("import sys; sys.path.insert(0, sys.argv[1]); "
                      "from test_revision_preview import synthetic_worker; "
                      "sys.exit(synthetic_worker(*sys.argv[2:])['status'] != 'completed')")
            return [sys.executable, "-c", script, str(Path(__file__).resolve().parent),
                    str(admitted), str(output)]

        for patcher in (mock.patch.object(runner, "_worker_command", side_effect=command),
                        mock.patch.object(runner, "profile_runs_here", return_value=True)):
            patcher.start()
            self.addCleanup(patcher.stop)

    def check(self):
        return runner.main([str(self.path), "--check-preview", str(self.session)])

    def test_check_current_then_refuse_superseded_input_pose_and_frame(self):
        receipt = runner.run_capture(self.path, self.session)
        self.assertEqual(receipt["status"], "completed", receipt)
        self.assertEqual(self.check(), 0)
        self.assertFalse(receipt["effectiveCameraObserved"])
        self.doc["camera"]["sector"] += 1
        self.path.write_text(json.dumps(self.doc))
        self.assertEqual(self.check(), 2)
        self.doc["camera"]["sector"] -= 1
        self.path.write_text(json.dumps(self.doc))
        for row in [self.doc["files"][0], self.doc["files"][-1]]:
            src = Path(row["source"])
            original = src.read_bytes()
            src.write_bytes(original + b"new revision")
            self.assertEqual(self.check(), 2)
            src.write_bytes(original)
        frame = self.session / "capture.ppm"
        original = frame.read_bytes()
        frame.write_bytes(original[:-1] + b"x")
        self.assertEqual(self.check(), 2)

    def test_worker_completion_is_unusable_before_failed_cleanup(self):
        observed = []
        close = runner.WorkerTree.close

        def fail_cleanup(tree):
            close(tree)
            observed.append(self.check())
            raise OSError("synthetic cleanup failure")

        with mock.patch.object(runner.WorkerTree, "close", fail_cleanup):
            receipt = runner.run_capture(self.path, self.session)
        self.assertEqual(receipt["status"], "failed", receipt)
        self.assertFalse(receipt["cleanupComplete"])
        self.assertEqual(self.check(), 2)
        self.assertEqual(observed, [2])

    def test_cancellation_during_cleanup_revokes_finished_worker_result(self):
        cancel = threading.Event()
        close = runner.WorkerTree.close

        def cancel_during_cleanup(tree):
            close(tree)
            cancel.set()

        with mock.patch.object(runner.WorkerTree, "close", cancel_during_cleanup):
            receipt = runner.run_capture(self.path, self.session, cancel_event=cancel)
        self.assertEqual(receipt["status"], "failed", receipt)
        self.assertTrue(receipt["cancelled"])
        self.assertIsNone(receipt["immutableInputsUnchanged"])
        self.assertEqual(self.check(), 2)

    def test_final_receipt_appears_as_one_complete_file(self):
        write = runner.write_json
        exposed = []
        final = self.session / "launcher-receipt.json"

        def observe_write(path, data):
            if data.get("supervisorFinalized") is True:
                path.write_text("{")
                exposed.append(final.exists())
            write(path, data)

        with mock.patch.object(runner, "write_json", side_effect=observe_write):
            receipt = runner.run_capture(self.path, self.session)
        self.assertEqual(exposed, [False])
        self.assertEqual(json.loads(final.read_bytes()), receipt)
        self.assertEqual(self.check(), 0)
        del receipt["supervisorFinalized"]
        final.write_text(json.dumps(receipt))
        self.assertEqual(self.check(), 2)

    def test_refuse_request_or_earlier_source_replaced_during_hashing(self):
        receipt = runner.run_capture(self.path, self.session)
        self.assertEqual(receipt["status"], "completed", receipt)
        digest = runner.sha256_file
        last_source = Path(self.doc["files"][-1]["source"])
        first_source = Path(self.doc["files"][0]["source"])
        for changed in (self.path, first_source):
            with self.subTest(changed=changed):
                original = changed.read_bytes()
                def replace_during_hash(path):
                    if path == last_source:
                        if changed == self.path:
                            doc = json.loads(original)
                            doc["camera"]["sector"] += 1
                            changed.write_text(json.dumps(doc))
                        else:
                            tmp = changed.with_suffix(".new")
                            tmp.write_bytes(original + b"changed")
                            tmp.replace(changed)
                    return digest(path)
                try:
                    with mock.patch.object(runner, "sha256_file", side_effect=replace_during_hash):
                        self.assertEqual(self.check(), 2)
                finally:
                    changed.write_bytes(original)
