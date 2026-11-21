"""Batch policy and refusal tests with synthetic archives and small test limits."""
import os
from pathlib import Path
import struct
import sys
import tempfile
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

    def test_inventory_distinguishes_presence_of_episode_manifest(self):
        package(self.folder / 'a.gob', {'episode.jk': b'definition'})
        package(self.folder / 'b.goo', {'texture.mat': b'leaf'})
        self.assertEqual([r['episodeDefinitionPresent'] for r in self.report()], [True, False])

    def test_empty_collection_is_explicit(self):
        with self.assertRaisesRegex(triage.Refused, 'no-archives'):
            self.report()


if __name__ == '__main__':
    unittest.main()
