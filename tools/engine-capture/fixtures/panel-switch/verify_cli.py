"""Exercise 3DO button planning, JKL serialization and failure atomicity with original assets."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile

from make_project import create


def main(binary):
    def run(*args):
        return subprocess.run([str(binary), *map(str,args)], capture_output=True, text=True, timeout=60)
    with tempfile.TemporaryDirectory(prefix="red-panel-") as tmp:
        root = Path(tmp)/"sample"
        create(root,None)
        raw, recipe, out = root/"panel-source.jkl", root/"panel.json", root/"final.jkl"
        assert run("schema","panelSwitch").returncode == 0
        for args in (("plan",raw,recipe), ("apply",raw,recipe,out)):
            result = run(*args)
            assert result.returncode == 0, result.stdout + result.stderr
        contents = out.read_text()
        rows = [line.strip().lower().split() for line in contents.splitlines() if line.strip().startswith(("0:","3:"))]
        wall = next(row for row in rows if row[1:3] == ["rlgate","wall"])
        button = next(row for row in rows if row[1:3] == ["red_panelbutton","red_panelbutton"])
        assert [float(v) for v in wall[3:9]] == [0,-.09,.2,0,0,0],wall
        assert max(abs(float(v)-expected) for v,expected in zip(button[3:6], [0,-.133,.077])) < 1e-6,button
        assert any(row[:4] == ["0:","paneluse.cog","3","1"] for row in rows)
        assert "collide=1" in contents.lower() and "collide=3" in contents.lower()
        refusing = root/"refused.jkl"
        occupied = run("apply",out,recipe,refusing)
        assert occupied.returncode == 5 and "panelLinkOccupied" in occupied.stdout and not refusing.exists(),occupied.stdout
        obj = json.loads(recipe.read_text())
        obj["operations"][0]["u"] = 1
        recipe.write_text(json.dumps(obj))
        outside = run("apply",raw,recipe,refusing)
        assert outside.returncode == 5 and "panelFootprint" in outside.stdout and not refusing.exists(),outside.stdout


if __name__ == "__main__":
    main(Path(sys.argv[1]).resolve())
