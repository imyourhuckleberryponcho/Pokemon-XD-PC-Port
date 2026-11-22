# Music, battle effects and models

Own music and sound, new move effects and abilities, and own character models. Back to the [front page](README.md).

## Music and sound (0.7.0)

Full text: [music-and-sound.md](../modding/music-and-sound.md). The tool `xd_audio_tool` (needs the player's disc; writes only the author's own sound):
`list <disc>`, `info <disc> <archive.fsys>`, `decode <disc> <archive.fsys> [--pair N | --header-id ID] -o x.wav` (the game's own sound: for preview,
never shared), `preview <hdr.bin> <body.bin> -o x.wav` (a mod's two files back to WAV), `encode <sound.wav|ogg> <disc> <archive.fsys> --mod <folder>
[--pair N | --header-id ID [--body-id ID]] [--loop [--loop-start S] [--loop-end E] | --no-loop] [--trim-intro] [--max-blocks N]`.
- A song or cry = two entries of an archive: header (type 20, 192 bytes "I_SF") and sound (type 21, DSP-ADPCM, per 0x8000-byte block channel 0 then
  channel 1). A mod replaces them as `fsys/<archive>.fsys/#<header entry>.bin` and `#<sound entry>.bin`. The sound's entry id is the header's id minus 1.
  Songs: `stm_bgm_*.fsys` (one pair each), jingles `stm_me_*.fsys`; cries: `stm_se_nakigoe_archive.fsys` (388 pairs: all headers first, then all bodies;
  a species' cry = `pokemon_data.voice` -> `snd_data.file` -> `snd_file` row `{archive group, header id << 16, sound id << 16}`).
- Loops: the game jumps back to the start of a 0x10000-nibble block (57 344 samples); the tool lands the loop start on a block (silence in front, or
  `--trim-intro`) and makes the loop a whole number of blocks by resampling it and setting the header's rate. Cries are cut at 2 blocks.
- Sound streams count as game files: `"applies"` stays `new_playthrough` (a cry's or jingle's length changes how long a scene waits).
- **Single sound effects** (`sounds/effects.json`): `{"effects": [{"fx": 1, "file": "sounds/blip.wav", "volume": 0.8, "loop": "keep"}, {"sample": 24, "bank": "battle", "file": "..."}]}`.
  The game's effect = an `fx` number (0-200 menu bank, 201+ battle bank) -> a MusyX macro -> a sample id; samples live in `sound/snd_se.samp`
  (bank `menu`), `sound/snd_se_battle.samp` (`battle`), `sound/snd_music_atmos.samp` (`atmos`) with a 32-byte-per-sample directory (`*_sdir` entries of
  `common.fsys`) and 40-byte DSP-ADPCM coefficient blocks. The mod holds only the author's WAV/OGG: at start-up the loader encodes it (mono, at the old
  sample's speed setting) into the old sample's room (or at the file's end, at most +256 KB in all) and writes the changed directory + sample file as
  replaced disc files (`xd_sounds.cpp`, `engine/audio/audio_sfx.cpp`). `"fx"` must play exactly one sample (else use `"sample"`, with `"bank"` when the
  id exists in two banks, or `"all": true` = every sample the effect starts gets the sound). Tool: `xd_audio_tool sfx-list <disc> [--bank B]` (effects -> samples, sample table), `sfx-decode <disc> (--fx N | --sample N) -o x.wav`
  (the game's own sound: never shared), `sfx-try <sound> <disc> (--fx N | --sample N) [--volume X] [--loop|--no-loop] -o heard.wav` (what the loader will make).
  `tools/make_probe_effects.py <out> <first fx> <last fx> <mod id> <bank> [step Hz] [base Hz]` writes a probe mod (a made-up tone per effect) to find which effect
  is which sound by listening. Loaded at start-up: a change needs a restart. Effects never change the game's logic (RNG proven equal).
- **New songs** (`sounds/songs.json`): `{"songs": [{"name": "forest-theme", "file": "sounds/forest.ogg", "loop": true, "loop_start": N, "loop_end": N, "trim_intro": false, "volume": 1..127}]}`.
  Each song = a new archive `stm_modsongN.fsys` (header + body entries, built with the stream encoder) in `GSfsys.toc` + a `snd_file` row + a `snd_data` row
  after the cries' rows (copy of the song row 1267: stream flag, reverb 12). The song's number is the `snd_data` row. Ways to use it: a story room's
  `objects/<room>.json` `"music": {"bgm": "forest-theme"}` (name of any song of a mod that is on; unknown name = a problem line, the room gets no music row of its
  own); story scripts: `song @forest-theme` (stand-in `0x77......`, replaced like `species @x`) as the argument of `Sound.openPlay`; not from Luau yet.
  The rows are `unbound`: a playthrough does not record them, so a save loads without the mod and the song is simply not played.
- Never put the game's own streams, or what `decode` makes of them, in a mod: the packer refuses byte-equal copies.

## Battle effects: new move effects, new abilities, rules for one battle

Full page: [battle-effects.md](../modding/battle-effects.md). In short for an assistant:

- A mod's `battle/*.json` holds `"moves"`, `"abilities"` and `"rule_sets"`. The move ROWS (type, power, copy of a donor move for the picture) are made in `data/*.json`
  as usual; `battle/` says what the move DOES. Names (moves, types, items) are resolved like everywhere else. Numbers: write `13` for a type when the name
  fails (`xd_data_mods <disc> --names type`).
- A move effect is a list of blocks: `stat`, `status`, `heal`, `hurt`, `recoil`, `drain`, `switch_out`, `weather`, `remove_item`, `set_type`, `field`, `message`.
  Tests are `"when"` texts (`target_has_status`, `weather:rain`, ...). `power_rules` / `accuracy_rules` change a move's power / accuracy by a test.
- An ability is the same blocks under an event key (`switch_in`, `end_of_turn`, `ko`, `contact`, `switch_out`, `before_move`) plus `damage_dealt`, `damage_taken`,
  `stat_mods`, `absorb`, `stat_drop_block`, `status_block`, `priority`, `reflect_status`, `remove_secondary`. `"id"` is 124 to 255 (0 = next free from 124).
- Rule sets: `level_cap`, `level_all`, `banned_items`, `inverse`, `weather`; started by the Luau call `battle_rules("name")`.
- More blocks: `force_switch`, `terrain`, `side` (`aurora_veil`, `sticky_web`), `set_ability`, `cure_status`, `hurt_dealt`, `moody`, `stat: "best"`. Move keys: `crit`, `ignore_boosts`,
  `use_target_attack`, `defend_stat`, `first_turn_only`, `power_formula`, `protect`. Ability keys: `hit`, `hit_dealt`, `fainted`, `stat_dropped`, `any_faint`, `contrary`, `no_contact`, `convert`, `aura`,
  `breaks_auras`, `immune_moves`, `accuracy_against`, `accuracy_dealt`, `crit_when`. Tests: `target_status:x`, `user_status:x`, `terrain:x`, `{"move_in": [...]}`, `hp_crossed_half`.
- Do not promise: `force_double` (not built), a Luau function that returns effects, the trainer AI knowing absorb / bounce abilities.
- Random numbers: a block with `"chance"` below 100 draws one number of the game's own generator when it runs; never draw others. Online: the digest of the `battle`
  files is in the room fingerprint.

## Characters and models

Full text: [characters-and-models.md](../modding/characters-and-models.md). Tool: `python tools/model_convert/gltf_to_pkx.py` (Python 3 + numpy + Pillow; reads the player's own
disc copy of the character; writes game data: keep under build/).
- `list <disc.iso> <archive.fsys>` and `extract <disc.iso> <archive.fsys> <entry number> <out>`: read the player's GameCube disc image (FST, FSYS entries, LZSS) so the donor can be taken out (game data: under build/).
- `skeleton <donor> <out.json>`: the donor's joints (number, parent, rest position, animated or not), animation count, bounds.
- `swap <donor> <model.glb|gltf> <out> [--height H] [--turn DEG] [--flip-winding] [--no-texture] [--texture-size 128] [--attach N] [--joint-map map.json]`:
  the donor's skeleton, animations and PKX header stay; the body is replaced by the glTF's triangles (node transforms applied, one CMPR picture per material from the base-colour
  texture, scaled to the donor's height and set on its lowest joint). Default fit = **rigid per nearest bone** (each triangle follows the closest bone segment; hard seams);
  `--joint-map {"<glTF joint name>": <donor joint number>}` with a skinned glTF = weighted vertices; `--attach N` = the whole mesh on one joint.
- Two donor kinds, detected by the header: a trainer's / Pokemon's battle model (`pkx_<name>.fsys`, entry 0, type 15, with the 0x84-byte PKX header) and a person's model in a room
  (a bare HSD archive: entry type 2 of `field_common.fsys` / the room's archive; the model number of a story person = a row of `peopleInfoData`, `modelResID` >> 16 = the entry's id).
- The result replaces the donor's entry through the ordinary mod layout (`fsys/<archive>/#<n>.bin`). It contains the donor's skeleton and animations (game data): for the maker's own game; the
  checker cannot call it the game's own file (it differs) but it must not be shared. Not done: the port building it at start-up from the player's disc (needs the converter in C++), animations of the mod's own,
  new trainers (only replacing), world objects. Reader limits: the donor's joint constraints (`robj`) are dropped; direct-vertex trainer meshes cannot be READ (they are replaced, so it does not matter).
- Test without game data: `python tools/model_convert/test_model_convert.py` (made-up donor + `make_test_model.py`'s block robot).
