#include "game/gc_fused.h"

#include <math.h>
#include <string.h>

static unsigned fb(float f) { unsigned u; memcpy(&u, &f, 4); return u; }
static unsigned long long db(double d) { unsigned long long u; memcpy(&u, &d, 8); return u; }

int gc_fused_c_check(float a, float b, float c, double da, double dc) {
    int bad = 0;
    float x = c;
    bad += fb(GC_MADD(a, b, c)) != fb((float)fma((double)a, (double)b, (double)c));
    bad += fb(GC_MADDR(c, a, b)) != fb((float)fma((double)a, (double)b, (double)c));
    bad += fb(GC_MSUB(a, b, c)) != fb((float)fma((double)a, (double)b, -(double)c));
    bad += fb(GC_MSUBR(c, a, b)) != fb(-(float)fma((double)a, (double)b, -(double)c));
    GC_MADD_TO(x, a, b);
    bad += fb(x) != fb((float)fma((double)a, (double)b, (double)c));
    x = c;
    GC_MSUB_FROM(x, a, b);
    bad += fb(x) != fb(-(float)fma((double)a, (double)b, -(double)c));
    bad += db(GC_MADD(a, da, dc)) != db(fma((double)a, da, dc));
    bad += db(GC_MADD(a, b, dc)) != db((double)(a * b) + dc);
    return bad;
}
