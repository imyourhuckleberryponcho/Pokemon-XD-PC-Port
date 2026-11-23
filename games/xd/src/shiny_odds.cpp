#include "xd_shiny.h"

extern "C" int PortXD_CheatShinyDivisor(void);

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

std::vector<std::string> sActive;
int sDivisor = 8192;
bool sShadow = false, sGifts = false, sSpots = false;
int Divisor() {
  const int cheat = PortXD_CheatShinyDivisor();
  return cheat > 0 && cheat < sDivisor ? cheat : sDivisor;
}
bool Shadow() { return sShadow || PortXD_CheatShinyDivisor() > 0; }
bool Gifts() { return sGifts || PortXD_CheatShinyDivisor() > 0; }
bool Spots() { return sSpots || PortXD_CheatShinyDivisor() > 0; }

uint32_t Mix(uint64_t x) {
  x += 0x9E3779B97F4A7C15ull;
  x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
  x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
  x ^= x >> 31;
  return static_cast<uint32_t>(x >> 16);
}

bool IsShiny(uint32_t trainerRnd, uint32_t rnd) {
  return ((trainerRnd >> 16) ^ (trainerRnd & 0xFFFF) ^ (rnd >> 16) ^ (rnd & 0xFFFF)) < 8;
}

uint32_t Boost(uint32_t rnd, uint32_t trainerRnd, int nature) {
  const int divisor = Divisor();
  if (divisor >= 8192 || IsShiny(trainerRnd, rnd)) return rnd;
  const double p = (1.0 / divisor - 1.0 / 8192.0) / (1.0 - 1.0 / 8192.0);
  const uint32_t h32 = Mix((static_cast<uint64_t>(rnd) << 32) | trainerRnd);
  if (p < 1.0 && h32 >= static_cast<uint32_t>(p * 4294967296.0)) return rnd;
  const uint32_t lo = rnd & 0xFFFF, a = (trainerRnd >> 16) ^ (trainerRnd & 0xFFFF);
  const uint32_t k0 = Mix(static_cast<uint64_t>(rnd) * 31u + trainerRnd) & 7;
  for (uint32_t i = 0; i < 8; i++) {
    const uint32_t k = (k0 + i) & 7;
    const uint32_t candidate = (((a ^ lo ^ k) & 0xFFFF) << 16) | lo;
    if (!IsShiny(trainerRnd, candidate)) continue;
    if (nature >= 0 && static_cast<int>(candidate % 25) != nature) continue;
    std::fprintf(stderr, "[shiny] PID %08X made shiny (%08X, odds 1 in %d)\n", candidate, rnd, divisor);
    return candidate;
  }
  return rnd;
}

}

namespace xd::shiny {

std::vector<std::string> OptionsOf(int oddsIndex, int unlocks) {
  std::vector<std::string> o;
  if (oddsIndex > 0 && oddsIndex < kOddsCount) o.push_back("shiny_odds=" + std::to_string(kOdds[oddsIndex].divisor));
  if ((unlocks & kUnlockShadow) != 0) o.push_back("shiny_shadow=on");
  if ((unlocks & kUnlockGifts) != 0) o.push_back("shiny_gifts=on");
  if ((unlocks & kUnlockSpots) != 0) o.push_back("shiny_spots=on");
  return o;
}

int OddsIndexOf(const std::vector<std::string>& options) {
  for (const std::string& o : options) {
    if (o.rfind("shiny_odds=", 0) != 0) continue;
    const int d = std::atoi(o.c_str() + 11);
    for (int i = 0; i < kOddsCount; i++) {
      if (kOdds[i].divisor == d) return i;
    }
    const int t = std::atoi(o.c_str() + 11);
    if (t < 2 || t > 8192 || std::getenv("XD_SHINY") == nullptr)
      std::fprintf(stderr, "[shiny] this playthrough has odds this version doesn't know (\"%s\"): 1 in 8192\n", o.c_str());
  }
  return 0;
}

int UnlocksOf(const std::vector<std::string>& options) {
  int u = 0;
  for (const std::string& o : options) {
    if (o == "shiny_shadow=on") u |= kUnlockShadow;
    if (o == "shiny_gifts=on") u |= kUnlockGifts;
    if (o == "shiny_spots=on") u |= kUnlockSpots;
  }
  return u;
}

std::vector<std::string> TestSwitch() {
  const char* env = std::getenv("XD_SHINY");
  if (env == nullptr || env[0] == '\0') return {};
  if (const char* o = std::getenv("XD_REQUIRE_ORIGINAL"); o != nullptr && std::strcmp(o, "1") == 0) return {};
  int odds = 0, any = 0;
  int unlocks = 0;
  const std::string text = env;
  for (size_t at = 0; at <= text.size();) {
    const size_t end = std::min(text.find(';', at), text.size());
    const std::string part = text.substr(at, end - at);
    if (part.rfind("odds=", 0) == 0) {
      const int d = std::atoi(part.c_str() + 5);
      for (int i = 0; i < kOddsCount; i++) {
        if (kOdds[i].divisor == d) odds = i;
      }
      if (odds == 0 && d >= 2 && d < 8192) any = d;
    } else if (part == "shadow=on") {
      unlocks |= kUnlockShadow;
    } else if (part == "gifts=on") {
      unlocks |= kUnlockGifts;
    } else if (part == "spots=on") {
      unlocks |= kUnlockSpots;
    }
    at = end + 1;
  }
  std::vector<std::string> o = OptionsOf(odds, unlocks);
  if (any != 0) o.insert(o.begin(), "shiny_odds=" + std::to_string(any));
  return o;
}

void SetActive(const std::vector<std::string>& options, const char* why) {
  sActive = options;
  sDivisor = kOdds[OddsIndexOf(options)].divisor;
  for (const std::string& o : options) {
    const int d = o.rfind("shiny_odds=", 0) == 0 ? std::atoi(o.c_str() + 11) : 0;
    if (d >= 2 && d <= 8192 && std::getenv("XD_SHINY") != nullptr) sDivisor = d;
  }
  const int u = UnlocksOf(options);
  sShadow = (u & kUnlockShadow) != 0, sGifts = (u & kUnlockGifts) != 0, sSpots = (u & kUnlockSpots) != 0;
  if (!options.empty()) {
    std::fprintf(stderr, "[shiny] %s: odds 1 in %d, Shadow Pokemon %s, gifts %s, Bonsly/Munchlax %s\n", why, sDivisor, sShadow ? "can be shiny" : "locked",
                 sGifts ? "can be shiny" : "locked", sSpots ? "can be shiny" : "locked");
  }
}

const std::vector<std::string>& Active() { return sActive; }

}

extern "C" {
uint32_t PortXD_RealCreateRndFit(void* pokemon, int8_t sex, int8_t seikaku, int8_t rare, uint32_t trainerRnd);
uint32_t pokemonCreateRnd(void);
uint16_t pokemonBiosGetDarkpokemonDataId(void* pokemon);
uintptr_t heroGetStatus(void* hero, uint32_t selector, uint16_t index);

uint32_t pokemonCreateRndFit(void* pokemon, int8_t sex, int8_t seikaku, int8_t rare, uint32_t trainerRnd) {
  const uint32_t rnd = PortXD_RealCreateRndFit(pokemon, sex, seikaku, rare, trainerRnd);
  if (pokemon == nullptr || rare != -1 || Divisor() >= 8192) return rnd;
  return Boost(rnd, trainerRnd, seikaku);
}

uint32_t PortXD_FitMaybeShadow(void* pokemon, int8_t sex, int8_t seikaku, int32_t rare, uint32_t trainerRnd) {
  if (Shadow() && rare == 0 && pokemon != nullptr && pokemonBiosGetDarkpokemonDataId(pokemon) != 0) {
    rare = -1;
    std::fprintf(stderr, "[shiny] Shadow Pokemon %u: made without the shiny lock\n", pokemonBiosGetDarkpokemonDataId(pokemon));
  }
  return pokemonCreateRndFit(pokemon, sex, seikaku, static_cast<int8_t>(rare), trainerRnd);
}

uint32_t PortXD_HeroFit(void* pokemon, int32_t sex, int32_t seikaku, int32_t rare, uint32_t trainerRnd) {
  if (Shadow() && rare == 0) {
    rare = -1;
    std::fprintf(stderr, "[shiny] a gift Shadow Pokemon: made without the shiny lock\n");
  }
  return pokemonCreateRndFit(pokemon, static_cast<int8_t>(sex), static_cast<int8_t>(seikaku), static_cast<int8_t>(rare), trainerRnd);
}

uint32_t PortXD_GiftFit(void* pokemon, int32_t sex, int32_t seikaku, int32_t rare, uint32_t trainerRnd) {
  if (Gifts() && rare == 0) rare = -1;
  return pokemonCreateRndFit(pokemon, static_cast<int8_t>(sex), static_cast<int8_t>(seikaku), static_cast<int8_t>(rare), trainerRnd);
}

uint32_t PortXD_SpotFit(void* pokemon, int8_t sex, int8_t seikaku, int8_t rare, uint32_t trainerRnd) {
  if (Spots() && rare == 0) rare = -1;
  return pokemonCreateRndFit(pokemon, sex, seikaku, rare, trainerRnd);
}

uint32_t PortXD_SpotRnd(void) {
  const uint32_t rnd = pokemonCreateRnd();
  if (Divisor() >= 8192) return rnd;
  return Boost(rnd, static_cast<uint32_t>(heroGetStatus(nullptr, 2, 0)), -1);
}
}
