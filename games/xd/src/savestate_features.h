#pragma once
#include <any>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>
#include <vector>

#include "port_savestate.h"

namespace xd::savestate {

struct FeatureEntry {
  const char* name;
  std::function<std::any()> save;
  std::function<void(const std::any&)> load;
  std::function<void(const std::any&, portstate::Out&)> toBytes;
  std::function<bool(portstate::In&, std::any&)> fromBytes;
};

inline std::vector<FeatureEntry>& Features() {
  static std::vector<FeatureEntry> list;
  return list;
}

struct Feature {
  Feature(const char* name, std::function<std::any()> save, std::function<void(const std::any&)> load,
          std::function<void(const std::any&, portstate::Out&)> toBytes = nullptr, std::function<bool(portstate::In&, std::any&)> fromBytes = nullptr) {
    Features().push_back({name, std::move(save), std::move(load), std::move(toBytes), std::move(fromBytes)});
  }
};

}

namespace xd::savestate {

struct BytesFeature {
  static void Log(const char* name, const char* what, const std::vector<uint8_t>& b) {
    if (std::getenv("XD_SAVESTATE_FEATURE_LOG") == nullptr) return;
    uint64_t h = 1469598103934665603ull;
    for (uint8_t c : b) h = (h ^ c) * 1099511628211ull;
    std::fprintf(stderr, "[savestate] feature %s %s: %zu bytes, hash %016llx\n", name, what, b.size(), static_cast<unsigned long long>(h));
  }
  BytesFeature(const char* name, std::function<void(portstate::Out&)> write, std::function<bool(portstate::In&)> read) {
    Features().push_back({name,
                          [write, name] {
                            portstate::Out o;
                            write(o);
                            Log(name, "taken", o.b);
                            return std::any(std::move(o.b));
                          },
                          [read, write, name](const std::any& a) {
                            const std::vector<uint8_t>& b = std::any_cast<const std::vector<uint8_t>&>(a);
                            if (std::getenv("XD_SAVESTATE_FEATURE_LOG") != nullptr) {
                              portstate::Out before;
                              write(before);
                              Log(name, "before the load", before.b);
                            }
                            portstate::In in(b.data(), b.size());
                            read(in);
                            Log(name, "put back", b);
                          },
                          [](const std::any& a, portstate::Out& out) { out.vec(std::any_cast<const std::vector<uint8_t>&>(a)); },
                          [](portstate::In& in, std::any& a) {
                            std::vector<uint8_t> b = in.vec<uint8_t>();
                            if (!in.ok()) return false;
                            a = std::move(b);
                            return true;
                          }});
  }
};

}
