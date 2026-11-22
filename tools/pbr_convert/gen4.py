"""Fourth-generation Pokemon: Battle Revolution models that have NO XD model of the same name (docs/GEN4.md).

  python tools/pbr_convert/gen4.py plan  <work folder> [--src <pairs folder>]
        features of every PBR model, the stand-in chosen for each Gen 4 model -> <work>/plan.tsv
  python tools/pbr_convert/gen4.py run   <work folder> [--src <pairs folder>] [--jobs 6] [--only a,b] [--force]
        converts them into <work>/out/<name>.pkx (+ <work>/log/<name>.txt), then <work>/batch.tsv
  python tools/pbr_convert/gen4.py hosts <work folder> --disc <XD disc>
        <work>/pairs.tsv for sheets.py / battle.py: each Gen 4 model on a host XD species' file (test mod)

<src> (default build/pbrconv2) is batch.py's work folder: pbr/<name>.sdr, xd/<name>.pkx, pairs.tsv, batch.tsv.
Everything written is game data: the work folder must be under build/.

The idea (the rule is explained in docs/GEN4.md, "The no-counterpart path"): pbr_to_xd.convert() needs an XD
model for four things, and a model without one borrows them from a STAND-IN, an XD Pokemon of the same body plan:
  1. the file's container (header, move sections, animation slots, particle bank): the stand-in's;
  2. which PBR action plays in which slot: the stand-in's own pair says which NAMED action plays in each slot
     (bulbasaur's slot 3 = "tackle"); the new model plays its action of that name (else the role lists);
  3. the attachment joints (hands, feet, mouth, tail...): the stand-in's XD joint -> its nearest PBR effect point
     (ef_*, fixed names in every PBR model); the new model takes the point of that name (else the one at the same
     place in the body);
  4. the size: none needed. PBR's units are XD's (median XD / PBR idle box over the shared models 1.005, 90 % of
     them within 3.5 %), so PBR's own size is used as it is. The stand-in is chosen near the model's size anyway.
"""
import json, math, os, re, sys, time, traceback
HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(os.path.dirname(HERE), 'pbr_survey'))

DEFAULT_SRC = os.path.join(os.path.dirname(os.path.dirname(REPO)) if False else REPO, 'build', 'pbrconv2')

NOT_STANDIN = {'metamon', 'migawari', 'eipam', 'tamago'}
GEN4_FIRST, GEN4_LAST = 387, 493


def src_default():
    p = ''
    return p if os.path.isdir(p) else DEFAULT_SRC


def read_pairs(src):
    """[(dex, name, xd_name_or_'')] from pairs.tsv; dex = the PBR archive's number (the national number)."""
    out = []
    for l in open(os.path.join(src, 'pairs.tsv')):
        c = l.rstrip('\n').split('\t')
        if len(c) >= 2 and c[1] and c[0].startswith('pkx_') and c[0][4:7].isdigit():
            c[1] = c[1].split(':')[0]
            out.append((int(c[0][4:7]), c[1], c[2] if len(c) > 2 else '', c))
    return out


def gen4_names(src):
    """Models of dex 387..493 with no XD file (Bonsly's 'usohachi' has one: it is among the 388 pairs)."""
    return [(d, n) for d, n, x, _ in read_pairs(src) if GEN4_FIRST <= d <= GEN4_LAST and not x]


def sdr_path(src, work, name):
    """A model's file: the source folder's, or <work>/pbr/ when it has to be put there (a member whose name holds a
    colon, Froslass's, came out of batch.py as an NTFS stream, not a file)."""
    own = os.path.join(os.path.dirname(work) if os.path.basename(work).startswith('cache') else work, 'pbr', name + '.sdr')
    return own if os.path.exists(own) else os.path.join(src, 'pbr', name + '.sdr')


def tokens(name):
    n = name.lower()
    n = re.sub(r'^(left|right|l|r)[_]?', '', n)
    n = re.sub(r'(_l|_r|_left|_right)$', '', n)
    return re.sub(r'\d+$', '', n)


def features(sdr_path):
    """What the choice of stand-in looks at: effect points, bone words, action names, native size and proportions."""
    import numpy as np
    import pbr_eval, sdr_read
    sdr = sdr_read.read(open(sdr_path, 'rb').read())
    ev = pbr_eval.Eval(sdr, 'joint8_under_joint')
    acts = [a[0] for a in sdr.actions if not a[0].startswith('tx_')]
    idle = next((i for i, a in enumerate(sdr.actions) if a[0] == 'wait'), None)
    if idle is None:
        idle = next((i for i, a in enumerate(sdr.actions) if a[0] == 'kuchu_wait'), 0)
    po = ev.sample(idle)
    bo = pbr_eval.boxes(ev.points(np.eye(4)[None, None] @ ev.world(po.local), po.visible))
    ext = [float(x) for x in (bo[:, 3:] - bo[:, :3]).mean(axis=0)]
    return {'ef': sorted(set(n.name for n in sdr.nodes if n.name.startswith('ef_'))),
            'bones': sorted(set(tokens(n.name) for n in sdr.nodes if n.kind == 2)),
            'actions': acts, 'ext': ext, 'nodes': len(sdr.nodes)}


def jac(a, b):
    a, b = set(a), set(b)
    return len(a & b) / float(len(a | b)) if a | b else 1.0


def score(g, c):
    """Higher = a better stand-in. Body plan from the shared effect points and bone words, the actions the stand-in's
    slots are filled from, then size and proportions (log distances)."""
    ge, ce = g['ext'], c['ext']
    gh, ch = max(ge), max(ce)
    size = abs(math.log(max(gh, 1e-3) / max(ch, 1e-3)))
    asp = sum(abs(math.log(max(x, 1e-3) / max(sum(ge), 1e-3)) - math.log(max(y, 1e-3) / max(sum(ce), 1e-3))) for x, y in zip(ge, ce))
    air = (('kuchu_wait' in g['actions']) != ('kuchu_wait' in c['actions']))
    return (3.0 * jac(g['ef'], c['ef']) + 2.0 * jac(g['bones'], c['bones']) + 2.0 * jac(g['actions'], c['actions'])
            - 1.5 * size - 1.0 * asp - 1.5 * air)





PLAN_OF = {}
for _plan, _dex in {
    'biped': '390-395 400-402 407-409 419 424 427 428 439-440 443-448 453 454 459-461 463-467 475 484 486',
    'quad': '387-389 399 403-405 410 411 417 418 431 432 434 435 449 450 470 471 473 483 485 492 493',
    'flyer': '396-398 414-416 430 441 468 469 472 488',
    'floater': '425 426 429 433 436 437 442 462 474 476-482 491',
    'arthropod': '451 452',
    'fish': '456-458 489 490',
    'blob': '406 412 413 420-423 455',
    'serpent': '487',
}.items():
    for _r in _dex.split():
        _a, _, _b = _r.partition('-')
        for _d in range(int(_a), int(_b or _a) + 1):
            PLAN_OF[_d] = _plan
_STANDINS = {
    'biped': '7-9 25 26 56 57 62 66-68 106 107 124-126 143 150 158-160 214 237 248 254-260 280-282 287-289 296 297 307 308',
    'quad': '1-3 58 59 77 78 111 112 128 133-136 152-154 196 197 216 217 231 232 241 243-245 322 323 335',
    'flyer': '12 16-18 21 22 41 42 49 142 144-146 163 164 169 193 198 267 269 276 277 284 333 334 227 249 250 373 330',
    'floater': '92 93 200 201 355 356 358 337 338 343 344 351 109 110 81 82 374-376 385 386 151 251',
    'arthropod': '14 15 46 47 98 99 123 127 167 168 204 205 213 341 342 347 348',
    'fish': '118-121 129 170 171 211 223 224 318-321 339 349 350 366-370 72 73 90 91 116 117 226 340',
    'blob': '35 39 40 43-45 50 51 60 61 69-71 74 88 89 100 101 102 113 114 174 175 183 184 189 191 192 194 195 273-275 285 286 '
            '298 316 317 331 332',
    'serpent': '23 24 95 130 147 148 208 336',
}
PLAN_STANDINS = {}
for _plan, _dex in _STANDINS.items():
    PLAN_STANDINS[_plan] = []
    for _r in _dex.split():
        _a, _, _b = _r.partition('-')
        PLAN_STANDINS[_plan] += list(range(int(_a), int(_b or _a) + 1))

OVERRIDE = {}


def load_all_features(src, cache):
    p = os.path.join(cache, 'features.json')
    data = json.load(open(p)) if os.path.exists(p) else {}
    changed = False
    for d, n, x, _ in read_pairs(src):
        if n in data:
            continue
        try:
            data[n] = features(sdr_path(src, cache, n))
            changed = True
        except Exception as ex:
            data[n] = {'error': repr(ex)}
    if changed:
        json.dump(data, open(p, 'w'))
    return data


def converted_standins(src):
    ok = set()
    for l in open(os.path.join(src, 'batch.tsv')).read().split('\n')[1:]:
        c = l.split('\t')
        if len(c) > 4 and c[3] == 'converted':
            ok.add(c[0])
    return ok


def choose(name, feats, cands, dex, by_dex):
    """cands: converted stand-in names; by_dex: national number -> name."""
    if dex in OVERRIDE:
        n = by_dex[OVERRIDE[dex]]
        return n, score(feats[name], feats[n])
    g = feats[name]
    allowed = [by_dex[d] for d in PLAN_STANDINS[PLAN_OF[dex]] if d in by_dex and by_dex[d] in cands]
    best = max(allowed, key=lambda c: score(g, feats[c]))
    return best, score(g, feats[best])


def roles_of(src, standin, cache):
    """The stand-in's own pair, run as far as the attachment joints: slot -> action name, XD joint -> role."""
    p = os.path.join(cache, 'standin_%s.json' % standin)
    if os.path.exists(p):
        d = json.load(open(p))
        return {'pick_names': {int(k): v for k, v in d['pick_names'].items()}, 'roles': {int(k): v for k, v in d['roles'].items()},
                'pick_times': {int(k): v for k, v in d['pick_times'].items()}}
    import hsd_read, pbr_to_xd, sdr_read
    sdr = sdr_read.read(open(os.path.join(src, 'pbr', standin + '.sdr'), 'rb').read())
    xp, xm, _ = hsd_read.read_pkx(open(os.path.join(src, 'xd', standin + '.pkx'), 'rb').read(), meshes=False)
    st = {'stop_roles': True}
    try:
        pbr_to_xd.convert(sdr, xp, xm, 0.1, lambda s: None, stats=st)
    except pbr_to_xd.StopConversion:
        pass
    out = {'pick_names': st['pick_names'], 'roles': st['roles'], 'pick_times': st['pick_times']}
    json.dump(out, open(p, 'w'))
    return out


def make_plan(src, work):
    cache = os.path.join(work, 'cache_gen4')
    os.makedirs(cache, exist_ok=True)
    feats = load_all_features(src, cache)
    cands = [n for n in converted_standins(src) if n not in NOT_STANDIN and 'actions' in feats.get(n, {})]
    by_dex = {d: n for d, n, x, _ in read_pairs(src) if x}
    rows = []
    for d, n in gen4_names(src):
        if 'actions' not in feats.get(n, {}):
            rows.append((d, n, '', 0.0))
            continue
        s, sc = choose(n, feats, cands, d, by_dex)
        rows.append((d, n, s, sc))
    with open(os.path.join(work, 'plan.tsv'), 'w') as f:
        for d, n, s, sc in rows:
            f.write('%d\t%s\t%s\t%.2f\t%.2f\t%.2f\n' % (d, n, s, sc, max(feats[n]['ext']) if 'ext' in feats[n] else 0,
                                                       max(feats[s]['ext']) if s else 0))
    return rows


def check_percent(lines):
    for l in lines:
        m = re.match(r'CHECK: converted pose.*distance ([\d.]+) %', l)
        if m:
            return float(m.group(1))
    return None


def one(args):
    src, work, dex, name, standin = args
    import pbr_to_xd
    cache = os.path.join(work, 'cache_gen4')
    lines, t0, state, size = [], time.time(), 'converted', 0
    out = os.path.join(work, 'out', name + '.pkx')
    try:
        g4 = roles_of(src, standin, cache)
        lines.append('stand-in: %s' % standin)
        size = pbr_to_xd.convert_file(sdr_path(src, work, name), os.path.join(src, 'xd', standin + '.pkx'), out,
                                      log=lines.append, gen4=g4)
        err = check_percent(lines)
        if err is not None and err > 3.0:


            lines2 = []
            out2 = out + '.ct'
            size2 = pbr_to_xd.convert_file(sdr_path(src, work, name), os.path.join(src, 'xd', standin + '.pkx'), out2,
                                           log=lines2.append, gen4=dict(g4, comp_t=True))
            err2 = check_percent(lines2)
            if err2 is not None and err2 < err:
                os.replace(out2, out)
                lines = lines2 + ['INFO: scale compensation on translations too: pose error %.2f %% (was %.2f %%)' % (err2, err)]
                size = size2
            else:
                os.remove(out2)
                lines.append('NOTE: pose error %.2f %%; the other compensation rule gave %s: kept' % (err, err2))
    except Exception as ex:
        state = 'refused'
        lines.append('REFUSED: %s: %s' % (type(ex).__name__, ex))
        lines.append(traceback.format_exc())
        if os.path.exists(out):
            os.remove(out)
    lines.append('STATE %s bytes %d seconds %.1f' % (state, size, time.time() - t0))
    open(os.path.join(work, 'log', name + '.txt'), 'w').write('\n'.join(lines) + '\n')
    return name, state


def run(src, work, jobs, only, force):
    import multiprocessing
    for d in ('out', 'log'):
        os.makedirs(os.path.join(work, d), exist_ok=True)
    plan = make_plan(src, work)

    cache = os.path.join(work, 'cache_gen4')
    for s in sorted(set(p[2] for p in plan if p[2])):
        roles_of(src, s, cache)
    todo = [(src, work, d, n, s) for d, n, s, _ in plan if s and (not only or n in only)
            and (force or not os.path.exists(os.path.join(work, 'log', n + '.txt')))]
    print('to convert: %d' % len(todo), flush=True)
    t0 = time.time()
    if jobs <= 1:
        res = [one(t) for t in todo]
    else:
        with multiprocessing.Pool(jobs) as pool:
            res = list(pool.imap_unordered(one, todo))
    print('done in %.0f s; refused: %s' % (time.time() - t0, [n for n, s in res if s != 'converted']))
    rows = ['name\tdex\tstandin\tstate\tbytes\tnotes']
    for d, n, s, _ in plan:
        lp = os.path.join(work, 'log', n + '.txt')
        if not os.path.exists(lp):
            continue
        txt = open(lp).read().split('\n')
        st = [l for l in txt if l.startswith('STATE ')]
        w = st[-1].split() if st else ['STATE', 'unknown', 'bytes', '0']
        notes = sum(1 for l in txt if l.startswith(('NOTE', 'WRONG')))
        wrong = [l for l in txt if l.startswith(('WRONG', 'REFUSED'))]
        rows.append('%s\t%d\t%s\t%s\t%s\t%d notes; %s' % (n, d, s, w[1], w[3], notes, ' | '.join(wrong)[:200]))
    open(os.path.join(work, 'batch.tsv'), 'w').write('\n'.join(rows) + '\n')


def hosts(work, src, disc):
    """pairs.tsv for sheets.py: model name, XD archive + entry of the HOST species, its XD species number.
    Host = the stand-in when it is free (its file already has the right layout), else another species of the same
    pair list; one host per model, species up to 251 only (their number in the game = the national number)."""
    plan = [l.rstrip('\n').split('\t') for l in open(os.path.join(work, 'plan.tsv'))]
    pairs = {n: c for d, n, x, c in read_pairs(src) if x}
    used, rows = set(), []
    order = sorted(plan, key=lambda r: -float(r[3]))
    pool = [n for d, n, x, c in read_pairs(src) if x and d <= 251 and n not in NOT_STANDIN]
    for r in order:
        n, s = r[1], r[2]
        h = s if (s in pairs and int(pairs[s][0][4:7]) <= 251 and s not in used) else None
        if h is None:
            import gen4 as me
            feats = json.load(open(os.path.join(work, 'cache_gen4', 'features.json')))
            free = [p for p in pool if p not in used]
            h = max(free, key=lambda c: score(feats[n], feats[c]))
        used.add(h)
        c = pairs[h]
        rows.append((n, c[0], c[2], c[3], c[4] if c[4] not in ('', '0') else str(int(c[0][4:7]))))
    with open(os.path.join(work, 'pairs.tsv'), 'w') as f:
        for n, a, xa, xi, sp in rows:
            f.write('\t'.join([a, n, xa, xi, sp, '2']) + '\n')
    print('hosts written:', len(rows))


def main():
    a = sys.argv[1:]
    if len(a) < 2:
        print(__doc__)
        return 2
    cmd, work = a[0], os.path.abspath(a[1])
    src, jobs, only, force, disc = src_default(), 6, None, False, None
    i = 2
    while i < len(a):
        if a[i] == '--src':
            src = a[i + 1]; i += 1
        elif a[i] == '--jobs':
            jobs = int(a[i + 1]); i += 1
        elif a[i] == '--only':
            only = set(a[i + 1].split(',')); i += 1
        elif a[i] == '--force':
            force = True
        elif a[i] == '--disc':
            disc = a[i + 1]; i += 1
        i += 1
    os.makedirs(work, exist_ok=True)
    if cmd == 'plan':
        for r in make_plan(src, work):
            print(r)
        return 0
    if cmd == 'run':
        return run(src, work, jobs, only, force)
    if cmd == 'hosts':
        return hosts(work, src, disc)
    print(__doc__)
    return 2


if __name__ == '__main__':
    sys.exit(main())
