#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

extern "C" {
void* PortXD_GameAddress(uint32_t gcAddress);
extern uint32_t* __hsd_rand_seed_hold;
#define FN(n) void n();
#include "xg_calc_fns.inc"
#undef FN
}

struct FightOutPokemon;
void fightSeqFightOutPokemonTokuseiCheckTurn(struct FightOutPokemon*);
namespace {
uint64_t CxxTokuseiCheckTurn(uint64_t a, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t) {
  fightSeqFightOutPokemonTokuseiCheckTurn((FightOutPokemon*)(uintptr_t)a);
  return 0;
}
typedef uint64_t (*Fn)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
struct Entry { const char* name; Fn fn; };
const Entry kFns[] = {
#define FN(n) {#n, reinterpret_cast<Fn>(&n)},
#include "xg_calc_fns.inc"
#undef FN
    {"fightSeqFightOutPokemonTokuseiCheckTurn", &CxxTokuseiCheckTurn},
};

Fn Find(const std::string& nameIn) {
  std::string name = nameIn;
  if (name == "xg_hikae_calc") name = "fightSeqWazaDamageCalcHikaePokemon";
  if (name == "xg_shadow_1BC") name = "PortXD_XG_fightSeqWazaDamageCalcHikaePokemon_1BC";
  for (const Entry& e : kFns) if (name == e.name) return e.fn;
  return nullptr;
}

std::vector<std::string> Split(const std::string& s) {
  std::vector<std::string> out;
  size_t i = 0;
  while (i < s.size()) {
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r')) ++i;
    size_t j = i;
    while (j < s.size() && s[j] != ' ' && s[j] != '\t' && s[j] != '\r') ++j;
    if (j > i) out.push_back(s.substr(i, j - i));
    i = j;
  }
  return out;
}

int64_t Fit(uint64_t v, const std::string& kind) {
  if (kind == "u8") return (int64_t)(v & 0xFF);
  if (kind == "u16") return (int64_t)(v & 0xFFFF);
  if (kind == "u32") return (int64_t)(v & 0xFFFFFFFFu);
  if (kind == "s8") return (int64_t)(int8_t)(v & 0xFF);
  if (kind == "s16") return (int64_t)(int16_t)(v & 0xFFFF);
  if (kind == "s32") return (int64_t)(int32_t)(v & 0xFFFFFFFFu);
  if (kind == "nz") return v != 0 ? 1 : 0;
  return (int64_t)v;
}

alignas(16) uint8_t g_scratch[512];
}

extern "C" void PortXD_RunQuestions(const char* questionFile, const char* outPath) {
  std::FILE* in = std::fopen(questionFile, "r");
  std::FILE* out = std::fopen(outPath, "w");
  if (in == nullptr || out == nullptr || __hsd_rand_seed_hold == nullptr) return;
  std::map<std::string, uint64_t> vars;
  std::memset(g_scratch, 0, sizeof g_scratch);
  vars["SCRATCH"] = (uint64_t)(uintptr_t)g_scratch;
  Fn target = Find("fightTargetGetPtrAsNowFightType");
  Fn callSeq = Find("fightSeq_CallWazaSeq");
  if (target == nullptr || callSeq == nullptr) { std::fprintf(out, "ERR base functions missing\n"); std::fclose(out); return; }
  vars["A"] = target(0x11, 0, 0, 0, 0, 0);
  vars["D"] = target(0x12, 0, 0, 0, 0, 0);
  auto val = [&](const std::string& t, bool* ok) -> uint64_t {
    *ok = true;
    if (!t.empty() && t[0] == '$') {
      auto it = vars.find(t.substr(1));
      if (it == vars.end()) { *ok = false; return 0; }
      return it->second;
    }
    return (uint64_t)(int64_t)std::strtoll(t.c_str(), nullptr, 0);
  };
  char buf[2048];
  while (std::fgets(buf, sizeof buf, in) != nullptr) {
    std::string line(buf);
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.pop_back();
    const std::vector<std::string> w = Split(line);
    if (w.empty() || w[0][0] == '#') continue;
    const std::string& cmd = w[0];
    auto seedOut = [&]() { return (unsigned)*__hsd_rand_seed_hold; };
    if (cmd == "P") { std::fprintf(out, "%s\n", line.c_str()); std::fflush(out); continue; }
    if (cmd == "SEED" && w.size() >= 2) {
      *__hsd_rand_seed_hold = (uint32_t)std::strtoull(w[1].c_str(), nullptr, 16);
      continue;
    }
    if (cmd == "SEQ" || cmd == "SCR") {
      if (w.size() < 2) continue;
      uint32_t gc = 0;
      bool okv;
      const uint64_t idv = val(w[1], &okv);
      if (cmd == "SEQ") {
        const uint8_t* e = static_cast<const uint8_t*>(PortXD_GameAddress(0x80417EDFu)) + 4 * idv;
        gc = (uint32_t)e[0] << 24 | (uint32_t)e[1] << 16 | (uint32_t)e[2] << 8 | e[3];
      } else {
        gc = (uint32_t)idv;
      }
      void* script = gc >= 0x80000000u ? PortXD_GameAddress(gc) : nullptr;
      if (script == nullptr) { std::fprintf(out, "%s => NOSCRIPT %08x\n", line.c_str(), seedOut()); std::fflush(out); continue; }
      std::fprintf(out, "%s => begin %08x\n", line.c_str(), seedOut());
      std::fflush(out);
      callSeq((uint64_t)(uintptr_t)script, 0, 0, 0, 0, 0);
      std::fprintf(out, "%s => ran %08x\n", line.c_str(), seedOut());
      std::fflush(out);
      continue;
    }
    if (cmd == "ARITH" && w.size() >= 5) {
      bool o1, o2;
      const uint32_t x = (uint32_t)val(w[3], &o1), y = (uint32_t)val(w[4], &o2);
      uint32_t r = 0;
      if (w[2] == "add") r = x + y; else if (w[2] == "sub") r = x - y; else if (w[2] == "mul") r = x * y; else if (w[2] == "div") r = y ? x / y : 0;
      vars[w[1]] = r;
      continue;
    }
    if (cmd == "MEM" || cmd == "POKE") {
      bool ok1, ok2;
      if (w.size() < 4) continue;
      uint8_t* p = (uint8_t*)(uintptr_t)val(w[2], &ok1);
      const uint64_t off = val(w[3], &ok2);
      if (!ok1 || !ok2 || p == nullptr) { std::fprintf(out, "%s => BADARG\n", line.c_str()); continue; }
      p += off;
      const int n = w[1] == "u8" ? 1 : w[1] == "u16" ? 2 : 4;
      if (cmd == "MEM") {
        uint32_t v = 0;
        std::memcpy(&v, p, (size_t)n);
        std::fprintf(out, "%s => %u %08x\n", line.c_str(), v, seedOut());
      } else {
        bool ok3;
        const uint64_t v = val(w[4], &ok3);
        std::memcpy(p, &v, (size_t)n);
      }
      continue;
    }
    size_t fnAt = cmd == "V" ? 2 : cmd == "Q" ? 2 : 1;
    if ((cmd != "V" && cmd != "C" && cmd != "Q") || w.size() <= fnAt - 1 + (cmd == "C" ? 0 : 0)) continue;
    if (cmd == "C") fnAt = 1;
    if (w.size() <= fnAt) continue;
    Fn fn = Find(w[fnAt]);
    if (fn == nullptr) { std::fprintf(out, "%s => UNKNOWN_FN\n", line.c_str()); std::fflush(out); continue; }
    uint64_t a[6] = {0, 0, 0, 0, 0, 0};
    bool bad = false;
    for (size_t i = fnAt + 1; i < w.size() && i - fnAt - 1 < 6; ++i) {
      bool ok;
      a[i - fnAt - 1] = val(w[i], &ok);
      if (!ok) bad = true;
    }
    if (bad) { std::fprintf(out, "%s => BADARG\n", line.c_str()); std::fflush(out); continue; }
    const uint64_t r = fn(a[0], a[1], a[2], a[3], a[4], a[5]);
    if (cmd == "V") {
      vars[w[1]] = r;
      std::fprintf(out, "%s => %d %08x\n", line.c_str(), r != 0 ? 1 : 0, seedOut());
    } else if (cmd == "Q") {
      std::fprintf(out, "%s => %lld %08x\n", line.c_str(), (long long)Fit(r, w[1]), seedOut());
    }
    std::fflush(out);
  }
  std::fprintf(out, "end\n");
  std::fclose(out);
  std::fclose(in);
}
