#include <cmath>
#include <cstring>
#include <vector>
#include <algorithm>

#ifndef USE_FMA
#define USE_FMA 0
#endif
static inline double mad(double a, double b, double c) { return USE_FMA ? std::fma(a, b, c) : a * b + c; }

struct M4 { double m[4][4]; };

static M4 ident() { M4 r; memset(&r, 0, sizeof r); for (int i = 0; i < 4; i++) r.m[i][i] = 1; return r; }

static M4 mul(const M4& a, const M4& b) {
    M4 c;
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++) {
            double acc = a.m[i][0] * b.m[0][j];
            for (int k = 1; k < 4; k++) acc = std::fma(a.m[i][k], b.m[k][j], acc);
            c.m[i][j] = acc;
        }
    return c;
}

static double dotb(const double* x, int xs, const double* y, int ys, int n) {
    if (n <= 0) return 0.0;
    double acc = x[(n - 1) * xs] * y[(n - 1) * ys];
    for (int k = n - 2; k >= 0; k--) acc += x[k * xs] * y[k * ys];
    return acc;
}

static bool inv4(const M4& A, M4& out) {
    double a[4][4]; memcpy(a, A.m, sizeof a);
    int piv[4];
    for (int j = 0; j < 4; j++) {
        for (int i = 0; i < j; i++) if (piv[i] != i) std::swap(a[i][j], a[piv[i]][j]);
        for (int i = 1; i < j; i++) a[i][j] -= dotb(&a[i][0], 1, &a[0][j], 4, i);
        for (int i = j; i < 4; i++) a[i][j] -= dotb(&a[i][0], 1, &a[0][j], 4, j);
        int jp = j; double mx = std::fabs(a[j][j]);
        for (int i = j + 1; i < 4; i++) if (std::fabs(a[i][j]) > mx) { mx = std::fabs(a[i][j]); jp = i; }
        piv[j] = jp;
        if (a[jp][j] == 0.0) return false;
        if (jp != j) for (int c = 0; c <= j; c++) std::swap(a[j][c], a[jp][c]);
        double r = 1.0 / a[j][j];
        for (int i = j + 1; i < 4; i++) a[i][j] *= r;
    }
    double B[4][4]; memset(B, 0, sizeof B);
    for (int i = 0; i < 4; i++) B[i][i] = 1;
    for (int k = 0; k < 4; k++) if (piv[k] != k) for (int j = 0; j < 4; j++) std::swap(B[k][j], B[piv[k]][j]);
    for (int c = 0; c < 4; c++) {
        for (int i = 0; i < 4; i++) B[i][c] -= dotb(&a[i][0], 1, &B[0][c], 4, i);
        for (int i = 3; i >= 0; i--) {
            double d = dotb(&a[i][i + 1], 1, &B[i + 1][c], 4, 3 - i);
            B[i][c] = (B[i][c] - d) * (1.0 / a[i][i]);
        }
    }
    memcpy(out.m, B, sizeof B);
    return true;
}

static void rot_mat(double rx, double ry, double rz, double R[3][3]) {
    double sx = sin(rx), cx = cos(rx), sy = sin(ry), cy = cos(ry), sz = sin(rz), cz = cos(rz);
    R[0][0] = cy * cz; R[0][1] = sx * sy * cz - cx * sz; R[0][2] = cx * sy * cz + sx * sz;
    R[1][0] = cy * sz; R[1][1] = sx * sy * sz + cx * cz; R[1][2] = cx * sy * sz - sx * cz;
    R[2][0] = -sy;     R[2][1] = sx * cy;                R[2][2] = cx * cy;
}

static double norm3(double a, double b, double c) { return sqrt(mad(c, c, mad(b, b, a * a))); }

static void axis_angle_mat(const double v[3], double R[3][3]) {
    double ang = norm3(v[0], v[1], v[2]) * 2.0;
    if (ang <= 1e-12) { memset(R, 0, 9 * sizeof(double)); R[0][0] = R[1][1] = R[2][2] = 1; return; }
    double d = ang / 2.0;
    double x = v[0] / d, y = v[1] / d, z = v[2] / d;
    double c = cos(ang), s = sin(ang), t = 1 - c;
    R[0][0] = t * x * x + c;     R[0][1] = t * x * y - s * z; R[0][2] = t * x * z + s * y;
    R[1][0] = t * x * y + s * z; R[1][1] = t * y * y + c;     R[1][2] = t * y * z - s * x;
    R[2][0] = t * x * z - s * y; R[2][1] = t * y * z + s * x; R[2][2] = t * z * z + c;
}

static void euler_from_mat(const double R[3][3], double out[3]) {
    double r20 = R[2][0], rx, ry, rz;
    if (std::fabs(r20) < 0.999999) {
        ry = -asin(r20); rx = atan2(R[2][1], R[2][2]); rz = atan2(R[1][0], R[0][0]);
    } else {
        ry = -std::copysign(3.14159265358979323846 / 2, r20); rz = 0.0; rx = atan2(-R[1][2], R[1][1]);
    }
    out[0] = rx; out[1] = ry; out[2] = rz;
}

static double det3(const double a[3][3]) {
    return a[0][0] * (a[1][1] * a[2][2] - a[1][2] * a[2][1]) - a[0][1] * (a[1][0] * a[2][2] - a[1][2] * a[2][0]) + a[0][2] * (a[1][0] * a[2][1] - a[1][1] * a[2][0]);
}

static void split(const double A[3][3], double sc[3], double eul[3]) {
    double R[3][3];
    for (int j = 0; j < 3; j++) {
        double s = sqrt(mad(A[2][j], A[2][j], mad(A[1][j], A[1][j], A[0][j] * A[0][j])));
        sc[j] = s > 1e-9 ? s : 1e-9;
    }
    for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) R[i][j] = A[i][j] / sc[j];
    if (det3(R) < 0) { for (int i = 0; i < 3; i++) R[i][0] = -R[i][0]; sc[0] = -sc[0]; }
    euler_from_mat(R, eul);
}

static M4 local4(const double sc[3], const double r[3], const double t[3]) {
    M4 L = ident();
    double R[3][3]; rot_mat(r[0], r[1], r[2], R);
    for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) L.m[i][j] = R[i][j] * sc[j];
    for (int i = 0; i < 3; i++) L.m[i][3] = t[i];
    return L;
}

struct Skel {
    int nb; const int* parent; const unsigned char* ssc;
    bool needs_ssc(const std::vector<double>& s) const {
        for (int i = 0; i < nb; i++) if (ssc[i]) { int p = parent[i]; for (int k = 0; k < 3; k++) if (std::fabs(s[p * 3 + k] - 1.0) > 1e-4) return true; }
        return false;
    }
    void ssc_to_plain(std::vector<double>& s, std::vector<double>& r, std::vector<double>& t) const {
        if (!needs_ssc(s)) return;
        std::vector<double> s2 = s, r2 = r, t2 = t;
        std::vector<M4> Wt(nb), Wa(nb);
        for (int i = 0; i < nb; i++) {
            int p = parent[i];
            M4 L = local4(&s[i * 3], &r[i * 3], &t[i * 3]);
            if (p < 0) { Wt[i] = Wa[i] = L; continue; }
            const double* sp = &s[p * 3];
            if (ssc[i] && std::min({std::fabs(sp[0]), std::fabs(sp[1]), std::fabs(sp[2])}) > 0.05) {
                M4 Lt = ident();
                for (int k = 0; k < 3; k++) Lt.m[k][3] = sp[k] * t[i * 3 + k];
                M4 Lr = ident();
                for (int a = 0; a < 3; a++) for (int b = 0; b < 3; b++) Lr.m[a][b] = L.m[a][b];
                M4 dg = ident();
                for (int k = 0; k < 3; k++) dg.m[k][k] = 1 / sp[k];
                Wt[i] = mul(mul(mul(Wt[p], dg), Lt), Lr);
                double Rr[3][3]; rot_mat(r[i * 3], r[i * 3 + 1], r[i * 3 + 2], Rr);
                double Mm[3][3];
                for (int a = 0; a < 3; a++) for (int b = 0; b < 3; b++) Mm[a][b] = (Rr[a][b] * s[i * 3 + b]) / sp[a];
                double cs[3], ce[3];
                split(Mm, cs, ce);
                bool ok;
                double es[3] = {0, 0, 0}, ee[3] = {0, 0, 0}, Dt[3] = {0, 0, 0};
                M4 inv;
                if (inv4(Wa[p], inv)) {
                    M4 D = mul(inv, Wt[i]);
                    double D3[3][3];
                    for (int a = 0; a < 3; a++) for (int b = 0; b < 3; b++) D3[a][b] = D.m[a][b];
                    split(D3, es, ee);
                    for (int a = 0; a < 3; a++) Dt[a] = D.m[a][3];
                    bool fin = true;
                    for (int a = 0; a < 4; a++) for (int b = 0; b < 4; b++) if (!std::isfinite(D.m[a][b])) fin = false;
                    double mxe = std::max({std::fabs(es[0]), std::fabs(es[1]), std::fabs(es[2])});
                    double mxc = std::max({std::fabs(cs[0]), std::fabs(cs[1]), std::fabs(cs[2])});
                    ok = fin && mxe <= 2.0 * std::max(mxc, 1.0);
                } else ok = false;
                if (ok) { for (int k = 0; k < 3; k++) { s2[i * 3 + k] = es[k]; r2[i * 3 + k] = ee[k]; t2[i * 3 + k] = Dt[k]; } }
                else { for (int k = 0; k < 3; k++) { s2[i * 3 + k] = cs[k]; r2[i * 3 + k] = ce[k]; t2[i * 3 + k] = t[i * 3 + k]; } }
            } else {
                Wt[i] = mul(Wt[p], L);
            }
            Wa[i] = mul(Wa[p], local4(&s2[i * 3], &r2[i * 3], &t2[i * 3]));
        }
        s = s2; r = r2; t = t2;
    }
};

static bool herp(const double* kf, const double* kv, const double* ks, const unsigned char* khs, int n, double frame, double* out) {
    if (n == 0) return false;
    if (n == 1 || frame <= kf[0]) { *out = kv[0]; return true; }
    if (frame >= kf[n - 1]) { *out = kv[n - 1]; return true; }
    int lo = 0;
    for (int i = 0; i < n - 1; i++) if (kf[i] <= frame) lo = i;
    int b = lo + 1;
    double d = kf[b] - kf[lo];
    if (d == 0) { *out = kv[lo]; return true; }
    double t = (frame - kf[lo]) / d;
    if (!khs[lo] || !khs[b]) { *out = kv[lo] + (kv[b] - kv[lo]) * t; return true; }
    double t2 = t * t, t3 = t * t * t;
    *out = ((2 * t3 - 3 * t2 + 1) * kv[lo] + (t3 - 2 * t2 + t) * ks[lo] * d + (-2 * t3 + 3 * t2) * kv[b] + (t3 - t2) * ks[b] * d);
    return true;
}

static void axis_to_euler(std::vector<double>& r, int i) {
    double R3[3][3], e[3];
    axis_angle_mat(&r[i * 3], R3);
    euler_from_mat(R3, e);
    for (int k = 0; k < 3; k++) r[i * 3 + k] = e[k];
}

extern "C" __declspec(dllexport) int sample_at(
    int nb, const int* parent, const unsigned char* ssc, const double* raw_s, const double* raw_r, const double* raw_t,
    int nmb, const int* mb_index, const unsigned char* mb_axis, const int* ch_off, const int* ch_cnt,
    const double* kf, const double* kv, const double* ks, const unsigned char* khs,
    int nframes, const double* frames, double* S, double* R, double* T) {
    Skel sk{nb, parent, ssc};
    const size_t per = (size_t)nb * 3;
    for (int fi = 0; fi < nframes; fi++) {
        double frame = frames[fi];
        std::vector<double> s(raw_s, raw_s + per), r(raw_r, raw_r + per), t(raw_t, raw_t + per);
        std::vector<unsigned char> axis(nb, 0);
        for (int m = 0; m < nmb; m++) {
            int i = mb_index[m];
            if (i < 0) continue;
            axis[i] = mb_axis[m];
            if (mb_axis[m]) { r[i * 3] = r[i * 3 + 1] = r[i * 3 + 2] = 0.0; }
            for (int k = 0; k < 9; k++) {
                int o = ch_off[m * 9 + k], n = ch_cnt[m * 9 + k];
                double v;
                if (!herp(kf + o, kv + o, ks + o, khs + o, n, frame, &v)) continue;
                std::vector<double>& dst = (k / 3 == 0) ? s : (k / 3 == 1) ? r : t;
                dst[i * 3 + k % 3] = v;
            }
        }
        if (sk.needs_ssc(s)) {
            for (int i = 0; i < nb; i++) if (axis[i]) axis_to_euler(r, i);
            std::fill(axis.begin(), axis.end(), 0);
            sk.ssc_to_plain(s, r, t);
        }
        for (int i = 0; i < nb; i++) if (axis[i]) axis_to_euler(r, i);
        memcpy(S + fi * per, s.data(), per * sizeof(double));
        memcpy(R + fi * per, r.data(), per * sizeof(double));
        memcpy(T + fi * per, t.data(), per * sizeof(double));
    }
    const double PI = 3.14159265358979323846, period = 2 * PI, lowv = -PI;
    std::vector<double> run(per, 0.0), orig(R, R + (size_t)nframes * per);
    for (int fi = 1; fi < nframes; fi++) {
        for (size_t q = 0; q < per; q++) {
            double dd = orig[fi * per + q] - orig[(fi - 1) * per + q];
            double md = fmod(dd - lowv, period);
            if (md != 0) { if (md < 0) md += period; } else md = 0.0;
            double ddmod = md + lowv;
            if (ddmod == lowv && dd > 0) ddmod = PI;
            double ph = ddmod - dd;
            if (std::fabs(dd) < PI) ph = 0;
            run[q] += ph;
            R[fi * per + q] = orig[fi * per + q] + run[q];
        }
    }
    return 0;
}

extern "C" __declspec(dllexport) int t_mul4(const double* a, const double* b, double* c) {
    M4 A, B; memcpy(A.m, a, sizeof A.m); memcpy(B.m, b, sizeof B.m);
    M4 C = mul(A, B); memcpy(c, C.m, sizeof C.m); return 0;
}
extern "C" __declspec(dllexport) int t_inv4(const double* a, double* c) {
    M4 A, C; memcpy(A.m, a, sizeof A.m);
    if (!inv4(A, C)) return 1;
    memcpy(c, C.m, sizeof C.m); return 0;
}
