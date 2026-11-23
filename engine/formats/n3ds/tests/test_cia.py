"""Test: the C++ dump reader (n3ds_tool) takes the same inputs as tools/n3ds/ncch.py (.cci, decrypted .cia) and refuses the same
bad ones with the SAME sentence. All bytes are made up (no game data). The made-up file builders are the ones of
tools/n3ds/test_ncch_cia.py (branch cia-support); pass its folder with --py-tools (default: tools/n3ds of this tree).
  python -I test_cia.py --tool <n3ds_tool.exe> [--py-tools <folder with test_ncch_cia.py + ncch.py>]
"""
import argparse, os, subprocess, sys, tempfile

ap = argparse.ArgumentParser()
ap.add_argument('--tool', required=True)
ap.add_argument('--py-tools', default=os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', '..', '..', 'tools', 'n3ds'))
a = ap.parse_args()
sys.path.insert(0, a.py_tools)
import test_ncch_cia as T   # noqa: E402
import ncch                 # noqa: E402

d = tempfile.mkdtemp(prefix='cia_cpp_')
game, payload = T.fake_ncch()
g2, _ = T.fake_ncch(plain=False)
cases = {
    'a.cci': T.make_cci(game),
    'a10000.cia': T.cia_prefix(len(game), sig=0x10000) + game,
    'a10004.cia': T.cia_prefix(len(game), sig=0x10004) + game,
    'a10005.cia': T.cia_prefix(len(game), sig=0x10005) + game,
    'two.cia': T.cia_prefix(len(game), extra_content=0x400) + game + b'\0' * 0x400,
    'enc.cia': T.cia_prefix(len(game), encrypted=True) + game,
    'enc2.cia': T.cia_prefix(len(g2)) + g2,
    'upd.cia': T.cia_prefix(len(game), title_high=0x0004000E, encrypted=True) + game,
    'dlc.cia': T.cia_prefix(len(game), title_high=0x0004008C) + game,
    'sys.cia': T.cia_prefix(len(game), title_high=0x00040010) + game,
    'cut.cia': (T.cia_prefix(len(game)) + game)[:T.align(0x2020) + 0x200],
    'cut2.cia': (T.cia_prefix(len(game)) + game)[:len(T.cia_prefix(len(game))) + 0x80],
}
bad = 0
for name, data in cases.items():
    p = os.path.join(d, name)
    open(p, 'wb').write(data)
    try:
        r, prod = ncch.open_rom(p)
        want = 'OK ' + prod + ' ' + ','.join('%s:%d' % (k, v[1]) for k, v in sorted(r.files.items()))
        r.f.close()
    except SystemExit as ex:
        want = 'ERR ' + str(ex.code)
    except AssertionError as ex:
        want = 'ERR assert'
    c = subprocess.run([a.tool, 'list', p], capture_output=True, text=True)
    if c.returncode == 0:
        lines = c.stdout.strip().split('\n')
        prod = lines[0].split()[0]
        got = 'OK ' + prod + ' ' + ','.join(sorted('%s:%d' % (l.split()[1], int(l.split()[0])) for l in lines[1:]))
    else:
        msg = c.stderr.strip()
        got = 'ERR ' + (msg[len('error: '):] if msg.startswith('error: ') else msg)
    ok = got == want or (want == 'ERR assert' and got.startswith('ERR'))
    print(('ok   ' if ok else 'FAIL ') + name + '  ' + got[:100])
    if not ok:
        bad += 1
        print('     python says: ' + want[:160])
    os.remove(p)
os.rmdir(d)
print('FAILED %d' % bad if bad else 'all passed')
sys.exit(1 if bad else 0)
