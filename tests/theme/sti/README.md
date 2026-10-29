# Shared STI test files

Image files in the formats agreed in #31, for every reader to test against:
this firmware (`tests/theme/test_package.cpp`) and SmallTV Pomodoro.

There are two source images, each 70 pixels wide, so runs longer than 64
pixels are split:

- `alpha`: 6 colours with alpha. It has transparent rows (two equal ones,
  which share their data), half-transparent pixels, repeats, odd-length
  literal runs, and transparent runs over 64 pixels.
- `opaque`: 15 colours, no alpha. It has a literal run over 64 pixels (in
  two equal rows), a repeat over 64 pixels, and runs of 1, 3 and 5 pixels.

Each source comes in every form the packer writes:

| file | form |
|---|---|
| `<source>-sti1.sti` | STI1, raw pixels |
| `<source>-sti2-direct.sti` | STI2, runs of RGB565 colours |
| `<source>-sti2-indexed.sti` | STI2, runs of palette indices |

`expected.json` gives, for each file, its `width`, `height` and `pixels`:
every pixel in row order as `[RGB565, alpha]`, with a fully transparent
pixel as `[0, 0]`. A reader passes when it decodes every file to exactly
those pixels.

The files are made by `make.py` from the packer (`tools/theme_pack.py`):
`python3 tests/theme/sti/make.py`. A test (`tests/theme/test_pack.py`) checks
that the packer still writes exactly these bytes, so a change to the files is
always a deliberate one.
