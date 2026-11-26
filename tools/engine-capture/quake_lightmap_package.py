"""Encode a reviewed Quake lightmap atlas and publish matching JK or MotS episode archives."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

from mots_baked_draw_pass import switch_draw_pass
from quake_lightmap_atlas import file_digest as digest, table
from verify_episode_pair import material_reserve, open_edition, require, verify

MAX_BYTES = 512 << 20


def verify_files(folder, hashes, suffix, captured):
    require(isinstance(hashes, dict) and 0 < len(hashes) <= 4096, "Invalid atlas file inventory")
    result = {}
    total = 0
    for name, expected in sorted(hashes.items()):
        require(re.fullmatch(r"[a-z0-9_-]{1,63}" + re.escape(suffix), name) and
                isinstance(expected, str) and re.fullmatch(r"[0-9a-f]{64}", expected),
                f"Invalid atlas file binding: {name!r}")
        path = folder / name
        size = path.stat().st_size
        total += size
        require(0 < size <= 8 << 20 and total <= MAX_BYTES, f"Atlas input exceeds limit: {name}")
        private = captured / name
        shutil.copyfile(path, private)
        require(private.stat().st_size == size and digest(private) == expected,
                f"Atlas input size or digest mismatch: {name}")
        result[name] = private
    return result


def check_model_materials(parent, updated, page_names):
    def materials(raw):
        _, names = table(raw.decode("ascii").replace("\r\n", "\n"), "MATERIALS")
        return {name.lower() for name in names}

    missing = materials(updated) - materials(parent) - page_names
    require(not missing, f"New model MAT missing from atlas: {', '.join(sorted(missing))}")


def append_pages(raw, page_names):
    """Splice into one native table, retaining all other bytes and existing runtime headroom."""
    matches = list(re.finditer(rb"(?im)^[ \t]*section[ \t]*:[ \t]*materials[ \t]*(?:[#;][^\r\n]*)?\r?$", raw))
    require(len(matches) == 1, "Expected exactly one materials section")
    start = matches[0].end()
    end = re.search(rb"(?im)^[ \t]*(end\b|section[ \t]*:)", raw[start:])
    require(end and end[1].lower() == b"end", "Unterminated materials section")
    offset = start + end.start()
    block = raw[start:offset]
    stats = material_reserve(raw)
    header = re.search(rb"(?im)^[ \t]*world[ \t]+materials[ \t]+(\d+)", block)
    require(stats["free"] >= 0 and header, "Invalid material capacity")
    new = [name.encode("ascii") for name in page_names]
    existing = set(re.findall(rb"(?im)^[ \t]*\d+[ \t]*:[ \t]*([^ \t\r\n]+)", block.lower()))
    require(len(set(new)) == len(new) and not existing.intersection(new), "Atlas MAT already registered")
    newline = b"\r\n" if b"\r\n" in block else b"\n"
    rows = b"".join(str(i).encode() + b": " + name + b" 1.000000 1.000000" + newline
                    for i, name in enumerate(new, stats["listed"]))
    total = stats["capacity"] + len(new)
    require(total <= 2**31 - 1, "Material capacity overflow")
    return (raw[:start] + block[:header.start(1)] + str(total).encode() +
            block[header.end(1):] + rows + raw[offset:])


def red(executable, command, request, destination):
    result = subprocess.run([str(executable), command, "-", str(destination)],
                            input=json.dumps(request).encode(), capture_output=True, timeout=120)
    if result.returncode or not destination.is_file():
        raise ValueError(f"{command} failed: " + (result.stderr or result.stdout).decode(errors="replace")[-1200:])


def ref(path):
    return {"path": str(path.resolve()), "sha256": digest(path)}


def recommend(pages, master):
    """Allocate candidate colors by weighted error with stock palette entries held fixed."""
    import numpy as np
    from PIL import Image
    from scipy.spatial import cKDTree

    histogram = np.zeros(32768, np.int64)
    for page in pages:
        with Image.open(page) as image:
            require(image.format == "PNG" and image.size == (256, 256) and image.mode == "RGB",
                    f"Expected a 256Ã—256 RGB atlas page: {page.name}")
            rgb = np.asarray(image).reshape(-1, 3)[::3].astype(np.uint16) // 8
        histogram += np.bincount(rgb[:, 0] * 1024 + rgb[:, 1] * 32 + rgb[:, 2], minlength=32768)
    occupied = np.flatnonzero(histogram)
    colors = np.stack((occupied // 1024, occupied // 32 % 32, occupied % 32), axis=1) * 8 + 3.5
    weights = histogram[occupied].astype(float) ** .82
    require(len(master) >= 832 and master[:4] == b"CMP ", "Expected a native master CMP")
    fixed = np.unique(np.frombuffer(master[64:832], np.uint8).reshape(-1, 3)[1:192], axis=0)
    distance = cKDTree(fixed).query(colors)[0] ** 2
    seeds = []
    for _ in range(min(len(colors), 63)):
        ix = int(np.argmax(distance * weights))
        if distance[ix] == 0:
            break
        seeds.append(colors[ix])
        distance = np.minimum(distance, np.sum((colors - colors[ix]) ** 2, axis=1))
    if not seeds:
        return []
    seeds = np.array(seeds)
    for _ in range(14):
        assignments = cKDTree(np.vstack([fixed, seeds])).query(colors)[1]
        for i in range(len(seeds)):
            selected = assignments == len(fixed) + i
            if selected.any():
                seeds[i] = np.average(colors[selected], axis=0, weights=weights[selected])
    return np.unique(np.rint(seeds).astype(np.uint8), axis=0).tolist()


def expand(archive, folder):
    """Use private numeric scratch names; archive member names are logical RED destinations."""
    folder.mkdir()
    entries = {}
    with open_edition(archive) as (_, stream, index):
        require(len(index) <= 4096 and sum(size for _, size in index.values()) <= MAX_BYTES,
                "Parent episode exceeds RED archive-create limits")
        for i, (name, (offset, size)) in enumerate(index.items()):
            path = folder / (str(i) + Path(name).suffix)
            require(size <= 256 << 20, "Episode member exceeds RED input limit")
            stream.seek(offset)
            path.write_bytes(stream.read(size))
            entries[name] = path
    return entries


def package(args, work, result):
    left, right = args.indexed, args.hardware
    require(left.suffix.lower() in (".gob", ".goo") and left.name.lower() == right.name.lower(),
            "Provide like-named bare indexed and hardware episodes")
    require(all(p.stat().st_size <= MAX_BYTES for p in (left, right)), "Episode exceeds RED size limit")
    for folder, path in (("input-8", left), ("input-16", right)):
        (work / folder).mkdir()
        shutil.copyfile(path, work / folder / path.name)
    left, right = work / "input-8" / left.name, work / "input-16" / right.name
    verify(left, right)
    require(args.atlas.joinpath("receipt.json").stat().st_size <= 16 << 20, "Atlas receipt too large")
    receipt = json.loads((args.atlas / "receipt.json").read_bytes())
    pages = verify_files(args.atlas / "rgb", receipt["pageSha256"], ".png", work)
    models = verify_files(args.atlas / "models", receipt["modelSha256"], ".3do", work)
    require(len(pages) == receipt["pageCount"] and len(models) == receipt["modelCount"],
            "Incomplete atlas page or model inventory")
    parents = receipt["parentModelSha256"]
    require(models.keys() == parents.keys(), "Replacement and parent model inventories differ")
    editions = {label: expand(path, work / label) for label, path in [("indexed8", left), ("hardware", right)]}
    first = editions["indexed8"]
    require([k for k in first if k.startswith("jkl/") and k.endswith(".jkl")] == [args.level],
            "Expected a single-level episode with the selected JKL")
    require(args.palette_member in first and args.palette_member.startswith("misc/cmp/")
            and args.palette_member.endswith(".cmp"), "Missing selected episode CMP")
    names = [p.stem + ".mat" for p in pages.values()]
    page_names = set(names)
    for key, path in models.items():
        name = "3do/" + key
        require(name in first and digest(first[name]) == parents[key], f"Wrong parent model: {key}")
        check_model_materials(first[name].read_bytes(), path.read_bytes(), page_names)
    require(all("mat/" + n not in first for n in names), "New MAT collides with a parent archive member")
    old_jkl = first[args.level].read_bytes()
    colormaps = re.findall(rb"(?im)^[ \t]*world[ \t]+colormaps[ \t]+1[ \t]*\r?\n[ \t]*0:[ \t]*([^\s#;]+)",
                           old_jkl.lower())
    require(colormaps == [Path(args.palette_member).name.encode("ascii")],
            "Expected the selected master as the level's single colormap")
    draw_pass = None
    if args.mots_parent:
        require(left.suffix.lower() == ".goo", "Ordinary ARCH draw pass requires a MotS GOO")
        old_jkl, draw_pass = switch_draw_pass(old_jkl, {k: p.read_bytes() for k, p in models.items()},
                                              args.mots_parent)
    jkl = work / "level.jkl"
    jkl.write_bytes(append_pages(old_jkl, names))
    master = first[args.palette_member]
    palette = work / "palette.cmp"
    colors = recommend(pages.values(), master.read_bytes())
    if colors:
        protected = [left, *args.protect]
        red(args.red_agent, "cmp-master-create", {"schemaVersion": 1, "master": ref(master),
            "colors": [{"rgb": rgb} for rgb in colors], "protectArchives": [ref(p) for p in protected]}, palette)
    else:
        shutil.copyfile(master, palette)
    palette_ref = ref(palette)
    outputs = {}
    for label, files in editions.items():
        encoded = work / (label + "-mat")
        encoded.mkdir()

        def encode(page):
            path = encoded / (page.stem + ".mat")
            request = {"format": "rgb565", "cels": [ref(page)], "mipmaps": 1}
            if label == "indexed8":
                request.update(format="indexed8", palette=palette_ref, dither=False, reserveIndexZero=True)
            red(args.red_agent, "mat-create", request, path)
            return "mat/" + path.name, path

        with ThreadPoolExecutor(max_workers=4) as pool:
            files.update(pool.map(encode, pages.values()))
        files.update({"3do/" + k: p for k, p in models.items()})
        files.update({args.level: jkl, args.palette_member: palette})
        folder = result / label
        folder.mkdir()
        destination = folder / left.name
        red(args.red_agent, "archive-create", {"entries": [{"name": k, **ref(p)} for k, p in files.items()]},
            destination)
        outputs[label] = destination
    paired = verify(outputs["indexed8"], outputs["hardware"], material_reserve(jkl.read_bytes())["free"])
    return {"atlasReceiptSha256": digest(args.atlas / "receipt.json"),
            "parentSha256": {"indexed8": digest(left), "hardware": digest(right)},
            "archiveSha256": {k: digest(p) for k, p in outputs.items()}, "paletteSha256": palette_ref["sha256"],
            "drawPass": draw_pass, "paired": paired}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("atlas", "indexed", "hardware", "output"):
        parser.add_argument(name, type=Path)
    parser.add_argument("--red-agent", type=Path, required=True, help="RED executable")
    parser.add_argument("--level", required=True, help="native JKL member, e.g. jkl/arena.jkl")
    parser.add_argument("--palette-member", required=True, help="episode master, e.g. misc/cmp/arena.cmp")
    parser.add_argument("--mots-parent", help="optional static world template for the proven ordinary draw pass")
    parser.add_argument("--protect", type=Path, action="append", default=[], help="extra archive using this CMP")
    args = parser.parse_args()
    require(not args.output.exists(), "Choose a new output directory")
    with tempfile.TemporaryDirectory(dir=args.output.parent) as tmp:
        work = Path(tmp)
        result = work / "result"
        result.mkdir()
        proof = package(args, work, result)
        (result / "receipt.json").write_text(json.dumps(proof, indent=2) + "\n", encoding="utf-8")
        args.output.mkdir()
        installed = []
        try:
            for child in result.iterdir():
                target = args.output / child.name
                child.rename(target)
                installed.append(target)
        except OSError:
            for target in installed:
                if target.is_dir():
                    shutil.rmtree(target)
                elif target.is_file():
                    target.unlink()
            args.output.rmdir()
            raise
    print(json.dumps(proof, indent=2))


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, KeyError, ImportError, subprocess.SubprocessError) as error:
        sys.exit(str(error))
