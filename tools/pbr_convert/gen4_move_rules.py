"""Our decisions for the 113 Gen 4 moves as real moves (tools/pbr_convert/gen4_moves.py, docs/GEN4.md "Phase 3").

Only rules and names: no game data. A Gen 4 move whose effect number (PBR's, a DS "battle effect") also occurs among the
Gen 1-3 moves needs no new logic: its row copies a Gen 1-3 move with that same effect (the "donor") and takes the Gen 4 type,
power, accuracy, PP, priority, effect chance and flags from the PBR disc. That is class "a" (58 moves). The others (effect
numbers 214 and up) are:
  b  a new effect the port implements (games/xd/src/gen4_moves.cpp; EFFECT_SLOTS)
  c  approximated: the damaging part is exact, a side effect is missing (the note says which); the row copies a
     Gen 1-3 move of the nearest effect
  d  not offered (the move IS its effect and the port has none): learn-sets keep the substitutes of gen4_rules.MOVE_SUBSTITUTE
"""


MOVE_PLAN = {
    'Roost': ('b', 'Recover', 'heals half; the user has no Flying type until the turn ends'),
    'Gravity': ('b', 'Safeguard', 'five turns: Ground hits Flying and Levitate, accuracy x5/3'), 'Miracle Eye': ('b', 'Leer', 'the target is identified: Dark hits Psychic, evasion above normal is ignored'),
    'Wake-Up Slap': ('b', 'Pound', 'double power against a sleeping target (no wake-up)'),
    'Hammer Arm': ('b', 'Pound', 'the user\'s Speed falls one stage'),
    'Gyro Ball': ('b', 'Pound', 'power 25 x target Speed / user Speed + 1 (max 150)'),
    'Healing Wish': ('b', 'Safeguard', 'the user faints; the next Pokemon that comes in is fully healed and cured'),
    'Brine': ('b', 'Pound', 'power 130 when the target is at half HP or less, else 65'),
    'Natural Gift': ('b', 'Pound', "type and power by the user's berry (the berry is used up)"),
    'Feint': ('b', 'Pound', 'hits through Protect and ends it'),
    'Pluck': ('b', 'Pound', "the user eats the target's berry (Oran, Sitrus and the status berries act)"),
    'Bug Bite': ('b', 'Pound', "the user eats the target's berry (Oran, Sitrus and the status berries act)"),
    'Tailwind': ('b', 'Safeguard', 'the user\'s side moves at double speed for three turns'),
    'Acupressure': ('b', 'Focus Energy', "the user's random stat rises two stages"), 'Metal Burst': ('b', 'Counter', 'returns 1.5 times the damage the user just took (physical or special)'),
    'U-turn': ('b', 'Pound', 'then the user switches out (stat stages are not passed)'),
    'Close Combat': ('b', 'Superpower', 'the user\'s Defense and Sp. Def fall one stage each'),
    'Payback': ('b', 'Pound', 'power 100 when the target moved first, else 50'),
    'Assurance': ('b', 'Pound', 'power 100 when the target was hurt this turn, else 50'),
    'Embargo': ('b', 'Leer', 'the target cannot use its held item for five turns'), 'Fling': ('b', 'Pound', "power 10 (berry) or 30 (other item) by the user's item, which is thrown away (no item effects)"), 'Psycho Shift': ('b', 'Leer', "the user's status problem moves to the target"), 'Trump Card': ('b', 'Pound', 'power by the PP the move has left (200 / 80 / 60 / 50 / 40)'),
    'Heal Block': ('b', 'Leer', 'the target cannot heal for five turns (healing moves fail; Aqua Ring)'),
    'Wring Out': ('b', 'Pound', 'power 120 x target HP / max HP'),
    'Crush Grip': ('b', 'Pound', 'power 120 x target HP / max HP'),
    'Power Trick': ('b', 'Focus Energy', "the user's Attack and Defense swap (used again: back)"), 'Gastro Acid': ('b', 'Leer', "the target's ability is suppressed"), 'Lucky Chant': ('b', 'Safeguard', "no critical hits against the user's side for five turns"), 'Me First': ('b', 'Metronome', "uses the target's chosen damaging move at 1.5 times the power"),
    'Copycat': ('b', 'Metronome', 'uses the last move any Pokemon used'), 'Power Swap': ('b', 'Leer', "Attack and Sp. Atk stages swap with the target's"), 'Guard Swap': ('b', 'Leer', "Defense and Sp. Def stages swap with the target's"),
    'Punishment': ('b', 'Pound', 'power 60 + 20 per stage the target has raised (max 200)'),
    'Last Resort': ('b', 'Pound', 'works only after the user has used all its other moves'), 'Worry Seed': ('b', 'Leer', "the target's ability becomes Insomnia"),
    'Sucker Punch': ('b', 'Pound', 'fails unless the target has not moved and chose a damaging move'),
    'Toxic Spikes': ('b', 'Safeguard', "poison spikes (two layers) on the foe's side"), 'Heart Swap': ('b', 'Leer', "all stat stages swap with the target's"), 'Aqua Ring': ('b', 'Focus Energy', 'heals 1/16 of its HP at the end of every turn'), 'Magnet Rise': ('b', 'Focus Energy', 'five turns of Ground immunity'),
    'Flare Blitz': ('b', 'Double-Edge', 'recoil a third, 10 % burn'),
    'Defog': ('b', 'Leer', "evasion falls; the target's side loses its hazards and screens"),
    'Trick Room': ('b', 'Safeguard', 'the slower Pokemon moves first for five turns'),
    'Captivate': ('b', 'Leer', 'Sp. Atk of an opposite-gender target falls two stages'),
    'Stealth Rock': ('b', 'Spikes', 'Rock-type damage (1/8 of HP x the type chart) to Pokemon of the foe\'s side that switch in'),
    'Chatter': ('b', 'Psybeam', 'confuses with the Diamond/Pearl chance (31 %)'),
    'Judgment': ('c', 'Pound', 'Normal: the plates that change its type do not exist in XD, so this is the faithful result'),
    'Head Smash': ('b', 'Double-Edge', 'recoil half of the damage dealt'),
    'Lunar Dance': ('b', 'Safeguard', 'the user faints; the next Pokemon that comes in is fully healed, cured and gets its PP back'),
    'Seed Flare': ('b', 'Psychic', "40 % the target's Sp. Def falls two stages"),
    'Shadow Force': ('b', 'Fly', 'vanishes like Fly; hits through Protect and ends it'),
    'Fire Fang': ('b', 'Flamethrower', '10 % burn and 10 % flinch'),
    'Ice Fang': ('b', 'Ice Beam', '10 % freeze and 10 % flinch'),
    'Thunder Fang': ('b', 'Thunderbolt', '10 % paralysis and 10 % flinch'),
    'Charge Beam': ('b', 'Psychic', "70 % the user's Sp. Atk rises one stage"),
}



PUNCH = {'Bullet Punch', 'Drain Punch', 'Hammer Arm'}



IGNORES_PROTECT = {'Feint', 'Shadow Force'}


CHANCE = {'Chatter': 31}







RANGE_OF_TARGET = {0x000: 0, 0x001: 1, 0x002: 3, 0x004: 4, 0x008: 6, 0x010: 5, 0x020: 5, 0x040: 5, 0x080: 7, 0x100: 5, 0x200: 5}





KEEP_RANGE = {'Discharge', 'Lava Plume'}







FLAG_FIX = {'Bug Buzz': (('bouon', 1),), 'Chatter': (('bouon', 1), ('yubiwohuru', 0), ('monomane', 0), ('nekonote', 0), ('negoto', 0)),
            'Dark Void': (('bouon', 0),), 'Metal Burst': (('yubiwohuru', 1), ('nekonote', 1)), 'Me First': (('monomane', 1),),
            'Copycat': (('monomane', 1),), 'Shadow Force': (('hidenFlag', 0),)}


FIXED_POWER = {'Gyro Ball': 60, 'Brine': 65, 'Wring Out': 60, 'Crush Grip': 60, 'Punishment': 60, 'Payback': 50, 'Assurance': 50}




ANIM_DONOR = {

    'Trick Room': 'Psych Up', 'Stealth Rock': 'Spikes', 'Roost': 'Recover', 'Tailwind': 'Mist',

    'Gravity': 'Psych Up', 'Miracle Eye': 'Foresight', 'Acupressure': 'Focus Energy', 'Embargo': 'Torment', 'Psycho Shift': 'Skill Swap', 'Heal Block': 'Torment', 'Gastro Acid': 'Toxic', 'Lucky Chant': 'Safeguard', 'Power Swap': 'Skill Swap', 'Guard Swap': 'Skill Swap', 'Worry Seed': 'Leech Seed', 'Toxic Spikes': 'Spikes', 'Heart Swap': 'Skill Swap', 'Aqua Ring': 'Rain Dance', 'Magnet Rise': 'Agility', 'Defog': 'Haze', 'Captivate': 'Attract', 'Healing Wish': 'Wish', 'Lunar Dance': 'Wish', 'Power Trick': 'Psych Up', 'Me First': 'Mimic', 'Copycat': 'Mimic',

    'Close Combat': 'Superpower', 'Hammer Arm': 'DynamicPunch', 'Bullet Punch': 'Mach Punch', 'Wake-Up Slap': 'DoubleSlap',
    'Wring Out': 'Wrap', 'Crush Grip': 'ViceGrip', 'Night Slash': 'Slash', 'Poison Jab': 'Poison Sting', 'Payback': 'Knock Off',
    'Assurance': 'Pursuit', 'Zen Headbutt': 'Extrasensory', 'Iron Head': 'Headbutt', 'Head Smash': 'Skull Bash',
    'Giga Impact': 'Double-Edge', 'Aqua Tail': 'Crabhammer', 'Aqua Jet': 'Waterfall', 'Flare Blitz': 'Flame Wheel',
    'Wood Hammer': 'Needle Arm', 'Power Whip': 'Vine Whip', 'Grass Knot': 'Vine Whip',

    'Pluck': 'Peck', 'Brave Bird': 'Sky Attack', 'U-turn': 'Quick Attack', 'X-Scissor': 'Fury Cutter', 'Bug Bite': 'Leech Life',
    'Attack Order': 'Pin Missile', 'Air Slash': 'Air Cutter', 'Vacuum Wave': 'SonicBoom', 'Chatter': 'Supersonic',

    'Aura Sphere': 'Mist Ball', 'Focus Blast': 'Weather Ball', 'Magnet Bomb': 'Weather Ball', 'Dark Pulse': 'Shadow Ball',
    'Dragon Pulse': 'DragonBreath', 'Mirror Shot': 'Signal Beam', 'Flash Cannon': 'Signal Beam', 'Judgment': 'Luster Purge',
    'Roar of Time': 'Hyper Beam', 'Spacial Rend': 'Aeroblast', 'Magma Storm': 'Fire Spin', 'Lava Plume': 'Eruption',

    'Rock Wrecker': 'Rock Throw', 'Stone Edge': 'Rock Tomb', 'Ice Shard': 'Icicle Spear', 'Avalanche': 'Ice Ball',
    'Seed Bomb': 'Bullet Seed', 'Leaf Storm': 'Razor Leaf',
}






EFFECT_SLOTS = {'U-turn': 12, 'Close Combat': 14, 'Stealth Rock': 15, 'Trick Room': 21, 'Hammer Arm': 22, 'Roost': 55, 'Tailwind': 56,


                'Gravity': 61, 'Magnet Rise': 61, 'Aqua Ring': 61, 'Toxic Spikes': 61, 'Lucky Chant': 61, 'Acupressure': 61,
                'Defog': 63, 'Miracle Eye': 63, 'Gastro Acid': 63, 'Worry Seed': 63, 'Embargo': 63, 'Heal Block': 63,
                'Captivate': 63, 'Power Swap': 63, 'Guard Swap': 63, 'Heart Swap': 63, 'Psycho Shift': 63,
                'Seed Flare': 64, 'Charge Beam': 74, 'Sucker Punch': 96,


                'Fire Fang': 110, 'Ice Fang': 110, 'Thunder Fang': 110, 'Flare Blitz': 110, 'Fling': 110, 'Natural Gift': 110,
                'Pluck': 110, 'Bug Bite': 110, 'Me First': 131, 'Copycat': 131, 'Healing Wish': 141, 'Lunar Dance': 141,
                'Last Resort': 96, 'Power Trick': 61}




IMPLEMENTED = {'Trick Room', 'U-turn', 'Close Combat', 'Hammer Arm', 'Stealth Rock', 'Tailwind', 'Roost'} | set(EFFECT_SLOTS) | {
    'Feint', 'Trump Card', 'Metal Burst', 'Head Smash', 'Chatter', 'Shadow Force', 'Wake-Up Slap', 'Gyro Ball', 'Brine', 'Payback', 'Assurance', 'Wring Out', 'Crush Grip', 'Punishment'}
