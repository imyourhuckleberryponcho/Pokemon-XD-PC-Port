r"""The player's Read me texts that are the same on Windows, Linux and Mac (every feature reads the
same on all three). One place, so the three Read mes cannot drift apart:

    tools/package_release.py   the Windows "Read me.txt"
    tools/package_linux.py     the Linux "Read me.txt"
    games/xd/mac/Read me (Mac).txt   the Mac one (a plain file the Mac packager copies: its shared part is made here;
                                     refresh it with:  python tools/readme_text.py --write-mac )

Only plain ASCII, written for players. A new feature = add its "How to" here and a line under "New in"; nothing in
this file may point at documents that are not published.
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MAC_README = os.path.join(ROOT, 'games', 'xd', 'mac', 'Read me (Mac).txt')
RELEASES_URL = 'https://github.com/imyourhuckleberryponcho/Pokemon-XD-PC-Port/releases'
DISCORD_URL = 'https://discord.gg/XVbDVXH4Pn'


SYSTEMS = {
    'windows': {'data': '%APPDATA%\\Pokemon XD', 'sep': '\\', 'launcher': '"Pokemon XD Launcher.exe"',
                'example': '"Pokemon XD Launcher.exe" --no-launcher --fullscreen --preset enhanced',
                'portable': 'the folder "portable" next to "Pokemon XD Launcher.exe"', 'bin': 'the recycle bin'},
    'linux': {'data': '~/.local/share/Pokemon XD', 'sep': '/', 'launcher': '"Pokemon XD Launcher.sh"',
              'example': './"Pokemon XD Launcher.sh" --no-launcher --fullscreen --preset enhanced',
              'portable': 'the folder "portable" next to "Pokemon XD Launcher.sh"', 'bin': 'your desktop trash'},
    'mac': {'data': '~/Library/Application Support/Pokemon XD', 'sep': '/', 'launcher': 'the app',
            'example': 'open -a "Pokemon XD" --args --fullscreen   (in Terminal; --no-launcher works the same way, or run '
                       '"Pokemon XD.app/Contents/MacOS/xd_launcher" --no-launcher directly)',
            'portable': 'the folder "portable" next to the app, which needs the app to sit in a folder you can write to',
            'bin': 'the Trash'},
}


def paths(system):
    s = SYSTEMS[system]
    d, sep = s['data'], s['sep']
    return {
        'data': d,
        'card': d + sep + 'USA' + sep + 'Card A',
        'backups': d + sep + 'save_backups',
        'saves': d + sep + 'saves',
        'xg': d + sep + 'xg',
    }


def presets():
    return """The presets (launcher, Preset page)
------------------------------------
- Original: exactly the GameCube game: same speed, same rules, same random numbers. Nothing is changed; the
  extras below that change the game (randomizer, challenge rules, Gen 4, speed-ups, widescreen...) are off.
- Enhanced (recommended for most players): the GameCube game plus the port's improvements: a sharper picture
  that fills your window, widescreen, shorter waits, a fast-forward key. The New Game setup screen then lets
  you choose a randomizer, challenge rules, Gen 4 Pokemon and rules and more for each new save. The game's own
  rules stay as they were until you choose otherwise.
- Custom: your own mix; changing a game setting in Original or Enhanced switches to it.
The launcher asks which one you want the first time it starts; you can change it any time on the Preset
page. Online battles and trades, Discord, controls, volumes and full screen work in every preset.
"""


def howto(system):
    p = paths(system)
    s = SYSTEMS[system]
    return f"""How to...
=========
Everything here is done in the launcher (it opens first) unless it says "in the game". Your own discs are only read, never
changed, and nothing from any disc comes with this program: what you make from them is kept in your data folder
({p['data']}), on your PC.

Play Pokemon XG (from your own XG disc)
---------------------------------------
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

Play in Japanese or another language (Extra assets, Language)
-------------------------------------------------------------
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

Battle Revolution models and the Gen 4 pack (Extra assets; needs your own Pokemon Battle Revolution disc)
-------------------------------------------------------------------------------------------------------
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

Play online (battles and trades with a friend)
----------------------------------------------
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

Bring a save in (Save page: Dolphin / GameCube, Japanese, European, GBA, Colosseum)
------------------------------------------------------------------------------------
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
  are backed up first; the GBA save is not changed (a new copy named <name>.traded.sav is made beside it).
- Colosseum: open a save, press "Colosseum import...", choose a Pokemon Colosseum (USA) save, tick the Pokemon and press
  "Copy the ticked Pokemon into the PC boxes". Shadow Pokemon not purified yet and Eggs stay behind.
- Saves from XD and XG look alike: when the launcher cannot tell which game a save is from it asks you.

Randomizer and challenges
-------------------------
- Start a NEW GAME (not in the Original preset). The New Game setup screen opens; there choose Randomizer: Light,
  Classic, Chaos or Custom (every switch its own row: wild Pokemon, trainers, Shadow Pokemon, items, TMs, the starter
  and more). Every randomized game has a code: the same code makes the same game, so you can share it; "Use a friend's
  code" types one in.
- Challenge rules on the same screen: No items in battle, Nuzlocke (a fainted Pokemon is lost to the last PC box, one
  catch per area) and level caps (no experience past the next boss's highest level).
- The choice is kept with the save. The game restarts once by itself to apply it, straight back into your New Game.
  Randomizer and the challenges also work in XG.

Discord presence
----------------
- If the Discord app is running on this PC, the launcher and the game show what you are doing ("In the launcher",
  "Story mode", "In a battle", "Online battle") and for how long, next to your name in Discord. It is on by default,
  talks only to the Discord app on this same PC, and shows no save name, trainer name or place.
- To turn it off: Settings page of the launcher, or SETTINGS in the game, "Discord presence".
- If nothing shows up while Discord is open, check that Discord is not running as administrator: a program started normally cannot reach it.

Start options (for other launchers, Steam shortcuts, scripts)
-------------------------------------------------------------
Another launcher (RecompLauncher, a Steam shortcut, Steam Deck / Bazzite) can start the game with options written after
the program's name. The launcher takes the same options and hands them to the game. Every option is for that one
start only and is never saved (a disc that loads is remembered, as always).
- --disc <file>        use this disc image. Just the file after the program's name still works too.
- --fullscreen         full screen; --windowed: a window.
- --data-folder <folder>   keep saves, settings, logs, cache and mods in that folder for this start. A new folder starts
                       empty (it is a clean copy of the game), so your normal saves are not touched.
- --preset original    or --preset enhanced: play with that preset (Original is exactly the GameCube game).
- --save <name>        load the save with that name (the names on the launcher's Save page). A name that does not exist is
                       reported and the game does not start.
- --no-launcher        go straight into the game with the remembered disc (or --disc); the program you started stays
                       open until the game ends, so a Steam shortcut follows the game itself.
- --help and --version.
Example: {s['example']}

Where your data is kept
-----------------------
Saves, settings, mods, logs and bug reports live in one data folder: {p['data']}.
- The first time the launcher starts it asks once where it should be: your user folder (recommended: it survives deleting or
  updating the game folder) or next to the game (portable: for a USB stick or one folder; {s['portable']}).
- Launcher, Save page, "Where your data is kept": "Open data folder"; "Move my data..." (user folder <-> portable: it
  copies everything, checks the copy, switches, and puts the old folder in {s['bin']} so nothing is lost; the launcher then
  starts again); "Remove all game data..." (asks first, names the folder, offers to export your saves first).
"""


NEW_073 = """New in 0.7.3
------------
- Save editor (launcher: Save, open a save, Edit save...): change your Pokemon (species, level, moves, IVs, EVs,
  item, nature...), your trainer (name, money, Coupons, play time) and your bag; also Japanese saves. Nothing is
  written until you press "Save changes", and the save as it was goes into the save backups first. A change that
  alters the game marks the playthrough as "cheats used" (no competitive online rooms, no valid speedrun);
  nicknames, names and moving Pokemon around do not. A Pokemon traded from a "cheats used" save says so first.
- Choose your starter (New Game screen, Enhanced): pick any Pokemon your playthrough has instead of Eevee, and with
  "Two starters" a second one. A page with search and a jump per generation opens after the name entry. A chosen
  starter marks the playthrough as "cheats used".
- Gen 4 Pokemon made from an Ultra Sun / Ultra Moon dump get their own faces and cries (Leafeon no longer shows
  Umbreon's face, Cranidos no longer cries like Wartortle). The launcher says "out of date, make again": press Make
  once on the Ultra Sun / Ultra Moon card (about 10 minutes).
- Abilities and moves: Illusion, Delta Stream's strong winds, Sky Drop (with its rules: it fails on Flying types,
  Pokemon of 200 kg or more and behind a Substitute), Phantom Force / Shadow Force can't be hit while gone, a wild
  Pokemon with Wimp Out or Emergency Exit runs away, and Imposter shows the copied Pokemon.
- New setting "Controller in the background": a controller keeps working while the game window is not in front
  (off by default; controllers only, never the keyboard).
- Smoother sound above 60 frames a second on slower PCs.
- Linux: the GameCube controller adapter library comes with the download.
"""


NEW_072 = """New in 0.7.2
------------
- Fixed: the game stopped when entering Kaminko's House (and could stop in other busy scenes). It hit every 0.7.1
  player, with or without extra models.
- Fixed: a missing name no longer crashes shops, item use, evolution or battle messages; it shows "????" instead.
- Fixed: "can't load or save" after making the Battle Revolution models in a playthrough with Gen 5-7 Pokemon (the
  save check no longer looks at picture numbers; nothing in the save had changed).
- Fixed: bug reports no longer contain the Windows user name (also with a portable game folder, or a disc on
  OneDrive, the Desktop or Downloads).
- Fixed: "????" items with the randomizer on Pokemon XG. New randomizer codes only give real items on XD too; old
  codes still give exactly the same game.
- Fixed: making the Ultra Sun / Ultra Moon pack works again with an XD disc in .rvz, .gcz, .wia or .ciso format.
- Fixed: making the Gen 5-7 pack no longer fails as a whole when some Pokedex pictures can't be drawn (they show
  "?" and are counted; making the pack again fills them in).
- Fixed (Linux): the language pack, Gen 5-7 pack and other helpers no longer fail with "exit code 127".
- Fixed: updating from a very deep folder on Windows; a Colosseum save import or GBA trade no longer loses Strategy
  Memo entries.
"""


NEW_071 = """New in 0.7.1
------------
- Runs better on weaker PCs: threads that only wait now sleep, the sound held back after a stutter shrinks back
  sooner (so sound stays in step with the picture), less work in the graphics translation and the cutscene video,
  and the sound chip about 20 percent faster (the sound is bit for bit the same). The log says at start-up whether
  this PC's graphics were prepared by the launcher.
- Fewer freezes in the first battles: the launcher prepares the graphics the Battle Revolution models need too.
- Hidden abilities in the Gen 5, 6 and 7 rule sets: a wild Pokemon at a Poke Spot has it 1 time in 20, and the
  Ability Patch from the Mt. Battle prize desk gives any Pokemon its hidden ability.
- More abilities: Unnerve, Cursed Body, Harvest, Cheek Pouch, Imposter, Infiltrator (also past a Substitute),
  Corrosion, Dancer, Wimp Out and Emergency Exit (your own side), Power Construct, RKS System (17 Memories at the
  prize desk). Two-turn and trapping moves: Freeze Shock, Ice Burn, Geomancy, Phantom Force, Shell Trap, Beak Blast,
  Thousand Waves, Spirit Shackle, Anchor Shot. Not yet: Illusion, Sky Drop, Delta Stream's weather.
- Gen 5-7 Pokemon also work on Pokemon XG.
- A cheat menu (Enhanced): switch on "Enable cheats" in SETTINGS, then F5 (or hold L + R and press START) opens it:
  catching, EXP multiplier, money, items, Rare Candies, shiny chance, perfect IVs / EVs, no damage, win battles,
  instant purification. A save that used cheats is marked; it can't join competitive online rooms.
- The Ultra Sun / Ultra Moon converter is now built into the program: about 3 times faster and uses far less memory.
- A decrypted digital copy (.cia) of Ultra Sun / Ultra Moon works like a cartridge dump.
- Nintendo's GameCube controller adapter is found (Windows needs the same driver Dolphin uses).
- Mods can add up to 4096 new Pokemon, moves and items each (was 1024).
- Fixed: a crash at New Game when both the Battle Revolution and the Ultra Sun / Ultra Moon models were made;
  new Pokemon showing blank (no name) with Gen 5-7 and the Fairy type both on; Gen 4 Pokemon missing their
  Ultra Sun / Ultra Moon models and a very long "check your mods" box; endless restarts when a New Game's
  Pokemon could not be loaded (XG); a plainer message when an antivirus removed the game's program;
  Pokedex pictures no longer caught mid-blink.
"""


NEW_070 = """New in 0.7.0
------------
This is the big one. Parts that need a game disc or dump of your own are marked.
New:
- Gen 5, 6 and 7 Pokemon (314 of them, Victini to Zeraora), made from your own Pokemon Ultra Sun / Ultra Moon (3DS)
  dump: species, moves (261 new ones), abilities (about 110 new ones), cries, models and round face pictures. Alolan
  forms, Mega Evolution (a Z button row in the move menu; Mega Stones at the Poke Coupon desk) and Primal Groudon /
  Kyogre. Battle forms change in battle (Aegislash, Mimikyu, Wishiwashi, Minior, Darmanitan, Meloetta, Greninja).
  Not everything is complete: some new abilities and moves say so in their text, and Z-moves only do damage.
- Pokemon Ultra Sun / Ultra Moon models can also be used for the Gen 1-4 Pokemon, and the Gen 4 Pokemon can be made
  from that dump alone (no Battle Revolution disc needed). Gen 4 moves, abilities and items now show in French, German,
  Italian, Spanish and Japanese too.
- Rule sets on the New Game screen and in online rooms: XD, Gen 4, Gen 5, Gen 6, Gen 7 (each with its generation's
  numbers and mechanics), the Pokemon generations you want, and "Looks" (which models to show: XD, Battle Revolution or
  Ultra Sun / Ultra Moon, in your own order, with a pick per Pokemon). Each player sees his own Looks online.
- Pokedex (Enhanced): the PDA's Strategy Memo opens a real Pokedex for all 807 Pokemon: list, details, stats, moves,
  evolutions, where to find each one (Poke Spots, Shadow Pokemon, trainers) and Ultra Sun / Moon entry texts. Original
  keeps the game's own Memo.
- Native-looking menus: SETTINGS in the field menu looks like the bag, the title SETTINGS like OPTIONS, the EXTRAS
  box, the New Game window, the MODS page (switch on / off, pick up and move), a mod's question as a person talking.
- Battle replays: keep your battles (setting), play them back with Space (pause), [ and ] (speed), a replay bar and a
  free camera you can move while it plays. Online battles save a file on both PCs too.
- Online formats: six formats, Sleep clause, team preview, a battle clock, and plain refusal screens that say why a room
  cannot be joined. 0.6.1 players can still join a classic room.
- More Shadow Pokemon (New Game option, Enhanced): Off / A few / Many.
- Modding is much bigger: new moves, abilities and battle rules without code, sounds, textures and music, scripts
  (now Luau), stories and bigger Pokemon / move / item lists, a check-and-pack page in the launcher, and "Reload mods"
  to see changes without restarting. The Modding Guide and a guide for AI helpers are in the public repository.
- Smoother and faster: the graphics work is cheaper, fast-forward is lighter, fast-forward can keep the music at
  normal speed, frame rates above 60 and optional FXAA / MSAA / texture filtering (all off by default).
- Start options (--disc, --fullscreen, --data-folder, --save, --no-launcher...) and a clear choice of where your data
  is kept.
- Japanese text for all of the port's own screens (machine-made: corrections welcome).
Fixed:
- The thin grey line down the battle menu in widescreen.
- A rare crash while taking a save picture, and the picture maker for the launcher's icons no longer stops on a model that loads late.
Also: a battle rule that sets a level cap (rule sets, mods) never changes your real Pokemon's level or experience.
"""

NEW_061 = """New in 0.6.1
------------
New:
- Gen 4 Pokemon and the Gen 4 rules are now two separate choices: you can have the Gen 4 Pokemon (made from your own
  Battle Revolution disc) without the Gen 4 rules, and with the rules off the Gen 3 abilities work as before.
- Gen 4 Pokemon also work in Pokemon XG.
- A Performance option (Settings) for weaker PCs.
- Smoother play on weak PCs: the sound chip now runs on its own thread.
Fixed:
- Magmortar's colours: its body was all red. Re-make the Battle Revolution models in the launcher (Extra assets says
  "Out of date: make again").
- Deoxys was invisible with the Spanish language pack. Re-make the language pack in the launcher (Extra assets says
  "Out of date: make again").
"""

NEW_060 = """New in 0.6.0
------------
New:
- Pokemon XG (the fan romhack) is supported: play it from your own XG disc, with the randomizer, the challenges and
  online XG-to-XG battles and trades.
- Japanese language pack: build it from your own Japanese disc (Extra assets in the launcher) and play the game in Japanese.
- Japanese and European save import: saves from Dolphin or a GameCube memory card can be brought in (the Save page).
- Discord presence: if the Discord app is running, it shows what you are playing and for how long. It is a setting and is
  on by default.
- The port's own code is now under the MIT license (earlier releases stay CC0).
Fixed:
- Online double battles: the host can choose moves for both Pokemon again.
- Japanese name entry: the name now shows once, on its boxes.
- Pokemon XG: trainers judge their benched Pokemon the same way XG does on Dolphin.
- Pokemon XG: a Pokemon with Download no longer freezes the game when it comes out.
- The weather tint (Sunny Day and the others) now covers the whole picture in widescreen.
- The first picture after pausing the game is no longer sometimes black.
"""

NEW_05X = """New in 0.5.6
------------
- Fixed the game closing by itself at the start of some battles in randomizer games, when a trainer's Pokemon knew Conversion.
- Fixed the title screen never appearing (only clouds) when a language pack (Spanish, French, German, Italian) and the Gen 4 rules were both on;
  if a title ever fails to appear, the loading screen now says so after about 25 seconds.
- Fixed black ground (grass, paths, battle floors) when Resolution is set to Match the window and the window's height isn't a multiple of 5.
- Fixed the lightning at Kaminko's house.

New in 0.5.5
------------
- Mac: the launcher no longer closes right after opening when you have a save (it opens normally now).
  It also keeps a log file now.
- The Strategy Memo's cry screen shows the right waveform for every Pokemon (some showed none, or another
  Pokemon's).
- Mac: the first-run report no longer reports false problems when Apple's developer tools are not set up.
- Mac players: approve the new version once in System Settings > Privacy & Security > Open Anyway.

New in 0.5.4
------------
- Linux and Mac: the game no longer crashes when a Pokemon levels up and learns a new move while it
  already knows four (the "forget a move" screen). Windows was not affected.
- The loading screen could stay stuck after "Restart into ..." (the game never got past the title menu). The
  restart now presses NEW GAME again if the game did not take it, and hands the title over to you with a short
  note if it still does not start.
- A folder name with accents or other non-English letters (for example "Diagnostico" with an accent) no longer
  breaks creating the language pack or starting the game from that folder. Needs Windows 10 version 1903 or newer.

New in 0.5.3
------------
- Online battles: fixes the two PCs showing different stages and then drifting apart after the first turn
  (a "desync"). Both now agree on the arena and the random numbers before the battle starts, and the
  Pokemon are checked to be the same on both sides.
- Online battles: rooms with one Pokemon per side (singles) now really fight singles.
- Online battles: the friend who joins no longer sees the battle menus piled up in the middle of the screen
  with the widescreen edge menus on.
- Online battles: a save made with the randomizer or the Gen 4 rules can now host a room.
- Online battles: a loading screen (with the music) now covers the short time while the lobby's menus
  are pressed for you, so button mashing can no longer disturb it. It gives up and lets go after 90 seconds.
- Preparing the game for your PC (shaders and pictures) now runs hidden in the background, says so in
  plain words in the launcher, and is safe to interrupt: closing the launcher stops it, and the next
  start picks up cleanly. You can press Play while it is still working.
- Updater: a copy in a flat folder (as the old 0.4.0 download unpacked) is now offered a proper player
  folder beside it, and a game kept inside OneDrive or another synced folder gets a hint to move it.

New in 0.5.2
------------
- Online battles: making a room with the Gen 4 rules now offers "Restart into the room" (0.5.1 said a
  restart could not fix it and only offered Back).
- Linux: fixes "Could not place the game's memory at its GameCube addresses", which stopped about every
  second start on newer Linux versions (2024 and later, e.g. Arch).

New in 0.5.1
------------
- Fixes the quick black flashes some players saw every second or two while playing 0.5.0 (caused by the
  new save pictures in the save list).

New in 0.5.0
------------
- Online friend battles (VS MODE): one of you makes a room, the other types its code. Both PCs run the
  battle and only the button presses travel, encrypted, straight between the two PCs (no server of ours).
  Teams: a preset, your own save's party or PC boxes, or any Pokemon you pick; the host chooses the rules
  (Classic / Gen 4 / modern types), Shadow Pokemon on or off, and the levels. If your router does not let the
  two PCs connect, the game says so.
- Online trading with friends: at the Pokemon Center PC in the story and from ONLINE on the title screen.
  Every Pokemon is checked before it is offered; both of you say Yes before anything changes; a dropped
  connection changes nothing. Pokemon that evolve by trade evolve in the game's own scene.
- Modern types (New Game option): the Fairy type and the later type chart.
- Randomizer: randomize the starter (random, pick 1 of 3, or the Eevee family).
- Other languages: German, French, Italian and Spanish from your own PAL disc (Extra assets
  page, then Language on the Play page). The port's own texts in French, German, Italian and Spanish are
  machine-made: corrections are welcome.
- Launcher: import saves from Dolphin / GameCube, trade with a Game Boy Advance save, import a Colosseum
  save; every page works with a controller (Steam Deck).
- HD texture packs (Dolphin-style), a speedrun timer, skipping the logo movies and play statistics
  (battles, bosses, time; caught / snagged / purified counts and steps are NOT done yet).
- The port's own screens look like the game's, with a loading screen. New Game and the save picker restart
  straight into the playthrough. The physical/special split is chosen per playthrough. The in-game
  SETTINGS page has a 60 fps row. Leafeon and Glaceon have full-body pictures in the Eevee offer.
- Fixed: the crash in the Eevee offer with Leaf Stone / Ice Stone (Gen 4); the battle menu in widescreen;
  black pictures in the launcher; menus that ran too fast on high-refresh monitors; B now backs out of the
  New Game setup screen.

Older versions (0.4.2 and before)
---------------------------------
The full history is on the GitHub releases page: """ + RELEASES_URL + """
"""


def new_in():
    return NEW_073 + chr(10) + NEW_072 + chr(10) + NEW_071 + chr(10) + NEW_070 + chr(10) + NEW_061 + chr(10) + NEW_060 + chr(10) + NEW_05X


def known_issues(system):
    return """Known issues in this build
--------------------------
- Timing is a few frames off the GameCube in some loads and scene starts (not visible in normal play;
  it matters for speedruns). The "Original" preset is not yet frame-exact.
- Sound is very close to the GameCube, but not bit-for-bit identical.
- Widescreen (an improvement, off in Original): people standing in the wide side areas keep still until
  they come back into the middle (the GameCube never animates them there); some lists and popups don't
  move to the sides yet. Please press F10 when something looks wrong.
- Rarely, a new game can get stuck at the HQ Lab when Professor Krane should hand over the Snag Machine. If it
  happens to you, press F10 right there and send the report.
- Japanese pack: the trainer's name entry is checked, the Eevee nickname entry has not been looked at yet.
- A few options in the Settings page are marked "doesn't work yet" (Anti-aliasing, Texture filtering, Original fog
  and glow, Battle speed, Run button, Fix original bugs, Purification speed): they are planned and do nothing so far.
"""


def to_markdown(text):
    """Plain Read me text as Markdown for the GitHub page: a line underlined with --- or === becomes a heading."""
    lines = text.rstrip(chr(10)).split(chr(10))
    out = []
    i = 0
    while i < len(lines):
        if i + 1 < len(lines) and re.fullmatch(r'[-=]{4,}', lines[i + 1]) and lines[i].strip():
            level = '##' if lines[i + 1][0] == '=' else '###'
            out.append(level + ' ' + lines[i])
            i += 2
            continue
        out.append(lines[i].replace('<', '&lt;').replace('>', '&gt;'))
        i += 1
    return chr(10).join(out) + chr(10)


def mac_block():
    """The shared sections as they go into the Mac Read me (between its own status text and its system notes)."""
    return presets() + chr(10) + howto('mac') + chr(10) + new_in() + chr(10) + known_issues('mac')


MAC_MARK_BEGIN = '=== SHARED TEXT BEGIN (made by tools/readme_text.py --write-mac; do not edit here) ==='
MAC_MARK_END = '=== SHARED TEXT END ==='


def write_mac():
    text = open(MAC_README, encoding='ascii').read()
    block = MAC_MARK_BEGIN + chr(10) + mac_block().rstrip(chr(10)) + chr(10) + MAC_MARK_END
    if MAC_MARK_BEGIN not in text:
        sys.exit('The Mac Read me has no shared-text markers yet: add the BEGIN and END lines where the block belongs.')
    text = re.sub(re.escape(MAC_MARK_BEGIN) + r'.*?' + re.escape(MAC_MARK_END), lambda m: block, text, flags=re.S)
    with open(MAC_README, 'w', encoding='ascii', newline=chr(10)) as f:
        f.write(text)
    print('wrote', MAC_README)


if __name__ == '__main__':
    if '--write-mac' in sys.argv:
        write_mac()
    else:
        print(mac_block())
