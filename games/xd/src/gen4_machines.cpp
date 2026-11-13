#include "xd_gen4_machines.h"

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

extern "C" {
uint32_t PortXD_RealMachineWaza(uint32_t machineNo);
bool PortXD_RealWazaMcn(void* pdata, uint16_t index);
uint32_t itemDataBiosGetWazaMachineNo(uint8_t* item);
void* pokemonDataBiosGetPtr(uint16_t pokeNum);
}

namespace {

constexpr uint32_t kOwnMachines = 58;

struct Machine {
  uint32_t tm = 0;
  std::string moveRef;
  uint32_t move = 0;
};
struct SpeciesBits {
  std::string key;
  std::string bits;
};

bool sActive = false;
std::vector<Machine> sMachines;
std::vector<SpeciesBits> sPending;
std::map<uint32_t, std::vector<uint8_t>> sBits;
bool sShown = false;

std::string Norm(std::string k) {
  for (char& c : k) c = c == ' ' ? '_' : c;
  return k;
}

const Machine* ByNumber(uint32_t machineNo) {
  if (!sActive || machineNo < kOwnMachines || machineNo - kOwnMachines >= sMachines.size()) return nullptr;
  return &sMachines[machineNo - kOwnMachines];
}

bool SpeciesOf(void* pdata, uint32_t& id) {
  const char* first = static_cast<const char*>(pokemonDataBiosGetPtr(0));
  const char* second = static_cast<const char*>(pokemonDataBiosGetPtr(1));
  if (first == nullptr || second == nullptr || second <= first || pdata == nullptr) return false;
  const std::ptrdiff_t stride = second - first, off = static_cast<const char*>(pdata) - first;
  if (off < 0 || off % stride != 0) return false;
  id = static_cast<uint32_t>(off / stride);
  return true;
}

}

namespace xd::gen4::machines {

void Clear() {
  sActive = false;
  sMachines.clear();
  sPending.clear();
  sBits.clear();
  sShown = false;
}

bool Active() { return sActive; }

bool Load(const std::filesystem::path& pack) {
  Clear();
  std::ifstream in(pack / L"machines.txt");
  if (!in) return true;
  std::string line;
  bool header = false;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    std::istringstream ls(line);
    std::string key;
    ls >> key;
    if (key.empty()) continue;
    if (key == "format") {
      uint32_t v = 0;
      ls >> v;
      if (v != 1) {
        std::fprintf(stderr, "[gen4] machines.txt has format %u (this game reads 1): no extra TMs\n", v);
        Clear();
        return false;
      }
      header = true;
    } else if (key == "machine") {
      uint32_t no = 0;
      Machine m;
      ls >> no >> m.tm >> m.moveRef;
      if (no != kOwnMachines + sMachines.size() || m.moveRef.empty()) {
        std::fprintf(stderr, "[gen4] machines.txt: a machine line is out of order (%s): no extra TMs\n", line.c_str());
        Clear();
        return false;
      }
      sMachines.push_back(std::move(m));
    } else if (key == "bits") {
      SpeciesBits b;
      ls >> b.key >> b.bits;
      if (b.bits.size() != sMachines.size()) {
        std::fprintf(stderr, "[gen4] machines.txt: wrong number of bits (%s): no extra TMs\n", line.c_str());
        Clear();
        return false;
      }
      sPending.push_back(std::move(b));
    }
  }
  if (!header) {
    Clear();
    return true;
  }
  sActive = !sMachines.empty();
  return true;
}

void Resolve(const engine::mods::LoadResult& result) {
  if (!sActive) return;
  std::map<std::string, uint32_t> moves, species;
  for (const auto& r : result.addedRows) {
    if (r.table == "waza_data") moves[Norm(r.key)] = r.id;
    if (r.table == "pokemon_data") species[Norm(r.key)] = r.id;
  }
  size_t missing = 0;
  for (Machine& m : sMachines) {
    if (!m.moveRef.empty() && std::isdigit(static_cast<unsigned char>(m.moveRef[0]))) {
      m.move = static_cast<uint32_t>(std::strtoul(m.moveRef.c_str(), nullptr, 10));
    } else if (auto it = moves.find(m.moveRef); it != moves.end()) {
      m.move = it->second;
    } else {
      missing++;
    }
  }
  size_t speciesCount = 0;
  for (const SpeciesBits& b : sPending) {
    uint32_t id = 0;
    if (b.key[0] == '#') {
      id = static_cast<uint32_t>(std::strtoul(b.key.c_str() + 1, nullptr, 10));
    } else if (auto it = species.find(b.key); it != species.end()) {
      id = it->second;
    } else {
      continue;
    }
    std::vector<uint8_t>& v = sBits[id];
    v.assign(b.bits.size(), 0);
    for (size_t i = 0; i < b.bits.size(); i++) v[i] = b.bits[i] == '1' ? 1 : 0;
    speciesCount++;
  }
  sPending.clear();
  std::fprintf(stderr, "[gen4] extra machines: %zu (TM%u..TM%u), %zu species with bits%s\n", sMachines.size(), sMachines.front().tm,
               sMachines.back().tm, speciesCount, missing ? " (some moves have no number: their TMs teach nothing)" : "");
}

}

extern "C" {

uint32_t itemDataBiosGetWazaIDByWazaMachineNo(uint32_t machineNo) {
  if (const Machine* m = ByNumber(machineNo & 0xFF)) return m->move;
  return PortXD_RealMachineWaza(machineNo);
}

bool pokemonDataBiosGetWazaMcn(void* pdata, uint16_t index) {
  const Machine* m = ByNumber(index);
  if (m == nullptr) return PortXD_RealWazaMcn(pdata, index);
  uint32_t id = 0;
  if (m->move == 0 || !SpeciesOf(pdata, id)) return false;
  const auto it = sBits.find(id);
  return it != sBits.end() && index - kOwnMachines < it->second.size() && it->second[index - kOwnMachines] != 0;
}

uint32_t PortXD_PocketMachineNo(uint8_t* item) {
  const uint32_t machine = itemDataBiosGetWazaMachineNo(item);
  sShown = false;
  if (machine != 0xFF) {
    if (const Machine* m = ByNumber(machine)) {
      sShown = true;
      return m->tm - 1;
    }
  }
  return machine;
}

uint32_t PortXD_PocketMachineWaza(uint32_t machineNo) {
  if (sShown) {
    sShown = false;
    for (const Machine& m : sMachines) {
      if (m.tm == (machineNo & 0xFF) + 1) return m.move;
    }
    return 0;
  }
  return itemDataBiosGetWazaIDByWazaMachineNo(machineNo);
}

}
