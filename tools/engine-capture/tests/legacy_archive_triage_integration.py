"""Exercise the installed-style triage entry point with real RED and original fixtures."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from test_verify_episode_pair import package

AGENT = Path(sys.argv.pop(1)).resolve()
HELPER = Path(__file__).resolve().parents[1] / 'triage_legacy_archives.py'


def digest(data):
    return hashlib.sha256(data).hexdigest()


class TriageIntegration(unittest.TestCase):
    def test_collection_keeps_siblings_and_reports_exact_map_and_mount_evidence(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            episodes = root / 'episodes'
            episodes.mkdir()
            stock = root / 'stock.gob'
            package(stock, {'mat/scaffold.mat': b'leaf', 'misc/cmp/scaffold.cmp': b'leaf'})
            level = root / 'room.jkl'
            subprocess.run([AGENT, 'new', level, 'jk'], check=True, capture_output=True)
            room = level.read_bytes()
            original = b'0.000000 4 0,0 1,1 2,2 3,3 1.0000 1.0000 1.0000 1.0000'
            oversized = b'0.000000 25 ' + b'0,0 ' * 25 + b'1.0000 ' * 25
            self.assertIn(original, room)
            maps = {'jkl/clean.jkl': room, 'jkl/broken.jkl': b'bad',
                    'jkl/large.jkl': room.replace(original, oversized, 1)}
            # The existing RED-authored relay fixture binds three placed COG parameters.
            fixture = HELPER.parent / 'fixtures/room-door-lift'
            cog_level = (fixture / 'redlift.jkl').read_bytes()
            self.assertIn(b'0: rlcontrol.cog 0 10 1', cog_level)
            maps['jkl/reference.jkl'] = cog_level.replace(b'0: rlcontrol.cog 0 10 1', b'0: rlcontrol.cog 9999 10 1')
            maps['cog/rlcontrol.cog'] = (fixture / 'rlcontrol.cog').read_bytes()
            package(episodes / 'arena.GOO', maps)
            package(episodes / 'tail.gob', {'jkl/room.jkl': room}, index_at_end=True)
            package(episodes / 'assets.gob', {'cog/test.cog': b'code'})
            (episodes / 'a.gob').write_bytes(b'damaged')
            inputs = [stock, level, *episodes.iterdir()]
            before = {p: p.read_bytes() for p in inputs}

            def run(*args):
                result = subprocess.run([sys.executable, '-B', HELPER, episodes, '--agent', AGENT, *args],
                                        text=True, capture_output=True)
                self.assertEqual(result.returncode, 1, result.stderr)
                self.assertNotIn(str(root), result.stdout)
                return json.loads(result.stdout)

            report = run('--stock', stock)
            self.assertEqual(report, run('--stock', stock))
            self.assertFalse(report['runtimeVerified'])
            self.assertEqual(report['stock'][0]['sha256'], digest(before[stock]))
            rows = {a['name']: a for a in report['archives']}
            self.assertEqual(list(rows), ['a.gob', 'arena.GOO', 'assets.gob', 'tail.gob'])
            self.assertEqual(rows['a.gob']['status'], 'refused')
            self.assertEqual(rows['a.gob']['sha256'], digest(b'damaged'))
            self.assertEqual(rows['assets.gob']['status'], 'no-maps')
            self.assertEqual(rows['arena.GOO']['sha256'], digest(before[episodes / 'arena.GOO']))
            levels = {m['name']: m for m in rows['arena.GOO']['maps']}
            clean = levels['jkl/clean.jkl']
            self.assertEqual(clean['sha256'], digest(room))
            self.assertEqual(clean['status'], 'checked')
            self.assertTrue(clean['validation']['loaded'])
            self.assertEqual(clean['dependencies']['missingObserved'], 0)
            self.assertFalse(levels['jkl/broken.jkl']['validation']['loaded'])
            self.assertIn('surface-corner-limit', levels['jkl/large.jkl']['validation']['issueCounts'])
            bindings = levels['jkl/reference.jkl']['validation']
            self.assertEqual(bindings['cogReferences'], 'checked')
            self.assertIn('cog-reference', bindings['issueCounts'])
            tail = rows['tail.gob']['maps'][0]
            self.assertTrue(tail['validation']['loaded'])
            self.assertEqual(tail['status'], 'checked')
            self.assertEqual(tail['dependencies']['missingObserved'], 0)
            alone = run()
            missing = next(m['dependencies'] for a in alone['archives'] if a['name'] == 'arena.GOO'
                           for m in a['maps'] if m['name'] == 'jkl/clean.jkl')
            self.assertEqual(missing['missingObserved'], 2)
            self.assertEqual({p['path'].split('/')[-1] for p in missing['unresolved']},
                             {'scaffold.mat', 'scaffold.cmp'})
            self.assertEqual(before, {p: p.read_bytes() for p in inputs})
            self.assertEqual(set(episodes.iterdir()), set(inputs[2:]))
            checked_folder = root / 'checked'
            checked_folder.mkdir()
            package(checked_folder / 'one.gob', {'jkl/clean.jkl': room}, index_at_end=True)
            success = subprocess.run([sys.executable, '-B', HELPER, checked_folder, '--agent', AGENT,
                                      '--stock', stock], capture_output=True)
            self.assertEqual(success.returncode, 0, success.stdout)
            self.assertEqual(json.loads(success.stdout)['archives'][0]['status'], 'checked')

    def test_resource_order_is_visible_in_transitive_references(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            episodes = root / 'episodes'
            episodes.mkdir()
            level = root / 'room.jkl'
            subprocess.run([AGENT, 'new', level, 'jk'], check=True, capture_output=True)
            room = level.read_bytes().replace(b'World models 1024', b'World models 1024\n0: choice.3do')
            self.assertIn(b'choice.3do', room)
            first, second = root / 'first.gob', root / 'second.goo'
            package(first, {'3do/choice.3do': b'0: first.mat\n'})
            package(second, {'3do/choice.3do': b'0: second.mat\n'}, index_at_end=True)
            package(episodes / 'a.gob', {'jkl/room.jkl': room})
            package(episodes / 'b.gob', {'jkl/room.jkl': room, '3do/choice.3do': b'0: episode.mat\n'})
            for mounts, expected in (((first, second), 'first.mat'), ((second, first), 'second.mat')):
                result = subprocess.run([sys.executable, '-B', HELPER, episodes, '--agent', AGENT,
                                         '--stock', mounts[0], '--stock', mounts[1]], capture_output=True)
                self.assertEqual(result.returncode, 1)
                report = json.loads(result.stdout)
                self.assertEqual([a['name'] for a in report['stock']], [p.name for p in mounts])
                for row, winner in zip(report['archives'], (expected, 'episode.mat')):
                    refs = row['maps'][0]['dependencies']
                    missing = {n['path'].split('/')[-1] for n in refs['unresolved']}
                    self.assertEqual(missing, {'scaffold.mat', 'scaffold.cmp', winner})


if __name__ == '__main__':
    unittest.main()
