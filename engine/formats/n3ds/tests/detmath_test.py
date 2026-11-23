"""Judge dm:: (engine/formats/n3ds/detmath.cpp) against mpmath at 60 digits.
   python detmath_test.py make <args.bin> [count]       random arguments (angles up to +-60, pairs for atan2)
   python detmath_test.py check <args.bin> <out.bin>    how many results are correctly rounded: dm:: and the C library
Run the test program (detmath_test) on each system and compare the out.bin files byte for byte too."""
import random
import struct
import sys

if sys.argv[1] == 'make':
    n = int(sys.argv[3]) if len(sys.argv) > 3 else 200000
    rnd = random.Random(12345)
    vals = []
    for i in range(n):
        a = rnd.uniform(-60, 60) if i % 3 else rnd.uniform(-4, 4)
        b = rnd.uniform(-60, 60)
        vals += [a, b]
    open(sys.argv[2], 'wb').write(struct.pack('<%dd' % len(vals), *vals))
else:
    import mpmath
    mpmath.mp.prec = 200
    a = struct.unpack('<%dd' % (len(open(sys.argv[3], 'rb').read()) // 8), open(sys.argv[3], 'rb').read())
    args = struct.unpack('<%dd' % (len(open(sys.argv[2], 'rb').read()) // 8), open(sys.argv[2], 'rb').read())
    names = ['sin', 'cos', 'atan2', 'asin', 'acos', 'pow', 'exp', 'log', 'cbrt', 'hypot']
    bad = {k: [0, 0] for k in names}
    n = len(a) // 20
    for i in range(n):
        x, y = args[2 * i], args[2 * i + 1]
        u = x * 0.1234567891
        u = u - int(u)
        px, py = abs(x) / 20.0 + 1e-3, y / 10.0
        ref = [mpmath.sin(x), mpmath.cos(x), mpmath.atan2(x, y), mpmath.asin(u), mpmath.acos(u), mpmath.power(px, py), mpmath.exp(py), mpmath.log(px),
               (mpmath.cbrt(x) if x >= 0 else -mpmath.cbrt(-x)), mpmath.hypot(x, y)]
        for k in range(10):
            r = float(ref[k])
            if a[20 * i + k] != r:
                bad[names[k]][0] += 1
            if a[20 * i + 10 + k] != r:
                bad[names[k]][1] += 1
    print('of', n, 'arguments; wrong (dm, C library):', bad)
