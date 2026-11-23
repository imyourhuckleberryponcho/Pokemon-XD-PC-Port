#include "detmath.h"

#include <cmath>

namespace dm {
namespace {

struct DD {
  double hi, lo;
};

inline DD quick_two_sum(double a, double b) {
  double s = a + b;
  return {s, b - (s - a)};
}
inline DD two_sum(double a, double b) {
  double s = a + b, bb = s - a;
  return {s, (a - (s - bb)) + (b - bb)};
}
inline DD two_prod(double a, double b) {
  double p = a * b;
  double t = 134217729.0 * a, ahi = t - (t - a), alo = a - ahi;
  double u = 134217729.0 * b, bhi = u - (u - b), blo = b - bhi;
  double e = ((ahi * bhi - p) + ahi * blo + alo * bhi) + alo * blo;
  return {p, e};
}
inline DD add(DD a, DD b) {
  DD s = two_sum(a.hi, b.hi), t = two_sum(a.lo, b.lo);
  double e = s.lo + t.hi;
  DD u = quick_two_sum(s.hi, e);
  e = u.lo + t.lo;
  return quick_two_sum(u.hi, e);
}
inline DD neg(DD a) { return {-a.hi, -a.lo}; }
inline DD sub(DD a, DD b) { return add(a, neg(b)); }
inline DD mul(DD a, DD b) {
  DD p = two_prod(a.hi, b.hi);
  double e = p.lo + (a.hi * b.lo + a.lo * b.hi);
  return quick_two_sum(p.hi, e);
}
inline DD mul_d(DD a, double b) {
  DD p = two_prod(a.hi, b);
  double e = p.lo + a.lo * b;
  return quick_two_sum(p.hi, e);
}
inline DD div_d(DD a, double d) {
  double q1 = a.hi / d;
  DD r = sub(a, two_prod(q1, d));
  double q2 = r.hi / d;
  r = sub(r, two_prod(q2, d));
  double q3 = r.hi / d;
  DD q = quick_two_sum(q1, q2);
  return add(q, DD{q3, 0.0});
}

const double P1 = 1.5707963267948966, P2 = 6.123233995736766e-17, P3 = -1.4973849048591698e-33, P4 = 5.562271104316826e-50;

void sincos_small(DD r, DD* s, DD* c, int which) {
  DD r2 = mul(r, r);
  if (which & 1) {
    DD term = r, sum = r;
    for (int k = 1; k <= 14; k++) {
      term = div_d(mul(term, r2), (double)((2 * k) * (2 * k + 1)));
      sum = (k & 1) ? sub(sum, term) : add(sum, term);
    }
    *s = sum;
  }
  if (which & 2) {
    DD t2 = {1.0, 0.0}, cs = {1.0, 0.0};
    for (int k = 1; k <= 14; k++) {
      t2 = div_d(mul(t2, r2), (double)((2 * k - 1) * (2 * k)));
      cs = (k & 1) ? sub(cs, t2) : add(cs, t2);
    }
    *c = cs;
  }
}

struct SinCosTable {
  DD sn[103], cs[103];
  DD inv6, inv24, inv2;
  SinCosTable() {
    for (int i = -51; i <= 51; i++) {
      DD r = {(double)i / 64.0, 0.0};
      DD ss = {0, 0}, cc = {0, 0};
      sincos_small(r, &ss, &cc, 3);
      sn[i + 51] = ss;
      cs[i + 51] = cc;
    }
    inv6 = div_d(DD{1.0, 0.0}, 6.0);
    inv24 = div_d(DD{1.0, 0.0}, 24.0);
    inv2 = {0.5, 0.0};
  }
};
const SinCosTable& sct() {
  static const SinCosTable t;
  return t;
}

void sincos_fast(DD r, DD* s, DD* c, int need) {
  const SinCosTable& T = sct();
  int i = (int)std::nearbyint(r.hi * 64.0);
  if (i > 51) i = 51;
  if (i < -51) i = -51;
  DD b = sub(r, DD{(double)i / 64.0, 0.0});
  double bh = b.hi, bh2 = bh * bh;
  DD b2 = mul(b, b);
  DD sb = {0, 0}, cb = {0, 0};
  if (need & 1) {
    DD b3 = mul(b2, b);
    double tail = bh * bh2 * bh2 * (1.0 / 120 + bh2 * (-1.0 / 5040 + bh2 * (1.0 / 362880)));
    sb = add(sub(b, mul(b3, T.inv6)), DD{tail, 0.0});
  }
  if (need & 2) {
    DD b4 = mul(b2, b2);
    double tail = bh2 * bh2 * bh2 * (-1.0 / 720 + bh2 * (1.0 / 40320 + bh2 * (-1.0 / 3628800)));
    cb = add(add(sub(DD{1.0, 0.0}, mul(b2, T.inv2)), mul(b4, T.inv24)), DD{tail, 0.0});
  }
  const DD& sa = T.sn[i + 51];
  const DD& ca = T.cs[i + 51];
  if (need & 1) *s = add(mul(sa, (need & 2) ? cb : DD{1.0, 0.0}), mul(ca, sb));
  if (need & 2) *c = sub(mul(ca, cb), mul(sa, sb));
}

void sincos_dd(double x, DD* s, DD* c, int which = 3) {
  double k = std::nearbyint(x * 0.6366197723675814);
  DD r = {x, 0.0};
  if (k != 0.0) {
    r = sub(r, two_prod(k, P1));
    r = sub(r, two_prod(k, P2));
    r = sub(r, two_prod(k, P3));
    r = sub(r, DD{k * P4, 0.0});
  }
  long long q = ((long long)k) & 3;
  if (q < 0) q += 4;
  int need = 0;
  if (which & 1) need |= (q & 1) ? 2 : 1;
  if (which & 2) need |= (q & 1) ? 1 : 2;
  DD ss = {0, 0}, cc = {0, 0};
  sincos_fast(r, &ss, &cc, 3);
  (void)need;
  if (which & 1) *s = q == 0 ? ss : q == 1 ? cc : q == 2 ? neg(ss) : neg(cc);
  if (which & 2) *c = q == 0 ? cc : q == 1 ? neg(ss) : q == 2 ? neg(cc) : ss;
}

bool usable(double x) { return std::isfinite(x) && std::fabs(x) <= 1e5; }

}

double sin(double x) {
  if (x == 0.0 || !usable(x)) return std::sin(x);
  DD s = {0, 0}, c = {0, 0};
  sincos_dd(x, &s, &c, 1);
  return s.hi;
}
double cos(double x) {
  if (!usable(x)) return std::cos(x);
  DD s = {0, 0}, c = {0, 0};
  sincos_dd(x, &s, &c, 2);
  return c.hi;
}

double atan2(double y, double x) {
  if (!std::isfinite(x) || !std::isfinite(y) || y == 0.0 || x == 0.0) return std::atan2(y, x);
  double t0 = std::atan2(y, x);
  DD s = {0, 0}, c = {0, 0};
  sincos_dd(t0, &s, &c);
  DD num = sub(mul_d(c, y), mul_d(s, x));
  DD den = add(mul_d(c, x), mul_d(s, y));
  double d = num.hi / den.hi;
  DD t = add(DD{t0, 0.0}, DD{d, 0.0});
  return t.hi;
}

double asin(double x) {
  if (!std::isfinite(x) || std::fabs(x) >= 1.0 || x == 0.0) return std::asin(x);
  double t0 = std::asin(x);
  DD s = {0, 0}, c = {0, 0};
  sincos_dd(t0, &s, &c);
  DD num = sub(s, DD{x, 0.0});
  double d = -(num.hi / c.hi);
  return add(DD{t0, 0.0}, DD{d, 0.0}).hi;
}

double acos(double x) {
  if (!std::isfinite(x) || std::fabs(x) >= 1.0) return std::acos(x);
  double t0 = std::acos(x);
  DD s = {0, 0}, c = {0, 0};
  sincos_dd(t0, &s, &c);
  DD num = sub(c, DD{x, 0.0});
  double d = num.hi / s.hi;
  return add(DD{t0, 0.0}, DD{d, 0.0}).hi;
}

double hypot(double x, double y) {
  if (!std::isfinite(x) || !std::isfinite(y) || x == 0.0 || y == 0.0) return std::hypot(x, y);
  double ax = std::fabs(x), ay = std::fabs(y);
  if (ax > 1e150 || ay > 1e150 || (ax < 1e-150 && ay < 1e-150)) return std::hypot(x, y);
  DD s = add(two_prod(x, x), two_prod(y, y));
  double r = std::sqrt(s.hi);
  DD rr = two_prod(r, r);
  DD d = sub(s, rr);
  double corr = d.hi / (2.0 * r);
  return add(DD{r, 0.0}, DD{corr, 0.0}).hi;
}

namespace {

struct ExpTable {
  DD invf[12];
  DD ln2;
  double ln2_3;
  ExpTable() {
    DD f = {1.0, 0.0};
    invf[0] = {1.0, 0.0};
    for (int n = 1; n < 12; n++) {
      f = mul_d(f, (double)n);
      invf[n] = div_d(DD{1.0, 0.0}, f.hi);
    }
    ln2 = {0.6931471805599453, 2.3190468138462996e-17};
    ln2_3 = 5.707708438416212e-34;
  }
};
const ExpTable& ext() {
  static const ExpTable t;
  return t;
}

DD exp_dd(DD x) {
  const ExpTable& T = ext();
  double k = std::nearbyint(x.hi * 1.4426950408889634);
  DD r = x;
  if (k != 0.0) {
    r = sub(r, two_prod(k, T.ln2.hi));
    r = sub(r, two_prod(k, T.ln2.lo));
    r = sub(r, DD{k * T.ln2_3, 0.0});
  }
  DD s = {r.hi * (1.0 / 256.0), r.lo * (1.0 / 256.0)};
  double sh = s.hi;
  double tail = sh * sh * sh * sh * sh * (1.0 / 120 + sh * (1.0 / 720 + sh * (1.0 / 5040 + sh * (1.0 / 40320))));
  DD s2 = mul(s, s);
  DD s3 = mul(s2, s);
  DD s4 = mul(s2, s2);
  DD p = add(add(add(s, mul(s2, T.invf[2])), mul(s3, T.invf[3])), add(mul(s4, T.invf[4]), DD{tail, 0.0}));
  for (int i = 0; i < 8; i++) p = add(mul_d(p, 2.0), mul(p, p));
  DD e = add(DD{1.0, 0.0}, p);
  int ki = (int)k;
  return DD{std::ldexp(e.hi, ki), std::ldexp(e.lo, ki)};
}

DD log_dd(double x) {
  double y0 = std::log(x);
  DD e = exp_dd(DD{-y0, 0.0});
  DD t = sub(mul_d(e, x), DD{1.0, 0.0});
  double th = t.hi;
  double corr = -0.5 * th * th + th * th * th * (1.0 / 3.0);
  return add(add(DD{y0, 0.0}, t), DD{corr, 0.0});
}

}

double exp(double x) {
  if (!std::isfinite(x) || std::fabs(x) > 700.0) return std::exp(x);
  if (x == 0.0) return 1.0;
  return exp_dd(DD{x, 0.0}).hi;
}

double log(double x) {
  if (!std::isfinite(x) || x <= 0.0 || x == 1.0) return std::log(x);
  if (x < 1e-300) return std::log(x);
  return log_dd(x).hi;
}

double pow(double x, double y) {
  if (!std::isfinite(x) || !std::isfinite(y) || x == 0.0 || y == 0.0 || x == 1.0) return std::pow(x, y);
  if (y == 1.0) return x;
  if (y == 2.0) return x * x;
  if (y == 0.5 && x > 0) return std::sqrt(x);
  double ax = std::fabs(x);
  bool neg = false;
  if (x < 0.0) {
    if (y != std::floor(y)) return std::pow(x, y);
    neg = std::fmod(std::fabs(y), 2.0) == 1.0;
  }
  if (y == 3.0) {
    DD sq = two_prod(ax, ax);
    double r = mul_d(sq, ax).hi;
    return neg ? -r : r;
  }
  if (ax < 1e-290 || ax > 1e290) return std::pow(x, y);
  DD l = log_dd(ax);
  DD p = mul_d(l, y);
  if (std::fabs(p.hi) > 690.0) return std::pow(x, y);
  double r = exp_dd(p).hi;
  return neg ? -r : r;
}

double cbrt(double x) {
  if (!std::isfinite(x) || x == 0.0) return std::cbrt(x);
  double y0 = std::cbrt(x);
  DD y2 = two_prod(y0, y0);
  DD cube = mul_d(y2, y0);
  DD r = sub(cube, DD{x, 0.0});
  double corr = r.hi / (3.0 * y2.hi);
  return add(DD{y0, 0.0}, DD{-corr, 0.0}).hi;
}

}
