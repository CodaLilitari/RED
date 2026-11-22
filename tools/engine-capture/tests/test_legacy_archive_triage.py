"""Batch policy and refusal tests with synthetic archives and small test limits."""
import hashlib
import io
import json
import os
from contextlib import nullcontext
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import time
import unittest
from unittest import mock

from test_verify_episode_pair import package
import triage_legacy_archives as triage


class LegacyArchiveTriageTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.folder = self.root / 'episodes'
        self.folder.mkdir()

    def report(self):
        return triage.triage(self.folder, 'unused-no-map-checks', [])['archives']

    def test_refuses_links_directories_and_nonrecursive_input(self):
        target = self.root / 'outside.gob'
        package(target, {'x': b'value'})
        nested = self.folder / 'nested'
        nested.mkdir()
        package(nested / 'hidden.gob', {'x': b'x'})
        (self.folder / 'directory.goo').mkdir()
        package(self.folder / 'regular.GOB', {'x': b'x'})
        try:
            (self.folder / 'link.gob').symlink_to(target)
        except OSError:
            if os.name != 'nt':
                raise
        rows = {r['name']: r for r in self.report()}
        self.assertEqual(rows['directory.goo']['reason'], 'not-regular-file')
        self.assertEqual(rows['regular.GOB']['status'], 'no-maps')
        self.assertNotIn('hidden.gob', rows)
        if 'link.gob' in rows:
            self.assertEqual(rows['link.gob']['reason'], 'not-regular-file')
            self.assertNotIn('sha256', rows['link.gob'])

    def test_directory_and_capture_work_are_bounded(self):
        package(self.folder / 'a.gob', {'x': b'x'})
        package(self.folder / 'b.goo', {'x': b'y'})
        size = (self.folder / 'a.gob').stat().st_size
        for option in ('MAX_CHILDREN', 'MAX_ARCHIVES'):
            with self.subTest(option=option), mock.patch.object(triage, option, 1):
                with self.assertRaises(triage.Refused):
                    self.report()
        with mock.patch.object(triage, 'MAX_TOTAL_BYTES', size):
            rows = self.report()
            self.assertEqual(rows[0]['status'], 'no-maps')
            self.assertEqual(rows[1]['reason'], 'source-byte-limit')
            self.assertNotIn('sha256', rows[1])
        with mock.patch.object(triage, 'MAX_BYTES', size - 1):
            self.assertEqual(self.report()[0]['reason'], 'source-byte-limit')

    def test_map_and_index_admission(self):
        package(self.folder / 'a.gob', {'jkl/one.jkl': b'x', 'jkl/two.jkl': b'x'})
        package(self.folder / 'b.goo', {'jkl/three.jkl': b'x'})
        for option in ('MAX_MAPS', 'MAX_TOTAL_MAPS'):
            with self.subTest(option=option), mock.patch.object(triage, option, 1), \
                    mock.patch.object(triage, 'MAX_MAP_BYTES', 0):
                rows = self.report()
                self.assertEqual(rows[0]['reason'], 'map-count-limit')
                self.assertEqual(rows[1]['maps'][0]['reason'], 'map-byte-limit')
        with mock.patch.object(triage, 'MAX_ENTRIES', 1):
            self.assertEqual(self.report()[0]['status'], 'refused')

    def test_repeated_native_mounts_have_a_collection_budget(self):
        package(self.folder / 'a.gob', {'one.jkl': b'first', 'two.jkl': b'second'})
        with mock.patch.object(triage, 'MAX_AGENT_INPUT_BYTES', 0), \
                mock.patch.object(triage, 'agent_check', side_effect=AssertionError('unexpected agent call')):
            maps = self.report()[0]['maps']
        self.assertEqual([m['reason'] for m in maps], ['agent-input-budget'] * 2)
        self.assertTrue(all(m['sha256'] for m in maps))

    def test_unsafe_names_and_overlapping_members_do_not_extract(self):
        package(self.folder / 'a.gob', {'../escape.jkl': b'x'})
        overlap = self.folder / 'b.goo'
        package(overlap, {'a': b'xx', 'b': b'yy'})
        data = bytearray(overlap.read_bytes())
        first, = struct.unpack_from('<I', data, 16)
        struct.pack_into('<I', data, 16 + 136, first + 1)
        overlap.write_bytes(data)
        rows = self.report()
        self.assertEqual(rows[0]['status'], 'refused')
        self.assertEqual(rows[1]['reason'], 'overlapping-payloads')
        self.assertFalse((self.root / 'escape.jkl').exists())
        self.assertEqual(overlap.read_bytes(), data)

    def test_agent_output_and_timeout_are_bounded(self):
        request = {'inputSha256': '0' * 64}
        # Python serves as a portable child process; its first argument is this script.
        program = self.root / 'validate'
        program.write_text('print("x" * 100)', encoding='ascii')
        with mock.patch.object(triage, 'MAX_OUTPUT', 32), self.assertRaisesRegex(triage.Refused, 'agent-output-limit'):
            triage.agent_check(sys.executable, self.root, 'validate', request)
        program.write_text('import time\ntime.sleep(20)', encoding='ascii')
        with mock.patch.object(triage, 'MAX_SECONDS', 0.1), self.assertRaisesRegex(triage.Refused, 'agent-timeout'):
            triage.agent_check(sys.executable, self.root, 'validate', request)

    def test_ambiguous_or_nonstandard_agent_json_is_refused(self):
        program = self.root / 'validate'
        request = {'inputSha256': '0' * 64}
        for extra in ('"ok":false,', '"value":NaN,'):
            with self.subTest(extra=extra):
                payload = '{' + extra + '"ok":true,"command":"validate","inputSha256":"' + request['inputSha256'] + '"}'
                program.write_text('print(' + repr(payload) + ')')
                with self.assertRaisesRegex(triage.Refused, 'invalid-agent-reply'):
                    triage.agent_check(sys.executable, self.root, 'validate', request)

    def test_descendant_holding_stdout_is_stopped_by_deadline_and_next_check_runs(self):
        # The descendant takes a nonblocking OS file lock before waiting with its inherited pipe.
        # Only the owned tree should be able to release that lock before the five-second sleep ends.
        guard = self.root / 'guard'
        guard.write_bytes(b'x')
        lock = ("import msvcrt; msvcrt.locking(handle.fileno(), msvcrt.LK_NBLCK, 1)" if os.name == 'nt'
                else "import fcntl; fcntl.flock(handle, fcntl.LOCK_EX | fcntl.LOCK_NB)")
        locking = "handle = open('guard','r+b'); " + lock
        child = locking + "; from pathlib import Path; Path('ready').touch(); import time; time.sleep(5)"
        worker = self.root / 'validate'
        worker.write_text("import subprocess,sys\nsubprocess.Popen([sys.executable,'-c'," + repr(child) + "])")
        started = time.monotonic()
        with mock.patch.object(triage, 'MAX_SECONDS', 1.5), self.assertRaisesRegex(triage.Refused, 'agent-timeout'):
            triage.agent_check(sys.executable, self.root, 'validate', {'inputSha256': '0' * 64})
        self.assertLess(time.monotonic() - started, 4.5)
        self.assertTrue((self.root / 'ready').is_file())
        subprocess.run([sys.executable, '-c', locking], cwd=self.root, check=True, capture_output=True, timeout=2)
        next_reply = {'ok': True, 'command': 'validate', 'inputSha256': '0' * 64}
        worker.write_text('print(' + repr(json.dumps(next_reply)) + ')')
        self.assertEqual(triage.agent_check(sys.executable, self.root, 'validate', next_reply), next_reply)

    def test_incompatible_agent_receipts_do_not_pass_or_abort_sibling_maps(self):
        valid = dict(valid=True, rawStructureScope='materials-and-georesource', rawStructureStatus='checked',
                     seamStatus='complete', fanDegeneracyStatus='complete', cogReferenceStatus='checked',
                     counts={'sectors': 1, 'things': 0, 'cogs': 0}, issues=[])
        self_digest = hashlib.sha256(b'x').hexdigest()
        inventory = {'sha256': self_digest, 'entries': 1, 'bytes': 1}
        deps = dict(schemaVersion=1, issues=[], scanComplete=True, detailsComplete=True, allReferencesResolved=True, missingObserved=0,
                    ambiguousObserved=0, unsupportedObserved=0, issuesObserved=0, unverifiedScopes=[],
                    scanReasons=[], nodes=[], sourceArchives=[dict(inventory, ordinal=0, stock=False)])
        cases = [('validate', dict(valid, valid='false')), ('validate', dict(valid, issues=[1])),
                 ('validate', dict(valid, counts=False)), ('validate', dict(valid, cogReferenceStatus='unverified-no-resources')),
                 ('validate', dict(valid, rawStructureScope='unknown')), ('validate', dict(valid, rawStructureStatus='incomplete')), ('dependencies', dict(deps, scanComplete='false')),
                 ('dependencies', dict(deps, allReferencesResolved=False)), ('dependencies', dict(deps, nodes=[{}])),
                 ('dependencies', dict(deps, issuesObserved=False)),
                 ('dependencies', dict(deps, sourceArchives=[dict(inventory, ordinal=1, stock=False)])),
                 ('dependencies', dict(deps, sourceArchives=[dict(inventory, ordinal=0, stock=True)])),
                 ('dependencies', dict(deps, sourceArchives=[dict(inventory, ordinal=0, stock=False, bytes=2)])),
                 ('dependencies', dict(deps, unverifiedScopes=['x' * 5000])),
                 ('validate', dict(valid, issues=[{'severity': 'warning', 'code': 'x' * 5000, 'message': 'sample'}]))]
        for command, broken in cases:
            with self.subTest(command=command, broken=broken):
                broken_hash = hashlib.sha256(b'bad').hexdigest()
                def receipt(_agent, _cwd, which, request):
                    if request['inputSha256'] == broken_hash and which == command:
                        return broken
                    return valid if which == 'validate' else deps

                with mock.patch.object(triage, 'agent_check', side_effect=receipt):
                    rows = [triage.inspect_map('unused', self.root, io.BytesIO(data), name, (0, len(data)),
                                [{'sha256': self_digest}], [inventory], triage.Budget(100, 2, 100))
                                for name, data in [('bad.jkl', b'bad'), ('good.jkl', b'good')]]
                self.assertEqual([row['status'] for row in rows], ['incomplete', 'checked'])
                self.assertEqual(rows[1]['validation'].get('rawStructureScope'), 'materials-and-georesource')
                self.assertLess(len(json.dumps(rows[0])), 4096)

    def dependency_receipt(self, **changes):
        inventory = {'sha256': '0' * 64, 'entries': 2, 'bytes': 64}
        result = dict(schemaVersion=1, scanComplete=True, detailsComplete=True,
                      allReferencesResolved=True, missingObserved=0, ambiguousObserved=0,
                      unsupportedObserved=0, issuesObserved=0, unverifiedScopes=[], scanReasons=[],
                      nodes=[{'resolution': 'source', 'path': 'level.jkl'}], issues=[],
                      sourceArchives=[dict(inventory, ordinal=0, stock=False)])
        result.update(changes)
        with mock.patch.object(triage, 'agent_check', return_value=result):
            return triage.dependencies('unused', self.root, {}, [], [inventory])

    def test_dependency_schema_version_must_be_supported(self):
        for version in (2, None, True):
            with self.subTest(version=version), self.assertRaises(triage.Refused):
                self.dependency_receipt(schemaVersion=version)

    def test_dependency_issue_rows_agree_with_observed_count_and_shape(self):
        issue = {'node': 0, 'code': 'test', 'message': 'typed issue'}
        for changes in (dict(issues=[issue]), dict(issuesObserved=1), dict(issuesObserved=1, issues=[{}]),
                        dict(detailsComplete=False, issues=[issue])):
            with self.subTest(changes=changes), self.assertRaises(triage.Refused):
                self.dependency_receipt(**changes)
        for complete, rows in ((True, [issue]), (False, [])):
            with self.subTest(complete=complete):
                self.assertEqual(self.dependency_receipt(issuesObserved=1, issues=rows,
                                 detailsComplete=complete)['issuesObserved'], 1)

    def test_missing_and_ambiguous_counts_are_independent_even_for_partial_detail(self):
        for complete in (False, True):
            for counted, reported in (('missing', 'ambiguous'), ('ambiguous', 'missing')):
                changes = {counted + 'Observed': 1, 'allReferencesResolved': False,
                           'nodes': [{'resolution': reported, 'path': 'mat/missing.mat'}]}
                with self.subTest(complete=complete, counted=counted), self.assertRaises(triage.Refused):
                    self.dependency_receipt(detailsComplete=complete, **changes)
        truncated = self.dependency_receipt(detailsComplete=False, missingObserved=1, allReferencesResolved=False)
        self.assertTrue(truncated['unresolvedTruncated'])

    def test_directory_child_limit_precedes_archive_limit_in_both_orders(self):
        for name in ('a.gob', 'b.gob', 'notes.txt', 'extra.txt'):
            (self.folder / name).touch()
        with os.scandir(self.folder) as scan:
            entries = sorted(scan, key=lambda entry: entry.name)
        for ordered in (entries, entries[::-1]):
            with self.subTest(order=[entry.name for entry in ordered]), \
                    mock.patch.object(triage.os, 'scandir', return_value=nullcontext(iter(ordered))), \
                    mock.patch.object(triage, 'MAX_CHILDREN', 3), mock.patch.object(triage, 'MAX_ARCHIVES', 1), \
                    self.assertRaisesRegex(triage.Refused, 'directory-child-limit'):
                triage.candidates(self.folder, None)

    def test_inventory_distinguishes_presence_of_episode_manifest(self):
        package(self.folder / 'a.gob', {'episode.jk': b'definition'})
        package(self.folder / 'b.goo', {'texture.mat': b'leaf'})
        self.assertEqual([r['episodeDefinitionPresent'] for r in self.report()], [True, False])

    def test_empty_collection_is_explicit(self):
        with self.assertRaisesRegex(triage.Refused, 'no-archives'):
            self.report()


if __name__ == '__main__':
    unittest.main()
