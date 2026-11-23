#pragma once
#include <array>
#include <utility>
#include <vector>

namespace n3ds {

using Weights = std::vector<std::pair<int, double>>;

struct DecimateResult {
  std::vector<int> keep;
  std::vector<std::array<int, 3>> tris;
};

DecimateResult decimate(const std::vector<std::array<double, 3>>& pos, const std::vector<std::array<double, 2>>& uv, const std::vector<Weights>& wts,
                        const std::vector<std::array<int, 3>>& tris, int target, double uv_weight = 4.0, double skin_weight = 2.0);

}
