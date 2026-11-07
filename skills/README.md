# Agent skills

Skills in the [Agent Skills format](https://agentskills.io/) (`SKILL.md` plus scripts and
references), for any agent that reads the format.

| Skill | What it does |
|---|---|
| [`smalltv-theme-clock`](smalltv-theme-clock/) | Designs a theme clock from a short interview, writes the source folder, builds an installable `.stheme` and renders a PNG/GIF preview; can install it on a TV over the LAN |

The skill brings its own pure-Python toolkit (`scripts/stheme.py`, Python 3.9+ and Pillow).
It needs no compiler, PlatformIO or network, so it also runs in sandboxed chat
environments. Its packages are byte-identical to `tools/smalltv_theme.py build`, and its
previews match the firmware renderer pixel for pixel; `tests/theme/test_skill_parity.py`
checks both in CI.

## Install

The skill is a plain folder. Either:

- copy `smalltv-theme-clock/` into your agent's skills directory, or
- for apps that take skill uploads, zip it so the archive holds one top-level
  `smalltv-theme-clock/` directory and upload that (code execution must be enabled):

  ```sh
  cd skills && zip -r smalltv-theme-clock.zip smalltv-theme-clock
  ```

Then ask, for example: *"Make me a SmallTV theme clock: analog, dark blue, with the weather
in Amsterdam."*

In a sandbox without access to your network, the skill delivers the `.stheme` for you to
install from **Display → Theme clocks**. Installing straight onto the TV only works where
the agent runs on your network.
