"""The Gen 5-7 abilities (USUM ids 124..232) as battle effects for the Gen 5-7 pack (docs/GEN57_MOVES.md).

  defs, report = build_all(u)    -> battle/abilities.json's "abilities" part, and one report line per ability

The numbers are the games' own ability numbers (the pack's species rows use them); what each does is written here as the building blocks
of docs/modding/battle-effects.md (events, damage factors, absorb, acts_as, conversion, auras...). The mechanics are public facts (Bulbapedia
/ Serebii: numbers of Generation 7); an ability that has no entry here has its name and words only and says so ("no effect in this game yet").
STATUS: 'full' = as the mainline games in the single battles XD has; 'partial' = built with the named limit; the rest have no entry.
"""

BITE = ['Bite', 'Crunch', 'Fire Fang', 'Ice Fang', 'Thunder Fang', 'Poison Fang', 'Psychic Fangs', 'Hyper Fang']
PULSE = ['Aura Sphere', 'Dark Pulse', 'Dragon Pulse', 'Heal Pulse', 'Origin Pulse', 'Water Pulse']
BALL = ['Acid Spray', 'Aura Sphere', 'Barrage', 'Bullet Seed', 'Egg Bomb', 'Electro Ball', 'Energy Ball', 'Focus Blast', 'Gyro Ball', 'Ice Ball',
        'Magnet Bomb', 'Mist Ball', 'Mud Bomb', 'Octazooka', 'Rock Wrecker', 'Searing Shot', 'Seed Bomb', 'Shadow Ball', 'Sludge Bomb',
        'Weather Ball', 'Zap Cannon', 'Pollen Puff']
POWDER = ['Cotton Spore', 'Magic Powder', 'Poison Powder', 'Powder', 'Rage Powder', 'Sleep Powder', 'Spore', 'Stun Spore']
HEALING = ['Recover', 'Softboiled', 'Roost', 'Heal Order', 'Moonlight', 'Morning Sun', 'Synthesis', 'Milk Drink', 'Slack Off', 'Shore Up', 'Heal Pulse',
           'Floral Healing', 'Absorb', 'Mega Drain', 'Giga Drain', 'Leech Life', 'Dream Eater', 'Drain Punch', 'Horn Leech', 'Parabolic Charge',
           'Draining Kiss', 'Oblivion Wing']


def st(stat, stages, target='user', **k):
    d = {'stat': stat, 'stages': stages}
    if target != 'user':
        d['target'] = target
    d.update(k)
    return d



NORMAL, FLYING, WATER, ELECTRIC, ICE, DARK, FAIRY = 0, 2, 11, 13, 15, 17, 18


def ate(type_from, type_to):
    return {'convert': [{'from': type_from, 'to': type_to, 'mult': 1.2}]}



TABLE = {
    124: ('Pickpocket', {'contact': [{'steal_item': True}]}, 'full', ''),
    170: ('Magician', {'hit_dealt': [{'steal_item': True}]}, 'full', ''),
    125: ('Sheer Force', {'remove_secondary': True, 'damage_dealt': [{'when': 'move_has_secondary', 'mult': 1.3}]}, 'full', ''),
    126: ('Contrary', {'contrary': True}, 'partial', 'turns round the changes that come through a move\'s extra effect or an ability; the plain stat moves (Swords Dance) are not turned'),
    128: ('Defiant', {'stat_dropped': [st('atk', 2)]}, 'partial', 'runs a moment after the drop (at the next hit or the end of the turn)'),
    129: ('Defeatist', {'stat_mods': [{'stat': 'atk', 'mult': 0.5, 'when': 'hp_below_half'}, {'stat': 'spa', 'mult': 0.5, 'when': 'hp_below_half'}]}, 'full', ''),
    133: ('Weak Armor', {'hit': [st('def', -1, when='physical'), st('spe', 2, when='physical')]}, 'full', ''),
    134: ('Heavy Metal', {}, 'partial', 'doubles the weight only for Heavy Slam / Heat Crash'),
    135: ('Light Metal', {}, 'partial', 'halves the weight only for Heavy Slam / Heat Crash'),
    136: ('Multiscale', {'damage_taken': [{'when': 'full_hp', 'mult': 0.5}]}, 'full', ''),
    137: ('Toxic Boost', {'damage_dealt': [{'when': ['physical', 'user_status:poison'], 'mult': 1.5}]}, 'full', ''),
    138: ('Flare Boost', {'damage_dealt': [{'when': ['special', 'user_status:burn'], 'mult': 1.5}]}, 'full', ''),
    141: ('Moody', {'end_of_turn': [{'moody': True}]}, 'full', ''),
    143: ('Poison Touch', {'hit_dealt': [{'status': 'poison', 'target': 'target', 'chance': 30, 'when': 'contact'}]}, 'full', ''),
    144: ('Regenerator', {'switch_out': [{'heal': 33.3}]}, 'full', ''),
    145: ('Big Pecks', {'stat_drop_block': ['def']}, 'partial', 'stops the extra-effect drops of moves; a drop that always happens (Screech) still lands'),
    146: ('Sand Rush', {'stat_mods': [{'stat': 'spe', 'mult': 2, 'when': 'weather:sand'}]}, 'full', ''),
    147: ('Wonder Skin', {'accuracy_against': [{'when': 'status_move', 'accuracy': 50}]}, 'full', ''),
    148: ('Analytic', {'damage_dealt': [{'when': 'slower_than_target', 'mult': 1.3}]}, 'partial', 'compares speeds; it does not look at priority'),
    142: ('Overcoat', {'immune_moves': POWDER}, 'partial', 'immune to powder / spore moves; the sandstorm / hail damage immunity is not built'),
    203: ('Long Reach', {'no_contact': True}, 'partial', "the foe's on-contact abilities of the pack (Mummy, Pickpocket, Poison Touch ...) do not run; the game's own contact abilities (Rough Skin, Static) still do"),
    152: ('Mummy', {'contact': [{'set_ability': 152, 'target': 'target'}]}, 'full', ''),
    153: ('Moxie', {'ko': [st('atk', 1)]}, 'full', ''),
    154: ('Justified', {'hit': [st('atk', 1, when='type:dark')]}, 'full', ''),
    155: ('Rattled', {'hit': [st('spe', 1, when='type:dark'), st('spe', 1, when='type:ghost'), st('spe', 1, when='type:bug')]}, 'full', ''),
    156: ('Magic Bounce', {'reflect_status': True}, 'full', ''),
    157: ('Sap Sipper', {'absorb': [{'type': 'Grass', 'stat': 'atk', 'stages': 1}]}, 'full', ''),
    158: ('Prankster', {'priority': [{'when': 'status_move', 'add': 1}]}, 'full', ''),
    159: ('Sand Force', {'damage_dealt': [{'when': ['weather:sand', 'type:rock'], 'mult': 1.3}, {'when': ['weather:sand', 'type:ground'], 'mult': 1.3},
                                         {'when': ['weather:sand', 'type:steel'], 'mult': 1.3}]}, 'full', 'the sandstorm damage immunity is not built'),
    160: ('Iron Barbs', {'contact': [{'hurt': 12.5, 'target': 'target'}]}, 'full', ''),
    162: ('Victory Star', {'accuracy_dealt': [{'mult': 1.1}]}, 'partial', 'its own moves only (no allies in single battles)'),
    168: ('Protean', {'before_move': [{'set_type': 'move'}]}, 'full', ''),
    169: ('Fur Coat', {'damage_taken': [{'when': 'physical', 'mult': 0.5}]}, 'full', ''),
    171: ('Bulletproof', {'immune_moves': BALL}, 'full', ''),
    172: ('Competitive', {'stat_dropped': [st('spa', 2)]}, 'partial', 'runs a moment after the drop (at the next hit or the end of the turn)'),
    173: ('Strong Jaw', {'damage_dealt': [{'when': {'move_in': BITE}, 'mult': 1.5}]}, 'full', ''),
    174: ('Refrigerate', ate(NORMAL, ICE), 'full', ''),
    175: ('Sweet Veil', {'status_block': ['sleep']}, 'partial', 'for itself only (no allies in single battles); stops the extra-effect sleep of moves'),
    177: ('Gale Wings', {'priority': [{'when': ['type:flying', 'full_hp'], 'add': 1}]}, 'full', ''),
    178: ('Mega Launcher', {'damage_dealt': [{'when': {'move_in': PULSE}, 'mult': 1.5}]}, 'full', ''),
    179: ('Grass Pelt', {'stat_mods': [{'stat': 'def', 'mult': 1.5, 'when': 'terrain:grassy'}]}, 'full', ''),
    181: ('Tough Claws', {'damage_dealt': [{'when': 'contact', 'mult': 1.3}]}, 'full', ''),
    182: ('Pixilate', ate(NORMAL, FAIRY), 'partial', 'Fairy exists only with the choice "Modern types"; without it the moves stay Normal'),
    183: ('Gooey', {'contact': [st('spe', -1, 'target')]}, 'full', ''),
    184: ('Aerilate', ate(NORMAL, FLYING), 'full', ''),
    185: ('Parental Bond', {'damage_dealt': [{'when': 'damaging_move', 'mult': 1.25}]}, 'partial', 'the second hit (a quarter as strong) is folded into one hit that is 1.25 times as strong; no second chance for a secondary effect'),
    186: ('Dark Aura', {'aura': {'type': DARK, 'mult': 1.33}}, 'full', ''),
    187: ('Fairy Aura', {'aura': {'type': FAIRY, 'mult': 1.33}}, 'partial', 'Fairy exists only with the choice "Modern types"'),
    188: ('Aura Break', {'breaks_auras': True}, 'full', ''),
    189: ('Primordial Sea', {'switch_in': [{'weather': 'rain'}]}, 'partial', 'plain rain (no heavy rain that nothing can end)'),
    190: ('Desolate Land', {'switch_in': [{'weather': 'sun'}]}, 'partial', 'plain sun (no extreme sun that nothing can end)'),
    192: ('Stamina', {'hit': [st('def', 1)]}, 'full', ''),
    195: ('Water Compaction', {'hit': [st('def', 2, when='type:water')]}, 'full', ''),
    196: ('Merciless', {'crit_when': ['target_status:poison']}, 'full', ''),
    198: ('Stakeout', {'damage_dealt': [{'when': 'target_first_turn', 'mult': 2}]}, 'full', ''),
    199: ('Water Bubble', {'damage_dealt': [{'when': 'type:water', 'mult': 2}], 'damage_taken': [{'when': 'type:fire', 'mult': 0.5}], 'status_block': ['burn']}, 'full',
          'the burn is stopped only for extra effects of moves'),
    200: ('Steelworker', {'damage_dealt': [{'when': 'type:steel', 'mult': 1.5}]}, 'full', ''),
    201: ('Berserk', {'hit': [st('spa', 1, when='hp_crossed_half')]}, 'full', ''),
    202: ('Slush Rush', {'stat_mods': [{'stat': 'spe', 'mult': 2, 'when': 'weather:hail'}]}, 'full', ''),
    204: ('Liquid Voice', {'convert': [{'to': WATER, 'mult': 1.0, 'when': 'sound'}]}, 'full', ''),
    205: ('Triage', {'priority': [{'when': {'move_in': HEALING}, 'add': 3}]}, 'full', ''),
    206: ('Galvanize', ate(NORMAL, ELECTRIC), 'full', ''),
    207: ('Surge Surfer', {'stat_mods': [{'stat': 'spe', 'mult': 2, 'when': 'terrain:electric'}]}, 'full', ''),
    214: ('Queenly Majesty', {'blocks_priority': True}, 'full', ''),
    219: ('Dazzling', {'blocks_priority': True}, 'full', ''),
    215: ('Innards Out', {'fainted': [{'hurt_dealt': 100, 'target': 'target'}]}, 'full', ''),
    218: ('Fluffy', {'damage_taken': [{'when': 'contact', 'mult': 0.5}, {'when': 'type:fire', 'mult': 2}]}, 'full', ''),
    221: ('Tangling Hair', {'contact': [st('spe', -1, 'target')]}, 'full', ''),
    224: ('Beast Boost', {'ko': [st('best', 1)]}, 'full', ''),
    226: ('Electric Surge', {'switch_in': [{'terrain': 'electric'}]}, 'full', ''),
    227: ('Psychic Surge', {'switch_in': [{'terrain': 'psychic'}]}, 'full', ''),
    228: ('Misty Surge', {'switch_in': [{'terrain': 'misty'}]}, 'full', ''),
    229: ('Grassy Surge', {'switch_in': [{'terrain': 'grassy'}]}, 'full', ''),
    230: ('Full Metal Body', {'acts_as': 'Clear Body'}, 'full', ''),
    231: ('Shadow Shield', {'damage_taken': [{'when': 'full_hp', 'mult': 0.5}]}, 'full', ''),

    161: ('Zen Mode', {}, 'full', 'Darmanitan: Zen Mode form at half HP or less, Standard above it (when it comes in and at the end of each turn): forms_battle.cpp'),
    176: ('Stance Change', {}, 'full', 'Aegislash: Blade Forme before a damaging move, Shield Forme before Kings Shield: forms_battle.cpp'),
    197: ('Shields Down', {}, 'partial', 'Minior: Core form at half HP or less, Meteor form above it; the status immunity of the Meteor form is not built'),
    208: ('Schooling', {}, 'full', 'Wishiwashi: School Form at level 20 and HP above a quarter, Solo Form below: forms_battle.cpp'),
    209: ('Disguise', {}, 'partial', 'Mimikyu: the first damaging hit does no damage, it loses 1/8 HP and is Busted until the battle ends; the AI does not know it'),
    216: ('Dancer', {'dancer': ['Swords Dance', 'Dragon Dance', 'Feather Dance', 'Teeter Dance', 'Petal Dance', 'Quiver Dance', 'Fiery Dance', 'Revelation Dance', 'Lunar Dance']}, 'partial',
          "after another Pokemon uses a dance move the first Dancer of the others copies it at once with the move's own script (its own message, PP line and effects); only the first Dancer copies when there are two, and a dance move that failed is copied too"),
    211: ('Power Construct', {}, 'partial', "Zygarde: Complete Forme (the form row from the dump) when its HP falls to half or less, checked when it comes in and at the end of each turn, kept for the rest of the battle: forms_battle.cpp; the Complete Forme keeps the base model (its own model is not made); only reachable through the hidden ability (Gen 5-7 rule sets)"),
    210: ('Battle Bond', {}, 'partial', 'Greninja: Ash-Greninja after a KO; the Water Shuriken change is not built'),
    163: ('Turboblaze', {}, 'full', "the foe's ability is ignored as Mold Breaker does it (needs the Gen 4 rules, where Mold Breaker lives)"),
    164: ('Teravolt', {}, 'full', "the foe's ability is ignored as Mold Breaker does it (needs the Gen 4 rules, where Mold Breaker lives)"),
    213: ('Comatose', {'status_block': ['all']}, 'partial', 'no status problems from the extra effects of moves; it does not count as asleep'),
    220: ('Soul-Heart', {'any_faint': [st('spa', 1)]}, 'partial', 'Sp. Atk +1 when a Pokemon is knocked out by a hit (not by poison, weather ...)'),

    131: ('Healer', {}, 'partial', 'ally only: does nothing in single battles (as in the games)'),
    132: ('Friend Guard', {}, 'partial', 'ally only: does nothing in single battles (as in the games)'),
    140: ('Telepathy', {}, 'partial', 'ally only: does nothing in single battles (as in the games)'),
    165: ('Aroma Veil', {}, 'partial', 'ally only: does nothing in single battles (as in the games)'),
    166: ('Flower Veil', {}, 'partial', 'ally only: does nothing in single battles (as in the games)'),
    180: ('Symbiosis', {}, 'partial', 'ally only: does nothing in single battles (as in the games)'),
    217: ('Battery', {}, 'partial', 'ally only: does nothing in single battles (as in the games)'),
    222: ('Receiver', {}, 'partial', 'ally only: does nothing in single battles (as in the games)'),
    223: ('Power of Alchemy', {}, 'partial', 'ally only: does nothing in single battles (as in the games)'),

    127: ('Unnerve', {'unnerve': True}, 'full', "foes' berries have no effect while it is out (the foe's berry effect is read as none)"),
    130: ('Cursed Body', {'disable_on_hit': 30}, 'full', "30 % to Disable the move that hit it (the game's own Disable state on the attacker)"),
    139: ('Harvest', {'end_of_turn': [{'restore_berry': True, 'when': 'weather:sun', 'message': '{name} harvested its berry!'},
                                      {'restore_berry': True, 'chance': 50, 'when': '!weather:sun', 'message': '{name} harvested its berry!'}]}, 'full',
          'the berry it used up (the game remembers it, as Recycle does) comes back at the end of the turn: 50 %, always in sun'),
    167: ('Cheek Pouch', {'berry_eaten': [{'heal': 33.3}]}, 'full', 'a third of its HP back when it eats a berry; the heal follows the berry at the end of the move that made it eat'),
    150: ('Imposter', {'switch_in': [{'transform': True, 'message': '{name} transformed into {foe}!'}]}, 'partial',
          "the game's own Transform command on entering; no transform picture (the model is changed by the game's Transform script, not by the command)"),
    151: ('Infiltrator', {'infiltrator': True}, 'partial', "its moves pass the foe's Reflect, Light Screen, Aurora Veil, Safeguard and Mist; Substitute is not bypassed"),
    193: ('Wimp Out', {'half_hp': [{'switch_out': 'user'}]}, 'full', "when a hit takes it to half HP or less it goes back at the end of the move that hit it (Baton Pass's own switch-out, where U-turn's runs) and the replacement comes in (seen on the player's side; a wild Pokemon flees through the game's own forced-out script: written, not tested); the foe side's replacement was not seen"),
    194: ('Emergency Exit', {'half_hp': [{'switch_out': 'user'}]}, 'partial', 'the same as Wimp Out'),
    212: ('Corrosion', {'poison_any_type': True}, 'full', 'poison from its moves also works on Steel and Poison types'),
    191: ('Delta Stream', {'strong_wind': True, 'switch_in': [{'message': 'The mysterious air current blows on regardless!'}]}, 'partial',
          "while it is out Flying Pokemon take no extra damage from Rock, Ice and Electric moves; the weather is NOT removed and weather moves still work"),
    225: ('RKS System', {'type_by_item': [{'item': '%s Memory' % n, 'type': t} for n, t in (
        ('Fighting', 1), ('Flying', 2), ('Poison', 3), ('Ground', 4), ('Rock', 5), ('Bug', 6), ('Ghost', 7), ('Steel', 8), ('Fire', 10), ('Water', 11),
        ('Grass', 12), ('Electric', 13), ('Psychic', 14), ('Ice', 15), ('Dragon', 16), ('Dark', 17), ('Fairy', 18))]}, 'full',
          'its type follows the Memory it holds (the 17 Memory items are in the pack and sold at the prize desk under the Gen 5-7 rules); Multi-Attack takes the same type'),
    232: ('Prism Armor', {'damage_taken': [{'when': 'super_effective', 'mult': 0.75}]}, 'full', ''),
}

NOT_BUILT_WHY = {
127: 'foes eating berries: berries are not tracked', 130: 'disabling the attacker: Disable is not a block',
    131: 'ally only', 132: 'ally only', 139: 'berries are not tracked', 140: 'ally only', 142: 'powder / weather immunity not built', 149: 'disguise as a party member (not built: needs the model and name of another party member)',
    150: 'Transform on entry', 151: 'passes screens and Substitute', 161: 'form change', 163: 'ignoring abilities needs the Gen 4 rules', 164: 'ignoring abilities needs the Gen 4 rules',
    165: 'ally only', 166: 'ally only', 167: 'berries are not tracked', 176: 'form change', 180: 'ally only', 191: 'no strong-wind weather',
    197: 'form change', 203: 'moves that never make contact', 208: 'form change', 209: 'form change',
    210: 'form change', 211: 'form change', 213: 'never wakes up', 216: 'copying dance moves', 217: 'ally only',
220: 'reacting to every faint', 222: 'ally only', 223: 'ally only', 225: 'form change',
}




FORM_TRIGGERS = {
    161: 'Zen Mode (Darmanitan): Zen Mode form when HP is half or less at the end of a turn, back to normal above half',
    176: 'Stance Change (Aegislash): Blade Forme when it uses a damaging move, Shield Forme when it uses King\'s Shield',
    197: 'Shields Down (Minior): Core form (shell broken) when HP falls to half or less; Meteor form above half; no status problems while in Meteor form',
    208: 'Schooling (Wishiwashi): School Form when level 20 or more and HP above a quarter, Solo Form at a quarter or less, checked on entering and at the end of a turn',
    209: 'Disguise (Mimikyu): Busted Form after the first damaging hit (the hit does no damage, it loses 1/8 of its HP instead); stays Busted until the battle ends',
    210: 'Battle Bond (Greninja): Ash-Greninja after it knocks out a foe',
    211: 'Power Construct (Zygarde): Complete Forme when HP is half or less at the end of a turn',
    225: 'RKS System (Silvally): type follows the memory item held',
    181: 'Tough Claws, 184 Aerilate, 185 Parental Bond (Mega Kangaskhan / Mega Pinsir / Mega Gyarados abilities): built, the form only has to give the ability number',
    189: 'Primordial Sea (Primal Kyogre) / 190 Desolate Land (Primal Groudon): built as plain rain / sun on entering; the Primal form change itself is the forms machinery\'s',
}


def build_all(u):
    defs, report = {}, []
    for aid in range(124, u.ability_count()):
        name = u.ability_name(aid, 'en')
        if aid in TABLE:
            tname, d, status, note = TABLE[aid]
            defs[tname] = dict({'id': aid}, **d)
            report.append(dict(id=aid, name=name, status=status, note=note))
        else:
            report.append(dict(id=aid, name=name, status='none', note=NOT_BUILT_WHY.get(aid, 'not built')))
        if aid in FORM_TRIGGERS:
            report[-1]['form_trigger'] = FORM_TRIGGERS[aid]
    return defs, report
