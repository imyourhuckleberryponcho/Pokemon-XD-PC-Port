"""Cases for engine/formats/pbr/tests/pbr_cry_level_test.cpp: every cry wave of the player's dump through gen4_cry.build_xd_3ds.
  python -I tools/n3ds/cry_level_cases.py <decrypted Ultra Sun / Moon .cci> <out cases.bin>
The output holds game data: keep it under build/ (never in the repo)."""
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(os.path.dirname(HERE), 'pbr_convert'))
sys.path.insert(0, os.path.join(os.path.dirname(HERE), 'pbr_survey'))
import gen4_cry
import n3ds_cries as nc


def main(argv):
    waves = nc.cry_waves(argv[1])
    n = 0
    with open(argv[2], 'wb') as f:
        for w in waves:
            p = nc.parse_cwav(w)
            h, b, _notes = gen4_cry.build_xd_3ds(p)
            f.write(struct.pack('<II', p['rate'], p['nsamp']))
            f.write(struct.pack('<16H', *[c & 0xFFFF for c in p['chans'][0]['coefs']]))
            f.write(struct.pack('<I', len(p['data'][0])) + p['data'][0])
            f.write(struct.pack('<I', len(h)) + h + struct.pack('<I', len(b)) + b)
            n += 1
    print(n, 'cases')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
