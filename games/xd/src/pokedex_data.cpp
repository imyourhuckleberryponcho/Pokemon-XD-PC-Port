#include "xd_pokedex.h"

#include "xd_battles_won.h"
#include "xd_forms.h"
#include "xd_gen4.h"
#include "xd_rulesets.h"
#include "xd_user_folder.h"

#include <i18n.h>
#include <port_user_folder.h>
#include <settings.h>

#include <dolphin/types.h>
#include <game/menuTables.h>
#include <game/pxdvs/app/pokemon/pokemonDB.hpp>
#include <game/relglobal.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>

extern "C" {
u16* GSmsgGetGSchar(u32 msg);
void* pokemonTokuseiDataBiosGetPtr(u16 id);
u32 pokemonTokuseiDataBiosGetName(void* data);
u32 pokemonTokuseiDataBiosGetDoc(void* data);
void* wazaDataBiosGetPtr(u16 id);
s16 wazaDataBiosGetIryoku(u8* waza);
u8 wazaDataBiosGetAvg(u8* waza);
u8 wazaDataBiosGetZokuseiDataId(u8* waza);
u32 wazaDataBiosGetDoc(u8* waza);
u32 wazaDataBiosGetName(void* waza);
void* itemDataBiosGetPtr(u16 id);
u32 itemDataBiosGetName(void* item);
uint32_t itemDataBiosGetWazaIDByWazaMachineNo(uint32_t machineNo);
bool pokemonDataBiosGetWazaMcn(void* pdata, uint16_t index);
uint16_t memoDataGetCount(void* memo);
uint16_t memoDataGetPokemonID(void* memo, uint16_t index);
void* savedataGetStatus(s32, s32);
uint16_t pokemonBiosGetPokemonDataId(void* pokemon);
uint8_t pokemonBiosGetLevel(void* pokemon);
uint32_t pokemonGetSex(void* pokemon);
uint16_t pokemonBiosGetDarkpokemonDataId(void* pokemon);
uint8_t pokemonCheckValid(void* pokemon);
uintptr_t heroGetStatus(void* hero, u32 selector, u16 index);
extern void* zokusei_data;
extern u32* zokusei_data_number;
extern u16 darkPokemonList[];
extern u32 darkPokemonList_number;
extern void* esabapokemondata_E1;
extern u32* esabapokemondata_E1_number;
extern void* esabapokemondata_E2;
extern u32* esabapokemondata_E2_number;
extern void* esabapokemondata_E3;
extern u32* esabapokemondata_E3_number;
extern void* esabapokemondata_EX;
extern u32* esabapokemondata_EX_number;
}

namespace xd::pokedex {
namespace {

namespace ep = engine::pokedex;
std::string Tr(const char* s) { return engine::i18n::Tr(s); }

struct EsaEntryView {
  u8 minLevel, maxLevel;
  u16 id;
  u32 prob, rate;
};
struct ZokuseiView {
  u8 wazaKind, unk1;
  u16 spriteID, smallSpriteID;
  u8 unk6[2];
  u32 name;
  u16 wazaJoutai[18];
};

void LoadEntries();
std::map<uint16_t, std::string> sEntries;
bool sEntriesTried = false;
std::vector<ep::Species> sAll;
std::map<uint16_t, size_t> sByIndex;
bool sBuilt = false;
ep::PlayerState sPlayer;
std::set<uint16_t> sCaught, sShiny;
bool sKept = false;
unsigned sChanged = 0;
std::map<uint16_t, ep::Where> sWhere;
std::map<uint16_t, std::vector<ep::WhereLine>> sYours;
bool sWhereBuilt = false;

std::string Msg(uint32_t id) {
  if (id == 0) return std::string();
  return Utf8(GSmsgGetGSchar(id));
}

std::string ItemName(uint16_t item) {
  void* p = itemDataBiosGetPtr(item);
  return p ? Msg(itemDataBiosGetName(p)) : std::string();
}

std::string MoveName(uint16_t move) {
  void* p = wazaDataBiosGetPtr(move);
  return p ? Msg(wazaDataBiosGetName(p)) : std::string();
}

std::string TypeName(uint8_t id);
}

namespace {
std::map<int, std::string> sPins;
bool sPinsChanged = false;
bool SourceOnPc(const std::string& id) {
  if (id == "xd") return true;
  const xd::rulesets::SourceFolder* f = xd::rulesets::SourceLayer(id);
  if (f == nullptr) return false;
  std::error_code ec;
  return std::filesystem::is_directory(PortUser_Folder("Pokemon XD") / "extra_assets" / f->layerId, ec);
}
}

std::vector<LookSource> LookSources() {
  std::vector<LookSource> out;
  for (const auto& s : xd::rulesets::Registry().Sources()) out.push_back({s.id, s.name, SourceOnPc(s.id)});
  return out;
}
std::string LookPin(int national) {
  const auto it = sPins.find(national);
  return it != sPins.end() && SourceOnPc(it->second) ? it->second : std::string();
}
void SetLookPin(int national, const std::string& id) {
  if (national <= 0) return;
  if (!id.empty()) {
    if (xd::rulesets::Registry().FindSource(id) == nullptr || !SourceOnPc(id)) return;
    if (sPins[national] != id) sPinsChanged = true;
    sPins[national] = id;
    std::fprintf(stderr, "[pokedex] Looks: No. %d pinned to %s\n", national, id.c_str());
  } else if (sPins.erase(national) != 0) {
    sPinsChanged = true;
    std::fprintf(stderr, "[pokedex] Looks: No. %d unpinned\n", national);
  }
}
std::map<int, std::string> LookPins() { return sPins; }
void SetLookPins(const std::map<int, std::string>& pins) {
  sPins = pins;
  sPinsChanged = false;
}
bool LookPinsChanged() { return sPinsChanged; }
void ClearLookPinsChanged() { sPinsChanged = false; }

std::string TypeNameOf(uint8_t id) { return TypeName(id); }

MoveInfo MoveInfoOf(uint16_t move) {
  MoveInfo r;
  void* p = wazaDataBiosGetPtr(move);
  if (p == nullptr) return r;
  r.ok = true;
  r.power = wazaDataBiosGetIryoku(static_cast<u8*>(p));
  r.accuracy = wazaDataBiosGetAvg(static_cast<u8*>(p));
  r.type = wazaDataBiosGetZokuseiDataId(static_cast<u8*>(p));
  r.typeName = TypeName(r.type);
  r.text = Msg(wazaDataBiosGetDoc(static_cast<u8*>(p)));
  return r;
}

namespace {
std::string TypeName(uint8_t id) {
  if (zokusei_data == nullptr || zokusei_data_number == nullptr || id >= *zokusei_data_number) return std::string();
  return Msg(static_cast<ZokuseiView*>(zokusei_data)[id].name);
}

std::string GrowthName(uint8_t id) {
  switch (id) {
    case 0: return Tr("Medium Fast");
    case 1: return Tr("Erratic");
    case 2: return Tr("Fluctuating");
    case 3: return Tr("Medium Slow");
    case 4: return Tr("Fast");
    case 5: return Tr("Slow");
  }
  return std::string();
}

std::string Upper(std::string s) {
  for (char& c : s) {
    if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 32);
  }
  return s;
}

ep::Evolution MakeEvolution(uint8_t kind, uint16_t param, uint16_t target) {
  ep::Evolution e;
  e.to = target;
  e.param = param;
  char b[64];
  switch (kind) {
    case 1: e.kind = ep::EvoKind::Friendship; e.text = Tr("FRIENDSHIP"); break;
    case 2: e.kind = ep::EvoKind::FriendshipDay; e.text = Tr("FRIENDSHIP, DAY"); break;
    case 3: e.kind = ep::EvoKind::FriendshipNight; e.text = Tr("FRIENDSHIP, NIGHT"); break;
    case 4:
      e.kind = ep::EvoKind::Level;
      std::snprintf(b, sizeof b, Tr("Level %d").c_str(), static_cast<int>(param));
      e.text = b;
      break;
    case 5: e.kind = ep::EvoKind::Trade; e.text = Tr("TRADE"); break;
    case 6:
    case 17:
      e.kind = kind == 6 ? ep::EvoKind::TradeItem : ep::EvoKind::Level;
      e.text = (kind == 6 ? Tr("TRADE HOLDING ") : Tr("LEVEL UP HOLDING ")) + Upper(ItemName(param));
      break;
    case 7:
    case 18:
    case 19:
      e.kind = ep::EvoKind::Stone;
      e.text = Upper(ItemName(param));
      if (kind == 18) e.text += Tr(" (FEMALE)");
      if (kind == 19) e.text += Tr(" (MALE)");
      break;
    case 8: e.kind = ep::EvoKind::Stats; e.text = Tr("ATTACK > DEFENSE"); break;
    case 9: e.kind = ep::EvoKind::Stats; e.text = Tr("ATTACK = DEFENSE"); break;
    case 10: e.kind = ep::EvoKind::Stats; e.text = Tr("ATTACK < DEFENSE"); break;
    case 11:
    case 12:
      e.kind = ep::EvoKind::Personality;
      std::snprintf(b, sizeof b, Tr("Level %d").c_str(), static_cast<int>(param));
      e.text = b;
      break;
    default:
      e.kind = ep::EvoKind::Other;
      std::snprintf(b, sizeof b, Tr("Level %d").c_str(), static_cast<int>(param));
      e.text = param > 0 && param <= 100 ? std::string(b) : Tr("SPECIAL");
      break;
  }
  return e;
}

std::string FormWord(const std::string& w) {
  if (w == "Alola") return Tr("Alolan");
  if (w == "Mega") return Tr("Mega");
  if (w == "Mega X") return Tr("Mega X");
  if (w == "Mega Y") return Tr("Mega Y");
  if (w == "Primal") return Tr("Primal");
  if (w == "Heat") return Tr("Heat");
  if (w == "Wash") return Tr("Wash");
  if (w == "Frost") return Tr("Frost");
  if (w == "Fan") return Tr("Fan");
  if (w == "Mow") return Tr("Mow");
  return w;
}

void Build() {
  sAll.clear();
  sByIndex.clear();
  sBuilt = true;
  if (pokemon_data == nullptr || pokemon_data_number == nullptr) return;
  const uint32_t n = std::min<uint32_t>(*pokemon_data_number, 0x2000);
  std::set<uint16_t> shadows;
  for (uint32_t i = 0; i < darkPokemonList_number && i < 512; i++) {
    if (darkPokemonList[i] != 0) shadows.insert(darkPokemonList[i]);
  }
  const std::vector<xd::gen4::ExtraEvolution>& extra = xd::gen4::ExtraEvolutions();
  const uint32_t kTutorRowToIndex[12] = {3, 5, 2, 7, 6, 4, 8, 0, 11, 9, 1, 10};
  for (uint32_t id = 1; id < n; id++) {
    const PokemonData& d = pokemon_data[id];
    if (d.name == 0) continue;
    ep::Species s;
    s.index = static_cast<uint16_t>(id);
    s.national = d.numPokemon >= 1 && d.numPokemon <= 1100 ? d.numPokemon : 0;
    s.mod = id >= 415 && s.national == 0;
    s.gen = s.national <= 386 ? 3 : s.national <= 493 ? 4 : s.national <= 649 ? 5 : s.national <= 721 ? 6 : 7;
    s.name = Msg(d.name);
    s.category = Msg(d.typeName);
    s.types[0] = d.zokuseiDataId[0];
    s.types[1] = d.zokuseiDataId[1];
    for (int k = 0; k < 2; k++) s.typeNames[k] = TypeName(s.types[k]);
    for (int k = 0; k < 2; k++) {
      if (d.tokuseiDataId[k] == 0) continue;
      void* t = pokemonTokuseiDataBiosGetPtr(d.tokuseiDataId[k]);
      if (t == nullptr) continue;
      s.abilities[k] = Msg(pokemonTokuseiDataBiosGetName(t));
      s.abilityText[k] = Msg(pokemonTokuseiDataBiosGetDoc(t));
    }
    s.heightDm = d.height;
    s.weightHg = d.weight;
    s.genderRatio = d.sexRatio;
    s.catchRate = d.get;
    s.growth = GrowthName(d.growDataId);
    s.base[0] = d.basisStatus[0].maxHp;
    s.base[1] = d.basisStatus[0].phyAtk;
    s.base[2] = d.basisStatus[0].phyDef;
    s.base[3] = d.basisStatus[0].speAtk;
    s.base[4] = d.basisStatus[0].speDef;
    s.base[5] = d.basisStatus[0].nimbleness;
    s.shadow = shadows.count(s.index) != 0;
    for (const PokemonDataWaza& w : d.waza) {
      if (w.dataId == 0) continue;
      ep::LearnedMove m;
      m.source = ep::MoveSource::Level;
      m.level = w.level;
      m.move = w.dataId;
      m.name = MoveName(w.dataId);
      s.moves.push_back(m);
    }
    for (uint32_t mc = 0; mc < 128; mc++) {
      const uint32_t move = itemDataBiosGetWazaIDByWazaMachineNo(mc);
      if (move == 0 || move > 0xFFFF || !pokemonDataBiosGetWazaMcn(const_cast<PokemonData*>(&d), static_cast<uint16_t>(mc))) continue;
      ep::LearnedMove m;
      m.source = ep::MoveSource::Machine;
      m.number = static_cast<uint16_t>(mc < 50 ? mc + 1 : mc < 58 ? 100 + (mc - 49) : mc - 58 + 51);
      m.move = static_cast<uint16_t>(move);
      m.name = MoveName(m.move);
      s.moves.push_back(m);
    }
    const WAZAOSHIE_DATA* tutors = static_cast<const WAZAOSHIE_DATA*>(wazaoshie_data);
    for (int row = 0; row < 12 && wazaoshie_data != nullptr && wazaoshie_data_number != nullptr && row < *wazaoshie_data_number; row++) {
      if (d.teachWaza[kTutorRowToIndex[row]] == 0 || tutors[row].id == 0) continue;
      ep::LearnedMove m;
      m.source = ep::MoveSource::Tutor;
      m.number = static_cast<uint16_t>(row);
      m.move = tutors[row].id;
      m.name = MoveName(m.move);
      s.moves.push_back(m);
    }
    for (const Sinka& e : d.sinka) {
      if (e.kind == 0 || e.pokemonDataId == 0) continue;
      s.evolutions.push_back(MakeEvolution(e.kind, e.buff, e.pokemonDataId));
    }
    for (const xd::gen4::ExtraEvolution& e : extra) {
      if (e.species == id) s.evolutions.push_back(MakeEvolution(e.kind, e.param, e.target));
    }
    if (!sEntriesTried) LoadEntries();
    if (const auto en = sEntries.find(s.national); s.national != 0 && en != sEntries.end()) s.entry = en->second;
    else if (s.mod && d.doc != 0) s.entry = Msg(d.doc);
    sByIndex[s.index] = sAll.size();
    sAll.push_back(std::move(s));
  }
  if (xd::forms::Loaded()) {
    for (const xd::forms::Form& f : xd::forms::All()) {
      const auto fi = sByIndex.find(static_cast<uint16_t>(f.row));
      const auto bi = sByIndex.find(static_cast<uint16_t>(f.base));
      if (fi == sByIndex.end() || bi == sByIndex.end() || fi->second == bi->second) continue;
      ep::Species& fs = sAll[fi->second];
      ep::Species& bs = sAll[bi->second];
      std::string word = f.key;
      const size_t open = word.find('('), close = word.rfind(')');
      word = open != std::string::npos && close != std::string::npos && close > open ? word.substr(open + 1, close - open - 1) : std::string("Form");
      fs.formOf = bs.index;
      fs.formName = FormWord(word);
      fs.national = bs.national;
      fs.gen = bs.gen;
      fs.shadow = false;
      fs.entry = bs.entry;
      bs.forms.push_back(fs.index);
    }
  }
  for (const ep::Species& s : sAll) {
    for (const ep::Evolution& e : s.evolutions) {
      const auto it = sByIndex.find(e.to);
      if (it != sByIndex.end() && sAll[it->second].evolvesFrom == 0) sAll[it->second].evolvesFrom = s.index;
    }
  }
  std::fprintf(stderr, "[pokedex] %zu species built from the game's tables\n", sAll.size());
}

void BuildWhere() {
  sWhere.clear();
  sWhereBuilt = true;
  struct Spot {
    void* table;
    u32* count;
    const char* name;
  };
  const Spot spots[] = {{esabapokemondata_E1, esabapokemondata_E1_number, "Rock Poke Spot"},
                        {esabapokemondata_E2, esabapokemondata_E2_number, "Oasis Poke Spot"},
                        {esabapokemondata_E3, esabapokemondata_E3_number, "Cave Poke Spot"},
                        {esabapokemondata_EX, esabapokemondata_EX_number, "Poke Spot (any)"}};
  for (const Spot& sp : spots) {
    if (sp.table == nullptr || sp.count == nullptr) continue;
    const auto* rows = static_cast<const EsaEntryView*>(sp.table);
    for (u32 i = 0; i < *sp.count && i < 64; i++) {
      if (rows[i].id == 0) continue;
      ep::WhereLine l;
      l.place = Tr(sp.name);
      l.level = rows[i].minLevel;
      l.note = rows[i].minLevel == rows[i].maxLevel ? "" : "Lv. " + std::to_string(rows[i].minLevel) + "-" + std::to_string(rows[i].maxLevel);
      sWhere[rows[i].id].spots.push_back(l);
    }
  }
  AddTrainerPlaces(sWhere);
}

std::string sEntriesLanguage;

void LoadEntries() {
  sEntriesTried = true;
  sEntries.clear();
  std::string lang = engine::i18n::Language() ? engine::i18n::Language() : "en";
  if (lang == "us") lang = "en";
  else if (lang == "ge") lang = "de";
  else if (lang == "sp") lang = "es";
  else if (lang == "jp") lang = "ja";
  sEntriesLanguage = lang;
  const bool moonFirst = engine::settings::Global().Bool("gameplay.pokedex_moon_entries").Valid() && engine::settings::Global().Bool("gameplay.pokedex_moon_entries").Get();
  std::ifstream in;
  std::filesystem::path f;
  for (int pass = 0; pass < 2 && !in.is_open(); pass++) {
    const bool moon = (pass == 0) == moonFirst;
    f = xd::UserFolder() / "extra_assets" / "pokedex" / ("entries_" + lang + (moon ? "_moon" : "_sun") + ".txt");
    in.open(f, std::ios::binary);
  }
  auto readFile = [&](std::ifstream& stream) {
    std::string line;
    while (std::getline(stream, line)) {
      if (!line.empty() && line.back() == '\r') line.pop_back();
      const size_t tab = line.find('\t');
      if (tab == std::string::npos) continue;
      const long no = std::strtol(line.substr(0, tab).c_str(), nullptr, 10);
      if (no > 0 && no < 2000 && sEntries.find(static_cast<uint16_t>(no)) == sEntries.end()) sEntries[static_cast<uint16_t>(no)] = line.substr(tab + 1);
    }
  };
  readFile(in);
  for (const char* suffix : {"_oras", "_xy"}) {
    std::ifstream more(xd::UserFolder() / "extra_assets" / "pokedex" / ("entries_" + lang + suffix + ".txt"), std::ios::binary);
    if (more.is_open()) readFile(more);
  }
  if (!sEntries.empty()) std::fprintf(stderr, "[pokedex] %zu entry texts read from %s\n", sEntries.size(), f.string().c_str());
}

bool SettingOn(const char* id) {
  const auto b = engine::settings::Global().Bool(id);
  return b.Valid() && b.Get();
}

}

bool Enabled() { return SettingOn("gameplay.pokedex"); }

std::string Utf8(const uint16_t* chars) {
  std::string out;
  for (int i = 0; chars != nullptr && i < 400 && chars[i] != 0; i++) {
    const uint32_t c = chars[i];
    if (c == 0x201C || c == 0x201D) out += '"';
    else if (c == 0x2019 || c == 0x2018) out += '\'';
    else if (c < 0x20) out += ' ';
    else if (c < 0x80) out += static_cast<char>(c);
    else if (c < 0x800) {
      out += static_cast<char>(0xC0 | (c >> 6));
      out += static_cast<char>(0x80 | (c & 0x3F));
    } else {
      out += static_cast<char>(0xE0 | (c >> 12));
      out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
      out += static_cast<char>(0x80 | (c & 0x3F));
    }
  }
  return out;
}

bool EntriesAvailable() {
  if (!sEntriesTried) LoadEntries();
  return !sEntries.empty();
}

bool Imperial() {
  const std::string lang = engine::i18n::Language();
  return lang.empty() || lang == "us" || lang == "en";
}

const std::vector<ep::Species>& All() {
  if (!sBuilt) Build();
  return sAll;
}
const ep::Species* Find(uint16_t index) {
  All();
  const auto it = sByIndex.find(index);
  return it == sByIndex.end() ? nullptr : &sAll[it->second];
}
void Invalidate() {
  sEntriesTried = false;
  sBuilt = false;
  sWhereBuilt = false;
}

const ep::Where& WhereOf(uint16_t index) {
  static const ep::Where kNone;
  if (!sWhereBuilt) BuildWhere();
  static ep::Where sOut;
  const auto it = sWhere.find(index);
  sOut = it == sWhere.end() ? kNone : it->second;
  const auto y = sYours.find(index);
  if (y != sYours.end()) sOut.yours = y->second;
  return sOut;
}

ep::PlayerState& Player() { return sPlayer; }

ep::Settings DexSettings() {
  ep::Settings s;
  s.showAll = SettingOn("gameplay.pokedex_show_all");
  s.showAllTrainers = s.showAll;
  s.showAllShiny = s.showAll;
  return s;
}

std::vector<uint16_t> MetOrder() {
  std::vector<uint16_t> out;
  const uint16_t count = memoDataGetCount(nullptr);
  for (uint16_t i = 0; i < count; i++) out.push_back(memoDataGetPokemonID(nullptr, i) & 0x3FFF);
  return out;
}

namespace {
constexpr size_t kBoxBytes = 20 + 30 * 0xC4;
}

bool RefreshPlayer() {
  const size_t before = sCaught.size() + sShiny.size();
  sPlayer.seen.clear();
  for (uint16_t id : MetOrder()) {
    if (id != 0) sPlayer.seen.insert(id);
  }
  sYours.clear();
  auto own = [&](void* rec, const std::string& place) {
    if (rec == nullptr || !(pokemonCheckValid(rec) & 0xFF)) return;
    const uint16_t sp = pokemonBiosGetPokemonDataId(rec);
    if (sp == 0) return;
    sCaught.insert(sp);
    ep::WhereLine l;
    l.level = pokemonBiosGetLevel(rec);
    const uint32_t sex = pokemonGetSex(rec) & 0xFF;
    l.sex = sex == 0 ? 'M' : sex == 1 ? 'F' : 0;
    l.place = place;
    if (pokemonBiosGetDarkpokemonDataId(rec) != 0) l.note = Tr("SHADOW");
    sYours[sp].push_back(l);
  };
  for (int slot = 0; slot < 6; slot++) own(reinterpret_cast<void*>(heroGetStatus(nullptr, 3, static_cast<u16>(slot))), Tr("PARTY"));
  if (uint8_t* base = static_cast<uint8_t*>(savedataGetStatus(0, 3))) {
    for (int box = 0; box < 8; box++) {
      for (int i = 0; i < 30; i++) own(base + static_cast<size_t>(box) * kBoxBytes + 20 + static_cast<size_t>(i) * 0xC4, Tr("PC BOX") + " " + std::to_string(box + 1));
    }
  }
  sPlayer.caught = sCaught;
  sPlayer.shinyOwned = sShiny;
  for (uint16_t c : sCaught) sPlayer.seen.insert(c);
  sPlayer.battleFought = [](uint32_t b) { return xd::battles::Won(b); };
  const bool changed = sCaught.size() + sShiny.size() != before;
  if (changed) sChanged++;
  return changed;
}

std::string CaughtText() { return ep::ListToText(sCaught); }
std::string ShinyText() { return ep::ListToText(sShiny); }
void SetLists(const std::string& caught, const std::string& shiny, bool kept) {
  sCaught = ep::ListFromText(caught);
  sShiny = ep::ListFromText(shiny);
  sKept = kept;
  sChanged++;
}
bool ListsKept() { return sKept; }
unsigned ListsChanged() { return sChanged; }

}
