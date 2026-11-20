"""Build the redistributable wind gallery conversion using RED's reviewed importer."""

import argparse
import hashlib
import io
import json
import math
from pathlib import Path
import struct
import subprocess
import wave

from original_rbsp import scene


def digest(data):
    return hashlib.sha256(data).hexdigest()


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")


def material(pixels, cutout=False):
    return (
        b"MAT "
        + struct.pack("<6I", 0x32, 2, 1, 1, 0, 8)
        + bytes(48)
        + struct.pack("<10I", 8, 0, 0, 0, 0, 0, 0, 0, 0, 0)
        + struct.pack("<6I", 8, 8, int(cutout), 0, 0, 1)
        + bytes(pixels * 4)
    )


def palette():
    colors = bytes(
        channel for i in range(256) for channel in (i, min(255, i + 8), min(255, i + 25))
    )
    levels = bytes(round(i * light / 63) for light in range(64) for i in range(256))
    return b"CMP " + bytes(60) + colors + levels


def wind():
    data = io.BytesIO()
    with wave.open(data, "wb") as sound:
        sound.setparams((1, 1, 8000, 0, "NONE", ""))
        sound.writeframes(
            bytes(
                round(
                    128
                    + 7 * math.sin(2 * math.pi * 93 * i / 8000)
                    + 4 * math.sin(2 * math.pi * 137 * i / 8000)
                )
                for i in range(8000)
            )
        )
    return data.getvalue()


def gob_read(data):
    (count,) = struct.unpack_from("<I", data, 12)
    result = {}
    for i in range(count):
        off, size, name = struct.unpack_from("<II128s", data, 16 + i * 136)
        result[name.split(b"\0")[0].decode().replace("\\", "/").lower()] = data[off : off + size]
    return result


def gob_write(entries):
    result = bytearray(b"GOB " + struct.pack("<III", 20, 12, len(entries)))
    off = 16 + 136 * len(entries)
    for name, content in entries.items():
        result += struct.pack("<II128s", off, len(content), name.replace("/", "\\").encode("ascii"))
        off += len(content)
    return bytes(result) + b"".join(entries.values())


def authored_level(raw, model_thing, entrance):
    """Apply explicit mapper decisions to this generated map by geometry and source ownership."""
    sections = raw.decode("ascii").splitlines()

    def section(name):
        start = next(i for i, line in enumerate(sections) if line.casefold() == "section: " + name)
        return start, next(i for i in range(start + 1, len(sections)) if sections[i] == "END")

    def append(name, row):
        sections.insert(section(name)[1], row)

    def add(name, header, row):
        a, b = section(name)
        n = next(i for i in range(a, b) if sections[i].casefold().startswith(header))
        parts = sections[n].split()
        parts[-1] = str(int(parts[-1]) + 1)
        sections[n] = " ".join(parts)
        append(name, row)

    def next_index(name):
        a, b = section(name)
        return 1 + max(
            (
                int(row.split(":", 1)[0])
                for row in sections[a + 1 : b]
                if row.split(":", 1)[0].isdigit()
            ),
            default=-1,
        )

    sky_slot = next_index("materials")
    append("materials", f"{sky_slot}: sky.mat 1 1")
    append("sounds", "0: galwind.wav")
    a, b = section("georesource")
    count_row = next(i for i in range(a, b) if sections[i].startswith("WORLD SURFACES "))
    count = int(sections[count_row].split()[-1])
    authored_skies = 0
    for i in range(count):
        line = count_row + 1 + i
        normal = [float(n) for n in sections[line + count].split()[1:]]
        tokens = sections[line].split()
        if normal[2] < -0.999 and int(tokens[7]) == -1:
            tokens[1], tokens[2], tokens[4] = str(sky_slot), "0x404", "4"
            sections[line] = " ".join(tokens)
            authored_skies += 1
    if authored_skies != 2:
        raise ValueError(f"Expected exactly two native gallery ceilings, found {authored_skies}")

    a, b = section("sectors")
    for i in range(b - 1, a, -1):
        if sections[i].startswith("AMBIENT LIGHT "):
            sections[i] = "AMBIENT LIGHT 0.65"
        if sections[i].startswith("CENTER "):
            sections.insert(i, "SOUND galwind.wav 0.3")
    append("cogscripts", "1: windgate.cog")
    append("cogs", f"0: windgate.cog {entrance} {model_thing} +windstone")
    add(
        "templates",
        "world templates",
        "+windstone none type=weapon move=physics collide=1 model3d=stone.3do physflags=0x201 "
        "typeflags=0xd damage=1 damageclass=1 vel=(0/2.5/1) size=.009 movesize=.009 timer=2",
    )
    add("models", "world models", f"{next_index('models')}: stone.3do")
    return ("\n".join(sections) + "\n").encode("ascii")


def create(root, executable):
    root.mkdir(exist_ok=False, parents=True)

    def asset(name, data):
        (root / name).write_bytes(data)
        return {"path": name, "sha256": digest(data)}

    def agent(*args, expect=0):
        r = subprocess.run(
            [str(executable), *map(str, args)], cwd=root, capture_output=True, timeout=90
        )
        if r.returncode != expect:
            raise RuntimeError(
                f"{args[0]} returned {r.returncode}: {r.stdout[-600:]!r} {r.stderr[-300:]!r}"
            )
        return json.loads(r.stdout or r.stderr)

    source = asset("gallery.bsp", scene())
    textures = {
        "floor": material(([140, 160] * 8)),
        "sky": material(([60, 80, 100, 120] * 4)),
        "grate": material(([30, 210, 210, 30] * 4)),
        "curve": material(([165, 175] * 8)),
    }
    recipe = {
        "source": source,
        "target": "jk",
        "levelName": "gallery",
        "scale": 0.005,
        "geometryMode": "hybrid-sectors",
        "renderMeshCollision": True,
        "patchSubdivisions": 4,
        "trianglesPerModel": 32,
        "palette": asset("gallery.cmp", palette()),
        "materials": [
            {"shader": "gallery/" + k, "name": k + ".mat", "source": asset(k + ".mat", value)}
            for k, value in textures.items()
        ],
    }
    write_json(root / "baseline.json", recipe)
    baseline = agent("jo-ja-plan", "baseline.json")
    agent(
        "jo-ja-apply",
        "baseline.json",
        "generated.gob",
        baseline["outputSha256"],
        baseline["manifestSha256"],
    )
    write_json(root / "baseline-report.json", baseline)
    write_json(root / "source-clip.json", agent("jo-ja-inspect", "gallery.bsp", "--brush", "0"))
    # Importing the color-key MAT directly in hybrid mode is an explicit refusal.
    import copy

    transparent = material([0, 210, 210, 0] * 4, True)
    cutout = copy.deepcopy(recipe)
    next(m for m in cutout["materials"] if m["name"] == "grate.mat")["source"] = asset(
        "cutout.mat", transparent
    )
    write_json(root / "direct-cutout.json", cutout)
    write_json(
        root / "direct-cutout-refusal.json", agent("jo-ja-plan", "direct-cutout.json", expect=5)
    )
    gallery = gob_read((root / "generated.gob").read_bytes())
    preserved = {
        "mat/sky.mat": textures["sky"],
        "mat/grate.mat": transparent,
        "sound/galwind.wav": wind(),
        "cog/windgate.cog": (Path(__file__).resolve().parent / "windgate.cog")
        .read_text(encoding="ascii")
        .replace("\n", "\r\n")
        .encode("ascii"),
        "3do/stone.3do": (Path(__file__).resolve().parent / "stone.3do")
        .read_text(encoding="ascii")
        .encode("ascii"),
        "episode.jk": b'"Wind Gallery"\nTYPE 1\nSEQ 1\n0: 1 1 LEVEL gallery.jkl 0 0 -1 -1\nEND\n',
    }
    grate = next(i for i, m in enumerate(baseline["models"]) if 2 in m["sourceSurfaces"])
    occupied = baseline["models"][grate]["sector"]
    preserved["jkl/gallery.jkl"] = authored_level(gallery["jkl/gallery.jkl"], grate, occupied)
    gallery.update(preserved)
    edited = gob_write(gallery)
    roles = {
        "mat/sky.mat": "sky",
        "mat/grate.mat": "visual",
        "sound/galwind.wav": "ambience",
        "cog/windgate.cog": "script",
        "3do/stone.3do": "throwables",
        "episode.jk": "gameplay",
        "jkl/gallery.jkl": "gameplay",
    }
    overlay = {
        "baselineRecipe": recipe,
        "baselineOutputSha256": baseline["outputSha256"],
        "authoredArchive": asset("authored.gob", edited),
        "overrides": [{"entry": name, "role": role} for name, role in roles.items()],
    }
    unchanged = dict(recipe, reimport=overlay)
    write_json(root / "reviewed.json", unchanged)
    reviewed = agent("jo-ja-plan", "reviewed.json")
    agent(
        "jo-ja-apply",
        "reviewed.json",
        "gallery.gob",
        reviewed["outputSha256"],
        reviewed["manifestSha256"],
    )
    delivered = gob_read((root / "gallery.gob").read_bytes())
    if any(delivered.get(n) != data for n, data in preserved.items()):
        raise ValueError("Published episode lost an authored member")
    write_json(root / "reviewed-report.json", reviewed)
    # Target movement redesign control. Override only this placed model's
    # collision mode. Every native vertex, draw surface, MAT and 3DO stays identical.
    pass_through = dict(delivered)
    lines = pass_through["jkl/gallery.jkl"].decode("ascii").splitlines()
    start = next(i for i, row in enumerate(lines) if row.casefold() == "section: things")
    row = next(i for i in range(start + 1, len(lines)) if lines[i].startswith(f"{grate}: "))
    lines[row] += " collide=0"
    pass_through["jkl/gallery.jkl"] = ("\n".join(lines) + "\n").encode("ascii")
    (root / "gallery-pass-through.gob").write_bytes(gob_write(pass_through))
    (root / "pass-through.json").write_bytes(
        (Path(__file__).resolve().parent / "pass-through.json").read_bytes()
    )

    optimized = dict(recipe, patchSubdivisions=2, trianglesPerModel=8)
    write_json(root / "optimized.json", optimized)
    optimized_report = agent("jo-ja-plan", "optimized.json")
    write_json(root / "optimized-report.json", optimized_report)
    optimized["reimport"] = overlay
    write_json(root / "optimized-reimport.json", optimized)
    blocked = agent("jo-ja-plan", "optimized-reimport.json", expect=4)
    write_json(root / "optimized-conflicts.json", blocked)
    conflicts = (blocked.get("reimport") or blocked.get("details", {}).get("reimport", {})).get(
        "conflicts", []
    )
    if not any(row["entry"].endswith("gallery.jkl") for row in conflicts):
        raise ValueError("Optimization must report the affected authored level before publication")
    agent(
        "jo-ja-apply",
        "optimized-reimport.json",
        "unreviewed.gob",
        reviewed["outputSha256"],
        reviewed["manifestSha256"],
        expect=4,
    )
    if (root / "unreviewed.gob").exists():
        raise ValueError("Conflicting geometry was unexpectedly published")
    # A mapper can review this original map's explicit mapping and repeat the declarations on the
    # new generated baseline. It is safe only when the selected source surface still has one owner.
    agent(
        "jo-ja-apply",
        "optimized.json",
        "generated-optimized.gob",
        optimized_report["outputSha256"],
        optimized_report["manifestSha256"],
    )
    opt_members = gob_read((root / "generated-optimized.gob").read_bytes())
    owners = [i for i, m in enumerate(optimized_report["models"]) if 2 in m["sourceSurfaces"]]
    if len(owners) != 1:
        raise ValueError("Ambiguous optimized grate owner, manual mapping required")
    owner = owners[0]
    mapped = dict(preserved)
    mapped["jkl/gallery.jkl"] = authored_level(
        opt_members["jkl/gallery.jkl"], owner, optimized_report["models"][owner]["sector"]
    )
    opt_members.update(mapped)
    opted = dict(optimized)
    opted.pop("reimport")
    rebased = {
        "baselineRecipe": opted,
        "baselineOutputSha256": optimized_report["outputSha256"],
        "authoredArchive": asset("authored-optimized.gob", gob_write(opt_members)),
        "overrides": overlay["overrides"],
    }
    write_json(root / "reviewed-optimization.json", dict(opted, reimport=rebased))
    revised = agent("jo-ja-plan", "reviewed-optimization.json")
    agent(
        "jo-ja-apply",
        "reviewed-optimization.json",
        "gallery-optimized.gob",
        revised["outputSha256"],
        revised["manifestSha256"],
    )
    opt_out = gob_read((root / "gallery-optimized.gob").read_bytes())
    if any(opt_out.get(n) != data for n, data in mapped.items()):
        raise ValueError("Reviewed optimization lost an authored member")
    write_json(
        root / "mapping-decision.json",
        {
            "sourceSha256": source["sha256"],
            "sourceSurface": 2,
            "previousModel": baseline["models"][grate]["name"],
            "approvedNewModel": optimized_report["models"][owner]["name"],
            "decision": "Bind the projectile sensor to the unique regenerated owner of the authored gallery grate",
            "previousTriangles": baseline["triangles"],
            "newTriangles": optimized_report["triangles"],
            "previousArchive": reviewed["outputSha256"],
            "approvedArchive": revised["outputSha256"],
        },
    )
    (root / "episode.jk").write_bytes(preserved["episode.jk"])
    (root / "gallery.jkl").write_bytes(preserved["jkl/gallery.jkl"])
    (root / "scenario.json").write_bytes(
        (Path(__file__).resolve().parent / "scenario.json").read_bytes()
    )
    write_json(
        root / "red-project.json",
        {
            "schemaVersion": 1,
            "episode": "episode.jk",
            "levels": [{"export": "gallery.jkl"}],
            "target": {"game": "jk", "engine": "OpenJKDF2", "renderer": "OpenGL"},
            "mounts": [
                {"kind": "project", "path": "gallery.gob", "sha256": reviewed["outputSha256"]}
            ],
            "scenarios": [
                {
                    "name": "wind-gallery-contact",
                    "level": "gallery.jkl",
                    "definition": "scenario.json",
                    "receipts": [],
                }
            ],
        },
    )
    write_json(
        root / "acceptance.json",
        {
            "generatedArchive": baseline["outputSha256"],
            "authoredArchive": digest(edited),
            "reimportedArchive": reviewed["outputSha256"],
            "approvedOptimizedArchive": revised["outputSha256"],
            "originalGeometry": {
                "nativeFaces": baseline["nativeFaces"],
                "models": len(baseline["models"]),
                "patches": baseline["patches"],
                "portalPairs": baseline["portalPairs"],
            },
            "optimizedGeometry": {
                "nativeFaces": optimized_report["nativeFaces"],
                "models": len(optimized_report["models"]),
                "patches": optimized_report["patches"],
                "portalPairs": optimized_report["portalPairs"],
            },
            "authoredMembers": {name: digest(data) for name, data in preserved.items()},
            "runtimeVerified": False,
        },
    )


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("destination", type=Path)
    parser.add_argument("--agent", required=True, type=Path)
    args = parser.parse_args()
    create(args.destination.resolve(), args.agent.resolve())
