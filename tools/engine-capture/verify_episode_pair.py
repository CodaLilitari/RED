"""Check two retail texture editions for matching episode and gameplay data."""
import argparse
from contextlib import contextmanager
import json
from pathlib import Path, PurePosixPath
import re
import shutil
import struct
import sys
import tempfile
import zipfile


def require(condition, message):
    if not condition:
        raise ValueError(message)


def archive_index(stream, *, max_entries=65536):
    size = stream.seek(0, 2)
    stream.seek(0)
    header = stream.read(12)
    require(len(header) == 12, "Truncated GOB header")
    magic, version, directory = struct.unpack("<4sII", header)
    require((magic, version) == (b"GOB ", 20) and 12 <= directory <= size - 4,
            "Expected a Jedi Knight GOB/GOO v0x14 directory")
    stream.seek(directory)
    count, = struct.unpack("<I", stream.read(4))
    table_end = directory + 4 + count * 136
    require(0 < count <= max_entries and table_end <= size, "Invalid GOB directory length")
    entries = {}
    for _ in range(count):
        offset, length, raw = struct.unpack("<II128s", stream.read(136))
        name, nul, _ = raw.partition(b"\0")
        name = name.decode("ascii").lower().replace("\\", "/")
        require(nul and not re.search(r'[\x00-\x1f:*?"<>|]', name)
                and all(p not in ("", ".", "..") for p in name.split("/")),
                f"Invalid GOB entry name {name!r}")
        require(name not in entries, f"Duplicate GOB entry {name}")
        require(offset >= 12 and offset + length <= size and
                (offset + length <= directory or offset >= table_end),
                f"{name} overlaps the directory or lies outside the GOB")
        entries[name] = offset, length
    return entries


@contextmanager
def open_edition(path):
    if path.suffix.lower() == ".zip":
        with zipfile.ZipFile(path) as z:
            archives = [i for i in z.infolist() if not i.is_dir() and
                        i.filename.lower().endswith((".gob", ".goo"))]
            require(len(archives) == 1, f"{path.name} must contain exactly one GOB or GOO")
            entry = archives[0]
            require(16 <= entry.file_size <= 2 << 30, "Episode archive size outside supported range")
            # Some native GOBs place the directory at EOF. Seek on a temporary file
            # so reading each member doesn't inflate the outer ZIP again.
            with z.open(entry) as packed, tempfile.TemporaryFile() as stream:
                shutil.copyfileobj(packed, stream)
                name = PurePosixPath(entry.filename.replace("\\", "/")).name.lower()
                yield name, stream, archive_index(stream)
    else:
        require(path.suffix.lower() in (".gob", ".goo"), "Expected an episode ZIP, GOB or GOO")
        with path.open("rb") as stream:
            yield path.name.lower(), stream, archive_index(stream)


def material_layout(data):
    require(len(data) >= 76, "Truncated MAT header")
    magic, version, kind, cels, _, mode, bpp = struct.unpack_from("<4s6I", data)
    require((magic, version) == (b"MAT ", 0x32) and cels > 0, "Expected a native MAT header")
    require((bpp == 8 and mode in (0, 3)) or (bpp == 16 and mode in (1, 2)),
            f"Unsupported retail edition color mode {mode} / {bpp} bpp")
    if kind == 0:
        require(bpp == 8 and len(data) >= 100, "Invalid indexed solid-color MAT")
        return bpp, ()
    require(kind == 2, f"Unsupported MAT type {kind}")
    offset = 76 + 40 * cels
    layout = []
    for _ in range(cels):
        require(offset + 24 <= len(data), "Truncated MAT cel")
        w, h, flags, _, _, mips = struct.unpack_from("<6I", data, offset)
        require(w and h and 0 < mips <= min(w, h).bit_length(), "Invalid MAT dimensions or mip count")
        layout.append((w, h, bool(flags & 1)))
        offset += 24 + sum((w >> m) * (h >> m) * (bpp // 8) for m in range(mips))
        require(offset <= len(data), "Truncated MAT mip pixels")
    return bpp, tuple(layout)


def material_reserve(data):
    match = re.search(rb"(?im)^[ \t]*section\s*:\s*materials[ \t]*(?:[#;][^\n]*)?\r?$", data)
    require(match, "Missing JKL materials section")
    rows = []
    for raw in data[match.end():].splitlines():
        line = re.split(rb"[#;]", raw, maxsplit=1)[0].strip().lower()
        if line == b"end" or line.startswith(b"section:"):
            break
        if line:
            rows.append(line)
    require(rows and re.fullmatch(rb"world\s+materials\s+\d+", rows[0]),
            "Missing WORLD MATERIALS declaration")
    capacity = int(rows[0].split()[-1])
    for i, row in enumerate(rows[1:]):
        entry = re.match(rb"(\d+)\s*:\s*\S+", row)
        require(entry and int(entry[1]) == i, f"Expected contiguous material row {i}")
    named = len(rows) - 1
    return {"capacity": capacity, "listed": named, "free": capacity - named}


def verify(indexed, hardware, minimum=0):
    require(minimum >= 0, "Minimum free material slots must be nonnegative")
    with open_edition(indexed) as (name, left, a), open_edition(hardware) as (other, right, b):
        require(name == other, f"Installed filenames differ: {name} and {other}")
        different = a.keys() ^ b.keys()
        require(not different, f"Archive inventories differ: {', '.join(sorted(different)[:10])}")
        require("episode.jk" in a and any(n.startswith("jkl/") and n.endswith(".jkl") for n in a),
                "Expected episode.jk and at least one JKL in the episode")
        levels = {}
        shared_indexed = converted = 0
        for key, (offset, size) in a.items():
            left.seek(offset)
            right.seek(b[key][0])
            x, y = left.read(size), right.read(b[key][1])
            if key.endswith(".mat"):
                try:
                    bits, shape = material_layout(x)
                    hw_bits, hw_shape = (bits, shape) if x == y else material_layout(y)
                    require(bits == 8, "The indexed edition contains a truecolor MAT")
                    if x == y:
                        shared_indexed += 1
                    else:
                        require(hw_bits == 16, "An indexed MAT differs between editions")
                        require(shape == hw_shape, "Cel dimensions, count or keying differ")
                        require(not any(keyed for _, _, keyed in shape),
                                "The editions must share indexed color-keyed MATs byte for byte")
                        converted += 1
                except ValueError as error:
                    raise ValueError(f"{key}: {error}") from error
            else:
                require(x == y, f"Shared resource differs: {key}")
                if key.endswith(".jkl"):
                    reserve = material_reserve(x)
                    require(reserve["free"] >= minimum,
                            f"{key}: {reserve['free']} free material slots; required {minimum}")
                    levels[key] = reserve
        require(converted > 0, "The hardware edition has no 16-bit MAT replacements")
        return {"installedArchive": name, "entries": len(a), "sharedIndexedMaterials": shared_indexed,
                "convertedMaterials": converted, "levels": levels}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("indexed", type=Path, help="8-bit edition ZIP, GOB or GOO")
    parser.add_argument("hardware", type=Path, help="16-bit edition ZIP, GOB or GOO")
    parser.add_argument("--min-free-materials", type=int, default=0)
    args = parser.parse_args()
    try:
        print(json.dumps(verify(args.indexed, args.hardware, args.min_free_materials), indent=2))
    except (OSError, ValueError, RuntimeError, zipfile.BadZipFile) as error:
        sys.exit(str(error))
