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

## Status: public beta — version 0.6.0

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

New in 0.6.0: Pokémon XG (the fan romhack) can be played from your own XG disc, with the randomizer, the challenges and online XG-to-XG battles; a Japanese language pack can be built from your own Japanese disc; Japanese and European saves can be imported (Dolphin or GameCube); Discord presence shows what you are playing (a setting, on by default); and the weather tint in widescreen and a black first picture after pausing are fixed. Japanese name entry shows the name once, on its boxes; in Pokémon XG trainers judge benched Pokémon as XG does and a Pokémon with Download no longer freezes the game; online double battles let the host choose moves for both Pokémon again. The port's own code is now under the MIT license (earlier releases stay CC0).

New in 0.5.6: no more game closing by itself at the start of some randomizer battles (a trainer's Pokémon that knew Conversion); the title screen now appears with a language pack and the Gen 4 rules together (and the loading screen says so if a title ever fails to appear); no more black ground with Resolution set to Match the window when the window's height isn't a multiple of 5; and the lightning at Kaminko's house is fixed.

New in 0.5.5: the Mac launcher no longer closes right after opening when you have a save (and keeps a log file); the Strategy Memo's cry screen shows the right waveform for every Pokémon; and the Mac first-run report no longer reports false problems when Apple's developer tools are not set up. Mac players: approve the new version once in System Settings > Privacy & Security > Open Anyway.

New in 0.5.4: Linux and Mac no longer crash when a Pokémon levels up and learns a move while it already knows four; the loading screen can no longer stay stuck after a restart (the title menu is pressed again, and handed to you with a note if the game still does not start); and folder names with accents no longer break the language pack or starting the game (Windows 10 1903 or newer).

New in 0.5.3: online battles no longer drift apart after the first turn (both PCs now agree on the arena and the random
numbers first), rooms with one Pokémon per side fight singles, the joiner's battle menus are no longer piled up with the
widescreen edge menus on, and saves made with the randomizer or the Gen 4 rules can host a room. A loading screen with music now
covers the lobby's automatic menu presses. Preparing the game for your PC now runs hidden in the background, explains itself,
is safe to interrupt and lets you press Play meanwhile. The updater now handles flat folders (it sets up a proper one beside it) and hints when the game sits in OneDrive.

New in 0.5.2: making an online room with the Gen 4 rules now offers to restart into the room (0.5.1 refused), and
on newer Linux versions the game no longer stops at about every second start with "Could not place the game's memory".

New in 0.5.1: a fix for the quick black flashes some players saw every second or two while playing 0.5.0.

New in 0.5.0:

- **Online friend battles** in VS MODE: one of you makes a room, the other types its code. Both PCs run the battle and
  only the button presses travel, encrypted, straight between the two PCs (no server of ours). Teams: a preset, your own
  save's party or PC boxes, or any Pokémon you pick; the host chooses the rules (Classic / Gen 4 / modern types), Shadow
  Pokémon on or off and the levels. If your router doesn't let the two PCs connect, the game says so (not yet tried
  through two real home routers).
- **Online trading with friends**, at the Pokémon Center PC in the story and from ONLINE on the title screen. Every
  Pokémon is checked; both say Yes before anything changes. Trade evolutions happen in the game's own scene.
- **Modern types** (New Game option: the Fairy type and the later type chart), **randomized starter**, **other languages**
  (English UK, German, French, Italian, Spanish from your own PAL disc; the port's own translations are machine-made,
  corrections welcome), **HD texture packs**, a **speedrun timer** with skip-the-logos and play statistics (caught /
  snagged / purified counts and steps are not done yet).
- Launcher: **save import** (Dolphin / GameCube, Game Boy Advance trade, Colosseum) and every page works with a controller.
- The port's screens look like the game's, with a loading screen; New Game and the save picker restart straight into the
  playthrough; the physical/special split is chosen per playthrough; the in-game SETTINGS page has a 60 fps row.
- Fixed: the Eevee-offer crash with Leaf / Ice Stone (Gen 4), the widescreen battle menu, black launcher pictures, menus
  too fast on high-refresh monitors; B backs out of the New Game setup.

New in 0.4.2 (bug fixes):

- **Pick your save at Continue:** with more than one save, Continue on the title screen lists them all with a picture
  of where you saved, the trainer, the play time and the rules of that save. With one save, nothing changes.
- **Shadow Pokémon's moves** were blank in the move menu: fixed.
- **Randomizer with Gen 4:** no more "mod conflict" window for the game's own parts (your own mods are still checked).
- **Ditto and Dialga** from Battle Revolution come with their full detail and shine. Press **make again** on the
  launcher's Extra assets page to update models you made before.
- **Randomizer:** TM descriptions fit their box.
- After choosing randomizer, Gen 4 or mods on the New Game screen, the game restarts straight into New Game (no logos or
  title menu in between).
- The launcher asks once how you want to play (Original or Enhanced); Enhanced is the default of a new install.
  Players who already have settings keep them.

New in 0.4.1:

- **Gen 4 Pokémon** (107, Turtwig to Arceus) for a new playthrough: own Pokémon Battle Revolution? The launcher's
  **Extra assets** page now also makes a Gen 4 pack from your disc; choose **Gen 4 Pokémon and rules** on the New Game
  screen. Own models, faces, cries, level-up moves and evolutions; their moves and abilities work as in Diamond and
  Pearl. Leafeon and Glaceon by stone, held-item evolutions, the Linking Cord for trade evolutions, and all those items
  plus TM51-TM92 as Poké Coupon prizes. Off by default: without it the game is exactly as before.
- **Gen 4 Pokémon at the Poké Spots** (added to each spot, in theme; the Sinnoh starters as rare finds) and a
  randomizer option **Include Gen 4 Pokémon**.
- The Battle Revolution converter is built into the program (no Python), updates on Linux and the Mac, script mods
  and mods that add new Pokémon, moves and items.
- Fixed: a crash in some move animations at 60 fps; a rare crash when closing the window on Windows.

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
     Pokemon XD Launcher.exe    <- start here
     Read me.txt
     game\             the launcher, the game and their libraries (keep them together)
   ```

2. Double-click `Pokemon XD Launcher.exe`. The launcher opens: choose the disc, a preset, settings, controls and mods, then
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
(the game is paused completely while that page is open). **Discord presence**: if the Discord app is running on
the same PC, the launcher and the game show "In the launcher" / "Story mode" / "In a battle" / "Online battle" and the
elapsed time in your Discord status. It talks only to the Discord app on your own PC through its local connection
(a named pipe on Windows, a socket on Linux and Mac): nothing is sent over the internet by the port, and no save
name, trainer name or place is included. It is on by default, is a preference (not part of the presets, so also on in
Original, where it never touches the game) and is never active in test runs; "Discord presence" in the launcher's
Settings or the in-game SETTINGS turns it off (docs/PRESENCE.md). Mods: see `xd_data_mods` and `xd_mods_check` (game data and file replacement mods, loaded from
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

   This writes `dist\Pokemon XD PC\` (see *Playing*): `Pokemon XD Launcher.exe` at the top, everything else in `game\`,
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

## Other ways to install

The game is also listed on [RecompLauncher](https://recomplauncher.com/game/pokemon-xd-pc-port), which can install and update it from these releases. It opens this project's own launcher, as the downloads above do.

## About the original game

Pokémon XD: Gale of Darkness was developed by Genius Sonority and published by Nintendo and The Pokémon
Company (2005). Pokémon is a trademark of Nintendo, Creatures Inc. and GAME FREAK inc.

## Legal

This project is not affiliated with or endorsed by Nintendo, The Pokémon Company, Genius Sonority, Creatures
or GAME FREAK. It contains no copyrighted game data. The port's own code is released under the MIT license (see `LICENSE`; releases before 0.6.0 stay CC0); the decomp code it
builds on is CC0, and the third-party libraries in `third_party/` keep their own licenses.
