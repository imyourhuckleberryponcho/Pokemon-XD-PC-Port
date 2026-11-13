#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "pbr_to_xd.h"

namespace pbr {

Weights quantise_weights(const std::vector<std::pair<int, double>>& w, double step, int most) {
  std::vector<std::pair<int, double>> items = w;
  std::stable_sort(items.begin(), items.end(), [](const auto& a, const auto& b) { return -a.second < -b.second; });
  if ((int)items.size() > most) items.resize(most);
  double total = 0;
  bool first = true;
  for (auto& it : items) {
    total = first ? it.second : total + it.second;
    first = false;
  }
  if (items.empty() || total == 0.0) total = 1.0;
  int n = (int)std::nearbyint(1.0 / step);
  std::vector<std::pair<int, double>> parts;
  for (auto& it : items) parts.emplace_back(it.first, it.second / total * n);
  std::vector<int> whole;
  for (auto& p : parts) whole.push_back((int)std::floor(p.second));
  int sum = 0;
  for (int x : whole) sum += x;
  int left = n - sum;
  std::vector<int> order(parts.size());
  for (size_t i = 0; i < order.size(); i++) order[i] = (int)i;
  std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return -(parts[a].second - whole[a]) < -(parts[b].second - whole[b]); });
  int take = left >= 0 ? std::min<int>(left, (int)order.size()) : std::max<int>((int)order.size() + left, 0);
  for (int k = 0; k < take; k++) whole[order[k]] += 1;
  Weights out;
  for (size_t i = 0; i < parts.size(); i++)
    if (whole[i] > 0) out.emplace_back(parts[i].first, whole[i] / (double)n);
  std::sort(out.begin(), out.end());
  return out;
}

std::vector<std::pair<int, double>> thin(const std::vector<double>& samples, double tol) {
  int n = (int)samples.size();
  std::vector<int> keep = {0};
  int a = 0;
  while (a < n - 1) {
    int b = a + 1, best = b;
    while (b < n) {
      bool ok = true;
      for (int k = a + 1; k < b; k++) {
        double lin = samples[a] + (samples[b] - samples[a]) * (double)(k - a) / (double)(b - a);
        if (std::fabs(lin - samples[k]) > tol) {
          ok = false;
          break;
        }
      }
      if (!ok) break;
      best = b;
      b++;
    }
    keep.push_back(best);
    a = best;
  }
  std::vector<std::pair<int, double>> out;
  for (int f : keep) out.emplace_back(f, samples[f]);
  return out;
}

std::vector<Key> keys_of(const std::vector<double>& samples, double tol) {
  double mx = samples[0], mn = samples[0];
  for (double x : samples) {
    if (x > mx) mx = x;
    if (x < mn) mn = x;
  }
  if (mx - mn <= tol) return {{OP_CON, samples[0], 0.0, (long long)samples.size() - 1}, {OP_CON, samples[0], 0.0, 0}};
  auto pts = thin(samples, tol);
  std::vector<Key> keys;
  for (size_t i = 0; i + 1 < pts.size(); i++) keys.push_back({OP_LIN, pts[i].second, 0.0, (long long)(pts[i + 1].first - pts[i].first)});
  keys.push_back({OP_LIN, pts.back().second, 0.0, 0});
  return keys;
}

std::vector<Key> vis_keys(const std::vector<char>& v) {
  std::vector<std::pair<double, long long>> runs;
  size_t start = 0;
  for (size_t f = 1; f <= v.size(); f++)
    if (f == v.size() || (v[f] != 0) != (v[start] != 0)) {
      runs.emplace_back(v[start] ? 1.0 : 0.0, (long long)(f - start));
      start = f;
    }
  std::vector<Key> keys;
  for (auto& r : runs) keys.push_back({OP_CON, r.first, 0.0, r.second});
  keys.back() = {OP_CON, keys.back().value, 0.0, std::max<long long>(keys.back().wait - 1, 1)};
  keys.push_back({OP_CON, keys.back().value, 0.0, 0});
  return keys;
}

std::vector<Key> step_keys(const std::vector<double>& v) {
  std::vector<std::pair<double, long long>> runs;
  size_t start = 0;
  for (size_t f = 1; f <= v.size(); f++)
    if (f == v.size() || std::fabs(v[f] - v[start]) > 1e-4) {
      runs.emplace_back(v[start], (long long)(f - start));
      start = f;
    }
  std::vector<Key> keys;
  for (auto& r : runs) keys.push_back({OP_CON, r.first, 0.0, r.second});
  keys.back() = {OP_CON, keys.back().value, 0.0, std::max<long long>(keys.back().wait - 1, 1)};
  keys.push_back({OP_CON, keys.back().value, 0.0, 0});
  return keys;
}

std::string texture_alpha(int fmt, int w, int h, const std::vector<uint8_t>& d) {
  if (fmt == 14) {
    size_t nb = d.size() / 8;
    long long cnt = 0;
    for (size_t b = 0; b < nb; b++) {
      const uint8_t* k = &d[b * 8];
      int c0 = (k[0] << 8) | k[1], c1 = (k[2] << 8) | k[3];
      if (!(c0 <= c1)) continue;
      for (int q = 4; q < 8; q++)
        for (int sh : {6, 4, 2, 0})
          if (((k[q] >> sh) & 3) == 3) cnt++;
    }
    return (double)cnt > 0.002 * w * h ? "texedge" : "opa";
  }
  if (fmt == 0 || fmt == 1) return "xlu";
  std::vector<int> al;
  if (fmt == 5) {
    for (size_t i = 0; i + 2 <= d.size(); i += 2) al.push_back((d[i] & 0x80) == 0 ? (d[i] >> 4) & 7 : 7);
  } else if (fmt == 6) {
    size_t nb = d.size() / 64;
    for (size_t b = 0; b < nb; b++)
      for (int j = 0; j < 32; j += 2) al.push_back((d[b * 64 + j] * 7 + 127) / 255);
  } else if (fmt == 2) {
    for (uint8_t x : d) al.push_back(((x >> 4) * 7 + 7) / 15);
  } else if (fmt == 3) {
    for (size_t i = 0; i < d.size(); i += 2) al.push_back((d[i] * 7 + 127) / 255);
  } else {
    return "opa";
  }
  double n = (double)al.size();
  if (al.empty()) n = 1.0;
  long long mid = 0, zero = 0;
  for (int a : al) {
    if (a > 0 && a < 7) mid++;
    if (a == 0) zero++;
  }
  if (mid / n > 0.02) return "xlu";
  if (zero / n > 0.002) return "texedge";
  return "opa";
}

double py_round(double x, int digits) {
  char buf[512];
  snprintf(buf, sizeof buf, "%.*f", digits, x);
  return strtod(buf, nullptr);
}

std::vector<int> pyset_order(const std::vector<int>& inserted) {
  struct Slot { bool used = false; long long key = 0; };
  std::vector<Slot> table(8);
  size_t mask = 7, fill = 0, usedn = 0;
  auto insert_clean = [](std::vector<Slot>& t, size_t mask_, long long key) {
    size_t perturb = (size_t)key, i = (size_t)key & mask_;
    while (true) {
      if (!t[i].used) { t[i].used = true; t[i].key = key; return; }
      if (i + 9 <= mask_) {
        for (int j = 0; j < 9; j++)
          if (!t[i + 1 + j].used) { t[i + 1 + j].used = true; t[i + 1 + j].key = key; return; }
      }
      perturb >>= 5;
      i = (i * 5 + 1 + perturb) & mask_;
    }
  };
  for (int k : inserted) {
    long long key = k;
    size_t perturb = (size_t)key, i = (size_t)key & mask;
    bool present = false, placed = false;
    while (!placed && !present) {
      size_t probes = (i + 9 <= mask) ? 9 : 0;
      size_t e = i;
      while (true) {
        if (!table[e].used) {
          table[e].used = true;
          table[e].key = key;
          placed = true;
          break;
        }
        if (table[e].key == key) { present = true; break; }
        e++;
        if (probes == 0) break;
        probes--;
      }
      if (placed || present) break;
      perturb >>= 5;
      i = (i * 5 + 1 + perturb) & mask;
    }
    if (present) continue;
    fill++;
    usedn++;
    if (fill * 5 < mask * 3) continue;
    size_t minused = usedn > 50000 ? usedn * 2 : usedn * 4;
    size_t newsize = 8;
    while (newsize <= minused) newsize <<= 1;
    std::vector<Slot> nt(newsize);
    for (auto& s : table)
      if (s.used) insert_clean(nt, newsize - 1, s.key);
    table.swap(nt);
    mask = newsize - 1;
  }
  std::vector<int> out;
  for (auto& s : table)
    if (s.used) out.push_back((int)s.key);
  return out;
}

std::vector<int> used_images(const Model& m) {
  std::vector<int> ins;
  for (auto& ma : m.materials)
    for (auto& t : ma.textures)
      if (t.image) ins.push_back(*t.image);
  for (auto& an : m.animations)
    for (auto& kv : an.materials) {
      if (!kv.second) continue;
      for (auto& ma : kv.second->items)
        if (ma)
          for (auto& ta : ma->texanims)
            for (int i : ta.images) ins.push_back(i);
    }
  return pyset_order(ins);
}

static int po2(int n) {
  int p = 8;
  while (p < n) p *= 2;
  return p;
}

static void reencode(Image& im, const Rgba& rgba, int w, int h, const std::string& cls) {
  if (cls == "xlu") {
    if (im.format >= 0 && im.format <= 3) {
      im.format = 3;
      im.data = gx_encode_ia8(rgba);
    } else {
      im.format = 5;
      im.data = gx_encode_rgb5a3(rgba);
    }
  } else {
    im.format = 14;
    im.data = gx_encode_cmpr(rgba);
  }
  im.width = w;
  im.height = h;
}

long long fit_textures(Model& m, std::vector<std::string>& classes, const Logger& log, long long budget) {
  std::vector<int> used = used_images(m);
  std::set<int> used_set(used.begin(), used.end());
  auto size = [&]() {
    long long s = 0;
    for (int k : used) s += (long long)m.images[k].data.size();
    return s;
  };
  long long before = size();
  for (size_t i = 0; i < m.images.size(); i++) {
    if (!used_set.count((int)i)) continue;
    Image& im = m.images[i];
    int bw, bh;
    if (!gx_tile(im.format, &bw, &bh)) { bw = 4; bh = 4; }
    int w = im.width, h = im.height, nw = w, nh = h;
    if ((long long)w * h <= (long long)MAX_SIDE * MAX_SIDE && std::max(w, h) <= 1024) {
    } else {
      nw = std::min(nw, MAX_SIDE);
      nh = std::min(nh, MAX_SIDE);
    }
    if (nw % bw || nh % bh) {
      nw = std::min(po2(nw), MAX_SIDE);
      nh = std::min(po2(nh), MAX_SIDE);
    }
    if (nw != w || nh != h) {
      Rgba rgba = gx_decode(im.format, w, h, im.data.data(), im.data.size());
      reencode(im, gx_resize(rgba, nw, nh), nw, nh, classes.at(i));
    }
  }
  if (budget >= 0) {
    while (size() > budget) {
      std::vector<int> can;
      for (int k : used)
        if (std::max(m.images[k].width, m.images[k].height) > 128) can.push_back(k);
      if (can.empty()) {
        std::vector<int> deep;
        for (int k : used)
          if ((m.images[k].format == 4 || m.images[k].format == 5 || m.images[k].format == 6) && classes.at(k) == "opa") deep.push_back(k);
        if (deep.empty()) break;
        int k = deep[0];
        for (int q : deep)
          if (m.images[q].data.size() > m.images[k].data.size()) k = q;
        Image& im = m.images[k];
        if (im.width % 8 || im.height % 8) break;
        Rgba rgba = gx_decode(im.format, im.width, im.height, im.data.data(), im.data.size());
        im.format = 14;
        im.data = gx_encode_cmpr(rgba);
        continue;
      }
      int i = can[0];
      for (int q : can)
        if (m.images[q].data.size() > m.images[i].data.size()) i = q;
      Image& im = m.images[i];
      Rgba rgba = gx_decode(im.format, im.width, im.height, im.data.data(), im.data.size());
      int hw = im.width / 2, hh = im.height / 2;
      reencode(im, gx_resize(rgba, hw, hh), hw, hh, classes.at(i));
    }
  }
  long long after = size();
  if (after != before) {
    int largest = 0;
    for (auto& im : m.images) largest = std::max(largest, std::max(im.width, im.height));
    char buf[160];
    snprintf(buf, sizeof buf, "INFO: pictures %lld -> %lld bytes (largest side %d)", before, after, largest);
    log(buf);
  }
  return before - after;
}

}
