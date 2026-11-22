"""The words of the Gen 4 moves (docs/GEN4.md "Extra machines" and 13.1): the name XD-style and a short description of each.

Part of the Gen 4 pack. gen4_moves.py (the move rows) and gen4_machines.py (the TM items) call it; the C++ pack builder's
pbr_pack_move_text.cpp is the same thing, byte for byte. Nothing here is game data: the descriptions are our own words.

XD writes every move name in capitals ("THUNDERBOLT", "SELF-DESTRUCT"), so the Gen 4 names are written the same way in the text
the game shows. The move rows keep their mixed-case KEY ("Stone Edge") for the add-rows layer: learn-sets and machines name the move by it.
A move's description in the summary screen is two lines of up to 28 characters ("{00}" is the game's line break); the bag's box
for a TM shows three lines of up to 19, as XD's own TMs do, so the same sentence is wrapped both ways.
"""

DOCS = {
    'Roost': "Restores half of the user's HP.",
    'Gravity': "Intensifies gravity for five turns.",
    'Miracle Eye': "Lets Psychic moves hit Dark types.",
    'Wake-Up Slap': "Hits hard if the target is asleep.",
    'Hammer Arm': "A heavy punch that lowers Speed.",
    'Gyro Ball': "Hits harder the slower the user is.",
    'Healing Wish': "The user faints to heal the next one.",
    'Brine': "Hits harder if the target is at half HP.",
    'Natural Gift': "Power and type depend on the berry.",
    'Feint': "Hits even through Protect or Detect.",
    'Pluck': "Pecks the target and eats its berry.",
    'Tailwind': "Doubles team Speed for three turns.",
    'Acupressure': "Sharply raises one stat of the user.",
    'Metal Burst': "Returns the damage taken with more power.",
    'U-turn': "Attacks, then the user switches out.",
    'Close Combat': "A reckless attack that lowers Defense.",
    'Payback': "Hits harder if the user moves last.",
    'Assurance': "Hits harder if the target was hurt.",
    'Embargo': "Stops the target from using its item.",
    'Fling': "Throws the held item at the target.",
    'Psycho Shift': "Passes the user's status to the target.",
    'Trump Card': "Hits harder the fewer PP are left.",
    'Heal Block': "Stops the target from recovering HP.",
    'Wring Out': "Hits harder the more HP the target has.",
    'Power Trick': "Swaps the user's Attack and Defense.",
    'Gastro Acid': "Negates the target's ability.",
    'Lucky Chant': "Protects the team from critical hits.",
    'Me First': "Copies the target's move with more power.",
    'Copycat': "Copies the move used just before.",
    'Power Swap': "Swaps Attack changes with the target.",
    'Guard Swap': "Swaps Defense changes with the target.",
    'Punishment': "Hits harder if the target's stats rose.",
    'Last Resort': "Usable once all other moves were used.",
    'Worry Seed': "Makes the target's ability Insomnia.",
    'Sucker Punch': "Strikes first if the target attacks.",
    'Toxic Spikes': "Scatters poison spikes at the foe's feet.",
    'Heart Swap': "Swaps stat changes with the target.",
    'Aqua Ring': "A veil of water that restores HP.",
    'Magnet Rise': "Floats the user with magnetism.",
    'Flare Blitz': "A fiery charge that hurts the user too.",
    'Force Palm': "A palm strike that may paralyze.",
    'Aura Sphere': "A sphere of aura that never misses.",
    'Rock Polish': "Polishes the body to sharply raise Speed.",
    'Poison Jab': "A jab that may poison the target.",
    'Dark Pulse': "A wave of evil that may make foes flinch.",
    'Night Slash': "Slashes with a high critical-hit rate.",
    'Aqua Tail': "Swings a tail like a wave of water.",
    'Seed Bomb': "Slams the target with hard seeds.",
    'Air Slash': "Slashes with air. May make foes flinch.",
    'X-Scissor': "Slashes the target in an X shape.",
    'Bug Buzz': "A damaging buzz that may lower Sp. Def.",
    'Dragon Pulse': "Shoots a shock wave from the mouth.",
    'Dragon Rush': "A fierce charge that may make foes flinch.",
    'Power Gem': "Attacks with a beam of light like jewels.",
    'Drain Punch': "A punch that restores HP by half the damage.",
    'Vacuum Wave': "Throws a wave of air that strikes first.",
    'Focus Blast': "A blast of focus that may lower Sp. Def.",
    'Energy Ball': "Draws power from nature. May lower Sp. Def.",
    'Brave Bird': "A reckless dive that hurts the user too.",
    'Earth Power': "Erupts the ground. May lower Sp. Def.",
    'Switcheroo': "Swaps held items with the target.",
    'Giga Impact': "A huge charge. The user rests next turn.",
    'Nasty Plot': "Sharply raises the user's Sp. Atk.",
    'Bullet Punch': "A fast punch that strikes first.",
    'Avalanche': "Hits harder if the user was hit first.",
    'Ice Shard': "Hurls ice that strikes first.",
    'Shadow Claw': "A claw slash with a high critical-hit rate.",
    'Thunder Fang': "A bite that may paralyze or make foes flinch.",
    'Ice Fang': "A bite that may freeze or make foes flinch.",
    'Fire Fang': "A bite that may burn or make foes flinch.",
    'Shadow Sneak': "Extends a shadow to strike first.",
    'Mud Bomb': "Hurls mud. It may lower accuracy.",
    'Psycho Cut': "Psychic blades. High critical-hit rate.",
    'Zen Headbutt': "A headbutt that may make foes flinch.",
    'Mirror Shot': "Fires polished light. May lower accuracy.",
    'Flash Cannon': "Fires light from the body. May lower Sp. Def.",
    'Rock Climb': "A charge that may confuse the target.",
    'Defog': "Lowers evasiveness and clears hazards.",
    'Trick Room': "Slower ones move first for five turns.",
    'Draco Meteor': "Meteors fall. Sharply lowers Sp. Atk.",
    'Discharge': "A burst of electricity that may paralyze.",
    'Lava Plume': "A scorching blast that may burn.",
    'Leaf Storm': "A fierce leaf storm. Lowers Sp. Atk a lot.",
    'Power Whip': "Violently lashes the target with vines.",
    'Rock Wrecker': "Launches a huge rock. The user rests next.",
    'Cross Poison': "A slash that may poison. High crit rate.",
    'Gunk Shot': "Shoots filth at the target. May poison.",
    'Iron Head': "A steel headbutt that may make foes flinch.",
    'Magnet Bomb': "Launches steel bombs that never miss.",
    'Stone Edge': "Stabs with sharp stones. High crit rate.",
    'Captivate': "Sharply lowers Sp. Atk of the other sex.",
    'Stealth Rock': "Scatters floating rocks that hurt foes.",
    'Grass Knot': "Hits harder the heavier the target is.",
    'Chatter': "A chattering attack that may confuse.",
    'Judgment': "Its type depends on the held plate.",
    'Bug Bite': "Bites the target and eats its berry.",
    'Charge Beam': "A beam of electricity. May raise Sp. Atk.",
    'Wood Hammer': "Slams with its body. The user takes recoil.",
    'Aqua Jet': "Strikes first with a burst of water.",
    'Attack Order': "Attacks with a high critical-hit rate.",
    'Defend Order': "Raises the user's Defense and Sp. Def.",
    'Heal Order': "Restores half of the user's HP.",
    'Head Smash': "A savage headbutt that hurts the user a lot.",
    'Double Hit': "Slams the target twice in a row.",
    'Roar of Time': "A huge blast. The user rests next turn.",
    'Spacial Rend': "Tears space apart. High critical-hit rate.",
    'Lunar Dance': "The user faints to fully heal the next one.",
    'Crush Grip': "Hits harder the more HP the target has.",
    'Magma Storm': "Traps the target in a storm of fire.",
    'Dark Void': "Puts all foes to sleep.",
    'Seed Flare': "A shock wave that may sharply lower Sp. Def.",
    'Ominous Wind': "A gust that may raise all of the user's stats.",
    'Shadow Force': "Vanishes, then strikes on the next turn.",

    'False Swipe': "Leaves the target with at least 1 HP.",
    'Endure': "Survives any hit with at least 1 HP.",
    'Will-O-Wisp': "Burns the target with a sinister flame.",
    'Silver Wind': "A silvery gust that may raise all stats.",
    'Explosion': "A huge blast. The user faints.",
    'Recycle': "Recycles an item that was used up.",
    'Thunder Wave': "A weak jolt that paralyzes the target.",
    'Swords Dance': "A dance that sharply raises Attack.",
    'Psych Up': "Copies the target's stat changes.",
    'Rock Slide': "Drops rocks. May make foes flinch.",
    'Sleep Talk': "Uses a move at random while asleep.",
    'Dream Eater': "Eats a sleeper's dream to restore HP.",
    'Swagger': "Confuses the target but raises Attack.",
    'Substitute': "Makes a decoy from some of the user's HP.",
}


def upper_name(name):
    """The name as XD writes it: capitals."""
    return name.upper()


def wrap(text, width, lines):
    """Greedy word wrap into at most `lines` lines of at most `width` characters, joined with the game's line break "{00}"."""
    out, cur = [], ''
    for word in text.split(' '):
        if cur and len(cur) + 1 + len(word) > width:
            out.append(cur)
            cur = word
        else:
            cur = word if not cur else cur + ' ' + word
    out.append(cur)
    assert len(out) <= lines and all(len(l) <= width for l in out), (text, out)
    return '{00}'.join(out)


def move_doc(name):
    """The move's description in the summary screen (two lines of up to 28)."""
    return wrap(DOCS[name], 28, 2)


def item_doc(name):
    """The same sentence in the bag's box for the move's TM (three lines of up to 19, as XD's own TMs)."""
    return wrap(DOCS[name], 19, 3)
