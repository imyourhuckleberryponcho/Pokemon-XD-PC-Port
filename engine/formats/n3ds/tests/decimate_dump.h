#pragma once
#include <chrono>

#include "n3ds_decimate.h"
#include "species_dump.h"

namespace n3ds_test {
inline void dump_decimate(const RomFs& r, int group, double ratio) {
  Detected d = detect(r);
  Garc g = garc_at(r, d.models);
  GfModel gm = parse_model(pc_split(lz11_maybe(g.read(1 + 9 * (size_t)group)))[0]);
  int si = 0;
  double secs = 0;
  for (auto& me : gm.meshes)
    for (auto& sm : me.subs) {
      auto tl = triangles(sm);
      si++;
      if (tl.size() <= 16) continue;
      const GfVerts& V = sm.verts;
      int n = V.n;
      std::vector<std::array<double, 3>> pos(n);
      for (int i = 0; i < n; i++) pos[i] = {V.get("pos").at(i, 0), V.get("pos").at(i, 1), V.get("pos").at(i, 2)};
      std::vector<std::array<double, 2>> uv;
      if (V.has("uv0"))
        for (int i = 0; i < n; i++) uv.push_back({V.get("uv0").at(i, 0), V.get("uv0").at(i, 1)});
      std::vector<Weights> wts(n);
      if (V.has("bidx") && V.has("bw")) {
        const Arr &bi = V.get("bidx"), &bw = V.get("bw");
        for (int i = 0; i < n; i++)
          for (int k = 0; k < bi.cols && k < bw.cols; k++) {
            int bidx = (int)bi.at(i, k);
            double w = bw.at(i, k);
            if (w > 0 && bidx < (int)sm.bone_idx.size()) {
              int j = sm.bone_idx[bidx];
              bool f = false;
              for (auto& p : wts[i])
                if (p.first == j) { p.second += w; f = true; }
              if (!f) wts[i].push_back({j, w});
            }
          }
      }
      std::vector<std::array<int, 3>> tris;
      for (auto& t : tl) tris.push_back({(int)t[0], (int)t[1], (int)t[2]});
      auto t0 = std::chrono::steady_clock::now();
      auto out = decimate(pos, uv, wts, tris, (int)(tris.size() * ratio));
      secs += std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
      std::vector<double> flat;
      for (auto& t : out.tris) for (int x : t) flat.push_back(out.keep[x]);
      printf("sub %d %zu -> %zu %zu %08x\n", si, tris.size(), out.keep.size(), out.tris.size(), crcd(flat));
    }
  fprintf(stderr, "sec decimate %.4f\n", secs);
}
}
