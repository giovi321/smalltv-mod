"""The build options for forks are set only where they are meant to be.

platformio.ini: the env that builds with the GitHub update off (so CI builds
that code path), and no other env with an option set. .github/workflows/
build.yml: the build step builds that env and the image check runs after it.
tests/buildflags/check_images.py: sees the download code in an image that has
it and its absence in one that has not (a check that could not see both
would prove nothing).
"""
import re
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CHECK = ROOT / "tests" / "buildflags" / "check_images.py"
MARKERS = (b"server reported no update", b"not enough heap even at boot")


def read(path):
    return (ROOT / path).read_text(encoding="utf-8")


def envs():
    """{env name: its section text} of platformio.ini."""
    text = read("platformio.ini")
    return {m.group(1): m.group(2) for m in re.finditer(r"^\[env:([^\]]+)\]\n(.*?)(?=^\[|\Z)", text, re.S | re.M)}


def flags(section):
    """The -D flags of a section, comments removed."""
    lines = [l.split(";")[0] for l in section.splitlines()]
    return set(re.findall(r"-D\s*([^\s]+)", "\n".join(lines)))


class EnvTest(unittest.TestCase):
    def test_noupdate_env_extends_the_standard_image(self):
        section = envs()["smalltv_noupdate"]
        self.assertRegex(section, r"(?m)^extends\s*=\s*env:smalltv\s*$")
        # The parent's flags are interpolated, so this image cannot drift from it.
        self.assertIn("${env:smalltv.build_flags}", section)
        self.assertEqual(flags(section), {"FW_NO_GITHUB_UPDATE"})

    def test_no_other_env_sets_a_fork_option(self):
        for name, section in envs().items():
            if name == "smalltv_noupdate":
                continue
            got = " ".join(flags(section))
            self.assertNotIn("FW_NO_GITHUB_UPDATE", got, name)
            self.assertNotIn("FW_VERSION_SUFFIX", got, name)

    def test_ci_builds_every_env(self):
        run = re.search(r"(?m)^\s*run: pio run (.*)$", read(".github/workflows/build.yml"))
        self.assertTrue(run, "no pio run line in the build step")
        built = set(re.findall(r"-e\s+(\S+)", run.group(1)))
        self.assertEqual(built, set(envs()))

    def test_ci_checks_the_images_after_the_build(self):
        text = read(".github/workflows/build.yml")
        self.assertIn("python3 tests/buildflags/check_images.py", text)
        self.assertLess(text.index("run: pio run"), text.index("tests/buildflags/check_images.py"))
        self.assertIn("sh tests/buildflags/run.sh", text)

    def test_noupdate_image_is_not_a_release_asset(self):
        # The image is built and size-checked, never published: no staging
        # line, so a release has no file a device could update itself to.
        self.assertNotIn("smalltv_noupdate", read(".github/workflows/build.yml").split("- name: Stage artifacts")[1]
                         .split("- name: Check image headroom")[0])


class CheckImagesTest(unittest.TestCase):
    def build_dir(self, tmp, standard, noupdate):
        for name, content in (("smalltv", standard), ("smalltv_noupdate", noupdate)):
            folder = Path(tmp) / name
            folder.mkdir()
            (folder / "firmware.bin").write_bytes(b"\x00junk\x00" + content + b"\x00more\x00")
        return tmp

    def run_check(self, tmp):
        return subprocess.run([sys.executable, "-I", str(CHECK), tmp], capture_output=True, text=True)

    def test_good_pair_passes(self):
        with tempfile.TemporaryDirectory() as tmp:
            run = self.run_check(self.build_dir(tmp, b"\x00".join(MARKERS), b"nothing of the kind"))
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)

    def test_download_code_in_the_noupdate_image_fails(self):
        for marker in MARKERS:
            with tempfile.TemporaryDirectory() as tmp:
                run = self.run_check(self.build_dir(tmp, b"\x00".join(MARKERS), b"xx" + marker + b"xx"))
            self.assertNotEqual(run.returncode, 0, marker)
            self.assertIn(marker.decode(), run.stdout + run.stderr)

    def test_standard_image_without_the_code_fails(self):
        # The markers moved or were renamed: the check must say so, not pass
        # on an image where it can no longer see anything.
        for missing in MARKERS:
            have = b"\x00".join(m for m in MARKERS if m != missing)
            with tempfile.TemporaryDirectory() as tmp:
                run = self.run_check(self.build_dir(tmp, have, b"nothing of the kind"))
            self.assertNotEqual(run.returncode, 0, missing)
            self.assertIn(missing.decode(), run.stdout + run.stderr)

    def test_missing_image_fails(self):
        with tempfile.TemporaryDirectory() as tmp:
            self.assertNotEqual(self.run_check(tmp).returncode, 0)

    def test_markers_are_in_the_source(self):
        # The markers must be literals of the download code, or the check proves nothing.
        source = read("src/OtaUpdate.cpp")
        for marker in MARKERS:
            self.assertIn(marker.decode(), source)


if __name__ == "__main__":
    unittest.main()
