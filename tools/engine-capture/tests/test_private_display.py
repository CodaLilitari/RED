"""Private Xvfb ownership contracts without starting an X server.

Only process creation is substituted; readiness pipes and descriptor checks
are real POSIX resources. This is not live display or OpenGL acceptance.
"""

import errno
import io
import os
import subprocess
import sys
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
import invoke_engine_capture as runner


@unittest.skipUnless(os.name == "posix", "POSIX display descriptors")
class PrivateDisplayTests(unittest.TestCase):
    def setUp(self):
        self.process = mock.Mock()
        self.process.poll.return_value = None
        self.process.stderr = io.BytesIO()
        self.launches = []
        self.pipes = []
        original_pipe = os.pipe

        def tracked_pipe():
            descriptors = original_pipe()
            self.pipes.extend(descriptors)
            return descriptors

        self.pipe_patch = mock.patch.object(runner.os, "pipe", side_effect=tracked_pipe)
        self.pipe_patch.start()
        self.addCleanup(self.pipe_patch.stop)
        self.addCleanup(self.close_remaining_descriptors)

    def close_remaining_descriptors(self):
        for descriptor in self.pipes:
            try:
                os.close(descriptor)
            except OSError:
                pass

    def assert_descriptors_closed(self):
        for descriptor in self.pipes:
            with self.assertRaises(OSError) as context:
                os.fstat(descriptor)
            self.assertEqual(context.exception.errno, errno.EBADF)

    def launcher(self, notification):
        def launch(command, **kwargs):
            self.launches.append((command, kwargs))
            os.write(kwargs["pass_fds"][0], notification)
            return self.process
        return launch

    def test_private_display_uses_allocated_number_and_releases_resources(self):
        with mock.patch.object(runner.shutil, "which", return_value="/synthetic/Xvfb"), \
                mock.patch.object(runner.subprocess, "Popen", side_effect=self.launcher(b"471\n")):
            with runner.PrivateDisplay(320, 240) as display:
                self.assertEqual(display.display, ":471")
                self.assertEqual(display.receipt(),
                                 {"display": ":471", "tcpListening": False,
                                  "userVisible": False})
                command, kwargs = self.launches[0]
                self.assertEqual(command,
                                 ["/synthetic/Xvfb", "-displayfd", str(self.pipes[1]),
                                  "-nolisten", "tcp", "-screen", "0", "320x240x24"])
                self.assertEqual(kwargs["pass_fds"], (self.pipes[1],))
                self.assertEqual(kwargs["stdout"], subprocess.DEVNULL)
                self.assertEqual(kwargs["stderr"], subprocess.PIPE)
                self.assert_descriptors_closed()
        self.process.terminate.assert_called_once()
        self.process.wait.assert_called_once_with(timeout=5)
        self.assertTrue(self.process.stderr.closed, "display stderr handle leaked")

    def test_process_creation_failure_closes_both_pipe_ends(self):
        with mock.patch.object(runner.shutil, "which", return_value="/synthetic/Xvfb"), \
                mock.patch.object(runner.subprocess, "Popen", side_effect=OSError("spawn failed")):
            with self.assertRaisesRegex(OSError, "spawn failed"):
                runner.PrivateDisplay().__enter__()
        self.assert_descriptors_closed()

    def test_missing_xvfb_refuses_before_pipe_allocation(self):
        with mock.patch.object(runner.shutil, "which", return_value=None):
            with self.assertRaisesRegex(runner.CaptureError, "needs Xvfb"):
                runner.PrivateDisplay().__enter__()
        self.assertEqual(self.pipes, [])

    def test_invalid_or_closed_readiness_reaps_process(self):
        for notification in (b"invalid\n", b"65536\n", b"", b"\xff\n"):
            with self.subTest(notification=notification):
                self.process.reset_mock()
                self.process.stderr = io.BytesIO()
                with mock.patch.object(runner.shutil, "which", return_value="/synthetic/Xvfb"), \
                        mock.patch.object(runner.subprocess, "Popen",
                                          side_effect=self.launcher(notification)):
                    with self.assertRaises((runner.CaptureError, UnicodeDecodeError)):
                        runner.PrivateDisplay().__enter__()
                self.process.terminate.assert_called_once()
                self.process.wait.assert_called_once_with(timeout=5)
                self.assert_descriptors_closed()

    def test_early_exit_and_readiness_timeout_release_resources(self):
        for early_exit in (True, False):
            with self.subTest(early_exit=early_exit):
                self.process.reset_mock()
                self.process.poll.return_value = 2 if early_exit else None
                self.process.stderr = io.BytesIO()
                with mock.patch.object(runner.shutil, "which", return_value="/synthetic/Xvfb"), \
                        mock.patch.object(runner.subprocess, "Popen",
                                          side_effect=self.launcher(b"")), \
                        mock.patch.object(runner.time, "monotonic", side_effect=[0.0, 11.0]):
                    with self.assertRaisesRegex(runner.CaptureError,
                                                "exited before" if early_exit else "within 10"):
                        runner.PrivateDisplay().__enter__()
                self.assert_descriptors_closed()

    def test_terminate_timeout_escalates_to_kill_and_closes_stderr(self):
        self.process.wait.side_effect = [subprocess.TimeoutExpired("Xvfb", 5), 0]
        display = runner.PrivateDisplay()
        display.process = self.process
        display.close()
        self.process.terminate.assert_called_once()
        self.process.kill.assert_called_once()
        self.assertEqual(self.process.wait.call_args_list,
                         [mock.call(timeout=5), mock.call(timeout=5)])
        self.assertTrue(self.process.stderr.closed, "display stderr handle leaked")
        display.close()
        self.process.kill.assert_called_once()


if __name__ == "__main__":
    unittest.main()
