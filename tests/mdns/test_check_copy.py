"""Tests for tools/check_mdns_copy.py, on fake trees (no network, no PlatformIO).

A fake project has a platformio.ini, a lib/ESP8266mDNS copy with its
UPSTREAM.json, and optionally a fake installed framework package.
Run: python -m unittest discover -s tests/mdns -p 'test_check_copy.py'
"""
import hashlib
import json
import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "tools"))
import check_mdns_copy as chk  # noqa: E402

PLATFORM = "espressif8266 @ 4.2.1"
FRAMEWORK = "3.30102.0"
UPSTREAM = {
    "src/a.cpp": b"// upstream a\r\nint a;\n",
    "src/b.h": b"// upstream b\n",
    "src/patched.cpp": b"// upstream patched\n",
    "library.properties": b"name=X\n",
}
PATCHED_NOW = b"// upstream patched\n// our fix\n"
OURS = {"README-SMALLTV.md": b"readme\n", "src/Stream.h": b"// ours\n"}


def sha(data):
    return hashlib.sha256(data.replace(b"\r\n", b"\n")).hexdigest()


def write(path, data):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(data)


class Tree:
    """Builds a matching fake project; tests then break one thing."""

    def __init__(self, tmp):
        self.root = os.path.join(tmp, "proj")
        self.pkg = os.path.join(tmp, "pkg")
        self.lib = os.path.join(self.root, "lib", "ESP8266mDNS")
        write(os.path.join(self.root, "platformio.ini"),
              ("[env:one]\nplatform = %s\nboard = x\n\n"
               "[env:two]\nplatform = %s\n\n"
               "[env:esp32]\nplatform = https://example.invalid/p.zip\n"
               % (PLATFORM, PLATFORM)).encode())
        for rel, data in UPSTREAM.items():
            write(os.path.join(self.lib, rel), data)
            write(os.path.join(self.pkg, "libraries", "ESP8266mDNS", rel), data)
        write(os.path.join(self.lib, "src/patched.cpp"), PATCHED_NOW)
        for rel, data in OURS.items():
            write(os.path.join(self.lib, rel), data)
        write(os.path.join(self.pkg, "package.json"),
              json.dumps({"name": "framework-arduinoespressif8266",
                          "version": FRAMEWORK}).encode())
        manifest = {
            "core": "3.1.2",
            "platform": PLATFORM,
            "framework_version": FRAMEWORK,
            "files": {k: sha(v) for k, v in UPSTREAM.items()},
            "patched": ["src/patched.cpp"],
            "added": sorted(OURS),
        }
        write(os.path.join(self.lib, "UPSTREAM.json"),
              json.dumps(manifest).encode())

    def run(self, package=True, require=False):
        return chk.check(self.root, self.pkg if package else None, require)

    def edit_manifest(self, fn):
        p = os.path.join(self.lib, "UPSTREAM.json")
        with open(p) as f:
            m = json.load(f)
        fn(m)
        write(p, json.dumps(m).encode())


class CheckTest(unittest.TestCase):
    def setUp(self):
        self._t = tempfile.TemporaryDirectory()
        self.t = Tree(self._t.name)

    def tearDown(self):
        self._t.cleanup()

    def test_matching_copy_passes(self):
        self.assertEqual(self.t.run(), [])

    def test_passes_without_package_when_not_required(self):
        self.assertEqual(self.t.run(package=False), [])

    def test_missing_package_fails_when_required(self):
        probs = self.t.run(package=False, require=True)
        self.assertTrue(any("not installed" in p for p in probs), probs)

    def test_platform_bump_fails(self):
        p = os.path.join(self.t.root, "platformio.ini")
        with open(p) as f:
            s = f.read()
        write(p, s.replace("4.2.1", "4.3.0", 1).encode())
        probs = self.t.run()
        self.assertTrue(any("4.3.0" in p for p in probs), probs)

    def test_second_env_bump_fails(self):
        p = os.path.join(self.t.root, "platformio.ini")
        with open(p) as f:
            s = f.read()
        i = s.rindex("4.2.1")
        write(p, (s[:i] + "4.3.0" + s[i + 5:]).encode())
        self.assertTrue(self.t.run())

    def test_no_pinned_platform_fails(self):
        write(os.path.join(self.t.root, "platformio.ini"),
              b"[env:one]\nplatform = espressif8266\n")
        probs = self.t.run()
        self.assertTrue(any("not pinned" in p or "no espressif8266" in p
                            for p in probs), probs)

    def test_no_esp8266_env_fails(self):
        write(os.path.join(self.t.root, "platformio.ini"), b"[env:one]\n")
        self.assertTrue(self.t.run())

    def test_installed_framework_version_differs_fails(self):
        write(os.path.join(self.t.pkg, "package.json"),
              json.dumps({"version": "3.30200.0"}).encode())
        probs = self.t.run()
        self.assertTrue(any("3.30200.0" in p for p in probs), probs)

    def test_installed_file_changed_fails(self):
        write(os.path.join(self.t.pkg, "libraries/ESP8266mDNS/src/b.h"),
              b"// upstream b, changed\n")
        probs = self.t.run()
        self.assertTrue(any("src/b.h" in p for p in probs), probs)

    def test_installed_patched_file_changed_fails(self):
        write(os.path.join(self.t.pkg,
                           "libraries/ESP8266mDNS/src/patched.cpp"), b"new\n")
        probs = self.t.run()
        self.assertTrue(any("patched.cpp" in p for p in probs), probs)

    def test_installed_file_missing_fails(self):
        os.remove(os.path.join(self.t.pkg, "libraries/ESP8266mDNS/src/a.cpp"))
        probs = self.t.run()
        self.assertTrue(any("src/a.cpp" in p for p in probs), probs)

    def test_copy_untouched_file_edited_fails(self):
        write(os.path.join(self.t.lib, "src/b.h"), b"// someone edited\n")
        probs = self.t.run()
        self.assertTrue(any("src/b.h" in p for p in probs), probs)

    def test_copy_file_missing_fails(self):
        os.remove(os.path.join(self.t.lib, "library.properties"))
        probs = self.t.run()
        self.assertTrue(any("library.properties" in p for p in probs), probs)

    def test_unlisted_extra_file_in_copy_fails(self):
        write(os.path.join(self.t.lib, "src/extra.cpp"), b"x\n")
        probs = self.t.run()
        self.assertTrue(any("extra.cpp" in p for p in probs), probs)

    def test_patched_file_may_differ_in_copy(self):
        # The patched file is the one place the copy is allowed to differ.
        write(os.path.join(self.t.lib, "src/patched.cpp"), b"another fix\n")
        self.assertEqual(self.t.run(), [])

    def test_patched_file_in_copy_missing_fails(self):
        os.remove(os.path.join(self.t.lib, "src/patched.cpp"))
        self.assertTrue(self.t.run())

    def test_added_file_missing_fails(self):
        os.remove(os.path.join(self.t.lib, "src/Stream.h"))
        probs = self.t.run()
        self.assertTrue(any("Stream.h" in p for p in probs), probs)

    def test_manifest_missing_fails(self):
        os.remove(os.path.join(self.t.lib, "UPSTREAM.json"))
        self.assertTrue(self.t.run())

    def test_manifest_platform_stale_fails(self):
        self.t.edit_manifest(lambda m: m.update(platform="espressif8266 @ 4.1.0"))
        self.assertTrue(self.t.run())

    def test_crlf_in_checkout_is_not_a_change(self):
        # A Windows checkout may turn LF into CRLF; the content is the same.
        write(os.path.join(self.t.lib, "src/b.h"), b"// upstream b\r\n")
        self.assertEqual(self.t.run(), [])

    def test_main_exit_codes(self):
        argv = ["--root", self.t.root, "--package", self.t.pkg]
        self.assertEqual(chk.main(argv), 0)
        write(os.path.join(self.t.lib, "src/b.h"), b"edited\n")
        self.assertEqual(chk.main(argv), 1)


if __name__ == "__main__":
    unittest.main()
