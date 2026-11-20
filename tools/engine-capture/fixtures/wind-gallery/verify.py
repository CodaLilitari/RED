"""Exercise RED's full portable conversion path in a fresh external fixture directory."""

import argparse
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET

from make_project import create, digest
from qualification_controls import create as create_controls


def check(executable):
    with tempfile.TemporaryDirectory(prefix="red-wind-gallery-") as temp:
        root = Path(temp) / "gallery"
        create(root, executable)
        controls = root / "controls"
        create_controls(root / "gallery.gob", controls)
        qualification = json.loads(Path(__file__).with_name("qualification.json").read_text())
        for name, variant in (
            ("gallery.gob", "reviewed"),
            ("gallery-optimized.gob", "reviewedOptimization"),
            ("gallery-pass-through.gob", "passThrough"),
        ):
            assert (
                digest((root / name).read_bytes())
                == qualification["ordinaryScenarios"][variant]["archiveSha256"]
            )
        for name, fingerprint in qualification["visual"]["controlArchiveSha256"].items():
            assert digest((controls / (name + ".gob")).read_bytes()) == fingerprint
        assert (
            digest((controls / "silent.gob").read_bytes())
            == qualification["audio"]["windows"]["silencedControl"]["archiveSha256"]
        )
        load = lambda name: json.loads((root / name).read_text())
        summary = load("acceptance.json")
        mapping = load("mapping-decision.json")
        overlay = load("source-clip.json")["collisionBrush"]
        assert summary["originalGeometry"]["patches"] == 1
        assert summary["originalGeometry"]["portalPairs"] == 1
        assert (
            summary["originalGeometry"]["nativeFaces"] > 0
            and summary["originalGeometry"]["models"] > 0
        )
        assert mapping["newTriangles"] < mapping["previousTriangles"]
        assert mapping["previousArchive"] != mapping["approvedArchive"]
        assert overlay["playerClip"] and overlay["shotClip"] and not overlay["hasLinkedDrawSurface"]
        assert len(overlay["sides"]) == 6
        assert "uses transparency" in load("direct-cutout-refusal.json")["error"]
        assert len(load("optimized-conflicts.json")["reimport"]["conflicts"]) >= 2
        query = lambda archive: json.loads(
            subprocess.run(
                [
                    str(executable),
                    "jo-ja-inspect",
                    str(root / archive),
                    "--target",
                    "jkl/gallery.jkl",
                    "0",
                    "0",
                ],
                check=True,
                capture_output=True,
                timeout=30,
            ).stdout
        )
        solid = query("gallery.gob")
        open_grate = query("gallery-pass-through.gob")
        assert solid["native"] == open_grate["native"]
        assert solid["model"]["modelSha256"] == open_grate["model"]["modelSha256"]
        assert solid["model"]["faces"] == open_grate["model"]["faces"]
        assert solid["model"]["shape"] == "faces" and open_grate["model"]["shape"] == "none"
        assert any(f.get("neighbor", {}).get("reciprocal") for f in solid["native"]["surfaces"])
        report = root / "slice.html"
        viewer = Path(__file__).resolve().parents[2] / "collision_overlay.py"
        subprocess.run(
            [
                sys.executable,
                "-B",
                str(viewer),
                "--agent",
                str(executable),
                "--source",
                str(root / "gallery.bsp"),
                "--archive",
                str(root / "gallery.gob"),
                "--level",
                "jkl/gallery.jkl",
                "--sector",
                "0",
                "--thing",
                "0",
                "--brush",
                "0",
                "--scale",
                ".005",
                "--axis",
                "z",
                "--depth",
                ".1",
                "--player",
                "2",
                "--projectile",
                "@+windstone",
                "--projectile-origin",
                ".18",
                ".2",
                ".1",
                "--decision",
                "Retain the original clip until play-tested.",
                "--output",
                str(report),
            ],
            check=True,
            timeout=50,
        )
        html = report.read_text()
        plot = ET.fromstring(html[html.index("<svg ") : html.index("</svg>") + 6])
        layers = {group.attrib["class"]: group for group in plot.findall("g")}
        assert len(layers["source"]) == 1 and len(layers["wall"]) > 0 and len(layers["portal"]) > 0
        assert (
            len(layers["model"]) > 0
            and len(layers["player"]) == 1
            and len(layers["projectile"]) == 1
        )


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("agent", type=Path)
    check(p.parse_args().agent.resolve())
