"""Real subprocess limits; pixels and engine programs remain synthetic."""

import json
import os
import subprocess
import sys
import tempfile
import time
import unittest
from pathlib import Path
from unittest import mock

from test_invoke_engine_capture import request_document
import invoke_engine_capture as runner


HARNESS = r'''
import json, os, sys, time
from pathlib import Path
sys.path.insert(0, sys.argv[1])
import invoke_engine_capture as runner
# This fixture runs a Python program, never a native OpenJKDF2 binary. Admit
# its synthetic profile on every host; production CLI admission is tested below.
runner.profile_runs_here = lambda profile: True
phase, request, output = sys.argv[2:5]
original_stage = runner.stage_files
original_after = runner.after_manifest
def stage(*args):
    if phase == 'preparation':
        Path(output, 'entered-phase').write_text('preparation')
        time.sleep(20)
    if phase == 'combined':
        time.sleep(0.7)
    return original_stage(*args)
def after(*args):
    if phase == 'collection':
        Path(output, 'entered-phase').write_text('collection')
        time.sleep(20)
    if phase == 'collection-failure':
        raise OSError('synthetic identity read failure')
    return original_after(*args)
runner.stage_files = stage
runner.after_manifest = after
program = "from pathlib import Path; import sys, time; "
if phase == 'combined':
    program += "time.sleep(0.7); "
if phase == 'logs':
    program += "sys.stdout.buffer.write(b'o'*100000); sys.stdout.flush(); sys.stderr.buffer.write(b'e'*100000); sys.stderr.flush(); "
if phase == 'failed-logs':
    program += "sys.stdout.buffer.write(b'o'*100000); sys.stdout.flush(); sys.stderr.buffer.write(b'e'*100000); sys.stderr.flush(); sys.exit(7); "
if phase == 'exact-logs':
    program += "sys.stdout.buffer.write(b'o'*512); sys.stdout.flush(); sys.stderr.buffer.write(b'e'*512); sys.stderr.flush(); "
if phase == 'timeout-logs':
    program += "sys.stdout.buffer.write(b'o'*100000); sys.stdout.flush(); sys.stderr.buffer.write(b'e'*100000); sys.stderr.flush(); time.sleep(20); "
if phase == 'timeout-small-logs':
    program += "sys.stdout.buffer.write(b'out'); sys.stdout.flush(); sys.stderr.buffer.write(b'err'); sys.stderr.flush(); time.sleep(20); "
if phase == 'descendant':
    program += "import subprocess; child = subprocess.Popen([sys.executable, '-c', 'import time; time.sleep(20)'], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL); Path('descendant.pid').write_text(str(child.pid)); "
program += "Path('capture.ppm').write_bytes(b'P6\\n64 64\\n255\\n'+bytes(64*64*3))"
runner.command_for = lambda *args: [sys.executable, '-c', program]
runner._worker_command = lambda request, output: [sys.executable, __file__, sys.argv[1], phase, str(request), str(output), 'worker']
if sys.argv[-1] == 'worker':
    result = runner._run_capture(Path(request), Path(output), supervised=True)
else:
    result = runner.run_capture(Path(request), Path(output))
print(json.dumps(result))
'''


class RunnerLimitTests(unittest.TestCase):
    def capture(self, root, phase, timeout=6):
        profile = 'linux' if sys.platform.startswith('linux') else 'windows'
        document = request_document(root, profile=profile)
        if profile == 'linux':
            document['graphicsBackend'] = 'sdl-offscreen'
        document.update(engineTimeoutMs=1000, runnerTimeoutMs=1000 if phase in
                        ('preparation', 'collection', 'combined', 'timeout-logs',
                         'timeout-small-logs') else 5000,
                        logLimitBytes=1024)
        request = root / 'input.json'
        request.write_text(json.dumps(document), encoding='utf-8')
        harness = root / 'harness.py'
        # Legacy sidecars remain optional. The synthetic engine has no graphics
        # report, so substitute only that observation validation boundary.
        harness.write_text(HARNESS.replace('runner.command_for =',
            "original_validate = runner.validate_capture\n"
            "runner.validate_capture = lambda output, request: original_validate(output, dict(request, graphicsBackend='xvfb'))\n"
            'runner.command_for ='), encoding='utf-8')
        command = [sys.executable, str(harness), str(Path(runner.__file__).parent),
                   phase, str(request), str(root / 'out')]
        kwargs = {'start_new_session': True} if os.name == 'posix' else {
            'creationflags': subprocess.CREATE_NEW_PROCESS_GROUP}
        process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, **kwargs)
        start = time.monotonic()
        try:
            stdout, stderr = process.communicate(timeout=timeout)
        except subprocess.TimeoutExpired:
            runner.kill_process_tree(process)
            stdout, stderr = process.communicate(timeout=3)
            self.fail(f'Runner failed to enforce its 1000ms deadline during {phase}; '
                      f'outer test guard stopped the owned process: {stderr!r}')
        elapsed = time.monotonic() - start
        self.assertEqual(process.returncode, 0, stderr.decode(errors='replace'))
        receipt = json.loads(stdout)
        self.assertEqual(json.loads((root / 'out/launcher-receipt.json').read_text()), receipt)
        return receipt, elapsed

    def test_stalled_preparation_is_stopped_with_a_failure_receipt(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            receipt, elapsed = self.capture(root, 'preparation')
            self.assertEqual((root / 'out/entered-phase').read_text(), 'preparation')
            self.assertTrue(receipt.get('runnerTimedOut'), receipt)
            self.assertEqual(receipt['status'], 'failed')
            self.assertEqual(receipt['failureStage'], 'staging')
            self.assertFalse(receipt['engineStarted'])
            self.assertIsNone(receipt.get('inputsUnchanged'))
            self.assertLess(elapsed, 5)

    def test_stalled_identity_collection_does_not_escape_the_total_deadline(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            receipt, elapsed = self.capture(root, 'collection')
            self.assertEqual((root / 'out/entered-phase').read_text(), 'collection')
            self.assertTrue(receipt.get('runnerTimedOut'), receipt)
            self.assertEqual(receipt['status'], 'failed')
            self.assertEqual(receipt['failureStage'], 'post-run-identity')
            self.assertFalse(receipt.get('identityCheckComplete', True))
            self.assertIsNone(receipt.get('inputsUnchanged'))
            self.assertLess(elapsed, 5)

    def test_preparation_and_engine_share_one_budget(self):
        with tempfile.TemporaryDirectory() as tmp:
            receipt, elapsed = self.capture(Path(tmp), 'combined')
            self.assertTrue(receipt.get('runnerTimedOut'), receipt)
            self.assertEqual(receipt['status'], 'failed')
            self.assertEqual(receipt['failureStage'], 'execution')
            self.assertLess(elapsed, 5)

    def test_stdout_and_stderr_share_one_retained_byte_limit(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            receipt, _ = self.capture(root, 'logs')
            self.assertEqual(receipt['status'], 'completed', receipt)
            logs = [root / 'out' / name for name in ('engine-stdout.log', 'engine-stderr.log')]
            self.assertEqual(sum(path.stat().st_size for path in logs), 1024)
            self.assertEqual(sum(receipt[path.name]['bytesObserved'] for path in logs), 200000)
            self.assertTrue(any(receipt[path.name]['truncatedForReceipt'] for path in logs))
            self.assertTrue(receipt['inputsUnchanged'])

    def test_failed_engine_keeps_bounded_logs_and_the_exit_reason(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            receipt, _ = self.capture(root, 'failed-logs')
            self.assertEqual(receipt['status'], 'failed')
            self.assertEqual(receipt['processExitCode'], 7)
            self.assertEqual(receipt['failureStage'], 'execution')
            self.assertEqual(sum((root / 'out' / name).stat().st_size for name in
                                 ('engine-stdout.log', 'engine-stderr.log')), 1024)
            self.assertIn('engine-stdout.log', receipt)
            self.assertIn('engine-stderr.log', receipt)
            self.assertIn('Engine exited with code 7', receipt['error'])

    def test_exact_limit_is_not_reported_as_truncated(self):
        with tempfile.TemporaryDirectory() as tmp:
            receipt, _ = self.capture(Path(tmp), 'exact-logs')
            self.assertEqual(receipt['status'], 'completed', receipt)
            for name in ('engine-stdout.log', 'engine-stderr.log'):
                self.assertEqual(receipt[name]['bytes'], 512)
                self.assertEqual(receipt[name]['bytesObserved'], 512)
                self.assertFalse(receipt[name]['truncatedForReceipt'])

    def test_runner_timeout_still_records_bounded_log_evidence(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            receipt, _ = self.capture(root, 'timeout-logs')
            self.assertTrue(receipt['runnerTimedOut'])
            for name in ('engine-stdout.log', 'engine-stderr.log'):
                self.assertIn(name, receipt)
                data = (root / 'out' / name).read_bytes()
                self.assertEqual(receipt[name]['sha256'], runner.sha256_bytes(data))
                self.assertEqual(receipt[name]['bytes'], len(data))
            self.assertEqual(sum(receipt[name]['bytes'] for name in
                                 ('engine-stdout.log', 'engine-stderr.log')), 1024)

    def test_identity_read_failure_cannot_complete_or_claim_unchanged(self):
        with tempfile.TemporaryDirectory() as tmp:
            receipt, _ = self.capture(Path(tmp), 'collection-failure')
            self.assertEqual(receipt['status'], 'failed')
            self.assertEqual(receipt['failureStage'], 'post-run-identity')
            self.assertFalse(receipt['identityCheckComplete'])
            self.assertIsNone(receipt['inputsUnchanged'])
            self.assertIn('identity read failure', receipt['error'])

    def test_timeout_preserves_small_flushed_diagnostics_without_waiting_for_a_full_chunk(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            receipt, _ = self.capture(root, 'timeout-small-logs')
            self.assertTrue(receipt['runnerTimedOut'])
            self.assertEqual((root / 'out/engine-stdout.log').read_bytes(), b'out')
            self.assertEqual((root / 'out/engine-stderr.log').read_bytes(), b'err')

    def test_owned_descendants_are_reaped_without_touching_an_unrelated_process(self):
        unrelated = subprocess.Popen([sys.executable, '-c', 'import time; time.sleep(20)'],
                                     stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        try:
            with tempfile.TemporaryDirectory() as tmp:
                root = Path(tmp)
                receipt, _ = self.capture(root, 'descendant')
                self.assertEqual(receipt['status'], 'completed', receipt)
                self.assertIsNone(unrelated.poll())
                pid = int((root / 'out/descendant.pid').read_text())
                if os.name == 'nt':
                    import ctypes
                    from ctypes import wintypes
                    api = ctypes.WinDLL('kernel32', use_last_error=True)
                    api.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
                    api.OpenProcess.restype = wintypes.HANDLE
                    api.WaitForSingleObject.argtypes = [wintypes.HANDLE, wintypes.DWORD]
                    api.WaitForSingleObject.restype = wintypes.DWORD
                    api.CloseHandle.argtypes = [wintypes.HANDLE]
                    handle = api.OpenProcess(0x100000, False, pid)
                    if handle:
                        try:
                            self.assertEqual(api.WaitForSingleObject(handle, 1000), 0)
                        finally:
                            api.CloseHandle(handle)
                    else:
                        self.assertEqual(ctypes.get_last_error(), 87)
                else:
                    try:
                        os.kill(pid, 0)
                    except ProcessLookupError:
                        pass
                    else:
                        # A dead orphan can remain a zombie until the host init
                        # reaps it; it cannot execute or hold pipe descriptors.
                        if sys.platform.startswith('linux'):
                            self.assertEqual(Path(f'/proc/{pid}/stat').read_text().split(') ')[1][0], 'Z')
                        else:
                            # Darwin has no /proc. Query only the owned PID;
                            # disappearance during the observation is also dead.
                            observed = subprocess.run(['ps', '-p', str(pid), '-o', 'stat='],
                                                      capture_output=True, text=True, timeout=3)
                            self.assertIn(observed.returncode, (0, 1), observed.stderr)
                            self.assertTrue(observed.stdout.strip().startswith('Z') or
                                            (observed.returncode == 1 and not observed.stdout.strip()),
                                            observed.stdout)
        finally:
            unrelated.kill()
            unrelated.wait(timeout=3)

    def test_worker_start_failure_keeps_existing_outputs_and_a_failed_receipt(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            profile = 'linux' if sys.platform.startswith('linux') else 'windows'
            request = root / 'input.json'
            request.write_text(json.dumps(request_document(root, profile=profile)))
            with mock.patch.object(runner, 'profile_runs_here', return_value=True), \
                    mock.patch.object(runner.WorkerTree, 'start', side_effect=OSError('owned spawn failed')):
                receipt = runner.run_capture(request, root / 'out')
            self.assertEqual(receipt['status'], 'failed')
            self.assertFalse(receipt['engineStarted'])
            self.assertIn('owned spawn failed', receipt['error'])
            original = (root / 'out/launcher-receipt.json').read_bytes()
            with mock.patch.object(runner, 'profile_runs_here', return_value=True), \
                    self.assertRaisesRegex(runner.CaptureError, 'already exists'):
                runner.run_capture(request, root / 'out')
            self.assertEqual((root / 'out/launcher-receipt.json').read_bytes(), original)

    def test_cleanup_failure_is_not_hidden_by_the_runner_timeout(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            profile = 'linux' if sys.platform.startswith('linux') else 'windows'
            request = root / 'input.json'
            request.write_text(json.dumps(request_document(root, profile=profile)))
            tree = mock.Mock()
            tree.start.return_value.wait.side_effect = subprocess.TimeoutExpired('owned worker', 1)
            tree.close.side_effect = runner.CaptureError('owned cleanup failed')
            with mock.patch.object(runner, 'profile_runs_here', return_value=True), \
                    mock.patch.object(runner, 'WorkerTree', return_value=tree):
                receipt = runner.run_capture(request, root / 'out')
            self.assertTrue(receipt['runnerTimedOut'])
            self.assertFalse(receipt.get('cleanupComplete', True))
            self.assertIn('owned cleanup failed', receipt.get('cleanupError', ''))

    def test_actual_cli_reports_hash_failure_or_unsupported_host_without_starting_engine(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            profile = 'linux' if sys.platform.startswith('linux') else 'windows'
            document = request_document(root, profile=profile)
            document['files'][0]['sha256'] = '0' * 64
            request = root / 'input.json'
            request.write_text(json.dumps(document))
            result = subprocess.run([sys.executable, runner.__file__, str(request),
                                     '--output', str(root / 'out')],
                                    capture_output=True, text=True, timeout=6)
            if not runner.profile_runs_here(profile):
                self.assertEqual(result.returncode, 2, result.stderr)
                refusal = json.loads(result.stderr)
                self.assertEqual(refusal['status'], 'refused')
                self.assertIn('does not run on this OS', refusal['error'])
                self.assertFalse((root / 'out').exists())
                return
            self.assertEqual(result.returncode, 1, result.stderr)
            receipt = json.loads((root / 'out/launcher-receipt.json').read_text())
            self.assertEqual(receipt['status'], 'failed')
            self.assertIn('Source hash mismatch', receipt['error'])
            self.assertFalse(receipt['engineStarted'])

    def test_darwin_cli_admission_refuses_both_profiles_before_creating_output(self):
        for profile in ('linux', 'windows'):
            with self.subTest(profile=profile), tempfile.TemporaryDirectory() as tmp:
                root = Path(tmp)
                request = root / 'input.json'
                request.write_text(json.dumps(request_document(root, profile=profile)))
                if sys.platform == 'darwin':
                    # On macOS this is the unmodified production command.
                    command = [sys.executable, runner.__file__]
                else:
                    # Exercise the same CLI boundary with Darwin platform facts;
                    # do not replace the production profile-admission function.
                    driver = root / 'darwin-admission.py'
                    driver.write_text(
                        'import sys, types\n'
                        f'sys.path.insert(0, {str(Path(runner.__file__).parent)!r})\n'
                        'import invoke_engine_capture as runner\n'
                        'runner.os = types.SimpleNamespace(name="posix")\n'
                        'runner.sys = types.SimpleNamespace(platform="darwin", stderr=sys.stderr)\n'
                        'raise SystemExit(runner.main(sys.argv[1:]))\n')
                    command = [sys.executable, str(driver)]
                result = subprocess.run([*command, str(request), '--output', str(root / 'out')],
                                        capture_output=True, text=True, timeout=6)
                self.assertEqual(result.returncode, 2, result.stderr)
                refusal = json.loads(result.stderr)
                self.assertEqual(refusal, {'status': 'refused', 'error':
                    f'CaptureError: The {profile} engine profile does not run on this OS.'})
                self.assertEqual(result.stdout, '')
                self.assertFalse((root / 'out').exists())


if __name__ == '__main__':
    unittest.main()
