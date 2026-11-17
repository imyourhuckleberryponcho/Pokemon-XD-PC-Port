#include "xd_save_edition.h"

#include <cstdio>

using namespace xd::save;

static int fails = 0;
static void Check(bool ok, const char* what) {
  std::printf("%s: %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok) fails++;
}
static Pokemon Mon(uint16_t species, uint16_t trainerId) {
  Pokemon p;
  p.species = species;
  p.otId = (0x1234u << 16) | trainerId;
  return p;
}

int main() {
  { Save s; Check(DetectEdition(s) == Edition::Unknown, "an empty save is unknown"); }
  { Save s; s.party[0] = Mon(0x85, 0x0D2F); Check(DetectEdition(s) == Edition::XG, "XG starter in the party"); }
  { Save s; s.party[0] = Mon(0x86, 0x7148); Check(DetectEdition(s) == Edition::XD, "XD starter in the party"); }
  { Save s; s.boxes[3].slots[7] = Mon(0x1C0, 0x0AB8); Check(DetectEdition(s) == Edition::XG, "XG starter (evolved) in a PC box"); }
  { Save s; s.party[0] = Mon(0x20, 0x1111); Check(DetectEdition(s) == Edition::Unknown, "other Pokemon only: unknown"); }
  { Save s; s.items.push_back({99, 1}); Check(DetectEdition(s) == Edition::XG, "Lode Stone in the bag"); }
  { Save s; s.pcItems.push_back({55, 2}); Check(DetectEdition(s) == Edition::XG, "Ability Capsule in the PC items"); }
  { Save s; s.pcItems.push_back({55, 0}); Check(DetectEdition(s) == Edition::Unknown, "an empty item slot does not count"); }
  { Save s; s.party[0] = Mon(0x86, 0x4A6F); s.items.push_back({99, 1}); Check(DetectEdition(s) == Edition::Unknown, "both marks: unknown (asked)"); }
  { Save s; s.boxes[0].name = u"Box 1"; s.boxes[1].name = u"Box 2"; Check(DetectEdition(s) == Edition::XG, "default box names 'Box n' = XG"); }
  { Save s; s.boxes[0].name = u"BOX 1"; s.boxes[7].name = u"BOX 8"; Check(DetectEdition(s) == Edition::XD, "default box names 'BOX n' = XD"); }
  { Save s; s.boxes[0].name = u"My Box"; s.boxes[1].name = u"Box"; s.boxes[2].name = u"Box x"; Check(DetectEdition(s) == Edition::Unknown, "renamed boxes do not count"); }
  { Save s; s.boxes[0].name = u"BOX 1"; s.party[0] = Mon(0x85, 0x0D2F); Check(DetectEdition(s) == Edition::Unknown, "box names and starter disagree: unknown (asked)"); }
  { Save s; s.boxes[0].name = u"Box 1"; s.party[0] = Mon(0x85, 0x0D2F); Check(DetectEdition(s) == Edition::XG, "box names and starter agree"); }
  return fails == 0 ? 0 : 1;
}
