"""Rebuild the Bespin glass placement overlay for four pinned native editions."""
import argparse
from hashlib import sha256
from pathlib import Path
import re
import sys
import tempfile

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parents[1]))
sys.path.insert(0, str(HERE.parent))
from nar_glass.build import publish_overlay
from verify_episode_pair import archive_index, require


PARENTS = {
    "707e3b32ae6030f93cc27e3528ad4b1df7ddf06e0f788bf40eaee9ce93ea26a3": "jk",
    "861352f45723e8655b0b5af6232d3fa9468a3c6cb3cc09fde6ac2cef27c6dc6d": "jk",
    "30c898ccb176c58a01120f6675e123ec633088d9e6cbf7cb563a080aa2fa2632": "mots",
    "d981b1057d5ac477fe721e2b7fe75fd7288868430bcb597f794d0fc7dc8f91db": "mots",
}
OUTPUT_DIGESTS = {
    "jk": "21851f855d899f7c50e18b2375edd74afc77f8fc3424bcb494346bfac1c52b1f",
    "mots": "81a6b253c3aed565bf2c9ac1d60dbf544c291819c37859d3c16d05466b0bddea",
}
JKL = "jkl/bespin.jkl"
PANES = {269: "glass2.3do", 270: "glass3.3do"}
ARCHITECTURE = range(219)


def table_rows(lines, label):
    starts = [i for i, line in enumerate(lines) if re.fullmatch(label + r"[ \t]+\d+", line.strip(), re.I)]
    require(len(starts) == 1, f"Expected one {label} table")
    for i in range(starts[0] + 1, len(lines)):
        fields = lines[i].partition("#")[0].lower().split()
        if fields == ["end"]:
            return
        if fields:
            yield i, fields
    raise ValueError(f"Unterminated {label} table")


def properties(fields):
    return dict(value.split("=", 1) for value in fields if "=" in value)


def placements(lines):
    templates = {"none": {}}
    for _, fields in table_rows(lines, "World templates"):
        name, base, *values = fields
        require(name not in templates and base in templates, f"Invalid template ancestry for {name}")
        templates[name] = {**templates[base], **properties(values)}

    things = {}
    for _, fields in table_rows(lines, "World things"):
        require(len(fields) >= 10 and fields[0].endswith(":"), "Invalid placement")
        index = int(fields[0][:-1])
        require(index not in things and fields[1] in templates, f"Invalid placement {index}")
        things[index] = {**templates[fields[1]], **properties(fields[10:])}
    return things


def edit_flag(line, old, new):
    code, comment, tail = line.partition("#")
    updated, n = re.subn(r"(?<!\S)thingflags=" + old + r"(?=\s|$)", "thingflags=" + new, code)
    require(n == 1, "Expected exactly one authored flag override")
    return updated + comment + tail


def repair_jkl(source, game):
    require(game in OUTPUT_DIGESTS, "Unknown game")
    lines = source.decode("ascii").splitlines(keepends=True)
    before = placements(lines)
    expected = set(PANES)
    if game == "jk":
        expected.update(ARCHITECTURE)
    early = 0x4000040 if game == "jk" else 0x48
    for idx in ARCHITECTURE:
        attrs = before.get(idx, {})
        require(attrs.get("model3d") == f"bp{idx:03}.3do" and attrs.get("type") == "cog"
                and attrs.get("collide") == "3" and int(attrs.get("numframes", 0)) == 0
                and int(attrs.get("thingflags", "0"), 0) == early,
                f"Unexpected architecture placement {idx}")
    for idx, model in PANES.items():
        attrs = before.get(idx, {})
        require(attrs.get("model3d") == model and attrs.get("collide") == "3"
                and int(attrs.get("thingflags", "0"), 0) == 8,
                f"Unexpected glass placement {idx}")

    for i, fields in table_rows(lines, "World templates"):
        if fields[:2] == ["_structure", "_decor"]:
            lines[i] = edit_flag(lines[i], "0x8", "0x0")
    if game == "jk":
        for i, fields in table_rows(lines, "World things"):
            if int(fields[0][:-1]) in ARCHITECTURE:
                lines[i] = edit_flag(lines[i], "0x4000040", "0x4000048")

    after = placements(lines)
    changed = {idx for idx in before if before[idx] != after[idx]}
    require(changed == expected, "Unexpected set of inherited flag changes")
    for idx in changed:
        desired = int(before[idx]["thingflags"], 0) ^ 8
        require(after[idx] == {**before[idx], "thingflags": hex(desired)},
                f"Unexpected effective properties for placement {idx}")
    return "".join(lines).encode("ascii")


def stream_digest(stream):
    digest = sha256()
    for block in iter(lambda: stream.read(1024 * 1024), b""):
        digest.update(block)
    return digest.hexdigest()


def build(parent, output):
    if output.exists() or output.is_symlink():
        raise FileExistsError(output)
    with parent.open("rb") as stream:
        source_hash = stream_digest(stream)
        require(source_hash in PARENTS, "Unsupported parent SHA-256")
        game = PARENTS[source_hash]
        stream.seek(0)
        offset, size = archive_index(stream)[JKL]
        stream.seek(offset)
        result = repair_jkl(stream.read(size), game)
    require(sha256(result).hexdigest() == OUTPUT_DIGESTS[game], "Unexpected repaired JKL digest")
    with tempfile.TemporaryDirectory(dir=output.parent) as tmp:
        stage = Path(tmp)
        path = stage / JKL
        path.parent.mkdir()
        path.write_bytes(result)
        with path.open("rb") as reread, parent.open("rb") as source:
            require(stream_digest(reread) == OUTPUT_DIGESTS[game], "Staged JKL readback failed")
            require(stream_digest(source) == source_hash, "Parent changed during build")
        publish_overlay(output, stage, [JKL])
    return game


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--parent", type=Path, required=True)
    parser.add_argument("--overlay", type=Path, required=True)
    args = parser.parse_args()
    try:
        game = build(args.parent, args.overlay)
    except (OSError, ValueError, KeyError) as error:
        parser.exit(1, f"{error}\n")
    print(f"{game} {JKL} {OUTPUT_DIGESTS[game]}")


if __name__ == "__main__":
    main()
