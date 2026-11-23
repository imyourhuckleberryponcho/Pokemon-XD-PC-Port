"""Writes tools/n3ds/gen57_tables.json: the hand-written tables of the Python data build (gen57_moves / gen57_move_plan / gen57_abilities /
gen57_all / gen57_forms / gen57_patch), as plain JSON for the C++ data build (usum_models_cpp). No game data: authored mechanics, words, numbers.
The Python files stay the source: run this after changing them (tests/n3ds_tables_test.py in the C++ proofs checks the file is current)."""
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
N3 = os.path.dirname(HERE)
sys.path.insert(0, N3)
sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(N3)), 'tools', 'pbr_convert'))
import gen57_moves as MV
import gen57_move_plan as P
import gen57_abilities as AB
import gen57_all as A
import gen57_forms as F
import gen57_patch as PT
import gen57_pack as GP


def build():
    t = {}
    t['moves'] = dict(
        anim_phys={str(k): v for k, v in MV.ANIM_PHYS.items()}, anim_spec={str(k): v for k, v in MV.ANIM_SPEC.items()},
        anim_status=[MV.ANIM_STATUS_SELF_UP, MV.ANIM_STATUS_FOE_DOWN, MV.ANIM_STATUS_NONE],
        stat={str(k): v for k, v in MV.STAT.items()}, inflict={str(k): v for k, v in MV.INFLICT.items()}, range={str(k): v for k, v in MV.RANGE.items()},
        features_done=sorted(P.FEATURES_DONE), feature_of_prop=P.FEATURE_OF_PROP, ok_pure=sorted(P.OK_PURE), force_partial=P.FORCE_PARTIAL,
        donor_status_default=P.DONOR_STATUS_DEFAULT, anim=P.ANIM, partial=P.PARTIAL, partial_ok=sorted(P.PARTIAL_OK), plan=P.PLAN)
    t['abilities'] = dict(table={str(k): list(v) for k, v in AB.TABLE.items()}, not_built_why={str(k): v for k, v in AB.NOT_BUILT_WHY.items()},
                          form_triggers={str(k): v for k, v in AB.FORM_TRIGGERS.items()})
    t['all'] = dict(spots57=A.SPOTS57, legend57=sorted(A.LEGEND57), fairy_species_old=sorted(A.FAIRY_SPECIES_OLD), evo_notes=A.EVO_NOTES,
                    no_effect=GP.NO_EFFECT)
    t['forms'] = dict(stone_doc=F.STONE_DOC, charm_doc={k: list(v) for k, v in F.CHARM_DOC.items()}, stone_coupon=F.STONE_COUPON, charm_coupon=F.CHARM_COUPON,
                      rotom_signature={str(k): v for k, v in F.ROTOM_SIGNATURE.items()}, model_form_code=F.MODEL_FORM_CODE)
    t['patch'] = dict(coupon=PT.PATCH_COUPON, memory_coupon=PT.MEMORY_COUPON, memories=PT.MEMORIES, doc={k: list(v) for k, v in PT.DOC.items()}, folders=PT.FOLDERS)
    return t


if __name__ == '__main__':
    out = os.path.join(N3, 'gen57_tables.json')
    text = json.dumps(build(), indent=1, ensure_ascii=True) + '\n'
    if len(sys.argv) > 1 and sys.argv[1] == '--check':
        sys.exit(0 if open(out, encoding='utf8').read() == text else 'gen57_tables.json is out of date: run cpp_ref/export_tables.py')
    open(out, 'w', encoding='utf8', newline='\n').write(text)
    print('wrote', out, len(text))
