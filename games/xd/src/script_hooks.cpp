#include "xd_gen4_abilities.h"
#include "xd_scripts.h"

#include <mods.h>
#include <script_host.h>

#include <cstdint>
#include <cstdio>
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
uint8_t fightFloorIsGcHeroWin(void* floor, uint16_t result);
void PortVideo_Notice(const char* text, double seconds);
}

namespace {

std::unique_ptr<engine::script::Host> sHost;
std::string sActiveText;

constexpr const char* kHookDamage = "battle.damage";
constexpr const char* kHookPredict = "battle.damage_predict";
constexpr const char* kHookMoveUsed = "battle.move_used";
constexpr const char* kHookTurnStart = "battle.turn_start";
constexpr const char* kHookTurnEnd = "battle.turn_end";
constexpr const char* kHookBattleEnd = "battle.end";

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

void ReportFailures() {
  for (const engine::script::Failure& f : sHost->TakeFailures()) {
    const std::string text = "Script mod " + f.modId + " switched off: " + f.message;
    PortVideo_Notice(text.substr(0, 150).c_str(), 10.0);
  }
}

}

namespace xd::scripts {

void Install(const engine::mods::LoadResult& result) {
  sHost.reset();
  sActiveText.clear();
  if (result.scripts.empty()) {
    return;
  }
  using engine::script::Value;
  sHost = std::make_unique<engine::script::Host>(
      std::vector<std::string>{kHookDamage, kHookPredict, kHookMoveUsed, kHookTurnStart, kHookTurnEnd, kHookBattleEnd},
      engine::script::Limits{},
      [](const std::string& line) { std::fprintf(stderr, "%s\n", line.c_str()); });
  Value type = Value::Table();
  type.Set("FIRE", 10).Set("WATER", 11);
  sHost->SetConstants("TYPE", type);
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
  const uint32_t r = fightActionFlowSyuuryou(action);
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
