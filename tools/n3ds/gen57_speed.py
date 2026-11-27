"""Numbers that save the Gen 5-7 converter work it would otherwise do again for every player (docs/GEN57.md "Speed").

Everybody's Ultra Sun dump is the same file, so some results of the converter are the same for everybody: which step of the size ladder a
model fits in (gen57_all.LADDER: the steps before it were too big for XD's 614400-byte file cap), and where each species' face camera looks
(gen57_facecam). gen57_speed_numbers.json holds those numbers for known dumps, keyed by the dump's fingerprint: plain numbers, nothing from the
game. A dump that is not in the table (Ultra Moon, another version) uses none of it: the converter searches as before. The result is the same
either way (the table only skips the search); gen57_speed_table.py makes the table and the proof is a file by file comparison of two packs.

  GEN57_NO_SPEED_TABLE=1   ignore the table (the proof run; also what a dump not in the table does)
  GEN57_RECORD_STEPS=<dir> write the ladder step of every model made into <dir> (gen57_speed_table.py reads them)
"""
import hashlib
import json
import os

HERE = os.path.dirname(os.path.abspath(__file__))
TABLE_FILE = os.path.join(HERE, 'gen57_speed_numbers.json')
VERSION = 1




LADDER = ((256, 2600, 1, 1, 0), (256, 2600, 3, 2, 0), (128, 2600, 3, 2, 0), (128, 2600, 8, 3, 0), (128, 2600, 16, 3, 32),
          (128, 2600, 16, 4, 16), (128, 2000, 24, 4, 16), (64, 1600, 24, 5, 12), (64, 1200, 32, 6, 8), (64, 900, 32, 6, 8), (64, 600, 32, 6, 8))
CAP = 614400


def dump_fingerprint(path):
    """The dump's size and a hash of its first and last megabyte (a few ms: the dump is gigabytes). Same as usum_gen57_make.fingerprint."""
    size = os.path.getsize(path)
    h = hashlib.sha1(str(size).encode())
    with open(path, 'rb') as f:
        h.update(f.read(1 << 20))
        f.seek(max(0, size - (1 << 20)))
        h.update(f.read(1 << 20))
    return '%d-%s' % (size, h.hexdigest()[:16])


def load(rom):
    """The table's numbers for this dump: dict(ladder={nat: step}, forms={name: step}, views={nat: entry}, wide={nat: entry}), or {} when the dump is
    not in the table (or the table is switched off)."""
    if os.environ.get('GEN57_NO_SPEED_TABLE') == '1' or not rom or not os.path.isfile(TABLE_FILE):
        return {}
    try:
        t = json.load(open(TABLE_FILE, encoding='utf-8'))
        if t.get('version') != VERSION:
            return {}
        return t['dumps'].get(dump_fingerprint(rom), {})
    except Exception:
        return {}


def convert_ladder(convert, gm, texs, mots, xd_bytes, height_cm, start=0, record=None):
    """The size ladder: the first step from `start` whose file fits the cap (the last step's result when none does).
    -> (pkx, info, step). `start` = what the table says for this model (steps before it were too big, so they are not tried again)."""
    start = max(0, min(int(start), len(LADDER) - 1))
    step = start
    for step in range(start, len(LADDER)):
        side, dec, atol, ctol, quant = LADDER[step]
        pkx, info = convert(gm, texs, mots, xd_bytes, mode='role', log=lambda t: None,
                            opts=dict(ground=True, decimate=dec, fire=True, light=1.0, eyes=True, size_rule='mainline', max_side=side,
                                      height_cm=height_cm, size_flat=True, hover_rule='model', match=False, anim_tol=atol, chan_tol=ctol, quant=quant), hover=None)
        if len(pkx) <= CAP:
            break
    if record:
        os.makedirs(os.path.dirname(record), exist_ok=True)
        json.dump(dict(step=step, size=len(pkx)), open(record, 'w'))
    return pkx, info, step


def record_path(kind, name):
    d = os.environ.get('GEN57_RECORD_STEPS')
    return os.path.join(d, '%s_%s.json' % (kind, name)) if d else None
