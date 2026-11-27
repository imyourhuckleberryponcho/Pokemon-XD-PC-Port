#include "xd_gen4_abilities.h"
#include "xd_modern_types.h"
#include "xd_gen57.h"
#include "xd_mod_battle.h"

#include "../../../engine/mods/json.h"

#include "data/common_rel.h"
#include "data/xd_data_mods.h"
#include "xd_type_icons.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>

extern "C" {
uint16_t PortXD_RealJoutai(uint32_t id, uint32_t target);
void* PortXD_RealSpritePtr(uint32_t id);
void PortXD_LogTypesTable();
}

namespace {

bool sLoaded = false;
int sGen57FairyRows = 0;
constexpr uint32_t kFairy = 18;
constexpr uint32_t kText = 0xBE00;
constexpr uint32_t kSprite = 2000;
constexpr uint32_t kSmallSprite = 2001;
constexpr uint32_t kKindSpecial = 2;

constexpr uint16_t kNeutral = 0x3F, kSuper = 0x41, kResist = 0x42, kImmune = 0x43;

uint16_t VsFairy(uint32_t attack) {
  switch (attack) {
    case 1: case 6: case 17: return kResist;
    case 3: case 8: return kSuper;
    case 16: return kImmune;
    default: return kNeutral;
  }
}

uint16_t FairyVs(uint32_t defend) {
  switch (defend) {
    case 1: case 16: case 17: return kSuper;
    case 3: case 8: case 10: return kResist;
    default: return kNeutral;
  }
}

uint16_t Modern(uint32_t attack, uint32_t defend, uint16_t game) {
  if (defend == 8 && (attack == 7 || attack == 17) && game == kResist) return kNeutral;
  return game;
}

struct Entry {
  alignas(8) uint8_t bytes[72];
  bool made;
};
Entry sEntries[2];

#pragma pack(push, 1)
struct SpriteVal {
  int16_t x, y, w, h;
  uint32_t tex;
};
#pragma pack(pop)

}

extern "C" uint16_t zokuseiBiosGetWazaJoutai(uint32_t id, uint32_t target);

namespace {
void LogChartOnce() {
  static bool done = false;
  if (done) return;
  done = true;
  struct Pair {
    const char* what;
    uint32_t a, d;
    uint16_t want;
  };
  static const Pair kPairs[] = {
      {"Fairy vs Dragon", 18, 16, kSuper},  {"Fairy vs Fighting", 18, 1, kSuper}, {"Fairy vs Dark", 18, 17, kSuper},
      {"Fairy vs Poison", 18, 3, kResist},  {"Fairy vs Steel", 18, 8, kResist},   {"Fairy vs Fire", 18, 10, kResist},
      {"Fairy vs Normal", 18, 0, kNeutral}, {"Fairy vs Fairy", 18, 18, kNeutral}, {"Dragon vs Fairy", 16, 18, kImmune},
      {"Fighting vs Fairy", 1, 18, kResist}, {"Dark vs Fairy", 17, 18, kResist},   {"Bug vs Fairy", 6, 18, kResist},
      {"Fire vs Fairy", 10, 18, kNeutral},   {"Poison vs Fairy", 3, 18, kSuper},   {"Steel vs Fairy", 8, 18, kSuper},
      {"Normal vs Fairy", 0, 18, kNeutral}, {"Ghost vs Steel", 7, 8, kNeutral},   {"Dark vs Steel", 17, 8, kNeutral},
      {"Fire vs Steel", 10, 8, kSuper},     {"Steel vs Steel", 8, 8, kResist},    {"Ghost vs Normal", 7, 0, kImmune},
      {"Ghost vs Ghost", 7, 7, kSuper},     {"Dark vs Dark", 17, 17, kResist},
  };
  int bad = 0;
  for (const Pair& p : kPairs) {
    const uint16_t got = zokuseiBiosGetWazaJoutai(p.a, p.d);
    if (got != p.want) bad++;
    std::fprintf(stderr, "[modern types] chart: %s = 0x%02X%s\n", p.what, got, got == p.want ? "" : " (UNEXPECTED)");
  }
  std::fprintf(stderr, "[modern types] chart check: %s\n", bad == 0 ? "all as the later games have it" : "DIFFERENT");
}

void LogTypesTableOnce() {
  static bool done = false;
  if (done) return;
  done = true;
  if (std::getenv("XD_TYPES_TABLE") != nullptr) PortXD_LogTypesTable();
}
}

extern "C" uint16_t zokuseiBiosGetWazaJoutai(uint32_t id, uint32_t target) {
  LogTypesTableOnce();
  if (xd::modbattle::Rules()) return xd::modbattle::InverseJoutai(sLoaded ? Modern(id, target, PortXD_RealJoutai(id, target)) : PortXD_RealJoutai(id, target));
  if (!sLoaded) return PortXD_RealJoutai(id, target);
  LogChartOnce();
  if (id == kFairy) return target == kFairy ? kNeutral : (target < 18 ? FairyVs(target) : 0);
  if (target == kFairy) return id < 18 ? VsFairy(id) : 0;
  return Modern(id, target, PortXD_RealJoutai(id, target));
}

extern "C" void* menuSpriteBiosGetPtr(uint32_t id) {
  if (!sLoaded || (id != kSprite && id != kSmallSprite)) return PortXD_RealSpritePtr(id);
  const bool big = id == kSprite;
  Entry& e = sEntries[big ? 0 : 1];
  if (!e.made) {
    const void* like = PortXD_RealSpritePtr(big ? 108 : 406);
    const auto* bigVal = reinterpret_cast<const SpriteVal*>(static_cast<const uint8_t*>(PortXD_RealSpritePtr(108)) + 8);
    std::memcpy(e.bytes, like, sizeof e.bytes);
    auto* val = reinterpret_cast<SpriteVal*>(e.bytes + 8);
    for (int i = 0; i < 5; i++) {
      val[i].x = static_cast<int16_t>(big ? xd::modern::kFairyBigX : xd::modern::kFairySmallX);
      val[i].y = static_cast<int16_t>(big ? xd::modern::kFairyBigY : xd::modern::kFairySmallY);
      if (!big) val[i].tex = bigVal[i].tex;
    }
    e.made = true;
  }
  return e.bytes;
}

namespace {
bool PatchPackTypes(std::string& text, const std::string& key, int a, int b) {
  const std::string quoted = "\"" + key + "\"";
  for (size_t at = text.find(quoted); at != std::string::npos; at = text.find(quoted, at + 1)) {
    size_t p = at + quoted.size();
    while (p < text.size() && (text[p] == ' ' || text[p] == '\n' || text[p] == '\r' || text[p] == '\t')) p++;
    if (p >= text.size() || text[p] != ':') continue;
    const size_t field = text.find("\"zokuseiDataId\"", p);
    if (field == std::string::npos) return false;
    const size_t open = text.find('[', field), close = text.find(']', field);
    if (open == std::string::npos || close == std::string::npos || open > close) return false;
    text.replace(open, close - open + 1, "[" + std::to_string(a) + ", " + std::to_string(b) + "]");
    return true;
  }
  return false;
}
bool PatchPackType(std::string& text, const std::string& key, int type) {
  const std::string quoted = "\"" + key + "\"";
  for (size_t at = text.find(quoted); at != std::string::npos; at = text.find(quoted, at + 1)) {
    size_t p = at + quoted.size();
    while (p < text.size() && (text[p] == ' ' || text[p] == '\n' || text[p] == '\r' || text[p] == '\t')) p++;
    if (p >= text.size() || text[p] != ':') continue;
    const size_t field = text.find("\"zokuseiDataId\"", p);
    if (field == std::string::npos) return false;
    const size_t colon = text.find(':', field);
    size_t end = colon + 1;
    while (end < text.size() && (text[end] == ' ' || (text[end] >= '0' && text[end] <= '9'))) end++;
    text.replace(colon + 1, end - colon - 1, " " + std::to_string(type));
    return true;
  }
  return false;
}
int PatchGen57FairyRows(std::vector<engine::mods::DataModFile>& files, int* speciesRows, int* formRows) {
  namespace J = engine::mods::json;
  std::map<uint32_t, std::array<bool, 2>> species;
  std::map<std::string, std::array<bool, 2>> forms;
  if (!xd::gen57::FairySlots(species, forms)) return -1;
  int n = 0, failed = 0;
  *speciesRows = *formRows = 0;
  for (engine::mods::DataModFile& f : files) {
    const bool isSpecies = f.label.find("02_species") != std::string::npos, isForms = f.label.find("03_forms") != std::string::npos;
    if (f.mod != "gen57" || (!isSpecies && !isForms)) continue;
    J::Value root;
    J::Reader reader(f.text);
    if (!reader.Document(root) || root.kind != J::Value::Kind::Object) continue;
    const auto add = root.fields.find("add");
    if (add == root.fields.end() || add->second.kind != J::Value::Kind::Object) continue;
    const auto rows = add->second.fields.find("pokemon");
    if (rows == add->second.fields.end() || rows->second.kind != J::Value::Kind::Object) continue;
    for (const std::string& key : rows->second.order) {
      const J::Value& row = rows->second.fields.at(key);
      const auto types = row.fields.find("zokuseiDataId");
      if (row.kind != J::Value::Kind::Object || types == row.fields.end() || types->second.items.size() != 2) continue;
      std::array<bool, 2> slots = {false, false};
      if (isSpecies) {
        const auto nat = row.fields.find("numPokemon");
        const auto it = nat == row.fields.end() ? species.end() : species.find(static_cast<uint32_t>(nat->second.number));
        if (it != species.end()) slots = it->second;
      } else {
        const auto it = forms.find(key);
        if (it != forms.end()) slots = it->second;
      }
      if (!slots[0] && !slots[1]) continue;
      const int a = slots[0] ? static_cast<int>(kFairy) : static_cast<int>(types->second.items[0].number);
      const int b = slots[1] ? static_cast<int>(kFairy) : static_cast<int>(types->second.items[1].number);
      if (PatchPackTypes(f.text, key, a, b)) {
        n++;
        (isSpecies ? *speciesRows : *formRows) += 1;
      } else {
        failed++;
        std::fprintf(stderr, "[modern types] the pack's row of %s could not be changed in place\n", key.c_str());
      }
    }
  }
  (void)failed;
  return n;
}
}

namespace xd::modern {

uint16_t ChartCode(uint32_t attack, uint32_t defend) { return zokuseiBiosGetWazaJoutai(attack, defend); }

bool Loaded() { return sLoaded; }

int Gen57FairyRows() { return sLoaded ? sGen57FairyRows : 0; }

void AppendTexts(std::vector<std::pair<uint32_t, std::string>>& texts) {
  if (!sLoaded) return;
  const char* fairy = xd::gen4::LangText("fairy", "Fairy");
  texts.emplace_back((xd::data::kAddedTextGroup << 20) | kText, fairy != nullptr ? fairy : "FAIRY");
}

void Prepare(bool wanted, bool gen4, std::vector<engine::mods::DataModFile>& dataFiles) {
  sLoaded = false;
  PortXD_SetModernType(false, 0, 0, 0, 0);
  xd::type_icons::SetActive(false);
  for (Entry& e : sEntries) e.made = false;
  if (!wanted) return;
  struct Change {
    const char* key;
    int a, b;
  };
  static const Change kSpecies[] = {
      {"#35", 18, 18},  {"#36", 18, 18},  {"#173", 18, 18},
      {"#39", 0, 18},   {"#40", 0, 18},   {"#174", 0, 18},
      {"#175", 18, 18}, {"#176", 18, 2},
      {"#209", 18, 18}, {"#210", 18, 18},
      {"#183", 11, 18}, {"#184", 11, 18}, {"#350", 0, 18},
      {"#122", 14, 18},
      {"#392", 14, 18}, {"#393", 14, 18}, {"#394", 14, 18},
      {"#355", 8, 18},
  };
  static const Change kGen4[] = {{"Togekiss", 18, 2}, {"Mime Jr.", 14, 18}};
  std::string json =
      "{\n \"_note\": \"made by the port (games/xd/src/modern_types.cpp): the Fairy type's Pokemon and moves\",\n \"pokemon\": {\n";
  bool first = true;
  const auto species = [&](const Change& c) {
    json += std::string(first ? "" : ",\n") + "  \"" + c.key + "\": {\"zokuseiDataId\": [" + std::to_string(c.a) + ", " +
            std::to_string(c.b) + "]}";
    first = false;
  };
  for (const Change& c : kSpecies) species(c);
  if (gen4) {
    for (const Change& c : kGen4) {
      bool done = false;
      for (engine::mods::DataModFile& f : dataFiles) {
        if (f.label.find("02_species") != std::string::npos && PatchPackTypes(f.text, c.key, c.a, c.b)) done = true;
      }
      if (!done) {
        std::fprintf(stderr, "[modern types] the pack's row of %s could not be changed in place: set by a file of ours\n", c.key);
        species(c);
      }
    }
  }
  if (xd::gen57::Loaded()) {
    int n = 0;
    const std::vector<std::string> names = xd::gen57::FairyMoveNames();
    for (const std::string& nm : names) {
      for (engine::mods::DataModFile& f : dataFiles) {
        if (f.label.find("01_moves") != std::string::npos && PatchPackType(f.text, nm, 18)) { n++; break; }
      }
    }
    std::fprintf(stderr, "[modern types] the Gen 5-7 pack's moves that become Fairy: %d of %zu\n", n, names.size());
  }
  sGen57FairyRows = 0;
  if (xd::gen57::Loaded()) {
    int sp = 0, fm = 0;
    const int n = PatchGen57FairyRows(dataFiles, &sp, &fm);
    if (n < 0) std::fprintf(stderr, "[modern types] the Gen 5-7 pack has no report.json: its Fairy species keep their Normal / other type (make the pack again)\n");
    else std::fprintf(stderr, "[modern types] the Gen 5-7 pack's rows that become Fairy: %d species and %d forms\n", sp, fm);
    sGen57FairyRows = n > 0 ? n : 0;
  }
  json += "\n },\n \"moves\": {\n  \"#204\": {\"zokuseiDataId\": 18},\n  \"#186\": {\"zokuseiDataId\": 18},\n  \"#236\": {\"zokuseiDataId\": 18}\n }\n}\n";
  engine::mods::DataModFile d;
  d.mod = "modern_types";
  d.builtIn = true;
  d.modName = "Modern types";
  d.label = "modern_types/types.json";
  d.text = json;
  dataFiles.push_back(std::move(d));
  PortXD_SetModernType(true, kKindSpecial, kSprite, kSmallSprite, (xd::data::kAddedTextGroup << 20) | kText);
  sLoaded = true;
  xd::type_icons::SetActive(true);
  std::fprintf(stderr, "[modern types] on: Fairy = type 18; %zu species%s and 3 moves become Fairy; Steel resists neither Ghost nor Dark\n",
               sizeof kSpecies / sizeof kSpecies[0] + (gen4 ? 2 : 0), gen4 ? " (with Togekiss, Mime Jr.)" : "");
}

}
