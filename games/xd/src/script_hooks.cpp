#include "i18n.h"
#include "xd_added_rows.h"
#include "xd_gen4_abilities.h"
#include "xd_scripts.h"

#include <mods.h>
#include <script_host.h>
#include <script_names.h>

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

constexpr const char* kPrelude =
    "local on = on\n"
    "ITEM_USE = {GAME = 0, REFUSE = 1, CONSUME = 2, KEEP = 3}\n"
    "function on_room_ready(f) on('field.room_ready', function(e) f(e.floor, e.mode) end) end\n"
    "function on_battle_start(f) on('field.battle_start', function(e) f(e.battle) end) end\n"
    "function on_battle_end(f) on('field.battle_end', function(e) f(e.battle, e.result, e.lost) end) end\n"
    "function on_item_use(f) on('field.item_use', function(e) return f(e.item) end) end\n"
    "function on_step(f) on('field.step', function(e) f() end) end\n";

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

Value ApiNotice(const std::string&, const std::vector<Value>& args, std::string& error) {
  if (args.size() != 1 || args[0].kind != Value::Kind::String) { error = "notice(\"text\")"; return Value(); }
  PortVideo_Notice(args[0].text.substr(0, 120).c_str(), 4.0);
  return Value();
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
  if (!sHost || !sHost->HasHandlers(kHookFieldBattleStart)) return;
  Value e = Value::Table();
  e.Set("battle", static_cast<int64_t>(battle));
  sHost->CallInt(kHookFieldBattleStart, e, "ignored", 0);
  ReportFailures();
}

void FieldBattleEnd(uint32_t battle, uint32_t result, int32_t lost) {
  sLastBattle.known = true;
  sLastBattle.battle = battle;
  sLastBattle.result = result;
  sLastBattle.lost = lost;
  sLastBattle.won = fightFloorIsGcHeroWin(nullptr, static_cast<uint16_t>(result)) ? 1 : 0;
  std::fprintf(stderr, "[script] battle %u ended: result %u, %s%s\n", battle, result, sLastBattle.won ? "the player's side won" : "the player's side did not win",
               lost ? " (a must-win battle: sent back to the Pokemon Center)" : "");
  if (!sHost || !sHost->HasHandlers(kHookFieldBattleEnd)) return;
  Value e = Value::Table();
  e.Set("battle", static_cast<int64_t>(battle)).Set("result", static_cast<int64_t>(result)).Set("lost", static_cast<int64_t>(lost != 0 ? 1 : 0)).Set("won", sLastBattle.won);
  sHost->CallInt(kHookFieldBattleEnd, e, "ignored", 0);
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
  if (!sHost) return;
  bool wantResult = false;
  for (const auto& f : result.scripts) wantResult = wantResult || f.text.find("battle_result") != std::string::npos;
  std::string wired;
  const auto note = [&](const char* name) { wired += (wired.empty() ? "" : ", ") + std::string(name); };
  if (sHost->HasHandlers(kHookFieldRoomReady)) PortXD_OnFloorReady = FieldRoomReady, note("room_ready");
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

void Install(const engine::mods::LoadResult& result) {
  sHost.reset();
  sNames = engine::script::Names();
  sActiveText.clear();
  sPendingBattle = 0, sLastBattle = LastBattle{};
  PortXD_OnFloorReady = nullptr, PortXD_OnBattleStart = nullptr, PortXD_OnBattleEnd = nullptr, PortXD_FieldItemUseFunc = nullptr, PortXD_OnHeroStep = nullptr;
  if (result.scripts.empty()) {
    return;
  }
  using engine::script::Value;
  sHost = std::make_unique<engine::script::Host>(
      std::vector<std::string>{kHookDamage, kHookPredict, kHookMoveUsed, kHookTurnStart, kHookTurnEnd, kHookBattleEnd, kHookFieldRoomReady, kHookFieldBattleStart,
                               kHookFieldBattleEnd, kHookFieldItemUse, kHookFieldStep},
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
  sHost->SetApi("start_battle", ApiStartBattle);
  sHost->SetApi("battle_id", ApiBattleId);
  sHost->SetApi("battle_result", ApiBattleResult);
  sHost->SetApi("notice", ApiNotice);
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

void PortXD_WsHpDec(void) {
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
