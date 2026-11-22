"""Batch: every Battle Revolution Pokemon model that carries an XD model's name, converted, with a table.

  python tools/pbr_convert/batch.py extract <XD disc> <folder of extracted PBR files> <work folder>
        XD's battle models -> <work>/xd/<name>.pkx, PBR's first model of each archive -> <work>/pbr/<name>.sdr,
        and <work>/pairs.tsv (PBR archive, name, XD archive, XD species id)
  python tools/pbr_convert/batch.py run <work folder> [--jobs 6] [--only a,b] [--force]
        converts every pair into <work>/out/<name>.pkx and writes <work>/batch.tsv:
        name, state (converted / refused), bytes, seconds, the reasons / notes met (counted)
  python tools/pbr_convert/batch.py table <work folder>
        the table again from the per-model logs (<work>/log/<name>.txt), with the counts per reason

Everything written is game data: the work folder must be under build/.
"""
import os, re, sys, time, traceback
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(os.path.dirname(HERE), 'pbr_survey'))


def extract(xd_disc, pbr_root, work):
    import noddisc
    os.makedirs(os.path.join(work, 'xd'), exist_ok=True)
    os.makedirs(os.path.join(work, 'pbr'), exist_ok=True)
    xd = {}
    d = noddisc.Disc(xd_disc)
    for p in sorted(d.files):
        if not (p.lower().startswith('pkx_') and p.lower().endswith('.fsys')):
            continue
        data = d.file(p)
        for e in noddisc.fsys_entries(data):
            if e.type != 15:
                continue
            dst = os.path.join(work, 'xd', e.name + '.pkx')
            b = None
            if not os.path.exists(dst):
                b = noddisc.extract(data, e)
                open(dst, 'wb').write(b)
            else:
                b = open(dst, 'rb').read(0x20)
            xd[e.name] = (p, e.index, noddisc.be16(b, 0x18))
    rows = []
    for fn in sorted(os.listdir(pbr_root)):
        if not (fn.startswith('pkx_') and fn.endswith('.fsys')):
            continue
        data = open(os.path.join(pbr_root, fn), 'rb').read()
        ents = noddisc.fsys_entries(data)
        if not ents:
            rows.append((fn, '', '', '', '', 'no members'))
            continue
        try:
            b = noddisc.extract(data, ents[0])
            sk = noddisc.be32(b, noddisc.be32(b, 8))
            o = noddisc.be32(b, sk)
            name = b[o:b.index(b'\0', o)].decode('latin1')
        except Exception as ex:
            rows.append((fn, '', '', '', '', 'unreadable: %s' % ex))
            continue
        dst = os.path.join(work, 'pbr', name + '.sdr')
        if not os.path.exists(dst):
            open(dst, 'wb').write(b)
        x = xd.get(name)
        rows.append((fn, name, x[0] if x else '', x[1] if x else '', x[2] if x else '', len(ents)))
    with open(os.path.join(work, 'pairs.tsv'), 'w') as f:
        for r in rows:
            f.write('\t'.join(str(c) for c in r) + '\n')
    print('XD models %d, PBR archives %d, pairs %d' % (len(xd), len(rows), sum(1 for r in rows if r[2])))


def pairs(work):
    out = []
    for l in open(os.path.join(work, 'pairs.tsv')):
        c = l.rstrip('\n').split('\t')
        if len(c) >= 5 and c[1] and c[2]:
            out.append(c)
    return out


def one(args):
    work, name, opts = args
    import pbr_to_xd
    log_path = os.path.join(work, 'log', name + '.txt')
    lines = []
    t0 = time.time()
    state = 'converted'
    size = 0
    try:
        size = pbr_to_xd.convert_file(os.path.join(work, 'pbr', name + '.sdr'), os.path.join(work, 'xd', name + '.pkx'),
                                      os.path.join(work, 'out', name + '.pkx'), log=lines.append, **opts)
    except pbr_to_xd.KeepXD as ex:
        state = 'refused'
        lines.append('REFUSED: %s' % ex)
        out = os.path.join(work, 'out', name + '.pkx')
        if os.path.exists(out):
            os.remove(out)
    except Exception as ex:
        state = 'refused'
        lines.append('REFUSED: %s: %s' % (type(ex).__name__, ex))
        lines.append(traceback.format_exc())
    lines.append('STATE %s bytes %d seconds %.1f' % (state, size, time.time() - t0))
    with open(log_path, 'w') as f:
        f.write('\n'.join(lines) + '\n')
    return name, state


GENERAL = [
    (r'^REFUSED: \w+: (.*)$', None),
    (r'^NOTE: (.*)$', None),
]


def reason_key(text):
    """One line of a log -> the reason it stands for (numbers and names taken out), or None."""
    m = re.match(r'^(REFUSED|NOTE|WRONG): (?:\w+Error: )?(.*)$', text)
    if not m:
        return None
    kind, t = m.group(1), m.group(2)
    t = re.sub(r"'[^']*'", "'..'", t)
    t = re.sub(r'\b(node|action|animation|material|texture|joint|mesh) [A-Za-z_][\w.|:]*', r'\1 ..', t)
    t = re.sub(r'-?\d+(\.\d+)?(x\d+)?', 'N', t)
    t = re.sub(r'^[\w.|]+: ', '', t) if kind == 'NOTE' and re.match(r'^[a-z_0-9]+: node', t) else t
    return kind + ': ' + t[:110]


def table(work):
    rows = []
    counts = {}
    for c in pairs(work):
        name = c[1]
        p = os.path.join(work, 'log', name + '.txt')
        if not os.path.exists(p):
            continue
        lines = open(p).read().split('\n')
        st = [l for l in lines if l.startswith('STATE ')]
        state, size, secs = 'unknown', '0', '0'
        if st:
            w = st[-1].split()
            state, size, secs = w[1], w[3], w[5]
        reasons = {}
        for l in lines:
            k = reason_key(l)
            if k:
                reasons[k] = reasons.get(k, 0) + 1
        for k in reasons:
            counts.setdefault(k, []).append(name)
        rows.append((name, c[0], c[4], state, size, secs, ' | '.join('%s x%d' % kv for kv in sorted(reasons.items()))))
    with open(os.path.join(work, 'batch.tsv'), 'w') as f:
        f.write('name\tpbr_archive\txd_species\tstate\tbytes\tseconds\treasons\n')
        for r in rows:
            f.write('\t'.join(str(x) for x in r) + '\n')
    conv = sum(1 for r in rows if r[3] == 'converted')
    with open(os.path.join(work, 'batch_reasons.txt'), 'w') as f:
        f.write('models %d, converted %d, refused %d\n' % (len(rows), conv, len(rows) - conv))
        sizes = sorted(int(r[4]) for r in rows if r[3] == 'converted')
        if sizes:
            f.write('converted bytes: total %d, median %d, largest %d; seconds total %.0f\n' % (
                sum(sizes), sizes[len(sizes) // 2], sizes[-1], sum(float(r[5]) for r in rows)))
        for k, names in sorted(counts.items(), key=lambda kv: -len(kv[1])):
            f.write('%4d  %s   e.g. %s\n' % (len(names), k, ' '.join(names[:6])))
    print(open(os.path.join(work, 'batch_reasons.txt')).read())


def run(work, jobs, only, force, opts):
    import multiprocessing
    for d in ('out', 'log'):
        os.makedirs(os.path.join(work, d), exist_ok=True)
    todo = []
    for c in pairs(work):
        name = c[1]
        if only and name not in only:
            continue
        if not force and os.path.exists(os.path.join(work, 'log', name + '.txt')):
            continue
        todo.append((work, name, opts))
    print('to convert: %d' % len(todo))
    t0 = time.time()
    if jobs <= 1 or len(todo) <= 1:
        res = [one(t) for t in todo]
    else:
        with multiprocessing.Pool(jobs) as pool:
            res = []
            for i, r in enumerate(pool.imap_unordered(one, todo)):
                res.append(r)
                if (i + 1) % 50 == 0:
                    print('%d done, %.0f s' % (i + 1, time.time() - t0), flush=True)
    print('done in %.0f s' % (time.time() - t0))
    table(work)


def main():
    a = sys.argv[1:]
    if len(a) >= 4 and a[0] == 'extract':
        return extract(a[1], a[2], a[3])
    if len(a) >= 2 and a[0] == 'table':
        return table(a[1])
    if len(a) >= 2 and a[0] == 'run':
        jobs, only, force, opts = 6, None, False, {}
        i = 2
        while i < len(a):
            if a[i] == '--jobs':
                jobs = int(a[i + 1]); i += 1
            elif a[i] == '--only':
                only = set(a[i + 1].split(',')); i += 1
            elif a[i] == '--force':
                force = True
            elif a[i].startswith('--') and '=' in a[i]:
                k, v = a[i][2:].split('=', 1)
                opts[k] = float(v) if re.match(r'^[\d.]+$', v) else v
            i += 1
        return run(a[1], jobs, only, force, opts)
    print(__doc__)
    return 2


if __name__ == '__main__':
    sys.exit(main())
