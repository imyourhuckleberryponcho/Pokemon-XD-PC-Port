#include "i18n.h"
#include "xd_added_rows.h"
#include "xd_forms.h"
#include "xd_gen4_abilities.h"
#include "xd_mod_battle.h"
#include "xd_scripts.h"
#include "script_teams.h"
#include "script_ext.h"

#include <mods.h>
#include <script_host.h>
#include <script_names.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <string>

extern "C" {
uint32_t figthOutPokemonGetPokemonDataId(void* fop);
void* fightOutPokemonGetPokemonPtr(void* fop);
uint16_t pokemon_GetHp(void* poke);
uint16_t pokemon_GetMaxHp(void* poke);
uint8_t pokemonBiosGetLevel(void* poke);
uint16_t fightFloor_GetTurnCount(void* floor);
uint16_t fightGetFightResultId(void);
void PortXD_OnlineTurnActions(void);
void PortXD_OnlineTurnEnd(void);
void PortXD_BattleReplayTurnEnd(void);
void PortXD_BattleReplayBattleEnd(uint32_t result);
void PortXD_OnlineBattleEnd(uint32_t result);
uint8_t fightFloorIsGcHeroWin(void* floor, uint16_t result);
void PortVideo_Notice(const char* text, double seconds);
uint32_t GSflagGet(uint32_t id);
void GSflagSet(uint32_t id, uint32_t value);
extern uint32_t* flag_data_number;
uintptr_t heroGetStatus(void* hero, uint32_t selector, uint16_t index);
uint32_t pokemonBiosGetPokemonDataId(void* pokemon);
void* pokemonDataBiosGetPtr(uint16_t species);
uint32_t pokemonDataBiosGetZokuseiDataId(void* data, uint16_t index);
int32_t GSfloorGetCurrent(void);
struct PortGSvec {
  float x, y, z;
};
int32_t heroItemAddItemDataId(void* hero, uint32_t item, uint32_t num, int32_t slot);
uint32_t heroItemCheckHaveItemDataId(void* hero, uint32_t item);
int32_t PortXD_GiftPokemon(int32_t species, int32_t level, int32_t move1, int32_t move2, int32_t move3, int32_t move4, int32_t flags);
extern uint32_t* fight_encount_data_number;
extern void (*PortXD_OnFloorReady)(uint32_t floorID, int32_t mode);
extern void (*PortXD_OnBattleStart)(uint32_t battleID);
extern void (*PortXD_OnBattleEnd)(uint32_t battleID, uint32_t result, int32_t lost);
extern int32_t (*(*PortXD_FieldItemUseFunc)(uint32_t item))(uint16_t, int*);
extern void (*PortXD_OnHeroStep)(void);
int32_t PortXD_GiftShadow(int32_t shadowId, int32_t level);
int32_t PortXD_ShadowSpecies(int32_t shadowId);
uint32_t PortXD_WildRowOfSpot(int32_t spot);
int PortXD_WildArm(int32_t species, int32_t level);
extern "C" uint32_t* __hsd_rand_seed_hold;
void PortXD_WildDisarm(void);
extern void (*PortXD_ScriptOnFlag)(uint32_t id, uint32_t oldValue, uint32_t newValue);
extern void (*PortXD_ScriptOnMoney)(int32_t oldValue, int32_t newValue);
extern void (*PortXD_ScriptOnCaught)(void* pokemon, int32_t slot);
extern void (*PortXD_ScriptOnMenu)(int32_t which);
uint32_t pokemonBiosGetDarkFlag(void* pokemon);
extern uint32_t* pokemon_data_number;
extern uint32_t* waza_data_number;
uint16_t pokemonDB_GetBasisMaxHp(uint16_t);
uint16_t pokemonDB_GetBasisPhyAtk(uint16_t);
uint16_t pokemonDB_GetBasisPhyDef(uint16_t);
uint16_t pokemonDB_GetBasisSpeAtk(uint16_t);
uint16_t pokemonDB_GetBasisSpeDef(uint16_t);
uint16_t pokemonDB_GetBasisNimbleness(uint16_t);
uint16_t pokemonDB_GetGiveExp(uint16_t);
int8_t pokemonDB_GetTokuseiDataId(uint16_t, uint16_t);
uint8_t pokemonDB_GetZokuseiDataId(uint16_t, uint32_t);
int16_t wazaDB_GetIryoku(uint16_t);
int32_t wazaDB_GetRangeId(uint16_t);
int8_t wazaDB_GetPri(uint16_t);
uint8_t wazaDB_GetZokuseiDataId(uint16_t);
uint8_t wazaDB_GetPp(uint16_t);
uint8_t wazaDB_GetAvg(uint16_t);
void* peopleGet(uint32_t group, uint32_t id);
void peopleGetPos(int32_t group, int32_t id, PortGSvec* out);
}

namespace {

using engine::i18n::Tr;

std::unique_ptr<engine::script::Host> sHost;
engine::script::Names sNames;
std::string sActiveText;

constexpr const char* kHookDamage = "battle.damage";
constexpr const char* kHookPredict = "battle.damage_predict";
constexpr const char* kHookMoveUsed = "battle.move_used";
constexpr const char* kHookTurnStart = "battle.turn_start";
constexpr const char* kHookTurnEnd = "battle.turn_end";
constexpr const char* kHookBattleEnd = "battle.end";
constexpr const char* kHookFieldRoomReady = "field.room_ready";
constexpr const char* kHookFieldBattleStart = "field.battle_start";
constexpr const char* kHookFieldBattleEnd = "field.battle_end";
constexpr const char* kHookFieldItemUse = "field.item_use";
constexpr const char* kHookFieldStep = "field.step";
constexpr const char* kHookFieldFlag = "field.flag_changed";
constexpr const char* kHookFieldMoney = "field.money_changed";
constexpr const char* kHookFieldCaught = "field.pokemon_caught";
constexpr const char* kHookFieldMenu = "field.menu_open";
constexpr const char* kHookAbility = "battle.ability";
constexpr const char* kHookAiSwitch = "battle.ai_switch";
constexpr const char* kHookAiItem = "battle.ai_item";
constexpr const char* kHookReplayKept = "field.replay_kept";

constexpr const char* kPrelude =
    "local on = on\n"
    "ITEM_USE = {GAME = 0, REFUSE = 1, CONSUME = 2, KEEP = 3}\n"
    "function on_room_ready(f) on('field.room_ready', function(e) f(e.floor, e.mode) end) end\n"
    "function on_battle_start(f) on('field.battle_start', function(e) f(e.battle) end) end\n"
    "function on_battle_end(f) on('field.battle_end', function(e) f(e.battle, e.result, e.lost) end) end\n"
    "function on_item_use(f) on('field.item_use', function(e) return f(e.item) end) end\n"
    "function on_step(f) on('field.step', function(e) f() end) end\n"
    "function on_flag_changed(f) on('field.flag_changed', function(e) f(e.flag, e.old, e.new) end) end\n"
    "function on_money_changed(f) on('field.money_changed', function(e) f(e.old, e.new) end) end\n"
    "function on_pokemon_caught(f) on('field.pokemon_caught', function(e) f(e.species, e.level, e.slot, e.shadow) end) end\n"
    "MENU = {FIELD = 0}\n"
    "function on_menu_open(f) on('field.menu_open', function(e) f(e.menu) end) end\n"
    "AI = {ALLOW = 0, VETO = 1}\n"
    "function on_replay_kept(f) on('field.replay_kept', function(e) f(e.label, e.battle, e.won) end) end\n";

struct LastBattle {
  bool known = false;
  uint32_t battle = 0, result = 0;
  int32_t lost = 0;
  int won = 0;
} sLastBattle;
uint32_t sPendingBattle = 0;
int sItemCode = 0, sItemConsume = 0;

engine::script::Value Battler(void* fop) {
  using engine::script::Value;
  Value v = Value::Table();
  void* pokemon = fightOutPokemonGetPokemonPtr(fop);
  v.Set("species", static_cast<int64_t>(figthOutPokemonGetPokemonDataId(fop) & 0xFFFF));
  if (pokemon != nullptr) {
    v.Set("level", static_cast<int64_t>(pokemonBiosGetLevel(pokemon)));
    v.Set("hp", static_cast<int64_t>(pokemon_GetHp(pokemon)));
    v.Set("max_hp", static_cast<int64_t>(pokemon_GetMaxHp(pokemon)));
  }
  return v;
}

using engine::script::Value;

const engine::mods::AddedRow* OwnRow(const std::string& mod, const char* table, const std::string& key) {
  for (const engine::mods::AddedRow& r : xd::added::Rows()) {
    if (r.mod == mod && r.table == table && r.key == key) return &r;
  }
  return nullptr;
}

Value ApiFlag(const std::string& mod, const std::vector<Value>& args, std::string& error) {
  if (args.size() != 1) { error = "flag(number or name): one argument"; return Value(); }
  uint32_t id = 0;
  if (args[0].kind == Value::Kind::Int) {
    id = static_cast<uint32_t>(args[0].integer);
  } else if (args[0].kind == Value::Kind::String) {
    const engine::mods::AddedRow* r = OwnRow(mod, "flag_data", args[0].text);
    int64_t named = 0;
    if (r != nullptr) {
      id = r->id;
    } else if (sNames.Find("flag", args[0].text, named)) {
      id = static_cast<uint32_t>(named);
    } else {
      error = "flag(\"" + args[0].text + "\"): this mod has no story flag of that name, and the game's flag names do not have it";
      return Value();
    }
  } else {
    error = "flag(number or name)";
    return Value();
  }
  if (flag_data_number == nullptr || id == 0 || id > *flag_data_number) return Value::Int(0);
  return Value::Int(static_cast<int64_t>(GSflagGet(id)));
}

Value ApiSetFlag(const std::string& mod, const std::vector<Value>& args, std::string& error) {
  if (args.size() != 2 || args[0].kind != Value::Kind::String || (args[1].kind != Value::Kind::Int && args[1].kind != Value::Kind::Bool)) {
    error = "set_flag(\"name\", value)";
    return Value();
  }
  const engine::mods::AddedRow* r = OwnRow(mod, "flag_data", args[0].text);
  if (r == nullptr) { error = "set_flag: this mod has no story flag called \"" + args[0].text + "\" (a script can only set its own mod's flags)"; return Value(); }
  const int64_t value = args[1].kind == Value::Kind::Bool ? (args[1].boolean ? 1 : 0) : args[1].integer;
  const uint32_t bits = r->bytes.empty() ? 1 : (r->bytes[0] & 0x3F);
  if (value < 0 || value >= (1ll << bits)) {
    error = "set_flag(\"" + args[0].text + "\"): the flag has " + std::to_string(bits) + " bit(s), the value must be 0 to " + std::to_string((1ll << bits) - 1);
    return Value();
  }
  if (flag_data_number == nullptr || r->id == 0 || r->id > *flag_data_number) { error = "set_flag: the game's flags are not ready"; return Value(); }
  GSflagSet(r->id, static_cast<uint32_t>(value));
  return Value::Int(value);
}

Value ApiParty(const std::string&, const std::vector<Value>&, std::string&) {
  Value list = Value::List();
  for (int slot = 0; slot < 6; slot++) {
    void* p = reinterpret_cast<void*>(heroGetStatus(nullptr, 3, static_cast<uint16_t>(slot)));
    if (p == nullptr) continue;
    const uint32_t species = pokemonBiosGetPokemonDataId(p) & 0xFFFF;
    if (species == 0) continue;
    Value one = Value::Table();
    one.Set("slot", slot + 1).Set("species", static_cast<int64_t>(species));
    one.Set("level", static_cast<int64_t>(pokemonBiosGetLevel(p)));
    one.Set("hp", static_cast<int64_t>(pokemon_GetHp(p))).Set("max_hp", static_cast<int64_t>(pokemon_GetMaxHp(p)));
    Value types = Value::List();
    if (void* data = pokemonDataBiosGetPtr(static_cast<uint16_t>(species))) {
      types.Add(Value::Int(static_cast<int64_t>(pokemonDataBiosGetZokuseiDataId(data, 0) & 0xFF)));
      types.Add(Value::Int(static_cast<int64_t>(pokemonDataBiosGetZokuseiDataId(data, 1) & 0xFF)));
    }
    one.Set("types", types);
    list.Add(std::move(one));
  }
  return list;
}

bool HeroPosition(float xyz[3]) {
  if (peopleGet(0, 0x64) == nullptr) return false;
  PortGSvec p{0, 0, 0};
  peopleGetPos(0, 0x64, &p);
  xyz[0] = p.x, xyz[1] = p.y, xyz[2] = p.z;
  return true;
}

Value ApiPosition(const std::string&, const std::vector<Value>&, std::string&) {
  Value v = Value::Table();
  float xyz[3] = {0, 0, 0};
  const bool known = HeroPosition(xyz);
  v.Set("floor", static_cast<int64_t>(GSfloorGetCurrent()));
  v.Set("x", static_cast<int64_t>(xyz[0])).Set("y", static_cast<int64_t>(xyz[1])).Set("z", static_cast<int64_t>(xyz[2]));
  v.Set("known", known ? 1 : 0);
  return v;
}

int ZoneInside(const std::string& name) {
  float p[3];
  if (!HeroPosition(p)) return 0;
  const uint32_t floor = static_cast<uint32_t>(GSfloorGetCurrent());
  for (const engine::mods::AddedRow& r : xd::added::Rows()) {
    if (r.table != "floor_zone" || r.id != floor || r.bytes.size() != 16) continue;
    const size_t dot = r.key.find('.');
    if (dot == std::string::npos || r.key.compare(dot + 1, std::string::npos, name) != 0) continue;
    const auto be = [&](size_t at) {
      uint32_t bits = static_cast<uint32_t>(r.bytes[at]) << 24 | static_cast<uint32_t>(r.bytes[at + 1]) << 16 | static_cast<uint32_t>(r.bytes[at + 2]) << 8 | r.bytes[at + 3];
      float f;
      std::memcpy(&f, &bits, 4);
      return f;
    };
    const float dx = p[0] - be(0), dy = p[1] - be(4), dz = p[2] - be(8), rad = be(12);
    const int inside = dx * dx + dy * dy + dz * dz <= rad * rad ? 1 : 0;
    static std::map<std::string, int> last;
    int& was = last.emplace(r.key, -1).first->second;
    if (was != inside) {
      was = inside;
      std::fprintf(stderr, "[script] zone %s: the hero is %s (%.0f %.0f %.0f)\n", r.key.c_str(), inside ? "inside" : "outside", p[0], p[1], p[2]);
    }
    return inside;
  }
  return 0;
}

Value ApiInZone(const std::string&, const std::vector<Value>& args, std::string& error) {
  if (args.size() != 1 || args[0].kind != Value::Kind::String) { error = "in_zone(\"name\")"; return Value(); }
  return Value::Int(ZoneInside(args[0].text));
}

Value ApiRoom(const std::string& mod, const std::vector<Value>& args, std::string& error) {
  if (args.size() != 1 || args[0].kind != Value::Kind::String) { error = "room(\"name\")"; return Value(); }
  const engine::mods::AddedRow* r = OwnRow(mod, "floordata", args[0].text);
  return r != nullptr ? Value::Int(r->id) : Value();
}

Value ApiGiveItem(const std::string& mod, const std::vector<Value>& args, std::string& error) {
  if (args.size() != 2 || args[0].kind != Value::Kind::Int || args[1].kind != Value::Kind::Int) { error = "give_item(item, count): two whole numbers (ITEM.POTION, 2)"; return Value(); }
  Value out = Value::Table();
  const int64_t item = args[0].integer, count = args[1].integer;
  if (count < 1 || count > 999 || item < 1 || item > 0xFFFF) {
    std::fprintf(stderr, "[script %s] give_item(%lld, %lld) refused: the count must be 1 to 999 and the item a real number\n", mod.c_str(), static_cast<long long>(item), static_cast<long long>(count));
    out.Set("status", Value::Text("refused")).Set("given", 0).Set("left", count > 0 ? count : 0);
    return out;
  }
  const int32_t left = heroItemAddItemDataId(nullptr, static_cast<uint32_t>(item), static_cast<uint32_t>(count), -1);
  if (left < 0) {
    out.Set("status", Value::Text("no_such_item")).Set("given", 0).Set("left", count);
  } else {
    out.Set("status", Value::Text(left == 0 ? "ok" : "bag_full")).Set("given", count - left).Set("left", static_cast<int64_t>(left));
  }
  std::fprintf(stderr, "[script %s] give_item(%lld, %lld): %s, %lld left over\n", mod.c_str(), static_cast<long long>(item), static_cast<long long>(count),
               left < 0 ? "no such item" : (left == 0 ? "all given" : "bag full"), static_cast<long long>(left < 0 ? count : left));
  return out;
}

Value ApiHaveItem(const std::string&, const std::vector<Value>& args, std::string& error) {
  if (args.size() != 1 || args[0].kind != Value::Kind::Int) { error = "have_item(item): one whole number (ITEM.POTION)"; return Value(); }
  if (args[0].integer < 1 || args[0].integer > 0xFFFF) return Value::Int(0);
  return Value::Int(static_cast<int64_t>(heroItemCheckHaveItemDataId(nullptr, static_cast<uint32_t>(args[0].integer))));
}

Value ApiGivePokemon(const std::string& mod, const std::vector<Value>& args, std::string& error) {
  if (args.size() < 2 || args.size() > 7) { error = "give_pokemon(species, level [, move1, move2, move3, move4 [, party_only]])"; return Value(); }
  int32_t v[7] = {0, 0, 0, 0, 0, 0, 0};
  for (size_t i = 0; i < args.size(); i++) {
    if (args[i].kind == Value::Kind::Int) v[i] = static_cast<int32_t>(args[i].integer);
    else if (args[i].kind == Value::Kind::Bool && i == 6) v[i] = args[i].boolean ? 1 : 0;
    else if (args[i].kind != Value::Kind::Nil) { error = "give_pokemon: whole numbers (SPECIES.EEVEE, 10, MOVE.TACKLE, ...), true / false last"; return Value(); }
  }
  const int32_t r = PortXD_GiftPokemon(v[0], v[1], v[2], v[3], v[4], v[5], v[6]);
  Value out = Value::Table();
  out.Set("status", Value::Text(r >= 0 ? "party" : r == -1 ? "pc" : r == -2 ? "no_room" : "bad")).Set("slot", static_cast<int64_t>(r >= 0 ? r + 1 : 0));
  std::fprintf(stderr, "[script %s] give_pokemon(%d, %d): %s\n", mod.c_str(), v[0], v[1],
               r >= 0 ? "party" : r == -1 ? "party full, sent to the PC box" : r == -2 ? "no room (party and PC full)" : "bad species or level");
  return out;
}

Value ApiSpeciesInfo(const std::string&, const std::vector<Value>& args, std::string& error) {
  if (args.size() != 1 || args[0].kind != Value::Kind::Int) { error = "species_info(species): one whole number (SPECIES.EEVEE)"; return Value(); }
  if (pokemon_data_number == nullptr || args[0].integer < 1 || args[0].integer >= static_cast<int64_t>(*pokemon_data_number)) return Value();
  const uint16_t n = static_cast<uint16_t>(args[0].integer);
  Value v = Value::Table();
  v.Set("hp", static_cast<int64_t>(pokemonDB_GetBasisMaxHp(n))).Set("attack", static_cast<int64_t>(pokemonDB_GetBasisPhyAtk(n)));
  v.Set("defense", static_cast<int64_t>(pokemonDB_GetBasisPhyDef(n))).Set("sp_attack", static_cast<int64_t>(pokemonDB_GetBasisSpeAtk(n)));
  v.Set("sp_defense", static_cast<int64_t>(pokemonDB_GetBasisSpeDef(n))).Set("speed", static_cast<int64_t>(pokemonDB_GetBasisNimbleness(n)));
  v.Set("base_exp", static_cast<int64_t>(pokemonDB_GetGiveExp(n)));
  Value types = Value::List(), abilities = Value::List();
  for (uint16_t i = 0; i < 2; i++) {
    types.Add(Value::Int(static_cast<int64_t>(pokemonDB_GetZokuseiDataId(n, i) & 0xFF)));
    abilities.Add(Value::Int(static_cast<int64_t>(pokemonDB_GetTokuseiDataId(n, i) & 0xFF)));
  }
  v.Set("types", types).Set("abilities", abilities);
  return v;
}

Value ApiMoveInfo(const std::string&, const std::vector<Value>& args, std::string& error) {
  if (args.size() != 1 || args[0].kind != Value::Kind::Int) { error = "move_info(move): one whole number (MOVE.TACKLE)"; return Value(); }
  if (waza_data_number == nullptr || args[0].integer < 1 || args[0].integer >= static_cast<int64_t>(*waza_data_number)) return Value();
  const uint16_t n = static_cast<uint16_t>(args[0].integer);
  Value v = Value::Table();
  v.Set("power", static_cast<int64_t>(wazaDB_GetIryoku(n))).Set("type", static_cast<int64_t>(wazaDB_GetZokuseiDataId(n) & 0xFF));
  v.Set("pp", static_cast<int64_t>(wazaDB_GetPp(n))).Set("priority", static_cast<int64_t>(wazaDB_GetPri(n)));
  v.Set("accuracy", static_cast<int64_t>(wazaDB_GetAvg(n))).Set("range", static_cast<int64_t>(wazaDB_GetRangeId(n)));
  return v;
}

Value ApiGiveShadow(const std::string& mod, const std::vector<Value>& args, std::string& error) {
  if (args.empty() || args.size() > 2 || args[0].kind != Value::Kind::Int || (args.size() == 2 && args[1].kind != Value::Kind::Int)) {
    error = "give_shadow_pokemon(shadow [, level]): whole numbers";
    return Value();
  }
  const int32_t r = PortXD_GiftShadow(static_cast<int32_t>(args[0].integer), args.size() == 2 ? static_cast<int32_t>(args[1].integer) : 0);
  Value out = Value::Table();
  out.Set("status", Value::Text(r >= 0 ? "party" : r == -2 ? "no_room" : r == -4 ? "already_have" : "bad")).Set("slot", static_cast<int64_t>(r >= 0 ? r + 1 : 0));
  std::fprintf(stderr, "[script %s] give_shadow_pokemon(%lld): %s\n", mod.c_str(), static_cast<long long>(args[0].integer),
               r >= 0 ? "party" : r == -2 ? "no room in the party" : r == -4 ? "the player already has this Shadow" : "no such Shadow row or level");
  return out;
}

Value ApiShadowSpecies(const std::string&, const std::vector<Value>& args, std::string& error) {
  if (args.size() != 1 || args[0].kind != Value::Kind::Int) { error = "shadow_species(shadow): one whole number"; return Value(); }
  const int32_t s = PortXD_ShadowSpecies(static_cast<int32_t>(args[0].integer));
  return s > 0 ? Value::Int(s) : Value();
}

Value ApiWildBattle(const std::string& mod, const std::vector<Value>& args, std::string& error) {
  if (args.size() != 3 || args[0].kind != Value::Kind::Int || args[1].kind != Value::Kind::Int || args[2].kind != Value::Kind::Int) {
    error = "wild_battle(spot, species, level): three whole numbers (spot 1, 2 or 3; SPECIES.EEVEE; 5)";
    return Value();
  }
  const uint32_t row = PortXD_WildRowOfSpot(static_cast<int32_t>(args[0].integer));
  if (row == 0 || !PortXD_WildArm(static_cast<int32_t>(args[1].integer), static_cast<int32_t>(args[2].integer))) {
    std::fprintf(stderr, "[script %s] wild_battle(%lld, %lld, %lld): not started (spot 1 to 3, a real species, level 1 to 100)\n", mod.c_str(), static_cast<long long>(args[0].integer),
                 static_cast<long long>(args[1].integer), static_cast<long long>(args[2].integer));
    return Value::Bool(false);
  }
  sPendingBattle = row;
  std::fprintf(stderr, "[script %s] wild_battle(%lld, %lld, %lld): queued as battle row %u\n", mod.c_str(), static_cast<long long>(args[0].integer), static_cast<long long>(args[1].integer),
               static_cast<long long>(args[2].integer), row);
  return Value::Bool(true);
}

static const Value* Field(const Value& table, const char* name) {
  if (table.kind != Value::Kind::Table) return nullptr;
  for (size_t i = 0; i < table.fieldNames.size(); i++) {
    if (table.fieldNames[i] == name) return &table.fieldValues[i];
  }
  return nullptr;
}
static bool ReadSix(const Value* v, uint8_t* out, int64_t lo, int64_t hi, const char* what, std::string& why) {
  if (v == nullptr || v->kind == Value::Kind::Nil) return true;
  if (v->kind == Value::Kind::Int) {
    if (v->integer < lo || v->integer > hi) { why = std::string(what) + " must be " + std::to_string(lo) + " to " + std::to_string(hi); return false; }
    for (int i = 0; i < 6; i++) out[i] = static_cast<uint8_t>(v->integer);
    return true;
  }
  if (v->kind != Value::Kind::List || v->fieldValues.size() != 6) { why = std::string(what) + " must be one number or a list of 6 (HP, Atk, Def, SpA, SpD, Spe)"; return false; }
  for (int i = 0; i < 6; i++) {
    const Value& e = v->fieldValues[i];
    if (e.kind != Value::Kind::Int || e.integer < lo || e.integer > hi) { why = std::string(what) + " entries are whole numbers " + std::to_string(lo) + " to " + std::to_string(hi); return false; }
    out[i] = static_cast<uint8_t>(e.integer);
  }
  return true;
}
static bool ReadTeam(const Value& list, xd::teams::Team& team, std::string& why) {
  if (list.kind != Value::Kind::List || list.fieldValues.empty()) { why = "the team must be a list of members: {{species = SPECIES.EEVEE, level = 50, moves = {MOVE.TACKLE}}, ...}"; return false; }
  for (const Value& m : list.fieldValues) {
    if (m.kind != Value::Kind::Table) { why = "every member of the team is a table: {species = ..., level = ..., moves = {...}}"; return false; }
    static const char* const kKnown[] = {"species", "level", "item", "moves", "nature", "ability", "friendship", "ivs", "evs"};
    for (const std::string& k : m.fieldNames) {
      if (std::find_if(std::begin(kKnown), std::end(kKnown), [&](const char* n) { return k == n; }) == std::end(kKnown)) {
        why = "a member has a field called \"" + k + "\" that the game does not know (species, level, item, moves, nature, ability, friendship, ivs, evs)";
        return false;
      }
    }
    xd::teams::Member out;
    const auto whole = [&](const char* name, int64_t lo, int64_t hi, int64_t& dest, bool required) {
      const Value* v = Field(m, name);
      if (v == nullptr || v->kind == Value::Kind::Nil) { if (required) why = std::string(name) + " is needed"; return !required; }
      if (v->kind != Value::Kind::Int || v->integer < lo || v->integer > hi) { why = std::string(name) + " must be a whole number " + std::to_string(lo) + " to " + std::to_string(hi); return false; }
      dest = v->integer;
      return true;
    };
    int64_t species = 0, level = 50, item = 0, nature = 0, ability = 0, friendship = 0;
    if (!whole("species", 1, 0xFFFF, species, true) || !whole("level", 1, 100, level, false) || !whole("item", 0, 0xFFFF, item, false) || !whole("nature", 0, 24, nature, false) ||
        !whole("ability", 0, 1, ability, false) || !whole("friendship", 0, 255, friendship, false)) return false;
    out.species = static_cast<uint16_t>(species), out.level = static_cast<uint8_t>(level), out.item = static_cast<uint16_t>(item), out.nature = static_cast<uint8_t>(nature);
    out.ability = static_cast<uint8_t>(ability), out.friendship = static_cast<uint8_t>(friendship);
    if (const Value* mv = Field(m, "moves"); mv != nullptr && mv->kind != Value::Kind::Nil) {
      if (mv->kind != Value::Kind::List || mv->fieldValues.empty() || mv->fieldValues.size() > 4) { why = "moves is a list of 1 to 4 moves"; return false; }
      for (size_t i = 0; i < mv->fieldValues.size(); i++) {
        if (mv->fieldValues[i].kind != Value::Kind::Int || mv->fieldValues[i].integer < 0 || mv->fieldValues[i].integer > 0xFFFF) { why = "moves entries are move numbers (MOVE.TACKLE)"; return false; }
        out.moves[i] = static_cast<uint16_t>(mv->fieldValues[i].integer);
      }
    }
    if (!ReadSix(Field(m, "ivs"), out.iv, 0, 31, "ivs", why) || !ReadSix(Field(m, "evs"), out.ev, 0, 255, "evs", why)) return false;
    team.members.push_back(out);
  }
  return true;
}

Value ApiSetTrainerTeam(const std::string& mod, const std::vector<Value>& args, std::string& error) {
  if (args.size() < 2 || args.size() > 3 || args[0].kind != Value::Kind::Int) { error = "set_trainer_team(slot, members [, options]): slot 1 to 3, a list of members"; return Value(); }
  xd::teams::Team team;
  std::string why;
  if (!ReadTeam(args[1], team, why)) { error = "set_trainer_team: " + why; return Value(); }
  if (args.size() == 3) {
    if (const Value* items = Field(args[2], "ai_items"); items != nullptr) {
      if (items->kind != Value::Kind::List || items->fieldValues.size() > 4) { error = "set_trainer_team: ai_items is a list of up to 4 items"; return Value(); }
      team.hasAiItems = true;
      for (size_t i = 0; i < items->fieldValues.size(); i++) {
        if (items->fieldValues[i].kind != Value::Kind::Int || items->fieldValues[i].integer < 0 || items->fieldValues[i].integer > 0xFFFF) { error = "set_trainer_team: ai_items entries are item numbers"; return Value(); }
        team.aiItems[i] = static_cast<uint16_t>(items->fieldValues[i].integer);
      }
    }
  }
  why = xd::teams::SetFoeTeam(static_cast<int>(args[0].integer), team);
  if (!why.empty()) std::fprintf(stderr, "[script %s] set_trainer_team(%lld): refused: %s\n", mod.c_str(), static_cast<long long>(args[0].integer), why.c_str());
  else std::fprintf(stderr, "[script %s] set_trainer_team(%lld): armed, %zu member(s)\n", mod.c_str(), static_cast<long long>(args[0].integer), team.members.size());
  return Value::Bool(why.empty());
}

Value ApiLendTeam(const std::string& mod, const std::vector<Value>& args, std::string& error) {
  if (args.size() != 1) { error = "lend_team(members): a list of members"; return Value(); }
  xd::teams::Team team;
  std::string why;
  if (!ReadTeam(args[0], team, why)) { error = "lend_team: " + why; return Value(); }
  why = xd::teams::LendTeam(team);
  if (!why.empty()) std::fprintf(stderr, "[script %s] lend_team: refused: %s\n", mod.c_str(), why.c_str());
  else std::fprintf(stderr, "[script %s] lend_team: armed, %zu member(s)\n", mod.c_str(), team.members.size());
  return Value::Bool(why.empty());
}

Value ApiRandomInt(const std::string& mod, const std::vector<Value>& args, std::string& error) {
  static std::map<std::string, uint64_t> counters;
  int64_t lo = 1, hi = 0;
  if (args.size() == 1 && args[0].kind == Value::Kind::Int) hi = args[0].integer;
  else if (args.size() == 2 && args[0].kind == Value::Kind::Int && args[1].kind == Value::Kind::Int) lo = args[0].integer, hi = args[1].integer;
  else { error = "random_int(n) or random_int(lo, hi): whole numbers"; return Value(); }
  if (hi < lo || hi - lo >= (int64_t{1} << 31)) { error = "random_int: the range is empty or too big"; return Value(); }
  uint64_t x = (__hsd_rand_seed_hold != nullptr ? *__hsd_rand_seed_hold : 0) * 0x9E3779B97F4A7C15ull + (counters[mod]++ + 1) * 0xD1B54A32D192ED03ull;
  for (const char c : mod) x = (x ^ static_cast<uint8_t>(c)) * 0x100000001B3ull;
  x ^= x >> 30, x *= 0xBF58476D1CE4E5B9ull, x ^= x >> 27, x *= 0x94D049BB133111EBull, x ^= x >> 31;
  return Value::Int(lo + static_cast<int64_t>(x % static_cast<uint64_t>(hi - lo + 1)));
}

Value ApiStartBattle(const std::string& mod, const std::vector<Value>& args, std::string& error) {
  if (args.size() != 1) { error = "start_battle(battle): a battle number or the name of one of this mod's battles"; return Value(); }
  uint32_t id = 0;
  if (args[0].kind == Value::Kind::Int) {
    id = static_cast<uint32_t>(args[0].integer);
  } else if (args[0].kind == Value::Kind::String) {
    const engine::mods::AddedRow* r = OwnRow(mod, "fight_encount_data", args[0].text);
    if (r == nullptr) { error = "start_battle(\"" + args[0].text + "\"): this mod has no battle of that name"; return Value(); }
    id = r->id;
  } else {
    error = "start_battle(battle): a battle number or name";
    return Value();
  }
  if (id == 0 || fight_encount_data_number == nullptr || id >= *fight_encount_data_number) {
    std::fprintf(stderr, "[script %s] start_battle(%u): there is no such battle row\n", mod.c_str(), id);
    return Value::Bool(false);
  }
  sPendingBattle = id;
  return Value::Bool(true);
}

Value ApiBattleId(const std::string& mod, const std::vector<Value>& args, std::string& error) {
  if (args.size() != 1 || args[0].kind != Value::Kind::String) { error = "battle_id(\"name\")"; return Value(); }
  const engine::mods::AddedRow* r = OwnRow(mod, "fight_encount_data", args[0].text);
  return r != nullptr ? Value::Int(r->id) : Value();
}

Value ApiBattleResult(const std::string&, const std::vector<Value>&, std::string&) {
  if (!sLastBattle.known) return Value();
  Value v = Value::Table();
  v.Set("battle", static_cast<int64_t>(sLastBattle.battle)).Set("result", static_cast<int64_t>(sLastBattle.result));
  v.Set("won", sLastBattle.won).Set("lost", static_cast<int64_t>(sLastBattle.lost != 0 ? 1 : 0));
  return v;
}

Value ApiBattleRules(const std::string& mod, const std::vector<Value>& args, std::string& error) {
  if (args.size() == 1 && args[0].kind == Value::Kind::Table) return xd::scriptext::BattleRulesTable(mod, args, error);
  if (args.size() > 1 || (args.size() == 1 && args[0].kind != Value::Kind::String)) { error = "battle_rules(\"name\"): the name of a rule set (battle/*.json \"rule_sets\")"; return Value(); }
  const std::string name = args.empty() ? std::string() : args[0].text;
  if (!xd::modbattle::SetRules(name)) { error = "battle_rules(\"" + name + "\"): no rule set of that name"; return Value(); }
  return Value::Bool(true);
}

Value ApiNotice(const std::string&, const std::vector<Value>& args, std::string& error) {
  if (args.size() != 1 || args[0].kind != Value::Kind::String) { error = "notice(\"text\")"; return Value(); }
  PortVideo_Notice(args[0].text.substr(0, 120).c_str(), 4.0);
  return Value();
}

void ReportFailures();
bool StoryCaller(const std::string& mod, const std::string& name, const std::vector<engine::script::Value>& args) {
  if (!sHost) return false;
  int64_t result = 0;
  std::string error;
  const bool ok = sHost->CallFunction(mod + ":" + name, args, result, error);
  ReportFailures();
  if (!ok) std::fprintf(stderr, "[script] story call %s:%s: %s\n", mod.c_str(), name.c_str(), error.c_str());
  return ok;
}

void ReportFailures() {
  for (const engine::script::Failure& f : sHost->TakeFailures()) {
    char text[512];
    std::snprintf(text, sizeof text, Tr("Script mod %s switched off: %s"), f.modId.c_str(), f.message.c_str());
    PortVideo_Notice(std::string(text).substr(0, 150).c_str(), 10.0);
  }
}

void FieldRoomReady(uint32_t floorId, int32_t mode) {
  if (!sHost) return;
  Value e = Value::Table();
  e.Set("floor", static_cast<int64_t>(floorId)).Set("mode", static_cast<int64_t>(mode));
  sHost->CallInt(kHookFieldRoomReady, e, "ignored", 0);
  ReportFailures();
}

void FieldBattleStart(uint32_t battle) {
  sLastBattle = LastBattle{};
  sLastBattle.battle = battle;
  xd::scriptext::SetArming(true);
  if (sHost && sHost->HasHandlers(kHookFieldBattleStart)) {
    Value e = Value::Table();
    e.Set("battle", static_cast<int64_t>(battle));
    sHost->CallInt(kHookFieldBattleStart, e, "ignored", 0);
    ReportFailures();
  }
  xd::scriptext::BattleStarted();
  xd::scriptext::SetArming(false);
}

void FieldBattleEnd(uint32_t battle, uint32_t result, int32_t lost) {
  sLastBattle.known = true;
  sLastBattle.battle = battle;
  sLastBattle.result = result;
  sLastBattle.lost = lost;
  sLastBattle.won = fightFloorIsGcHeroWin(nullptr, static_cast<uint16_t>(result)) ? 1 : 0;
  PortXD_WildDisarm();
  xd::teams::Clear();
  xd::scriptext::BattleEnded();
  std::fprintf(stderr, "[script] battle %u ended: result %u, %s%s\n", battle, result, sLastBattle.won ? "the player's side won" : "the player's side did not win",
               lost ? " (a must-win battle: sent back to the Pokemon Center)" : "");
  if (!sHost || !sHost->HasHandlers(kHookFieldBattleEnd)) return;
  Value e = Value::Table();
  e.Set("battle", static_cast<int64_t>(battle)).Set("result", static_cast<int64_t>(result)).Set("lost", static_cast<int64_t>(lost != 0 ? 1 : 0)).Set("won", sLastBattle.won);
  sHost->CallInt(kHookFieldBattleEnd, e, "ignored", 0);
  ReportFailures();
}

void FieldFlagChanged(uint32_t id, uint32_t oldValue, uint32_t newValue) {
  if (!sHost) return;
  Value e = Value::Table();
  e.Set("flag", static_cast<int64_t>(id)).Set("old", static_cast<int64_t>(oldValue)).Set("new", static_cast<int64_t>(newValue));
  sHost->CallInt(kHookFieldFlag, e, "ignored", 0);
  ReportFailures();
}
void FieldMoneyChanged(int32_t oldValue, int32_t newValue) {
  if (!sHost) return;
  Value e = Value::Table();
  e.Set("old", static_cast<int64_t>(oldValue)).Set("new", static_cast<int64_t>(newValue));
  sHost->CallInt(kHookFieldMoney, e, "ignored", 0);
  ReportFailures();
}
void FieldPokemonCaught(void* pokemon, int32_t slot) {
  if (!sHost || pokemon == nullptr) return;
  Value e = Value::Table();
  e.Set("species", static_cast<int64_t>(pokemonBiosGetPokemonDataId(pokemon) & 0xFFFF)).Set("level", static_cast<int64_t>(pokemonBiosGetLevel(pokemon)));
  e.Set("slot", static_cast<int64_t>(slot >= 0 ? slot + 1 : 0)).Set("shadow", static_cast<int64_t>((pokemonBiosGetDarkFlag(pokemon) & 0xFF) ? 1 : 0));
  sHost->CallInt(kHookFieldCaught, e, "ignored", 0);
  ReportFailures();
}
void FieldMenuOpen(int32_t which) {
  if (!sHost) return;
  Value e = Value::Table();
  e.Set("menu", static_cast<int64_t>(which));
  sHost->CallInt(kHookFieldMenu, e, "ignored", 0);
  ReportFailures();
}

void FieldStep() {
  if (!sHost) return;
  sHost->CallInt(kHookFieldStep, Value::Table(), "ignored", 0);
  ReportFailures();
}

int32_t ItemUseTrampoline(uint16_t, int* consume) {
  *consume = sItemConsume;
  return sItemCode;
}

int32_t (*FieldItemUse(uint32_t item))(uint16_t, int*) {
  if (!sHost) return nullptr;
  Value e = Value::Table();
  e.Set("item", static_cast<int64_t>(item));
  const int64_t code = sHost->CallInt(kHookFieldItemUse, e, "result", 0);
  ReportFailures();
  if (code == 0) return nullptr;
  if (code < 0 || code > 3) {
    std::fprintf(stderr, "[script] on_item_use(%u) answered %lld: only ITEM_USE.GAME (0), REFUSE (1), CONSUME (2) and KEEP (3) mean something; the game's own use is kept\n", item, static_cast<long long>(code));
    return nullptr;
  }
  sItemCode = code == 1 ? 1 : 2;
  sItemConsume = code == 2 ? 1 : 0;
  std::fprintf(stderr, "[script] on_item_use(%u): %s\n", item, code == 1 ? "refused, stays in the bag" : code == 2 ? "used, one is taken" : "used, kept");
  return ItemUseTrampoline;
}

void WireFieldHooks(const engine::mods::LoadResult& result) {
  PortXD_OnFloorReady = nullptr, PortXD_OnBattleStart = nullptr, PortXD_OnBattleEnd = nullptr, PortXD_FieldItemUseFunc = nullptr, PortXD_OnHeroStep = nullptr;
  PortXD_ScriptOnFlag = nullptr, PortXD_ScriptOnMoney = nullptr, PortXD_ScriptOnCaught = nullptr, PortXD_ScriptOnMenu = nullptr;
  if (!sHost) return;
  bool wantResult = false;
  for (const auto& f : result.scripts) wantResult = wantResult || f.text.find("battle_result") != std::string::npos;
  std::string wired;
  const auto note = [&](const char* name) { wired += (wired.empty() ? "" : ", ") + std::string(name); };
  if (sHost->HasHandlers(kHookFieldRoomReady)) PortXD_OnFloorReady = FieldRoomReady, note("room_ready");
  bool wantsWild = false;
  for (const auto& f : result.scripts) wantsWild = wantsWild || f.text.find("wild_battle") != std::string::npos;
  bool wantsTeams = false;
  for (const auto& f : result.scripts) wantsTeams = wantsTeams || f.text.find("set_trainer_team") != std::string::npos || f.text.find("lend_team") != std::string::npos ||
                                                    f.text.find("battle_rules") != std::string::npos || f.text.find("battle_tables") != std::string::npos;
  if (wantsTeams) wantResult = true;
  if (wantsWild) wantResult = true;
  if (sHost->HasHandlers(kHookFieldFlag)) PortXD_ScriptOnFlag = FieldFlagChanged, note("flag_changed");
  if (sHost->HasHandlers(kHookFieldMoney)) PortXD_ScriptOnMoney = FieldMoneyChanged, note("money_changed");
  if (sHost->HasHandlers(kHookFieldCaught)) PortXD_ScriptOnCaught = FieldPokemonCaught, note("pokemon_caught");
  if (sHost->HasHandlers(kHookFieldMenu)) PortXD_ScriptOnMenu = FieldMenuOpen, note("menu_open");
  if (wantResult || sHost->HasHandlers(kHookFieldBattleStart) || sHost->HasHandlers(kHookFieldBattleEnd)) {
    PortXD_OnBattleStart = FieldBattleStart, PortXD_OnBattleEnd = FieldBattleEnd;
    note("battle_start/end");
  }
  if (sHost->HasHandlers(kHookFieldItemUse)) PortXD_FieldItemUseFunc = FieldItemUse, note("item_use");
  if (sHost->HasHandlers(kHookFieldStep)) PortXD_OnHeroStep = FieldStep, note("step");
  std::fprintf(stderr, "[script] field hooks wired: %s\n", wired.empty() ? "none" : wired.c_str());
}

}

namespace xd::scripts {

namespace { std::string sLoadProblems; }

void Install(const engine::mods::LoadResult& result) {
  sHost.reset();
  sLoadProblems.clear();
  sNames = engine::script::Names();
  sActiveText.clear();
  sPendingBattle = 0, sLastBattle = LastBattle{};
  PortXD_OnFloorReady = nullptr, PortXD_OnBattleStart = nullptr, PortXD_OnBattleEnd = nullptr, PortXD_FieldItemUseFunc = nullptr, PortXD_OnHeroStep = nullptr;
  PortXD_ScriptOnFlag = nullptr, PortXD_ScriptOnMoney = nullptr, PortXD_ScriptOnCaught = nullptr, PortXD_ScriptOnMenu = nullptr;
  PortXD_WildDisarm();
  xd::teams::Clear();
  xd::scriptext::Reset();
  {
    std::map<std::string, std::string> versions;
    for (const engine::mods::Manifest& m : result.active) versions[m.id] = m.version;
    xd::scriptext::SetVersions(versions);
  }
  if (result.scripts.empty()) {
    return;
  }
  using engine::script::Value;
  sHost = std::make_unique<engine::script::Host>(
      std::vector<std::string>{kHookDamage, kHookPredict, kHookMoveUsed, kHookTurnStart, kHookTurnEnd, kHookBattleEnd, kHookFieldRoomReady, kHookFieldBattleStart,
                               kHookFieldBattleEnd, kHookFieldItemUse, kHookFieldStep, kHookFieldFlag, kHookFieldMoney, kHookFieldCaught, kHookFieldMenu, kHookAbility, kHookAiSwitch, kHookAiItem, kHookReplayKept},
      engine::script::Limits{},
      [](const std::string& line) { std::fprintf(stderr, "%s\n", line.c_str()); });
  sNames = BuildNames(result);
  sNames.Install(*sHost);
  std::fprintf(stderr, "[script] names for numbers: %s\n", sNames.Summary().c_str());
  for (const std::string& note : sNames.Notes()) std::fprintf(stderr, "[script] names: %s\n", note.c_str());
  sHost->SetApi("flag", ApiFlag);
  sHost->SetApi("set_flag", ApiSetFlag);
  sHost->SetApi("party", ApiParty);
  sHost->SetApi("position", ApiPosition);
  sHost->SetApi("room", ApiRoom);
  sHost->SetApi("in_zone", ApiInZone);
  sHost->SetApi("give_item", ApiGiveItem);
  sHost->SetApi("have_item", ApiHaveItem);
  sHost->SetApi("give_pokemon", ApiGivePokemon);
  sHost->SetApi("species_info", ApiSpeciesInfo);
  sHost->SetApi("move_info", ApiMoveInfo);
  sHost->SetApi("give_shadow_pokemon", ApiGiveShadow);
  sHost->SetApi("shadow_species", ApiShadowSpecies);
  sHost->SetApi("wild_battle", ApiWildBattle);
  sHost->SetApi("set_trainer_team", ApiSetTrainerTeam);
  sHost->SetApi("lend_team", ApiLendTeam);
  sHost->SetApi("random_int", ApiRandomInt);
  sHost->SetApi("start_battle", ApiStartBattle);
  sHost->SetApi("battle_id", ApiBattleId);
  sHost->SetApi("battle_result", ApiBattleResult);
  sHost->SetApi("notice", ApiNotice);
  sHost->SetApi("battle_rules", ApiBattleRules);
  xd::scriptext::RegisterApis(*sHost);
  xd::scriptext::SetStoryCaller(StoryCaller);
  sHost->SetPrelude(kPrelude);
  std::string last;
  std::vector<engine::script::Source> sources;
  for (size_t i = 0; i <= result.scripts.size(); i++) {
    if (i == result.scripts.size() || result.scripts[i].modId != last) {
      if (!sources.empty()) {
        std::string error;
        if (sHost->AddMod(last, sources, error)) {
          std::fprintf(stderr, "[script] %s: %zu file(s) loaded\n", last.c_str(), sources.size());
        } else {
          std::fprintf(stderr, "[script] %s: not used: %s\n", last.c_str(), error.c_str());
          sLoadProblems += last + ": not used: " + error + "\n";
        }
        sources.clear();
      }
      if (i == result.scripts.size()) break;
      last = result.scripts[i].modId;
    }
    sources.push_back({result.scripts[i].name, result.scripts[i].text});
  }
  WireFieldHooks(result);
  char digest[32];
  std::snprintf(digest, sizeof digest, "%016llx", static_cast<unsigned long long>(sHost->Digest()));
  for (const std::string& id : sHost->LiveModIds()) {
    sActiveText += (sActiveText.empty() ? "" : ",") + id;
  }
  sActiveText = "script mods " + (sActiveText.empty() ? std::string("none") : sActiveText) + " (scripts " + digest + ")";
  ReportFailures();
}

std::string ActiveText() { return sActiveText; }

void NotifyReplayKept(const char* label, uint32_t battle, int won) {
  if (!sHost || !sHost->HasHandlers(kHookReplayKept)) return;
  Value e = Value::Table();
  e.Set("label", Value::Text(label != nullptr ? label : "")).Set("battle", static_cast<int64_t>(battle)).Set("won", static_cast<int64_t>(won != 0 ? 1 : 0));
  sHost->CallInt(kHookReplayKept, e, "ignored", 0);
  ReportFailures();
}
std::string LoadProblems() { return sLoadProblems; }

}

extern "C" {

int32_t PortXD_ScriptLuaCall(const char* name, int32_t a, int32_t b, int32_t c, int32_t* out) {
  *out = 0;
  if (std::strncmp(name, "zone:", 5) == 0) {
    *out = ZoneInside(name + 5);
    return 1;
  }
  if (!sHost) return 0;
  int64_t result = 0;
  std::string error;
  xd::scriptext::SetInStoryCall(true);
  const bool ok = sHost->CallFunction(name, {Value::Int(a), Value::Int(b), Value::Int(c)}, result, error);
  ReportFailures();
  if (!ok) {
    std::fprintf(stderr, "[script] story call %s: %s\n", name, error.c_str());
    return 0;
  }
  std::fprintf(stderr, "[script] story call %s(%d, %d, %d) = %lld\n", name, a, b, c, static_cast<long long>(result));
  *out = static_cast<int32_t>(result);
  return 1;
}

uint32_t PortXD_TakePendingBattle(void) {
  const uint32_t id = sPendingBattle;
  sPendingBattle = 0;
  return id;
}

int32_t PortXD_BattleWon(void) { return fightFloorIsGcHeroWin(nullptr, fightGetFightResultId()) ? 1 : 0; }

void WS_HP_DEC(void);
void* fightSeq_GetWazaSeq(void);
void* fightTargetGetPtrAsNowFightType(uint32_t kind, uint32_t arg);
void* fightOutPokemon_GetFightWazaPtr(void* fop);
uint16_t fightOutPokemon_GetUseWazaDataId(void* fop);
int32_t fightWaza_GetDamage(void* waza);
void fightWaza_SetDamage(void* waza, int32_t value);
bool fightWazaIsHit(void* waza);
uint16_t fightWaza_GetIryoku(void* waza);
uint16_t fightWaza_GetZokusei(void* waza);

void PortXD_WsHpDecInner(void);
void PortXD_WsHpDec(void) {
  if (xd::modbattle::Active()) {
    const uint8_t* cmd = static_cast<const uint8_t*>(fightSeq_GetWazaSeq());
    void* a = fightTargetGetPtrAsNowFightType(0x11, 0);
    void* d = fightTargetGetPtrAsNowFightType(0x12, 0);
    void* t = cmd != nullptr ? fightTargetGetPtrAsNowFightType(cmd[1], 0) : nullptr;
    void* pk = d != nullptr ? fightOutPokemonGetPokemonPtr(d) : nullptr;
    if (a != nullptr && d != nullptr && a != d && t == d && pk != nullptr) {
      const int32_t before = pokemon_GetHp(pk);
      PortXD_WsHpDecInner();
      xd::modbattle::NoteDamage(a, d, before - static_cast<int32_t>(pokemon_GetHp(pk)));
      return;
    }
  }
  PortXD_WsHpDecInner();
}

void PortXD_WsHpDecInner(void) {
  xd::gen4::MagicGuardHpDec();
  if (sHost && sHost->HasHandlers(kHookDamage)) {
    const uint8_t* seq = static_cast<const uint8_t*>(fightSeq_GetWazaSeq());
    void* attacker = fightTargetGetPtrAsNowFightType(0x11, 0);
    void* defender = fightTargetGetPtrAsNowFightType(0x12, 0);
    void* target = seq != nullptr ? fightTargetGetPtrAsNowFightType(seq[1], 0) : nullptr;
    void* waza = attacker != nullptr ? fightOutPokemon_GetFightWazaPtr(attacker) : nullptr;
    if (waza != nullptr && defender != nullptr && target == defender && fightWazaIsHit(waza)) {
      const int32_t game = fightWaza_GetDamage(waza);
      if (game > 0) {
        using engine::script::Value;
        Value event = Value::Table();
        event.Set("attacker", Battler(attacker));
        event.Set("defender", Battler(defender));
        Value move = Value::Table();
        move.Set("id", static_cast<int64_t>(fightOutPokemon_GetUseWazaDataId(attacker)))
            .Set("power", static_cast<int64_t>(fightWaza_GetIryoku(waza)))
            .Set("type", static_cast<int64_t>(fightWaza_GetZokusei(waza)));
        event.Set("move", move);
        event.Set("game_damage", static_cast<int64_t>(game));
        int64_t damage = sHost->CallInt(kHookDamage, event, "damage", game);
        ReportFailures();
        if (damage < 1) damage = 1;
        if (damage > 9999) damage = 9999;
        if (damage != game) {
          fightWaza_SetDamage(waza, static_cast<int32_t>(damage));
        }
      }
    }
  }
  WS_HP_DEC();
}

uint32_t PortXD_RealTokusei(void* fop);
uint32_t fightOutPokemonGetTokuseiDataId(void* fop) {
  const uint32_t real = PortXD_RealTokusei(fop);
  const uint32_t game = xd::modbattle::ActsAs(real);
  static bool inside = false;
  if (!sHost || fop == nullptr || inside || !sHost->HasHandlers(kHookAbility)) return game;
  inside = true;
  using engine::script::Value;
  Value event = Battler(fop);
  event.Set("game_ability", static_cast<int64_t>(game));
  const int64_t answer = sHost->CallInt(kHookAbility, event, "ability", static_cast<int64_t>(game));
  ReportFailures();
  inside = false;
  if (answer < 0 || answer > 0xFF) return game;
  return static_cast<uint32_t>(answer);
}

int32_t PortXD_RealAiSwitch(void* trainer, void* pokemon, void* target);
uint8_t PortXD_RealAiItem(void* trainer, void* pokemon, void* target);
static bool AiVeto(const char* hook, void* pokemon) {
  if (!sHost || pokemon == nullptr || !sHost->HasHandlers(hook)) return false;
  const int64_t answer = sHost->CallInt(hook, Battler(pokemon), "veto", 0);
  ReportFailures();
  return answer == 1;
}
int32_t fightTrainerAiSelectFightActionIrekae(void* trainer, void* pokemon, void* target) {
  if (AiVeto(kHookAiSwitch, pokemon)) return 0;
  return PortXD_RealAiSwitch(trainer, pokemon, target);
}
uint8_t fightTrainerAiSelectFightActionItem(void* trainer, void* pokemon, void* target) {
  if (AiVeto(kHookAiItem, pokemon)) return 0;
  return PortXD_RealAiItem(trainer, pokemon, target);
}

int32_t PortXD_AiWazaDamageReal(void* trainer, void* pokemon, uint16_t waza, void* target, uint8_t dispatch);

int32_t fightTrainerAiWazaDoWazaDamageFunc(void* trainer, void* pokemon, uint16_t waza, void* target, uint8_t dispatch) {
  const int32_t game = PortXD_AiWazaDamageReal(trainer, pokemon, waza, target, dispatch);
  if (sHost && dispatch == 0 && game > 0 && pokemon != nullptr && target != nullptr && sHost->HasHandlers(kHookPredict)) {
    using engine::script::Value;
    Value event = Value::Table();
    event.Set("attacker", Battler(pokemon));
    event.Set("defender", Battler(target));
    Value move = Value::Table();
    move.Set("id", static_cast<int64_t>(waza));
    event.Set("move", move);
    event.Set("game_damage", static_cast<int64_t>(game));
    int64_t damage = sHost->CallInt(kHookPredict, event, "damage", game);
    ReportFailures();
    if (damage < 1) damage = 1;
    if (damage > 9999) damage = 9999;
    return static_cast<int32_t>(damage);
  }
  return game;
}

uint32_t fightActionFlowFightOutPokemonOutWaza(void* action);

uint32_t PortXD_FlowOutWaza(void* action) {
  const uint32_t r = fightActionFlowFightOutPokemonOutWaza(action);
  if (sHost && (r & 0xFF) == 1 && sHost->HasHandlers(kHookMoveUsed)) {
    void* attacker = fightTargetGetPtrAsNowFightType(0x11, 0);
    void* defender = fightTargetGetPtrAsNowFightType(0x12, 0);
    if (attacker != nullptr) {
      using engine::script::Value;
      Value event = Value::Table();
      event.Set("attacker", Battler(attacker));
      if (defender != nullptr) event.Set("defender", Battler(defender));
      Value move = Value::Table();
      move.Set("id", static_cast<int64_t>(fightOutPokemon_GetUseWazaDataId(attacker)));
      event.Set("move", move);
      event.Set("turn", static_cast<int64_t>(fightFloor_GetTurnCount(nullptr)));
      sHost->CallInt(kHookMoveUsed, event, "ignored", 0);
      ReportFailures();
    }
  }
  return r;
}

uint32_t fightActionFlowAllFightOutPokemonDoFightAction(void* action);
uint32_t fightActionFlowOneTurnPost(void* action);
uint32_t fightActionFlowSyuuryou(void* action);

static int sLastTurnStart = -1;

static void NotifyTurn(const char* hook, int64_t turn) {
  using engine::script::Value;
  Value event = Value::Table();
  event.Set("turn", turn);
  sHost->CallInt(hook, event, "ignored", 0);
  ReportFailures();
}

uint32_t PortXD_FlowDoFightAction(void* action) {
  PortXD_OnlineTurnActions();
  xd::forms::TurnStart();
  if (sHost && sHost->HasHandlers(kHookTurnStart)) {
    const int turn = fightFloor_GetTurnCount(nullptr);
    if (turn != sLastTurnStart) {
      sLastTurnStart = turn;
      NotifyTurn(kHookTurnStart, turn);
    }
  }
  return fightActionFlowAllFightOutPokemonDoFightAction(action);
}

uint32_t PortXD_FlowOneTurnPost(void* action) {
  const bool listening = sHost && sHost->HasHandlers(kHookTurnEnd);
  const bool over = listening && fightGetFightResultId() != 0;
  const uint32_t r = fightActionFlowOneTurnPost(action);
  PortXD_OnlineTurnEnd();
  PortXD_BattleReplayTurnEnd();
  if (listening && !over) {
    NotifyTurn(kHookTurnEnd, fightFloor_GetTurnCount(nullptr));
  }
  return r;
}

uint32_t PortXD_FlowSyuuryou(void* action) {
  const bool listening = sHost && sHost->HasHandlers(kHookBattleEnd);
  const uint16_t result = listening ? fightGetFightResultId() : 0;
  const int won = listening && fightFloorIsGcHeroWin(nullptr, result) ? 1 : 0;
  const int turn = listening ? fightFloor_GetTurnCount(nullptr) : 0;
  const uint16_t onlineResult = fightGetFightResultId();
  const uint32_t r = fightActionFlowSyuuryou(action);
  PortXD_OnlineBattleEnd(onlineResult);
  PortXD_BattleReplayBattleEnd(onlineResult);
  xd::modbattle::BattleEnded();
  xd::forms::BattleEnded();
  sLastTurnStart = -1;
  if (listening) {
    using engine::script::Value;
    Value event = Value::Table();
    event.Set("result", static_cast<int64_t>(result));
    event.Set("won", static_cast<int64_t>(won));
    event.Set("turn", static_cast<int64_t>(turn));
    sHost->CallInt(kHookBattleEnd, event, "ignored", 0);
    ReportFailures();
  }
  return r;
}

}
