# New move effects, new abilities and battle rules

> Early version: the format can still change.

**What this page covers:** new move effects (Flame Charge raising Speed, Volt Switch swapping out, Shell Smash changing five stats), new
abilities with real effects (Moxie, Sap Sipper, Prankster), terrains, and rules for one battle (a level cap, inverse types, weather at the start).
They are written as plain text files made of JSON blocks; no programming is needed.

Two small examples to copy are in the repository: `tools/examples/battle_mods/gen57-moves` (twelve moves) and
`tools/examples/battle_mods/gen57-abilities` (thirteen abilities and three rule sets).

## Where the files go

A mod folder gets a `battle` folder. Every `.json` file in it is read (in the order of their names):

```
my-mod/
  mod.json
  data/add.json         the new move rows (see add-pokemon-moves-items.md)
  battle/moves.json     what the new moves DO
  battle/abilities.json what the new abilities DO
```

The move rows say the number part (type, power, accuracy, PP, priority, which move's picture is shown: `"copy": "Ember"`). The files in
`battle` say what the move does **besides** that. A file has up to three parts:

```json
{
  "moves":     { "Flame Charge": { ... } },
  "abilities": { "Moxie": { ... } },
  "rule_sets": { "Level 40": { ... } }
}
```

A mistake in one entry leaves that entry out and says why in the log (`[mods] problem: battle effects: ...`, with the file and line);
everything else still works. The checker `xd_mod_check` is not taught about the folder yet.

## Moves

```json
"Scald": { "effects": [ { "status": "burn", "target": "target", "chance": 30 } ] }
```

A move that has `effects` gets the port's own effect script. A damaging move (power above 0) is "the plain hit, then your effects". A
move with power 0 is "the picture, then your effects"; it first has to hit if one of the effects is aimed at the target. A move with
**only** `power_rules` or `accuracy_rules` keeps the effect of the move it copies.

### The effect blocks

Each block does one thing. The ones that act on a Pokémon take `"target"`: `"user"` (default for stats, heal, hurt), `"target"` (the one
the move hit; default for status, remove_item) or `"foes"` (every foe on the field). `"chance": 30` rolls once, with the game's own
random numbers, when the block runs. `"when"` is a test (see the list below): the block runs only if it is true. A `"message"` is shown
after the block (`{name}` is the Pokémon's name).

| Block | What it does |
|---|---|
| `{"stat": "atk", "stages": 1}` | A stat stage changes by that much (-6 to 6). Stats: `atk def spe spa spd acc eva`. The game's own message, and the game's own protection (Mist, Clear Body ...) work |
| `{"status": "burn"}` | `sleep poison toxic burn freeze paralysis confusion flinch`; the game's own rules apply (a Pokémon with a status problem is not given another) |
| `{"heal": 50}` | The Pokémon gets 50 percent of its maximum HP back (the game's own heal picture) |
| `{"hurt": 12.5}` | It loses 12.5 percent of its maximum HP |
| `{"recoil": 25}` | **Moves only.** The user loses 25 percent of the damage the move dealt |
| `{"drain": 50}` | **Moves only.** The user gets half the damage the move dealt as HP |
| `{"switch_out": "user"}` | **Moves only.** After the hit the user goes back and you choose the next Pokémon (Volt Switch, U-turn) |
| `{"force_switch": true}` | **Moves only.** After the hit the target is forced out and the foe's next Pokémon comes in (Dragon Tail, Circle Throw). It uses the game's own Roar scene, so a foe with nobody left stays in |
| `{"weather": "rain"}` | `sun rain sand hail`. Only if no weather is on |
| `{"remove_item": true}` | The target's held item is lost (Knock Off) |
| `{"set_type": "move"}` | The user's type becomes the move's type (or a named one: `"Water"`) until it goes back |
| `{"field": "my_terrain", "turns": 5, "end_message": "..."}` | A field state you name, for some turns. Tests can ask for it with `"field:my_terrain"` |
| `{"message": "words"}` | Only a message |
| `{"terrain": "grassy"}` | `grassy electric psychic misty`: a terrain for five turns (see "Terrains" below) |
| `{"side": "aurora_veil"}` / `{"side": "sticky_web"}` | Aurora Veil (damage x2/3 for the user's side for five turns) / Sticky Web (a foe that comes in on the ground loses a Speed stage). Put `"when": "weather:hail"` on Aurora Veil |
| `{"set_ability": 152}` | The Pokémon's ability becomes that one (a number, or the name of one of the game's own) |
| `{"cure_status": true}` | The Pokémon's sleep / poison / burn / freeze / paralysis is cured |
| `{"hurt_dealt": 100}` | The Pokémon loses that percent of the damage the hit just dealt (Innards Out) |
| `{"moody": true}` | One random stat +2 and another one -1 |

`{"stat": "best", "stages": 1}` raises the Pokémon's best stat (Beast Boost). `"set_type"` also takes `"target": "target"` (Soak). A move that has a
switch-out block **and** a status effect (Parting Shot) works too: the stats fall first, then the user goes back.

### More things a move can say

Next to `effects`, a move entry may hold (all optional):

| Key | What it does |
|---|---|
| `"crit": "always"` | every hit is a critical hit (Storm Throw), unless the target has Battle Armor / Shell Armor |
| `"ignore_boosts": true` | the target's Defense and Sp. Def changes do not count (Chip Away, Sacred Sword) |
| `"use_target_attack": true` | the damage uses the TARGET's Attack and its changes (Foul Play) |
| `"defend_stat": "def"` | a special move that meets the target's Defense (Psyshock) |
| `"first_turn_only": true` | fails unless the user came in this turn (First Impression) |
| `"power_formula": "weight"` | `weight` (Heavy Slam: the user's weight against the target's), `speed` (Electro Ball), `boosts` (Stored Power: 20 + 20 for each raised stage) |
| `"protect": "kings_shield"` | a Protect move (copy Protect as the donor) with a punishment for a contact move it stops: `kings_shield` (Attack -2), `spiky_shield` (1/8 HP), `baneful_bunker` (poison) |

More tests for `"when"`: `target_status:poison` / `user_status:burn` (`poison paralysis burn freeze sleep`), `terrain:electric` (`grassy electric psychic misty any`),
`target_grounded`, `user_grounded`, `hp_crossed_half`, `slower_than_target`, `target_first_turn`, and `{"move_in": ["Bite", "Crunch"]}` (the move is one of these;
a name the game does not have is left out).

### Changing the power or the accuracy

```json
"Hex": { "power_rules": [ { "when": "target_has_status", "mult": 2 } ] }
"Hurricane": { "accuracy_rules": [ { "when": "weather:rain", "accuracy": 255 }, { "when": "weather:sun", "accuracy": 50 } ] }
```

Every rule whose test is true applies, one after the other. `accuracy` is 1 to 100; 255 = never misses. Power rules go into the game's
own damage question, so **the trainer AI sees them too** when it picks a move.

### The tests (`"when"`)

One text or a list (all must be true). `"!"` in front turns it round (`"!contact"`).

`full_hp not_full_hp hp_below_half hp_above_half hp_below_third` (the user's), `target_full_hp target_hp_below_half`,
`status_move damaging_move contact physical special sound`, `type:fire` (the move's type), `user_type:ghost`, `target_type:grass`,
`weather:rain` (`sun rain sand hail none any`), `has_status no_status target_has_status target_no_status` (a status problem),
`user_has_item user_no_item target_has_item target_no_item`, `first_turn`, `move_has_secondary`, `super_effective not_very_effective`,
`move:Hex`, `field:name`.

## Abilities

```json
"Moxie": { "id": 153, "text": "Attack rises after a KO.", "ko": [ { "stat": "atk", "stages": 1 } ] }
```

- `"id"`: the ability's number, **124 to 255** (the game's own use 1 to 76; the Gen 4 pack 77 to 123). `0` or no `"id"`: the next free
  number from 124. A Pokémon has the ability when its row's `tokuseiDataId` holds that number.
- `"text"`: the line the summary shows. The name shown is the entry's name in capitals.

| Key | When it works |
|---|---|
| `switch_in` | the Pokémon comes in. `target: "foes"` reaches the foes (Intimidate) |
| `end_of_turn` | at the end of every turn, after the game's own checks (weather damage, Leftovers ...) |
| `ko` | its move knocked out the target |
| `contact` | a move that makes contact hit it (the owner is `user`, the attacker is `target`) |
| `switch_out` | it goes back (the heal has no picture) |
| `before_move` | its move is about to start (`set_type`: Protean) |
| `damage_dealt`, `damage_taken` | `{"when": ..., "mult": 1.3}` rules on the damage it deals / takes |
| `stat_mods` | `{"stat": "atk", "mult": 2}`: atk spa (its damage), def spd (damage it takes), spe (its speed). Attack and Defense are done as damage factors, not as the stat itself |
| `absorb` | `{"type": "Grass", "stat": "atk", "stages": 1}` or `{"type": "Water", "heal": 25}`: a damaging move of that type does nothing to it, and it gains |
| `acts_as` | `"Clear Body"`: for every check the **game itself** makes (Clear Body, Levitate, Static, Intimidate, Shed Skin ...) this ability counts as that one of the game's. This is the way to copy a game ability and add to it |
| `stat_drop_block` | stats foes cannot lower (`["all"]`, or `["atk"]`). Works for the **extra effects** of moves (the 10 percent Attack drop); a move or ability that lowers a stat for certain (Leer, Intimidate) is stopped by `"acts_as": "Clear Body"` instead |
| `status_block` | statuses foes cannot give it by a move's extra effect (the same limit) |
| `priority` | `{"when": "status_move", "add": 1}`: the moves it uses go first (Prankster) |
| `reflect_status` | `true`: a move the game lets Magic Coat reflect goes back at its user (Magic Bounce) |
| `remove_secondary` | `true`: its damaging moves lose their extra-effect rolls (Sheer Force; add a `damage_dealt` rule with `"move_has_secondary"` for the bonus) |
| `hit` | a damaging move hit it and it is still up (`when` can test the move: `type:dark`, `physical`, `hp_crossed_half`). The owner is `user`, the attacker is `target` |
| `hit_dealt` | its own damaging move hit (`when: contact`): Poison Touch |
| `fainted` | it was knocked out by a hit (Innards Out: `hurt_dealt`) |
| `stat_dropped` | a foe lowered one of its stats (Defiant, Competitive). It runs a moment later: at the next hit or the end of the turn |
| `any_faint` | another Pokémon on the field was knocked out by a hit (Soul-Heart: Sp. Atk +1). The owner is `user`. Not by poison, weather or recoil |
| `no_contact` | `true`: its own moves never make contact (Long Reach): the foe's `contact` effects (Mummy, Pickpocket ...) do not run after its hits. The game's own contact abilities (Rough Skin, Static) still do |
| `contrary` | `true`: stat changes it gets are turned round (only the ones that come through a move's extra effect or an ability) |
| `convert` | `[{"from": "Normal", "to": "Ice", "mult": 1.2, "when": "sound"}]`: its moves of that type become another type, and are stronger by `mult` (Aerilate family, Liquid Voice). `from` missing = any type. A move that becomes Fairy needs "Modern types" |
| `aura` | `{"type": "Dark", "mult": 1.33}`: every move of that type on the field is stronger while this Pokémon is out (Dark Aura); `breaks_auras: true` makes them weaker instead (Aura Break) |
| `immune_moves` | `["Shadow Ball", ...]`: these moves do nothing to it (Bulletproof) |
| `accuracy_against` | `[{"when": "status_move", "accuracy": 50}]`: foes' moves can be at most this accurate against it (Wonder Skin) |
| `accuracy_dealt` | `[{"mult": 1.1}]`: its own moves are this much more accurate (Victory Star) |
| `crit_when` | tests: its damaging moves are critical hits when they are true (Merciless: `target_status:poison`) |

## Terrains

`{"terrain": "electric"}` in a move or in `switch_in` of an ability starts a terrain for five turns (a new one replaces the old). Numbers of the 7th generation:
the matching move type of a Pokémon on the ground is x1.5 (Grassy: Grass, Electric: Electric, Psychic: Psychic); Misty halves Dragon moves against a Pokémon on the ground;
Grassy halves Earthquake / Magnitude / Bulldoze against it and gives every Pokémon on the ground 1/16 of its HP back at the end of the turn; Electric keeps a Pokémon
on the ground awake; Misty keeps status problems and confusion off it. Psychic Terrain does not stop priority moves yet. "On the ground" = not a Flying type and not Levitate.

The ability's effect is looked up when the game asks, so it works for a Pokémon whose ability a script changed (`battle.ability`), too.

## Rules for one battle

```json
"rule_sets": { "Level 40": { "level_cap": 40, "inverse": false, "weather": "rain", "banned_items": ["Leftovers"] } }
```

`level_cap` (Pokémon above it are lowered **for the battle**), `level_all`, `banned_items` (held items of these have no effect),
`inverse` (the type chart turned round: super effective and not very effective swap, immune becomes super effective), `weather`
(rain, sun, sand, hail when the first Pokémon comes out), `force_double` (accepted, **not built yet**).

A Pokémon whose level a rule changed gets no experience and no level-up from that battle; EVs, PP and status carry over.

A script starts a rule set for the next battle with `battle_rules("Level 40")`, for example from `on_battle_start`. It ends by itself
when the battle ends. `battle_rules()` ends it at once.

## What the trainer AI knows

New damaging moves are asked through the same damage question as every move, so the AI knows their power rules and the abilities'
damage factors. It does not know that Sap Sipper takes a Grass move, that Magic Bounce reflects a move, or what a status move's
blocks do (it uses its usual rules for a move with no power).

## Online battles and replays

The text of the `battle` files is part of what both PCs must have: the room refuses a PC with other definitions (the digest is in the
room fingerprint and in the recording's header). Blocks only use the game's own random numbers at the moment they run, in the order of
the blocks, so the same mods and the same choices give the same battle on both PCs and in a replay.

## Not built yet

`force_double`, the Luau route for effects (a script that returns blocks), field states that
change damage by themselves (`field` only starts the state; tests can read it), bag items banned by a rule set.

## See also

- [battle-scripts.md](battle-scripts.md): Luau scripts for battles
- [add-pokemon-moves-items.md](add-pokemon-moves-items.md): the move and ability rows these effects belong to
- [story-scripts.md](story-scripts.md): `battle_rules("name")` starts a rule set from a script
