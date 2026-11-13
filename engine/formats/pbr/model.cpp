#include <cmath>
#include <stdexcept>

#include "pbr_model.h"

namespace pbr {

Mat mat_mul(const Mat& a, const Mat& b) {
  Mat r{};
  for (int i = 0; i < 3; i++) {
    double a0 = a[i * 4], a1 = a[i * 4 + 1], a2 = a[i * 4 + 2], a3 = a[i * 4 + 3];
    r[i * 4] = a0 * b[0] + a1 * b[4] + a2 * b[8];
    r[i * 4 + 1] = a0 * b[1] + a1 * b[5] + a2 * b[9];
    r[i * 4 + 2] = a0 * b[2] + a1 * b[6] + a2 * b[10];
    r[i * 4 + 3] = a0 * b[3] + a1 * b[7] + a2 * b[11] + a3;
  }
  return r;
}

Mat mat_inv(const Mat& m) {
  double a = m[0], b = m[1], c = m[2], d = m[4], e = m[5], f = m[6], g = m[8], h = m[9], i = m[10];
  double det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
  if (std::fabs(det) < 1e-30) throw std::runtime_error("matrix cannot be inverted");
  double k = 1.0 / det;
  Mat r = {(e * i - f * h) * k, (c * h - b * i) * k, (b * f - c * e) * k, 0,
           (f * g - d * i) * k, (a * i - c * g) * k, (c * d - a * f) * k, 0,
           (d * h - e * g) * k, (b * g - a * h) * k, (a * e - b * d) * k, 0};
  double tx = m[3], ty = m[7], tz = m[11];
  r[3] = -(r[0] * tx + r[1] * ty + r[2] * tz);
  r[7] = -(r[4] * tx + r[5] * ty + r[6] * tz);
  r[11] = -(r[8] * tx + r[9] * ty + r[10] * tz);
  return r;
}

V3 mat_point(const Mat& m, const V3& p) {
  return {m[0] * p[0] + m[1] * p[1] + m[2] * p[2] + m[3], m[4] * p[0] + m[5] * p[1] + m[6] * p[2] + m[7],
          m[8] * p[0] + m[9] * p[1] + m[10] * p[2] + m[11]};
}

V3 mat_vec(const Mat& m, const V3& p) {
  return {m[0] * p[0] + m[1] * p[1] + m[2] * p[2], m[4] * p[0] + m[5] * p[1] + m[6] * p[2],
          m[8] * p[0] + m[9] * p[1] + m[10] * p[2]};
}

Mat mat_rot(double rx, double ry, double rz) {
  double sx = std::sin(rx), cx = std::cos(rx), sy = std::sin(ry), cy = std::cos(ry), sz = std::sin(rz), cz = std::cos(rz);
  return {cy * cz, sx * sy * cz - cx * sz, cx * sy * cz + sx * sz, 0,
          cy * sz, sx * sy * sz + cx * cz, cx * sy * sz - sx * cz, 0,
          -sy, sx * cy, cx * cy, 0};
}

Mat mat_srt(const V3& scale, const V3& rot, const V3& pos) {
  Mat m = mat_rot(rot[0], rot[1], rot[2]);
  for (int r = 0; r < 3; r++) {
    m[r * 4] *= scale[0];
    m[r * 4 + 1] *= scale[1];
    m[r * 4 + 2] *= scale[2];
  }
  m[3] = pos[0];
  m[7] = pos[1];
  m[11] = pos[2];
  return m;
}

std::vector<Mat> Model::world_static() const {
  std::vector<Mat> out;
  for (size_t i = 0; i < joints.size(); i++) {
    const Joint& x = joints[i];
    Mat l = mat_srt(x.scale, x.rot, x.pos);
    out.push_back(x.parent < 0 ? l : mat_mul(out[x.parent], l));
  }
  return out;
}

}
