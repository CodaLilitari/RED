"""Controller coverage and real child-process tests with synthetic native replies."""
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

from test_archive_gameplay import executable_document, runner, sha
import gameplay_capture as gameplay


def observation(**overrides):
    return {'op': 'observe', 'name': 'above-floor', 'seconds': .25, 'maxGapSeconds': .2,
                 'expect': {'levelActive': True, 'player.position.2': {'min': -.5}}, **overrides}


class ObservationTests(unittest.TestCase):
    def test_interval_admission(self):
        with tempfile.TemporaryDirectory() as temp:
            doc = executable_document(Path(temp))
            doc['gameplay']['steps'] = [observation()]
            self.assertEqual(runner.validate_request(doc)['gameplay']['steps'], [observation()])
            for changes in ({'seconds': 0}, {'seconds': 31}, {'seconds': True},
                            {'maxGapSeconds': .01}, {'maxGapSeconds': 2.1}, {'expect': {}},
                            {'maxGapSeconds': float('inf')}, {'maxGapSeconds': None}, {'retryTap': {}}):
                with self.subTest(changes=changes), self.assertRaises(runner.CaptureError):
                    doc['gameplay']['steps'] = [observation(**changes)]
                    runner.validate_request(doc)

    def exercise(self, replies):
        clock = [100.]
        def advance(seconds):
            clock[0] += seconds
        replies = iter(replies)
        client = mock.Mock()
        def request(command, timeout):
            self.assertEqual(command, 'state')
            delay, height = next(replies)
            advance(delay)
            state = {'levelActive': True, 'player': {'position': [0, 0, height]}}
            return {'id': client.request.call_count, 'result': state}
        client.request.side_effect = request
        controller = gameplay.ScenarioRunner({}, {}, '.', 120., (), client)
        controller.row = {}
        with mock.patch.object(gameplay.time, 'monotonic', side_effect=lambda: clock[0]), \
                mock.patch.object(controller, 'sleep', side_effect=advance):
            with self.assertRaises(gameplay.AgentError):
                controller.execute(observation())
        return controller.row

    def test_intermediate_violation_and_unavailable_numeric_field(self):
        for height, expected in [(-1, 'failed'), (None, 'inconclusive'), (True, 'inconclusive')]:
            with self.subTest(height=height):
                row = self.exercise([(.01, 0), (.01, height), (.01, 0)] * 5)
                self.assertEqual(row['observation']['outcome'], expected)
                self.assertEqual(len(row['observation']['samples']), 2)

    def test_late_terminal_violation_retains_evidence_but_is_inconclusive(self):
        row = self.exercise([(.01, 0)] * 4 + [(.3, -1)])
        evidence = row['observation']
        self.assertGreaterEqual(evidence['samples'][-1]['sentSeconds']
                                - evidence['samples'][0]['receivedSeconds'], .25 - 1e-9)
        self.assertGreater(evidence['maxGapSeconds'], .2)
        self.assertEqual(row['mismatches'][0]['actual'], -1)
        self.assertEqual(evidence['outcome'], 'inconclusive')

    def test_wrong_boolean_type_is_inconclusive(self):
        controller = gameplay.ScenarioRunner({}, {}, '.', float('inf'), (), mock.Mock())
        controller.client.request.return_value = {'id': 2, 'result': {'levelActive': 1}}
        controller.row = {}
        with self.assertRaises(gameplay.AgentError):
            controller.execute(observation(expect={'levelActive': True}))
        self.assertEqual(controller.row['observation']['outcome'], 'inconclusive')

    def test_nonfinite_exponent_is_refused_at_the_native_reply_boundary(self):
        for invalid in [b'{"value":1e999}', b'{"value":NaN}', b'{"id":3,"id":4}']:
            with self.subTest(invalid=invalid), mock.patch.object(gameplay, 'read_regular', return_value=invalid):
                with self.assertRaises(gameplay.AgentError):
                    gameplay.read_json('response.json')

    def test_sparse_or_slow_first_reply_cannot_pass(self):
        for delays in [[.21], [.01, .21], [.09, .09]]:
            with self.subTest(delays=delays):
                row = self.exercise([(d, 0) for d in delays] + [(.01, 0)] * 20)
                self.assertEqual(row['observation']['outcome'], 'inconclusive')
                self.assertGreater(row['observation']['maxGapSeconds'], .2)


@unittest.skipUnless(sys.platform.startswith('linux'), 'native Linux agent protocol')
class ObservationExecutionTests(unittest.TestCase):
    def test_package_bound_pass_failure_and_inconclusive_run(self):
        for expected, fields in [('passed', {'player.position.2': {'min': -.5}}),
                                 ('failed', {'player.position.2': {'min': 1}}),
                                 ('inconclusive', {'door.open': True})]:
            with self.subTest(expected=expected), tempfile.TemporaryDirectory() as temp:
                root = Path(temp)
                doc = executable_document(root)
                doc['gameplay']['steps'] = [{'op': 'checkpoint', 'name': 'image'},
                                           observation(expect=fields)]
                path = root / 'input.json'
                path.write_text(json.dumps(doc, indent=2))
                out = root / 'out'
                result = runner.run_capture(path, out)
                self.assertEqual(result['status'], 'completed' if expected == 'passed' else 'failed')
                self.assertTrue(result['immutableInputsUnchanged'])
                summary = result['gameplay']
                self.assertEqual(summary['scenarioSha256'], sha((out / 'scenario.json').read_bytes()))
                scenario = json.loads((out / 'scenario.json').read_bytes())
                self.assertEqual(scenario['inputs'], result['gameplayInputs'])
                self.assertEqual(scenario['inputs']['requestSha256'], sha(path.read_bytes()))
                for dest, field in [('episode/test.gob', 'packageSha256'), ('openjkdf2', 'engineSha256')]:
                    self.assertEqual(scenario['inputs'][field], sha((out / dest).read_bytes()))
                self.assertEqual(summary['scenarioInputSha256'], sha((out / 'scenario-input.json').read_bytes()))
                self.assertEqual(summary['journal']['sha256'], sha((out / 'agent/commands.jsonl').read_bytes()))
                evidence = scenario['steps'][1]['observation']
                self.assertEqual(evidence['outcome'], expected)
                samples = evidence['samples']
                responses = {r['id']: r for r in map(json.loads, (out / 'agent/commands.jsonl').read_bytes().splitlines())}
                for sample in samples:
                    self.assertEqual(responses[sample['requestId']]['command'], 'state')
                    self.assertLessEqual(sample['sentSeconds'], sample['receivedSeconds'])
                if expected == 'passed':
                    self.assertGreaterEqual(samples[-1]['sentSeconds'] - samples[0]['receivedSeconds'], .25)
                    self.assertLessEqual(evidence['maxGapSeconds'], .2)
                    self.assertGreaterEqual(len(samples), 3)
                self.assertGreaterEqual(len(summary['captures']), 1)

    def test_replaced_handoff_fails_before_staging(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            doc = executable_document(root)
            doc['gameplay']['steps'] = [observation()]
            path = root / 'input.json'
            path.write_text(json.dumps(doc))
            command = runner._worker_command
            def replace_handoff(admitted, output):
                alternate = json.loads(admitted.read_bytes())
                alternate['gameplay']['name'] = 'different-admitted-recipe'
                admitted.write_text(json.dumps(alternate))
                return command(admitted, output)
            with mock.patch.object(runner, '_worker_command', side_effect=replace_handoff):
                result = runner.run_capture(path, root / 'out')
            self.assertEqual(result['status'], 'failed')
            self.assertFalse(result['engineStarted'])
            self.assertFalse((root / 'out/openjkdf2').exists())
            self.assertIn('admitted request', result['error'].lower())

    def test_large_overlapping_mismatches_have_terminal_report_and_raw_journal(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            doc = executable_document(root)
            fields = {'.'.join(['a'] * depth): {'min': 0} for depth in range(1, 33)}
            doc['gameplay']['steps'] = [observation(expect=fields)]
            engine = Path(doc['files'][0]['source'])
            code = engine.read_text().replace("    elif command == 'key 82 0':",
                "        result = 'x' * (600 * 1024)\n"
                "        for _ in range(32): result = {'a': result}\n"
                "    elif command == 'key 82 0':")
            engine.write_text(code)
            doc['files'][0]['sha256'] = sha(engine.read_bytes())
            path = root / 'input.json'
            path.write_text(json.dumps(doc))
            out = root / 'out'
            result = runner.run_capture(path, out)
            self.assertEqual(result['status'], 'failed')
            report = (out / 'scenario.json').read_bytes()
            scenario = json.loads(report)
            self.assertEqual(scenario['status'], 'failed')
            row = scenario['steps'][0]
            self.assertEqual(row['status'], 'failed')
            self.assertEqual(row['observation']['outcome'], 'inconclusive')
            self.assertEqual([failure['field'] for failure in row['mismatches']], list(fields))
            self.assertLess(len(json.dumps(row['mismatches'])), 16384)
            sample_id = row['observation']['samples'][-1]['requestId']
            journal = (out / 'agent/commands.jsonl').read_bytes()
            record = next(r for r in map(json.loads, journal.splitlines()) if r['id'] == sample_id)
            actual = record['response']['result']
            for failure in row['mismatches']:
                actual = actual['a']
                self.assertEqual(failure['actualType'], 'object' if isinstance(actual, dict) else 'string')
                self.assertEqual(failure['requestId'], sample_id)
            self.assertEqual(actual, 'x' * (600 * 1024))
            self.assertEqual(result['gameplay']['scenarioSha256'], sha(report))
            self.assertEqual(result['gameplay']['journal']['sha256'], sha(journal))

    def test_tampered_recipe_journal_or_package_invalidates_run(self):
        cases = {
            'admitted request': ("Path('.admitted-request.json').write_text('{}')", 'capture-validation'),
            'recipe': ("Path('scenario-input.json').write_text('{}')", 'capture-validation'),
            'journal': ("(agent / 'commands.jsonl').write_text('wrong')", 'capture-validation'),
            'package': ("Path('episode/test.gob').write_bytes(b'different')", 'post-run-identity'),
        }
        for name, (action, stage) in cases.items():
            with self.subTest(name=name), tempfile.TemporaryDirectory() as temp:
                root = Path(temp)
                doc = executable_document(root)
                doc['gameplay']['steps'] = [observation()]
                engine = Path(doc['files'][0]['source'])
                hook = "        Path('registry.json')"
                code = engine.read_text().replace(hook, f"        {action}\n{hook}")
                engine.write_text(code)
                doc['files'][0]['sha256'] = sha(engine.read_bytes())
                path = root / 'input.json'
                path.write_text(json.dumps(doc))
                result = runner.run_capture(path, root / 'out')
                self.assertEqual(result['status'], 'failed')
                self.assertEqual(result['failureStage'], stage)
                self.assertEqual(result['immutableInputsUnchanged'], name != 'package')

    def test_missing_final_sample_or_wrong_native_request_id_cannot_pass(self):
        for action in ['raise SystemExit(0)', 'identity += 500']:
            with self.subTest(action=action), tempfile.TemporaryDirectory() as temp:
                root = Path(temp)
                doc = executable_document(root)
                doc['gameplay']['steps'] = [observation()]
                engine = Path(doc['files'][0]['source'])
                code = engine.read_text().replace("    elif command == 'state':",
                    "    elif command == 'state':\n        if identity >= 3:\n            " + action)
                engine.write_text(code)
                doc['files'][0]['sha256'] = sha(engine.read_bytes())
                path = root / 'input.json'
                path.write_text(json.dumps(doc))
                out = root / 'out'
                result = runner.run_capture(path, out)
                self.assertEqual(result['status'], 'failed')
                report = json.loads((out / 'scenario.json').read_bytes())
                evidence = report['steps'][0]['observation']
                self.assertEqual(evidence['outcome'], 'inconclusive')
                self.assertEqual(len(evidence['samples']), 1)
