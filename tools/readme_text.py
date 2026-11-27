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


NEW_080 = """New in 0.8.0
------------
- Prize desk and Arceus Plates: the prize desk shows its goods on pages (L / R, or Q / W): page 1 is the game's own
  goods exactly, then Evolution items, Mega Stones, and Memories & Plates (also on XG). The 17 Arceus Plates are sold
  there (Gen 4 rules or later; the Pixie Plate with Modern types) for 10,000 coupons each. With the Gen 4 / Gen 5-7
  Pokemon on, an Arceus holding a Plate takes the Plate's coloured form (in battle, and in the party, summary and PC
  outside battle), its type changes with it, Judgment takes the Plate's type, and any holder of a Plate gets +20%
  power on the Plate's type. The prize desk's list is no longer cut at 60 goods: the last Gen 4 TMs show now.
- More forms from your Ultra Sun / Ultra Moon dump: Deoxys, Wormadam, Giratina Origin, Shaymin Sky, the Therian
  genies, Kyurem Black / White, Keldeo, Meowstic, Pumpkaboo / Gourgeist sizes, Zygarde 10%, Hoopa Unbound, Oricorio,
  Lycanroc, Necrozma, Floette Eternal and Magearna, plus look-only sets: Unown (28 letters), Vivillon (20 patterns),
  Furfrou (10 trims), Pikachu's 7 caps, Burmy cloaks, Shellos / Gastrodon, Deerling / Sawsbuck seasons, Genesect
  drives, Flabebe / Florges colours and Minior colours. A new setting, Random looks (Enhanced), gives a new Pokemon a
  random look from its set; it does not touch the game's random numbers. IMPORTANT: your Gen 4-7 packs must be made
  again (Launcher, Extra assets, Make again; the launcher says "Out of date"). Old packs keep working until then;
  saves made with the old packs keep their Pokemon. Making them again takes a while (about 20 minutes on a busy PC)
  and adds about 30 MB.
- The Gen 4 Battle Revolution faces have their own pictures (before, Chimchar, Monferno and Infernape showed Deoxys
  faces and Piplup showed Bonsly's). Every converted model keeps its full size box, so the PC box previews are
  complete and centred.
- HD texture maker: a button in the launcher (Extra assets, Other) makes a sharper copy of the game's 3D pictures from
  your own disc, without playing it. It asks before it downloads the upscaler (Real-ESRGAN, with its licences), works
  at 2x or 4x, can be stopped and continued, and keeps the original colours. The settings HD textures and HD texture
  pack switch the pictures live, in the middle of the game, without changing the game's random numbers. Windows and
  Linux; it is not in the Mac build yet. A whole game at 2x is about 1 GB and about an hour on a busy PC.
- Randomizer (code version 9; old codes make exactly the same game as before): it can change the teams of Mt. Battle
  and the Orre Colosseum (each with its own switch; every team follows the battles' own rules); you can leave whole
  generations out; a new row "Abilities of every generation" gives the Gen 4-7 Pokemon other abilities too (Gen 4
  abilities only with the Gen 4 rules, Gen 5-7 abilities only with a Gen 5-7 rule set); Battle CDs, Battle Bingo and
  Quick Battle are never randomized. Every new row is explained on the New Game screen.
- More Shadow Pokemon: Mt. Battle's 100 trainers can have Shadow Pokemon too, counted separately from the story's
  (each place gets the percentage of its own possible Shadow Pokemon; the New Game screen shows the real number, on
  the plain disc 100% is 201). A Shadow Pokemon you do not snag in Mt. Battle is still a Shadow Pokemon the next time
  you fight that trainer; one you snagged shows up as a normal Pokemon.
- Save editor: ability names and a one-line description for every ability (also the Gen 4 and Gen 5-7 ones, with the
  hidden ability), a Form row (Alolan forms, Rotom's appliances and the other forms of the packs), and Arceus follows
  its Plate (give it a Plate and its form changes; pick a form and the matching Plate is put in its hand). Make Shadow
  turns a Pokemon into a Shadow Pokemon, with 256 extra Shadow rows. Anything that changes the game marks the
  playthrough, as before.
- More PC boxes: up to 32 boxes.
- Stream overlay (off by default): your party as files for an OBS page, in three looks (Battle belt, Showcase, Native
  XD), with 3D pictures, moving pictures, and launcher buttons (Open overlay folder, How to use it in OBS). A guide is
  in the download (Stream overlay guide).
- Save states (experimental, off by default, Windows and Linux; the Mac comes later): save the whole game at any
  moment and put it back, four slots with a picture, place and date (key F1, or SAVE STATES in the field menu). Using
  it marks the playthrough like a cheat. Not with mods, a speedrun timer or online.
- Fixed: Poke Spots gave only Munchlax; the spot's normal Pokemon never appeared (in every version since 0.3.5, also
  with the randomizer or the packs). The spot lists are made from the disc at each start, so your save is fixed at
  once.
- Fixed: a freeze in the Purify Chamber ceremony with the Gen 4 or Gen 5-7 Pokemon (the game's drawing buffer was too
  small for the bigger models; with such models it is 1 MB now, the plain game is unchanged).
- Fixed: CONTINUE did not switch on the playthrough's rules (Gen 4 Pokemon, Nuzlocke, level cap, and so on) when the
  New Game setup screen is off (the Original preset, or "New Game setup screen" off in Settings), also after the
  "Faithful or Enhanced?" question. Now every way into CONTINUE sets them up. Your lists were never emptied, only the
  rules were not watching in such a session.
- Gen 5-7 models that are flat or long (Stunfisk and the like) are now sized correctly in the game (converter 0.2.7:
  make your packs again, as said above).
- Fixed on XG: the Gen 4 evolution items (Electirizer, Magmarizer and the like) blocked their own Pokemon's evolution.
- Fixed: the Meganium face in the party list and its small picture in the PC box shown for Plate-form Arceus.
- Online: a guest who is refused by a room now sees why (both versions named; the host's other reasons shown). The
  challenge check on the New Game screen under Faithful is back.
- The New Game screen and the other switch rows: the right button changes the value too. UP from the top of the field
  menu goes to SETTINGS (it went to EXIT).
- Mods: a mod can say it needs (or recommends) your own extra assets. Rooms for modders (experimental): your own glTF
  models as room parts, pieces from other rooms, changing a room of the game, extra entrances, and a plain message at
  CONTINUE when a save used a changed room. The guides are in docs/modding. No example room yet.
- Android (BETA, testers wanted): Pokemon XD PC now also runs on Android phones, tablets and BlueStacks, as one .apk file (arm64 and x86_64), in the same releases. It has on-screen touch buttons (three
  layouts you can move and resize), a pause list, a phone launcher with the same settings, saves, mods and updates as
  the PC. It is a beta: it can still crash, freeze or stutter, and not every phone has been tried. You need your own
  disc image on the phone, and Android 9 or newer. The app is not in the Play Store (allow "install unknown apps"
  once). On the phone you can make the packs yourself (Battle Revolution Gen 4, Colosseum, languages, Ultra Sun models
  and Gen 4; the Gen 5-7 pack still has to be made on a PC). Want to help test? Join the Discord (link at the top of
  this page), say which phone you have, and send the bug report (pause list, BUG REPORT) when something goes wrong.
- Online: 0.8.0 plays with 0.8.0 only (the battles changed). Older versions are turned away with a clear message on
  both sides.
"""


NEW_077 = """New in 0.7.7
------------
- Gen 5-7 abilities: with a Gen 5, 6 or 7 rule set on, the abilities whose effect is in the game now say what they do
  in the summary (before, they all said "No effect with the XD rules." although they have worked since 0.7.1). The
  nine that only matter next to an ally say "Works in double battles only."; the ones that still have no battle code
  say "Its effect isn't in the game yet."
- Galvanize, Liquid Voice, Aerilate and Refrigerate now change the move's type, for the type chart and for the
  same-type bonus too.
- Gen 5-7 moves: the moves the Gen 5-7 pack adds are physical or special by their own category when the physical/special
  split is on (Struggle Bug is special, Flame Charge is physical), also on XG.
- Wonder Guard no longer lets a Shadow move hit through it when the Pokemon holding it is a Shadow Pokemon (a Shadow
  Shedinja; it only comes up with the randomizer or More Shadow Pokemon).
- Feebas: with a Gen 5, 6 or 7 rule set it evolves into Milotic when it levels up holding a Prism Scale (a new item,
  sold at the prize desk with those rules; the scale is used up). In the plain game nothing raises its Beauty, so it
  never evolved.
- Modern types: Fire against Fairy is neutral now (it was resisted).
- Purify Chamber: a crash is fixed.
- A crash on busy scenes with many Ultra Sun / Ultra Moon models (more likely at 4x resolution) is fixed: the game no longer
  stops when a frame's graphics memory is full.
- Closing the game after using the music at normal speed in fast-forward no longer ends in a "stopped" report.
- The start log has one line for the bigger models of the extra-assets layers instead of one line per model.
- The randomizer is explained on the New Game screen: every option and every choice has a description page (long ones
  have several pages: turn them with R / L, or X / Y where those are taken). A Randomizer guide button in the launcher
  (Preset page and Save page) opens "Randomizer guide.txt" from the game folder.
- Sound: the reverb (the echo in caves and rooms) now runs the game's own code instead of a stand-in, so caves and
  rooms sound a little different from 0.7.6.
- Online: 0.7.7 plays with 0.7.7 only (the battles changed). Older versions are turned away with a clear message on
  both sides.
"""


NEW_076 = """New in 0.7.6
------------
- More Shadow Pokemon is now a percentage: Off, 10%, 25%, 50%, 75% or 100% (it replaces A few / Many / All). It
  is the share of the Shadow Pokemon that are possible: a story trainer's own Pokemon turns into a Shadow
  Pokemon (a trainer's Dustox becomes a Shadow Dustox, with the same level, moves and item, and you snag and
  purify it as usual; no other species is swapped in). No species is a Shadow twice in the whole game: a
  Pokemon whose species is a Shadow already (the game's own ones included) is skipped and the trainer's next
  one is used, so 100% is every species the story trainers have that is no Shadow yet. On the USA disc with
  the original Pokemon: 10% makes 12, 25% makes 29, 50% makes 59, 75% makes 88 and 100% makes 117; with the
  randomizer the numbers are those of your code's randomized teams. The New Game screen shows the real number
  for your choice. Each trainer's lead comes first, then the second and third, spread through the story, so a
  trainer can have several Shadow Pokemon and you can snag more than one in a battle (the Snag Machine needs a
  turn to recharge). Mt. Battle and the Colosseums never get Shadow Pokemon. With the randomizer the same rule
  applies to the randomized teams.
- A game you started with A few, Many or All in 0.7.5 stays exactly as it was (a game started with "All" keeps
  its 44).
- A save made with a percentage in 0.7.6 can't be opened by 0.7.5 or older: they say so and change nothing on
  the card. The Shadow Pokemon past the game's own list (more than 44 extra ones: 50% and up) are kept in a
  small extra file next to the save; it goes with the save when you back it up or move it.
- Not yet for those Shadow Pokemon past the game's own list: trading them (PC trade, GBA trade) and online
  battles. The game says why and refuses them.
- Pokemon XG: the Gen 4-7 Pokemon now have their own cries (they used the cry of the Pokemon they stand in for).
- The save info no longer lists "Gen 4 rules" next to a Gen 5/6/7 rule set.
- Online battles are unchanged: 0.7.6 plays with 0.7.5.
- Thanks to everyone who asked for this one.
"""


NEW_075 = """New in 0.7.5
------------
- Online battles: when your Pokemon fainted in a room, the game sent the fainted Pokemon out again instead of
  opening the party menu, until the battle broke. The party menu now opens, is drawn, and refuses a fainted
  Pokemon or one already in battle. (0.5.0 to 0.7.4)
- Online battles: your whole team fights in single rooms (up to six); before, only the first two took part.
  The bag also opens in rooms, and a drawn battle says it was a draw instead of "the connection was lost".
- Online battles: 0.7.5 only meets 0.7.5 (the battles changed). Older versions are turned away with a clear
  message on both sides. A room also says which pack differs (Gen 4 or Gen 5-7) when two players' packs don't
  match.
- Fairy type: with "Modern types" on, the Gen 5-7 Fairy Pokemon and forms (Sylveon, Flabebe, Xerneas, Mega
  Gardevoir...) are Fairy now; before they kept their old type. Without the rule they keep the old types, as
  before.
- Poke Spots: Gen 5, Gen 6 and Gen 7 each add five Pokemon a spot (three common, one rare, one starter), and
  with Gen 7 the Alolan Geodude, Diglett, Sandshrew (Rock), Vulpix, Meowth (Oasis), Rattata and Grimer (Cave).
  Only the generations your playthrough has; a Pokemon already waiting at a spot stays.
- Poke Spots: with Gen 4 and Gen 5-7 Pokemon on, a warning window at every start ("esabapokemondata ... no
  #3") is gone, and the chances add up again (the last Gen 5-7 Pokemon of each spot could never appear). No
  need to make your pack again.
- Alolan Rattata made from your Ultra Sun / Ultra Moon dump was invisible in battle and in the summary; it
  shows now, with the packs you already have.
- Ultra Sun / Ultra Moon packs (press Make again once; the launcher says "out of date", older packs still
  work): the Gen 5-7 Pokemon that evolve by a stone, a trade, a held item or a condition (Sachet, Whipped
  Dream, knowing a move...) evolve the way they should, and the prize desk sells the items they need.
  Spiritomb's face shows through its shell; Magcargo, Dusknoir, Finneon, Umbreon, Dialga and six more have
  their right colours; Munchlax gets its own pictures and can be picked as a starter again.
- Playing online with Ultra Sun / Ultra Moon packs: both players make the pack again, a re-made pack doesn't
  match an older one.
- Double battles with Gen 4-7 moves: moves aim at the right targets (Dark Void puts both foes to sleep, Aura
  Sphere hits one).
- Pokemon XG: the Gen 4 and Gen 5-7 Pokemon show their own faces and PC box pictures (Gothita had Mew's face).
- The opening's Eevee offer: a picture that came out wrong on your PC (a white box) is drawn again, or the
  Pokemon's face is shown.
- Controllers: the launcher shows every controller the PC sees, and "Set it up..." makes a button list for one
  the program doesn't know yet (you press each button once).
- Making the pack no longer says "64 Pokedex pictures are missing" every time (those were empty rows, not
  Pokemon).
- Bug reports (F10) also list the textures in use, to find missing or white pictures.
- Thanks to everyone who tested and reported bugs.
"""


NEW_074 = """New in 0.7.4
------------
- Two copies on one save: starting a second game on a save that is already open now says which save, folder and
  program holds it, and offers "Try again", "End the other copy", "Play without the card" or "Quit". A copy
  that is refused no longer blocks the first one, and a restart that hangs is ended by itself after a minute.
- Long Pokemon names: Gen 5-7 names of 11-12 letters (Fletchinder...) show whole in battle, the party list
  and the summary, not cut at 10 letters.
- The little pop-up at the left edge (new mail, info, a Pokemon ready to purify) no longer shows the Purify
  Poke Ball on top of the mail picture.
- The loudest Gen 4 and Gen 5-7 cries from your Ultra Sun / Ultra Moon dump (Dialga, Giratina...) are
  brought down to about the game's own volume. In Enhanced the sound output also has a soft limiter for
  the loudest peaks (off in Original and Faithful). Packs made before this say "out of date, make again":
  press Make once on the Ultra Sun / Ultra Moon card.
- PC box and party pictures of the Gen 4 and Gen 5-7 Pokemon made from your Ultra Sun / Ultra Moon dump are
  drawn from their own models, so Dialga no longer shows Raikou's picture (same "make again" for older
  packs).
- Ultra Sun / Ultra Moon eyes: the eyes of Zoroark, Zorua, Shuppet and others are drawn properly (same "make
  again").
- Playing online with Ultra Sun / Ultra Moon packs: both players press Make again, a re-made pack doesn't
  match an older one.
- GameCube controller adapter: when it is not found, the launcher and the game's log now say why (in use by
  another program, a wrong driver, not listed by Windows, the library missing), the launcher tries again while
  the Controls page is open and lets go of the adapter while the game runs.
- Save editor: every nature shows its name and what it raises and lowers ("Adamant (+Attack, -Sp. Atk)"), also
  in playthroughs with mods, the randomizer or Gen 4 / Gen 5-7 rules. "Add" finds its template Pokemon in your
  party and PC boxes and accepts a purified former Shadow Pokemon.
- The Custom randomizer starts with "Trainers: moves = natural (by level)".
- Pokemon XG: the Gen 4 pack's unused shop rows are no longer added to XG's shops, and the randomizer
  gives XG's first Shadow Pokemon the moves of their real level. Randomizer codes for Pokemon XD make the
  same game as before.
- With the Gen 4 / Gen 5-7 Pokemon on: Pokemon XG's prize desk now also sells the Gen 4 evolution items
  (with the Gen 4 Pokemon) and, with the Gen 5-7 Pokemon and the Mega Evolution rule on, the Alola Charm
  and the Mega Stones of Pokemon you have caught. The opening's Eevee offer on XG gets Leafeon (Leaf
  Stone) and Glaceon (Ice Stone) as on XD, and with the Gen 5-7 Pokemon, on XD and XG, Sylveon (Eevee +
  Shiny Stone).
- Frame rate above 60: the in-between pictures stop when their time is used up and follow what the sound
  card has queued, so a slow PC should crackle less.
- The sound chip's busiest loops run as hand-written code (about 1.4 times faster, sound unchanged to the
  last bit).
- The graphics-command reading skips re-checking texture memory that nothing wrote to.
- The Linux download is less than a third of its old size (debug information and the old converter are taken
  out; crash reports still name the functions).
- The Ultra Sun / Ultra Moon converter in the download is now only the fast one; the old slow Python one is no
  longer included.
- Developer menu: "give <species> <level> shiny" and "giveshadow" for testing.
- Thanks to everyone who tested and reported bugs.
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
    return NEW_080 + chr(10) + NEW_077 + chr(10) + NEW_076 + chr(10) + NEW_075 + chr(10) + NEW_074 + chr(10) + NEW_073 + chr(10) + NEW_072 + chr(10) + NEW_071 + chr(10) + NEW_070 + chr(10) + NEW_061 + chr(10) + NEW_060 + chr(10) + NEW_05X


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


GUIDE_MD = os.path.join(ROOT, 'docs', 'RANDOMIZER_GUIDE.md')
GUIDE_TXT = os.path.join(ROOT, 'docs', 'RANDOMIZER_GUIDE.txt')


def guide_text():
    """docs/RANDOMIZER_GUIDE.md as the plain text the players get ("Randomizer guide.txt": next to the Read me and in game/,
    opened by the launcher's Randomizer guide button). The .txt in docs/ is made by:  python tools/readme_text.py --write-guide"""
    import textwrap
    raw = []
    for line in open(GUIDE_MD, encoding='ascii').read().split('\n'):
        line = line.rstrip()

        if raw and line.strip() and raw[-1].strip() and not line.startswith('#') and not raw[-1].startswith('#') \
                and not re.match(r'\s*- ', line):
            raw[-1] += ' ' + line.strip()
        else:
            raw.append(line)
    out = []
    for line in raw:
        line = re.sub(r'`([^`]*)`', r'\1', line)
        line = re.sub(r'\*\*([^*]+)\*\*', r'\1', line)
        line = re.sub(r'(?<![\w*])\*([^*\n]+?)\*(?![\w*])', r'\1', line)
        m = re.match(r'(#+) (.*)', line)
        if m:
            title = m.group(2)
            under = {1: '=', 2: '-', 3: '~'}[min(len(m.group(1)), 3)]
            out += ['', title, under * len(title), '']
            continue
        m = re.match(r'(\s*)(- +)?(.*)', line)
        indent, bullet, body = m.group(1), m.group(2) or '', m.group(3)
        if not body:
            out.append('')
            continue
        out += textwrap.wrap(body, 100, initial_indent=indent + bullet, subsequent_indent=indent + ' ' * len(bullet)) or ['']
    return re.sub(r'\n{3,}', '\n\n', '\n'.join(out)).strip('\n') + '\n'


def write_guide():
    with open(GUIDE_TXT, 'w', encoding='ascii', newline='\n') as f:
        f.write(guide_text())
    print('wrote', GUIDE_TXT)


def android_readme(version='0.8.0'):
    """The Read me for the Android test build (an APK; docs/ANDROID.md). Plain ASCII."""
    return """Pokemon XD PC """ + version + """ - Android (TEST BUILD)
=====================================================

This is a first test build of the port for Android phones, tablets and BlueStacks. It plays the same game as
the PC version, with touch buttons. It may still have problems; your reports help (see the end).

What you need
-------------
- Android 9 or newer (64-bit phone or tablet, or BlueStacks 5 with a "Pie 64-bit", Android 11 or Android 13
  instance). About 3 GB of free memory and 4 GB of free storage. A fairly recent phone is best.
- Your own disc image of Pokemon XD: Gale of Darkness (USA): an .iso, .gcm or .rvz file. The game is not
  included and never will be. Copy the file to the phone (any folder, Downloads is fine).

Installing
----------
1. Download "Pokemon-XD-PC-""" + version + """-android.apk" from the releases page:
   """ + RELEASES_URL + """
2. Open the file. Android says it may not install apps from this source: tap Settings, switch on "Allow from
   this source", go back, and tap Install. (The words differ a little on each phone: it is Android's own
   "install unknown apps" permission, which belongs to the app you opened the file with, for example your
   browser or file manager.)
3. Google Play Protect may say the app is unknown or ask to scan it. The app is not in the Play Store; tap
   "More details" and "Install anyway" (or "Scan app" and then install). Nothing is wrong: it only means
   Google has not seen this app before.
4. Open "Pokemon XD". Updates later: the launcher asks (once) if it may look for updates, and then offers a new
   version with its notes; you confirm, Android asks again, and the app is replaced. Your saves and settings stay.

First start
-----------
The app asks three things, one at a time: choose your disc image (Android's file window opens: find the file;
it is read where it is, not copied), the app checks it (about a minute the first time), and a page explains
the touch controls. Then choose Original (exactly the GameCube game) or Enhanced (the port's improvements).
Then press Play.

Playing
-------
- The GameCube buttons are on the screen. Touch the pause button (two bars) for the game's SETTINGS, and
  BUTTONS there to move or resize the touch buttons. The Controls page of the launcher has the layouts.
- A Bluetooth or USB controller works too; the touch buttons then hide.
- The launcher's pages (Disc, Preset, Settings, Controls, Mods, Save, Reports) are the same as on the PC.
  Scroll by dragging with a finger.
- The launcher turns with the phone: held upright it is one column with a menu button for the pages; held sideways it
  has the page list on the left. The game itself always plays sideways.

Where things are kept
---------------------
- Saves, settings and logs live inside the app's own storage, which other apps and file managers cannot see.
  To take a copy out: launcher, Save page, choose the save, Export...: Android's window asks where to put the
  file. To bring a save in (from a PC, Dolphin or another phone): Save page, "Import a save...".
- Uninstalling the app deletes everything inside it: export your saves first.
- Mods: copy a mod's .zip to the phone, then Mods page, "Add a mod (.zip)...".
- Extra assets (the Gen 4 / battle-model packs and other made-from-your-disc packs): the converters run only on
  a PC. On a PC make the pack with the PC launcher (Extra assets page), zip the pack's folder from the data
  folder's extra_assets, copy the zip to the phone, and on the Extra assets page choose "Use a pack from a PC
  (.zip)...".

Problems and reports
--------------------
If the game stops, crashes or looks wrong, it saves a report by itself. Open the launcher, Reports page: the
list shows them. "Save my last play session..." (or Export on a report) writes a zip where you choose, hiding
your user name. Nothing is ever sent anywhere: you send the zip yourself, on the Discord (""" + DISCORD_URL + """) or the
releases page. Please also tell us: your phone or tablet model, the Android version, how much memory it has,
which graphics chip if you know, and what you did just before.

On BlueStacks 5 (Windows)
-------------------------
1. Make a new instance of type "Pie 64-bit" (or Android 11 / 13): Multi-instance Manager, New instance, Fresh
   instance. The older "Nougat" instances cannot install this app.
2. Instance settings: Graphics renderer "Vulkan" (or "Vulkan & OpenGL"; if the game does not start, try
   "OpenGL"); Performance: at least 4 CPU cores and 4 GB of memory.
3. Drag the .apk file onto the BlueStacks window (or Ctrl+Shift+B, "Install apk"). It installs like on a phone.
4. Put the disc image where the instance can see it: BlueStacks side toolbar, "Media Manager", "Import from
   Windows", choose the .iso. In the app's file window look in the instance's Pictures, Documents or Download
   folder (BlueStacks puts it in one of them).
5. Use the mouse as a finger. A keyboard or a controller attached to the PC may work through BlueStacks'
   own key mapping; the touch buttons are always there.

Not working yet on Android
--------------------------
- Making extra packs on the phone itself (use a PC and copy, above).
- The Colosseum import and the GBA link pages are not tested on phones.
- Vibration, and a few touch-button details (some screens may not hide the buttons).
- Only the arm64 and x86_64 builds exist; 32-bit phones are not supported.
"""


def android_release_lines(version='0.8.0'):
    """The lines about Android for the release text (a TEST build, in a file of its own)."""
    return """Android (TEST build)
--------------------
- New: Pokemon XD PC for Android phones and tablets: one file, "Pokemon-XD-PC-""" + version + """-android.apk"
  (arm64 and x86_64). The same launcher, touch buttons, a controller, saves, mods and settings as on the PC.
  This is a first test: please report what you find.
- You need your own disc image on the phone. The app is not in the Play Store: allow "install unknown apps" for the
  app you open the file with, and tap "Install anyway" if Play Protect asks.
- Works with Android 9 and newer, and in BlueStacks 5 on a 64-bit instance (Pie 64-bit, Android 11 or 13).
- The app updates itself the same way the PC version does: it asks, shows what is new, and Android asks you to
  confirm.
"""


if __name__ == '__main__':
    if '--write-guide' in sys.argv:
        write_guide()
    elif '--check-guide' in sys.argv:
        ok = os.path.isfile(GUIDE_TXT) and open(GUIDE_TXT, encoding='ascii', newline='').read() == guide_text()
        print('guide: ok' if ok else 'guide: docs/RANDOMIZER_GUIDE.txt is out of date: python tools/readme_text.py --write-guide')
        sys.exit(0 if ok else 1)
    elif '--write-android' in sys.argv:
        out = sys.argv[sys.argv.index('--write-android') + 1]
        ver = sys.argv[sys.argv.index('--version') + 1] if '--version' in sys.argv else '0.8.0'
        with open(out, 'w', encoding='ascii', newline=chr(13) + chr(10)) as f:
            f.write(android_readme(ver))
        print('wrote', out)
    elif '--write-mac' in sys.argv:
        write_mac()
    else:
        print(mac_block())
