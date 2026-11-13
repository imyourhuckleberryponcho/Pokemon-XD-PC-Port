# Pokémon XD: Gale of Darkness — PC Port

[![Discord: join](https://img.shields.io/badge/Discord-Join%20the%20server-5865F2?logo=discord&logoColor=white)](https://discord.gg/XVbDVXH4Pn)

A native PC port of **Pokémon XD: Gale of Darkness** (GameCube, USA, `GXXE01`), built from the
[Pokémon XD decompilation](https://github.com/imyourhuckleberryponcho/Pokemon-XD-Decomp). Not an emulator: the game's own code, rebuilt for 64-bit Windows,
with Linux and Mac builds for testers.

The goal is a "definitive edition": with the default settings the game plays exactly like the GameCube release
(same timing, same random numbers, same quirks), and every enhancement is an option you turn on yourself.

This repository contains **no game assets, ROMs, ISOs or executables**, and none are compiled into the program.
You need your own legally obtained copy of the game (USA, `GXXE01`); the port reads everything from your disc
image while it runs.

## Status: public beta — version 0.4.0

**Playable from New Game into the story, with known bugs.** It is not finished: expect crashes, stalls and
graphics differences, and keep backups of saves you care about. Downloads are on the [Releases](../../releases)
page, one per system:

| System | State |
|---|---|
| **Windows** 10 / 11 (64-bit) | The main build: what is developed and tested every day. |
| **Linux** (x86_64) | Test build. Checked against the Windows build in automatic runs (same random numbers, same saves, same sound), but so far only with software graphics: it has **not been played on a real Linux PC or a Steam Deck** yet. |
| **Mac** | Test build. Two players have run it on Apple M2 Macs (macOS 15 and 27, through Rosetta 2): picture, sound, controllers, settings and saving work. 0.3.5 stopped after a few minutes of play; 0.3.6 has the fix, which has not been played on a Mac yet. It is an Intel (x86_64) program: Apple-silicon Macs run it through Rosetta 2. New in 0.4.0: the app opens the launcher (with Battle Revolution models); this is the first Mac build with it and it has not been played on a Mac yet. More testers wanted. |

**Found a bug, or want to help test?** Join the [Discord](https://discord.gg/XVbDVXH4Pn) — see
*Reporting a bug* below.

New in 0.4.0:

- **MODS on the title screen:** switch mods on or off and set their order inside the game.
- **Battle Revolution models** (Windows and Linux; Mac: new, untested): own Pokémon Battle Revolution (Wii, USA)? The launcher's new
  **Extra assets** page turns your disc's Pokémon into models this game can show, then a setting switches them on.
  Built in, made on your PC from your own disc; nothing from either game is included.
- **Randomizer:** shops, move tutor moves, Poké Spot options, TM descriptions that fit.
- **Physical/special split** (off unless you turn it on), as from Diamond and Pearl on.
- **Fast-forward sound at its own pitch**, and fast speeds slow down instead of stuttering on PCs that can't keep up.
- **Controllers:** Nintendo controllers by their printed letters; a second controller takes over from a built-in
  one (Steam Deck).
- **Linux launcher**, and on the Mac the app now opens the launcher (new, untested on a Mac).
- **Fixed:** streaks across the screen in some move effects, Flamethrower going straight up, the move tutor, the
  SETTINGS button over the VS MODE menu.

New in 0.3.6 (fixes after the first days in public):

- **Mac:** the game stopped after a few minutes of play; its working memory is now kept for it from the start.
  Quitting no longer ends in a crash message.
- **Shrunk disc images** (`.nkit.iso`) no longer get the "content is different from a known-good copy" warning:
  the game says that such a copy cannot be compared, and starts.
- The log no longer says "STUCK" while a message window is waiting for you.

New in 0.3.5:

- **Several saves and a save manager** in the launcher: keep more than one playthrough, choose which one the game
  loads, look at what is on each, restore automatic backups.
- **Linux and Mac builds** (see the table above).
- **Slow PCs:** the sound holds up better when pictures take long, fast-forward switches itself off when the PC
  cannot keep up, and the log says how hard the PC is working (helps us with reports from weak laptops).
- **Sound:** the GameCube sound chip's built-in routines were checked against a real console's and are equal.
- **Timing:** more of the GameCube's own timing is modelled (how long the console takes to load and set up a
  scene). It is not switched on yet: it stays off until it is proven against Dolphin.

From 0.3.0:

- **New Game setup screen** (not in the Original preset): choose how a playthrough is played — Faithful or Enhanced,
  a randomizer, challenge rules. The choice is kept with the save; Continue asks when the save and your settings
  disagree.
- **Randomizer:** Light, Classic, Chaos or Custom, every category its own switch (wild Pokémon, trainers, Shadow
  Pokémon, items, TM moves, trainers' levels, held items and natures). Made from your own disc when the game
  starts; each randomized game has a code, and the same code makes the same game.
- **Challenge runs:** no items in battle; Nuzlocke (a fainted Pokémon is lost to the last PC box, one catch per
  area); level caps (no experience past the next boss's highest level).
- **Frame rate 60** (a setting, 30 by default): in-between pictures for smoother 3D motion. The game's logic still
  runs exactly as at 30; this was checked over every test recording (same random numbers, same saves).

What works today:

- Boot, title screen, New Game, the opening cutscene, the tutorial battle and Krane's lab, with music and sound
- Walking around, doors, talking to people, story cutscenes, trainer battles
- Saving and loading on the memory card (see *Saves* below), including continuing from an existing save; saves
  written by the port load in Dolphin and on a GameCube, and the other way round
- VS mode (Quick Battle)
- 268 of the game's 283 maps load and run when entered directly; the rest are developer test maps and menus
- Story checkpoints across the whole game (one per developer story point) load and run from a save, and every
  battle in the game is being run automatically to find what still breaks
- Loading times follow the GameCube's disc drive
- A settings screen inside the game (in the game's own menu style), fast-forward, text speed, separate volumes for
  music, effects and cries, PC button pictures, photo mode, automatic save backups, bug reports with F10

Known differences and open problems (being worked on):

- Timing is a few video frames off the GameCube in some loads and scene starts (the real console spends extra
  frames on heavy work such as building a scene). Not visible in normal play, but it matters for speedruns and for
  random numbers; modelling it exactly is in progress and will only be switched on once it is proven against Dolphin.
- Some later story scenes are still untested in real play; a crash or stall there is likely.
- Graphics are close to the original but not yet identical in every scene.

### How it is checked

Every change is tested against recordings of real play that are replayed in the port and checked frame by frame;
battles and graphics are also compared value by value against the Dolphin emulator. Game code is fixed in the
decompilation, so the port and the decompilation improve together.

## Playing

1. Build it (below), or use a build someone gives you. A finished build is one folder, `Pokemon XD PC`:

   ```
   Pokemon XD PC\
     Pokemon XD.exe    <- start here
     Read me.txt
     game\             the launcher, the game and their libraries (keep them together)
   ```

2. Double-click `Pokemon XD.exe`. The launcher opens: choose the disc, a preset, settings, controls and mods, then
   press Play. Its Disc page can also make a Start menu or desktop shortcut. (`game\xd.exe` can be run directly
   too.) The first time, a window asks
   for your disc image (Pokemon XD, USA: `.iso`, `.gcm`, `.rvz`, `.wia`, `.gcz` or `.ciso`); after it has been
   checked, later starts use it again without asking. You can also drag the disc image onto `xd.exe`, or run
   `xd.exe "<path to your disc image>"`.

All of your files (settings, saves, bug reports, recordings, mods) are in one folder, `%APPDATA%\Pokemon XD`.
**Portable copy:** put a file `data_location.json` containing `{"version": 1, "mode": "portable"}` next to
`xd.exe`, and everything is kept in a `portable` folder next to it instead (for a USB stick or a self-contained
copy). Only the graphics card's shader cache stays in `%APPDATA%\Pokemon XD`.

**Controls:** a GameCube-style controller (any controller SDL supports) or the keyboard. Default keys:
arrows = main stick, `X` = A, `Z` = B, `C` = X, `S` = Y, `D` = Z, `Q`/`W` = L/R, `Enter` = Start. Keys and controller
buttons can be changed on the launcher's Controls page.

### Reporting a bug

1. **Press F10 when something goes wrong.** That saves a bug report in `%APPDATA%\Pokemon XD\reports` with a
   screenshot, the controller input that led there (it can be replayed, which is how bugs get found), the log and
   your settings. If the game crashes or gets stuck, a report is written automatically. F11 adds a marker to the
   input recording ("the bug is at my second marker"). F12 takes a screenshot.
2. **Open the launcher's Reports page and press Export.** It makes one zip of the report; your Windows user name
   is hidden in it. Nothing is ever sent by the program itself: you decide what leaves your PC.
3. **Post the zip in the [Discord](https://discord.gg/XVbDVXH4Pn)** with a sentence on what you saw and what you
   expected. (No Discord? Open an issue on this repository and attach the zip.)

Please do not post screenshots, videos or files of the game itself in issues on this repository.

### Mac: what to expect

The Mac build is a test build. Two players ran 0.3.5 on Apple M2 Macs and it played (picture, sound, controllers,
settings, saving) until it stopped after a few minutes; 0.3.6 fixes that stop, and the fix has been built and
started on a rented Mac machine but not yet played. If you have a Mac and your own disc image: thank you, and
please tell us how far you get.

- The app is not signed by Apple. macOS will refuse to open it at first: right-click the app and choose Open, or
  allow it under System Settings > Privacy & Security. The `Read me (Mac).txt` in the download has the steps.
- It needs macOS 13.3 or newer. Apple-silicon Macs need Rosetta 2 (macOS offers to install it).
- The game keeps some memory addresses in the GameCube's small format. On the Mac the program now hands out its own
  memory from low addresses for that, and the automatic runs show it working, but only a real playthrough can prove
  it everywhere. If the game ever stops with a message about an "address above 4 GB", please report it.
- There is no launcher on the Mac yet. If A and B (confirm and cancel) seem the wrong way round on your
  controller, the line `gamepad1.layout = label` (or `= position`) in `settings.ini` in
  `~/Library/Application Support/Pokemon XD` changes whether the buttons follow their printed letters or their
  place on the controller. Please tell us which controller it was.
- The download contains a script that writes a first-run report (no game data in it). Sending that file back,
  whatever happens, is the single most useful thing a Mac tester can do.

### Saves

Saves stay in the GameCube's own format, one `.gci` file per save in `%APPDATA%\Pokemon XD\USA\Card A` —
the same layout as Dolphin's "GCI folder" memory cards, so saves can be copied between Dolphin and the port.
Included tools: `xd_save_dump` (show what is in a save), `xd_save_convert` (GameCube/PC byte order) and
`xd_gba_trade` (trade with Game Boy Advance Pokémon saves the way the GameCube link cable does).

### Icon and launcher background

The program ships with its own original icon. After your disc has been checked, the launcher can use art from
**your own disc** instead: the game's logo (the default), its banner, Pokémon and trainer pictures, or 3D renders of
Shadow Lugia, Eevee and the hero that the game draws once in the background (about a minute, the first time). The
launcher background works the same way: a rendered scene, the map of Orre or the menus' pattern. All of it is made
on your PC from your disc and kept in `%APPDATA%\Pokemon XD\icons` and `backgrounds`; none of it is part of this
repository or of a build.

### Settings and the launcher

The easiest way is the launcher (`xd_launcher.exe`): pick the disc, choose a preset (**Original**, the GameCube game
exactly; **Enhanced**, the recommended improvements; or **Custom**), change single settings (anything that differs
from the GameCube is marked), set up the controls, turn mods on or off and see what is on your save. Everything is
stored in `%APPDATA%\Pokemon XD\settings.ini`; `xd_settings list` shows every setting with its meaning.
The **Original** preset is the default and behaves like the GameCube; enhancements are off unless you turn them
on. Available now: **higher internal resolution**, anti-aliasing and texture filtering; **widescreen** (16:9,
16:10, 21:9 or the window's shape: a wider 3D view, with menus and battle boxes at the screen's sides); **frame
rate 60**; **frame pacing** for monitors that aren't 60 Hz; full screen; fast-forward, text speed and fast battle
animations; separate volumes; photo mode. Options that are listed but say "not yet" come later. The same settings
can be changed inside the game: SETTINGS below EXIT in the field menu, or left of OPTIONS on the title screen
(the game is paused completely while that page is open). Mods: see `xd_data_mods` and `xd_mods_check` (game data and file replacement mods, loaded from
`%APPDATA%\Pokemon XD\mods`).

## Building

### Requirements

- Windows 10 or 11, 64-bit
- Visual Studio 2022 with the "Desktop development with C++" workload (its bundled CMake and Ninja are used)
- Python 3.9+ with libclang (`python -m pip install libclang`; the build reads the game's data tables' layout
  with it, so that no game data is compiled into the program)
- Git

### Steps

1. Clone this repository with its submodules, and the decompilation next to it:

   ```
   git clone --recursive <this repository> Pokemon-XD-Port
   git clone --recursive https://github.com/imyourhuckleberryponcho/Pokemon-XD-Decomp Pokemon-XD-Decomp
   ```

2. Take a snapshot of the decompilation's source for the port to build from:

   ```
   cd Pokemon-XD-Port
   python tools/sync_decomp.py
   ```

   (If the decompilation is somewhere else, add `--decomp <folder>`.)

3. Build:

   ```
   build.bat release
   ```

   (This builds the game, the launcher and the save tools.)

4. Make the player's folder:

   ```
   python tools/package_release.py
   ```

   This writes `dist\Pokemon XD PC\` (see *Playing*): `Pokemon XD.exe` at the top, everything else in `game\`,
   with no test tools or debug files. Or run the build directly:

   ```
   build\release\bin\xd_launcher.exe
   build\release\bin\xd.exe "<path to your Pokemon XD (USA) .iso>"
   ```

## Repository layout

| Path | Contents |
|---|---|
| `platform/` | Replacements for the GameCube hardware: graphics, audio (the real DSP code is run), input, files, memory card, threads and timing |
| `engine/` | Code shared by the game and the tools: settings, controls, save formats, mods, GBA trading, task switching |
| `games/xd/` | Pokémon XD specific glue: build list, data conversion, start-up, bug reports, save and trade tools |
| `launcher/` | The launcher: disc, presets, settings, controls, mods, save info |
| `third_party/` | [Aurora](https://github.com/encounter/aurora) (GameCube graphics on modern GPUs) with the port's patches, [Borealis](https://github.com/encounter/borealis) (data folder, file window), SDL |
| `tools/` | Snapshot, build-list and release-folder scripts |

## Roadmap

1. **It boots on PC** — done.
2. **Plays all the way through** — in progress: the full story, then a whole playthrough that matches the GameCube
   frame by frame. Version 1.0 when that holds.
3. Definitive basics — done: widescreen, higher resolution, frame rate 60, frame pacing, controller options and a
   settings screen inside the game.
4. Launcher — done; save tools and mods — partly done.
5. Randomizer and challenge runs (Nuzlocke, level caps, no items) — first versions done in 0.3.0.
6. Linux and Mac builds, several saves with a save manager — first versions in 0.3.5 (Linux and Mac need testers).
   **Next:** GameCube-exact timing switched on once proven, slow-PC work, more randomizer and challenge options.
7. Deeper modding and editors.
8. Pokémon Colosseum, on the same base.

## Thanks

- **Count-Sheep** for the data folder, portable mode and disc picker, and for ideas on testing and validation.
- **encounter** for [Aurora](https://github.com/encounter/aurora), [Borealis](https://github.com/encounter/borealis)
  and [nod](https://github.com/encounter/nod) (reading GameCube disc images, including compressed ones).
- The [Dolphin](https://dolphin-emu.org) emulator, which this port is compared against to stay faithful.

## About the original game

Pokémon XD: Gale of Darkness was developed by Genius Sonority and published by Nintendo and The Pokémon
Company (2005). Pokémon is a trademark of Nintendo, Creatures Inc. and GAME FREAK inc.

## Legal

This project is not affiliated with or endorsed by Nintendo, The Pokémon Company, Genius Sonority, Creatures
or GAME FREAK. It contains no copyrighted game data. Released under CC0 1.0 (see `LICENSE`); the third-party
libraries in `third_party/` keep their own licenses.
