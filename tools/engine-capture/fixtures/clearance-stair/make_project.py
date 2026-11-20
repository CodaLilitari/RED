"""Generate the original narrow-door and beveled-stair OpenJKDF2 control episode."""

import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import struct

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location(
    "red_relay_fixture", HERE.parent / "room-door-lift" / "make_project.py"
)
relay = importlib.util.module_from_spec(spec)
spec.loader.exec_module(relay)
box, grayscale, material = relay.box, relay.grayscale, relay.material


def resource_archive():
    assets = {
        "mat/rlwall.mat": material((156, 145, 145, 156)),
        "mat/rlmark.mat": material((228, 72, 72, 228)),
        "misc/cmp/rlgray.cmp": grayscale(),
        "3do/rlgate.3do": box("gate", (0.123, 0.022, 0.5), "rlmark.mat"),
        "3do/rlplatform.3do": box("platform", (0.124, 0.045, 0.06), "rlmark.mat"),
        "3do/rlriser.3do": (HERE / "rlriser.3do").read_text(encoding="ascii").encode("ascii"),
        "3do/rlpilot.3do": box("pilot", (0.02, 0.02, 0.03), "rlwall.mat"),
        "misc/snd/rlquiet.snd": b"# Silent original fixture sound class.\n",
        "cog/rlcontrol.cog": (HERE / "rlcontrol.cog")
        .read_text(encoding="ascii")
        .replace("\n", "\r\n")
        .encode("ascii"),
    }
    index = bytearray(b"GOB " + struct.pack("<III", 0x14, 12, len(assets)))
    offset = 16 + len(assets) * 136
    for name, data in assets.items():
        index.extend(
            struct.pack("<II128s", offset, len(data), name.replace("/", "\\").encode("ascii"))
        )
        offset += len(data)
    return bytes(index) + b"".join(assets.values())


def create(destination):
    destination.mkdir(parents=True, exist_ok=False)
    for name in ("episode.jk", "scenario.json"):
        (destination / name).write_bytes((HERE / name).read_text(encoding="ascii").encode("ascii"))
    # Preserve the published native control's wire bytes on LF and CRLF checkouts.
    (destination / "redlift.jkl").write_bytes(
        (HERE / "redlift.jkl").read_text(encoding="ascii").replace("\n", "\r\n").encode("ascii")
    )
    archive = resource_archive()
    (destination / "relay-art.gob").write_bytes(archive)
    profile = {
        "schemaVersion": 1,
        "episode": "episode.jk",
        "levels": [{"export": "redlift.jkl"}],
        "target": {"game": "jk", "engine": "OpenJKDF2", "renderer": "OpenGL"},
        "mounts": [
            {
                "kind": "project",
                "path": "relay-art.gob",
                "sha256": hashlib.sha256(archive).hexdigest(),
            }
        ],
        "scenarios": [
            {
                "name": "clearance-stair",
                "level": "redlift.jkl",
                "definition": "scenario.json",
                "receipts": [],
            }
        ],
    }
    (destination / "red-project.json").write_bytes(
        (json.dumps(profile, indent=2) + "\n").encode("utf-8")
    )


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "destination", type=Path, help="new directory for the editable example project"
    )
    create(parser.parse_args().destination)
