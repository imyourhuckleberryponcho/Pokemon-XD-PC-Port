"""Hand-written part of the Gen 5-7 move plan (docs/GEN57_MOVES.md): what the dump's record cannot say.

PLAN[english name] = {
  'donor': XD move whose own battle script is reused (multi hit, high crit, protect...),
  'blocks': effect blocks of the mod-battle system (replace the ones worked out from the record); a block may carry
            'needs': [feature] and is left out while that feature is not in FEATURES_DONE,
  'power_rules' / 'accuracy_rules' / 'props': battle/moves.json keys,
  'needs': features the whole move needs; 'anim': animation, 'note': words for the report }
FEATURES_DONE = the engine features built so far (games/xd/src/mod_battle.cpp); a move that needs one that is not
in the set is written as what is built (damage only / name only) and says so in the report.
"""

FEATURES_DONE = {'blocks', 'switch_out', 'power_rules', 'status_switch', 'force_switch', 'crit', 'ignore_boosts', 'foul_play', 'def_stat', 'first_turn_only',
                 'power_formula', 'terrain', 'set_type_target', 'cure_status', 'side', 'protect_variant', 'cond_fainted', 'charge', 'needs_physical_hit', 'heats_up', 'type_from_user'}


FEATURE_OF_PROP = {'crit': 'crit', 'ignore_boosts': 'ignore_boosts', 'use_target_attack': 'foul_play', 'defend_stat': 'def_stat',
                   'first_turn_only': 'first_turn_only', 'power_formula': 'power_formula', 'type_chart': 'type_chart', 'protect': 'protect_variant', 'charge': 'charge', 'needs_physical_hit': 'needs_physical_hit', 'heats_up': 'heats_up', 'type_from_user': 'type_from_user'}

OK_PURE = {'Hone Claws', 'Quiver Dance', 'Coil', 'Flame Charge', 'Acid Spray', 'Work Up', 'Cotton Guard', 'Shift Gear', 'Noble Roar', 'Play Nice',
           'Confide', 'Tearful Look', 'Baby-Doll Eyes', 'Toxic Thread', 'Lunge', 'Trop Kick', 'Fire Lash', 'Power-Up Punch', 'Nuzzle',
           'Draining Kiss', 'Oblivion Wing', 'Parabolic Charge', 'Dragon Ascent', 'Heal Pulse', 'Clanging Scales', 'Shell Smash',
           'Volt Switch', 'Water Shuriken', 'V-create', 'Diamond Storm', 'Glaciate', 'Relic Song', 'Hyperspace Fury', 'Eerie Impulse', 'Leafage', 'Brutal Swing', 'Dragon Hammer'}


FORCE_PARTIAL = {'Techno Blast': 'type should follow the Drive held (always Normal here)', 
                 'Phantom Force': "vanishes in the air state (Fly's): the few moves that hit a flying target also hit it; does not break Protect", 'Steamroller': 'no double damage against Minimize',
                 'Heart Stamp': None, 'Solar Blade': None}
FORCE_PARTIAL = {k: v for k, v in FORCE_PARTIAL.items() if v}

DONOR_STATUS_DEFAULT = 'Splash'


def is_z(name, mid):
    return 622 <= mid <= 658 or 695 <= mid <= 703 or 719 <= mid <= 719 or 723 <= mid <= 728


ANIM = {
    'Flame Charge': 'Flame Wheel', 'Volt Switch': 'Thunderbolt', 'Dragon Tail': 'Dragon Claw', 'Hex': 'Shadow Ball',
    'Shell Smash': 'Withdraw', 'Quiver Dance': 'Dragon Dance', 'Coil': 'Bulk Up', 'Work Up': 'Bulk Up', 'Hone Claws': 'Sharpen',
    'Autotomize': 'Agility', 'Shift Gear': 'Agility', 'Cotton Guard': 'Iron Defense', 'Geomancy': 'Cosmic Power',
    'Parting Shot': 'Screech', 'Heal Pulse': 'Softboiled', 'Floral Healing': 'Softboiled', 'Shore Up': 'Recover', 'Purify': 'Softboiled',
    'Kings Shield': 'Protect', "King's Shield": 'Protect', 'Spiky Shield': 'Protect', 'Baneful Bunker': 'Protect', 'Mat Block': 'Protect',
    'Wide Guard': 'Protect', 'Quick Guard': 'Protect', 'Crafty Shield': 'Protect', 'Grassy Terrain': 'Ingrain',
    'Misty Terrain': 'Mist', 'Electric Terrain': 'Charge', 'Psychic Terrain': 'Calm Mind', 'Aurora Veil': 'Light Screen',
    'Sticky Web': 'String Shot', 'Play Rough': 'Body Slam', 'Moonblast': 'Magical Leaf', 'Draining Kiss': 'Sweet Kiss',
    'Boomburst': 'Hyper Voice', 'Hurricane': 'Gust', 'Scald': 'Water Pulse', 'Wild Charge': 'Volt Tackle',
    'Foul Play': 'Faint Attack', 'Psyshock': 'Psychic', 'Psystrike': 'Psychic', 'Secret Sword': 'Aeroblast', 'Fire Lash': 'Vine Whip',
}


PARTIAL = {
    'Autotomize': 'Speed +2 only: the weight loss is not built',     'Freeze-Dry': 'Water Pokemon are not hit super effectively', 'Magnetic Flux': 'raises the user whatever its ability (should need Plus or Minus; it is made for allies)',
    'Gear Up': 'raises the user whatever its ability (should need Plus or Minus; it is made for allies)', 'Aromatic Mist': 'ally only: does nothing in single battles (as in the games)',
    'Smack Down': 'does not ground a Flying target', 'Synchronoise': 'hits any target (should hit only one sharing a type)',
    'Echoed Voice': 'power does not grow with use', 'Clear Smog': 'does not reset the target\'s stat changes', 'Sky Drop': 'plain hit (no lifting turn)',
    'Incinerate': 'does not burn up a berry', 'Retaliate': 'power not doubled after a faint', 'Final Gambit': 'plain hit of fixed power (should deal the user\'s HP and faint)',
    'Water Pledge': 'no combination with the other Pledges', 'Fire Pledge': 'no combination with the other Pledges', 'Grass Pledge': 'no combination with the other Pledges',
    'Fusion Flare': 'power not doubled after Fusion Bolt', 'Fusion Bolt': 'power not doubled after Fusion Flare', 'Flying Press': 'plain Fighting hit (not also Flying)',
    'Belch': 'works without having eaten a berry', 'Hyperspace Hole': 'does not break through Protect', 'Thousand Arrows': 'does not ground a Flying target',
    
    'Sparkling Aria': 'does not cure a burn', 'Throat Chop': 'does not stop sound moves', 'Pollen Puff': 'cannot heal an ally (plain hit)',
    'Burn Up': 'the user does not lose its Fire type', 'Revelation Dance': 'type does not follow the user (Normal)', 'Core Enforcer': 'does not switch off an ability',
    'Beak Blast': 'no message when it starts heating up; the burn is as the games', 'Shell Trap': 'its own failure message is the plain "But it failed"; in double battles it hits one foe, not both', 'Stomping Tantrum': 'power not doubled after a failed move',
    'Spectral Thief': 'does not steal stat boosts', 'Sunsteel Strike': 'does not ignore abilities', 'Moongeist Beam': 'does not ignore abilities',
    'Disarming Voice': None, 'Boomburst': None, 'Petal Blizzard': None, 'Flame Burst': None, 'Round': None,
}
PARTIAL_OK = {k for k, v in PARTIAL.items() if v is None}
PARTIAL = {k: v for k, v in PARTIAL.items() if v}
OK_PURE_EXTRA = PARTIAL_OK

PLAN = {

    'Volt Switch': {'blocks': [{'switch_out': 'user'}]},
    'Parting Shot': {'blocks': [{'stat': 'atk', 'stages': -1, 'target': 'target'}, {'stat': 'spa', 'stages': -1, 'target': 'target'},
                                {'switch_out': 'user', 'needs': ['status_switch']}], 'needs': ['status_switch']},

    'Dragon Tail': {'blocks': [{'force_switch': True, 'needs': ['force_switch']}], 'needs': ['force_switch']},
    'Circle Throw': {'blocks': [{'force_switch': True, 'needs': ['force_switch']}], 'needs': ['force_switch']},

    'Storm Throw': {'props': {'crit': 'always'}, 'needs': ['crit']},
    'Frost Breath': {'props': {'crit': 'always'}, 'needs': ['crit']},
    'Chip Away': {'props': {'ignore_boosts': True}, 'needs': ['ignore_boosts']},
    'Sacred Sword': {'props': {'ignore_boosts': True}, 'needs': ['ignore_boosts']},
    'Darkest Lariat': {'props': {'ignore_boosts': True}, 'needs': ['ignore_boosts']},
    'Foul Play': {'props': {'use_target_attack': True}, 'needs': ['foul_play']},
    'Psyshock': {'props': {'defend_stat': 'def'}, 'needs': ['def_stat']},
    'Psystrike': {'props': {'defend_stat': 'def'}, 'needs': ['def_stat']},
    'Secret Sword': {'props': {'defend_stat': 'def'}, 'needs': ['def_stat']},
    'First Impression': {'props': {'first_turn_only': True}, 'needs': ['first_turn_only']},
    'Shell Trap': {'props': {'needs_physical_hit': True}, 'needs': ['needs_physical_hit']},
    'Multi-Attack': {'props': {'type_from_user': True}, 'needs': ['type_from_user']},
    'Beak Blast': {'props': {'heats_up': True}, 'needs': ['heats_up']},

    'Heavy Slam': {'props': {'power_formula': 'weight'}, 'needs': ['power_formula']},
    'Heat Crash': {'props': {'power_formula': 'weight'}, 'needs': ['power_formula']},
    'Electro Ball': {'props': {'power_formula': 'speed'}, 'needs': ['power_formula']},
    'Stored Power': {'props': {'power_formula': 'boosts'}, 'needs': ['power_formula']},
    'Power Trip': {'props': {'power_formula': 'boosts'}, 'needs': ['power_formula']},
    'Hex': {'power_rules': [{'when': 'target_has_status', 'mult': 2}], 'ok': True},
    'Acrobatics': {'power_rules': [{'when': 'user_no_item', 'mult': 2}], 'ok': True},
    'Venoshock': {'power_rules': [{'when': 'target_status:poison', 'mult': 2}], 'ok': True},
    'Hurricane': {'accuracy_rules': [{'when': 'weather:rain', 'accuracy': 255}, {'when': 'weather:sun', 'accuracy': 50}], 'ok': True},

    'Grassy Terrain': {'blocks': [{'terrain': 'grassy'}], 'needs': ['terrain']},
    'Electric Terrain': {'blocks': [{'terrain': 'electric'}], 'needs': ['terrain']},
    'Psychic Terrain': {'blocks': [{'terrain': 'psychic'}], 'needs': ['terrain']},
    'Misty Terrain': {'blocks': [{'terrain': 'misty'}], 'needs': ['terrain']},

    'Aurora Veil': {'blocks': [{'side': 'aurora_veil', 'when': 'weather:hail', 'needs': ['side']}], 'needs': ['side']},
    'Sticky Web': {'blocks': [{'side': 'sticky_web', 'target': 'target', 'needs': ['side']}], 'needs': ['side']},

    'Soak': {'blocks': [{'set_type': 'Water', 'target': 'target', 'needs': ['set_type_target']}], 'needs': ['set_type_target']},
    "Trick-or-Treat": {'blocks': [{'set_type': 'Ghost', 'target': 'target', 'needs': ['set_type_target']}], 'needs': ['set_type_target'],
                       'note': 'the target becomes Ghost (it should only gain Ghost)'},
    "Forest's Curse": {'blocks': [{'set_type': 'Grass', 'target': 'target', 'needs': ['set_type_target']}], 'needs': ['set_type_target'],
                       'note': 'the target becomes Grass (it should only gain Grass)'},

    'Floral Healing': {'blocks': [{'heal': 50, 'target': 'target'}], 'note': '66 percent in Grassy Terrain is not built'},
    'Purify': {'blocks': [{'heal': 50, 'target': 'user', 'when': 'target_has_status'}, {'cure_status': True, 'target': 'target'}], 'needs': ['cure_status']},
    'Strength Sap': {'blocks': [{'stat': 'atk', 'stages': -1, 'target': 'target'}, {'heal': 25, 'target': 'user'}],
                     'note': "heals a quarter of its HP (it should heal as much as the target's Attack)"},
    'Shore Up': {'blocks': [{'heal': 50, 'message': '{name} shored up and restored its HP!'}], 'note': '66 percent in a sandstorm is not built'},
    'Shell Smash': {'blocks': [{'stat': 'def', 'stages': -1}, {'stat': 'spd', 'stages': -1}, {'stat': 'atk', 'stages': 2},
                               {'stat': 'spa', 'stages': 2}, {'stat': 'spe', 'stages': 2}]},


    'Geomancy': {'blocks': [{'stat': 'spa', 'stages': 2}, {'stat': 'spd', 'stages': 2}, {'stat': 'spe', 'stages': 2}], 'needs': ['charge'],
                 'props': {'charge': {'donor': 'Razor Wind', 'message': '{name} is absorbing power!'}}},
    'Freeze Shock': {'blocks': [{'status': 'paralysis', 'target': 'target', 'chance': 30}], 'needs': ['charge'],
                     'props': {'charge': {'donor': 'Razor Wind', 'message': '{name} became cloaked in a freezing light!'}}},
    'Ice Burn': {'blocks': [{'status': 'burn', 'target': 'target', 'chance': 30}], 'needs': ['charge'],
                 'props': {'charge': {'donor': 'Razor Wind', 'message': '{name} became cloaked in freezing air!'}}},

    'Phantom Force': {'blocks': [], 'needs': ['charge'], 'props': {'charge': {'donor': 'Fly', 'message': '{name} vanished instantly!'}}},


    'Thousand Waves': {'blocks': [{'status': 'trap', 'target': 'target', 'when': '!target_type:ghost', 'message': '{name} can no longer escape!'}], 'ok': True},
    'Spirit Shackle': {'blocks': [{'status': 'trap', 'target': 'target', 'when': '!target_type:ghost', 'message': '{name} can no longer escape!'}], 'ok': True},
    'Anchor Shot': {'blocks': [{'status': 'trap', 'target': 'target', 'when': '!target_type:ghost', 'message': '{name} can no longer escape!'}], 'ok': True},

    'Rototiller': {'blocks': [{'stat': 'atk', 'stages': 1, 'when': 'user_type:grass'}, {'stat': 'spa', 'stages': 1, 'when': 'user_type:grass'},
                              {'stat': 'atk', 'stages': 1, 'target': 'target', 'when': 'target_type:grass'}, {'stat': 'spa', 'stages': 1, 'target': 'target', 'when': 'target_type:grass'}], 'ok': True},
    'Flower Shield': {'blocks': [{'stat': 'def', 'stages': 1, 'when': 'user_type:grass'}, {'stat': 'def', 'stages': 1, 'target': 'target', 'when': 'target_type:grass'}], 'ok': True},
    'Venom Drench': {'blocks': [{'stat': 'atk', 'stages': -1, 'target': 'target', 'when': 'target_status:poison'}, {'stat': 'spa', 'stages': -1, 'target': 'target', 'when': 'target_status:poison'},
                                {'stat': 'spe', 'stages': -1, 'target': 'target', 'when': 'target_status:poison'}], 'ok': True},
    'Aromatic Mist': {'blocks': [], 'own_effect': True, 'donor': 'Splash'},

    "King's Shield": {'donor': 'Protect', 'own_effect': True, 'props': {'protect': 'kings_shield'}, 'needs': ['protect_variant']},
    'Spiky Shield': {'donor': 'Protect', 'own_effect': True, 'props': {'protect': 'spiky_shield'}, 'needs': ['protect_variant']},
    'Baneful Bunker': {'donor': 'Protect', 'own_effect': True, 'props': {'protect': 'baneful_bunker'}, 'needs': ['protect_variant']},
    'Mat Block': {'donor': 'Protect', 'own_effect': True, 'note': 'plain Protect (should work on the first turn only)'},
    'Rage Powder': {'donor': 'Follow Me', 'own_effect': True},
    'Mind Blown': {'blocks': [{'hurt': 50, 'message': '{name} hurt itself!'}], 'ok': True},
    'Fell Stinger': {'blocks': [{'stat': 'atk', 'stages': 3, 'when': 'target_fainted', 'needs': ['cond_fainted']}], 'needs': ['cond_fainted']},
}
