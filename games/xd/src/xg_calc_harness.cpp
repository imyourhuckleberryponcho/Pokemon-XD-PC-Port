#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <string>

extern "C" {
void* PortXD_GameAddress(uint32_t gcAddress);
void* PortXD_GameAddressIfKnown(uint32_t gcAddress, uint32_t* size);
int32_t PortXD_RealDamageCalc(void* attacker, void* defender, void* attackSide, uint16_t waza, uint16_t iryoku, int32_t zokusei);
extern uint32_t* __hsd_rand_seed_hold;
uint8_t wazaDB_GetCompatible(uint16_t); uint8_t wazaDB_GetHidenFlag(uint16_t); uint8_t wazaDB_GetBouon(uint16_t);
uint8_t wazaDB_GetOujanosirusi(uint16_t); uint8_t wazaDB_GetOumugaesi(uint16_t); uint8_t wazaDB_GetYokodori(uint16_t);
uint8_t wazaDB_GetDageki(uint16_t); uint8_t wazaDB_GetAddFightKoukaAvg(uint16_t); uint16_t wazaDB_GetSeqId(uint16_t);
int16_t wazaDB_GetIryoku(uint16_t); int32_t wazaDB_GetRangeId(uint16_t); int8_t wazaDB_GetPri(uint16_t);
uint8_t wazaDB_GetZokuseiDataId(uint16_t); uint8_t wazaDB_GetPp(uint16_t); uint8_t wazaCheckDarkWaza(uint16_t);
int32_t waza_GetZokuseiKindData(uint16_t); uint16_t zokuseiGetWazaJoutai(uint16_t, uint16_t);
uint16_t fightOutPokemonGetTokuseiDataId(void*); uint16_t fightOutPokemonGetSoubiItemSoubiDataId(void*); uint16_t figthOutPokemonGetPokemonDataId(void*);
uint8_t fightOutPokemonIsJoutaiNormal(void*); uint8_t fightOutPokemonIsJoutaiDataId(void*, uint16_t); uint8_t fightSideIsJoutaiDataId(void*, uint16_t);
uint32_t fightOutPokemon_GetAbicntPhyAtk(void*); uint32_t fightOutPokemon_GetAbicntPhyDef(void*); uint32_t fightOutPokemon_GetAbicntSpeAtk(void*);
uint32_t fightOutPokemon_GetAbicntSpeDef(void*);
void* fightFloorGetFightOutPokemonPtrToHeroPtr(void*, void*); uint32_t fightOutPokemonGetNowNimbleness(void*, uint8_t, uint8_t, uint16_t, void*);
uint8_t fightSeqIsTenkouHit(uint16_t); uint32_t fightFloorGetNowTenkouDataId(void*, uint8_t); uint8_t fightOutPokemonIsHpMantan(void*);
int8_t pokemonDB_GetTokuseiDataId(uint16_t, uint16_t); uint16_t pokemonDB_GetBasisMaxHp(uint16_t); uint16_t pokemonDB_GetBasisPhyAtk(uint16_t);
uint16_t pokemonDB_GetBasisPhyDef(uint16_t); uint16_t pokemonDB_GetBasisSpeAtk(uint16_t); uint16_t pokemonDB_GetBasisSpeDef(uint16_t);
uint16_t pokemonDB_GetBasisNimbleness(uint16_t); uint16_t pokemonDB_GetGiveExp(uint16_t); uint32_t pokemonDB_GetInitFriend(uint16_t);
uint8_t wazaDB_GetCompatible(uint16_t); uint8_t wazaDB_GetHidenFlag(uint16_t); uint8_t wazaDB_GetBouon(uint16_t);
uint8_t wazaDB_GetOujanosirusi(uint16_t); uint8_t wazaDB_GetOumugaesi(uint16_t); uint8_t wazaDB_GetYokodori(uint16_t);
uint8_t wazaDB_GetDageki(uint16_t); uint8_t wazaDB_GetAddFightKoukaAvg(uint16_t); uint16_t wazaDB_GetSeqId(uint16_t);
int16_t wazaDB_GetIryoku(uint16_t); int32_t wazaDB_GetRangeId(uint16_t); int8_t wazaDB_GetPri(uint16_t);
uint8_t wazaDB_GetZokuseiDataId(uint16_t); uint8_t wazaDB_GetPp(uint16_t); uint8_t wazaCheckDarkWaza(uint16_t);
int32_t waza_GetZokuseiKindData(uint16_t); uint16_t zokuseiGetWazaJoutai(uint16_t, uint16_t);

void PortXD_CalcHarness(void* attacker, void* defender, void* attackSide) {
  static const char* path = std::getenv("XD_CALC_HARNESS");
  static bool done = false;
  if (std::getenv("XD_CALC_QUESTIONS") != nullptr) return;
  if (path == nullptr || path[0] == 0 || done || attacker == nullptr || defender == nullptr || attacker == defender) return;
  done = true;
  std::FILE* f = std::fopen(path, "w");
  if (f == nullptr) return;
  const uint32_t seed = __hsd_rand_seed_hold != nullptr ? *__hsd_rand_seed_hold : 0;
  auto ask = [&](unsigned waza, unsigned power, int type, int dir) {
    if (__hsd_rand_seed_hold != nullptr) *__hsd_rand_seed_hold = seed;
    const int32_t r = dir == 0 ? PortXD_RealDamageCalc(attacker, defender, attackSide, (uint16_t)waza, (uint16_t)power, type)
                               : PortXD_RealDamageCalc(defender, attacker, attackSide, (uint16_t)waza, (uint16_t)power, type);
    std::fprintf(f, "%u %u %d %d %d\n", waza, power, type, dir, (int)r);
  };
  for (int dir = 0; dir < 2; ++dir) {
    for (unsigned w = 0; w < 400; ++w) ask(w, 0, -1, dir);
    for (unsigned w = 0; w < 400; ++w) ask(w, 60, -1, dir);
    for (unsigned w : {1u, 33u, 52u, 94u, 57u, 85u}) {
      for (int t = 0; t < 18; ++t) ask(w, 0, t, dir);
    }
  }
  for (int dir = 0; dir < 2; ++dir)
    for (unsigned w : {133u, 134u, 1u})
      for (unsigned pw = 1; pw <= 150; pw += 7) ask(w, pw, -1, dir);
#define ACC(fn) for (unsigned w = 0; w < 400; ++w) std::fprintf(f, "A " #fn " %u %d\n", w, (int)fn((uint16_t)w));
  ACC(wazaDB_GetCompatible) ACC(wazaDB_GetHidenFlag) ACC(wazaDB_GetBouon) ACC(wazaDB_GetOujanosirusi) ACC(wazaDB_GetOumugaesi)
  ACC(wazaDB_GetYokodori) ACC(wazaDB_GetDageki) ACC(wazaDB_GetAddFightKoukaAvg) ACC(wazaDB_GetSeqId) ACC(wazaDB_GetIryoku)
  ACC(wazaDB_GetRangeId) ACC(wazaDB_GetPri) ACC(wazaDB_GetZokuseiDataId) ACC(wazaDB_GetPp) ACC(wazaCheckDarkWaza)
  ACC(waza_GetZokuseiKindData)
#undef ACC
#define ACS(fn, cast) for (unsigned w = 0; w < 440; ++w) std::fprintf(f, "A " #fn " %u %d\n", w, (int)(cast)fn((uint16_t)w));
  ACS(pokemonDB_GetBasisMaxHp, uint16_t) ACS(pokemonDB_GetBasisPhyAtk, uint16_t) ACS(pokemonDB_GetBasisPhyDef, uint16_t)
  ACS(pokemonDB_GetBasisSpeAtk, uint16_t) ACS(pokemonDB_GetBasisSpeDef, uint16_t) ACS(pokemonDB_GetBasisNimbleness, uint16_t)
  ACS(pokemonDB_GetGiveExp, uint16_t) ACS(pokemonDB_GetInitFriend, uint16_t)
#undef ACS
  for (unsigned w = 0; w < 440; ++w)
    for (unsigned fl = 0; fl < 2; ++fl) std::fprintf(f, "A pokemonDB_GetTokuseiDataId %u,%u %d\n", w, fl, (int)pokemonDB_GetTokuseiDataId((uint16_t)w, (uint16_t)fl));
  for (int who = 0; who < 2; ++who) {
    void* p = who == 0 ? attacker : defender;
    std::fprintf(f, "S %d tokusei %d\n", who, (int)fightOutPokemonGetTokuseiDataId(p));
    std::fprintf(f, "S %d item %d\n", who, (int)fightOutPokemonGetSoubiItemSoubiDataId(p));
    std::fprintf(f, "S %d species %d\n", who, (int)figthOutPokemonGetPokemonDataId(p));
    std::fprintf(f, "S %d normal %d\n", who, (int)fightOutPokemonIsJoutaiNormal(p));
    std::fprintf(f, "S %d hpfull %d\n", who, (int)fightOutPokemonIsHpMantan(p));
    std::fprintf(f, "S %d abicnt %u %u %u %u\n", who, (unsigned)fightOutPokemon_GetAbicntPhyAtk(p) & 255, (unsigned)fightOutPokemon_GetAbicntPhyDef(p) & 255,
                 (unsigned)fightOutPokemon_GetAbicntSpeAtk(p) & 255, (unsigned)fightOutPokemon_GetAbicntSpeDef(p) & 255);
    for (unsigned j = 0; j < 80; ++j) std::fprintf(f, "S %d joutai %u %d\n", who, j, (int)fightOutPokemonIsJoutaiDataId(p, (uint16_t)j));
  }
  for (unsigned j = 0; j < 80; ++j) std::fprintf(f, "S side joutai %u %d\n", j, (int)fightSideIsJoutaiDataId(attackSide, (uint16_t)j));
  std::fprintf(f, "S weather %d\n", (int)fightFloorGetNowTenkouDataId(nullptr, 1));
  for (int who = 0; who < 2; ++who) {
    void* p = who == 0 ? attacker : defender;
    void* hero = fightFloorGetFightOutPokemonPtrToHeroPtr(nullptr, p);
    for (unsigned bd = 0; bd < 2; ++bd)
      for (unsigned tk = 0; tk < 5; ++tk)
        for (unsigned rn : {0u, 7u})
          std::fprintf(f, "N %d %u %u %u %u\n", who, bd, tk, rn, (unsigned)fightOutPokemonGetNowNimbleness(p, (uint8_t)bd, (uint8_t)tk, (uint16_t)rn, hero));
  }
  for (unsigned w = 0; w < 40; ++w) std::fprintf(f, "N tenkouhit %u %u\n", w, (unsigned)fightSeqIsTenkouHit((uint16_t)w));
  {
    const uint8_t* table = static_cast<const uint8_t*>(PortXD_GameAddress(0x80417EDFu));
    for (unsigned i = 0; i < 0xE8; ++i) {
      const uint8_t* e = table + 4 * i;
      const uint32_t gc = (uint32_t)e[0] << 24 | (uint32_t)e[1] << 16 | (uint32_t)e[2] << 8 | e[3];
      uint32_t size = 0;
      const uint8_t* q = gc >= 0x80000000u && gc < 0x81800000u ? static_cast<const uint8_t*>(PortXD_GameAddressIfKnown(gc, &size)) : nullptr;
      if (q == nullptr) { std::fprintf(f, "T %u null\n", i); continue; }
      std::fprintf(f, "T %u ", i);
      for (unsigned k = 0; k < 48; ++k) std::fprintf(f, "%02x", q[k]);
      std::fprintf(f, "\n");
    }
  }
  for (unsigned a = 0; a < 18; ++a)
    for (unsigned b = 0; b < 18; ++b) std::fprintf(f, "A zokuseiGetWazaJoutai %u,%u %d\n", a, b, (int)zokuseiGetWazaJoutai((uint16_t)a, (uint16_t)b));
  std::fprintf(f, "end\n");
  std::fclose(f);
  if (__hsd_rand_seed_hold != nullptr) *__hsd_rand_seed_hold = seed;
}
}

extern "C" {
void PortXD_RealWsDamageLoss(void);
void PortXD_RealWsDamageLossOnly(void);
void WS_CRITICAL_CHECK(void);
void WS_DAMAGE_CALC(void);
void fightSeqDamageLoss(uint8_t, uint8_t);
void fightSeq_NextWazaSeq(int32_t);
void fightWaza_SetCritical(void*, uint32_t);
uint8_t fightWaza_GetCritical(void*);
void fightWaza_SetDamage(void*, int32_t);
int32_t fightWaza_GetDamage(void*);
void* fightTargetGetPtrAsNowFightType(uint32_t, uint32_t);
void* fightOutPokemon_GetFightWazaPtr(void*);
void fightOutPokemon_SetUseWazaDataId(void*, uint16_t);
uint16_t fightOutPokemon_GetUseWazaDataId(void*);
int32_t fightWaza_GetIryoku(void*);
uint16_t fightWaza_GetZokusei(void*);
uint8_t fightWaza_GetDamageValue(void*);
}

static size_t StageOffset(void* p, uint32_t (*getter)(void*)) {
  uint8_t* b = static_cast<uint8_t*>(p);
  for (size_t off = 0x100; off < 0x1400; ++off) {
    const uint8_t keep = b[off];
    b[off] = 0xA7;
    const bool hit = (getter(p) & 255) == 0xA7;
    b[off] = keep;
    if (hit) return off;
  }
  return 0;
}

extern "C" void PortXD_RunQuestions(const char* questionFile, const char* outPath);

static void ScriptHarness() {
  static const char* base = std::getenv("XD_CALC_HARNESS");
  static bool done = false;
  if (base == nullptr || base[0] == 0 || done || __hsd_rand_seed_hold == nullptr) return;
  done = true;
  if (const char* q = std::getenv("XD_CALC_QUESTIONS")) {
    PortXD_RunQuestions(q, (std::string(base) + ".questions").c_str());
    std::fflush(nullptr);
    std::_Exit(0);
  }
  std::string path = std::string(base) + ".script";
  std::FILE* f = std::fopen(path.c_str(), "w");
  if (f == nullptr) return;
  void* attacker = fightTargetGetPtrAsNowFightType(0x11, 0);
  void* waza = fightOutPokemon_GetFightWazaPtr(attacker);
  const uint32_t seed0 = *__hsd_rand_seed_hold;
  auto seedOf = [](unsigned i) { return (uint32_t)(i * 2654435761u + 12345u); };
  for (unsigned i = 0; i < 512; ++i) {
    *__hsd_rand_seed_hold = seedOf(i);
    WS_CRITICAL_CHECK();
    fightSeq_NextWazaSeq(-1);
    std::fprintf(f, "C %u %u %u\n", i, (unsigned)fightWaza_GetCritical(waza), (unsigned)*__hsd_rand_seed_hold);
  }
  std::fprintf(f, "I move %u power %d type %u crit %u damage %d value %u\n", (unsigned)fightOutPokemon_GetUseWazaDataId(attacker),
               (int)fightWaza_GetIryoku(waza), (unsigned)fightWaza_GetZokusei(waza), (unsigned)fightWaza_GetCritical(waza),
               (int)fightWaza_GetDamage(waza), (unsigned)fightWaza_GetDamageValue(waza));
  for (unsigned c = 1; c <= 3; ++c) {
    fightWaza_SetCritical(waza, c);
    *__hsd_rand_seed_hold = seedOf(0);
    WS_DAMAGE_CALC();
    fightSeq_NextWazaSeq(-1);
    std::fprintf(f, "D %u %d %u\n", c, (int)fightWaza_GetDamage(waza), (unsigned)*__hsd_rand_seed_hold);
  }
  {
    const uint16_t move0 = fightOutPokemon_GetUseWazaDataId(attacker);
    fightWaza_SetCritical(waza, 2);
    for (unsigned w = 0; w < 400; ++w) {
      fightOutPokemon_SetUseWazaDataId(attacker, (uint16_t)w);
      *__hsd_rand_seed_hold = seedOf(1);
      WS_DAMAGE_CALC();
      fightSeq_NextWazaSeq(-1);
      std::fprintf(f, "M %u %d %u\n", w, (int)fightWaza_GetDamage(waza), (unsigned)*__hsd_rand_seed_hold);
    }
    fightOutPokemon_SetUseWazaDataId(attacker, move0);
  }
  {
    void* defender = fightTargetGetPtrAsNowFightType(0x12, 0);
    uint8_t* dp = static_cast<uint8_t*>(defender);
    uint8_t* ap = static_cast<uint8_t*>(attacker);
    const uint16_t move0 = fightOutPokemon_GetUseWazaDataId(attacker);
    const size_t oPhyAtk = StageOffset(attacker, fightOutPokemon_GetAbicntPhyAtk), oSpeAtk = StageOffset(attacker, fightOutPokemon_GetAbicntSpeAtk);
    const size_t oPhyDef = StageOffset(defender, fightOutPokemon_GetAbicntPhyDef), oSpeDef = StageOffset(defender, fightOutPokemon_GetAbicntSpeDef);
    std::fprintf(f, "# stage offsets %zu %zu %zu %zu\n", oPhyAtk, oSpeAtk, oPhyDef, oSpeDef);
    const uint8_t keepPd = dp[oPhyDef], keepSd = dp[oSpeDef];
    const uint8_t keepPa = ap[oPhyAtk], keepSa = ap[oSpeAtk];
    static const unsigned pairs[][2] = {{6, 6}, {9, 6}, {12, 6}, {0, 6}, {6, 9}, {6, 12}, {6, 0}, {12, 12}};
    for (const auto& pr : pairs) {
      for (unsigned c = 2; c <= 3; ++c) {
        dp[oSpeDef] = (uint8_t)pr[0];
        dp[oPhyDef] = (uint8_t)pr[1];
        ap[oPhyAtk] = 6;
        ap[oSpeAtk] = 6;
        fightWaza_SetCritical(waza, c);
        for (unsigned w = 0; w < 400; ++w) {
          fightOutPokemon_SetUseWazaDataId(attacker, (uint16_t)w);
          *__hsd_rand_seed_hold = seedOf(1);
          WS_DAMAGE_CALC();
          fightSeq_NextWazaSeq(-1);
          std::fprintf(f, "Z %u %u %u %u %d %u\n", pr[0], pr[1], c, w, (int)fightWaza_GetDamage(waza), (unsigned)*__hsd_rand_seed_hold);
        }
      }
    }
    dp[oPhyDef] = keepPd; dp[oSpeDef] = keepSd;
    ap[oPhyAtk] = keepPa; ap[oSpeAtk] = keepSa;
    fightOutPokemon_SetUseWazaDataId(attacker, move0);
  }
  for (int dmg : {1, 17, 100, 250, 1000}) {
    for (unsigned i = 0; i < 48; ++i) {
      fightWaza_SetDamage(waza, dmg);
      *__hsd_rand_seed_hold = seedOf(i);
      fightSeqDamageLoss(1, 1);
      std::fprintf(f, "L %d %u %d %u\n", dmg, i, (int)fightWaza_GetDamage(waza), (unsigned)*__hsd_rand_seed_hold);
    }
  }
  for (uint32_t addr = 0x80B99000u; addr < 0x80B9B800u; addr += 64) {
    uint32_t size = 0;
    const uint8_t* q = static_cast<const uint8_t*>(PortXD_GameAddressIfKnown(addr, &size));
    if (q == nullptr || size < 64) { std::fprintf(f, "P %08x null\n", addr); continue; }
    std::fprintf(f, "P %08x ", addr);
    for (unsigned k = 0; k < 64; ++k) std::fprintf(f, "%02x", q[k]);
    std::fprintf(f, "\n");
  }
  std::fprintf(f, "end\n");
  std::fclose(f);
  *__hsd_rand_seed_hold = seed0;
}

extern "C" void WS_DAMAGE_LOSS(void) {
  ScriptHarness();
  PortXD_RealWsDamageLoss();
}
extern "C" void WS_DAMAGE_LOSS_ONLY(void) {
  ScriptHarness();
  PortXD_RealWsDamageLossOnly();
}
