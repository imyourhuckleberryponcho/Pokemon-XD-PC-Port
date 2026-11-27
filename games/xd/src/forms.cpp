#include "xd_forms.h"

#include "xd_gen57.h"
#include "xd_pc_boxes.h"

#include <settings.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>

extern "C" {
uint8_t pokemonDB_GetZokuseiDataId(uint16_t id, uint16_t idx);
uint16_t pokemonBiosGetPokemonDataId(void* pokemon);
void pokemonBiosSetPokemonDataId(void* pokemon, uint16_t id);
uint16_t pokemonBiosGetItemDataId(void* pokemon);
uintptr_t heroGetStatus(void* hero, uint32_t selector, uint16_t index);
void* pcboxGetPokemonBoxName(void* pcbox, int8_t box);
uint8_t fightIsFight(void);
int32_t GSfloorGetCurrent(void);
extern void* floordata;
}
namespace es = engine::settings;

namespace {
std::vector<xd::forms::Form> sForms;
std::map<uint32_t, uint32_t> sBase;
bool sLoaded = false;
uint32_t sPatch = 0;
std::vector<uint32_t> sMemories;
uint32_t sCharm = 0;
struct Look { uint32_t row, weight; };
std::map<uint32_t, std::vector<Look>> sLooks;
uint32_t LookHash(uint32_t pid, uint32_t row) {
  uint64_t x = (static_cast<uint64_t>(pid) << 32 | row) + 0x9E3779B97F4A7C15ull;
  x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
  x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
  x ^= x >> 31;
  return static_cast<uint32_t>(x >> 16);
}

std::vector<std::string> Split(const std::string& s, char sep) {
  std::vector<std::string> out;
  std::string cur;
  for (char c : s) {
    if (c == sep) { out.push_back(cur); cur.clear(); } else cur += c;
  }
  out.push_back(cur);
  return out;
}
xd::forms::Kind KindOf(const std::string& k) {
  using xd::forms::Kind;
  if (k == "alola") return Kind::Alola;
  if (k == "mega") return Kind::Mega;
  if (k == "primal") return Kind::Primal;
  if (k == "battle") return Kind::Battle;
  if (k == "cosmetic") return Kind::Cosmetic;
  if (k == "plate") return Kind::Plate;
  return Kind::None;
}
}

namespace xd::forms {

void Load(const engine::mods::LoadResult& result) {
  sForms.clear();
  sBase.clear();
  sCharm = 0;
  sPatch = 0;
  sMemories.clear();
  sLooks.clear();
  sLoaded = false;
  if (!xd::gen57::Loaded()) return;
  std::ifstream in(xd::gen57::PackFolder() / L"forms.txt");
  if (!in) return;
  std::map<std::string, uint32_t> species, items, baseOfRow;
  for (const auto& r : result.addedRows) {
    if (r.table == "pokemon_data") {
      species[r.key] = r.id;
      baseOfRow[r.key] = r.copyOf;
    } else if (r.table.find("item") != std::string::npos) {
      items[r.key] = r.id;
    }
  }
  std::string line;
  std::map<std::string, uint32_t> baseWeight;
  std::vector<std::pair<std::string, Look>> looks;
  std::map<std::string, uint32_t> baseRowOfName;
  while (std::getline(in, line)) {
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
    if (line.rfind("spawnbase ", 0) == 0) {
      const std::vector<std::string> f = Split(line.substr(10), '|');
      if (f.size() == 2) baseWeight[f[0]] = static_cast<uint32_t>(std::atoi(f[1].c_str()));
      continue;
    }
    if (line.rfind("patch ", 0) == 0) {
      const auto st = items.find(line.substr(6));
      if (st != items.end()) sPatch = st->second;
      continue;
    }
    if (line.rfind("memory ", 0) == 0) {
      const auto st = items.find(line.substr(7));
      if (st != items.end()) sMemories.push_back(st->second);
      continue;
    }
    if (line.rfind("charm ", 0) == 0) {
      const auto st = items.find(line.substr(6));
      if (st != items.end()) sCharm = st->second;
      continue;
    }
    if (line.rfind("form ", 0) != 0) continue;
    const std::vector<std::string> f = Split(line.substr(5), '|');
    if (f.size() < 3) continue;
    const auto it = species.find(f[0]);
    if (it == species.end()) continue;
    Form form;
    form.key = f[0];
    form.row = it->second;
    form.base = baseOfRow[f[0]];
    if (const auto b = species.find(f[1]); b != species.end()) form.base = b->second;
    form.kind = KindOf(f[2]);
    if (f.size() > 3 && !f[3].empty()) {
      const auto st = items.find(f[3]);
      if (st != items.end()) form.stone = st->second;
    }
    if (form.base == 0) continue;
    sBase[form.row] = form.base;
    sForms.push_back(form);
    if (f.size() > 4 && std::atoi(f[4].c_str()) > 0) {
      looks.push_back({f[1], {form.row, static_cast<uint32_t>(std::atoi(f[4].c_str()))}});
      baseRowOfName[f[1]] = form.base;
    }
  }
  for (const auto& l : looks) {
    const uint32_t base = baseRowOfName[l.first];
    const auto bw = baseWeight.find(l.first);
    if (bw == baseWeight.end() || bw->second == 0) continue;
    auto& v = sLooks[base];
    if (v.empty()) v.push_back({base, bw->second});
    v.push_back(l.second);
  }
  sLoaded = !sForms.empty();
  size_t megas = 0, alola = 0;
  for (const Form& f : sForms) { megas += f.kind == Kind::Mega; alola += f.kind == Kind::Alola; }
  std::fprintf(stderr, "[forms] %zu form rows (%zu Alolan, %zu Mega) numbered, %zu species with random looks\n", sForms.size(), alola, megas, sLooks.size());
}

bool Loaded() { return sLoaded; }
const std::vector<Form>& All() { return sForms; }

uint32_t BaseOf(uint32_t row) {
  const auto it = sBase.find(row);
  return it == sBase.end() ? row : it->second;
}
uint32_t CharmItem() { return sCharm; }
uint32_t PatchItem() { return sPatch; }
const std::vector<uint32_t>& MemoryItems() { return sMemories; }
bool IsForm(uint32_t row) { return sBase.find(row) != sBase.end(); }
std::vector<uint32_t> PlateItems() {
  std::vector<uint32_t> out;
  for (const Form& f : sForms) if (f.kind == Kind::Plate && f.stone != 0) out.push_back(f.stone);
  return out;
}
bool IsPlateRow(uint32_t row) {
  for (const Form& f : sForms) if (f.kind == Kind::Plate && f.row == row) return true;
  return false;
}
bool PlateBoosts(uint32_t item, uint32_t moveType) {
  if (item == 0 || !sLoaded) return false;
  for (const Form& f : sForms) {
    if (f.kind == Kind::Plate && f.stone == item) return pokemonDB_GetZokuseiDataId(static_cast<uint16_t>(f.row), 0) == moveType;
  }
  return false;
}

namespace {
bool HasPlateForms(uint32_t base) {
  for (const Form& f : sForms) if (f.kind == Kind::Plate && f.base == base) return true;
  return false;
}
constexpr size_t kPokemonBytes = 0xC4;
int sSynced = 0;
}

void PlateSyncOne(void* pokemon) {
  if (!sLoaded || pokemon == nullptr) return;
  const uint32_t row = pokemonBiosGetPokemonDataId(pokemon);
  if (row == 0) return;
  const uint32_t base = BaseOf(row);
  if (!HasPlateForms(base)) return;
  const uint32_t want = PlateFormRow(base, pokemonBiosGetItemDataId(pokemon));
  const uint32_t target = want != 0 ? want : base;
  if (target == row) return;
  pokemonBiosSetPokemonDataId(pokemon, static_cast<uint16_t>(target));
  if (sSynced++ < 40) std::fprintf(stderr, "[forms] Arceus outside battle: species row %u -> %u (held item %u)\n", row, target, static_cast<unsigned>(pokemonBiosGetItemDataId(pokemon)));
}

void PlateFrame() {
  static bool any = false, known = false;
  static uint32_t frame = 0;
  if (!known || (frame & 255) == 0) { any = false; for (const Form& f : sForms) any |= f.kind == Kind::Plate; known = true; }
  frame++;
  if (!sLoaded || !any || floordata == nullptr || fightIsFight() != 0 || GSfloorGetCurrent() == 0) return;
  for (int i = 0; i < 6; i++) PlateSyncOne(reinterpret_cast<void*>(heroGetStatus(nullptr, 3, static_cast<uint16_t>(i))));
  if (frame % 20 != 0) return;
  for (int b = 0; b < xd::pcboxes::Count(); b++) {
    uint8_t* box = static_cast<uint8_t*>(pcboxGetPokemonBoxName(nullptr, static_cast<int8_t>(b)));
    if (box == nullptr) continue;
    for (int i = 0; i < 30; i++) PlateSyncOne(box + 20 + static_cast<size_t>(i) * kPokemonBytes);
  }
}

uint32_t PlateFormRow(uint32_t base, uint32_t item) {
  if (item == 0) return 0;
  for (const Form& f : sForms) if (f.kind == Kind::Plate && f.base == base && f.stone == item) return f.row;
  return 0;
}

size_t LookSpecies() { return sLooks.size(); }

uint32_t PickLook(uint32_t row, uint32_t pid, bool force) {
  const auto it = sLooks.find(row);
  if (it == sLooks.end()) return row;
  if (!force && !es::Global().Bool("gameplay.random_forms").Get()) return row;
  uint64_t total = 0;
  for (const Look& l : it->second) total += l.weight;
  uint64_t r = LookHash(pid, row) % total;
  for (const Look& l : it->second) {
    if (r < l.weight) return l.row;
    r -= l.weight;
  }
  return row;
}

uint32_t AlolaEvolution(uint32_t current, uint32_t held, uint32_t result) {
  if (!sLoaded || sCharm == 0 || result == 0 || held != sCharm || IsForm(current)) return result;
  for (const Form& f : sForms) {
    if (f.kind == Kind::Alola && f.base == result) return f.row;
  }
  return result;
}

}

extern "C" {
uint16_t pokemonBiosGetPokemonDataId(void* pokemon);
void pokemonBiosSetPokemonDataId(void* pokemon, uint16_t id);
uint16_t pokemonBiosGetDarkpokemonDataId(void* pokemon);
}

namespace xd::forms {
void RandomLook(void* pokemon, uint32_t pid) {
  if (pokemon == nullptr || sLooks.empty()) return;
  if (pokemonBiosGetDarkpokemonDataId(pokemon) != 0) return;
  const uint32_t row = pokemonBiosGetPokemonDataId(pokemon);
  const uint32_t pick = PickLook(row, pid, false);
  if (pick != row) pokemonBiosSetPokemonDataId(pokemon, static_cast<uint16_t>(pick));
}
}

extern "C" {
uint16_t PortXD_BaseSpecies(uint16_t id) { return static_cast<uint16_t>(xd::forms::BaseOf(id)); }
}

extern "C" {
uint16_t PortXD_RealDoItemSoubi(void* pokemon, uint16_t item, uint8_t equip);
uint16_t pokemonDoItemSoubi(void* pokemon, uint16_t item, uint8_t equip) {
  const uint16_t r = PortXD_RealDoItemSoubi(pokemon, item, equip);
  if (fightIsFight() == 0) xd::forms::PlateSyncOne(pokemon);
  return r;
}
}
