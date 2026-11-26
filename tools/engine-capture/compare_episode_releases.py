"""Compare the installed GOB/GOO and its resource bytes in two episode releases."""
import argparse
import copy
from contextlib import contextmanager
import hashlib
import json
import os
from pathlib import Path
import re
import stat
import struct
import sys
import tempfile
import zipfile
import zlib

from verify_episode_pair import archive_index, require


MAX_ARCHIVE = 512 * 1024 * 1024
MAX_ZIP = 1024 * 1024 * 1024
MAX_ZIP_DIRECTORY = 1024 * 1024
MAX_ZIP_MEMBERS = 4096
MAX_GOB_MEMBERS = 16384
BLOCK = 1024 * 1024


def check_zip_extra(extra, name):
    pos = 0
    while pos < len(extra):
        require(pos + 4 <= len(extra), "Truncated ZIP extra field")
        kind, size = struct.unpack_from("<HH", extra, pos)
        pos += 4
        require(pos + size <= len(extra), "Truncated ZIP extra data")
        require(kind != 1, "ZIP64 is unsupported")
        if kind == 0x7075:
            expected = struct.pack("<BI", 1, zlib.crc32(name)) + name
            require(extra[pos:pos + size] == expected, "Ambiguous ZIP Unicode path")
        pos += size


def validate_zip_directory(source, size):
    # ZipFile reads the entire central directory at once. Bound it before opening.
    source.seek(max(0, size - 65557))
    tail = source.read(65557)
    # A ZIP comment can contain arbitrary bytes, including another signature.
    # Use the actual directory/comment spans to identify the classic end record.
    for match in re.finditer(rb"PK\x05\x06", tail):
        pos = match.start()
        if pos + 22 > len(tail):
            continue
        _, disk, directory_disk, local_count, count, length, offset, comment = struct.unpack_from(
            "<4s4H2IH", tail, pos)
        end = size - len(tail) + pos
        if pos + 22 + comment == len(tail) and offset + length == end:
            break
    else:
        raise ValueError("Missing or incorrectly framed ZIP end record")
    require(disk == directory_disk == 0 and local_count == count and
            0 < count <= MAX_ZIP_MEMBERS and length <= MAX_ZIP_DIRECTORY,
            "Invalid or unsupported ZIP directory size or count")
    if end >= 20:
        source.seek(end - 20)
        require(source.read(4) != b"PK\x06\x07", "ZIP64 is unsupported")
    source.seek(offset)
    directory = source.read(length)
    pos = 0
    for _ in range(count):
        require(pos + 46 <= length and directory[pos:pos + 4] == b"PK\x01\x02",
                "Invalid ZIP central directory entry")
        names, extras, comments, disk = struct.unpack_from("<4H", directory, pos + 28)
        pos += 46
        require(disk == 0 and pos + names + extras + comments <= length, "Invalid ZIP member framing")
        check_zip_extra(directory[pos + names:pos + names + extras], directory[pos:pos + names])
        pos += names + extras + comments
    require(pos == length, "ZIP directory member count mismatch")
    return count, end


def zip_episode(archive, expected_count):
    infos = archive.infolist()
    require(len(infos) == expected_count, "ZIP directory member count mismatch")
    names = set()
    episodes = []
    for info in infos:
        raw = info.orig_filename.replace("\\", "/")
        require(info.flag_bits & 0x800 or raw.isascii(), "Use UTF-8 for non-ASCII ZIP names")
        name = raw.removesuffix("/")
        require(all(c.isprintable() and c not in ':*?"<>|' for c in name) and
                all(part not in ("", ".", "..") for part in name.split("/")), "Unsafe ZIP member name")
        key = name.casefold()
        require(key not in names, "Duplicate ZIP member name")
        names.add(key)
        if not raw.endswith("/") and key.endswith((".gob", ".goo")):
            episodes.append((info, name.rsplit("/", 1)[-1]))
    require(len(episodes) == 1, "ZIP must contain exactly one GOB or GOO")
    info, basename = episodes[0]
    require(info.compress_type in (zipfile.ZIP_STORED, zipfile.ZIP_DEFLATED)
            and not info.flag_bits & 1, "Episode must use unencrypted stored or deflated ZIP data")
    require(not stat.S_ISLNK(info.external_attr >> 16), "Episode ZIP member is a symbolic link")
    return info, basename


def check_local_header(wrapper, archive, info):
    wrapper.seek(info.header_offset)
    header = wrapper.read(30)
    require(len(header) == 30 and header[:4] == b"PK\x03\x04", "Invalid ZIP local header")
    flags, method = struct.unpack_from("<HH", header, 6)
    require((flags, method) == (info.flag_bits, info.compress_type), "Inconsistent ZIP method or flags")
    recorded = struct.unpack_from("<III", header, 14)
    expected = (info.CRC, info.compress_size, info.file_size)
    descriptor = bool(flags & 8)
    require(recorded == expected or (descriptor and all(v in (0, w) for v, w in zip(recorded, expected))),
            "Inconsistent ZIP local sizes or CRC")
    name_len, extra_len = struct.unpack_from("<HH", header, 26)
    name = wrapper.read(name_len)
    extra = wrapper.read(extra_len)
    require(len(name) == name_len and len(extra) == extra_len, "Truncated ZIP local name or extras")
    check_zip_extra(extra, name)
    if descriptor:
        data_end = wrapper.tell() + info.compress_size
        next_header = min([archive.start_dir] + [x.header_offset for x in archive.infolist()
                                                 if x.header_offset > info.header_offset])
        require(data_end + 12 <= next_header, "Missing ZIP data descriptor")
        wrapper.seek(data_end)
        data = wrapper.read(min(16, next_header - data_end))
        fields = struct.pack("<III", *expected)
        require(data[:12] == fields or data == b"PK\x07\x08" + fields, "Invalid ZIP data descriptor")


def copy_exact(source, destination, size, digest=None):
    remaining = size
    while remaining:
        block = source.read(min(BLOCK, remaining))
        require(block, "Truncated input stream")
        destination.write(block)
        if digest is not None:
            digest.update(block)
        remaining -= len(block)
    require(not source.read(1), "Input exceeds declared size")


def source_identity(stream):
    info = os.fstat(stream.fileno())
    require(stat.S_ISREG(info.st_mode), "Expected a regular input file")
    return info.st_size, info.st_mtime_ns, info.st_ctime_ns


@contextmanager
def open_checked_zip(source, wrapper_size):
    require(wrapper_size <= MAX_ZIP, "ZIP file exceeds size limit")
    # Keep ZIP preflight and the actual reader on the same bounded copy.
    with tempfile.TemporaryFile() as wrapper:
        copy_exact(source, wrapper, wrapper_size)
        count, end = validate_zip_directory(wrapper, wrapper_size)
        if end + 22 < wrapper_size:
            # ZipFile also searches for a signature. End the private copy at the
            # validated record so it opens precisely the directory we admitted.
            wrapper.seek(end + 20)
            wrapper.write(b"\0\0")
            wrapper.truncate()
        with zipfile.ZipFile(wrapper) as archive:
            require(len(archive.infolist()) == count, "ZIP directory member count mismatch")
            yield wrapper, archive


def copy_zip_member(wrapper, archive, info, destination, digest):
    check_local_header(wrapper, archive, info)
    # Admit one extra byte to detect a decoded stream longer than the recorded size.
    checked = copy.copy(info)
    checked.file_size += 1
    with archive.open(checked) as decoded:
        copy_exact(decoded, destination, info.file_size, digest)


def capture_zip(source, destination, wrapper_size, digest):
    with open_checked_zip(source, wrapper_size) as (wrapper, archive):
        info, name = zip_episode(archive, len(archive.infolist()))
        require(16 <= info.file_size <= MAX_ARCHIVE, "Episode archive size outside supported range")
        copy_zip_member(wrapper, archive, info, destination, digest)
        return name, info.file_size


def capture_release(path, destination):
    suffix = path.suffix.lower()
    require(suffix in (".zip", ".gob", ".goo"), "Expected an episode ZIP, GOB or GOO")
    flags = os.O_RDONLY | getattr(os, "O_BINARY", 0) | getattr(os, "O_NONBLOCK", 0)
    with os.fdopen(os.open(path, flags), "rb") as source:
        identity = source_identity(source)
        size = identity[0]
        name = path.name
        digest = hashlib.sha256()
        if suffix == ".zip":
            name, size = capture_zip(source, destination, size, digest)
        else:
            require(16 <= size <= MAX_ARCHIVE, "Episode archive size outside supported range")
            copy_exact(source, destination, size, digest)
        require(source_identity(source) == identity, "Input changed while capturing episode bytes")
    return {"name": name, "size": size, "sha256": digest.hexdigest()}


def inventory(stream):
    entries = archive_index(stream, max_entries=MAX_GOB_MEMBERS)
    result = {}
    previous_end = 0
    for name, (offset, size) in sorted(entries.items(), key=lambda item: item[1][0]):
        require(all(c.isprintable() for c in name), "Invalid GOB entry name")
        if size:
            require(offset >= previous_end, "Overlapping GOB member payloads")
            previous_end = offset + size
        digest = hashlib.sha256()
        stream.seek(offset)
        remaining = size
        while remaining:
            block = stream.read(min(BLOCK, remaining))
            require(block, "Truncated GOB member")
            digest.update(block)
            remaining -= len(block)
        result[name] = {"size": size, "sha256": digest.hexdigest()}
    return result


def inspect_release(path):
    with tempfile.TemporaryFile() as snapshot:
        description = capture_release(path, snapshot)
        return description, inventory(snapshot)


def compare(baseline, candidate):
    left, before = inspect_release(baseline)
    right, after = inspect_release(candidate)
    added = [{"name": name, "candidate": after[name]} for name in sorted(after.keys() - before.keys())]
    removed = [{"name": name, "baseline": before[name]} for name in sorted(before.keys() - after.keys())]
    common = before.keys() & after.keys()
    changed = [{"name": name, "baseline": before[name], "candidate": after[name]}
               for name in sorted(common) if before[name] != after[name]]
    classification = "byteIdentical" if left["sha256"] == right["sha256"] else "repacked"
    if added or removed or changed:
        classification = "payloadDifferences"
    return {"schemaVersion": 1, "classification": classification,
            "inputs": {"baseline": baseline.name, "candidate": candidate.name},
            "baseline": left, "candidate": right,
            "installNameChanged": left["name"].casefold() != right["name"].casefold(),
            "added": added, "removed": removed, "changed": changed, "unchanged": len(common) - len(changed)}


class Parser(argparse.ArgumentParser):
    def error(self, message):
        raise ValueError(message)


def main():
    parser = Parser(description=__doc__)
    parser.add_argument("baseline", type=Path)
    parser.add_argument("candidate", type=Path)
    try:
        args = parser.parse_args()
        result = compare(args.baseline, args.candidate)
    except (OSError, ValueError, RuntimeError, EOFError, zipfile.BadZipFile, zlib.error, struct.error) as error:
        print(json.dumps({"error": str(error)[:512]}))
        return 2
    print(json.dumps(result, sort_keys=True))
    return int(result["classification"] == "payloadDifferences" or result["installNameChanged"])


if __name__ == "__main__":
    sys.exit(main())
