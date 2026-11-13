"""Read-only disc reader for surveys through the port's own disc library (nod.dll, the one xd.exe uses).

Opens whatever nod opens (GameCube or Wii; .iso .ciso .wbfs .rvz ...), lists the data partition's files and
reads them. Also: FSYS archive entries and LZSS (layouts as in engine/formats/lzss_fsys.c).
No key or game data is in this file; Wii decryption is whatever nod.dll does by itself.

    python tools/pbr_survey/noddisc.py <disc> info            # header, partitions, main.dol hash
    python tools/pbr_survey/noddisc.py <disc> list            # every file: size, path
    python tools/pbr_survey/noddisc.py <disc> extract <dir>   # all files into <dir> (game data: under build/ only)

NOD_DLL=<path> names the library (default: build/release/bin/nod.dll of the main checkout on Windows).
The library's file name per system is LIB_NAME: nod.dll (Windows), libnod.so (Linux), libnod.dylib (Mac).
"""
import ctypes, hashlib, os, struct, sys

# WHY a per-system name: the Linux and Mac packages ship libnod.so / libnod.dylib (what Aurora's nod package
# builds there), not nod.dll; pbr_models looks for LIB_NAME next to itself.
LIB_NAME = 'nod.dll' if sys.platform == 'win32' else 'libnod.dylib' if sys.platform == 'darwin' else 'libnod.so'
DEFAULT_DLL = LIB_NAME if sys.platform == 'win32' else LIB_NAME


def be32(b, o=0):
    return struct.unpack_from('>I', b, o)[0]


def be16(b, o=0):
    return struct.unpack_from('>H', b, o)[0]


class Blob(ctypes.Structure):
    _fields_ = [('data', ctypes.POINTER(ctypes.c_ubyte)), ('size', ctypes.c_size_t)]

    def bytes(self):
        return ctypes.string_at(self.data, self.size) if self.data and self.size else b''


class Meta(ctypes.Structure):
    _fields_ = [(n, Blob) for n in ('boot', 'bi2', 'apploader', 'dol', 'fst', 'ticket', 'tmd', 'cert', 'h3')]


class PartInfo(ctypes.Structure):
    _fields_ = [('index', ctypes.c_uint32), ('kind', ctypes.c_uint32), ('data_size', ctypes.c_uint64)]


class Disc:
    def __init__(self, path):
        self.nod = ctypes.CDLL(os.environ.get('NOD_DLL', DEFAULT_DLL))
        n = self.nod
        n.nod_error_message.restype = ctypes.c_char_p
        n.nod_read.restype = ctypes.c_int64
        n.nod_read.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t]
        n.nod_free.argtypes = [ctypes.c_void_p]
        n.nod_disc_size.restype = ctypes.c_uint64
        n.nod_disc_size.argtypes = [ctypes.c_void_p]
        n.nod_disc_partitions.restype = ctypes.c_size_t
        n.nod_disc_partitions.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t]
        n.nod_partition_is_wii.restype = ctypes.c_bool
        n.nod_partition_is_wii.argtypes = [ctypes.c_void_p]
        n.nod_partition_meta.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
        n.nod_partition_open_file.argtypes = [ctypes.c_void_p, ctypes.c_uint32, ctypes.c_void_p]
        n.nod_disc_open_partition_kind.argtypes = [ctypes.c_void_p, ctypes.c_uint32, ctypes.c_void_p, ctypes.c_void_p]
        self.disc = ctypes.c_void_p()
        if n.nod_disc_open(path.encode('utf-8'), None, ctypes.byref(self.disc)) != 0:
            raise OSError('nod_disc_open: %s' % n.nod_error_message())
        self.part = ctypes.c_void_p()
        if n.nod_disc_open_partition_kind(self.disc, 0, None, ctypes.byref(self.part)) != 0:
            raise OSError('data partition: %s' % n.nod_error_message())
        self.meta = Meta()
        if n.nod_partition_meta(self.part, ctypes.byref(self.meta)) != 0:
            raise OSError('partition meta: %s' % n.nod_error_message())
        self.boot = self.meta.boot.bytes()
        self.game_id = self.boot[:6].decode('latin1')
        self.is_wii = bool(n.nod_partition_is_wii(self.part))
        self.files = {}   # path -> (fst index, size)
        fst = self.meta.fst.bytes()
        count = be32(fst, 8)
        names = fst[count * 12:]
        dir_end = [(count, '')]
        i = 1
        while i < count:
            while dir_end and i >= dir_end[-1][0]:
                dir_end.pop()
            prefix = dir_end[-1][1] if dir_end else ''
            w0, a, b = struct.unpack_from('>III', fst, i * 12)
            o = w0 & 0xFFFFFF
            nm = names[o:names.index(b'\0', o)].decode('latin1')
            if w0 >> 24:
                dir_end.append((b, prefix + nm + '/'))
            else:
                self.files[prefix + nm] = (i, b)
            i += 1

    def partitions(self):
        arr = (PartInfo * 16)()
        k = self.nod.nod_disc_partitions(self.disc, arr, 16)
        return [(arr[j].index, arr[j].kind, arr[j].data_size) for j in range(min(k, 16))]

    def dol(self):
        return self.meta.dol.bytes()

    def file(self, path):
        index, size = self.files[path]
        h = ctypes.c_void_p()
        if self.nod.nod_partition_open_file(self.part, index, ctypes.byref(h)) != 0:
            raise OSError('open %s: %s' % (path, self.nod.nod_error_message()))
        buf = ctypes.create_string_buffer(size or 1)
        done = 0
        while done < size:
            got = self.nod.nod_read(h, ctypes.byref(buf, done), size - done)
            if got <= 0:
                raise OSError('read %s' % path)
            done += got
        self.nod.nod_free(h)
        return buf.raw[:size]


class Entry:
    __slots__ = ('index', 'id', 'word0', 'type', 'name', 'full', 'offset', 'size', 'packed_size', 'packed', 'words')


def fsys_entries(data):
    """List of Entry for an FSYS archive's bytes (empty list if it is not one)."""
    if data[:4] != b'FSYS':
        return []
    n = be32(data, 0x0C)
    info = be32(data, 0x18)
    table = be32(data, info)
    out = []

    def cstr(o):
        if o == 0 or o >= len(data):
            return ''
        return data[o:data.index(b'\0', o)].decode('latin1')
    for k in range(n):
        h = be32(data, table + 4 * k)
        w = struct.unpack_from('>10I', data, h)
        e = Entry()
        e.index = k
        e.word0 = w[0]
        e.id = w[0] >> 16
        e.offset, e.size = w[1], w[2]
        e.packed = bool(w[3] & 0x80000000)
        e.packed_size = w[5]
        e.full = cstr(w[7])
        e.type = w[8]
        e.name = cstr(w[9])
        e.words = w
        out.append(e)
    return out


def stored(data, e):
    return data[e.offset:e.offset + (e.packed_size if e.packed else e.size)]


def lzss_unpack(data):
    if data[:4] != b'LZSS':
        return None
    total = be32(data, 4)
    out = bytearray(total)
    window = bytearray(0x1000)
    wpos = 0xFEE
    flags = 0
    dpos = 0
    i = 16
    n = len(data)
    while dpos < total:
        flags >>= 1
        if not flags & 0x100:
            if i >= n:
                break
            flags = data[i] | 0xFF00
            i += 1
        if flags & 1:
            b = data[i]
            i += 1
            out[dpos] = b
            dpos += 1
            window[wpos] = b
            wpos = (wpos + 1) & 0xFFF
        else:
            lo = data[i]
            hi = data[i + 1]
            i += 2
            off = lo | ((hi & 0xF0) << 4)
            for k in range((hi & 0xF) + 3):
                if dpos >= total:
                    break
                c = window[(off + k) & 0xFFF]
                out[dpos] = c
                dpos += 1
                window[wpos] = c
                wpos = (wpos + 1) & 0xFFF
    return bytes(out)


def extract(data, e):
    s = stored(data, e)
    return lzss_unpack(s) if e.packed else s


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    d = Disc(sys.argv[1])
    cmd = sys.argv[2]
    if cmd == 'info':
        print('game id', d.game_id, 'wii partition' if d.is_wii else 'gamecube')
        print('disc size (as nod presents it)', d.nod.nod_disc_size(d.disc))
        for index, kind, size in d.partitions():
            print('partition', index, 'kind', kind, '(0 data, 1 update, 2 channel)', 'data bytes', size)
        dol = d.dol()
        print('main.dol', len(dol), 'bytes, SHA-1', hashlib.sha1(dol).hexdigest())
        print('files', len(d.files), 'bytes', sum(s for i, s in d.files.values()))
        print('ticket', d.meta.ticket.size, 'tmd', d.meta.tmd.size, 'cert', d.meta.cert.size, 'h3', d.meta.h3.size)
    elif cmd == 'list':
        for p in sorted(d.files):
            print('%10d  %s' % (d.files[p][1], p))
    elif cmd == 'extract':
        out = sys.argv[3]
        for p in sorted(d.files):
            dst = os.path.join(out, *p.split('/'))
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            if not os.path.exists(dst) or os.path.getsize(dst) != d.files[p][1]:
                with open(dst, 'wb') as f:
                    f.write(d.file(p))
        print('extracted', len(d.files), 'files')
    return 0


if __name__ == '__main__':
    sys.exit(main())
