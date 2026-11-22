#include "xd_memo_overflow.h"

#include "xd_save_ext.h"

#include <cstdint>
#include <cstdio>
#include <vector>

struct Memo;

namespace {

constexpr uint16_t kMain = 500;

std::vector<xd::memo::Entry> sOverflow;

}

extern "C" {

uint16_t PortXD_RealMemoGetCount(Memo* memo);
uint16_t PortXD_RealMemoGetPokemonID(Memo* memo, uint16_t index);
uint32_t PortXD_RealMemoGetRnd(Memo* memo, uint16_t id);
uint32_t PortXD_RealMemoGetTrainerRnd(Memo* memo, uint16_t id);
uint32_t PortXD_RealMemoSetFlag(Memo* memo);
void PortXD_RealMemoSetPokemonID(Memo* memo, uint16_t id);
void PortXD_RealMemoSet(Memo* memo, void* pokemon);
void PortXD_RealMemoInit(Memo* memo);
uint32_t HSD_Rand(void);
uint32_t pokemonBiosGetPokemonDataId(void*);
uint32_t pokemonBiosGetRnd(void*);
uint32_t pokemonBiosGetCatchTrainerRnd(void*);

}

namespace {

bool InMain(Memo* memo, uint16_t id) {
  const uint16_t count = PortXD_RealMemoGetCount(memo);
  for (uint16_t i = 0; i < count; i++) {
    if ((PortXD_RealMemoGetPokemonID(memo, i) & 0x3FFF) == id) return true;
  }
  return false;
}

xd::memo::Entry* InOverflow(uint16_t id) {
  for (auto& e : sOverflow) {
    if ((e.id & 0x3FFF) == id) return &e;
  }
  return nullptr;
}

uint32_t RandomLikeTheGame() {
  const uint32_t low = HSD_Rand();
  const uint32_t high = HSD_Rand() << 16;
  return (high | low) % 0xFFFFFFFFu;
}

void AddOverflow(Memo* memo, uint16_t id, uint32_t rnd, uint32_t trainerRnd) {
  if (InMain(memo, id) || InOverflow(id) != nullptr) return;
  xd::memo::Entry e;
  e.id = static_cast<uint16_t>(id | 0x8000);
  e.rnd = rnd;
  e.trainerRnd = trainerRnd;
  sOverflow.push_back(e);
}

std::vector<uint8_t> SaveSection() {
  std::vector<uint8_t> out;
  if (sOverflow.empty()) return out;
  const auto put32 = [&](uint32_t v) {
    for (int i = 0; i < 4; i++) out.push_back(static_cast<uint8_t>(v >> (8 * i)));
  };
  put32(static_cast<uint32_t>(sOverflow.size()));
  for (const auto& e : sOverflow) {
    out.push_back(static_cast<uint8_t>(e.id));
    out.push_back(static_cast<uint8_t>(e.id >> 8));
    put32(e.trainerRnd);
    put32(e.rnd);
  }
  return out;
}

bool LoadSection(const uint8_t* d, size_t n, uint16_t version) {
  if (version != 1 || n < 4) return false;
  const auto get32 = [&](size_t at) {
    return static_cast<uint32_t>(d[at]) | (static_cast<uint32_t>(d[at + 1]) << 8) | (static_cast<uint32_t>(d[at + 2]) << 16) |
           (static_cast<uint32_t>(d[at + 3]) << 24);
  };
  const uint32_t count = get32(0);
  if (count > 16000 || n != 4 + static_cast<size_t>(count) * 10) return false;
  std::vector<xd::memo::Entry> in;
  for (uint32_t i = 0; i < count; i++) {
    const size_t at = 4 + static_cast<size_t>(i) * 10;
    xd::memo::Entry e;
    e.id = static_cast<uint16_t>(d[at] | (d[at + 1] << 8));
    e.trainerRnd = get32(at + 2);
    e.rnd = get32(at + 6);
    in.push_back(e);
  }
  sOverflow = std::move(in);
  return true;
}

}

namespace xd::memo {

void RegisterSaveExt() {
  xd::saveext::Handler h;
  h.name = "memo";
  h.version = 1;
  h.required = false;
  h.save = SaveSection;
  h.load = LoadSection;
  h.reset = ClearOverflow;
  xd::saveext::Register(h);
}

const std::vector<Entry>& Overflow() { return sOverflow; }
void ClearOverflow() { sOverflow.clear(); }

}

extern "C" {

uint16_t memoDataGetCount(Memo* memo) {
  const uint16_t main = PortXD_RealMemoGetCount(memo);
  return static_cast<uint16_t>(main + (main >= kMain ? sOverflow.size() : 0));
}

uint16_t memoDataGetPokemonID(Memo* memo, uint16_t index) {
  if (index >= kMain) {
    const size_t at = static_cast<size_t>(index) - kMain;
    return at < sOverflow.size() ? sOverflow[at].id : 0;
  }
  return PortXD_RealMemoGetPokemonID(memo, index);
}

uint32_t memoDataGetPokemonRndFromID(Memo* memo, uint16_t id) {
  if (!sOverflow.empty() && !InMain(memo, id)) {
    const xd::memo::Entry* e = InOverflow(id);
    if (e != nullptr) return e->rnd;
  }
  return PortXD_RealMemoGetRnd(memo, id);
}

uint32_t memoDataGetPokemonTrainerRndFromID(Memo* memo, uint16_t id) {
  if (!sOverflow.empty() && !InMain(memo, id)) {
    const xd::memo::Entry* e = InOverflow(id);
    if (e != nullptr) return e->trainerRnd;
  }
  return PortXD_RealMemoGetTrainerRnd(memo, id);
}

uint32_t memoDataSetMemoFlag(Memo* memo) {
  uint32_t result = PortXD_RealMemoSetFlag(memo);
  for (auto& e : sOverflow) {
    if (e.id & 0x8000) result = 1;
    e.id &= 0x3FFF;
  }
  return result;
}

void memoDataSetPokemonID(Memo* memo, uint16_t id) {
  if (PortXD_RealMemoGetCount(memo) < kMain) {
    PortXD_RealMemoSetPokemonID(memo, id);
    return;
  }
  if (InMain(memo, id) || InOverflow(id) != nullptr) return;
  AddOverflow(memo, id, RandomLikeTheGame(), 0);
}

void memoDataSet(Memo* memo, void* pokemon) {
  if (PortXD_RealMemoGetCount(memo) < kMain) {
    PortXD_RealMemoSet(memo, pokemon);
    return;
  }
  const uint16_t id = static_cast<uint16_t>(pokemonBiosGetPokemonDataId(pokemon));
  if (InMain(memo, id) || InOverflow(id) != nullptr) return;
  AddOverflow(memo, id, pokemonBiosGetRnd(pokemon), pokemonBiosGetCatchTrainerRnd(pokemon));
}

void memoInit(Memo* memo) {
  PortXD_RealMemoInit(memo);
  sOverflow.clear();
}

}
