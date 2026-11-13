#include <cmath>

#include "pbr_model.h"

namespace pbr {

void mat_decompose(const Mat& m, V3* scale, V3* rot, V3* pos) {
  double sx = std::sqrt(std::pow(m[0], 2.0) + std::pow(m[4], 2.0) + std::pow(m[8], 2.0));
  double sy = std::sqrt(std::pow(m[1], 2.0) + std::pow(m[5], 2.0) + std::pow(m[9], 2.0));
  double sz = std::sqrt(std::pow(m[2], 2.0) + std::pow(m[6], 2.0) + std::pow(m[10], 2.0));
  double det = (m[0] * (m[5] * m[10] - m[6] * m[9]) - m[1] * (m[4] * m[10] - m[6] * m[8]) + m[2] * (m[4] * m[9] - m[5] * m[8]));
  if (det < 0) sx = -sx;
  double r00, r10, r20;
  if (sx != 0.0) { r00 = m[0] / sx; r10 = m[4] / sx; r20 = m[8] / sx; } else { r00 = 1; r10 = 0; r20 = 0; }
  double r21 = sy != 0.0 ? m[9] / sy : 0, r22 = sz != 0.0 ? m[10] / sz : 1;
  double r11 = sy != 0.0 ? m[5] / sy : 1, r12 = sz != 0.0 ? m[6] / sz : 0;
  double rx, ry, rz;
  if (std::fabs(r20) < 0.999999) {
    ry = -std::asin(r20);
    rx = std::atan2(r21, r22);
    rz = std::atan2(r10, r00);
  } else {
    ry = -std::copysign(3.141592653589793 / 2, r20);
    rz = 0.0;
    rx = std::atan2(-r12, r11);
  }
  *scale = {sx, sy, sz};
  *rot = {rx, ry, rz};
  *pos = {m[3], m[7], m[11]};
}

std::vector<Seg> track_segments(const Track& track) {
  std::vector<Seg> segs;
  double p1 = 0.0, d0 = 0.0, d1 = 0.0;
  int last_op = 0;
  double pos = 0.0, wait = 0.0;
  bool first = true;
  for (const Key& k : track.keys) {
    int op = k.op;
    double v = k.value, s = k.slope;
    double w = (double)k.wait;
    if (op == OP_SLP) {
      d0 = d1;
      d1 = s;
      last_op = OP_SLP;
      continue;
    }
    int intrp = last_op;
    double p0 = p1;
    p1 = v;
    if (op == OP_SPL) {
      d0 = d1;
      d1 = s;
    } else if (op == OP_SPL0 || intrp != OP_SLP) {
      d0 = d1;
      d1 = 0.0;
    }
    if (!first) {
      segs.push_back({pos, wait, intrp, p0, p1, d0, d1});
      pos += wait;
    }
    first = false;
    wait = w;
    last_op = op;
  }
  segs.push_back({pos, 0.0, 0, p1, p1, 0.0, 0.0});
  return segs;
}

static double hermite(double t, double dur, double p0, double p1, double d0, double d1) {
  double s = t / dur;
  double s2 = s * s, s3 = s * s * s;
  return ((2 * s3 - 3 * s2 + 1) * p0 + (-2 * s3 + 3 * s2) * p1 + (s3 - 2 * s2 + s) * dur * d0 + (s3 - s2) * dur * d1);
}

double track_value_segs(const std::vector<Seg>& segs, double track_start, double frame) {
  double t = frame - track_start;
  for (const Seg& g : segs) {
    if (t < g.start + g.dur) {
      double tt = t - g.start;
      if (tt <= 0) return g.p0;
      if (g.op == OP_CON || g.op == OP_KEY) return g.p0;
      if (g.op == OP_LIN) return g.p0 + (g.p1 - g.p0) * tt / g.dur;
      return hermite(tt, g.dur, g.p0, g.p1, g.d0, g.d1);
    }
  }
  return segs.back().p1;
}

double track_value(const Track& t, double frame) { return track_value_segs(track_segments(t), t.start, frame); }

std::vector<Mat> pose_at(const Model& model, const Animation* anim, double frame) {
  std::vector<Mat> out;
  for (size_t i = 0; i < model.joints.size(); i++) {
    const Joint& x = model.joints[i];
    V3 s = x.scale, r = x.rot, p = x.pos;
    const AnimObj* a = anim ? anim->joint((int)i) : nullptr;
    if (a) {
      double f = a->end < frame ? a->end : frame;
      for (const Track& tr : a->tracks) {
        int k = tr.kind;
        if (tr.keys.empty()) continue;
        double v = track_value(tr, f);
        if (J_ROTX <= k && k <= J_ROTZ) r[k - J_ROTX] = v;
        else if (J_TRAX <= k && k <= J_TRAZ) p[k - J_TRAX] = v;
        else if (J_SCAX <= k && k <= J_SCAZ) s[k - J_SCAX] = v;
      }
    }
    Mat l = mat_srt(s, r, p);
    out.push_back(x.parent < 0 ? l : mat_mul(out[x.parent], l));
  }
  return out;
}

}
