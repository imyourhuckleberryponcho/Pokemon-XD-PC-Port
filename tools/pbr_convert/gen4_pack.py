"""The Gen 4 pack: what the game needs on disk to play the 107 Diamond/Pearl species (docs/GEN4.md, "Playable species").

  python tools/pbr_convert/gen4_pack.py make <pack folder> --xd <XD disc> --species <gen4_data.py output> --moves <gen4_moves.py mods/gen4-moves>
                                         --models <folder of the 106 converted .pkx> --cries <folder of gen4_cry.py's cries> [--faces <folder>]
  python tools/pbr_convert/gen4_pack.py check <pack folder>

<pack folder> is <data folder>/extra_assets/pbr_gen4 (a built-in extra-assets layer next to <data folder>/extra_assets/pbr, the Battle
Revolution models; the game uses it ONLY when the New Game choice "Gen 4 rules" is on). It holds, all made from the player's own discs:
  files/GSfsys.toc                the disc's table of archives with 107+ more rows (the new archives below)
  files/pkx_g4_<model>.fsys       one archive per converted model (106): the model file as its only member
  files/stm_se_nakigoe_g4.fsys    the cries (header + body streams, 2 members each)
  data/01_moves.json, 02_species.json   data-mod files ("add" blocks, docs/MODDING.md): the move and species rows
  tables.json                     rows for the tables a data mod cannot grow (snd_file, snd_data, flag_data, pokemon_pkx_data ...)
  gen4.json                       what the pack is (version, counts), read by the launcher and the game
Never in the repository (game data). The FSYS layout written here is the one every archive on the disc has (measured on
pkx_*.fsys, stm_se_nakigoe_archive.fsys, poke_face.fsys: header 0x40, a table of entry offsets at 0x60, the names, 0x70-byte entry
headers, the data on 32-byte boundaries, a 32-byte closing block ending in "FSYS").
"""
import argparse, json, os, re, shutil, struct, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(os.path.dirname(HERE), 'pbr_survey'))
import noddisc
import gen4_icon
import gen4_cry
import gen4_text_i18n as TI

PACK_VERSION = 2
PKX_GROUP_FIRST = 4000
CRY_GROUP = NAKIGOE_GROUP_OLD = 1966
PKX_RES = 0x00011E00
FIRST_NEW_SPECIES = 415
WAVE_OLD = 389
SND_DATA_COUNT, SND_FILE_COUNT, FLAG_COUNT, PKX_COUNT, FACE_COUNT = 1507, 516, 2324, 419, 838


def be32(b, o):
    return struct.unpack_from('>I', b, o)[0]


def align(v, n):
    return (v + n - 1) // n * n



def build_fsys(archive_id, entries):
    """entries: [(word0, type, name, data)] -> an archive with every member stored unpacked; a member may be
    (word0, type, name, stored bytes, unpacked size, packed) to copy an LZSS member of an existing archive as it is."""
    entries = [e if len(e) == 6 else (e[0], e[1], e[2], e[3], len(e[3]), False) for e in entries]
    n = len(entries)
    table = 0x60
    names_at = align(table + 4 * n, 16)
    names = bytearray()
    name_off = []
    for _, _, name, _, _, _ in entries:
        name_off.append(names_at + len(names))
        names += name.encode('latin1') + b'\0'
    hdrs_at = align(names_at + len(names), 16)
    data_at = align(hdrs_at + 0x70 * n, 32)
    pos = data_at
    offs = []
    for _, _, _, data, _, _ in entries:
        offs.append(pos)
        pos = align(pos + len(data), 32)
    total = pos + 0x20
    out = bytearray(total)
    out[0:4] = b'FSYS'
    struct.pack_into('>8I', out, 4, 0x201, archive_id, n, 0x80000000, 3, 0x40, data_at, total)
    struct.pack_into('>3I', out, 0x40, table, names_at, data_at)
    out[names_at:names_at + len(names)] = names
    for i, (w0, typ, name, data, size, packed) in enumerate(entries):
        h = hdrs_at + 0x70 * i
        struct.pack_into('>I', out, table + 4 * i, h)
        struct.pack_into('>10I', out, h, w0, offs[i], size, 0x80000000 if packed else 0, 0, len(data), 0, 0, typ, name_off[i])
        out[offs[i]:offs[i] + len(data)] = data
    out[total - 4:total] = b'FSYS'
    return bytes(out)


def extend_toc(toc, rows):
    """The disc's gsfsys.toc with more rows (id, name): names go behind the old ones, the group rows move up."""
    count, go = be32(toc, 8), be32(toc, 0x10)
    old_rows = toc[go:go + 16 * count]
    trailer = toc[go + 16 * count:]
    strings = bytearray(toc[0x20:go])
    base = 0x20
    new_rows = bytearray()
    for gid, name in rows:
        off = base + len(strings)
        strings += name.encode('latin1') + b'\0'
        new_rows += struct.pack('>IIII', gid, off, 0, 0)
    strings += bytes(align(len(strings), 16) - len(strings))
    out = bytearray(toc[:0x20])
    new_go = base + len(strings)
    struct.pack_into('>I', out, 8, count + len(rows))
    struct.pack_into('>I', out, 0x10, new_go)
    out += strings + old_rows + new_rows + trailer
    return bytes(out)




CATEGORY = {
    387: 'Tiny Leaf', 388: 'Grove', 389: 'Continent', 390: 'Chimp', 391: 'Playful', 392: 'Flame', 393: 'Penguin', 394: 'Penguin',
    395: 'Emperor', 396: 'Starling', 397: 'Starling', 398: 'Predator', 399: 'Plump Mouse', 400: 'Beaver', 401: 'Cricket',
    402: 'Cricket', 403: 'Flash', 404: 'Spark', 405: 'Gleam Eyes', 406: 'Bud', 407: 'Bouquet', 408: 'Head Butt', 409: 'Head Butt',
    410: 'Shield', 411: 'Shield', 412: 'Bagworm', 413: 'Bagworm', 414: 'Moth', 415: 'Tiny Bee', 416: 'Beehive', 417: 'EleSquirrel',
    418: 'Sea Weasel', 419: 'Sea Weasel', 420: 'Cherry', 421: 'Blossom', 422: 'Sea Slug', 423: 'Sea Slug', 424: 'Long Tail',
    425: 'Balloon', 426: 'Blimp', 427: 'Rabbit', 428: 'Rabbit', 429: 'Magical', 430: 'Big Boss', 431: 'Catty', 432: 'Tiger Cat',
    433: 'Bell', 434: 'Skunk', 435: 'Skunk', 436: 'Bronze', 437: 'Bronze Bell', 439: 'Mime', 440: 'Playhouse', 441: 'Music Note',
    442: 'Forbidden', 443: 'Land Shark', 444: 'Cave', 445: 'Mach', 447: 'Emanation', 448: 'Aura', 449: 'Hippo', 450: 'Heavyweight',
    451: 'Scorpion', 452: 'Ogre Scorp', 453: 'Toxic Mouth', 454: 'Toxic Mouth', 455: 'Bug Catcher', 456: 'Wing Fish', 457: 'Neon',
    458: 'Kite', 459: 'Frosted Tree', 460: 'Frosted Tree', 461: 'Sharp Claw', 462: 'Magnet Area', 463: 'Licking', 464: 'Drill',
    465: 'Vine', 466: 'Thunderbolt', 467: 'Blast', 468: 'Jubilee', 469: 'Ogre Darner', 470: 'Verdant', 471: 'Fresh Snow',
    472: 'Fang Scorp', 473: 'Twin Tusk', 474: 'Virtual', 475: 'Blade', 476: 'Compass', 477: 'Gripper', 478: 'Snow Land',
    479: 'Plasma', 480: 'Knowledge', 481: 'Emotion', 482: 'Willpower', 483: 'Temporal', 484: 'Spatial', 485: 'Lava Dome',
    486: 'Colossal', 487: 'Renegade', 488: 'Lunar', 489: 'Sea Drifter', 490: 'Seafaring', 491: 'Pitch-Black', 492: 'Gratitude',
    493: 'Alpha',
}


def species_fields(rec):
    """The record (292 bytes, GameCube order) as the data-mod layer's flat field names (docs/MODDING.md), every value of
    the arrays written out so the copied row's leftovers never show."""
    u8 = lambda o: rec[o]
    u16 = lambda o: struct.unpack_from('>H', rec, o)[0]
    f = {}
    for n, o in (('growDataId', 0), ('get', 1), ('sexRatio', 2)):
        f[n] = u8(o)
    for n, o in (('giveExp', 4), ('initFriend', 6), ('height', 8), ('weight', 10), ('numPokemon', 0x12), ('numZukan', 0x14)):
        f[n] = u16(o)
    f['zokuseiDataId'] = [u8(0x30), u8(0x31)]
    f['tokuseiDataId'] = [u8(0x32), u8(0x33)]
    f['wazaMcn'] = list(rec[0x34:0x34 + 58])
    f['teachWaza'] = list(rec[0x6E:0x6E + 12])
    f['itemDataId'] = [u16(0x7A), u16(0x7C)]
    f['kowaza'] = [u16(0x7E + 2 * i) for i in range(8)]
    f['basisStatus'] = [u16(0x8E + 2 * i) for i in range(12)]
    f['sinka.kind'] = [u8(0xA6 + 6 * i) for i in range(5)]
    f['sinka.buff'] = [u16(0xA8 + 6 * i) for i in range(5)]
    f['sinka.pokemonDataId'] = [u16(0xAA + 6 * i) for i in range(5)]
    f['waza.level'] = [u8(0xC4 + 4 * i) for i in range(20)]
    f['waza.dataId'] = [u16(0xC6 + 4 * i) for i in range(20)]
    return f


def face_k(dex):
    """The face's place among the 106 pictures (Bonsly keeps XD's): its poke_face rows are FACE_COUNT + 2k (normal) and +1 (shiny)."""
    return dex - 387 - (1 if dex > 438 else 0)


def nice_name(n):
    return n.title().replace('Mime Jr.', 'Mime Jr.')






ITEM_EVOLUTIONS = {
    'LEAFEON': (7, '98'),
    'GLACEON': (7, 'Ice_Stone'),
    'TOGEKISS': (7, 'Shiny_Stone'), 'ROSERADE': (7, 'Shiny_Stone'),
    'HONCHKROW': (7, 'Dusk_Stone'), 'MISMAGIUS': (7, 'Dusk_Stone'),
    'GALLADE': (19, 'Dawn_Stone'), 'FROSLASS': (18, 'Dawn_Stone'),
    'WEAVILE': (17, 'Razor_Claw'), 'GLISCOR': (17, 'Razor_Fang'),
    'RHYPERIOR': (6, 'Protector'), 'ELECTIVIRE': (6, 'Electirizer'), 'MAGMORTAR': (6, 'Magmarizer'),
    'PORYGON-Z': (6, 'Dubious_Disc'), 'DUSKNOIR': (6, 'Reaper_Cloth'),
}


def make_species(a, pack):
    allj = json.load(open(os.path.join(a.species, 'species_all.json')))['species']
    flags = {}
    rows = []
    add, edit = {}, {}
    k_cry = 0
    cries, pkx = [], []
    model_k = 0
    tables = []
    xd = a.standins
    for s in allj:
        e, rec = s['extra'], bytes.fromhex(s['record_hex'])
        dex = e['dex']
        fl = species_fields(rec)
        sid = e['xd_species_id']
        existing = sid < FIRST_NEW_SPECIES
        fl['mitaFlag'] = e['dex_flags_proposed']['seen']
        fl['tukamaetaFlag'] = e['dex_flags_proposed']['caught']

        if e['model'] is not None:
            row = PKX_COUNT + model_k
            pkx.append(dict(row=row, group=PKX_GROUP_FIRST + model_k, res=PKX_RES, model=e['model'], dex=dex))
            fl['pkxDataId'] = row
            model_k += 1
        else:
            fl['pkxDataId'] = 418
        if dex != 438 and a.faces and os.path.isdir(a.faces):
            fl['cgData.spriteId'] = [FACE_COUNT + 2 * face_k(dex), FACE_COUNT + 2 * face_k(dex) + 1]

        if dex != 438:
            j = len(cries)


            fl['cgData.pokebodyId'] = [gen4_icon.icon_key(face_k(dex), False), gen4_icon.icon_key(face_k(dex), True)]
            fl['nakigoeId'] = WAVE_OLD + j
            cries.append(dict(dex=dex, voice_from=fl_voice(s), n=j))
            fl['voice'] = SND_DATA_COUNT + j if not os.environ.get('GEN4_KEEP_VOICES') else fl_voice(s)
        else:
            fl['voice'] = 1269
        if existing:
            edit['#%d' % sid] = fl
            e['_key'] = '#%d' % sid
        else:
            name = nice_name(e['name'])
            d = dict(copy=e['standin_species_id'])
            d.update(fl)
            d['text'] = TI.species_text(name, name.upper(), CATEGORY[dex].upper() if dex in CATEGORY else None)
            add[name] = d
            e['_key'] = name


    by_id = {s['extra']['xd_species_id']: s['extra']['_key'] for s in allj}
    for key, d in list(add.items()) + list(edit.items()):
        d['sinka.pokemonDataId'] = [by_id.get(v, v) if v >= FIRST_NEW_SPECIES - 2 else v for v in d['sinka.pokemonDataId']]
    patches = json.load(open(os.path.join(a.species, 'existing_patches.json')))
    nm = {s['extra']['dex']: s['extra']['_key'] for s in allj}
    a.evo_lines = []
    if 'Happiny' in add:
        add['Happiny']['sinka.kind'], add['Happiny']['sinka.buff'], add['Happiny']['sinka.pokemonDataId'] = [0] * 5, [0] * 5, [0] * 5
        a.evo_lines.append('evo Happiny 17 Oval_Stone #113')
    for p in patches:
        if p['to_name'] in ITEM_EVOLUTIONS:
            kind, item = ITEM_EVOLUTIONS[p['to_name']]
            a.evo_lines.append('evo #%d %d %s %s' % (p['xd_species_id'], kind, item, nm[p['to_dex']].replace(' ', '_')))
            continue
        if p.get('slot') is None:
            continue
        d = edit.setdefault('#%d' % p['xd_species_id'], {})
        kind, buff = p['kind'], p['buff']
        if p['to_name'] in ('MAGNEZONE', 'PROBOPASS'):
            kind, buff = 7, 96
        d['sinka.kind[%d]' % p['slot']] = kind
        d['sinka.buff[%d]' % p['slot']] = buff
        d['sinka.pokemonDataId[%d]' % p['slot']] = nm[p['to_dex']]

    for p in json.load(open(os.path.join(a.species, 'existing_machines.json'))):
        d = edit.setdefault('#%d' % p['xd_species_id'], {})
        for i in p['machines']:
            d['wazaMcn[%d]' % i] = 1
    doc = {'_note': 'made by tools/pbr_convert/gen4_pack.py from the player\'s own discs: game data, never in the repository',
           'add': {'pokemon': add}, 'pokemon': edit}
    json.dump(doc, open(os.path.join(pack, 'data', '02_species.json'), 'w'), indent=1)
    return cries, pkx, allj


def fl_voice(s):
    return s['fields']['voice']


def read_standin_flags(a):
    return None


def menus_fix(pack):
    """The menu fields of the species rows (same as make) applied to a pack made before: `gen4_pack.py menus <pack>` (idempotent)."""
    path = os.path.join(pack, 'data', '02_species.json')
    doc = json.load(open(path))
    n = 0
    for name, d in doc['add']['pokemon'].items():
        if d.get('cgData.pokebodyId', [0])[0] < gen4_icon.icon_key(0, False):
            d['cgData.pokebodyId'] = [0x1E6F3200, 0x1E703200]
        dex = d.get('numPokemon')
        if dex in CATEGORY:
            d['text'] = TI.species_text(name, d.get('text', {}).get('name', name.upper()), CATEGORY[dex].upper())
        n += 1
    json.dump(doc, open(path, 'w'), indent=1)
    print('menu fields set on %d species of %s' % (n, path))
    return 0


def texts_fix(pack):
    """The other languages' words (gen4_text_i18n.py) put into the "text" blocks of a pack made before: `gen4_pack.py texts <pack>`
    (idempotent). The same blocks `make` writes."""
    import gen4_items as GI, gen4_move_text as MT
    d = os.path.join(pack, 'data')
    n = 0
    path = os.path.join(d, '01_moves.json')
    doc = json.load(open(path))
    for name, row in doc['add']['moves'].items():
        row['text'] = TI.not_built_text(name) if row['text']['doc'] == 'Not built yet: does nothing.' else TI.move_text(name)
        n += 1
    json.dump(doc, open(path, 'w'), indent=1)
    path = os.path.join(d, '00_items.json')
    doc = json.load(open(path))
    by_doc = {MT.item_doc(m): m for m in MT.DOCS}
    plain = {i[0]: i[4] for i in GI.ITEMS}
    for key, row in doc['add']['items'].items():
        if key in plain:
            row['text'] = TI.item_text(key, plain[key])
        else:
            row['text'] = TI.machine_text(key, int(key[2:]), by_doc[row['text']['doc']])
        n += 1
    json.dump(doc, open(path, 'w'), indent=1)
    path = os.path.join(d, '02_species.json')
    doc = json.load(open(path))
    for name, row in doc['add']['pokemon'].items():
        dex = row.get('numPokemon')
        row['text'] = TI.species_text(name, row['text']['name'], CATEGORY[dex].upper() if dex in CATEGORY else None)
        n += 1
    json.dump(doc, open(path, 'w'), indent=1)
    print('language texts set on %d rows of %s' % (n, pack))
    return 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('cmd', choices=['make', 'check', 'menus', 'texts'])
    ap.add_argument('pack')
    ap.add_argument('--xd', default='')
    ap.add_argument('--species', default='')
    ap.add_argument('--moves', default='')
    ap.add_argument('--models', default='')
    ap.add_argument('--cries', default='')
    ap.add_argument('--pbr', default='')
    ap.add_argument('--faces', default='')
    a = ap.parse_args()
    if a.cmd == 'check':
        return check(a)
    if a.cmd == 'menus':
        return menus_fix(a.pack)
    if a.cmd == 'texts':
        return texts_fix(a.pack)
    return make(a)


def make(a):
    pack = os.path.abspath(a.pack)
    tmp = pack + '.tmp'
    if os.path.isdir(tmp):
        shutil.rmtree(tmp)
    os.makedirs(os.path.join(tmp, 'files'))
    os.makedirs(os.path.join(tmp, 'data'))
    a.standins = None
    cries, pkx, allj = make_species(a, tmp)
    disc = noddisc.Disc(a.xd)
    toc_name = [k for k in disc.files if k.lower() == 'gsfsys.toc'][0]
    toc = disc.file(toc_name)

    toc_rows = []

    for p in pkx:
        data = open(os.path.join(a.models, p['model'] + '.pkx'), 'rb').read()
        name = 'pkx_g4_' + p['model']
        fs = build_fsys(p['group'], [(p['res'], 15, p['model'], data)])
        open(os.path.join(tmp, 'files', name + '.fsys'), 'wb').write(fs)
        toc_rows.append((p['group'], name))
        p['archive'] = name

    entries = []
    for c in cries:
        base = [f for f in os.listdir(a.cries) if f.startswith('%03d_' % c['dex'])]
        hdr = open(os.path.join(a.cries, [f for f in base if f.endswith('.hdr')][0]), 'rb').read()
        body = open(os.path.join(a.cries, [f for f in base if f.endswith('.body')][0]), 'rb').read()
        c['wave'] = gen4_cry.wave_disp(hdr, body)
        c['hdr_id'] = 0x1000 + 2 * c['n']
        c['body_id'] = 0x1001 + 2 * c['n']
        entries.append(((c['hdr_id'] << 16) | (20 << 9), 20, '(null)', hdr))
        entries.append(((c['body_id'] << 16) | (21 << 9), 21, '(null)', body))



    cry_name = [k for k in disc.files if k.lower() == 'stm_se_nakigoe_archive.fsys'][0]
    cd = disc.file(cry_name)
    old = noddisc.fsys_entries(cd)
    used = {e.word0 >> 16 for e in old}
    ents = [(e.word0, e.type, e.name, noddisc.stored(cd, e), e.size, e.packed) for e in old]
    for c in cries:
        assert c['hdr_id'] not in used and c['body_id'] not in used, 'cry member id clash'

    hdrs = [e for e in ents if e[1] == 20] + [e for e in entries if e[1] == 20]
    bods = [e for e in ents if e[1] == 21] + [e for e in entries if e[1] == 21]
    rest = [e for e in ents if e[1] not in (20, 21)]
    open(os.path.join(tmp, 'files', cry_name), 'wb').write(build_fsys(be32(cd, 8), hdrs + bods + rest))
    open(os.path.join(tmp, 'files', toc_name), 'wb').write(extend_toc(toc, toc_rows))

    faces = []
    face_rows = []
    if a.faces and os.path.isdir(a.faces):
        pf_name = [k for k in disc.files if k.lower() == 'poke_face.fsys'][0]
        pf = disc.file(pf_name)
        old = noddisc.fsys_entries(pf)
        ents = [(e.word0, e.type, e.name, noddisc.stored(pf, e), e.size, e.packed) for e in old]
        for dex in sorted(s['extra']['dex'] for s in allj if s['extra']['dex'] != 438):
            k = face_k(dex)
            f = [x for x in os.listdir(a.faces) if x.startswith('%03d_' % dex) and x.endswith('.face')]
            if not f:
                continue
            data = open(os.path.join(a.faces, f[0]), 'rb').read()
            fid = 0x2800 + k
            ents.append(((fid << 16) | (9 << 9), 9, 'g4face%03d' % dex, data))
            faces.append((dex, fid))
        open(os.path.join(tmp, 'files', pf_name), 'wb').write(build_fsys(be32(pf, 8), ents))

    icons = gen4_icon.Icons(a.pbr, a.xd)
    ents = [(e.word0, e.type, e.name, noddisc.stored(icons.dance, e), e.size, e.packed) for e in icons.dance_entries]
    used = {e[0] >> 16 for e in ents}
    for dex in sorted(s['extra']['dex'] for s in allj if s['extra']['dex'] != 438):
        for m in icons.members(dex, face_k(dex)):
            assert m[0] >> 16 not in used, 'poke_dance member id clash'
            ents.append(m)
    open(os.path.join(tmp, 'files', icons.dance_name), 'wb').write(build_fsys(be32(icons.dance, 8), ents))


    import gen4_spots
    os.makedirs(os.path.join(tmp, 'optional'))
    by_name = {s['extra']['_key']: s['extra']['xd_species_id'] for s in allj}
    spots, spot_rows = gen4_spots.files(by_name)
    json.dump(spots, open(os.path.join(tmp, 'optional', 'spots.json'), 'w'), indent=1)
    open(os.path.join(tmp, 'optional', 'spots_tables.txt'), 'w').write('\n'.join(['format 1'] + spot_rows) + '\n')

    shutil.copy(os.path.join(a.moves, 'data', 'moves.json'), os.path.join(tmp, 'data', '01_moves.json'))

    import gen4_data as G
    x = G.XD(a.xd, a.species, G.DEFAULT_EXE)
    seen_ref, caught_ref = be32(x.rec(1), 0x24), be32(x.rec(1), 0x28)
    out = ['format 1', 'pkxcount %d' % PKX_COUNT]
    for c in cries:
        n = c['n']
        if os.environ.get('GEN4_COPY_FILES'):
            import subprocess, re
            t = subprocess.run([os.environ['GEN4_COPY_FILES'], a.xd, '--show', 'snd_data', '#%d' % c['voice_from']],
                               capture_output=True, text=True).stdout
            out.append('row snd_file %d 1 %s' % (SND_FILE_COUNT + n, re.search(r'file\s+(\d+)', t).group(1)))
        else:
            out.append('row snd_file %d 1 -1 0:4:%d 4:4:%d 8:4:%d' % (SND_FILE_COUNT + n, CRY_GROUP, (c['hdr_id'] << 16) | (20 << 9), (c['body_id'] << 16) | (21 << 9)))
        out.append('row snd_data %d 1 %d 6:2:%d' % (SND_DATA_COUNT + n, c['voice_from'], SND_FILE_COUNT + n))

    out.append('row flag_data %d 107 %d' % (FLAG_COUNT, seen_ref))
    out.append('row flag_data %d 107 %d' % (FLAG_COUNT + 107, caught_ref))
    for p in pkx:
        out.append('pkx %d %d %d' % (p['row'], p['group'], p['res']))
    for c in cries:
        out.append('wave %d %s' % (c['dex'], c['wave'].hex()))
    for k, (dex, fid) in enumerate(faces):
        k = face_k(dex)
        out.append('row poke_face %d 1 -1 0:1:64 4:4:%d' % (FACE_COUNT + 2 * k, (fid << 16) | 0x1200))
        out.append('row poke_face %d 1 -1 0:1:192 4:4:%d' % (FACE_COUNT + 2 * k + 1, (fid << 16) | 0x1200))
        face_rows.append((dex, FACE_COUNT + 2 * k, FACE_COUNT + 2 * k + 1))
    for dex, n, sh in face_rows:
        out.append('face %d %d %d' % (dex, n, sh))
    for s in allj:
        out.append('species %d %d %s' % (s['extra']['dex'], s['extra']['xd_species_id'], s['extra']['_key'].replace(' ', '_')))
    machines = make_machines(a, x, allj)
    out.extend(make_items(a, tmp, machines))
    open(os.path.join(tmp, 'machines.txt'), 'w', newline='').write(machines[2])
    open(os.path.join(tmp, 'tables.txt'), 'w').write(chr(10).join(out) + chr(10))


    leaf = [s['extra']['_key'] for s in allj if s['extra']['dex'] == 470][0].replace(' ', '_')
    ice = [s['extra']['_key'] for s in allj if s['extra']['dex'] == 471][0].replace(' ', '_')
    evo = ['format 1', 'cord Linking_Cord', 'eevee %s 98 %s Ice_Stone' % (leaf, ice)] + a.evo_lines
    open(os.path.join(tmp, 'evolutions.txt'), 'w').write(chr(10).join(evo) + chr(10))


    rz = ['format 1']
    nid = FIRST_NEW_SPECIES
    legend = {480, 481, 482, 483, 484, 485, 486, 487, 488, 490, 491, 492, 493}
    for s in allj:
        e, rec = s['extra'], bytes.fromhex(s['record_hex'])
        if e['xd_species_id'] < FIRST_NEW_SPECIES:
            continue
        f = species_fields(rec)
        ev = [e2 if e2 < 1000 else 0 for e2 in f['sinka.pokemonDataId']]
        rz.append('sp %d %d %d %s | %s | %s | %s | %s | %s' % (
            nid, e['dex'], 1 if e['dex'] in legend else 0, ' '.join(map(str, f['basisStatus'][:6])),
            ' '.join(map(str, f['zokuseiDataId'])), ' '.join(map(str, f['tokuseiDataId'])),
            ' '.join(map(str, f['sinka.pokemonDataId'])),
            ' '.join(map(str, f['waza.level'])), ' '.join(map(str, f['waza.dataId']))))
        e['_rzid'] = nid
        nid += 1
    open(os.path.join(tmp, 'randomizer.txt'), 'w').write(chr(10).join(rz) + chr(10))
    json.dump(dict(id='battle-revolution-gen4', version=PACK_VERSION, species=len(allj), models=len(pkx), cries=len(cries),
                   game='GXXE01', generated_from='RPBE01'), open(os.path.join(tmp, 'gen4.json'), 'w'), indent=1)
    old = pack + '.old'
    if os.path.isdir(old):
        shutil.rmtree(old)
    if os.path.isdir(pack):
        os.rename(pack, old)
    os.rename(tmp, pack)
    if os.path.isdir(old):
        shutil.rmtree(old, ignore_errors=True)
    print('pack %s: %d species, %d models, %d cries' % (pack, len(allj), len(pkx), len(cries)))
    return 0


def make_machines(a, x, allj):
    """The extra machines (gen4_machines.py; docs/GEN4.md "Extra machines (TMs)"): (item rows, coupon desk names, machines.txt)."""
    import gen4_data as G
    import gen4_machines
    return gen4_machines.from_discs(G.PBR(a.pbr), x, allj)


def make_items(a, pack, machines=((), (), '')):
    """The Gen 4 items (gen4_items.py; docs/GEN4.md "Items"): data/00_items.json, and the tables.txt lines of the shops' new lists.
    `machines` = make_machines()'s answer: its item rows and desk names follow the evolution items in the same file and lists."""
    import tempfile
    import gen4_data as G
    import gen4_items
    settings = tempfile.mkdtemp(prefix='gen4_items_')
    try:
        first_of, goods_at = gen4_items.disc_reader(G.DEFAULT_EXE, a.xd, settings)
        text, lines = gen4_items.files(first_of, goods_at, machines[0], machines[1])
    finally:
        shutil.rmtree(settings, ignore_errors=True)
    open(os.path.join(pack, 'data', '00_items.json'), 'w').write(text)
    return lines


def check(a):
    pack = a.pack
    bad = 0
    for f in sorted(os.listdir(os.path.join(pack, 'files'))):
        if not f.lower().endswith('.fsys'):
            continue
        d = open(os.path.join(pack, 'files', f), 'rb').read()
        es = noddisc.fsys_entries(d)
        ok = bool(es) and be32(d, 0x20) == len(d) and d[-4:] == b'FSYS' and be32(d, 0xC) == len(es)
        for e in es:
            ok = ok and e.offset + (e.packed_size if e.packed else e.size) <= len(d) - 0x20
        if not ok:
            print('BAD', f)
            bad += 1
        elif not f.startswith('pkx_g4_'):
            print('ok', f, len(es), 'members')
    toc = open(os.path.join(pack, 'files', 'GSfsys.toc'), 'rb').read()
    go, n = be32(toc, 0x10), be32(toc, 8)
    print('toc: %d rows, last %s' % (n, toc[be32(toc, go + 16 * (n - 1) + 4):].split(bytes(1))[0]))
    print('checked; bad:', bad)
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
