#include "xd_exp_share.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
std::vector<std::string> sActive;
bool sOn = false;
}

namespace xd::expshare {

std::vector<std::string> OptionsOf(bool on) {
  std::vector<std::string> o;
  if (on) o.push_back(kOption);
  return o;
}

bool OnIn(const std::vector<std::string>& options) {
  return std::find(options.begin(), options.end(), std::string(kOption)) != options.end();
}

std::vector<std::string> TestSwitch() {
  const char* env = std::getenv("XD_EXP_SHARE_ALL");
  if (env == nullptr || std::strcmp(env, "1") != 0) return {};
  if (const char* o = std::getenv("XD_REQUIRE_ORIGINAL"); o != nullptr && std::strcmp(o, "1") == 0) return {};
  return OptionsOf(true);
}

void SetActive(const std::vector<std::string>& options, const char* why) {
  sActive = options;
  sOn = OnIn(options);
  if (sOn) std::fprintf(stderr, "[exp share] %s: Pokemon that did not battle get half the experience\n", why);
}

bool Active() { return sOn; }

}

extern "C" {
int PortXD_ExpAllOn(void) { return sOn ? 1 : 0; }

unsigned PortXD_ExpAllBench(unsigned base, int metCount) {
  const unsigned each = metCount > 0 ? base / static_cast<unsigned>(metCount) : base;
  const unsigned half = std::max(1u, each / 2u);
  std::fprintf(stderr, "[exp share] a Pokemon that did not battle: %u (a participant's share: %u)\n", half, std::max(1u, each));
  return half;
}
}
