"""The hidden abilities of the Gen 5-7 games as numbers: <pack>/battle/hidden_abilities.json (0.7.1, docs/GEN57.md "Hidden abilities").

  python gen57_hidden.py --rom <Ultra Sun / Moon dump> --pack <pack folder>      (adds the file to a finished pack)
  hidden = build(u)                                                              (gen57_pack.py calls this while it makes a pack)

One number per species (the third ability slot of the dump's species record, by the National Dex number): game data made on the player's own
PC, never in the repository. PLACEHOLDER (how players get a hidden ability is not decided yet): the game side is games/xd/src/hidden_ability.cpp.
"""
import argparse
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import gen57_data as D

LAST_SPECIES = 807


def build(u, last=LAST_SPECIES):
    """{national number: ability number} for every species whose hidden ability is a third, different ability."""
    out = {}
    for nat in range(1, last + 1):
        p = u.personal(nat)
        a1, a2, hidden = p.abilities
        if hidden == 0 or hidden in (a1, a2) or hidden >= u.ability_count():
            continue
        out[nat] = hidden

    out.setdefault(718, 211)
    return out


def write(u, pack):
    hidden = build(u)
    os.makedirs(os.path.join(pack, 'battle'), exist_ok=True)
    path = os.path.join(pack, 'battle', 'hidden_abilities.json')
    json.dump({'_note': 'made by gen57_hidden.py from the player\'s own dump: the hidden ability of each species by National Dex number (PLACEHOLDER rule: docs/GEN57.md)',
               'hidden_abilities': {str(k): v for k, v in sorted(hidden.items())}}, open(path, 'w'), indent=0)
    return len(hidden), path


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--rom', required=True)
    ap.add_argument('--pack', required=True)
    a = ap.parse_args()
    n, path = write(D.Usum(a.rom), a.pack)
    print('hidden abilities:', n, 'species ->', path)


if __name__ == '__main__':
    main()
