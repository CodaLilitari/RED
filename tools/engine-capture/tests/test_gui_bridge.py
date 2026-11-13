"""Optional CLI inspection and ownership channel; never real engine rendering."""

import json
import os
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from pathlib import Path
from unittest import mock

from test_invoke_engine_capture import request_document, runner
from test_runner_limits import HARNESS


class GuiBridgeTests(unittest.TestCase):
    def test_validate_only_accepts_each_profile_without_output_or_execution(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for profile in ("windows", "linux"):
                with self.subTest(profile=profile):
                    document = request_document(root, profile)
                    request = root / "input.json"
                    request.write_text(json.dumps(document), encoding="utf-8")
                    output = root / "never-created"
                    result = subprocess.run([sys.executable, "-B", runner.__file__, str(request),
                                             "--validate-only", "--output", str(output)],
                                            capture_output=True, text=True, timeout=5)
                    self.assertEqual(result.returncode, 0, result.stderr)
                    report = json.loads(result.stdout)
                    self.assertEqual(report["status"], "validated")
                    self.assertEqual(report["request"]["engineProfile"], profile)
                    self.assertEqual(report["canRunHere"], runner.profile_runs_here(profile))
                    self.assertFalse(report["sourceHashesVerified"])
                    self.assertFalse(output.exists())

    def test_validate_only_refuses_invalid_request_without_an_output_argument(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            document = request_document(root)
            document["files"][1]["destination"] = "C:outside.txt"
            request = root / "input.json"
            request.write_text(json.dumps(document), encoding="utf-8")
            result = subprocess.run([sys.executable, "-B", runner.__file__, str(request), "--validate-only"],
                                    capture_output=True, text=True, timeout=5)
            self.assertEqual(result.returncode, 2)
            self.assertTrue(result.stderr.startswith("{"), result.stderr)
            self.assertEqual(json.loads(result.stderr)["status"], "refused")
            self.assertIn("Unsafe file destination", json.loads(result.stderr)["error"])

    def test_already_cancelled_call_refuses_before_creating_a_session(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            request = root / "input.json"
            request.write_text(json.dumps(request_document(root)), encoding="utf-8")
            cancelled = threading.Event()
            cancelled.set()
            with mock.patch.object(runner, "profile_runs_here", return_value=True):
                try:
                    with self.assertRaisesRegex(runner.CaptureError, "cancel"):
                        runner.run_capture(request, root / "output", cancel_event=cancelled)
                except TypeError as error:
                    self.fail(f"Public capture must accept optional ownership cancellation: {error}")
            self.assertFalse((root / "output").exists())

    def test_explicit_stdin_byte_and_eof_cancel_owned_work_with_failure_receipt(self):
        for mode in ("byte", "eof"):
            with self.subTest(mode=mode), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                document = request_document(root, "windows" if os.name == "nt" else "linux")
                document.update(engineTimeoutMs=1000, runnerTimeoutMs=90000)
                request = root / "input.json"
                request.write_text(json.dumps(document), encoding="utf-8")
                harness = root / "gui-harness.py"
                source = HARNESS.replace(
                    "result = runner.run_capture(Path(request), Path(output))",
                    "raise SystemExit(runner.main([request, '--output', output, '--cancel-on-stdin']))")
                harness.write_text(source, encoding="utf-8")
                process = subprocess.Popen([sys.executable, "-B", str(harness),
                                            str(Path(runner.__file__).parent), "preparation",
                                            str(request), str(root / "output")],
                                           stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                try:
                    deadline = time.monotonic() + 5
                    entered = root / "output/entered-phase"
                    while not entered.exists() and process.poll() is None and time.monotonic() < deadline:
                        time.sleep(0.01)
                    self.assertTrue(entered.exists(), "owned preparation phase was not reached")
                    if mode == "byte":
                        process.stdin.write(b"cancel\n")
                        process.stdin.flush()
                    else:
                        process.stdin.close()
                        process.stdin = None
                    stdout, stderr = process.communicate(timeout=4)
                    self.assertEqual(process.returncode, 1, stderr.decode(errors="replace"))
                    self.assertEqual(json.loads(stdout)["status"], "failed")
                    receipt = json.loads((root / "output/launcher-receipt.json").read_text())
                    self.assertTrue(receipt["cancelled"])
                    self.assertFalse(receipt["runnerTimedOut"])
                    self.assertFalse(receipt["identityCheckComplete"])
                    self.assertIsNone(receipt["inputsUnchanged"])
                    self.assertTrue(receipt["cleanupComplete"])
                    self.assertEqual(receipt["acceptance"], "unverified")
                finally:
                    if process.poll() is None:
                        runner.kill_process_tree(process)
                        process.wait(timeout=3)


if __name__ == "__main__":
    unittest.main()
