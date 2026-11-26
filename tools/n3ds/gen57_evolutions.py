"""Gen 5-7 evolutions that are not a plain level (0.7.5, docs/GEN57.md "Evolutions"): stones, trade, held items, conditions.

XD's species record knows level / friendship / trade (docs/GEN4.md 10.1). Before 0.7.5 every other Ultra Sun / Moon method became a LEVEL (gen57_all.EVO_NOTES).
Now the ones below keep their meaning. The record carries only what XD's own check can do (Karrablast and Shelmet: kind 5, trade); the rest is a PC-only list in the
pack, `evolutions.txt` (the format of the Gen 4 pack's, src/gen4_pack.cpp / gen57_pack.cpp read it):

    format 1
    evo <species> <kind> <item | level | move> <target>      kind 7 use an item, 6 trade holding it, 20 level + a Dark Pokemon in the party,
                                                             21 level + female, 22 level up knowing the move (#<row>; a 5th word = the level the
                                                             dump's learn-set gives that move: the game uses it when it has no such move, XG's Stomp)
    shop <item>                                              what the prize desk sells for these evolutions (a name the pack adds, or #<number> of the disc's)

Items are written as `#<number>` for the disc's own rows (Sun 93, Moon 94, Fire 95, Thunder 96, Water 97, Leaf 98: the same numbers in XD and XG) and by name for
the rows the packs add (Shiny / Dusk / Ice Stone: the Gen 4 pack's; Sachet and Whipped Dream: the data files this module writes).
The Python is the source; tools/n3ds/cpp_ref/export_tables.py writes the tables for the C++ converter (engine/formats/n3ds/usum_pack.cpp).
"""
import json
import os

COUPON = 6000
CLASSIC = {'Sun Stone': 93, 'Moon Stone': 94, 'Fire Stone': 95, 'Thunder Stone': 96, 'Water Stone': 97, 'Leaf Stone': 98}
SHOP_ORDER = ['Fire Stone', 'Thunder Stone', 'Water Stone', 'Leaf Stone', 'Moon Stone', 'Sun Stone']



RULES = [
    ('Pansage', 'Simisage', 'item', 'Leaf Stone'),
    ('Pansear', 'Simisear', 'item', 'Fire Stone'),
    ('Panpour', 'Simipour', 'item', 'Water Stone'),
    ('Munna', 'Musharna', 'item', 'Moon Stone'),
    ('Cottonee', 'Whimsicott', 'item', 'Sun Stone'),
    ('Petilil', 'Lilligant', 'item', 'Sun Stone'),
    ('Helioptile', 'Heliolisk', 'item', 'Sun Stone'),
    ('Minccino', 'Cinccino', 'item', 'Shiny Stone'),
    ('Floette', 'Florges', 'item', 'Shiny Stone'),
    ('Lampent', 'Chandelure', 'item', 'Dusk Stone'),
    ('Doublade', 'Aegislash', 'item', 'Dusk Stone'),
    ('Eelektrik', 'Eelektross', 'item', 'Thunder Stone'),
    ('Charjabug', 'Vikavolt', 'item', 'Thunder Stone'),
    ('Crabrawler', 'Crabominable', 'item', 'Ice Stone'),
    ('Cosmoem', 'Solgaleo', 'item', 'Sun Stone'),
    ('Cosmoem', 'Lunala', 'item', 'Moon Stone'),
    ('Karrablast', 'Escavalier', 'trade', ''),
    ('Shelmet', 'Accelgor', 'trade', ''),
    ('Spritzee', 'Aromatisse', 'held', 'Sachet'),
    ('Swirlix', 'Slurpuff', 'held', 'Whipped Dream'),
    ('Pancham', 'Pangoro', 'level_dark', ''),
    ('Salandit', 'Salazzle', 'level_female', ''),
    ('Steenee', 'Tsareena', 'knows_move', ''),
    ('Poipole', 'Naganadel', 'knows_move', ''),
]
HOW_TEXT = {'trade': 'trade (the game\'s own kind 5; the Linking Cord does it too)', 'level_dark': 'level up with a Dark Pokemon in the party',
            'level_female': 'level up, female only', 'knows_move': 'level up knowing the move'}


ITEMS = {'Sachet': 647, 'Whipped Dream': 646}
ITEM_DOC = {
    'us': 'Loved by a certain{00}Pokémon. Helps it{00}evolve.',
    'ge': 'Beliebt bei einem{00}bestimmten Pokémon.{00}Hilft Entwicklung.',
    'fr': "Aimé d'un certain{00}Pokémon. Aide à{00}évoluer.",
    'it': "Piace a un Pokémon{00}in particolare.{00}Aiuta l'evoluzione.",
    'sp': 'Gusta a ciertos{00}Pokémon. Ayuda a{00}evolucionar.',
    'jp': 'あるポケモンが{00}このんでいる。{00}しんかに　つかう。',
}
FOLDERS = {'data': 'us', 'data_fr': 'fr', 'data_ge': 'ge', 'data_it': 'it', 'data_sp': 'sp', 'data_jp': 'jp'}
DUMP_LANG = {'us': 'en', 'ge': 'de', 'fr': 'fr', 'it': 'it', 'sp': 'es', 'jp': 'ja'}
STONE_FILE = '07_stone_coupons.json'
ITEMS_FILE = '06_evolution_items.json'


def rule(source, target):
    for r in RULES:
        if r[0] == source and r[1] == target:
            return r
    return None


def item_ref(name):
    """The evolutions.txt spelling of an item: #<number> for the disc's own, else the row's name with _ for spaces."""
    return '#%d' % CLASSIC[name] if name in CLASSIC else name.replace(' ', '_')


def move_row(usum_id):
    """The XD row of a USUM move (the same numbers gen57_all.move_row gives): Gen 1-3 as they are, the later ones +20."""
    if 1 <= usum_id <= 354:
        return usum_id
    return usum_id + 20


def evolution_lines(u, nats):
    """evolutions.txt: one `evo` line per rule that applies to the species of `nats` (in the dump's order), then the `shop` lines."""
    out = ['format 1']
    for nat in nats:
        sname = u.species_name(nat)
        for e in u.evolutions(nat):
            tname = u.species_name(e.species)
            r = rule(sname, tname)
            if r is None or r[2] == 'trade':
                continue
            s, t = sname.replace(' ', '_'), tname.replace(' ', '_')
            how = r[2]
            if how == 'item':
                out.append('evo %s 7 %s %s' % (s, item_ref(r[3]), t))
            elif how == 'held':
                out.append('evo %s 6 %s %s' % (s, item_ref(r[3]), t))
                out.append('evo %s 7 %s %s' % (s, item_ref(r[3]), t))
            elif how == 'level_dark':
                out.append('evo %s 20 %d %s' % (s, e.level, t))
            elif how == 'level_female':
                out.append('evo %s 21 %d %s' % (s, e.level, t))
            elif how == 'knows_move':
                lvl = next((lv for lv, mv in u.levelup(nat) if mv == e.arg), 1)
                out.append('evo %s 22 #%d %s %d' % (s, move_row(e.arg), t, max(lvl, 1)))
    for name in SHOP_ORDER + list(ITEMS):
        out.append('shop ' + item_ref(name))
    return out


def side_targets(u, nat):
    """The species names a species evolves into by the pack's list (for the randomizer's evolution line), in the dump's order."""
    sname = u.species_name(nat)
    res = []
    for e in u.evolutions(nat):
        tname = u.species_name(e.species)
        r = rule(sname, tname)
        if r is not None and r[2] != 'trade':
            res.append(tname)
    return res


def note(sname, tname):
    """The report note of a rule's evolution, or None when the species has none."""
    r = rule(sname, tname)
    if r is None:
        return None
    if r[2] == 'item':
        return 'evolution to %s: use the %s (evolutions.txt)' % (tname, r[3])
    if r[2] == 'held':
        return 'evolution to %s: use the %s, or trade holding it (evolutions.txt)' % (tname, r[3])
    return 'evolution to %s: %s%s' % (tname, HOW_TEXT[r[2]], '' if r[2] == 'trade' else ' (evolutions.txt)')


def write_items(u, pack, shown):
    """The data files of the new items (Sachet, Whipped Dream: a copy of the Thunder Stone, so it is used like a stone) and of the classic stones' coupon prices (XD's
    Poke Coupon desk did not sell them), in every language folder the pack has."""
    n = 0
    for folder, lg in FOLDERS.items():
        d = os.path.join(pack, folder)
        if not os.path.isdir(d):
            continue
        items = {}
        for name, num in ITEMS.items():
            items[name] = {'copy': 'Thunder Stone', 'text': {'name': shown(u.item_name(num, DUMP_LANG[lg])), 'doc': ITEM_DOC[lg]}, 'coupon': COUPON}
        json.dump({'_note': 'made by gen57_evolutions.py: the Sachet and the Whipped Dream (items that evolve Spritzee and Swirlix; 0.7.5; placeholder price)', 'add': {'items': items}},
                  open(os.path.join(d, ITEMS_FILE), 'w', encoding='utf-8'), indent=1, ensure_ascii=False)
        json.dump({'_note': 'made by gen57_evolutions.py: the six classic stones get a Poke Coupon price, so the prize desk can sell them (XD only: XG sells them itself; 0.7.5; placeholder price)',
                   'items': {'#%d' % num: {'coupon': COUPON} for num in CLASSIC.values()}},
                  open(os.path.join(d, STONE_FILE), 'w', encoding='utf-8'), indent=1, ensure_ascii=False)
        n += 1
    return n
