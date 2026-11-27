#include "xd_pc_boxes.h"

#include "xd_save_ext.h"

#include <xd_save_convert.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#ifdef XD_PCBOXES_HOOKS
extern "C" {
extern int32_t (*PortXD_PcBoxCount)(void);
extern void* (*PortXD_PcBoxExtraBox)(int32_t box);
extern int32_t (*PortXD_PcBoxGetLastView)(void);
extern void (*PortXD_PcBoxSetLastView)(int32_t box);
void pcboxMakeDefaultName(uint16_t* dst, int32_t boxNumber);
void pokemonInitAry(void* pokemon, uint16_t count);
}
#endif

namespace xd::pcboxes {
namespace {

Box sBoxes[kMaxBoxes - kGameBoxes];
int sCount = kGameBoxes;
int sLastView = 0;
NameFn sName = nullptr;
ClearFn sClear = nullptr;

bool sPending[kMaxBoxes - kGameBoxes];

Box& At(int b) { return sBoxes[b - kGameBoxes]; }

void InitBox(int b) {
  std::memset(&At(b), 0, sizeof(Box));
  sPending[b - kGameBoxes] = true;
}

void Materialize(int b) {
  if (!sPending[b - kGameBoxes]) return;
  sPending[b - kGameBoxes] = false;
  Box& box = At(b);
  if (sClear != nullptr) sClear(&box.pokemon[0][0], kSlots);
  if (sName != nullptr) sName(box.name, b + 1);
}

void Swap16(uint8_t* p) { std::swap(p[0], p[1]); }

void SwapBox(uint8_t* raw) {
  for (size_t i = 0; i < kNameChars; i++) Swap16(raw + i * 2);
  for (int s = 0; s < kSlots; s++) xd::save::SwapPokemonRecord(raw + kNameChars * 2 + static_cast<size_t>(s) * kPokemonBytes);
}

}

int Clamp(int boxes) { return boxes > kGameBoxes && boxes <= kMaxBoxes ? boxes : kGameBoxes; }

void SetInitFunctions(NameFn name, ClearFn clear) {
  sName = name;
  sClear = clear;
}

void Configure(int fromSetup) {
  const int want = Clamp(fromSetup);
  if (want > sCount) Raise(want);
}

int Count() { return sCount; }

bool Raise(int boxes) {
  if (boxes <= sCount || boxes > kMaxBoxes) return false;
  for (int b = sCount; b < boxes; b++) InitBox(b);
  sCount = boxes;
  return true;
}

void Forget() {
  sCount = kGameBoxes;
  sLastView = 0;
  std::memset(sBoxes, 0, sizeof sBoxes);
  std::memset(sPending, 0, sizeof sPending);
}

Box* ExtraBox(int b) {
  if (b < kGameBoxes || b >= sCount) return nullptr;
  Materialize(b);
  return &At(b);
}

int LastView() { return sLastView < sCount ? sLastView : 0; }
void SetLastView(int box) { sLastView = box >= 0 && box < sCount ? box : 0; }

std::vector<uint8_t> SaveSection() {
  std::vector<uint8_t> out;
  if (sCount <= kGameBoxes) return out;
  out.push_back(static_cast<uint8_t>(sCount));
  out.push_back(static_cast<uint8_t>(LastView()));
  for (int b = kGameBoxes; b < sCount; b++) {
    Materialize(b);
    uint8_t raw[kBoxBytes];
    std::memcpy(raw, &At(b), kBoxBytes);
    SwapBox(raw);
    out.insert(out.end(), raw, raw + kBoxBytes);
  }
  return out;
}

bool LoadSection(const uint8_t* d, size_t n, uint16_t version) {
  if (version != 1 || n < 2) return false;
  const int total = d[0];
  if (total <= kGameBoxes || total > kMaxBoxes || n != 2 + static_cast<size_t>(total - kGameBoxes) * kBoxBytes) return false;
  if (total > sCount) Raise(total);
  for (int b = kGameBoxes; b < total; b++) {
    uint8_t* raw = reinterpret_cast<uint8_t*>(&At(b));
    sPending[b - kGameBoxes] = false;
    std::memcpy(raw, d + 2 + static_cast<size_t>(b - kGameBoxes) * kBoxBytes, kBoxBytes);
    SwapBox(raw);
  }
  for (int b = total; b < sCount; b++) InitBox(b);
  sLastView = d[1] < sCount ? d[1] : 0;
  return true;
}

void ResetSection() {
  for (int b = kGameBoxes; b < sCount; b++) InitBox(b);
  sLastView = 0;
}

void Install() {
  xd::saveext::Handler h;
  h.name = "pcbox";
  h.version = 1;
  h.required = true;
  h.save = SaveSection;
  h.load = LoadSection;
  h.reset = ResetSection;
  xd::saveext::Register(h);
#ifdef XD_PCBOXES_HOOKS
  SetInitFunctions([](uint16_t* dst, int n) { pcboxMakeDefaultName(dst, n); },
                   [](uint8_t* p, int count) { pokemonInitAry(p, static_cast<uint16_t>(count)); });
  PortXD_PcBoxCount = []() -> int32_t { return Count(); };
  PortXD_PcBoxExtraBox = [](int32_t b) -> void* { return ExtraBox(b); };
  PortXD_PcBoxGetLastView = []() -> int32_t { return LastView(); };
  PortXD_PcBoxSetLastView = [](int32_t box) { SetLastView(box); };
#endif
}

}
