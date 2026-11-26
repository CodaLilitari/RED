"""Build a two-room switch project from original RED fixture assets."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import subprocess

HERE = Path(__file__).resolve().parent
RELAY = HERE.parent / "room-door-lift"


def archive(entries):
    offset = 16 + 136 * len(entries)
    rows = []
    for name, data in entries.items():
        rows.append(struct.pack("<II128s", offset, len(data), name.replace("/", "\\").encode("ascii")))
        offset += len(data)
    return struct.pack("<4sIII", b"GOB ", 20, 12, len(entries)) + b"".join(rows) + b"".join(entries.values())


def create(root, agent):
    root.mkdir(parents=True, exist_ok=False)
    spec = importlib.util.spec_from_file_location("relay_assets", RELAY / "make_project.py")
    relay = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(relay)
    data = relay.resource_archive()
    entries = {}
    for i in range(struct.unpack_from("<I", data, 12)[0]):
        offset, length, name = struct.unpack_from("<II128s", data, 16+i*136)
        name = name.split(b"\0")[0].decode("ascii").replace("\\","/")
        if name != "cog/rlcontrol.cog":
            entries[name] = data[offset:offset+length]
    entries["cog/paneluse.cog"] = (HERE/"paneluse.cog").read_bytes()
    entries["3do/rlgate.3do"] = (HERE/"panelwall.3do").read_bytes()
    pixels = bytes(245 if 30 <= x <= 33 and 10 <= y <= 15 else 104 for y in range(64) for x in range(64))
    entries["mat/panelwall.mat"] = (relay.material((104,)*4)[:-28] + struct.pack("<6I",64,64,0,0,0,1) + pixels)
    resources = root/"panel-art.gob"
    resources.write_bytes(archive(entries))
    text = (RELAY / "redlift.jkl").read_text(encoding="ascii")
    text = text.replace("rlcontrol.cog 0 10 1", "paneluse.cog -1 1").replace("rlcontrol.cog", "paneluse.cog")
    text = text.replace("1: rlmark.mat 1 1", "1: rlmark.mat 1 1\n2: panelwall.mat 1 1")
    text = text.replace("rlgate none type=cog move=path", "rlgate none type=cog")
    old = next(line for line in text.splitlines() if line.startswith("0: rlgate Gate "))
    text = text.replace(old, "0: rlgate Wall 0 -.09 .20 0 0 0 0")
    text = text.replace("2: rlpilot Player 0 -.30 .09", "2: rlpilot Player 0 -.25 .09")
    raw = root/"panel-source.jkl"
    raw.write_text(text,encoding="ascii")
    patch = {"schemaVersion":1,"resourceArchives":[{"path":resources.as_posix(),
        "sha256":hashlib.sha256(resources.read_bytes()).hexdigest()}],
        "operations":[{"op":"panelSwitch","owner":0,"mesh":0,"face":2,"cog":0,"symbol":"button","v":-.123,"radius":.022}]}
    recipe = root/"panel.json"
    recipe.write_text(json.dumps(patch,indent=2)+"\n",encoding="ascii")
    if agent:
        out = root/"panel.jkl"
        subprocess.run([agent, "apply", raw, recipe, out], check=True, cwd=root)
        for label, x in (("panel", "0"), ("beside", ".24")):
            level = out.read_text(encoding="ascii")
            seen = []
            for line in level.splitlines():
                if line.lstrip().lower().startswith("2: rlpilot player "):
                    parts = line.split()
                    parts[3] = x
                    seen.append(line)
                    level = level.replace(line, " ".join(parts), 1)
            if len(seen) != 1:
                raise ValueError("Expected one authored player start")
            episode = f'"RED panel switch {label}"\nTYPE 1\nSEQ 1\n0: 1 1 LEVEL panel.jkl 0 0 -1 -1\nEND\n'
            (root/f"{label}.gob").write_bytes(archive({**entries, "episode.jk":episode.encode("ascii"),
                "jkl/panel.jkl":level.encode("ascii")}))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    parser.add_argument("--agent", type=Path, help="apply the recipe and create panel and neighboring-wall control episodes")
    args = parser.parse_args()
    create(args.output.resolve(), args.agent.resolve() if args.agent else None)
