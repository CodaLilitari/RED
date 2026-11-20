"""Create the single-variable visual and audio controls used with the Wind Gallery."""

import argparse
from pathlib import Path
import struct

from make_project import gob_read, gob_write, material


def create(archive, destination):
    entries = gob_read(archive.read_bytes())
    destination.mkdir(parents=True, exist_ok=False)

    def variant(name, entry, data):
        (destination / name).write_bytes(gob_write(entries | {entry: data}))

    sound = entries["sound/galwind.wav"]
    if sound[:4] != b"RIFF" or sound[36:40] != b"data" or len(sound) != 8044:
        raise ValueError("Expected the fixture's one-second unsigned PCM wind cue")
    variant("silent.gob", "sound/galwind.wav", sound[:44] + bytes([128]) * 8000)
    variant("bright-sky.gob", "mat/sky.mat", material([248] * 16))

    # The original rear boundary is invisible. Give the same wall a visible
    # material in both grate controls so that actual coverage can be measured.
    lines = entries["jkl/gallery.jkl"].decode("ascii").splitlines()
    start = next(i for i, row in enumerate(lines) if row.startswith("WORLD SURFACES "))
    index = start + 1 + 4
    row = lines[index].split()
    if row[0] != "4:" or row[1] != "-1" or row[4] != "0":
        raise ValueError("Expected the original gallery rear boundary at surface 4")
    row[1], row[4] = "0", "4"
    lines[index] = " ".join(row)
    entries["jkl/gallery.jkl"] = ("\n".join(lines) + "\n").encode("ascii")
    (destination / "backing.gob").write_bytes(gob_write(entries))

    grate = bytearray(entries["mat/grate.mat"])
    if struct.unpack_from("<I", grate, 124)[0] != 1:
        raise ValueError("Expected the original color-key MAT")
    struct.pack_into("<I", grate, 124, 0)
    variant("opaque.gob", "mat/grate.mat", grate)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=Path)
    parser.add_argument("destination", type=Path)
    args = parser.parse_args()
    create(args.archive, args.destination)
