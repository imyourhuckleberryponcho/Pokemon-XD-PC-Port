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

## Status: public beta — version 0.6.1

**Playable from New Game into the story, with known bugs.** It is not finished: expect crashes, stalls and
graphics differences, and keep backups of saves you care about. Downloads are on the [Releases](../../releases)
page, one per system:

| System | State |
|---|---|
| **Windows** 10 / 11 (64-bit) | The main build: what is developed and tested every day. |
| **Linux** (x86_64) | Test build. Checked against the Windows build in automatic runs (same random numbers, same saves, same sound), but so far only with software graphics: it has **not been played on a real Linux PC or a Steam Deck** yet. |
| **Mac** | Test build. Players have run it on Apple M2 Macs (macOS 15 and 27, through Rosetta 2): picture, sound, controllers, settings and saving work. It is an Intel (x86_64) program: Apple-silicon Macs run it through Rosetta 2. The app opens the launcher. The newest parts (Pokémon XG, the Japanese pack, save import, Discord) have not been tried on a Mac yet. More testers wanted. |

**Found a bug, or want to help test?** Join the [Discord](https://discord.gg/XVbDVXH4Pn) — see
*Reporting a bug* below.

## Quick start

1. **You need** your own copy of Pokémon XD: Gale of Darkness (USA, `GXXE01`) as a disc image (`.iso`, `.rvz`, `.gcm`, ...). Windows 10 or 11, 64-bit, a graphics card with Direct3D 12 (Vulkan or Direct3D 11 as fall-backs).
2. **Download** your system's file from the [Releases](../../releases) page and unpack it.
3. **Start** `Pokemon XD Launcher.exe` (Linux: `Pokemon XD Launcher.sh`; Mac: the app). On the launcher's Disc page choose your disc image, then press Play.
4. **Pick how you play** (the launcher asks the first time): **Original** is exactly the GameCube game, **Enhanced** adds the port's improvements (recommended), **Custom** is your own mix.

The download's `Read me.txt` has all of the following, step by step.

## How to...
Everything here is done in the launcher (it opens first) unless it says "in the game". Your own discs are only read, never
changed, and nothing from any disc comes with this program: what you make from them is kept in your data folder
(`%APPDATA%\Pokemon XD`), on your PC.

### Play Pokemon XG (from your own XG disc)
- You need a disc image of Pokemon XG version 1.2.1 (the fan game) that you made yourself; this program does not
  include it.
- Launcher, Disc page: choose that image the way you would choose the XD one. It says "XG mode"; press Play.
- XG keeps its own saves, apart from XD's (in the folder "xg" inside your data folder), so your XD saves are never touched.
  A save only loads in its own game; the Save page marks every save XD or XG.
- Works in XG: the randomizer (also the starter), the challenge rules, and online battles and trades with other XG
  players (XD and XG players cannot play each other).
- Not in XG (greyed with a reason): Gen 4 Pokemon and rules, the Fairy / modern types (XG has its own), and language
  packs (they would replace XG's own text).
- To play XD again, choose your XD disc on the Disc page.

### Play in Japanese or another language (Extra assets, Language)
- The game stays your USA copy (keep it on the Disc page: the game's code comes from it); a language pack only
  changes its text, menus and pictures. You make a pack from a disc of that version.
- Japanese: launcher, Extra assets page, "Pokemon XD (Japanese)": choose your Japanese disc (game ID GXXJ01) and
  press "Make the Japanese pack". It copies a few hundred MB and takes a few minutes.
- German, French, Italian and Spanish: the same page, "Pokemon XD (European, PAL)": choose your European disc
  (GXXP01) and press "Make the language pack".
- When it says Done, choose the language in the "Language" row of the Play bar at the bottom of the launcher (or in
  the game: SETTINGS, Language). It applies the next time the game starts; in the game the "Restart now" row
  does that for you. Choose English (US) to go back.
- The port's own screens in these languages are machine-made translations: corrections are welcome.

### Battle Revolution models and the Gen 4 pack (Extra assets; needs your own Pokemon Battle Revolution disc)
- You need a disc image of Pokemon Battle Revolution (Wii, USA) as well as your XD disc.
- Launcher, Extra assets page, "Pokemon Battle Revolution (USA)": choose the disc and press "Make models and Gen 4
  pack". It usually takes a minute or two and is done once.
- Models: tick "Use them in the game" (the setting "Battle Revolution models", also in Settings and in the game's
  SETTINGS). Pokemon in battle then look like Battle Revolution's. It is an improvement, so it makes the preset
  Custom; it applies the next time the game starts. Without it the game looks as before.
- Gen 4 Pokemon (107 of them, Turtwig to Arceus): start a NEW playthrough, and on the New Game setup screen choose
  "Gen 4 Pokemon and rules". A save made with it stays tied to the pack and says so if the pack is missing. Not in
  the Original preset, and not in XG.
- After a program update, "Look again" and, if it says out of date, "make again" on the Extra assets page.

### Play online (battles and trades with a friend)
- You both need your own copy of the game, the same version of this program, and the same game (XD with XD,
  XG with XG). It works in every preset.
- Make a room: on the title screen choose VS MODE, then ONLINE BATTLE (or EXTRAS, ONLINE, Battle a friend), then
  "Make a room". Choose the rules (rule set, single or double battles, teams, levels, items). The game shows a
  6-letter room code (like ABC-DEF) in big letters: tell it to your friend and wait on that screen (it keeps
  looking for about 3 minutes).
- Join a room: the same menu, "Join a room"; type the code (on-screen keyboard, or your keyboard). You see the host's
  rules in plain words and what you may be missing (for example the Gen 4 pack: the game offers to restart into
  the room). Press Accept, then both press Ready. The game presses its own menus for you while a loading
  screen is up: do not press buttons then. Afterwards: Rematch or Leave.
- Trade: title screen, EXTRAS, ONLINE, "Trade with a friend", or in the story at the Pokemon Center PC, "Trade with
  a friend online". Same room code. Every Pokemon is checked before it is offered, and both of you say Yes before
  anything changes; a dropped connection changes nothing.
- The two PCs connect directly. There is no server of ours: only the button presses travel, encrypted, between you
  two. Free public services are used just to introduce the two PCs to each other. Your friend can see your
  internet address (the game warns you): only share a code with people you trust.
- "Could not open a direct connection" means one of your networks blocks it (a VPN, a strict router, some mobile
  or company networks). Try: turn a VPN off; have one of you play on another network, for example a phone hotspot;
  check that the code is typed right and that you both have the same version. Still stuck: press F10 right
  then and send the report (see "When something goes wrong").

### Bring a save in (Save page: Dolphin / GameCube, Japanese, European, GBA, Colosseum)
Do these with the game closed. Nothing is ever replaced: you always get a NEW save, and the file you chose is only copied.
- Dolphin or a GameCube memory card: Save page, "Import from Dolphin / GameCube...". The launcher looks in Dolphin's
  folders by itself (USA, European and Japanese saves) and lists what it finds; click one to see what is in it, then
  "Add as a new save". Something elsewhere (a .gci, a whole memory card image, a cheat device's .gcs / .sav): "Choose a file...".
- Japanese save: "Import a Japanese save...". It is turned into the USA game's layout: trainer, Pokemon, PC boxes, bag,
  Pokedex and story are kept; names stay in Japanese characters, and the game goes on at the entrance of the place you
  saved in. The text stays English unless you made the Japanese pack and chose Japanese (see above).
- European save: added as it is. With the European language pack made and its language chosen, it reads as you played it;
  otherwise the text is English.
- Game Boy Advance trade: open a save on the Save page, press "GBA trade...", choose a GBA save (Ruby, Sapphire, Emerald,
  FireRed, LeafGreen), pick one Pokemon on each side (the list says why one cannot go) and press "Trade these two". Both saves
  are backed up first; the GBA save is not changed (a new copy named &lt;name&gt;.traded.sav is made beside it).
- Colosseum: open a save, press "Colosseum import...", choose a Pokemon Colosseum (USA) save, tick the Pokemon and press
  "Copy the ticked Pokemon into the PC boxes". Shadow Pokemon not purified yet and Eggs stay behind.
- Saves from XD and XG look alike: when the launcher cannot tell which game a save is from it asks you.

### Randomizer and challenges
- Start a NEW GAME (not in the Original preset). The New Game setup screen opens; there choose Randomizer: Light,
  Classic, Chaos or Custom (every switch its own row: wild Pokemon, trainers, Shadow Pokemon, items, TMs, the starter
  and more). Every randomized game has a code: the same code makes the same game, so you can share it; "Use a friend's
  code" types one in.
- Challenge rules on the same screen: No items in battle, Nuzlocke (a fainted Pokemon is lost to the last PC box, one
  catch per area) and level caps (no experience past the next boss's highest level).
- The choice is kept with the save. The game restarts once by itself to apply it, straight back into your New Game.
  Randomizer and the challenges also work in XG.

### Discord presence
- If the Discord app is running on this PC, the launcher and the game show what you are doing ("In the launcher",
  "Story mode", "In a battle", "Online battle") and for how long, next to your name in Discord. It is on by default,
  talks only to the Discord app on this same PC, and shows no save name, trainer name or place.
- To turn it off: Settings page of the launcher, or SETTINGS in the game, "Discord presence".

### New in 0.6.1

- **Gen 4 Pokemon and Gen 4 rules are two separate choices.** You can use the Gen 4 Pokemon without the Gen 4 rules, and with the rules off the Gen 3 abilities work as before. Gen 4 Pokemon also work in Pokémon XG.
- **Performance option** (Settings) and smoother play on weak PCs: the sound chip now runs on its own thread.
- Fixed: Magmortar's colours (re-make the Battle Revolution models in the launcher: Extra assets says "Out of date: make again"); Deoxys invisible with the Spanish language pack (re-make the language pack).

### New in 0.6.0

- **Pokémon XG** (the fan romhack) can be played from your own XG disc, with the randomizer, the challenges and online XG-to-XG battles and trades.
- **Japanese language pack**: build it from your own Japanese disc (Extra assets in the launcher) and play the game in Japanese.
- **Japanese and European save import**: saves from Dolphin or a GameCube memory card can be brought in.
- **Discord presence** shows what you are playing and for how long (a setting, on by default).
- The port's own code is now under the **MIT license** (earlier releases stay CC0).
- Fixed: online double battles (the host can choose moves for both Pokémon again); Japanese name entry (the name shows once, on its boxes); Pokémon XG (trainers judge benched Pokémon as XG does on Dolphin, and a Pokémon with Download no longer freezes the game); the weather tint in widescreen; a black first picture after pausing.

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
  Pokémon on or off and the levels. If your router doesn't let the two PCs connect, the game says so.
- **Online trading with friends**, at the Pokémon Center PC in the story and from ONLINE on the title screen. Every
  Pokémon is checked; both say Yes before anything changes. Trade evolutions happen in the game's own scene.
- **Modern types** (New Game option: the Fairy type and the later type chart), **randomized starter**, **other languages**
  (German, French, Italian and Spanish from your own PAL disc; the port's own translations are machine-made,
  corrections welcome), **HD texture packs**, a **speedrun timer** with skip-the-logos and play statistics (caught /
  snagged / purified counts and steps are not done yet).
- Launcher: **save import** (Dolphin / GameCube, Game Boy Advance trade, Colosseum) and every page works with a controller.
- The port's screens look like the game's, with a loading screen; New Game and the save picker restart straight into the
  playthrough; the physical/special split is chosen per playthrough; the in-game SETTINGS page has a 60 fps row.
- Fixed: the Eevee-offer crash with Leaf / Ice Stone (Gen 4), the widescreen battle menu, black launcher pictures, menus
  too fast on high-refresh monitors; B backs out of the New Game setup.

Older versions (0.4.2 and before): the full history is on the [releases page](https://github.com/imyourhuckleberryponcho/Pokemon-XD-PC-Port/releases).

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
- If A and B (confirm and cancel) seem the wrong way round on your
  controller, the line `gamepad1.layout = label` (or `= position`) in `settings.ini` in
  `~/Library/Application Support/Pokemon XD` changes whether the buttons follow their printed letters or their
  place on the controller. Please tell us which controller it was.
- The download contains a script that writes a first-run report (no game data in it). Sending that file back,
  whatever happens, is the single most useful thing a Mac tester can do.

### Saves

Saves stay in the GameCube's own format, one `.gci` file per save, in `%APPDATA%\Pokemon XD\USA\Card A` (your first save) —
the same layout as Dolphin's "GCI folder" memory cards, so saves can be copied between Dolphin and the port. The launcher's
Save page keeps several saves side by side (every save after the first is in `%APPDATA%\Pokemon XD\saves\<id>`), makes
automatic backups (the newest 10, in a `save_backups` folder beside each save) and imports saves from Dolphin or a GameCube
card (USA, European and Japanese), trades with Game Boy Advance saves and copies Pokémon over from a Colosseum save.
Pokémon XG keeps its own saves (`%APPDATA%\Pokemon XD\xg`).
Tools you can build: `xd_save_dump` (show what is in a save), `xd_save_convert` (GameCube/PC byte order) and
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
on. Available now: **higher internal resolution**; **widescreen** (16:9,
16:10, 21:9 or the window's shape: a wider 3D view, with menus and battle boxes at the screen's sides); **frame
rate 60**; **frame pacing** for monitors that aren't 60 Hz; full screen; fast-forward, text speed and fast battle
animations; separate volumes; photo mode. A few options are marked "doesn't work yet" (anti-aliasing, texture filtering, original fog and glow, battle speed, run button, fix original bugs, purification speed): they are planned and do nothing so far. The same settings
can be changed inside the game: SETTINGS below EXIT in the field menu, or left of OPTIONS on the title screen
(the game is paused completely while that page is open). **Discord presence**: if the Discord app is running on
the same PC, the launcher and the game show "In the launcher" / "Story mode" / "In a battle" / "Online battle" and the
elapsed time in your Discord status. It talks only to the Discord app on your own PC through its local connection
(a named pipe on Windows, a socket on Linux and Mac): nothing is sent over the internet by the port, and no save
name, trainer name or place is included. It is on by default, is a preference (not part of the presets, so also on in
Original, where it never touches the game) and is never active in test runs; "Discord presence" in the launcher's
Settings or the in-game SETTINGS turns it off (docs/PRESENCE.md).

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
7. Pokémon Colosseum, on the same base.

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
