# Agent skills

Skills in the open [Agent Skills](https://agentskills.io/) format (`SKILL.md` + scripts and
references). They work in Claude (Claude Code, the Claude apps) and OpenAI (Codex, ChatGPT),
and in other agents that read the format.

| Skill | What it does |
|---|---|
| [`smalltv-theme-clock`](smalltv-theme-clock/) | Designs a theme clock from a short interview, writes the source folder, builds an installable `.stheme` and renders a PNG/GIF preview; can install it on a TV over the LAN |

The skill brings its own pure-Python toolkit (`scripts/stheme.py`, Python 3.9+ and Pillow).
It needs no compiler, PlatformIO or network, so it also runs in chat sandboxes. Its
packages are byte-identical to `tools/smalltv_theme.py build`, and its previews match the
firmware renderer pixel for pixel.

## Install

- **Claude Code / Codex** in a clone of this repository: nothing to do. `.claude/skills/` and
  `.agents/skills/` link to this folder, so the skill appears automatically. (On Windows,
  clone with `git config core.symlinks true` or copy the folder into those two places.)
- **Everywhere else** (Claude apps, ChatGPT, or user-level installs): zip the folder so that
  the archive holds one top-level `smalltv-theme-clock/` directory, then upload it in the
  app's skills settings (code execution must be enabled), or copy the folder to
  `~/.claude/skills/` (Claude Code) or `~/.agents/skills/` (Codex):

  ```sh
  cd skills && zip -r smalltv-theme-clock.zip smalltv-theme-clock
  ```

Then ask, for example: *"Make me a SmallTV theme clock: analog, dark blue, with the weather
in Amsterdam."*

In chat sandboxes the skill delivers the `.stheme` for you to install from **Display →
Theme clocks**. Installing straight onto the TV only works where the agent runs on your
network (Claude Code, Codex).
