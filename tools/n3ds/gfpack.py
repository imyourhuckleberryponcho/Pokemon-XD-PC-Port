"""GFPackage ('PC' container) used by Gen 6/7 for models, textures and animations."""
import struct

def pc_split(b):
    if b[:2] != b'PC':
        return None
    n = struct.unpack_from('<H', b, 2)[0]
    offs = struct.unpack_from('<%dI' % (n + 1), b, 4)
    return [b[offs[i]:offs[i + 1]] for i in range(n)]
