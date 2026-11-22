# Translations and text

**What this page covers:** the four things people mean by "translation": playing the game in another language, rewriting the game's messages,
rewriting story dialogue, and translating the port's own screens.

| You want to | Use | Status |
|---|---|---|
| Play the game in German, French, Italian or Spanish | The language pack, made from the European disc | Available |
| Rewrite some of the game's messages (a fan translation, a joke text, new item descriptions) | A data mod with a `text` block | Available |
| Rewrite story or map dialogue | Replace the room's files, or story scripts | Experimental; see [story-scripts.md](story-scripts.md) |
| Correct a translation of the port's own screens (launcher and settings) | Send a fix for the repository's `locale` files | Available |

## 1. Other languages from the European disc

You do not make a mod for this. You need your own **European (PAL) disc**, game ID `GXXP01`, besides your USA disc.

1. In the launcher, open the **Extra assets** page.
2. In the card "Languages (from the European disc)", choose the PAL disc and press **Make the language pack**.
3. When it says "Ready: 4 languages", choose the **Language** on the launcher's play bar, or in the game's SETTINGS page.
4. The language takes effect the next time the game starts. The game offers **Restart now** and **Next start**.

The pack is made on your PC and lives in `<data folder>\extra_assets\languages`. It is never part of the port, and it
must never be shared. The USA disc stays the main disc: the game's code comes from it.

Names in a save are stored as text, so a save works in any language.

## 1b. Japanese from the Japanese disc

1. In the launcher, open **Extra assets** and use the card "Japanese (from the Japanese disc)": pick your Japanese Pokemon XD disc image
   (GXXJ01). The USA disc is still needed; the game's program is the USA one and only the Japanese texts, fonts and pictures are used.
2. Choose **日本語** as the language in Settings (system.language = jp).
3. Nothing from the Japanese disc is shipped with the port; the pack is made on your PC.

### What works
Title, memory-card messages, dialogue with furigana, battle, party, summary, bag, PDA, shops, PC, world map, save screen, name entry
(three sheets; R = dakuten / handakuten: keyboard W by default, the controller's right shoulder), the launcher in Japanese.

### What is not finished
- The ~1850 messages the Japanese game keeps in other places or does not have (GBA link, Colosseum save, rule sentences, EXIT, Language,
  the trainer classes...) now come from the Japanese game's own texts where it has them; 86 are translated by the port (machine-made, in
  `games/xd/tools/xd_languages_jp_translations.tsv`: improve them there and rebuild the pack). A few words stay English: BOX, PC, P*DA, HP, EXP,
  "Edit Rule 1-3".
- The 8-box name entry (box names) and the 10-box one (Colosseum rule names) keep their frames; hero and Pokemon names show five boxes.
- Pokemon in an old USA save keep their English names. The Japanese rooms' maps are not used (the walls differ).
- The Japanese launcher text is machine-made and needs a native speaker's check.
- The staff roll is the Japanese disc's own movie (Latin letters, the Japanese staff list). Bingo and Battle Disk were not reached. Linux: see docs/LANGUAGES.md.

## 2. Change messages with a data mod

Put a `text` block in a data file:

```json
{
  "text": {
    "#10200": "Sharply raises{00}the ATTACK stat."
  }
}
```

Mod folder:

```
my-text/
  mod.json
  data/
    text.json
```

`mod.json`:

```json
{
  "id": "my-text",
  "name": "My text",
  "version": "1.0",
  "author": "Your name",
  "description": "Rewrites one message.",
  "game": "GXXE01",
  "applies": "restart"
}
```

Steps:

1. Find the message number with `xd_data_mods <disc> --text <from> [to]`. It prints messages by number, in the form a
   `text` block takes. (The number `10200` above is only an example.)
2. Write your text. `{00}` is a line break. Other control codes: `{XX}` or `{XX aa bb}` as printed by `--text`. `{{` is
   a literal `{`.
3. Keep each table's total length: all new texts in one table together may not be longer than the old ones (2 bytes a
   character). Shorten if the game refuses.
4. Switch the mod on. It takes effect at the next start.

What can change: the game's common message tables and the menus' own tables (item descriptions, menu words).
Map and story dialogue cannot be changed this way.

Texts of rows you add (names, descriptions) go in the `text` key of the row. See
[add-pokemon-moves-items.md](add-pokemon-moves-items.md).

## 2b. Words for several languages in one mod

A row a mod adds (a new move, item or Pokémon) can carry its words for every language of the game. In the `text` block add the
language to the field name: `"name.sp"`, `"doc.fr"`, `"typeName.ge"` (codes: `sp` Spanish, `fr` French, `ge` German, `it` Italian,
`jp` Japanese). The game picks the words of the language it runs in; when a language has none, the plain `name` / `doc` show.

```json
"text": { "name": "Zap Test", "doc": "A test move.",
          "name.sp": "ZAP DE PRUEBA", "doc.sp": "Un movimiento de prueba." }
```

Write names the way the game writes them in that language (capitals; French has no accents on capitals; German writes ß as SS)
and keep them short: a move name must fit the battle menu (about 15 letters in the European fonts). The Gen 4 pack ships its names and
short descriptions in all five languages this way; its descriptions are machine-made and a native speaker's corrections are welcome
(`tools/pbr_convert/gen4_text_i18n.tsv`).

Sizes to keep in mind (on screen, 640 x 480): a bag list row holds about 240 px of name (the count "X 1" sits at its end), a bag description
box three lines of about 225 px, the summary's move description two lines of about 325 px, an ability name about 180 px and an ability
description one line of about 350 px. A text that is too wide runs over the frame or the count. In the European languages that is roughly
12 to 14 letters for a name and 25 to 30 for an ability line; Japanese lines hold about 9 (bag) to 19 (ability) full-width characters.
XD has no Pokedex entry text, so a Pokémon row needs only its name and its category word (the game adds "POKéMON" itself).

## 3. Mods in another language

Mods can have any text in their `name` and `description`. Disc file names inside a mod must be plain English letters,
digits and a few signs; the game warns if not.

A translation made as a data mod replaces text for every player who uses it, in the language the game runs in. If you
mix it with the language pack, remember a message number means the same message in every language, but lengths differ.
Test in the language you target.

## 4. Translating the port's own screens

The launcher and the port's own menus have their own texts (in English, with German, French, Italian and Spanish).
They are plain text files named `fr.txt`, `de.txt`, `it.txt` and `es.txt` in the repository's `locale` folder. The
translations were made by machine, and corrections are welcome. Change a line, keep the `%d`/`%s` marks in the same
order, and send it in as a change to the repository. The game's own text is never in those files.

## Test it

- After making the language pack, check the title screen, a battle and the field menu in each language.
- For a text mod: `xd_data_mods <disc> --mods` should say "N message text(s) rewritten".

## Common mistakes

- Asking the USA disc for another language. Use the European disc.
- A `text` that is too long.
- Writing a line break as a real new line. Use `{00}`.
- Sharing the language pack. Never do that.

## See also

- [change-data.md](change-data.md): the `text` block
- [add-pokemon-moves-items.md](add-pokemon-moves-items.md): text for new rows
