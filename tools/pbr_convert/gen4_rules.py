"""The substitution rules of the Gen 4 data converter (docs/GEN4.md, "Phase 2: data").

Only rules and names: no game data. Everything here is OUR decision where Pokemon XD lacks something a Gen 4
Pokemon (from Pokemon Battle Revolution's tables) has. Names are matched ignoring case, spaces and signs.
"""






MOVE_SUBSTITUTE = {
    'Roost': 'Recover', 'Gravity': None, 'Miracle Eye': 'Foresight', 'Wake-Up Slap': 'Karate Chop',
    'Hammer Arm': 'DynamicPunch', 'Gyro Ball': 'Iron Tail', 'Healing Wish': None, 'Brine': 'Water Pulse',
    'Natural Gift': 'Return', 'Feint': 'Quick Attack', 'Pluck': 'Wing Attack', 'Tailwind': None,
    'Acupressure': None, 'Metal Burst': 'Counter', 'U-turn': 'Silver Wind', 'Close Combat': 'Superpower',
    'Payback': 'Bite', 'Assurance': 'Bite', 'Embargo': None, 'Fling': 'Thief', 'Psycho Shift': None,
    'Trump Card': 'Return', 'Heal Block': None, 'Wring Out': 'Body Slam', 'Power Trick': None,
    'Gastro Acid': None, 'Lucky Chant': None, 'Me First': None, 'Copycat': 'Mirror Move', 'Power Swap': None,
    'Guard Swap': None, 'Punishment': 'Crunch', 'Last Resort': 'Hyper Beam', 'Worry Seed': None,
    'Sucker Punch': 'Faint Attack', 'Toxic Spikes': 'Spikes', 'Heart Swap': None, 'Aqua Ring': 'Ingrain',
    'Magnet Rise': None, 'Flare Blitz': 'Fire Blast', 'Force Palm': 'Brick Break', 'Aura Sphere': 'Sky Uppercut',
    'Rock Polish': 'Agility', 'Poison Jab': 'Sludge Bomb', 'Dark Pulse': 'Crunch', 'Night Slash': 'Faint Attack',
    'Aqua Tail': 'Waterfall', 'Seed Bomb': 'Leaf Blade', 'Air Slash': 'Aerial Ace', 'X-Scissor': 'Silver Wind',
    'Bug Buzz': 'Silver Wind', 'Dragon Pulse': 'Dragon Claw', 'Dragon Rush': 'Dragon Claw',
    'Power Gem': 'AncientPower', 'Drain Punch': 'Brick Break', 'Vacuum Wave': 'Mach Punch',
    'Focus Blast': 'Cross Chop', 'Energy Ball': 'Magical Leaf', 'Brave Bird': 'Drill Peck',
    'Earth Power': 'Earthquake', 'Switcheroo': 'Trick', 'Giga Impact': 'Hyper Beam', 'Nasty Plot': 'Tail Glow',
    'Bullet Punch': 'Metal Claw', 'Avalanche': 'Ice Punch', 'Ice Shard': 'Powder Snow',
    'Shadow Claw': 'Shadow Punch', 'Thunder Fang': 'Spark', 'Ice Fang': 'Ice Punch', 'Fire Fang': 'Fire Punch',
    'Shadow Sneak': 'Shadow Punch', 'Mud Bomb': 'Mud Shot', 'Psycho Cut': 'Psybeam', 'Zen Headbutt': 'Extrasensory',
    'Mirror Shot': 'Steel Wing', 'Flash Cannon': 'Meteor Mash', 'Rock Climb': 'Body Slam', 'Defog': None,
    'Trick Room': None, 'Draco Meteor': 'Outrage', 'Discharge': 'Thunderbolt', 'Lava Plume': 'Flamethrower',
    'Leaf Storm': 'SolarBeam', 'Power Whip': 'SolarBeam', 'Rock Wrecker': 'Rock Slide',
    'Cross Poison': 'Poison Fang', 'Gunk Shot': 'Sludge Bomb', 'Iron Head': 'Iron Tail',
    'Magnet Bomb': 'Steel Wing', 'Stone Edge': 'Rock Slide', 'Captivate': None, 'Stealth Rock': 'Spikes',
    'Grass Knot': 'Razor Leaf', 'Chatter': 'Wing Attack', 'Judgment': 'Hyper Voice', 'Bug Bite': 'Silver Wind',
    'Charge Beam': 'ThunderShock', 'Wood Hammer': 'Leaf Blade', 'Aqua Jet': 'Water Gun',
    'Attack Order': 'Silver Wind', 'Defend Order': 'Harden', 'Heal Order': 'Recover', 'Head Smash': 'Rock Slide',
    'Double Hit': 'DoubleSlap', 'Roar of Time': 'Outrage', 'Spacial Rend': 'Dragon Claw', 'Lunar Dance': None,
    'Crush Grip': 'Slam', 'Magma Storm': 'Fire Blast', 'Dark Void': 'Hypnosis', 'Seed Flare': 'SolarBeam',
    'Ominous Wind': 'Shadow Ball', 'Shadow Force': 'Shadow Ball',
}





ABILITY_SUBSTITUTE = {
    77: ('Tangled Feet', 20), 78: ('Motor Drive', 10), 79: ('Rivalry', 0), 80: ('Steadfast', 39),
    81: ('Snow Cloak', 8), 82: ('Gluttony', 0), 83: ('Anger Point', 0), 84: ('Unburden', 0),
    85: ('Heatproof', 47), 86: ('Simple', 0), 87: ('Dry Skin', 11), 88: ('Download', 0), 89: ('Iron Fist', 0),
    90: ('Poison Heal', 61), 91: ('Adaptability', 0), 92: ('Skill Link', 0), 93: ('Hydration', 61),
    94: ('Solar Power', 0), 95: ('Quick Feet', 62), 96: ('Normalize', 0), 97: ('Sniper', 0), 98: ('Magic Guard', 0),
    99: ('No Guard', 0), 100: ('Stall', 0), 101: ('Technician', 0), 102: ('Leaf Guard', 0), 103: ('Klutz', 0),
    104: ('Mold Breaker', 0), 105: ('Super Luck', 0), 106: ('Aftermath', 0), 107: ('Anticipation', 0),
    108: ('Forewarn', 0), 109: ('Unaware', 0), 110: ('Tinted Lens', 0), 111: ('Filter', 0), 112: ('Slow Start', 0),
    113: ('Scrappy', 0), 114: ('Storm Drain', 11), 115: ('Ice Body', 44), 116: ('Solid Rock', 0),
    117: ('Snow Warning', 0), 118: ('Honey Gather', 53), 119: ('Frisk', 0), 120: ('Reckless', 0),
    121: ('Multitype', 0), 122: ('Flower Gift', 0), 123: ('Bad Dreams', 0),
}









FALLBACK_LEVEL_BABY = 20
FALLBACK_LEVEL = 40
BABIES = {'Munchlax', 'Bonsly', 'Mime Jr.', 'Happiny', 'Mantyke', 'Budew', 'Chingling', 'Riolu'}
