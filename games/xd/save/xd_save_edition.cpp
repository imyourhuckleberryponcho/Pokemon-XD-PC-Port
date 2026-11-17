#include "xd_save_edition.h"

namespace xd::save {
namespace {

constexpr uint16_t kXgStarterIds[2] = {0x0D2F, 0x0AB8};
constexpr uint16_t kXdStarterIds[2] = {0x7148, 0x4A6F};
constexpr uint16_t kXgOnlyItems[2] = {55, 99};

bool Is(uint16_t v, const uint16_t (&ids)[2]) { return v == ids[0] || v == ids[1]; }

void Look(const Pokemon& p, bool& xg, bool& xd) {
  if (p.IsEmpty()) return;
  const uint16_t lo = p.OtTrainerId(), hi = p.OtSecretId();
  if (Is(lo, kXgStarterIds) || Is(hi, kXgStarterIds)) xg = true;
  if (Is(lo, kXdStarterIds) || Is(hi, kXdStarterIds)) xd = true;
}

bool HasXgItem(const std::vector<ItemSlot>& v) {
  for (const ItemSlot& s : v) {
    if (s.count != 0 && (s.item == kXgOnlyItems[0] || s.item == kXgOnlyItems[1])) return true;
  }
  return false;
}

int BoxNameKind(const std::u16string& n) {
  if (n.size() < 5 || n[3] != u' ') return 0;
  for (size_t i = 4; i < n.size(); i++) {
    if (n[i] < u'0' || n[i] > u'9') return 0;
  }
  if (n[0] == u'B' && n[1] == u'o' && n[2] == u'x') return 1;
  if (n[0] == u'B' && n[1] == u'O' && n[2] == u'X') return 2;
  return 0;
}

}

Edition DetectEdition(const Save& s) {
  bool xg = false, xd = false;
  for (const Pokemon& p : s.party) Look(p, xg, xd);
  for (const Box& b : s.boxes) {
    const int kind = BoxNameKind(b.name);
    if (kind == 1) xg = true;
    if (kind == 2) xd = true;
    for (const Pokemon& p : b.slots) Look(p, xg, xd);
  }
  Look(s.dayCare, xg, xd);
  for (const auto& c : s.chambers) {
    for (const Pokemon& p : c) Look(p, xg, xd);
  }
  for (const auto* pocket : {&s.items, &s.keyItems, &s.balls, &s.tmHm, &s.berries, &s.colognes, &s.battleCds, &s.pcItems}) {
    if (HasXgItem(*pocket)) xg = true;
  }
  if (xg && !xd) return Edition::XG;
  if (xd && !xg) return Edition::XD;
  return Edition::Unknown;
}

const char* EditionName(Edition e) { return e == Edition::XG ? "XG" : e == Edition::XD ? "XD" : "unknown"; }

}
