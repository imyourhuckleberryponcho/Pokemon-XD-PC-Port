"""The Ability Patch (0.7.1): an item sold at Mt. Battle's Poke Coupon prize desk that switches a Pokemon to its species' hidden ability.

  python gen57_patch.py --pack <a pack folder>        adds data*/04_ability_patch.json and the line "patch Ability Patch" to forms.txt (idempotent)
  (gen57_forms.py calls add(pack) at the end of its run, so a pack made by the launcher has it)

PLACEHOLDER (the price is not fixed yet): PATCH_COUPON, the only place that sets it. Mega Stones cost 12,000, the Alola Charm 5,000.
The item only does something, and is only sold, under the Gen 5, 6 and 7 rule sets (games/xd/src/hidden_ability.cpp, forms_shop.cpp).
"""
import argparse
import json
import os

PATCH_COUPON = 20000
MEMORY_COUPON = 3000
MEMORIES = ['Fighting', 'Flying', 'Poison', 'Ground', 'Rock', 'Bug', 'Ghost', 'Steel', 'Fire', 'Water', 'Grass', 'Electric', 'Psychic', 'Ice', 'Dragon', 'Dark', 'Fairy']

DOC = {
    'us': ('ABILITY PATCH', 'Changes a Pokemon{00}to its hidden{00}ability.'),
    'fr': ('PATCH CAPACITE', 'Donne au Pokemon{00}sa capacite{00}cachee.'),
    'de': ('FAEHIGK.-PFLASTER', 'Gibt einem Pokemon{00}seine Versteckte{00}Faehigkeit.'),
    'it': ('CEROTTO ABILITA', 'Da al Pokemon{00}la sua abilita{00}nascosta.'),
    'es': ('PARCHE HABILIDAD', 'Da al Pokemon su{00}habilidad{00}oculta.'),
    'jp': ('とくせいパッチ', 'ポケモンを{00}かくれとくせいに{00}かえる。'),
}
FOLDERS = {'data': 'us', 'data_fr': 'fr', 'data_ge': 'de', 'data_it': 'it', 'data_sp': 'es', 'data_jp': 'jp'}


def add(pack, copy='Moon Stone'):
    n = 0
    for folder, lg in FOLDERS.items():
        d = os.path.join(pack, folder)
        if not os.path.isdir(d):
            continue
        name, doc = DOC[lg]
        json.dump({'_note': 'made by gen57_patch.py: the 17 RKS System Memories (0.7.1 round 3; placeholder price)',
                   'add': {'items': {'%s Memory' % t: {'copy': copy, 'text': {'name': ('%s MEMORY' % t).upper(), 'doc': 'Held by Silvally:{00}its type changes{00}to %s.' % t}, 'coupon': MEMORY_COUPON} for t in MEMORIES}}},
                  open(os.path.join(d, '05_rks_memories.json'), 'w', encoding='utf-8'), indent=1, ensure_ascii=False)
        json.dump({'_note': 'made by gen57_patch.py: the Ability Patch (0.7.1; placeholder price)',
                   'add': {'items': {'Ability Patch': {'copy': copy, 'text': {'name': name, 'doc': doc}, 'coupon': PATCH_COUPON}}}},
                  open(os.path.join(d, '04_ability_patch.json'), 'w', encoding='utf-8'), indent=1, ensure_ascii=False)
        n += 1
    ft = os.path.join(pack, 'forms.txt')
    if os.path.exists(ft):
        text = open(ft, encoding='utf-8').read()
        if '\npatch ' not in text:
            open(ft, 'w', encoding='utf-8', newline='').write(text.rstrip('\n') + '\npatch Ability Patch\n')
        text = open(ft, encoding='utf-8').read()
        if '\nmemory ' not in text:
            open(ft, 'w', encoding='utf-8', newline='').write(text.rstrip('\n') + '\n' + ''.join('memory %s Memory\n' % t for t in MEMORIES))
    return n


if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    ap.add_argument('--pack', required=True)
    print('Ability Patch added to', add(ap.parse_args().pack and ap.parse_args().pack), 'language folders')
