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

## Status: public beta — version 0.7.4

**Playable from New Game into the story, with known bugs.** It is not finished: expect crashes, stalls and
graphics differences, and keep backups of saves you care about. Downloads are on the [Releases](../../releases)
page, one per system:

| System | State |
|---|---|
| **Windows** 10 / 11 (64-bit) | The main build: what is developed and tested every day. |
| **Linux** (x86_64) | Test build. Players run it on Linux PCs, and it is checked against the Windows build in automatic runs (same random numbers, same saves, same sound). More testers wanted. |
| **Mac** | Test build. Players have run it on Apple M2 Macs (macOS 15 and 27, through Rosetta 2): picture, sound, controllers, settings and saving work. It is an Intel (x86_64) program: Apple-silicon Macs run it through Rosetta 2. The app opens the launcher. More testers wanted. |

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

### Start options (for other launchers, Steam shortcuts, scripts)
Another launcher (RecompLauncher, a Steam shortcut, Steam Deck / Bazzite) can start the game with options written after
the program's name. The launcher takes the same options and hands them to the game. Every option is for that one
start only and is never saved (a disc that loads is remembered, as always).
- `--disc <file>`: use this disc image. Just the file after the program's name still works too.
- `--fullscreen`, or `--windowed` for a window.
- `--data-folder <folder>`: keep saves, settings, logs, cache and mods in that folder for this start. A new folder starts
  empty, so your normal saves are not touched.
- `--preset original` or `--preset enhanced`: play with that preset (Original is exactly the GameCube game).
- `--save <name>`: load the save with that name (the names on the launcher's Save page). A name that does not exist is
  reported and the game does not start.
- `--no-launcher`: go straight into the game with the remembered disc (or `--disc`); the program you started stays open
  until the game ends, so a Steam shortcut follows the game itself.
- `--help` and `--version`.

Windows: `"Pokemon XD Launcher.exe" --no-launcher --fullscreen --preset enhanced`. Linux: `./"Pokemon XD Launcher.sh" --no-launcher --fullscreen`.
Mac: `open -a "Pokemon XD" --args --fullscreen`. More in `docs/COMMAND_LINE.md`.

### Where your data is kept
Saves, settings, mods, logs and bug reports live in one data folder (`%APPDATA%\Pokemon XD`, `~/.local/share/Pokemon XD`,
`~/Library/Application Support/Pokemon XD`).
- The first time the launcher starts it asks once where it should be: your user folder (recommended: it survives deleting or
  updating the game folder) or next to the game (portable: for a USB stick or one folder).
- Launcher, Save page, "Where your data is kept": "Open data folder"; "Move my data..." (user folder <-> portable: it
  copies everything, checks the copy, switches, and puts the old folder in the recycle bin / trash so nothing is lost; the
  launcher then starts again); "Remove all game data..." (asks first, names the folder, offers to export your saves first).

### New in 0.7.4

- Two copies on one save: starting a second game on a save that is already open now says which save, folder and program holds it, and offers "Try again", "End the other copy", "Play without the card" or "Quit". A copy that is refused no longer blocks the first one, and a restart that hangs is ended by itself after a minute.
- Long Pokemon names: Gen 5-7 names of 11-12 letters (Fletchinder...) show whole in battle, the party list and the summary, not cut at 10 letters.
- The little pop-up at the left edge (new mail, info, a Pokemon ready to purify) no longer shows the Purify Poke Ball on top of the mail picture.
- The loudest Gen 4 and Gen 5-7 cries from your Ultra Sun / Ultra Moon dump (Dialga, Giratina...) are brought down to about the game's own volume. In Enhanced the sound output also has a soft limiter for the loudest peaks (off in Original and Faithful). Packs made before this say "out of date, make again": press Make once on the Ultra Sun / Ultra Moon card.
- PC box and party pictures of the Gen 4 and Gen 5-7 Pokemon made from your Ultra Sun / Ultra Moon dump are drawn from their own models, so Dialga no longer shows Raikou's picture (same "make again" for older packs).
- Ultra Sun / Ultra Moon eyes: the eyes of Zoroark, Zorua, Shuppet and others are drawn properly (same "make again").
- Playing online with Ultra Sun / Ultra Moon packs: both players press Make again, a re-made pack doesn't match an older one.
- GameCube controller adapter: when it is not found, the launcher and the game's log now say why (in use by another program, a wrong driver, not listed by Windows, the library missing), the launcher tries again while the Controls page is open and lets go of the adapter while the game runs.
- Save editor: every nature shows its name and what it raises and lowers ("Adamant (+Attack, -Sp. Atk)"), also in playthroughs with mods, the randomizer or Gen 4 / Gen 5-7 rules. "Add" finds its template Pokemon in your party and PC boxes and accepts a purified former Shadow Pokemon.
- The Custom randomizer starts with "Trainers: moves = natural (by level)".
- Pokemon XG: the Gen 4 pack's unused shop rows are no longer added to XG's shops, and the randomizer gives XG's first Shadow Pokemon the moves of their real level. Randomizer codes for Pokemon XD make the same game as before.
- With the Gen 4 / Gen 5-7 Pokemon on: Pokemon XG's prize desk now also sells the Gen 4 evolution items (with the Gen 4 Pokemon) and, with the Gen 5-7 Pokemon and the Mega Evolution rule on, the Alola Charm and the Mega Stones of Pokemon you have caught. The opening's Eevee offer on XG gets Leafeon (Leaf Stone) and Glaceon (Ice Stone) as on XD, and with the Gen 5-7 Pokemon, on XD and XG, Sylveon (Eevee + Shiny Stone).
- Frame rate above 60: the in-between pictures stop when their time is used up and follow what the sound card has queued, so a slow PC should crackle less.
- The sound chip's busiest loops run as hand-written code (about 1.4 times faster, sound unchanged to the last bit).
- The graphics-command reading skips re-checking texture memory that nothing wrote to.
- The Linux download is less than a third of its old size (debug information and the old converter are taken out; crash reports still name the functions).
- The Ultra Sun / Ultra Moon converter in the download is now only the fast one; the old slow Python one is no longer included.
- Developer menu: "give <species> <level> shiny" and "giveshadow" for testing.
- Thanks to everyone who tested and reported bugs.


### New in 0.7.3

- Save editor (launcher: Save, open a save, Edit save...): change your Pokemon (species, level, moves, IVs, EVs, item, nature...), your trainer (name, money, Coupons, play time) and your bag; also Japanese saves. Nothing is written until you press "Save changes", and the save as it was goes into the save backups first. A change that alters the game marks the playthrough as "cheats used" (no competitive online rooms, no valid speedrun); nicknames, names and moving Pokemon around do not. A Pokemon traded from a "cheats used" save says so first.
- Choose your starter (New Game screen, Enhanced): pick any Pokemon your playthrough has instead of Eevee, and with "Two starters" a second one. A page with search and a jump per generation opens after the name entry. A chosen starter marks the playthrough as "cheats used".
- Gen 4 Pokemon made from an Ultra Sun / Ultra Moon dump get their own faces and cries (Leafeon no longer shows Umbreon's face, Cranidos no longer cries like Wartortle). The launcher says "out of date, make again": press Make once on the Ultra Sun / Ultra Moon card (about 10 minutes).
- Abilities and moves: Illusion, Delta Stream's strong winds, Sky Drop (with its rules: it fails on Flying types, Pokemon of 200 kg or more and behind a Substitute), Phantom Force / Shadow Force can't be hit while gone, a wild Pokemon with Wimp Out or Emergency Exit runs away, and Imposter shows the copied Pokemon.
- New setting "Controller in the background": a controller keeps working while the game window is not in front (off by default; controllers only, never the keyboard).
- Smoother sound above 60 frames a second on slower PCs.
- Linux: the GameCube controller adapter library comes with the download.


### New in 0.7.2

- Fixed: the game stopped when entering Kaminko's House (and could stop in other busy scenes). It hit every 0.7.1 player, with or without extra models.
- Fixed: a missing name no longer crashes shops, item use, evolution or battle messages; it shows "????" instead.
- Fixed: "can't load or save" after making the Battle Revolution models in a playthrough with Gen 5-7 Pokemon (the save check no longer looks at picture numbers; nothing in the save had changed).
- Fixed: bug reports no longer contain the Windows user name (also with a portable game folder, or a disc on OneDrive, the Desktop or Downloads).
- Fixed: "????" items with the randomizer on Pokemon XG. New randomizer codes only give real items on XD too; old codes still give exactly the same game.
- Fixed: making the Ultra Sun / Ultra Moon pack works again with an XD disc in .rvz, .gcz, .wia or .ciso format.
- Fixed: making the Gen 5-7 pack no longer fails as a whole when some Pokedex pictures can't be drawn (they show "?" and are counted; making the pack again fills them in).
- Fixed (Linux): the language pack, Gen 5-7 pack and other helpers no longer fail with "exit code 127".
- Fixed: updating from a very deep folder on Windows; a Colosseum save import or GBA trade no longer loses Strategy Memo entries.


### New in 0.7.1

- Runs better on weaker PCs: threads that only wait now sleep, the sound held back after a stutter shrinks back sooner (so sound stays in step with the picture), less work in the graphics translation and the cutscene video, and the sound chip about 20 percent faster (the sound is bit for bit the same). The log says at start-up whether this PC's graphics were prepared by the launcher.
- Fewer freezes in the first battles: the launcher prepares the graphics the Battle Revolution models need too.
- Hidden abilities in the Gen 5, 6 and 7 rule sets: a wild Pokemon at a Poke Spot has it 1 time in 20, and the Ability Patch from the Mt. Battle prize desk gives any Pokemon its hidden ability.
- More abilities: Unnerve, Cursed Body, Harvest, Cheek Pouch, Imposter, Infiltrator (also past a Substitute), Corrosion, Dancer, Wimp Out and Emergency Exit (your own side), Power Construct, RKS System (17 Memories at the prize desk). Two-turn and trapping moves: Freeze Shock, Ice Burn, Geomancy, Phantom Force, Shell Trap, Beak Blast, Thousand Waves, Spirit Shackle, Anchor Shot. Not yet: Illusion, Sky Drop, Delta Stream's weather.
- Gen 5-7 Pokemon also work on Pokemon XG.
- A cheat menu (Enhanced): switch on "Enable cheats" in SETTINGS, then F5 (or hold L + R and press START) opens it: catching, EXP multiplier, money, items, Rare Candies, shiny chance, perfect IVs / EVs, no damage, win battles, instant purification. A save that used cheats is marked; it can't join competitive online rooms.
- The Ultra Sun / Ultra Moon converter is now built into the program: about 3 times faster and uses far less memory.
- A decrypted digital copy (.cia) of Ultra Sun / Ultra Moon works like a cartridge dump.
- Nintendo's GameCube controller adapter is found (Windows needs the same driver Dolphin uses).
- Mods can add up to 4096 new Pokemon, moves and items each (was 1024).
- Fixed: a crash at New Game when both the Battle Revolution and the Ultra Sun / Ultra Moon models were made; new Pokemon showing blank (no name) with Gen 5-7 and the Fairy type both on; Gen 4 Pokemon missing their Ultra Sun / Ultra Moon models and a very long "check your mods" box; endless restarts when a New Game's Pokemon could not be loaded (XG); a plainer message when an antivirus removed the game's program; Pokedex pictures no longer caught mid-blink.


### New in 0.7.0

The big one. Parts marked "needs" use a disc or dump you made yourself; nothing from them is included.

- **Gen 5, 6 and 7 Pokémon** (314, Victini to Zeraora): species, 261 new moves, about 110 new abilities, cries, models and round face pictures, made from your own Pokémon Ultra Sun / Ultra Moon (3DS) dump (needs). Alolan forms, **Mega Evolution** (a Z-button row in the move menu; Mega Stones at the Poké Coupon desk) and Primal Groudon / Kyogre. Battle forms change in battle (Aegislash, Mimikyu, Wishiwashi, Minior, Darmanitan, Meloetta, Greninja). Not complete: some new abilities and moves say "no effect in this game yet", and Z-moves only do damage.
- **Ultra Sun / Ultra Moon models** for the Gen 1-4 Pokémon too, and the **Gen 4 Pokémon can be made from that dump alone** (no Battle Revolution disc needed). Gen 4 moves, abilities and items now show in French, German, Italian, Spanish and Japanese.
- **Rule sets**: XD, Gen 4, Gen 5, Gen 6 and Gen 7, the Pokémon generations you want, and **Looks** (which model source to show, in your order, with a pick per Pokémon), on the New Game screen and in online rooms (Enhanced).
- **Pokédex** (Enhanced): the PDA's Strategy Memo opens a Pokédex for all 807 Pokémon, with details, stats, moves, evolutions, where to find each one and Ultra Sun / Moon entry texts (needs the dump). Original keeps the game's own Memo.
- **Native-looking menus**: SETTINGS in the field menu and the title, the EXTRAS box, the New Game window and the MODS page are drawn in the game's own style.
- **Battle replays**: keep your battles, play them back with pause, speed control, a replay bar and a free camera. Online battles save a file on both PCs.
- **Online formats**: Sleep clause, team preview, a battle clock and plain refusal screens. 0.6.1 players can still join a classic room.
- **More Shadow Pokémon** (New Game option, Enhanced): Off / A few / Many.
- **Modding** is much bigger (battle effects, sounds, textures, scripts in Luau, stories, bigger lists, a check-and-pack page, "Reload mods" without restarting). The [Modding Guide](docs/modding/README.md) and a [guide for AI helpers](docs/modding-ai/README.md) are in this repository.
- **Speed and looks**: cheaper graphics work, lighter fast-forward (music can stay at normal speed), frame rates above 60, optional FXAA / MSAA / texture filtering (off by default).
- Start options and a clear choice of where your data is kept; Japanese text for the port's own screens (machine-made).
- Fixed: the thin grey line down the battle menu in widescreen; a rare crash while taking a save picture; the icon maker no longer stops on a model that loads late.

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
