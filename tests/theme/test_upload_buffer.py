"""The ESP8266 web server reserves one HTTPUpload object for a file upload
(new HTTPUpload(), with a HTTP_UPLOAD_BUFLEN-byte buffer: 2048 by default)
and keeps it until the request ends -- also while ThemeWeb's finishUpload()
validates the package. A theme install needs that heap then, so the ESP8266
images ask for a 1024-byte buffer. The define must be in build_flags, not
build_src_flags: HTTPUpload is declared in the library's header, and the
library's own files must be built with the same size as ours.

This reads platformio.ini; the firmware build itself checks the value too
(a static_assert in ThemeWeb.cpp)."""
import os
import re
import unittest

ROOT = os.path.join(os.path.dirname(__file__), "..", "..")


def read(path):
    with open(os.path.join(ROOT, path), encoding="utf-8") as f:
        return f.read()


def env_text(ini, name):
    """The lines of one [env:...] section, comments removed."""
    match = re.search(r"^\[env:%s\]\n(.*?)(?=^\[|\Z)" % re.escape(name), ini, re.S | re.M)
    assert match, name
    return "\n".join(l for l in match.group(1).splitlines() if not l.lstrip().startswith(";"))


def flags(section, key):
    """The value of `key = ...` (continued on indented lines) as text."""
    match = re.search(r"^%s\s*=(.*?)(?=^\S|\Z)" % key, section, re.S | re.M)
    return match.group(1) if match else ""


class UploadBuffer(unittest.TestCase):
    def setUp(self):
        self.ini = read("platformio.ini")

    def test_esp8266_images_use_a_1024_byte_buffer(self):
        build_flags = flags(env_text(self.ini, "smalltv"), "build_flags")
        self.assertRegex(build_flags, r"-D\s*HTTP_UPLOAD_BUFLEN=1024\b")

    def test_the_define_reaches_the_library(self):
        # build_src_flags would leave the web server library at 2048 bytes
        # and ours at 1024: the two would disagree on the struct.
        section = env_text(self.ini, "smalltv")
        self.assertNotIn("HTTP_UPLOAD_BUFLEN", flags(section, "build_src_flags"))
        self.assertNotIn("HTTP_UPLOAD_BUFLEN", flags(env_text(self.ini, "smalltv_lean"), "build_src_flags"))

    def test_lean_image_inherits_it(self):
        lean = flags(env_text(self.ini, "smalltv_lean"), "build_flags")
        self.assertIn("${env:smalltv.build_flags}", lean)

    def test_the_build_checks_the_size(self):
        source = read("src/features/theme/ThemeWeb.cpp")
        self.assertRegex(source, r"static_assert\(\s*HTTP_UPLOAD_BUFLEN\s*<=\s*1024")


if __name__ == "__main__":
    unittest.main()
