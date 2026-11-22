"""Showcase plan for the fire / glow proofs.
   python make_plan_fx.py <out plan.json> <species,..> <variant=layer folder|xd>,.. [att|hit]
   Species are national numbers (Gen 1-3 only: the stand-in battle gives them to the party).
   Clip names: fx_<species number>_<variant>_<kind>."""
import json
import sys

KEYS = {}


def inp(t0=6050, n=3):
    one = [(0, 'b'), (40, 'b'), (80, 'b'), (120, 'b'), (160, 'up'), (180, 'left'), (220, 'a')] + KEYS.get('x', [(250, 'up'), (270, 'left')]) + \
          [(300, 'a')] + [(d, 'a') for d in range(350, 670, 50)]
    return ','.join('%d:%s' % (t0 + 680 * k + d, key) for k in range(n) for d, key in one)


def main():
    out, species, variants = sys.argv[1], sys.argv[2].split(','), sys.argv[3].split(',')
    kind = sys.argv[4] if len(sys.argv) > 4 else 'att'
    clips = {}
    for sp in species:
        for v in variants:
            name, layer = v.split('=', 1)
            cn = 'fx_%s_%s_%s' % (sp, name, kind)
            clips[cn] = {"note": cn, "savepoint": "500_Phenac_City_opens", "continue": "own",
                         "frames": {"att": [5950, 7200], "quick": [6150, 6750], "mid": [6000, 6500], "summary": [5880, 6150]}.get(kind, [6550, 7300]), "turbo": 16,
                         "settings": {"video.widescreen": "on", "video.battle_revolution_models": 'off' if layer == 'xd' else 'on'},
                         "assets": {} if layer == 'xd' else {'pbr': layer},
                         "dev": "5450:give %s %d,5460:lead 2,5480:battle 150" % (sp, 18 if kind == 'att' else 14), "input": inp()}
            clips[cn]['env'] = {'XD_GPU_TIMING': '2'}
            if kind == 'summary':
                clips[cn]["dev"] = "5450:give %s 18,5460:lead 2" % sp
                clips[cn]["input"] = "5500:y,5600:a,5750:a,5850:a"
    json.dump({"name": "fx", "note": "tools/n3ds/make_plan_fx.py", "fade": 0.5, "fade_out": 1.0, "clips": clips, "edit": []},
              open(out, 'w'), indent=1)
    print(len(clips), 'clips')


main()
