#include <mod_values.h>

#include <cstdio>
#include <string>

using namespace engine::mods;

namespace {
bool sOk = true;
void check(bool cond, const std::string& what) {
  std::printf("%s %s\n", cond ? "ok  " : "FAIL", what.c_str());
  sOk &= cond;
}
}

int main() {
  ModValues v;
  std::string why;
  check(v.Get("m", "streak") == 0 && !v.Has("m", "streak"), "a name nobody set is 0");
  check(v.Set("m", "streak", 48, why) && v.Get("m", "streak") == 48 && v.Changed(), "set and read a number bigger than a flag");
  check(!v.Set("m", "Streak", 1, why) && !why.empty(), "a capital letter is refused: " + why);
  check(!v.Set("m", "1st", 1, why), "a name that starts with a digit is refused");
  check(!v.Set("m", "a b", 1, why), "a space in a name is refused");
  check(!v.Set("m", "currency:bp", 1, why), "the colon (a currency's own key) is refused to a script's name");
  check(!v.Set("m", "x", ModValues::kLimit + 1, why), "a number past the limit is refused");
  check(v.Set("m", "x", -ModValues::kLimit, why) && v.Get("m", "x") == -ModValues::kLimit, "the limit itself is fine");
  int64_t r = 0;
  check(v.Add("m", "streak", 2, r, why) && r == 50 && v.Get("m", "streak") == 50, "add");
  check(v.Add("m", "big", ModValues::kLimit, r, why) && v.Add("m", "big", 5, r, why) && r == ModValues::kLimit, "a counter stops at the end of its range");
  check(v.Get("other", "streak") == 0, "another mod does not see it");

  check(!v.Earn("m", "bp", 5, r, why), "earning a currency nobody declared is refused: " + why);
  check(v.Define("m", "bp", "Battle Points", 100, why), "declare a currency");
  check(!v.Define("m", "bp2", "", 100, why) && !v.Define("m", "bp3", "X", 0, why), "a currency needs a name and a cap");
  check(v.Earn("m", "bp", 60, r, why) && r == 60 && v.Balance("m", "bp") == 60, "earn");
  check(v.Earn("m", "bp", 60, r, why) && r == 100, "earning stops at the cap");
  check(!v.Spend("m", "bp", 101, why) && v.Balance("m", "bp") == 100 && why.find("not enough") != std::string::npos, "spending more than there is changes nothing: " + why);
  check(v.Spend("m", "bp", 48, why) && v.Balance("m", "bp") == 52, "spend");
  check(!v.Spend("m", "bp", -1, why), "a negative amount is refused");
  for (int i = 0; i < 8; i++) v.Define("c", "id" + std::to_string(i), "N", 10, why);
  check(!v.Define("c", "ninth", "N", 10, why), "at most 8 currencies per mod");

  std::map<std::string, std::string> versions{{"m", "1.2"}};
  const std::vector<ModValueSet> sets = v.Export(versions);
  bool found = false;
  for (const ModValueSet& s : sets) {
    if (s.mod == "m") found = s.version == "1.2" && s.values.at("streak") == 50 && s.values.at("currency:bp") == 52;
  }
  check(found, "export carries the mod's version and the currency's amount");
  ModValues w;
  size_t dropped = 99;
  w.Load(sets, &dropped);
  check(dropped == 0 && w.Get("m", "streak") == 50 && w.Balance("m", "bp") == 52 && w.Currency("m", "bp") == nullptr && !w.Changed(),
        "load gives the numbers back (the amount is saved, the declaration is not: the mod declares the currency again at start)");
  check(w.Digest() == v.Digest(), "the loaded numbers have the digest of the saved ones");
  ModValues a, b;
  a.Set("m", "k", 3, why), a.Set("m", "j", 4, why);
  b.Set("m", "j", 4, why), b.Set("m", "k", 3, why);
  check(a.Digest() == b.Digest(), "the digest does not depend on the order the numbers were set");
  b.Set("m", "k", 4, why);
  check(a.Digest() != b.Digest(), "a different number is a different digest");

  std::vector<ModValueSet> bad(1);
  bad[0].mod = "m";
  bad[0].values["BAD NAME"] = 1;
  bad[0].values["fine"] = 2;
  bad[0].values["huge"] = ModValues::kLimit + 7;
  ModValues z;
  z.Load(bad, &dropped);
  check(dropped == 2 && z.Get("m", "fine") == 2, "a file with broken names or numbers: they are dropped and counted, the rest loads");
  std::printf(sOk ? "mod_values_test: all passed\n" : "mod_values_test: FAILED\n");
  return sOk ? 0 : 1;
}
