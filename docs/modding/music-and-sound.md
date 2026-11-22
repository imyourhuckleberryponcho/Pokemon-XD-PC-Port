# Your own music and cries

> Early version: the format can still change.

**What this page covers:** playing your own song or a Pokémon's cry instead of the game's, single sound effects, and new songs. You need a sound
file (WAV or OGG), your own disc image, and the program `xd_audio_tool`, which comes with the port (in the build's `bin` folder, next to
`xd_mod_check`).

**The rule from [sharing.md](sharing.md) still holds:** your mod holds only your own sound. The tool reads your disc to learn how
the game wants it, and writes nothing of the game into your mod. Do not put the game's own songs or cries in a mod you share:
the checker and the packer refuse a file that is the game's own.

## What can be replaced

| Sound | Where it lives | Replaceable today |
|---|---|---|
| Songs that play from the disc (about 100: the Pokémon Center, the ports, the Colosseum and battle songs, the title, the credits ...) and the short jingles | `stm_bgm_*.fsys`, `stm_me_*.fsys` on the disc | Yes, with the tool |
| Pokémon cries (388) | `stm_se_nakigoe_archive.fsys` | Yes, with the tool |
| Sound effects (menu sounds, move sounds, doors) | `sound/snd_se.samp`, `snd_se_battle.samp` and tables in `common.fsys` | Yes, one at a time: `sounds/effects.json` (see "Single sound effects") |
| Songs the game plays from music instructions, not from a recording (many field and ambient songs) | `bgm_archive.fsys` and the sound tables | Not yet |

`xd_audio_tool list <disc>` lists every sound stream your disc has.

## The game's sound format, in plain words

Each song or cry is **two pieces** inside a disc archive: a small *header* (192 bytes: speed, length, loop) and the *sound* (4-bit
DSP-ADPCM, the console's own sound format, in two channels). A mod replaces those two pieces. The tool makes both from your WAV or
OGG, and it can turn the game's own pieces (and your finished ones) back into WAV so you can listen.

Two things you should know:

- **Loops.** A song that loops goes back to a place in the middle when it reaches the end. The game can only jump back to the start of a
  *block* (a bit under 1.8 seconds of sound). So the tool does two things for you: it puts a little silence before the music (or, with
  `--trim-intro`, cuts a little from the start) so the loop's start falls on a block, and it plays the loop at a slightly different
  speed *setting* (for example 31 947 instead of 32 000) so the loop is a whole number of blocks, with the music itself resampled to match.
  You hear the same tune at the same speed; the loop stays seamless. The tool tells you what it did.
- **Cries are short.** A cry is read whole into memory, and the longest cry in the game is two blocks (3.6 seconds). The tool cuts a
  longer sound there and says so.

## Steps: replace a song

1. **Find the song.** `xd_audio_tool list <disc>` lists them. Names help (`stm_bgm_pokecen` is the Pokémon Center). To hear the
   original: `xd_audio_tool decode <disc> stm_bgm_pokecen.fsys -o original.wav` (that file is the game's own sound: keep it to yourself).
2. **Make your sound file.** WAV (16, 24 or 32-bit, or float) or OGG Vorbis; mono or stereo; any speed from 8 000 to 48 000 Hz is
   fine. Other formats: convert to WAV first (Audacity, ffmpeg). If it should loop, give the loop points: put them in the file (a WAV's
   "smpl" loop, an OGG's `LOOPSTART` and `LOOPLENGTH` tags, which game-music tools write) or on the command line.
3. **Make the two pieces into your mod folder:**

   ```
   xd_audio_tool encode my-song.ogg <disc> stm_bgm_pokecen.fsys --mod my-music-mod --loop-start 441000 --loop-end 3528000
   ```

   (`--loop-start` and `--loop-end` are sample numbers *of your file*. Leave them out to take the file's own loop, or the whole sound
   when the original loops and your file says nothing. `--no-loop` plays once.) It writes
   `my-music-mod/fsys/stm_bgm_pokecen.fsys/#0.bin` and `#1.bin`: exactly the files a mod uses to replace parts of an archive
   ([sharing.md](sharing.md)).
4. **Listen to what you made:** `xd_audio_tool preview my-music-mod/fsys/stm_bgm_pokecen.fsys/#0.bin my-music-mod/fsys/stm_bgm_pokecen.fsys/#1.bin -o heard.wav`.
5. **Add `mod.json`** (see [getting-started.md](getting-started.md); `"game": "GXXE01"`), check it (`xd_mod_check my-music-mod`, or the
   **Check this mod** button on the launcher's Mods page) and switch it on.

The port treats sound streams like the game's other files, so the mod's `"applies"` stays `new_playthrough` (the checker says so if you
write `any_time`): the length of a jingle or a cry changes how long a scene waits, and that is not "looks and sound only".

## Steps: replace a cry

(This replaces the cry an existing Pokémon already has. For a Pokémon your mod **adds**, give its row `"cry": "sounds/x.wav"` instead and the game packs the WAV itself: see [add-pokemon-moves-items.md](add-pokemon-moves-items.md).)

A cry is a pair of pieces in `stm_se_nakigoe_archive.fsys`, which holds all 388. Find the pair of the species you want:

1. `xd_data_mods <disc> --show pokemon_data <species number>` shows `voice` (a number of the `snd_data` table).
2. `xd_data_mods <disc> --show snd_data <voice>` shows `file`; `xd_data_mods <disc> --show snd_file <file>` shows three numbers: the
   middle one, divided by 65 536, is the *id* of the cry's header; the third, divided likewise, the id of its sound. (Example: Typhlosion,
   species 157, voice 619, file 173, `[1966, 330573824, 330639872]`: header id 5044, sound id 5045.)
3. `xd_audio_tool encode my-cry.wav <disc> stm_se_nakigoe_archive.fsys --mod my-music-mod --header-id 5044 --body-id 5045` (cries never loop).
   (Name both ids: for 9 of the 388 cries the sound's id is not the header's minus one.)

`xd_audio_tool info <disc> stm_se_nakigoe_archive.fsys` lists the 388 pairs with their ids and lengths, and
`decode <disc> stm_se_nakigoe_archive.fsys --header-id <id> --body-id <id> -o cry.wav` writes one as WAV, so you can check you have the right one.

## What the checker says

`xd_mod_check` (and the launcher button) reads your two pieces: it refuses a file that is the game's own, and warns when a mod replaces
a file inside an archive with something that is not the game's format. It does not (yet) listen to your sound: preview it.

## Single sound effects

A **sound effect** is one short sound: a menu blip, a footstep, a door, a hit, a Poké Ball clicking. You can replace any one of them with a WAV
or OGG file, without touching the others.

**How the game keeps them (so you know why it works this way):** the effects are small samples in two big files on the disc
(`sound/snd_se.samp` for menus and the world, `sound/snd_se_battle.samp` for battles, plus a small one for ambient loops). A list in
`common.fsys` says where each sample starts and how it sounds. The game plays an *effect* (a number); the effect plays a *sample*.

**Your mod holds only your own sound.** You do not copy any of the game's files. The port builds the new sample list and the new sample file
**in memory, from the player's own disc, when the game starts**: your sound is put in the old one's place (or after the end of the file when it is
longer). Nothing of the game is stored in your mod.

### Steps

1. **Find the effect.** `xd_audio_tool sfx-list <disc>` lists every effect (its number, and the sample it plays) and every sample (its length
   in seconds, speed, whether it loops). There are about 1,000 effects. To *hear* the original:
   `xd_audio_tool sfx-decode <disc> --fx 1 -o original.wav` (the game's own sound: keep it to yourself). The quick way to learn which number is
   which sound is the **probe**: make a mod that gives a few effects different beeps (see "Finding the effect you want" below).
2. **Make your sound.** WAV or OGG, mono or stereo, any speed. Short is better: an effect is read into the sound chip's memory, which is
   nearly full. A sound shorter than the original goes into the original's room; a longer one is added at the end of the file (up to 256 KB more in
   all). The port plays it at the original's speed setting, so it is not too high or low.
3. **Check what the game will play:** `xd_audio_tool sfx-try my-blip.wav <disc> --fx 1 -o heard.wav`. It builds the effect exactly as the game
   will and writes what the game will play, so you can listen. It also says what it did ("0.12 seconds at 44100 Hz (the old one: 0.13 seconds)").
4. **Put it in the mod.** `mod.json` as usual (`"game": "GXXE01"`), your sound in `sounds/`, and a file `sounds/effects.json`:

   ```json
   {
     "effects": [
       { "fx": 1, "file": "sounds/blip.wav" },
       { "fx": 217, "file": "sounds/thump.ogg", "volume": 0.8 },
       { "sample": 24, "bank": "battle", "file": "sounds/zap.wav", "loop": false }
     ]
   }
   ```

   | Key | What it does |
   |---|---|
   | `"fx"` | The effect to replace (a number from `sfx-list`). It must play exactly one sample, unless you add `"all": true` (then every sample it plays becomes your sound). |
   | `"sample"` | Instead of `fx`: the sample itself, when an effect plays several or you want every effect that uses it. Add `"bank"` (`"menu"`, `"battle"` or `"atmos"`) when the number exists in more than one. |
   | `"file"` | Your sound (WAV or OGG), a path inside the mod. |
   | `"volume"` | Louder or softer than the file (1 = as it is). |
   | `"loop"` | `true`, `false` or `"keep"` (the default: it loops when the original loops). `"loop_start"` / `"loop_end"` are sample numbers *of your file*. |

5. **Check it** with `xd_mod_check` (or the launcher's **Check this mod**): it reads the file, tells you about a missing or unreadable sound, and with
   your disc it also says when an effect number does not exist. Switch the mod on and **restart the game** (effects are read at start-up).

### What to know

- Several effects can share one sample (menu effects 0 and 1 both play sample 108): replacing the *sample* changes both.
- When two mods replace the same sample, the later one (by name) wins and the game says so.
- The game's *timing does not depend on effects*, so replacing one never changes the game's random numbers or the battle. A very long
  effect that never ends is not allowed (the 256 KB limit).
- Effects that the game builds from several samples at once (a move's whole sound) are listed with several sample numbers: replace each with
  `"sample"`, or all of them with the same sound by `"all": true`.
- Cries and songs are different files: use the sections above.

### Finding the effect you want

`sfx-list` has numbers, not names. Two ways to learn which sound is which:
- **Listen.** `sfx-decode` writes the original as WAV; the short ones (under a second) are the clicks, blips and hits.
- **Probe.** Make a mod with a different beep for each of 100 effects, play the part of the game you care about, and listen for the beep. The
  number is in the file name. (The port's tests do exactly this with `tools/make_probe_effects.py`.)

## Adding a NEW song

You can add songs that no part of the game plays yet, and use them from a room's music setting or a story script.

1. Make the song as before (a WAV or OGG; loop points in the file or in the entry).
2. Put it in the mod with a file `sounds/songs.json`:

   ```json
   {
     "songs": [
       { "name": "forest-theme", "file": "sounds/forest.ogg", "loop": true, "loop_start": 441000, "loop_end": 3528000 },
       { "name": "fanfare", "file": "sounds/fanfare.wav", "loop": false }
     ]
   }
   ```

   `"name"`: letters, digits, `-`, `_`, `.` (up to 48). `"loop"`: `true` / `false` (a WAV's or OGG's own loop is used when you give none).
   `"trim_intro"`: cut the sound before a loop instead of padding it. `"volume"`: 1 to 127 (the game's own scale; the default is a normal song's).
3. **Use it.**
   - **A room's music** (a story mod's `objects/<room>.json`): `"music": { "bgm": "forest-theme" }`. A name of a song of any mod that is on works;
     a name no mod gives is told when the game starts and the room gets no music of its own (use `"like"` to copy a room's music instead).
   - **A story script:** `song @forest-theme` gives the song's number (a stand-in the game replaces when mods load), for `Sound.openPlay`.
     See [story-scripts.md](story-scripts.md).
   - **In Luau:** not yet (scripts cannot start songs).
4. Check and switch on. A song is added at start-up as a new sound file (`stm_modsongN.fsys`) and two new table rows.

**Without the mod:** the save does not hold the song, only the room's own music row (which belongs to the mod's room). A save made with the mod loads
without it: the song is simply not played; the old music (or none) plays. The port does **not** tie the save to a song mod.

## See also

- [story-objects.md](story-objects.md): a room's music
- [story-scripts.md](story-scripts.md): `song @name` in a script
- [sharing.md](sharing.md): what may and may not be shared
