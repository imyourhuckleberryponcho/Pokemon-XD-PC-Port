#include "pokedex.h"

#include <cstdio>

using namespace engine::pokedex;
static int sFail = 0;
#define CHECK(c, what)                    \
  do {                                    \
    if (!(c)) {                           \
      std::printf("FAIL: %s\n", what);    \
      sFail++;                            \
    }                                     \
  } while (0)

int main() {
  std::vector<Species> all;
  auto add = [&](uint16_t i, uint16_t nat, const char* n, uint8_t t0, uint16_t h, bool mod = false, bool shadow = false) {
    Species s;
    s.index = i;
    s.national = nat;
    s.name = n;
    s.types[0] = t0;
    s.heightDm = h;
    s.mod = mod;
    s.shadow = shadow;
    all.push_back(s);
  };
  add(133, 133, "Eevee", 0, 3);
  add(25, 25, "Pikachu", 12, 4);
  add(300, 200, "Misdreavus", 7, 7, false, true);
  add(900, 0, "Zorblat", 3, 9, true);
  PlayerState p;
  p.seen = {133, 25, 300};
  p.caught = {133};
  Settings set;
  Query q;
  auto nat = Arrange(all, p, set, q, {});
  CHECK(nat.size() == 4 && nat[0] == 25 && nat[1] == 133 && nat[2] == 300 && nat[3] == 900, "National order, mod species last");
  q.order = Order::AToZ;
  auto az = Arrange(all, p, set, q, {});
  CHECK(az[0] == 133 && az[1] == 300 && az[2] == 25, "A-Z: Eevee, Misdreavus, Pikachu");
  q.order = Order::National;
  q.show = Show::Caught;
  CHECK(Arrange(all, p, set, q, {}) == std::vector<uint16_t>{133}, "caught only");
  q.show = Show::NotCaught;
  CHECK(Arrange(all, p, set, q, {}).size() == 3, "not caught yet");
  q.show = Show::Shadow;
  CHECK(Arrange(all, p, set, q, {}) == std::vector<uint16_t>{300}, "shadow filter");
  q.show = Show::FromMods;
  CHECK(Arrange(all, p, set, q, {}) == std::vector<uint16_t>{900}, "mods filter");
  {
    std::vector<Species> withForms = all;
    Species f;
    f.index = 901, f.national = 25, f.name = "Pikachu", f.formOf = 25, f.formName = "Alolan";
    withForms.push_back(f);
    withForms[1].forms.push_back(901);
    Query qq;
    CHECK(Arrange(withForms, p, set, qq, {}).size() == 4, "forms are not listed");
    CHECK(Count(withForms, p).total == 4, "forms are not counted");
  }
  q.show = Show::All;
  q.typeId = 3;
  CHECK(Arrange(all, p, set, q, {}).empty(), "unseen never matches a type");
  q.typeId = 12;
  CHECK(Arrange(all, p, set, q, {}) == std::vector<uint16_t>{25}, "type filter on a seen one");
  q.typeId = -1;
  q.nameContains = "EEV";
  CHECK(Arrange(all, p, set, q, {}) == std::vector<uint16_t>{133}, "name search, case-free");
  q.nameContains.clear();
  q.order = Order::OrderMet;
  auto met = Arrange(all, p, set, q, {300, 25, 133});
  CHECK(met[0] == 300 && met[1] == 25 && met[2] == 133, "order met");
  CHECK(NumberText(all[0]) == "No133" && NumberText(all[3]) == "No---", "number text");
  CHECK(RowName(all[3], Status(p, 900), set) == "-----", "unseen = dashes (P2)");
  CHECK(EvolutionTargetName(all[3], Seen::Unseen, set) == "?-----", "hidden evolution name (P3)");
  set.showAll = true;
  CHECK(RowName(all[3], Status(p, 900), set) == "Zorblat", "show-everything setting");
  set.showAll = false;
  CHECK(!ShinyAllowed(p, 133, set), "shiny not before owning one (P5)");
  p.shinyOwned.insert(133);
  CHECK(ShinyAllowed(p, 133, set), "shiny after owning one");
  WhereLine tl;
  tl.battle = 7;
  CHECK(!TrainerLineVisible(p, tl, set), "trainer not fought: hidden (P4)");
  p.battleFought = [](uint32_t b) { return b == 7; };
  CHECK(TrainerLineVisible(p, tl, set), "trainer fought: shown");
  auto c = Count(all, p);
  CHECK(c.seen == 3 && c.caught == 1 && c.total == 4, "counts");
  CHECK(ListToText({4, 1, 133}) == "1,4,133" && ListFromText("1,4,133,x,0") == std::set<uint16_t>({1, 4, 133}), "list round trip");
  CHECK(HeightText(10, false) == "1.0 m" && HeightText(10, true) == "3'03''", "height");
  CHECK(WeightText(100, false) == "10.0 kg" && WeightText(290, true) == "63.9 lbs.", "weight");
  CHECK(GenderText(255) == "Genderless" && GenderText(31) == "87.9% M  12.1% F", "gender");
  auto w = Wrap("one two three four five", 9.f, [](const std::string& t) { return static_cast<float>(t.size()); });
  CHECK(w.size() == 3 && w[0] == "one two" && w[1] == "three" && w[2] == "four five", "wrap");
  auto w2 = Wrap("abcdefghijkl", 5.f, [](const std::string& t) { return static_cast<float>(t.size()); });
  CHECK(w2.size() == 3 && w2[0] == "abcde", "long word cut");
  std::printf(sFail == 0 ? "pokedex_test: ALL OK\n" : "pokedex_test: %d FAILED\n", sFail);
  return sFail == 0 ? 0 : 1;
}
