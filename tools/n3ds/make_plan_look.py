r"""Plan for tools/showcase.py: summary-screen and battle clips of converted species (eyes / blinks / sizes / hover).
   python make_plan_look.py <out plan.json> <name=layer folder,..> <species,..> [summary|battle|both]
   (scratch folder buildgent_usumlook; layer names become the clip suffix, e.g. before=...\layer_before,after=...\layer_a8)
The summary save has one Pokemon (the new one goes to slot 2, "lead 2" swaps it to the front; a full party would send it to the PC)."""
import json
import sys

out = sys.argv[1]
layers = dict(kv.split('=', 1) for kv in sys.argv[2].split(','))
species = [int(x) for x in sys.argv[3].split(',')]
mode = sys.argv[4] if len(sys.argv) > 4 else 'both'
BATTLE_KEYS = "6050:b,6090:b,6130:b,6170:b,6210:up,6230:left,6270:a,6300:up,6320:left,6350:a"
clips = {}
for sp in species:
    for tag, layer in layers.items():
        common = {"continue": "own", "turbo": 16, "settings": {"video.widescreen": "on"}, "assets": {"usum": layer},
                  "env": {"XD_CHALLENGES": "models_usum"}}
        if mode in ('summary', 'both'):
            clips['look_sum_%d_%s' % (sp, tag)] = dict(common, note="summary screen %d %s" % (sp, tag),
                savepoint="010_Got_out_of_the_Battle_Sim_room", frames=[5900, 6700],
                dev="5300:give %d 20,5320:lead 2" % sp, input="5500:y,5600:a,5750:a,5850:a")
        if mode in ('battle', 'both'):
            clips['look_bat_%d_%s' % (sp, tag)] = dict(common, note="battle %d %s" % (sp, tag),
                savepoint="500_Phenac_City_opens", frames=[6350, 7000],
                dev="5450:give %d 18,5460:lead 2,5480:battle 150" % sp, input=BATTLE_KEYS)
json.dump({"name": "look", "note": "tools/n3ds/make_plan_look.py", "fade": 0.5, "fade_out": 1.0, "clips": clips, "edit": []},
          open(out, 'w'), indent=1)
print(len(clips), 'clips')
