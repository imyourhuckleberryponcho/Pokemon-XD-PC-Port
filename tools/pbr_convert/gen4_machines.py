"""Extra machines (docs/GEN4.md "Extra machines (TMs)"): the Gen 4 TMs the XD disc has no machine for.

Part of the Gen 4 pack (gen4_pack.py calls `build()`; the C++ pack builder's pbr_pack_machines.cpp is the same thing, byte for byte).
A GENERAL mechanism: a list of extra machines (tm number, move), item rows for them, and per species which of them it can learn. The
Gen 4 set is the first user (TM51..TM92 of Diamond/Pearl except the ones XD already has a machine for); a later "modern types" set
can hand the same pieces another list. Nothing here is game data: the item rows are copies of XD's own TM01 row with our own names,
coupon prices and texts; the species bits are Battle Revolution's TM bits for the moves in question, kept as 0/1 text.

What `build()` makes:
  items         [(name, row)] for gen4_items.files(extra_items=...): kind 4 (a machine), buff = the machine number (58 up: XD's own
                machines are 0..57, 50..57 being the HMs)
  coupon        the item names the Poke Coupon exchange desks sell after the evolution items (gen4_items.files(extra_coupon=...))
  machines.txt  the pack file the game reads (games/xd/src/gen4_machines.cpp):
                  format 1
                  machine <machine number> <TM number> <move: a number (XD's own move) or a name (a Gen 4 move the pack adds)>
                  bits <species: #<XD number> or the pack's species key> <one 0/1 per machine, in the order of the machine lines>
                Species with no bit set are left out. New species first (the pack's order), then XD's own by number.

Machines for the two Gen 4 HMs that XD lacks (Defog, Rock Climb) are NOT made: an HM here would need the HM rule (never used up,
cannot be held), which is a different piece of the game.
"""
import json
import re

import gen4_move_text as MT
import gen4_text_i18n as TI

FIRST_MACHINE = 58
FIRST_EXTRA_BIT = 50
LAST_EXTRA_BIT = 91
GEN4_MOVE_SHIFT = 20
PRICE_COPY = 'TM01'




COUPON = {
    'Stone Edge': 4500, 'Focus Blast': 4500, 'Energy Ball': 4000, 'Dragon Pulse': 4000, 'Dark Pulse': 4000, 'Flash Cannon': 4000,
    'Swords Dance': 4000, 'Trick Room': 4000, 'X-Scissor': 3500, 'Poison Jab': 3500, 'Shadow Claw': 3500, 'Drain Punch': 3500,
    'Rock Slide': 3500, 'Giga Impact': 3500, 'Explosion': 3500, 'U-turn': 3500, 'Roost': 3500, 'Stealth Rock': 3500,
    'Gyro Ball': 3000, 'Avalanche': 3000, 'Grass Knot': 3000, 'Charge Beam': 3000, 'Payback': 3000, 'Brine': 3000,
    'Silver Wind': 3000, 'Rock Polish': 3000, 'Substitute': 3000, 'Thunder Wave': 2500, 'Will-O-Wisp': 2500, 'Sleep Talk': 2500,
    'Dream Eater': 2500, 'Swagger': 2500, 'Psych Up': 2000, 'Recycle': 2000, 'Natural Gift': 2000, 'False Swipe': 2000,
    'Pluck': 2000, 'Endure': 1500, 'Embargo': 1500, 'Captivate': 1500, 'Fling': 1500,
}


def machine_list(tmlist, xd_tm_moves, move_name):
    """[(machine number, TM number, XD move id, move name)]: Battle Revolution's TM51..92 whose move XD has no machine for.
    tmlist = the 100 machine moves of Battle Revolution (index = bit), xd_tm_moves = XD's 58 machine moves (XD ids),
    move_name(Battle Revolution's move id) = its name."""
    have = set(xd_tm_moves)
    out = []
    for k in range(FIRST_EXTRA_BIT, LAST_EXTRA_BIT + 1):
        pbr = tmlist[k]
        xd = pbr if pbr <= 354 else pbr + GEN4_MOVE_SHIFT
        if xd in have:
            continue
        out.append((FIRST_MACHINE + len(out), k + 1, xd, move_name(pbr), k))
    return out


def move_ref(m):
    """What a machine line says for the move: XD's own move = its number, a Gen 4 move = its name (spaces as _)."""
    return str(m[2]) if m[2] <= 354 else m[3].replace(' ', '_')


def build(tmlist, xd_tm_moves, move_name, new_species, existing, tm_bits):
    """new_species = [(species key, national number)] in the pack's order; existing = [(XD number, national number)] ascending;
    tm_bits(national number) = the set of Battle Revolution machine bits of that species.
    Returns (items, coupon names, machines.txt text)."""
    mach = machine_list(tmlist, xd_tm_moves, move_name)
    items, coupon = [], []
    for no, tm, xd, name, k in mach:
        item = 'TM%02d' % tm
        price = COUPON[name]
        row = {'copy': PRICE_COPY, 'price': 3000 if price >= 3000 else 2000, 'coupon': price, 'buff': no}
        row['text'] = TI.machine_text(item, tm, name)
        items.append((item, row))
        coupon.append(item)
    lines = ['format 1']
    for m in mach:
        lines.append('machine %d %d %s' % (m[0], m[1], move_ref(m)))
    def bits(dex):
        have = tm_bits(dex)
        return ''.join('1' if m[4] in have else '0' for m in mach)
    for key, dex in new_species:
        b = bits(dex)
        if '1' in b:
            lines.append('bits %s %s' % (key.replace(' ', '_'), b))
    for sid, dex in existing:
        b = bits(dex)
        if '1' in b:
            lines.append('bits #%d %s' % (sid, b))
    return items, coupon, '\n'.join(lines) + '\n'


def from_discs(pbr, x, allj):
    """build() with the player's discs: pbr = gen4_data.PBR, x = gen4_data.XD, allj = the pack's 107 species (their 'extra' blocks)."""
    new = [(s['extra']['_key'], s['extra']['dex']) for s in allj if s['extra']['xd_species_id'] >= 415]
    existing = sorted((sid, dex) for dex, sid in x.nat2xd.items())
    return build(pbr.tmlist, x.tm_moves, pbr.move_name, new, existing, lambda dex: set(pbr.personal(dex)['tm']))
