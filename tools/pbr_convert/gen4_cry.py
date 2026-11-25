"""Gen 4 cries: Pokemon Battle Revolution's .brstm (DSP-ADPCM, stereo, 32 kHz) -> XD's cry streams (docs/GEN4.md, "Cries").

  python tools/pbr_convert/gen4_cry.py selftest --xd <XD disc> [--out <folder>]
        parse every XD cry (stm_se_nakigoe_archive.fsys: 388 x header + body), build it again, must be byte-identical
  python tools/pbr_convert/gen4_cry.py convert <out folder> --pbr <PBR disc> [--only 448,445]
        the 107 Gen 4 cries -> <out>/cries/<dex>_<name>.hdr + .body (+ cries.tsv)
  python tools/pbr_convert/gen4_cry.py test <work folder> --exe <xd.exe> --xd <XD disc> --pbr <PBR disc> [--timeout 200]
        a battle where two XD species cry with Gen 4 cries (a test mod: file replacement) + XD_AUDIO_DUMP

Format (measured, 388 of 388 rebuilt byte for byte; both in big-endian):
  header, type 20, 192 bytes: 'I_SF', 0x20040109, rate, samples, end+1, channels, block size (0x8000), 0,
          start nibble address (2), end nibble address, loop end (= end), 0..., then one 0x40-byte DSP-ADPCM
          channel block per channel from 0x40: 16 s16 coefficients, gain, first frame header, history 1 and 2,
          loop frame header, loop history 1 and 2 (the history/gain words 0), zero padding
  body, type 21: for each 0x8000-byte slice of the channels' data: channel 0's slice, channel 1's slice (zero padded)
Which archive entry belongs to which species: Species.voice -> snd_data[voice].file -> snd_file[file] = {1966,
  header id << 16, body id << 16} -> the archive's members with those ids (see GEN4.md "Cries").
"""
import argparse, os, struct, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(os.path.dirname(HERE), 'pbr_survey'))
import noddisc

BLOCK = 0x8000
MAX_BLOCKS = 2


def be32(b, o):
    return struct.unpack_from('>I', b, o)[0]


def be16(b, o):
    return struct.unpack_from('>H', b, o)[0]


def frames_of(nsamp):
    return (nsamp + 13) // 14



def parse_brstm(d):
    assert d[:4] == b'RSTM'
    head = be32(d, 0x10)
    data = be32(d, 0x20)
    base = head + 8
    assert d[head:head + 4] == b'HEAD' and d[data:data + 4] == b'DATA'
    refs = [struct.unpack_from('>BBHI', d, base + 8 * i) for i in range(3)]
    si = base + refs[0][3]
    codec, loop, chn = d[si], d[si + 1], d[si + 2]
    rate = be16(d, si + 4)
    lstart, nsamp, dataoff, nblk, blksz, _bs, lastsz = struct.unpack_from('>7I', d, si + 8)
    assert codec == 2, 'not DSP-ADPCM'
    ci = base + refs[2][3]
    nc = d[ci]
    chans = []
    for i in range(nc):
        cref = struct.unpack_from('>BBHI', d, ci + 4 + 8 * i)
        ap = struct.unpack_from('>BBHI', d, base + cref[3])
        q = base + ap[3]
        coefs = struct.unpack_from('>16h', d, q)
        gain, ps, h1, h2, lps, lh1, lh2 = struct.unpack_from('>7H', d, q + 32)
        chans.append(dict(coefs=coefs, gain=gain, ps=ps, h1=h1, h2=h2, lps=lps, lh1=lh1, lh2=lh2))

    pos = dataoff
    streams = [bytearray() for _ in range(nc)]
    for b in range(nblk):
        last = b == nblk - 1
        sz = lastsz if last else blksz
        pad = ((sz + 0x1F) & ~0x1F) if last else sz
        for c in range(nc):
            streams[c] += d[pos:pos + sz]
            pos += pad
    need = frames_of(nsamp) * 8
    return dict(rate=rate, nsamp=nsamp, loop=loop, chans=chans, data=[bytes(s[:need]) for s in streams])


def parse_xd(hdr, body):
    assert hdr[:4] == b'I_SF' and len(hdr) == 192
    rate, nsamp, endp1, nch, blk = struct.unpack_from('>5I', hdr, 8)
    assert blk == BLOCK
    chans = []
    for c in range(nch):
        o = 0x40 + 0x40 * c
        coefs = struct.unpack_from('>16h', hdr, o)
        gain, ps, h1, h2, lps, lh1, lh2 = struct.unpack_from('>7H', hdr, o + 32)
        chans.append(dict(coefs=coefs, gain=gain, ps=ps, h1=h1, h2=h2, lps=lps, lh1=lh1, lh2=lh2))
    need = frames_of(nsamp) * 8
    nb = (need + BLOCK - 1) // BLOCK
    data = [bytearray() for _ in range(nch)]
    for j in range(nb):
        for c in range(nch):
            o = (j * nch + c) * BLOCK
            data[c] += body[o:o + BLOCK]
    return dict(rate=rate, nsamp=nsamp, loop=0, chans=chans, data=[bytes(x[:need]) for x in data], endp1=endp1)


def build_xd(info, max_blocks=MAX_BLOCKS):
    """-> (header 192 bytes, body, notes). Longer than max_blocks slices: cut at a frame boundary."""
    nch = len(info['data'])
    nsamp = info['nsamp']
    notes = []
    cap = max_blocks * BLOCK
    data = info['data']
    if len(data[0]) > cap:
        data = [x[:cap] for x in data]
        nsamp = cap // 8 * 14
        notes.append('cut at %d samples (%.2f s) of %d' % (nsamp, nsamp / info['rate'], info['nsamp']))
    last = nsamp - 1
    end = last // 14 * 16 + 2 + last % 14
    h = bytearray(192)
    h[0:4] = b'I_SF'
    struct.pack_into('>9I', h, 4, 0x20040109, info['rate'], nsamp, end + 1, nch, BLOCK, 0, 2, end)
    struct.pack_into('>I', h, 0x28, end)
    for c, ch in enumerate(info['chans']):
        o = 0x40 + 0x40 * c
        struct.pack_into('>16h', h, o, *ch['coefs'])
        ps = data[c][0]
        struct.pack_into('>7H', h, o + 32, 0, ps, 0, 0, ps, 0, 0)
    nb = (len(data[0]) + BLOCK - 1) // BLOCK
    body = bytearray()
    for j in range(nb):
        for c in range(nch):
            piece = data[c][j * BLOCK:(j + 1) * BLOCK]
            body += piece + bytes(BLOCK - len(piece))
    return bytes(h), bytes(body), notes









LOUD_RMS = 9778


def cry_is_loud(chan, data, nsamp):
    pcm = decode_dsp_ints(chan, data, nsamp)
    if not pcm:
        return False
    return sum(v * v for v in pcm) > len(pcm) * LOUD_RMS * LOUD_RMS


def halve_frames(data):
    out = bytearray(data)
    for f in range(0, len(out) - 7, 8):
        if out[f] & 15:
            out[f] -= 1
    return bytes(out)


def build_xd_3ds(info):
    """build_xd for a cry of the 3DS games: the same, with the level matched to XD's cries (see above). -> (header, body, notes)"""
    notes = []
    if cry_is_loud(info['chans'][0], info['data'][0], info['nsamp']):
        info = dict(info, data=[halve_frames(d) for d in info['data']])
        notes.append('level -6 dB (louder than XD cries)')
    h, b, n2 = build_xd(info)
    return h, b, notes + n2



def decode_dsp_ints(chan, data, nsamp):
    """DSP-ADPCM -> a list of s16 values (integers only, so the C++ port (pbr_pack_cry.cpp wave_of) gives the same list)"""
    out = []
    c = chan['coefs']
    h1, h2 = chan['h1'], chan['h2']
    for f in range(0, len(data) - 7, 8):
        hdr = data[f]
        sc = 1 << (hdr & 15)
        c1, c2 = c[(hdr >> 4) * 2], c[(hdr >> 4) * 2 + 1]
        for k in range(14):
            if len(out) >= nsamp:
                return out
            b = data[f + 1 + k // 2]
            nib = (b >> 4) if k % 2 == 0 else (b & 15)
            if nib >= 8:
                nib -= 16
            v = ((nib * sc << 11) + 1024 + c1 * h1 + c2 * h2) >> 11
            v = max(-32768, min(32767, v))
            out.append(v)
            h2, h1 = h1, v
    return out


WAVE_PER_SECOND = 30
WAVE_MIN, WAVE_MAX = 16, 60


def wave_disp(hdr, body):
    """The PDA memo's waveform of a cry (the game's WaveDispData: a count, then that many signed bytes drawn as the line):
    the cry cut into `count` equal pieces (about 30 a second), the loudest sample of each (the first of equals) scaled so
    the loudest of all is +-127. -> bytes"""
    info = parse_xd(hdr, body)
    nsamp, rate = info['nsamp'], info['rate']
    pcm = decode_dsp_ints(info['chans'][0], info['data'][0], nsamp)
    nsamp = len(pcm)
    n = max(WAVE_MIN, min(WAVE_MAX, (nsamp * WAVE_PER_SECOND * 2 + rate) // (2 * rate)))
    picks = []
    for k in range(n):
        a, b = nsamp * k // n, nsamp * (k + 1) // n
        best = 0
        for v in pcm[a:b]:
            if abs(v) > abs(best):
                best = v
        picks.append(best)
    peak = max(abs(v) for v in picks) or 1
    out = bytearray([n])
    for v in picks:
        m = abs(v) * 127 // peak
        out.append((-m if v < 0 else m) & 255)
    return bytes(out)



def archive(xd_disc):
    D = noddisc.Disc(xd_disc)
    d = D.file('stm_se_nakigoe_archive.fsys')
    es = noddisc.fsys_entries(d)
    return d, es


def snd_files(xd_disc, cache, exe=''):
    """snd_file rows {a, header id << 16, body id << 16} (516), through xd_data_mods (the port's table reader); cached."""
    import json, re, subprocess
    from concurrent.futures import ThreadPoolExecutor
    if os.path.exists(cache):
        return json.load(open(cache))
    env = dict(os.environ, XD_SETTINGS_DIR=os.environ.get('XD_SETTINGS_DIR', ''))
    os.makedirs(env['XD_SETTINGS_DIR'], exist_ok=True)

    def row(i):
        out = subprocess.run([exe, xd_disc, '--show', 'snd_file', '#%d' % i], capture_output=True, text=True, env=env).stdout
        return list(map(int, re.search(r'\[(\d+), (\d+), (\d+)\]', out).groups()))
    with ThreadPoolExecutor(8) as ex:
        rows = list(ex.map(row, range(516)))
    json.dump(rows, open(cache, 'w'))
    return rows


def cry_pairs(rows):
    """[(snd_file row, header id, body id)] of the 388 cry streams"""
    return [(k, r[1] >> 16, r[2] >> 16) for k, r in enumerate(rows) if r[0] == 1966]


def selftest(a):
    d, es = archive(a.xd)
    byid = {}
    for e in es:
        byid[(e.type, e.id)] = e
    rows = snd_files(a.xd, os.path.join(a.out or '.', 'snd_file.json'))
    bad = 0
    sizes = {}
    pairs = cry_pairs(rows)
    heads = [byid[(20, h)] for _k, h, _b in pairs]
    for (_k, hid, bid) in pairs:
        e = byid[(20, hid)]
        b = byid[(21, bid)]
        h = noddisc.extract(d, e)
        bo = noddisc.extract(d, b)
        info = parse_xd(h, bo)
        h2, b2, notes = build_xd(info, max_blocks=4)
        sizes[len(bo)] = sizes.get(len(bo), 0) + 1
        if h2 != h or b2 != bo:
            bad += 1
            if bad < 4:
                diff = [i for i in range(192) if h2[i] != h[i]][:10]
                print('MISMATCH id %#x: header differs at %s, body equal %s' % (e.id, diff, b2 == bo))
    print('%d cries rebuilt, %d differ; body sizes %s' % (len(heads), bad, sizes))
    return 1 if bad else 0



def convert(a):
    from gen4_data import PBR
    pbr = PBR(a.pbr)
    only = set(int(x) for x in a.only.split(',')) if a.only else None
    out = os.path.join(a.out, 'cries')
    os.makedirs(out, exist_ok=True)
    files = {}
    for k in pbr.disc.files:
        if k.startswith('sound/sound/s') and k.endswith('.brstm'):
            files[int(k.split('/')[-1][1:4])] = k
    rows = []
    for dex in range(387, 494):
        if only and dex not in only:
            continue
        name = files[dex]
        info = parse_brstm(pbr.disc.file(name))
        h, b, notes = build_xd(info)
        base = os.path.join(out, '%03d_%s' % (dex, os.path.basename(name)[5:-6]))
        open(base + '.hdr', 'wb').write(h)
        open(base + '.body', 'wb').write(b)
        rows.append((dex, os.path.basename(name), info['nsamp'], '%.2f' % (info['nsamp'] / info['rate']), len(b), '; '.join(notes)))
    with open(os.path.join(out, 'cries.tsv'), 'w') as f:
        f.write('dex\tsource\tsamples\tseconds\tbody_bytes\tnotes\n')
        for r in rows:
            f.write('\t'.join(str(x) for x in r) + '\n')
    cut = [r for r in rows if r[5]]
    print('%d cries -> %s; %d cut to two slices' % (len(rows), out, len(cut)))
    return 0



def decode_dsp(chan, data, nsamp):
    """DSP-ADPCM -> s16 samples (our own decoder; the same arithmetic as the console's)."""
    import numpy as np
    out = np.zeros(nsamp, dtype=np.int32)
    c = chan['coefs']
    h1, h2 = chan['h1'], chan['h2']
    n = 0
    for f in range(0, len(data) - 7, 8):
        hdr = data[f]
        sc = 1 << (hdr & 15)
        c1, c2 = c[(hdr >> 4) * 2], c[(hdr >> 4) * 2 + 1]
        for k in range(14):
            if n >= nsamp:
                return out.astype(np.int16)
            b = data[f + 1 + k // 2]
            nib = (b >> 4) if k % 2 == 0 else (b & 15)
            if nib >= 8:
                nib -= 16
            v = ((nib * sc << 11) + 1024 + c1 * h1 + c2 * h2) >> 11
            v = max(-32768, min(32767, v))
            out[n] = v
            n += 1
            h2, h1 = h1, v
    return out.astype(np.int16)


def read_wav(path):
    import wave
    import numpy as np
    w = wave.open(path, 'rb')
    ch, rate, n = w.getnchannels(), w.getframerate(), w.getnframes()
    a = np.frombuffer(w.readframes(n), dtype=np.int16).reshape(-1, ch)
    return rate, a


def envelope(x, rate, ms=10):
    import numpy as np
    k = max(1, int(rate * ms / 1000))
    n = len(x) // k
    return np.sqrt((x[:n * k].astype(np.float64).reshape(n, k) ** 2).mean(axis=1))


def find_cry(dump_rate, dump, ref, ref_rate):
    """slide the reference's loudness envelope over the dump's; -> (start seconds, correlation)"""
    import numpy as np
    mono = dump.astype(np.float64).mean(axis=1) if dump.ndim > 1 else dump.astype(np.float64)
    if ref_rate != dump_rate:
        idx = (np.arange(int(len(ref) * dump_rate / ref_rate)) * ref_rate / dump_rate).astype(int)
        ref = ref[idx]
    ed, er = envelope(mono, dump_rate), envelope(ref.astype(np.float64), dump_rate)
    if len(ed) < len(er) + 2:
        return None, 0.0
    er0 = er - er.mean()
    best, bi = -2.0, 0
    nr = np.sqrt((er0 ** 2).sum()) + 1e-9
    for i in range(0, len(ed) - len(er)):
        w = ed[i:i + len(er)]
        wm = w - w.mean()
        c = float((wm * er0).sum() / (np.sqrt((wm ** 2).sum()) * nr + 1e-9))
        if c > best:
            best, bi = c, i
    return bi * 0.01, best


TEST_PLAN = [(125, 'ELECTABUZZ', 448), (157, 'TYPHLOSION', 445)]


def test(a):
    """Two runs of the opening simulation battle (replayed): plain, and with two cry streams replaced by Gen 4 cries."""
    import json
    import shutil
    import gen4_data
    import sheets
    work = os.path.abspath(a.out)
    os.makedirs(work, exist_ok=True)
    rows = snd_files(a.xd, os.path.join(work, 'snd_file.json'))
    d, es = archive(a.xd)
    byid = {(e.type, e.id): e for e in es}
    pbr = gen4_data.PBR(a.pbr)
    files = {int(k.split('/')[-1][1:4]): k for k in pbr.disc.files if k.startswith('sound/sound/s') and k.endswith('.brstm')}
    speciescry = {r[0]: r for r in json.load(open(a.species_cry))}
    for variant in ('plain', 'gen4'):
        run = os.path.join(work, 'run_' + variant)
        settings = os.path.join(run, 'settings')
        os.makedirs(settings, exist_ok=True)
        mods = os.path.join(settings, 'mods')
        if os.path.isdir(mods):
            shutil.rmtree(mods)
        with open(os.path.join(settings, 'settings.ini'), 'w') as f:
            f.write('format = 1\npreset = original\n')
        data = os.path.join(run, 'sim_species.json')
        json.dump({'_note': 'test only: the simulation battle\'s two Pokemon',
                   'virtual_pokemon': {'#1': {'pokemonID': TEST_PLAN[0][1]}, '#2': {'pokemonID': TEST_PLAN[1][1]}}},
                  open(data, 'w'))
        mod = os.path.join(mods, 'cry-test')
        os.makedirs(os.path.join(mod, 'data'))
        json.dump({'id': 'cry-test', 'name': 'Cry test', 'version': '0', 'game': 'GXXE01', 'applies': 'new_playthrough'},
                  open(os.path.join(mod, 'mod.json'), 'w'))
        shutil.copyfile(data, os.path.join(mod, 'data', 'sim_species.json'))
        if variant == 'gen4':
            dst = os.path.join(mod, 'fsys', 'stm_se_nakigoe_archive.fsys')
            os.makedirs(dst)
            for sp, _nm, dex in TEST_PLAN:
                _s, voice, fl, hid, bid = speciescry[sp]
                info = parse_brstm(pbr.disc.file(files[dex]))
                h, b, notes = build_xd(info)
                open(os.path.join(dst, '#%d' % byid[(20, hid)].index), 'wb').write(h)
                open(os.path.join(dst, '#%d' % byid[(21, bid)].index), 'wb').write(b)
        with open(os.path.join(settings, 'mods.ini'), 'w') as f:
            f.write('profile = default\n\n[profile default]\ncry-test = on\n')
        dump = os.path.join(run, 'out.wav').replace(os.sep, '/')
        code = sheets.run_game(a.exe, run, [
            'XD_REPLAY=' + os.path.join(sheets.REPO, 'tests', 'recordings', 'tutorial_battle_win_det.txt').replace(os.sep, '/'),
            'XD_DETERMINISTIC=1', 'XD_DISC_TIMING=fast', 'XD_TURBO=4', 'XD_AUDIO_DUMP=' + dump,
            'XD_SHOT_DIR=' + os.path.join(run, 'shots').replace(os.sep, '/'), 'XD_SHOT_FRAMES=7900,8200'],
            a.timeout, os.path.join(work, 'cache'))
        print(variant, 'exit', code)
    return 0


def analyse_envelope(a):
    """Compare the two dumps of `test`: where they differ is where the replaced cries play; measure those pieces and
    compare each with the Gen 4 original (decoded here from the brstm): length, loudness, envelope correlation."""
    import numpy as np
    import gen4_data
    work = os.path.abspath(a.out)
    r1, plain = read_wav(os.path.join(work, 'run_plain', 'out.wav'))
    r2, mod = read_wav(os.path.join(work, 'run_gen4', 'out.wav'))
    n = min(len(plain), len(mod))
    d = np.abs(plain[:n].astype(np.int32) - mod[:n].astype(np.int32)).max(axis=1)
    idx = np.nonzero(d > 0)[0]
    print('dump rate %d Hz; plain %.1f s, modified %.1f s (equal until %.2f s)' % (
        r1, len(plain) / r1, len(mod) / r2, (idx[0] / r1) if len(idx) else -1))
    segs = []
    if len(idx):
        s = p = idx[0]
        for i in idx[1:]:
            if i - p > int(0.25 * r1):
                segs.append((s, p))
                s = i
            p = i
        segs.append((s, p))
    pbr = gen4_data.PBR(a.pbr)
    files = {int(k.split('/')[-1][1:4]): k for k in pbr.disc.files if k.startswith('sound/sound/s') and k.endswith('.brstm')}
    refs = {}
    for sp, nm, dex in TEST_PLAN:
        info = parse_brstm(pbr.disc.file(files[dex]))
        ch = [decode_dsp(info['chans'][c], info['data'][c], info['nsamp']) for c in range(2)]
        refs[dex] = (np.stack(ch, axis=1), info['rate'], nm)
    print('%d differing pieces (longer than 0.25 s apart = separate):' % len(segs))
    for s, e in segs:
        seg = mod[s:e + 1].astype(np.float64)
        rms = np.sqrt((seg ** 2).mean())
        print('  %.2f-%.2f s (%.2f s long), RMS %.0f (%.1f dBFS)' % (s / r1, e / r1, (e - s) / r1, rms, 20 * np.log10(rms / 32768 + 1e-12)))
        for dex, (ref, rate, nm) in refs.items():

            lo = max(0, s - int(0.5 * r1))
            win = mod[lo:e + int(0.5 * r1)]
            pos, cor = find_cry(r2, win, ref.astype(np.float64).mean(axis=1), rate)
            rrms = np.sqrt((ref.astype(np.float64) ** 2).mean())
            if pos is not None:
                print('     Gen 4 dex %d (%s): best fit at %.2f s, envelope correlation %.3f; original %.2f s, RMS %.0f (%.1f dBFS)' % (
                    dex, nm, (lo / r1) + pos, cor, len(ref) / rate, rrms, 20 * np.log10(rrms / 32768 + 1e-12)))
                pl = plain[lo:e + int(0.5 * r1)]
                pos2, cor2 = find_cry(r1, pl, ref.astype(np.float64).mean(axis=1), rate)
                print('        the same search in the PLAIN dump: correlation %.3f' % cor2)
    return 0


def analyse(a):
    """The proof. The two dumps are the same game until the first cry that differs (the runs drift apart afterwards: the
    bigger files change load times). The difference there is  (Gen 4 cry) - (the XD cry it replaced), scaled by the
    game's cry volume: fitted by least squares against both decoded waveforms; what the fit leaves over is the
    unexplained share. Also: the cry's own length and loudness against the Gen 4 original."""
    import json
    import numpy as np
    import gen4_data
    work = os.path.abspath(a.out)
    r1, plain = read_wav(os.path.join(work, 'run_plain', 'out.wav'))
    r2, mod = read_wav(os.path.join(work, 'run_gen4', 'out.wav'))
    n = min(len(plain), len(mod))
    diff = mod[:n].astype(np.float64) - plain[:n].astype(np.float64)
    nz = np.nonzero(np.abs(diff).max(axis=1) > 0)[0]
    print('dump rate %d Hz; plain %.1f s, modified %.1f s; identical until %.2f s' % (r1, len(plain) / r1, len(mod) / r2,
                                                                                    nz[0] / r1 if len(nz) else -1))
    pbr = gen4_data.PBR(a.pbr)
    files = {int(k.split('/')[-1][1:4]): k for k in pbr.disc.files if k.startswith('sound/sound/s') and k.endswith('.brstm')}
    d, es = archive(a.xd)
    byid = {(e.type, e.id): e for e in es}
    sc = {r[0]: r for r in json.load(open(a.species_cry))}
    first = nz[0] if len(nz) else 0
    for sp, nm, dex in TEST_PLAN:
        info = parse_brstm(pbr.disc.file(files[dex]))
        G = np.stack([decode_dsp(info['chans'][c], info['data'][c], info['nsamp']) for c in range(2)], axis=1).astype(np.float64)
        _s, voice, fl, hid, bid = sc[sp]
        xi = parse_xd(noddisc.extract(d, byid[(20, hid)]), noddisc.extract(d, byid[(21, bid)]))
        X = np.stack([decode_dsp(xi['chans'][c], xi['data'][c], xi['nsamp']) for c in range(2)], axis=1).astype(np.float64)
        L = int(max(len(G), len(X)) / 32000 * r1) + int(1.2 * r1)
        s0 = max(0, first - int(0.05 * r1))
        win = diff[s0:s0 + L]
        best = None
        for lag in range(0, int(0.4 * r1), 4):
            A = np.zeros(len(win))
            B = np.zeros(len(win))
            m = min(len(G), len(win) - lag)
            if m <= 0:
                break
            A[lag:lag + m] = G[:m, 0]
            m = min(len(X), len(win) - lag)
            B[lag:lag + m] = X[:m, 0]
            M = np.stack([A, B], axis=1)
            y = win[:, 0]
            sol = np.linalg.lstsq(M, y, rcond=None)[0]
            frac = float(((y - M @ sol) ** 2).sum() / ((y ** 2).sum() + 1e-9))
            if best is None or frac < best[0]:
                best = (frac, lag, sol)
        frac, lag, sol = best
        grms = np.sqrt((G ** 2).mean())
        print('XD species %d (%s) <- Gen 4 dex %d: Gen 4 cry %.2f s (RMS %.0f), XD cry replaced %.2f s' % (
            sp, nm, dex, len(G) / 32000, grms, len(X) / 32000))
        print('   difference of the dumps = %.3f x Gen 4 cry %+.3f x XD original (starts %.3f s after the first difference); '
              'unexplained %.1f %% of the energy' % (sol[0], sol[1], lag / r1, 100 * frac))
        if frac < 0.3:
            print('   -> FOUND: the Gen 4 cry plays, %.2f s long, at %.2f of its original loudness (the game\'s cry volume), '
                  'heard RMS %.0f (%.1f dBFS)' % (len(G) / 32000, sol[0], sol[0] * grms,
                                                  20 * np.log10(abs(sol[0]) * grms / 32768 + 1e-12)))
        else:
            print('   -> not heard in this window (this species does not cry here, or later)')
    return 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('cmd', choices=['selftest', 'convert', 'test', 'analyse'])
    ap.add_argument('--exe', default='')
    ap.add_argument('--timeout', type=int, default=200)
    ap.add_argument('--species-cry', dest='species_cry', default='')
    ap.add_argument('out', nargs='?', default='')
    ap.add_argument('--xd', default='')
    ap.add_argument('--pbr', default='')
    ap.add_argument('--only', default='')
    a = ap.parse_args()
    return {'selftest': selftest, 'convert': convert, 'test': test, 'analyse': analyse}[a.cmd](a)


if __name__ == '__main__':
    sys.exit(main() or 0)
