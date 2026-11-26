"""All records are synthetic and authored for RED."""
import struct
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from quake_bsp_adapter import normalize


def fixture(version):
    count = 18 if version == 47 else 17
    data = [b""] * count
    data[0] = b'{ "classname" "worldspawn" }\0'
    data[1] = struct.pack("<64sII", b"textures/test/sky", 4 | 128, 0x20010000)
    data[7] = struct.pack("<6f4i", *([0] * 10))
    data[9] = struct.pack("<ii", 7, 3)
    data[10] = struct.pack("<10f4B", *range(10), 11, 12, 13, 14)
    data[13] = struct.pack("<12i12f2i", *range(26))
    data[15] = bytes(range(8))
    if version == 47:
        data[17] = bytes(128)
    offset = 8 + 8 * count
    entries = bytearray()
    for lump in data:
        entries.extend(struct.pack("<ii", offset, len(lump)))
        offset += len(lump)
    return struct.pack("<4si", b"IBSP", version) + entries + b"".join(data)


def lump(data, index):
    pos, length = struct.unpack_from("<ii", data, 8 + 8 * index)
    return data[pos:pos + length]


class QuakeBspAdapterTests(unittest.TestCase):
    def test_both_dialects_preserve_maps_and_translate_collision(self):
        for version in (46, 47):
            with self.subTest(version=version):
                source = fixture(version)
                result, report = normalize(source)
                self.assertEqual(result[:8], b"RBSP\x01\0\0\0")
                self.assertEqual(report["sourceVersion"], version)
                self.assertEqual(report["advertisementBytesOmitted"], 128 if version == 47 else 0)
                self.assertEqual(lump(result, 0), lump(source, 0))
                self.assertEqual(struct.unpack("<64sII", lump(result, 1))[1:], (0x202000, 0x80000010))
                self.assertEqual(struct.unpack("<3i", lump(result, 9)), (7, 3, -1))
                vertex = lump(result, 10)
                self.assertEqual(vertex[:28], lump(source, 10)[:28])
                self.assertEqual(vertex[52:68], lump(source, 10)[28:])
                surface = lump(result, 13)
                self.assertEqual(len(surface), 148)
                self.assertEqual(surface[:28], lump(source, 13)[:28])
                self.assertEqual(struct.unpack_from("<4i", surface, 36), (7, -3, -3, -3))
                self.assertEqual(surface[84:], lump(source, 13)[40:])
                self.assertEqual(report["lightGridSamplesOmitted"], 1)

    def test_refuse_partial_records_and_overlaps(self):
        source = bytearray(fixture(47))
        struct.pack_into("<i", source, 12 + 8 * 10, 43)
        with self.assertRaisesRegex(ValueError, "partial"):
            normalize(source)
        source = bytearray(fixture(46))
        struct.pack_into("<i", source, 8 + 8, 8 + 17 * 8)
        with self.assertRaisesRegex(ValueError, "overlap"):
            normalize(source)

    def test_refuse_unsupported_dialect(self):
        source = bytearray(fixture(46))
        struct.pack_into("<i", source, 4, 48)
        with self.assertRaisesRegex(ValueError, "46 or 47"):
            normalize(source)
