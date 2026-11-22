"""Moves Arduino_GFX's YCbCr lookup tables from DRAM to flash on the ESP8266.

GFX Library for Arduino 1.6.9 declares eleven YCbCr-to-RGB tables in
src/YCbCr2RGB.h as plain `static const` arrays. On the ESP8266 a const array
without PROGMEM is placed in .rodata, which the core copies into DRAM, and
`Arduino_DataBus::writeYCbCrPixels` (src/Arduino_DataBus.cpp) keeps them: it is
virtual, so the vtable references it even though smalltv-mod never calls it.
That is 6,804 B of DRAM, which on this chip is heap.

PROGMEM alone would break the function: it reads the tables with 16-bit loads,
and flash on the ESP8266 can only be read 32 bits at a time. So the patch does
two things, both only when ESP8266 is defined:

  src/YCbCr2RGB.h        the tables get PROGMEM, and a read macro that uses
                         pgm_read_word on the ESP8266 and a plain index
                         elsewhere;
  src/Arduino_DataBus.cpp writeYCbCrPixels reads the tables through that macro.

The function still works (more slowly, which does not matter for a function the
firmware does not call). The ESP32 builds do not run this script, and the
patched header leaves them unchanged anyway.

How it runs: as a pre: extra script of the ESP8266 envs (platformio.ini).
PlatformIO installs lib_deps before it runs pre scripts, so the library is in
.pio/libdeps/<env>/ by then. Each file is in one of three states:

  - the pinned 1.6.9 original (UPSTREAM hash): it is patched, and the result
    must match the PATCHED hash;
  - already patched (PATCHED hash): nothing to do;
  - anything else: the build stops. A different library version, or a hand
    edit, needs a look at the new file and new hashes here.

tools/ci_checks.py ycbcr checks the built images: every YCbCr table symbol in
an ESP8266 ELF has to be in flash, not DRAM.

Standalone use (the host test does this):
    python3 tools/patch_gfx_ycbcr.py <path to the GFX library folder>
"""
import hashlib
import os
import sys

LIB_NAME = "GFX Library for Arduino"
VERSION = "1.6.9"

HEADER = "src/YCbCr2RGB.h"
SOURCE = "src/Arduino_DataBus.cpp"

# sha256 of each file as GFX Library for Arduino 1.6.9 ships it, and after the
# patch below. The patched hashes follow from the originals and the patch, but
# they are written out rather than computed: a change to the patch text then
# needs new hashes here, so the result stays a reviewed one.
UPSTREAM = {
    HEADER: "5b75c28bf83a8eef05a85919c08175696e9920100f4b9736a759d6b6014458b7",
    SOURCE: "342d946717361402f7cf2d1af437ee796deadbcbe7bcfe84a29debff3d9d0f65",
}
PATCHED = {
    HEADER: "b3a15bcb4acf560f61ac6365b211bf80fb5d8c6238e07cefb3dd0ad1cde20f98",
    SOURCE: "d4de87dd5d7e29d5b84476bc565b71c917c3e58c7dd77041681aac1a5394833b",
}

TABLES = ("Y2I16", "CR2R16", "CR2G16", "CB2G16", "CB2B16",
          "CLIPR", "CLIPG", "CLIPB", "CLIPRBE", "CLIPGBE", "CLIPBBE")

HEADER_ANCHOR = "\nstatic const int16_t Y2I16[]"
HEADER_PRELUDE = """
// smalltv-mod (tools/patch_gfx_ycbcr.py): on the ESP8266 these tables are kept
// in flash instead of DRAM, and read with pgm_read_word (flash allows only
// 32-bit loads). _S16 reads the int16_t tables, _U16 the uint16_t ones.
// Elsewhere nothing changes.
#if defined(ESP8266)
#include <pgmspace.h>
#define GFX_YCBCR_TABLE PROGMEM
#define GFX_YCBCR_READ_S16(t, i) ((int16_t)pgm_read_word(&(t)[i]))
#define GFX_YCBCR_READ_U16(t, i) ((uint16_t)pgm_read_word(&(t)[i]))
#else
#define GFX_YCBCR_TABLE
#define GFX_YCBCR_READ_S16(t, i) ((t)[i])
#define GFX_YCBCR_READ_U16(t, i) ((t)[i])
#endif
"""

SOURCE_OLD = """      pxR = CR2R16[pxCr];
      pxG = -CB2G16[pxCb] - CR2G16[pxCr];
      pxB = CB2B16[pxCb];

      pxY = Y2I16[*yData++];
      _data16.value = CLIPRBE[pxY + pxR] | CLIPGBE[pxY + pxG] | CLIPBBE[pxY + pxB];
      write(_data16.lsb);
      write(_data16.msb);
      pxY = Y2I16[*yData++];
      _data16.value = CLIPRBE[pxY + pxR] | CLIPGBE[pxY + pxG] | CLIPBBE[pxY + pxB];
"""
SOURCE_NEW = """      pxR = GFX_YCBCR_READ_S16(CR2R16, pxCr);
      pxG = -GFX_YCBCR_READ_S16(CB2G16, pxCb) - GFX_YCBCR_READ_S16(CR2G16, pxCr);
      pxB = GFX_YCBCR_READ_S16(CB2B16, pxCb);

      pxY = GFX_YCBCR_READ_S16(Y2I16, *yData++);
      _data16.value = GFX_YCBCR_READ_U16(CLIPRBE, pxY + pxR) | GFX_YCBCR_READ_U16(CLIPGBE, pxY + pxG) | GFX_YCBCR_READ_U16(CLIPBBE, pxY + pxB);
      write(_data16.lsb);
      write(_data16.msb);
      pxY = GFX_YCBCR_READ_S16(Y2I16, *yData++);
      _data16.value = GFX_YCBCR_READ_U16(CLIPRBE, pxY + pxR) | GFX_YCBCR_READ_U16(CLIPGBE, pxY + pxG) | GFX_YCBCR_READ_U16(CLIPBBE, pxY + pxB);
"""


class PatchError(Exception):
    pass


def _sha(data):
    return hashlib.sha256(data).hexdigest()


def _patch_header(text):
    if text.count(HEADER_ANCHOR) != 1:
        raise PatchError("%s: the table block was not found" % HEADER)
    text = text.replace(HEADER_ANCHOR, HEADER_PRELUDE + HEADER_ANCHOR, 1)
    for name in TABLES:
        old = " %s[] = {" % name
        if text.count(old) != 1:
            raise PatchError("%s: table %s was not found once" % (HEADER, name))
        text = text.replace(old, " %s[] GFX_YCBCR_TABLE = {" % name, 1)
    return text


def _patch_source(text):
    if text.count(SOURCE_OLD) != 1:
        raise PatchError("%s: writeYCbCrPixels was not found" % SOURCE)
    return text.replace(SOURCE_OLD, SOURCE_NEW, 1)


PATCHERS = {HEADER: _patch_header, SOURCE: _patch_source}


def patched_bytes(rel, original):
    """The patched file for the pinned original (bytes in, bytes out)."""
    return PATCHERS[rel](original.decode("utf-8")).encode("utf-8")


def apply(lib_dir, log=print):
    """Patches lib_dir in place. Returns the number of files changed."""
    changed = 0
    for rel in (HEADER, SOURCE):
        path = os.path.join(lib_dir, *rel.split("/"))
        if not os.path.isfile(path):
            raise PatchError("%s is missing" % path)
        with open(path, "rb") as f:
            data = f.read()
        digest = _sha(data)
        if digest == PATCHED[rel]:
            continue
        if digest != UPSTREAM[rel]:
            raise PatchError(
                "%s is not the %s %s file this patch was made for (sha256 %s). "
                "Look at the new file, then update tools/patch_gfx_ycbcr.py "
                "(the patch and both hashes)." % (rel, LIB_NAME, VERSION, digest))
        new = patched_bytes(rel, data)
        if _sha(new) != PATCHED[rel]:
            raise PatchError("%s: the patch gave an unexpected result" % rel)
        with open(path, "wb") as f:
            f.write(new)
        log("patch_gfx_ycbcr: %s patched (YCbCr tables in flash)" % rel)
        changed += 1
    return changed


def _run_in_platformio():
    Import("env")  # noqa: F821  (provided by SCons)
    lib_dir = os.path.join(env.subst("$PROJECT_LIBDEPS_DIR"),  # noqa: F821
                           env.subst("$PIOENV"), LIB_NAME)  # noqa: F821
    try:
        apply(lib_dir)
    except PatchError as e:
        sys.stderr.write("Error: patch_gfx_ycbcr: %s\n" % e)
        env.Exit(1)  # noqa: F821


try:
    Import  # noqa: F821
except NameError:
    if __name__ == "__main__":
        if len(sys.argv) != 2:
            sys.exit(__doc__)
        try:
            apply(sys.argv[1])
        except PatchError as e:
            sys.exit("Error: %s" % e)
else:
    _run_in_platformio()
