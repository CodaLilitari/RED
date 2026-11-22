"""Rebuild the approved JK16 Nar glass variant from its exact parent GOB."""
import argparse
from contextlib import contextmanager
import ctypes
import errno
import hashlib
import io
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile


HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parents[1]))
sys.path.insert(0, str(HERE.parent))
from compare_episode_releases import BLOCK, source_identity
from invoke_engine_capture import sha256_bytes as digest, sha256_file
from verify_episode_pair import archive_index, require
from nar_glass.material import glass_material
from nar_glass.model import split_model


MAX_PARENT_BYTES = 53_592_641
JKL = "jkl/nsstreets.jkl"
DONORS = ("nv111", "nv118", "nv119", "nv120", "nv126", "nv127", "nv181")
EARLY_ARCHITECTURE = {*range(4), *range(13, 140)}


def load_recipe(path):
    spec = json.loads(path.read_text(encoding="utf-8"))
    canonical = json.dumps(spec, sort_keys=True, separators=(",", ":")).encode("ascii")
    # This is a closed recipe. Editing metadata must never turn acceptance
    # hashes into caller-specified input or publication criteria.
    require(digest(canonical) == "823f16df63a01cd21df9d59d925af7f0add6b36dcd1fe77f02779c66d73a79c5",
            "Unsupported or modified recipe manifest")
    return spec


@contextmanager
def open_parent(path, max_bytes=MAX_PARENT_BYTES):
    flags = os.O_RDONLY | getattr(os, "O_BINARY", 0) | getattr(os, "O_NONBLOCK", 0)
    with os.fdopen(os.open(path, flags), "rb") as source:
        identity = source_identity(source)
        size = identity[0]
        require(size <= max_bytes, "parent exceeds the supported GOB/GOO size limit")
        yield source, size
        require(source.tell() == size and not source.read(1) and source_identity(source) == identity,
                "parent changed during capture")


def check_parent(path, expected, max_bytes=MAX_PARENT_BYTES):
    observed = hashlib.sha256()
    with open_parent(path, max_bytes) as (source, size):
        for offset in range(0, size, BLOCK):
            observed.update(source.read(min(BLOCK, size - offset)))
    require(observed.hexdigest() == expected, "parent changed during staging")


def check_overlay(stage, expected):
    observed = {p.relative_to(stage).as_posix(): sha256_file(p)
                for p in stage.rglob("*") if p.is_file()}
    require(observed == expected, "Staged overlay disk readback differs from the accepted members")


def capture_parent(path, expected):
    # Capture once. A path replacement during generation or RED assembly cannot
    # change the bytes against which the recipe was admitted.
    with open_parent(path) as (source, size):
        data = source.read(size)
    require(digest(data) == expected, "parent SHA-256 does not match the supported JK16 GOB")
    return data, archive_index(io.BytesIO(data))


def order_panes(rows, pane_ids):
    pairs = [(int(row.split(":", 1)[0]), row) for row in rows]
    ids = {i for i, _ in pairs}
    require(len(ids) == len(rows) and pane_ids <= ids, "Invalid or duplicate pane placement ID")
    return [row for i, row in pairs if i in pane_ids] + [row for i, row in pairs if i not in pane_ids]


def revise_placements(original):
    # Keep every existing ID and template. Resource paths and model names here
    # describe this parent only. Mixed movers in another map need a
    # separate design for movement and collision.
    text = original.decode("ascii").replace("\r\n", "\n")
    pane_ids = {int(i) for i in re.findall(r"(?m)^\d+: nsglass\.cog (\d+) \d+$", text)}
    require(len(pane_ids) == 36, "Expected 36 breakable panes")
    things = re.search(r"(?im)^world things 494\n((?:\d+:[^\n]*\n)+)end$", text)
    models = re.search(r"(?im)^world models 568\n((?:\d+:[^\n]*\n)+)", text)
    require(things and models and models.end() < things.start(), "Unexpected resource tables")
    rows = order_panes(things[1].splitlines(), pane_ids)
    require({int(row.split(":", 1)[0]) for row in rows} == set(range(374)), "Unexpected original placement inventory")
    new_models, new_glass = [], []
    for i, donor in enumerate(DONORS):
        name = "ng" + donor[2:]
        donor_rows = [r.split() for r in rows if r.split()[1:3] == [donor, donor]]
        require(len(donor_rows) == 1, "Expected a unique fixed donor")
        fields = donor_rows[0]
        require(len(fields) == 10 and fields[6:] == ["0", "0", "0", "0"], "Unexpected donor transform")
        new_models.append(f"{329+i}: {name}.3do\n")
        new_glass.append(" ".join([f"{374+i}:", donor, name, *fields[3:], f"model3d={name}.3do"]))
    for i, row in enumerate(rows):
        if int(row.split(":", 1)[0]) in EARLY_ARCHITECTURE:
            rows[i] = row + " thingflags=0x4000048"
    text = (text[:models.end()] + "".join(new_models) + text[models.end():things.start(1)]
            + "\n".join(new_glass + rows) + "\n" + text[things.end(1):])
    return text.replace("\n", "\r\n").encode("ascii")


def create_overlay(data, index, spec):
    def member(name):
        offset, size = index[name]
        return data[offset:offset + size]
    overlay = {JKL: revise_placements(member(JKL)), "mat/ns67.mat": glass_material()}
    for donor in DONORS:
        name = f"3do/{donor}.3do"
        generated = "3do/ng" + donor[2:] + ".3do"
        require(generated not in index, "Additional model name already occupied")
        old = member(name).decode("ascii").replace("\r\n", "\n")
        retained, pane = split_model(old)
        overlay[name] = retained.replace("\n", "\r\n").encode("ascii")
        overlay[generated] = pane.replace("\n", "\r\n").encode("ascii")
    require({name: digest(payload) for name, payload in overlay.items()} == spec["overlay"],
            "Generated overlay differs from the accepted member SHA-256 inventory")
    return overlay


def publish_overlay(output, stage):
    """Transfer the complete staged directory to a new name in one operation."""
    if sys.platform == "win32":
        # Windows rename refuses an existing file or directory.
        os.rename(stage, output)
        return

    source, target = os.fsencode(stage), os.fsencode(output)
    if sys.platform.startswith("linux"):
        native_name = "renameat2"
        at_fdcwd, rename_noreplace = -100, 1
        argtypes = (ctypes.c_int, ctypes.c_char_p, ctypes.c_int, ctypes.c_char_p, ctypes.c_uint)
        args = (at_fdcwd, source, at_fdcwd, target, rename_noreplace)
    elif sys.platform == "darwin":
        native_name = "renamex_np"
        rename_excl = 0x4
        argtypes = (ctypes.c_char_p, ctypes.c_char_p, ctypes.c_uint)
        args = (source, target, rename_excl)
    else:
        raise OSError(errno.ENOTSUP, "atomic create-only directory publication is unavailable")

    rename = getattr(ctypes.CDLL(None, use_errno=True), native_name, None)
    if rename is None:
        raise OSError(errno.ENOTSUP, f"{native_name} is unavailable for atomic directory publication")
    rename.argtypes = argtypes
    rename.restype = ctypes.c_int
    if rename(*args) != 0:
        error = ctypes.get_errno()
        raise OSError(error, os.strerror(error), output)


def pack_archive(output, red, parent, data, index, overlay, spec):
    names = sorted(index.keys() | overlay.keys())
    require(len(names) == spec["archiveMembers"] and len(names) - len(overlay) == spec["unchangedMembers"],
            "Unexpected merged archive inventory")
    with tempfile.TemporaryDirectory(dir=output.parent) as scratch:
        root = Path(scratch).resolve()
        loose = root / "loose"
        entries = []
        for name in names:
            if name in overlay:
                payload = overlay[name]
            else:
                offset, size = index[name]
                payload = memoryview(data)[offset:offset + size]
            dest = loose / name
            dest.parent.mkdir(parents=True, exist_ok=True)
            dest.write_bytes(payload)
            entries.append({"name": name, "path": str(dest), "sha256": digest(payload)})
        valid = subprocess.run([str(red), "validate", str(loose / JKL), "--strict"],
                               capture_output=True, text=True, check=True)
        require(json.loads(valid.stdout).get("valid") is True, "RED rejected the revised JKL")
        request = root / "request.json"
        request.write_text(json.dumps({"schemaVersion": 1, "entries": entries}), encoding="utf-8")
        candidate = root / "nsstreets.gob"
        subprocess.run([str(red), "archive-create", str(request), str(candidate)],
                       stdout=subprocess.DEVNULL, check=True)
        actual = sha256_file(candidate)
        require(actual == spec["archiveSha256"], "RED archive differs from the accepted GOB SHA-256")
        check_parent(parent, spec["parentSha256"])
        os.link(candidate, output)  # Publish the verified file without replacing a racing destination.


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--parent", required=True, type=Path)
    dest = parser.add_mutually_exclusive_group(required=True)
    dest.add_argument("--overlay", type=Path, metavar="NEW_DIRECTORY")
    dest.add_argument("--archive", type=Path, metavar="NEW_GOB")
    parser.add_argument("--red", type=Path, metavar="EXECUTABLE")
    args = parser.parse_args()
    if bool(args.red) != bool(args.archive):
        parser.error("--archive requires --red; --overlay uses no external program")
    try:
        output = args.archive or args.overlay
        require(not os.path.lexists(output), "destination already exists")
        require(output.parent.is_dir(), "destination parent directory must exist")
        spec = load_recipe(HERE / "recipe.json")
        data, index = capture_parent(args.parent, spec["parentSha256"])
        overlay = create_overlay(data, index, spec)
        if args.archive:
            pack_archive(output, args.red.resolve(strict=True), args.parent, data, index, overlay, spec)
        else:
            with tempfile.TemporaryDirectory(dir=output.parent) as scratch:
                stage = Path(scratch) / "overlay"
                stage.mkdir()
                for name, payload in overlay.items():
                    target = stage / name
                    target.parent.mkdir(parents=True, exist_ok=True)
                    target.write_bytes(payload)
                check_overlay(stage, spec["overlay"])
                check_parent(args.parent, spec["parentSha256"])
                publish_overlay(output, stage)
        print(json.dumps({"parentSha256": spec["parentSha256"], "overlayMembers": len(overlay),
                          "archiveSha256": spec["archiveSha256"] if args.archive else None}))
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"{parser.prog}: {error}\n")


if __name__ == "__main__":
    main()
