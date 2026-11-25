import struct
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from verify_episode_pair import material_layout, verify

from test_archive_gameplay import gob_bytes


def texture(bits, keyed=False, *, mip_counts=(1,), selectors=(0,), side=2):
    header = b"MAT " + struct.pack("<6I", 50, 2, len(selectors), len(mip_counts), int(bits == 16), bits)
    cels = b"".join(struct.pack("<10I", 8, 0, 0, 0, 0, 0, 0, 0, 0, index) for index in selectors)
    textures = bytearray()
    for count in mip_counts:
        textures.extend(struct.pack("<6I", side, side, int(keyed), 0, 0, count))
        textures.extend(bytes(sum((side >> m) ** 2 for m in range(count)) * bits // 8))
    return header + bytes(48) + cels + textures


def edition(bits, **changes):
    files = {"episode.jk": b"LEVEL", "jkl\\arena.jkl":
             b"SECTION: MATERIALS\nWorld materials 3\n0: stone.mat 1 1\nend\n",
             "cog\\door.cog": b"code", "mat\\stone.mat": texture(bits),
             "mat\\leaf.mat": texture(8, True)}
    files.update(changes)
    return files


def package(path, files, name="arena.gob", index_at_end=False):
    data = gob_bytes(files.items())
    if index_at_end:
        start = 16 + 136 * len(files)
        table = bytearray(data[12:start])
        for i in range(len(files)):
            offset = 4 + 136 * i
            old, = struct.unpack_from("<I", table, offset)
            struct.pack_into("<I", table, offset, old - start + 12)
        data = data[:8] + struct.pack("<I", 12 + len(data) - start) + data[start:] + table
    if path.suffix == ".zip":
        with zipfile.ZipFile(path, "w") as z:
            z.writestr("Edition notes.txt", "Install the episode.")
            z.writestr("Episode/" + name, data)
    else:
        path.write_bytes(data)


class EpisodePairTests(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        self.indexed = Path(temp.name) / "indexed.zip"
        self.hardware = Path(temp.name) / "arena.gob"
        package(self.indexed, edition(8), index_at_end=True)

    def test_zip_and_bare_archives_in_both_native_directory_layouts(self):
        package(self.hardware, edition(16))
        result = verify(self.indexed, self.hardware, 2)
        self.assertEqual(result["convertedMaterials"], 1)
        self.assertEqual(result["sharedIndexedMaterials"], 1)
        self.assertEqual(result["levels"]["jkl/arena.jkl"]["free"], 2)
        with self.assertRaisesRegex(ValueError, "2 free material slots"):
            verify(self.indexed, self.hardware, 3)

    def test_rejects_gameplay_drift_and_mismatched_install_names(self):
        package(self.hardware, edition(16, **{"cog\\door.cog": b"changed"}))
        with self.assertRaisesRegex(ValueError, "door.cog"):
            verify(self.indexed, self.hardware)
        other = self.hardware.with_name("other.gob")
        self.hardware.rename(other)
        with self.assertRaisesRegex(ValueError, "Installed filenames"):
            verify(self.indexed, other)

    def test_passing_the_indexed_edition_twice_is_not_a_hardware_pair(self):
        with self.assertRaisesRegex(ValueError, "no 16-bit MAT"):
            verify(self.indexed, self.indexed)

    def test_rejects_changed_keyed_assets_and_truncated_mips(self):
        for material, message in [(texture(8, True)[:-1] + b"x", "indexed MAT differs"),
                                  (texture(16, True), "share indexed"),
                                  (texture(16)[:-1], "Truncated MAT")]:
            with self.subTest(message=message), self.assertRaisesRegex(ValueError, message):
                package(self.hardware, edition(16, **{"mat\\leaf.mat": material}))
                verify(self.indexed, self.hardware)

    def test_rejects_case_aliases_and_unmatched_resources(self):
        for key, message in [("MAT\\STONE.MAT", "Duplicate"), ("mat\\extra.mat", "inventories")]:
            with self.subTest(key=key), self.assertRaisesRegex(ValueError, message):
                package(self.hardware, edition(16, **{key: texture(16)}))
                verify(self.indexed, self.hardware)

    def test_compares_each_animated_cels_stored_mip_count(self):
        def animated(bits, selectors):
            return texture(bits, mip_counts=(1, 4), selectors=selectors, side=16)
        package(self.indexed, edition(8, **{"mat\\stone.mat": animated(8, (0, 1, 0))}))
        package(self.hardware, edition(16, **{"mat\\stone.mat": animated(16, (0, 1, 0))}))
        self.assertEqual(verify(self.indexed, self.hardware)["convertedMaterials"], 1)
        package(self.hardware, edition(16, **{"mat\\stone.mat": animated(16, (1, 0, 0))}))
        with self.assertRaisesRegex(ValueError, "mat/stone.mat: .*mip"):
            verify(self.indexed, self.hardware)

    def test_equivalent_animated_cels_may_use_different_texture_table_order(self):
        left = texture(8, mip_counts=(4, 2), selectors=(0, 1), side=16)
        right = texture(16, mip_counts=(2, 4), selectors=(1, 0), side=16)
        package(self.indexed, edition(8, **{"mat\\stone.mat": left}))
        package(self.hardware, edition(16, **{"mat\\stone.mat": right}))
        self.assertEqual(verify(self.indexed, self.hardware)["convertedMaterials"], 1)

    def test_shared_indexed_color_cels_have_no_texture_selector(self):
        header = b"MAT " + struct.pack("<6I", 50, 0, 2, 0, 0, 8) + bytes(48)
        solid = header + struct.pack("<6I", 0, 3, 0, 0, 0, 0) + struct.pack("<6I", 0, 7, 0, 0, 0, 0)
        package(self.indexed, edition(8, **{"mat\\flat.mat": solid}))
        package(self.hardware, edition(16, **{"mat\\flat.mat": solid}))
        self.assertEqual(verify(self.indexed, self.hardware)["sharedIndexedMaterials"], 2)

    def test_checks_each_declared_texture_against_retail_mip_limit(self):
        for bits in (8, 16):
            # The second, unreferenced texture has only a header. No pixel data
            # is needed to reject its declared count.
            material = bytearray(texture(bits, side=16))
            struct.pack_into("<I", material, 16, 2)
            material.extend(struct.pack("<6I", 16, 16, 0, 0, 0, 5))
            with self.subTest(bits=bits), self.assertRaisesRegex(ValueError, "retail.*4"):
                material_layout(material)

    def test_rejects_unresolved_cel_texture_selector(self):
        with self.assertRaisesRegex(ValueError, "selector"):
            material_layout(texture(8, selectors=(1,)))
