"""The Gen 4 items (docs/GEN4.md "Items"): thirteen new items, where they are sold, and what they do.

Part of the Gen 4 pack (gen4_pack.py calls `files()`; the C++ pack builder's pbr_pack_items.cpp is the same thing, byte for byte).
Nothing here is game data: the item rows are copies of rows the player's XD disc has (`"copy"` in the add-rows format of
docs/MODDING.md) with our own names, descriptions, prices and coupon values; the shops' lists are the disc's own lists plus the new
items, read from the disc by the caller.

What `files()` makes:
  data/00_items.json   the "add" block (items 594 up: the order of ITEMS) and the shops' new first entries (shopData.goodsIdx)
  `goods` lines        for the pack's tables.txt: the rows appended to shopGoods (the table cannot be made longer by a data mod; the
                       pack's extra rows do it, games/xd/src/gen4_pack.cpp turns the names into item numbers once the game knows them).
                       `goodscopy <row>` = a copy of that row of the table as it is when the game starts (a mod's changes to the
                       list stay, the new items come after it); `goods <number or name>` = that item; `goods 0` ends a list

How an item behaves is in the row it copies (docs/GEN4.md "Items"):
  stones, Linking Cord   a copy of the Thunder Stone: pocket "Items", item_param row 51 = the evolution flag (itemUse2Pokemon.cpp:
                         the stone is used on a Pokemon -> pokemonEvolutionCheck mode 1 with the item number)
  Razor Claw             a copy of the Scope Lens: the same held-effect row (item_soubi_data 41: a higher critical-hit stage)
  Razor Fang             a copy of the King's Rock: the same held-effect row (30: a flinch chance)
  the six others         a copy of a plain held item (the Red Scarf): held-effect row 0 = nothing when held, as in Gen 4
"""
import json

import gen4_text_i18n as TI
import os
import re
import subprocess

FIRST_NEW_ITEM = 594
SHOP_GOODS_COUNT = 184
COUPON_SHOPS = (9, 10, 11)




HELD = "A held item for{00}evolution. It does{00}nothing in battle."
ITEMS = [
    ("Ice Stone", "Thunder Stone", 2100, 6000, "A frosty stone that{00}evolves certain{00}Pokémon when used."),
    ("Dusk Stone", "Thunder Stone", 2100, 6000, "A dark stone that{00}evolves certain{00}Pokémon when used."),
    ("Dawn Stone", "Thunder Stone", 2100, 6000, "A bright stone that{00}evolves certain{00}Pokémon when used."),
    ("Shiny Stone", "Thunder Stone", 2100, 6000, "A shiny stone that{00}evolves certain{00}Pokémon when used."),
    ("Razor Claw", "Scope Lens", 100, 8000, "Raises the holder's{00}critical-hit stage.{00}Helps evolution."),
    ("Razor Fang", "King's Rock", 100, 8000, "Held: may make the{00}target flinch.{00}Helps evolution."),
    ("Protector", "Red Scarf", 100, 10000, HELD),
    ("Electirizer", "Red Scarf", 100, 10000, HELD),
    ("Magmarizer", "Red Scarf", 100, 10000, HELD),
    ("Reaper Cloth", "Red Scarf", 100, 10000, HELD),
    ("Dubious Disc", "Red Scarf", 100, 10000, HELD),
    ("Oval Stone", "Red Scarf", 100, 6000, HELD),
    ("Linking Cord", "Thunder Stone", 3000, 8000, "Acts as a trade for{00}evolution. Use on a{00}Pokémon."),
]


COUPON_EXTRA = ["Razor Claw", "Razor Fang", "Protector", "Electirizer", "Magmarizer", "Reaper Cloth", "Dubious Disc", "Oval Stone",
                "Ice Stone", "Dusk Stone", "Dawn Stone", "Shiny Stone", "Linking Cord"]


def key(name):
    return name.replace(' ', '_')


def files(first_of, goods_at, extra_items=(), extra_coupon=()):
    """first_of(shop) = shopData[shop].goodsIdx; goods_at(i) = shopGoods[i].value (the disc's own). Returns (json text, goods lines).
    extra_items = [(name, row dict)] added after ITEMS and extra_coupon = [name] added to the exchange desks' list after COUPON_EXTRA:
    the machines (gen4_machines.py; docs/GEN4.md "Extra machines") use these, so the pack has ONE list per desk."""
    items = {}
    for name, copy, price, coupon, doc in ITEMS:
        items[name] = {'copy': copy, 'price': price, 'coupon': coupon, 'text': TI.item_text(name, doc)}
    for name, row in extra_items:
        items[name] = row
    shop_edit = {}
    lines = []
    cursor = SHOP_GOODS_COUNT

    def new_list(shops, extra):
        nonlocal cursor
        firsts = {first_of(s) for s in shops}
        assert len(firsts) == 1, 'shops %s do not share a list' % (shops,)
        first = firsts.pop()
        orig = []
        while goods_at(first + len(orig)) != 0:
            orig.append(first + len(orig))
        start = cursor
        for row in orig:
            lines.append('goodscopy %d' % row)
        for n in extra:
            lines.append('goods %s' % key(n))
        lines.append('goods 0')
        cursor += len(orig) + len(extra) + 1
        for s in shops:
            shop_edit['#%d' % s] = {'goodsIdx': start}

    new_list(COUPON_SHOPS, COUPON_EXTRA + list(extra_coupon))
    doc = {'_note': 'Gen 4 items: copies of rows of the disc with our own names, prices and texts; the shops list them (tables.txt goods lines)',
           'add': {'items': items}, 'shopData': shop_edit}
    return json.dumps(doc, indent=1), lines


def disc_reader(exe, disc, settings_dir):
    """(first_of, goods_at) read from the XD disc with xd_data_mods (`--show shopData #n`, `--show shopGoods #i`), each answer kept."""
    env = dict(os.environ, XD_SETTINGS_DIR=settings_dir)
    os.makedirs(settings_dir, exist_ok=True)
    memo = {}

    def show(table, i, field):
        if (table, i) not in memo:
            memo[(table, i)] = subprocess.run([exe, disc, '--show', table, '#%d' % i], capture_output=True, text=True, env=env,
                                              check=True).stdout
        m = re.search(r'^\s*%s\s+(\d+)' % field, memo[(table, i)], re.M)
        return int(m.group(1))
    return (lambda shop: show('shopData', shop, 'goodsIdx')), (lambda i: show('shopGoods', i, 'value'))
