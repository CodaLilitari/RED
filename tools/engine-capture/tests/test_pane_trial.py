import json
import struct
import tempfile
import unittest
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import gameplay_capture
import pane_trial


class PaneTrialTests(unittest.TestCase):
    def test_generated_pair_and_bindings(self):
        profile = pane_trial.profile("jk", "indexed8", "arena", "arena.jkl", 9, 11, 100, 11, 50)
        no_fire = pane_trial.recipe(profile, False)
        fire = pane_trial.recipe(profile, True)
        self.assertEqual(no_fire["launch"], fire["launch"])
        self.assertFalse(any(s["op"] == "command" for s in no_fire["steps"]))
        self.assertEqual([s["value"] for s in fire["steps"] if s["op"] == "command"],
                         ["button 1 1", "button 1 0"])
        for item in (no_fire, fire):
            gameplay_capture.validate_scenario(item)
        with tempfile.TemporaryDirectory() as td:
            out = Path(td) / "trial"
            pane_trial.prepare(out, profile)
            text = (out / "redpane.cog").read_text()
            self.assertIn("int target=9 local", text)
            self.assertIn("int control=11 local", text)
            self.assertEqual(text.count("SetInv("), 1)
            self.assertNotIn("DestroyThing(", text)
            self.assertNotIn("DamageThing(", text)
            with self.assertRaises(FileExistsError):
                pane_trial.prepare(out, profile)

    def test_rejects_ambiguous_controls(self):
        for slots in [(9, 9, 100, 11), (9, -1, 100, 11), (9, 10, 100, 100)]:
            with self.subTest(slots=slots), self.assertRaises(ValueError):
                pane_trial.profile("jk", "indexed8", "arena", "arena.jkl", *slots, 50)

    def test_comparison_binds_package_recipe_frames_and_ammo(self):
        p = pane_trial.profile("jk", "indexed8", "arena", "arena.jkl", 9, 10, 100, 11, 50)

        def store(path, value):
            path.write_text(json.dumps(value), encoding="utf-8")
            return pane_trial.identity(path)

        def session(directory, fire, package):
            directory.mkdir()
            agent = directory / "agent"
            agent.mkdir()
            spec = gameplay_capture.validate_scenario(pane_trial.recipe(p, fire))
            recipe_hash = store(directory / "scenario-input.json", spec)
            journal_hash = store(agent / "commands.jsonl", {})
            (directory / "episode").mkdir()
            gob = directory / "episode/arena.gob"
            content = pane_trial.observer(p).encode("ascii")
            gob.write_bytes(struct.pack("<4sIII",b"GOB ",20,12,1)
                            + struct.pack("<II128s",152,len(content),b"cog\\redpane.cog") + content + package.encode("ascii"))
            inputs = {"engineSha256":"e"*64, "packageSha256":pane_trial.identity(gob), "scenarioInputSha256":recipe_hash}
            state = {"levelActive":True, "player":{"inventory":{"100":2 if fire else 1,"11":49 if fire else 50}}}
            steps = []
            for i, step in enumerate(spec["steps"]):
                row = {"op":step["op"], "name":step["name"], "status":"passed"}
                if row["op"] == "checkpoint":
                    frame = agent / f"frame-{i}.ppm"
                    frame.write_bytes(b"P6\n1 1\n255\n"+bytes(3))
                    row["capture"] = {"file":frame.name, "sha256":pane_trial.identity(frame)}
                steps.append(row)
            report_hash = store(directory / "scenario.json", {"inputs":inputs, "scenarioSha256":recipe_hash,
                "status":"passed", "physicsOverrides":False, "steps":steps, "finalState":state})
            store(directory / "launcher-receipt.json", {"status":"completed", "immutableInputsUnchanged":True,
                "gameplayInputs":inputs, "episodeArchive":{"path":"episode/arena.gob", "mapEntry":"jkl\\arena.jkl",
                                                             "archiveSha256":inputs["packageSha256"]},
                "gameplay":{"status":"passed", "scenarioSha256":report_hash, "journal":{"sha256":journal_hash}}})

        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            hold, shot, wrong = [root / name for name in ("hold","shot","other")]
            session(hold,False,"a"*64)
            session(shot,True,"a"*64)
            session(wrong,True,"b"*64)
            self.assertEqual(pane_trial.compare(p,hold,shot)["fire"]["remainingAmmo"],49)
            with self.assertRaisesRegex(ValueError,"packaged observer"):
                pane_trial.compare(dict(p,target=12),hold,shot)
            with self.assertRaisesRegex(ValueError,"same engine and packaged episode"):
                pane_trial.compare(p,hold,wrong)
            with self.assertRaisesRegex(ValueError,"recipe, report or journal"):
                pane_trial.compare(p,shot,hold)
            (shot/"agent/frame-1.ppm").write_bytes(b"changed")
            with self.assertRaisesRegex(ValueError,"checkpoint frame"):
                pane_trial.compare(p,hold,shot)
