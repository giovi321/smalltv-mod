"""Tests for tools/patch_gfx_ycbcr.py.

The patch logic runs on fake library folders (no network, no PlatformIO). When
a firmware build has installed the library (.pio/libdeps/smalltv), the last
test also checks that the installed files are the patched ones.
Run: python3 -m unittest discover -s tests/gfx -p 'test_patch_ycbcr.py'
"""
import hashlib
import os
import sys
import tempfile
import unittest

ROOT = os.path.join(os.path.dirname(__file__), "..", "..")
sys.path.insert(0, os.path.join(ROOT, "tools"))
import patch_gfx_ycbcr as p  # noqa: E402

FAKE_HEADER = ("#pragma once\n\n/* comment */\n\n"
               + "".join("static const int16_t %s[] = {1, -2};\n" % t for t in p.TABLES)
               ).encode()
FAKE_SOURCE = ("void f()\n{\n" + p.SOURCE_OLD + "}\n").encode()


def sha(data):
    return hashlib.sha256(data).hexdigest()


class FakeLib(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.lib = self.tmp.name
        os.makedirs(os.path.join(self.lib, "src"))
        self.files = {p.HEADER: FAKE_HEADER, p.SOURCE: FAKE_SOURCE}
        for rel, data in self.files.items():
            self.write(rel, data)
        self.saved = (dict(p.UPSTREAM), dict(p.PATCHED))
        for rel, data in self.files.items():
            p.UPSTREAM[rel] = sha(data)
            p.PATCHED[rel] = sha(p.patched_bytes(rel, data))

    def tearDown(self):
        p.UPSTREAM.clear()
        p.UPSTREAM.update(self.saved[0])
        p.PATCHED.clear()
        p.PATCHED.update(self.saved[1])
        self.tmp.cleanup()

    def path(self, rel):
        return os.path.join(self.lib, *rel.split("/"))

    def write(self, rel, data):
        with open(self.path(rel), "wb") as f:
            f.write(data)

    def read(self, rel):
        with open(self.path(rel), "rb") as f:
            return f.read()

    def test_patches_once_then_leaves_alone(self):
        self.assertEqual(p.apply(self.lib, log=lambda s: None), 2)
        header = self.read(p.HEADER).decode()
        for t in p.TABLES:
            self.assertIn(" %s[] GFX_YCBCR_TABLE = {" % t, header)
        self.assertIn("#define GFX_YCBCR_TABLE PROGMEM", header)
        source = self.read(p.SOURCE).decode()
        self.assertIn(p.SOURCE_NEW, source)
        self.assertNotIn("CLIPRBE[", source)
        self.assertEqual(p.apply(self.lib, log=lambda s: None), 0)

    def test_other_version_stops_and_changes_nothing(self):
        self.write(p.SOURCE, FAKE_SOURCE + b"// a newer library\n")
        with self.assertRaises(p.PatchError) as e:
            p.apply(self.lib, log=lambda s: None)
        self.assertIn("is not the", str(e.exception))
        self.assertEqual(self.read(p.SOURCE), FAKE_SOURCE + b"// a newer library\n")

    def test_unexpected_result_is_not_written(self):
        p.PATCHED[p.HEADER] = "0" * 64
        with self.assertRaises(p.PatchError):
            p.apply(self.lib, log=lambda s: None)
        self.assertEqual(self.read(p.HEADER), FAKE_HEADER)

    def test_missing_file(self):
        os.remove(self.path(p.SOURCE))
        with self.assertRaises(p.PatchError):
            p.apply(self.lib, log=lambda s: None)


class InstalledLib(unittest.TestCase):
    LIB = os.path.join(ROOT, ".pio", "libdeps", "smalltv", p.LIB_NAME)

    @unittest.skipUnless(os.path.isdir(os.path.join(ROOT, ".pio", "libdeps", "smalltv", p.LIB_NAME)),
                         "no firmware build has installed the library")
    def test_installed_files_are_patched(self):
        for rel in (p.HEADER, p.SOURCE):
            with open(os.path.join(self.LIB, *rel.split("/")), "rb") as f:
                self.assertEqual(sha(f.read()), p.PATCHED[rel], rel)


if __name__ == "__main__":
    unittest.main()
