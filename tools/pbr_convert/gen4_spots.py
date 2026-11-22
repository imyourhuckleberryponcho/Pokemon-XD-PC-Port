"""The curated Gen 4 additions to the three Poke Spots (docs/GEN4.md 13.6, docs/RANDOMIZER.md "Gen 4 Pokemon").

Each spot KEEPS its three Pokemon and gains five Gen 4 ones: eight entries a spot. The game picks by walking the entries' `prob` as
cumulative percentages against a roll of 0-99 (esaba.cpp _esabaAppearPokemonSub2: count from the table's head), so a longer table
whose probabilities still add up to 100 needs no code change. The originals keep 65 % between them in the disc's own proportions
(50/35/15 -> 33/23/9); the five Gen 4 species share 35 %: three common ones (9 each), the spot's rare find (4) and the spot's
Gen 4 starter (4). Levels: 10 up to the spot's own highest level; the rate (steps a Pokemon stays) is copied from an original entry.

  python tools/pbr_convert/gen4_spots.py            prints the table

gen4_pack.py calls files(ids) and writes the pack's optional/spots.json (a data-mod file: the originals' new probabilities) and
optional/spots_tables.txt (the appended rows, `row` lines read by games/xd/src/gen4_pack.cpp only when the New Game rule
"Gen 4 Pokemon at the Poke Spots" is on). With the rule off nothing of this is read and the tables are the disc's. No game data
here: the disc's rates are restated as numbers only.
"""


SPOTS = {

    'esabapokemondata_E1': ([33, 23, 9], [('Hippopotas', 9, 0), ('Skorupi', 9, 1), ('Cranidos', 9, 2), ('Gible', 4, 2), ('Chimchar', 4, 2)], 23),

    'esabapokemondata_E2': ([33, 23, 9], [('Shellos', 9, 0), ('Cherubi', 9, 1), ('Budew', 9, 2), ('Buizel', 4, 2), ('Turtwig', 4, 2)], 20),

    'esabapokemondata_E3': ([33, 23, 9], [('Bronzor', 9, 0), ('Stunky', 9, 1), ('Chingling', 9, 2), ('Riolu', 4, 2), ('Piplup', 4, 2)], 21),
}
ORIGINAL_RATES = {
    'esabapokemondata_E1': [300, 240, 150],
    'esabapokemondata_E2': [360, 210, 180],
    'esabapokemondata_E3': [330, 210, 90],
}
ORIGINAL_LEVELS = {
    'esabapokemondata_E1': [(10, 23), (10, 20), (10, 20)],
    'esabapokemondata_E2': [(10, 20), (10, 20), (10, 20)],
    'esabapokemondata_E3': [(10, 21), (10, 21), (10, 21)],
}


def files(ids):
    """ids: species name -> game species number. Returns (spots.json dict, [rows lines for spots_tables.txt])."""
    data = {'_note': 'Poke Spot odds of the Gen 4 set: the originals keep 65 % (33/23/9); the Gen 4 rows are appended (spots_tables.txt)'}
    rows = []
    for table, (probs, adds, top) in SPOTS.items():
        assert sum(probs) + sum(p for _, p, _ in adds) == 100, table
        assert top == max(hi for _, hi in ORIGINAL_LEVELS[table]), table
        data[table] = {'#%d' % i: {'prob, rate': [probs[i], ORIGINAL_RATES[table][i]]} for i in range(3)}
        for k, (name, prob, rate_of) in enumerate(adds):

            rows.append('row %s %d 1 %d 0:1:10 1:1:%d 2:2:%d 4:4:%d 8:4:%d' %
                        (table, 3 + k, rate_of, top, ids[name], prob, ORIGINAL_RATES[table][rate_of]))
    return data, rows


if __name__ == '__main__':
    for table, (probs, adds, top) in SPOTS.items():
        print(table, 'originals', probs, '+', ', '.join('%s %d%% Lv10-%d' % (n, p, top) for n, p, _ in adds))
