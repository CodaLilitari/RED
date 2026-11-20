"""Ensure normal level work selects RED when historical tooling is also present."""
import importlib.util
import pathlib
import tempfile
import unittest
from unittest import mock

ROOT = pathlib.Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('probe_tools', ROOT / 'scripts/probe_tools.py')
probe = importlib.util.module_from_spec(spec)
spec.loader.exec_module(probe)


class EditorSelection(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        self.root = pathlib.Path(temp.name)

    def test_mixed_package_selects_red_and_explicit_legacy_selects_sed(self):
        old = self.root / 'agent/sed-agent.exe'
        old.parent.mkdir()
        old.touch()
        with self.assertRaises(ValueError):
            probe.resolve_agent(self.root, None, False)
        red = self.root / 'usr/bin/red-agent'
        red.parent.mkdir(parents=True)
        red.touch()
        self.assertEqual(probe.resolve_agent(self.root, None, False), red.resolve())
        self.assertEqual(probe.resolve_agent(self.root, None, True), old.resolve())

    def test_renamed_sed_binary_cannot_be_selected_as_red(self):
        candidate = self.root / 'red-agent'
        candidate.touch()
        with mock.patch.object(probe, 'run', return_value={'patchJsonSchema': {}}):
            with self.assertRaisesRegex(ValueError, 'deprecated SED-MotS'):
                probe.probe(agent_path=candidate)

    def test_exact_root_takes_precedence_over_saved_config(self):
        import json
        config = self.root / 'local-config.json'
        config.write_text(json.dumps({'redRoot': 'stale-root', 'agentPath': 'stale-agent'}))
        with mock.patch('sys.argv', ['probe', '--config', str(config), '--red-root', str(self.root)]):
            with mock.patch.object(probe, 'probe', return_value={}) as inspect, mock.patch('builtins.print'):
                probe.main()
        inspect.assert_called_once_with(self.root, None, False)
