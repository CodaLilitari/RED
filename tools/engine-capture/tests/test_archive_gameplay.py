"""Archive admission and ordinary-play regressions. All engine fixtures are synthetic."""
import copy
import json
import os
import struct
import sys
import tempfile
import threading
import time
import unittest
from pathlib import Path
from unittest import mock

from test_invoke_engine_capture import request_document, make_source, sha, synthetic_graphics
import invoke_engine_capture as runner


def gob_bytes(entries):
    # Literal JK GOB v0x14 layout; no production archive writer in this oracle.
    offset = 16 + 136 * len(entries)
    index, payload = bytearray(), bytearray()
    for name, data in entries:
        index.extend(struct.pack('<II128s', offset, len(data), name.encode('ascii')))
        payload.extend(data)
        offset += len(data)
    return b'GOB ' + struct.pack('<III', 20, 12, len(entries)) + index + payload


def archive_document(root, entries=None):
    doc = request_document(root)
    doc['files'] = [item for item in doc['files'] if item['role'] != 'scenario']
    data = gob_bytes(entries if entries is not None else
                     [('episode.jk', b'episode'), ('jkl\\level.jkl', b'level')])
    doc['files'].append({'source': str(make_source(root, 'src/test.gob', data)),
                         'destination': 'episode/test.gob', 'sha256': sha(data), 'role': 'scenario'})
    doc['episodeArchive'] = 'episode/test.gob'
    return doc


def gameplay_document(root):
    doc = archive_document(root)
    marker = struct.pack('<I', 0x699232c4)
    doc['files'].append({'source': str(make_source(root, 'src/jk_.cd', marker)),
                         'destination': 'resource/jk_.cd', 'sha256': sha(marker), 'role': 'resource'})
    doc['motsCompat'] = False
    doc['gameplay'] = {'schemaVersion': 1, 'name': 'ordinary-movement-use',
                       'launch': {'game': 'jk', 'episode': 'test', 'map': 'level.jkl'},
                       'steps': [{'op': 'wait', 'expect': {'levelActive': True}},
                                 {'op': 'checkpoint', 'name': 'spawn', 'capture': True},
                                 {'op': 'tap', 'key': 'up', 'seconds': 0.1},
                                 {'op': 'tap', 'key': 'space', 'seconds': 0.1},
                                 {'op': 'assert', 'distanceFrom': {'checkpoint': 'spawn', 'min': 0.01}},
                                 {'op': 'checkpoint', 'name': 'after-use', 'capture': True}]}
    return doc


class ArchiveAdmissionTests(unittest.TestCase):
    def test_archive_only_is_admitted_without_loose_episode_or_map(self):
        with tempfile.TemporaryDirectory() as temp:
            doc = archive_document(Path(temp))
            admitted = runner.validate_request(doc)
            self.assertEqual(admitted['episodeArchive'], 'episode/test.gob')
            self.assertEqual([f['destination'] for f in admitted['files'] if f['role'] == 'scenario'],
                             ['episode/test.gob'])

    def test_selected_archive_must_match_and_loose_overrides_are_refused(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            good = archive_document(root)
            for name in ('episode/other.gob', '../test.gob', 'episode/test.zip', None):
                doc = copy.deepcopy(good)
                doc['episodeArchive'] = name
                with self.subTest(name=name), self.assertRaises(runner.CaptureError):
                    runner.validate_request(doc)
            doc = copy.deepcopy(good)
            doc['files'].extend(f for f in request_document(root)['files'] if f['role'] == 'scenario')
            with self.assertRaises(runner.CaptureError):
                runner.validate_request(doc)

    def test_archive_directory_has_canonical_required_entries_and_bounded_ranges(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            doc = archive_document(root)
            request = runner.validate_request(doc)
            report = runner.validate_episode_archive(request, Path(doc['files'][-1]['source']))
            self.assertEqual(report['entries'], 2)
            self.assertEqual(report['mapEntry'], 'jkl\\level.jkl')
            for entries in ([('episode.jk', b'x')],
                            [('episode.jk', b'x'), ('jkl\\LEVEL.JKL', b'y')],
                            [('episode.jk', b'x'), ('jkl\\level.jkl', b'y'), ('../escape', b'z')],
                            [('episode.jk', b'x'), ('jkl\\level.jkl', b'y'), ('episode.jk', b'z')]):
                source = make_source(root, 'bad.gob', gob_bytes(entries))
                with self.subTest(entries=entries), self.assertRaises(runner.CaptureError):
                    runner.validate_episode_archive(request, source)
            for data in (b'GOB ' + struct.pack('<III', 20, 12, 65537),
                         b'GOB ' + struct.pack('<III', 20, 12, 2),
                         gob_bytes([('episode.jk', b'x'), ('jkl\\level.jkl', b'y')])[:-1]):
                source = make_source(root, 'truncated.gob', data)
                with self.assertRaises(runner.CaptureError):
                    runner.validate_episode_archive(request, source)

    def test_invalid_archive_fails_before_engine_start_and_keeps_receipt(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            doc = archive_document(root, [('episode.jk', b'no level')])
            path = root / 'request.json'
            path.write_text(json.dumps(doc))
            # This boundary is synthetic; profile admission itself is tested separately.
            with mock.patch.object(runner, 'execute_engine') as execute, \
                    mock.patch.object(runner, 'profile_runs_here', return_value=True):
                result = runner._run_capture(path, root / 'out')
            execute.assert_not_called()
            self.assertEqual(result['status'], 'failed')
            self.assertFalse(result['engineStarted'])
            self.assertIn('map', result['error'].lower())

    def test_archive_config_writes_are_explicit_but_source_asset_and_legacy_guards_remain(self):
        for kind in ('archive-config', 'archive-source', 'archive-asset', 'legacy-config'):
            with self.subTest(kind=kind), tempfile.TemporaryDirectory() as temp:
                root = Path(temp)
                doc = request_document(root) if kind == 'legacy-config' else archive_document(root)
                doc['graphicsBackend'] = 'sdl-offscreen'
                path = root / 'input.json'
                path.write_text(json.dumps(doc))
                def execute(*args, **kwargs):
                    stage = args[1]
                    (stage / 'capture.ppm').write_bytes(b'P6\n64 64\n255\n' + b'\0' * (64*64*3))
                    (stage / 'capture.ppm.json').write_text(json.dumps({'graphics': synthetic_graphics()}))
                    (stage / 'registry.json').write_text('{"runtime":true}')
                    if kind == 'archive-source':
                        config = next(f for f in doc['files'] if f['destination'] == 'registry.json')
                        Path(config['source']).write_text('{"sourceTampered":true}')
                    if kind == 'archive-asset':
                        (stage / 'episode/test.gob').write_bytes(b'changed asset')
                    kwargs['on_start'](123)
                    return {'processId': 123, 'processExitCode': 0, 'timedOut': False}
                with mock.patch.object(runner, 'execute_engine', side_effect=execute), \
                        mock.patch.object(runner, 'profile_runs_here', return_value=True):
                    result = runner._run_capture(path, root / 'out')
                self.assertFalse(result['inputsUnchanged'])
                self.assertTrue(result['identityCheckComplete'])
                if kind == 'archive-config':
                    self.assertEqual(result['status'], 'completed', result)
                    self.assertTrue(result['immutableInputsUnchanged'])
                    self.assertEqual(result['stagedConfigChanges'], ['registry.json'])
                    self.assertEqual(result['mutableConfigDestinations'], ['registry.json', 'openjkdf2_cvars.json'])
                else:
                    self.assertEqual(result['status'], 'failed')
                    self.assertEqual(result['failureStage'], 'post-run-identity')
                    if kind != 'legacy-config':
                        self.assertFalse(result['immutableInputsUnchanged'])
                    else:
                        self.assertNotIn('mutableConfigDestinations', result)


class GameplayAdmissionTests(unittest.TestCase):
    def test_gameplay_does_not_require_a_dummy_posed_camera(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            doc = gameplay_document(root)
            del doc['camera']
            admitted = runner.validate_request(doc)
            self.assertNotIn('camera', admitted)
            self.assertNotIn('requestedCamera', runner.initial_receipt(admitted, root))
            del doc['gameplay']
            with self.assertRaises(runner.CaptureError):
                runner.validate_request(doc)

    def test_request_byte_budget_and_duplicate_fields_refuse_before_output(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            path = root / 'input.json'
            path.write_bytes(b' ' * (2 * 1024 * 1024 + 1))
            with self.assertRaisesRegex(runner.CaptureError, '2 MiB'):
                runner.run_capture(path, root / 'out')
            self.assertFalse((root / 'out').exists())
            doc = gameplay_document(root)
            raw = json.dumps(doc)
            path.write_text('{"episode":"shadow",' + raw[1:])
            with self.assertRaisesRegex(runner.CaptureError, 'Duplicate'):
                runner.run_capture(path, root / 'out')
            self.assertFalse((root / 'out').exists())

    def test_ordinary_recipe_has_agent_command_and_no_posed_camera_claim(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            request = runner.validate_request(gameplay_document(root))
            command = runner.command_for(request, root)
            self.assertIn('-agent', command)
            self.assertNotIn('-capture', command)
            self.assertNotIn('-noHUD', command)
            self.assertNotIn('-motsCompat', command)
            receipt = runner.initial_receipt(request, root)
            self.assertEqual(receipt['captureMode'], 'gameplay')
            self.assertNotIn('requestedCamera', receipt)
            self.assertNotIn('requestedFrame', receipt)
            self.assertFalse(receipt['physicsOverrides'])
            self.assertEqual(receipt['acceptance'], 'unverified')

    def test_injection_unknown_fields_launch_mismatch_and_limits_are_refused(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            good = gameplay_document(root)
            invalid = [dict(op='command', value='cog activate 1'),
                       dict(op='command', value='teleport 1 2 3'),
                       dict(op='command', value='key 26 2'),
                       dict(op='command', value='mouse 2049 0'),
                       dict(op='tap', key='space', seconds=2),
                       dict(op='sleep', seconds=float('nan')),
                       dict(op='assert', expect={'player': {'notNull': False}}),
                       dict(op='assert', distanceFrom={'checkpoint': 'future', 'min': 1}),
                       dict(op='tap', key='up', shell='echo wrong')]
            for step in invalid:
                doc = copy.deepcopy(good)
                doc['gameplay']['steps'] = [step]
                with self.subTest(step=step), self.assertRaises(runner.CaptureError):
                    runner.validate_request(doc)
            for field, value in (('steps', [{'op': 'sleep', 'seconds': .1}] * 257),
                                 ('steps', [{'op': 'checkpoint', 'name': 'c' + str(i)} for i in range(32)]),
                                 ('launch', {'game': 'jk', 'episode': 'other', 'map': 'level.jkl'})):
                doc = copy.deepcopy(good)
                doc['gameplay'][field] = value
                with self.subTest(field=field), self.assertRaises(runner.CaptureError):
                    runner.validate_request(doc)

    def test_gameplay_is_linux_only_and_requires_explicit_install_marker(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            doc = gameplay_document(root)
            doc['files'] = [f for f in doc['files'] if f['destination'] != 'resource/jk_.cd']
            with self.assertRaisesRegex(runner.CaptureError, 'marker|jk_.cd'):
                runner.validate_request(doc)
            doc = gameplay_document(root)
            doc['files'] = [f for f in doc['files'] if f['role'] != 'engine']
            doc['files'].extend(f for f in request_document(root, profile='windows')['files']
                                if f['role'] in ('engine', 'runtime'))
            with self.assertRaisesRegex(runner.CaptureError, 'Linux'):
                runner.validate_request(doc)


FAKE_ENGINE = r'''
import json, os, sys, time
from pathlib import Path
assert '-agent' in sys.argv and '-capture' not in sys.argv
assert 'OPENJKDF2_CAPTURE_REQUEST' not in os.environ
assert not Path('episode/test').exists()
agent = Path(os.environ['OPENJKDF2_AGENT_DIR'])
position = 0.0
uses = 0
while True:
    mailbox = agent / 'command.txt'
    if not mailbox.exists():
        time.sleep(.002)
        continue
    data = mailbox.read_text().splitlines()
    mailbox.unlink()
    assert data[0] == 'openjkdf2-agent-v1'
    identity, command = int(data[1]), data[2]
    verb = command.split(' ')[0]
    result = {}
    if command == 'ping':
        result = {'protocol': 'openjkdf2-agent-v1', 'leaseMs': 2000, 'maxCaptureWaitMs': 10000}
    elif command == 'state':
        result = {'levelActive': True, 'player': {'position': [position,0,0]}, 'actualUse': uses}
    elif command == 'key 82 0':
        position += .1
    elif command == 'key 44 1':
        uses += 1
    elif command == 'capture':
        name = 'frame-%d.ppm' % identity
        (agent / name).write_bytes(b'P6\n64 64\n255\n' + b'\x12\x34\x56' * (64*64))
        result = {'file': name, 'width': 64, 'height': 64, 'frame': identity}
    response = {'schemaVersion': 1, 'id': identity, 'ok': True, 'command': verb, 'result': result}
    temporary = agent / '.response.tmp'
    temporary.write_text(json.dumps(response))
    temporary.replace(agent / 'response.json')
    if command == 'quit':
        Path('registry.json').write_text('{"runtimeConfigChanged":true}')
        sys.stdout.write('o'*12000)
        sys.stderr.write('e'*12000)
        break
'''


def executable_document(root):
    doc = gameplay_document(root)
    data = ('#!' + sys.executable + '\n' + FAKE_ENGINE).encode()
    source = make_source(root, 'src/openjkdf2', data)
    doc['files'][0].update(source=str(source), sha256=sha(data))
    doc.update(graphicsBackend='sdl-offscreen', engineTimeoutMs=3000,
               runnerTimeoutMs=5000, logLimitBytes=1024)
    return doc


@unittest.skipUnless(sys.platform.startswith('linux'), 'native Linux agent protocol')
class GameplayExecutionTests(unittest.TestCase):
    def test_timeout_and_cancel_revoke_stale_completed_immutable_identity(self):
        for cancel_run in (False, True):
            with self.subTest(cancel=cancel_run), tempfile.TemporaryDirectory() as temp:
                root = Path(temp)
                doc = executable_document(root)
                doc.update(engineTimeoutMs=1000, runnerTimeoutMs=2000)
                request_path = root / 'request.json'
                request_path.write_text(json.dumps(doc))
                checkpoint = runner.initial_receipt(runner.validate_request(doc), root / 'out')
                checkpoint.update(status='completed', failureStage=None, identityCheckComplete=True,
                                  inputsUnchanged=False, immutableInputsUnchanged=True,
                                  stagedConfigChanges=['registry.json'])
                program = ('import sys,time; from pathlib import Path; '
                           'sys.stdin.buffer.readline(); '
                           'Path(sys.argv[1]).write_text(sys.argv[2]); time.sleep(10)')
                def worker(admitted, output):
                    return [sys.executable, '-c', program, str(output / '.capture-progress.json'), json.dumps(checkpoint)]
                cancel = threading.Event()
                def when_checkpoint_exists():
                    until = time.monotonic() + 3
                    while time.monotonic() < until and not (root / 'out/.capture-progress.json').exists():
                        time.sleep(.01)
                    # Wait for the worker's completed checkpoint, not the parent's initial one.
                    while time.monotonic() < until:
                        try:
                            value = json.loads((root / 'out/.capture-progress.json').read_text())
                            if value.get('immutableInputsUnchanged') is True:
                                break
                        except (OSError, ValueError):
                            pass
                        time.sleep(.01)
                    cancel.set()
                thread = threading.Thread(target=when_checkpoint_exists) if cancel_run else None
                if thread:
                    thread.start()
                try:
                    with mock.patch.object(runner, '_worker_command', side_effect=worker):
                        result = runner.run_capture(request_path, root / 'out', cancel_event=cancel if cancel_run else None)
                finally:
                    if thread:
                        thread.join(timeout=4)
                self.assertEqual(result['status'], 'failed')
                self.assertFalse(result['identityCheckComplete'])
                self.assertIsNone(result['inputsUnchanged'])
                self.assertIsNone(result['immutableInputsUnchanged'])
                self.assertTrue(result['cancelled'] if cancel_run else result['runnerTimedOut'])

    def test_packaged_ordinary_play_preserves_journal_captures_and_truthful_config_identity(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            doc = executable_document(root)
            path = root / 'input.json'
            path.write_text(json.dumps(doc))
            result = runner.run_capture(path, root / 'out')
            self.assertEqual(result['status'], 'completed', result)
            self.assertEqual(result['processExitCode'], 0)
            self.assertEqual(result['gameplay']['status'], 'passed')
            self.assertEqual(result['acceptance'], 'unverified')
            self.assertFalse(result['graphicsBackendObserved'])
            self.assertTrue(result['immutableInputsUnchanged'])
            self.assertFalse(result['inputsUnchanged'])
            self.assertEqual(result['stagedConfigChanges'], ['registry.json'])
            self.assertEqual(sum(result[name]['bytes'] for name in ('engine-stdout.log', 'engine-stderr.log')), 1024)
            self.assertFalse((root / 'out/episode/test').exists())
            self.assertFalse((root / 'out/capture.ppm').exists())
            journal = [json.loads(line) for line in (root / 'out/agent/commands.jsonl').read_text().splitlines()]
            commands = [row['command'] for row in journal]
            for command in ('key 82 1', 'key 82 0', 'key 44 1', 'key 44 0', 'release', 'quit'):
                self.assertIn(command, commands)
            self.assertEqual(commands[-2:], ['release', 'quit'])
            self.assertEqual(journal[-1]['id'], result['gameplay']['commands'])
            self.assertTrue(all(row['id'] == row['response']['id'] for row in journal))
            for capture in result['gameplay']['captures']:
                self.assertEqual(capture['sha256'], sha((root / 'out' / capture['path']).read_bytes()))
            self.assertEqual(len(result['gameplay']['captures']), 2)

    def test_failed_assertion_is_failed_and_retains_native_observations(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            doc = executable_document(root)
            doc['gameplay']['steps'] = [{'op': 'assert', 'expect': {'actualUse': 99}}]
            path = root / 'input.json'
            path.write_text(json.dumps(doc))
            result = runner.run_capture(path, root / 'out')
            self.assertEqual(result['status'], 'failed')
            self.assertIn('Assertion failed', result['error'])
            self.assertEqual(result['failureStage'], 'execution')
            self.assertTrue(result['immutableInputsUnchanged'])
            scenario = json.loads((root / 'out/scenario.json').read_text())
            self.assertEqual(scenario['status'], 'failed')
            self.assertEqual(scenario['steps'][0]['mismatches'][0]['actual'], 0)
            self.assertTrue((root / 'out/agent/commands.jsonl').is_file())

    def test_cancel_stops_owned_gameplay_worker_without_promoting_partial_evidence(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            doc = executable_document(root)
            doc['gameplay']['steps'] = [{'op': 'sleep', 'seconds': 10}]
            path = root / 'input.json'
            path.write_text(json.dumps(doc))
            cancel = threading.Event()
            timer = threading.Timer(.3, cancel.set)
            timer.start()
            try:
                result = runner.run_capture(path, root / 'out', cancel_event=cancel)
            finally:
                timer.cancel()
            self.assertEqual(result['status'], 'failed')
            self.assertTrue(result['cancelled'])
            self.assertFalse(result['identityCheckComplete'])
            self.assertIsNone(result['inputsUnchanged'])

    def test_client_bounds_and_unsafe_reply_fail_before_unbounded_reads(self):
        import gameplay_capture as gameplay
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root / 'agent').mkdir()
            client = gameplay.Client(root, mock.Mock(poll=lambda: None), time.monotonic() + 1)
            client.sequence = gameplay.MAX_COMMANDS
            with self.assertRaisesRegex(gameplay.AgentError, '4096'):
                client.request('key 26 1')
            self.assertFalse((root / 'agent/command.txt').exists())
            client.sequence = 0
            client.journal_bytes = gameplay.MAX_JOURNAL
            with self.assertRaisesRegex(gameplay.AgentError, '16 MiB'):
                client.request('key 26 1')
            self.assertFalse((root / 'agent/command.txt').exists())
            reply = root / 'agent/response.json'
            target = root / 'outside.json'
            target.write_text('{}')
            reply.symlink_to(target)
            with self.assertRaises(gameplay.AgentError):
                gameplay.read_json(reply)
            reply.unlink()
            os.mkfifo(reply)
            with self.assertRaises(gameplay.AgentError):
                gameplay.read_json(reply)


if __name__ == '__main__':
    unittest.main()
