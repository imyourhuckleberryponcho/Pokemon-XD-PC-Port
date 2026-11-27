# Randomizer guide

The randomizer makes a new game out of your own disc: other Pokemon in the Poke Spots, other Pokemon for the trainers,
other Shadow Pokemon to snag, other items in the boxes on the ground, and more. This guide says what every option
does, with examples, and answers the questions players ask most.

You find the options on the New Game screen (choose NEW GAME, the setup screen comes first). The Randomizer row is
there; under it you see one row per option. Select a row and the box at the bottom explains it. It also explains the
choice you have picked right now.

Randomizer needs the Enhanced preset. In Original it is not available.

## The quick version

- Pick a style (Light, Classic, Chaos or Custom) and start. That is all you need.
- Want to share your game? Give a friend your code. Same code, same game.
- Something looks odd (an Eevee with Poison Sting)? Read "Common confusions" at the end first.
- You cannot change the randomizer in a running save. The world is made when you start a New Game.

## Styles

A style is a ready-made set of options. Every option can still be changed afterwards.

- **Light**: only the wild Pokemon at the Poke Spots, which Pokemon are Shadow Pokemon, and the item boxes on the
  ground (shuffled). Everything else stays as on the disc. Good for a first try.
- **Classic**: Light, plus other Pokemon in the story trainers' teams, other abilities, other Pokemon able to learn
  each TM, and other moves on the TMs. The fairness options are on: a replacement is about as strong as the
  Pokemon it replaces, a team never has the same Pokemon twice.
- **Chaos**: the same as Classic with the fairness off. Strength is ignored (a trainer's early Pokemon can be a
  Dragonite), legendaries can show up anywhere, ground items are random.
- **Custom**: you set every option yourself. It starts from the options as they were, so pick Classic first and
  switch Custom on if you want "Classic, but...". A new Custom setup starts with everything off, except that
  trainers' moves are set to "By level".

The styles do not touch the shops, the tutor's moves, the starter, any levels, or Gen 4 / Gen 5-7 Pokemon. Those
options exist in every style and start off; you can turn them on in the rows below the style.

## Codes and seeds

Every randomized game has a code, for example `XDR8-4M7Q-...`. The code holds the seed (the dice) and every option you
chose. Two things follow:

- **Same code = same game.** A friend who types your code gets the same Pokemon in the same places, the same teams,
  the same items.
- **A new seed = a new game.** On the Randomizer code row, press left, right or A for a new random seed. The options stay,
  but everything is rolled again.

To share a code: on the launcher's Save page, choose "Copy code". Your friend copies it on their PC, then on the New Game
screen chooses "Use a friend's code" (press A). The code replaces every option above it with yours.

A save keeps its code for good. When you press CONTINUE the same world is made again from the code that is
kept next to the save. You cannot change a running save's options (the world was made when you started it).

"Show spoilers" on the launcher's Save page lists what the randomizer changed and where. It asks first, because it
spoils everything.

## The options

### Poke Spot Pokemon

The wild Pokemon at the three Poke Spots, the ones you lure with Poke Snacks.

- **Poke Spot Pokemon**: On changes them. Munchlax (the Herb Shop's) and Bonsly (the one that runs away) never
  change: the game treats them in its own way.
- **Similar strength**: On means a weak Pokemon becomes another weak one and a strong one a strong one (about the same
  total base stats), so a Sandshrew will not turn into a Dragonite. Off means anything can replace anything.
- **Scope** decides whether the same Pokemon is swapped the same way everywhere.
  - *Each spot its own* (default): every Poke Spot rolls its own new Pokemon. Sandshrew lives at both the Rock and the Oasis spot? It might become Larvitar at the Rock and Ralts at the Oasis. More variety.
  - *Same swap everywhere*: the game makes one swap list for the whole game. If Sandshrew becomes Larvitar, it is Larvitar at every spot where Sandshrew was. Two different Pokemon never turn into the same one. The world feels more consistent: once you know what replaced a Pokemon, you know it everywhere.
  - Munchlax and Bonsly, the special visitors, never change either way.
- **Type theme**: *Similar types* makes the new Pokemon share a type with the old one, so a rocky spot keeps a rocky
  feel. Off allows any type.
- **Poke Spot levels**: Kept, or +10%, +20%, +30%, +50% higher (never above level 100). Example: with +10%, a level 20 Pokemon is level 22.

### Trainers' Pokemon

The Pokemon in the story trainers' teams. The Shadow Pokemon in a team are handled by the Shadow row, not here.
A team always keeps its size.

- **Trainers' Pokemon**: On changes the Pokemon.
- **Similar strength**: On keeps the difficulty curve: early trainers stay easy, late ones stay hard.
- **Moves**: this one confuses people, so read it twice.
  - *Kept*: a trainer's new Pokemon uses the **old** Pokemon's moves. Example: a trainer's Cacnea is replaced by an
    Eevee, and the Eevee still knows Poison Sting. That is how this choice works, it is not a bug. Pick this when you
    want the trainer's old fights to feel the same.
  - *By level*: the new Pokemon knows the moves its own species learns by that level, like a wild Pokemon of that
    level. Most players want this one.
  (Up to version 0.7.3 a new Custom setup started on Kept; since 0.7.4 it starts on By level. An old code keeps what
  it was made with.)
- **No repeats**: On means a team never gets the same Pokemon twice.
- **Trainers' levels** (example: +30% turns a level 40 Pokemon into level 52): Kept; +10%, +20%, +30%, +50% higher; or *To strength*: a new Pokemon that is stronger than the
  old one gets a lower level and a weaker one a higher level, so the fight stays about as hard. Shadow Pokemon keep their levels.
- **Trainers' held items**: Kept, Random (each Pokemon holds an item trainers' Pokemon hold in the game) or None.
- **Trainers' natures**: Kept, or Random (any of the 25 natures).

### Shadow Pokemon

- **Shadow Pokemon**: On changes which species are the Shadow Pokemon. Each species is a Shadow Pokemon only once. The
  place and the moment where you meet each one stay. The P*DA's Shadow list follows the change. A purified Shadow
  Pokemon knows the moves its new species learns.
- **Similar strength**: On picks a replacement about as strong as the old one.
- **Keep Shadow Lugia**: On (recommended) keeps Shadow Lugia as Lugia, because the story, the scenes and the final
  battles show Lugia.

How many Shadow Pokemon there are is decided by the game; this option only changes which species they are. **More
Shadow Pokemon** (a separate row on the New Game screen) adds Shadow Pokemon to trainers that have none, in
percentages. It is not part of the randomizer code, so the code alone does not copy it.

### Starter

- **Starter Pokemon**: On gives you another Pokemon instead of Eevee at level 10. The film scenes and the Eevee
  evolution offer still say Eevee, because they are pre-made.
- **Starter choice**: *Random* picks one for you. *Pick 1 of 3* lets you choose from three when the game would give you
  Eevee. *Eevee family* gives Eevee or one of its evolutions.
- **Similar to Eevee**: On picks a Pokemon with about Eevee's total base stats (never a legendary). Off allows any
  Pokemon the Legendaries row allows. It does not apply to Eevee family.
- **Starter's moves**: *By level* gives the last four moves the starter learns up to level 10. *Kept* gives Eevee's four
  (Tackle, Tail Whip, Bite, Sand-Attack) whoever the starter is.

### Items

- **Items on the ground**: the item boxes lying on the maps. Important items (the story's and Battle CDs) never move.
  - *Off*: as the game.
  - *Random*: every box gets any item. You may find many of one kind and none of another.
  - *Shuffle*: the boxes swap their items. The same items exist, just in other places. Safe and fair.
- **Named items only** (the full name is "Items on the ground (random): only items that have a name"): for Random only. On means no "????" items. Off is how
  codes from before 0.7.2 worked. Leave it On unless you play an old code.
- **Poke Mart stock**: *Off*; *Shuffled* (the Marts' own goods turn up in other Marts); *Everyday goods* (medicine,
  status cures, repels, battle items, berries); *Anything* (also held items, TMs, vitamins, stones, valuables). Poke
  Balls and Poke Snacks always stay where they are.

### TMs, tutor and abilities

- **TM moves**: each TM teaches another move, all different. The bag and the shops show the new move and its
  description. *Random, no one-hit-KO* leaves out Fissure, Guillotine, Horn Drill and Sheer Cold. The names (TM01...) stay.
- **TM learners**: which Pokemon can learn each TM changes. Each TM stays about as common as on the disc.
- **Tutor moves**: the moves the move tutor teaches. All different, none that a TM teaches. The Pokemon that could
  learn the old move can learn the new one.
- **Abilities**: Pokemon get other abilities, so a Pokemon can surprise you in battle. Wonder Guard and Forecast are
  never handed out.
- **Same in a family**: a Pokemon keeps its ability when it evolves.

### Legendaries

Whether the options above may hand out legendary Pokemon.

- *Never*: no legendary is handed out. The legendaries the game already had stay where they were.
- *As on the disc*: a place that had a legendary (for example a legendary Shadow Pokemon) can only get another
  legendary; nobody else gets one.
- *Any*: legendaries can appear as Poke Spot Pokemon, in trainers' teams and as Shadow Pokemon.

### Gen 4 and Gen 5-7 Pokemon

- **Include Gen 4 Pokemon**: the 107 Gen 4 Pokemon (Turtwig, Lucario, Garchomp...) can be handed out too. They come from
  your own Pokemon Battle Revolution disc (launcher, Extra assets page).
- **Include Gen 5-7 Pokemon**: the 314 Gen 5-7 Pokemon too. Needs the Gen 4 option and your own Ultra Sun / Ultra Moon
  dump (Extra assets page).

## Common confusions

- **"A trainer's Pokemon has the wrong moves, is it a bug?"** If Trainers: Moves is *Kept*, no: the new Pokemon uses
  the old one's moves (an Eevee with Poison Sting). Switch that option to *By level* for a new game.
- **"I made a Custom code in an old version and it behaves differently."** A code always gives the same game it gave
  when it was made, also in later versions. Newer versions start fresh Custom setups on other defaults, but a code
  you already have is read exactly as it was.
- **"Munchlax and Bonsly never change."** Right. They never change, whatever the options say.
- **"Why is the Shadow Pokemon I snagged not the one on the P*DA?"** It should be: the list follows the change. If it
  is not, send us a bug report (F10 in the game).
- **"The story still shows Eevee / Lugia."** Pre-made film scenes cannot be changed.
- **"My friend's game is different from mine."** Check you both have the same code, the same options in More Shadow
  Pokemon, and the same Gen 4 / Gen 5-7 choices (those need the packs).
- **"Can I change the options in my running save?"** No. Start a new game.
- **"I want a harder game."** Try Chaos, or Classic with Trainers' levels +30% and Trainers' Pokemon on.
