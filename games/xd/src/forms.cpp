#include "xd_forms.h"

#include "xd_gen57.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>

namespace {
std::vector<xd::forms::Form> sForms;
std::map<uint32_t, uint32_t> sBase;
bool sLoaded = false;
uint32_t sCharm = 0;

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
  return Kind::None;
}
}

namespace xd::forms {

void Load(const engine::mods::LoadResult& result) {
  sForms.clear();
  sBase.clear();
  sCharm = 0;
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
  while (std::getline(in, line)) {
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
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
  }
  sLoaded = !sForms.empty();
  size_t megas = 0, alola = 0;
  for (const Form& f : sForms) { megas += f.kind == Kind::Mega; alola += f.kind == Kind::Alola; }
  std::fprintf(stderr, "[forms] %zu form rows (%zu Alolan, %zu Mega) numbered\n", sForms.size(), alola, megas);
}

bool Loaded() { return sLoaded; }
const std::vector<Form>& All() { return sForms; }

uint32_t BaseOf(uint32_t row) {
  const auto it = sBase.find(row);
  return it == sBase.end() ? row : it->second;
}
uint32_t CharmItem() { return sCharm; }
bool IsForm(uint32_t row) { return sBase.find(row) != sBase.end(); }

uint32_t AlolaEvolution(uint32_t current, uint32_t held, uint32_t result) {
  if (!sLoaded || sCharm == 0 || result == 0 || held != sCharm || IsForm(current)) return result;
  for (const Form& f : sForms) {
    if (f.kind == Kind::Alola && f.base == result) return f.row;
  }
  return result;
}

}

extern "C" {
uint16_t PortXD_BaseSpecies(uint16_t id) { return static_cast<uint16_t>(xd::forms::BaseOf(id)); }
}
