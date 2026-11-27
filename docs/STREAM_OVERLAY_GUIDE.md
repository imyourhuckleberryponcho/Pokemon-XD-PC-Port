# Stream overlay guide

The stream overlay shows your party (picture, name, level, HP bar, status) on your stream or in your recording. It is off until you switch it
on. It needs no server, no internet and no extra program: the game writes a few small files into its data folder, and OBS shows a page
from that folder.

## Four steps in OBS

1. Switch it on. In the launcher: Settings > System > Stream overlay. In the game: SETTINGS (after EXIT in the field menu, or next to
   OPTIONS on the title screen) > Stream overlay. Then start the game once.
2. Open the folder. In the launcher: Settings > System > "Open overlay folder". (By hand: the folder called stream_overlay inside the
   game's data folder, on Windows normally %APPDATA%\Pokemon XD\stream_overlay.)
3. In OBS: Sources > + > Browser. Tick "Local file", press Browse and pick one page from that folder (see "Which page to pick"). Set
   Width 1000 and Height 260. Leave everything else as it is. The background is transparent.
4. Play. The page follows your party by itself. OBS does not need to be restarted, and you can switch the overlay on and off while
   you play. Off shows nothing.

The first time, the program makes the game's own pieces (frames, bars, letters) from YOUR copy of the game. That takes a few seconds in
the background; until then a page shows plain boxes and it fixes itself when the pieces are ready.

## Which page to pick

All pages show the same party; they differ in how they look.

- overlay.html: the look you choose in SETTINGS ("Stream overlay look"). It changes at once when you change the setting.
- overlay_belt.html: Battle belt. One slim belt of party cards while you walk about. When a battle starts it opens: the two Pokemon that
  fight get the battle's name capsules with their 3D picture coming out of a round window, the others close into small round windows with
  an HP ring.
- overlay_showcase.html: Showcase. The Pokemon are the stars: each stands on a round metal stand above one metal rail with name, level and
  HP bar. In a battle the two that fight step forward.
- overlay_native.html: Native XD. The party screen's own cards, and in a battle the battle's own name capsules.

A page picked by its own name keeps its look, whatever the setting says.

## Sizes

- The normal shape is 1000 wide and 260 high, for the bottom or top of the picture.
- The tall shape is a column for the space beside a 4:3 picture. In OBS untick "Local file" and give the page as a web address of the file
  ending in ?shape=side, for example file:///C:/Users/you/AppData/Roaming/Pokemon%20XD/stream_overlay/overlay_belt.html?shape=side
  and use Width 400 to 420 and Height 620 to 1000.

## Moving pictures

With "Moving pictures" on (the default, under the Stream overlay switch) the Pokemon in Showcase and Battle belt move: they play
their idle animation. To make those pictures the program starts a second copy of the game, hidden and muted, for a few seconds per
Pokemon (about 20 seconds at the start). It never touches the game you play, never runs during a battle, and ends by itself when the game
does. If your PC is weak, or you do not want a second copy of the game running, switch "Moving pictures" off: the overlay then shows still
pictures and no second copy is ever started. Pictures already made are kept.

## What it never does

- It never uses the internet. There is no server and no web address in any page.
- It shows nothing in the game itself: the game looks and plays exactly the same with the overlay on or off (same random numbers, same sound).
- It only reads your party; it never changes your party, a battle or your save.
- Nothing is written while the switch is off. To remove everything, delete the stream_overlay folder.
- Nothing from the game is shipped with the program: the pictures and letters are made on your PC from your own disc.

## If something looks wrong

- A page shows only plain boxes: wait a few seconds; the pieces are made once, in the background.
- A Pokemon has a still picture but does not move: its moving picture is made in the background, about 3 to 5 seconds each; it switches by
  itself. Some models cannot be made (the still stays).
- OBS shows nothing: check that the page you picked is in the stream_overlay folder, that the overlay switch is on, and that the game has
  been started once with it on.

## Make your own look

Copy a page to a new name (for example my_overlay.html) and change the copy; the program never touches pages it did not write. The data
is in party.js and party.json in the same folder. docs/STREAM_OVERLAY.md in the public repository describes the format.
