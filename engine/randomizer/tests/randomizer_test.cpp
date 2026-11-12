#include <randomizer.h>

#include <cctype>
#include <cstdio>
#include <string>

namespace er = engine::randomizer;

static bool sOk = true;
static void check(bool cond, const char* what) {
  std::printf("%s %s\n", cond ? "ok  " : "FAIL", what);
  sOk &= cond;
}

int main() {
  er::Rng a(1), b(1), c(2);
  const uint64_t a1 = a.Next();
  check(a1 == b.Next(), "same seed, same numbers");
  check(a1 != c.Next(), "other seed, other numbers");
  er::Rng s1 = er::Rng::Stream(5, "wild"), s2 = er::Rng::Stream(5, "items"), s3 = er::Rng::Stream(5, "wild");
  const uint64_t w = s1.Next();
  check(w == s3.Next() && w != s2.Next(), "one stream per category");
  bool inRange = true;
  for (int i = 0; i < 10000; i++) inRange &= a.Below(7) < 7;
  check(inRange, "Below stays below");

  er::Schema schema;
  schema.prefix = "TST";
  schema.version = 3;
  schema.switches = {{"one", "One", {"off", "on"}}, {"three", "Three", {"off", "a", "b"}}, {"five", "Five", {"0", "1", "2", "3", "4"}, 3}};
  er::Settings st;
  st.seed = 0xDEADBEEFCAFEF00Dull;
  st.Set(schema, "one", 1);
  st.Set(schema, "three", 2);
  st.Set(schema, "five", 4);
  const std::string code = er::EncodeCode(schema, st);
  std::printf("     code: %s\n", code.c_str());
  check(code.rfind("TST3-", 0) == 0, "code starts with prefix + version");
  check(code.find_first_of("01IO", 5) == std::string::npos, "no look-alike characters");
  er::Settings back;
  bool seedOnly = true;
  std::string error;
  check(er::DecodeCode(schema, code, back, seedOnly, error) && !seedOnly && back.seed == st.seed && back.values == st.values,
        "code round-trips");
  std::string lower = code;
  for (char& ch : lower) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  check(er::DecodeCode(schema, lower, back, seedOnly, error) && back.values == st.values, "lower case accepted");
  bool allRefused = true;
  const char* alphabet = "23456789ABCDEFGHJKLMNPQRSTUVWXYZ";
  for (size_t i = 5; i < code.size(); i++) {
    if (code[i] == '-') continue;
    for (const char* p = alphabet; *p; p++) {
      if (*p == code[i]) continue;
      std::string t = code;
      t[i] = *p;
      er::Settings x;
      if (er::DecodeCode(schema, t, x, seedOnly, error)) {
        allRefused &= !(x.seed == st.seed && x.values == st.values);
      }
    }
  }
  check(allRefused, "typos never give the same settings back silently");
  int refused = 0, tried = 0;
  for (size_t i = 5; i < code.size(); i++) {
    if (code[i] == '-') continue;
    std::string t = code;
    t[i] = code[i] == 'Z' ? 'Y' : 'Z';
    er::Settings x;
    tried++;
    refused += er::DecodeCode(schema, t, x, seedOnly, error) ? 0 : 1;
  }
  check(refused == tried, "each changed character refused by the check");
  check(!er::DecodeCode(schema, code + "A", back, seedOnly, error), "extra character refused");
  check(!er::DecodeCode(schema, code.substr(0, code.size() - 1), back, seedOnly, error), "missing character refused");
  check(!er::DecodeCode(schema, "ABC3-2222", back, seedOnly, error), "other prefix refused");

  er::Schema v1;
  v1.prefix = "TST";
  v1.version = 1;
  v1.switches = {schema.switches[0], schema.switches[1]};
  er::Settings old;
  old.seed = 0x0123456789ABCDEFull;
  old.Set(v1, "one", 1);
  old.Set(v1, "three", 1);
  const std::string oldCode = er::EncodeCode(v1, old);
  std::printf("     version-1 code: %s\n", oldCode.c_str());
  check(oldCode.rfind("TST1-", 0) == 0, "the old build's code says version 1");
  er::Settings fromOld;
  fromOld.Set(schema, "five", 3);
  check(er::DecodeCode(schema, oldCode, fromOld, seedOnly, error) && !seedOnly && fromOld.seed == old.seed &&
            fromOld.values.size() == 3 && fromOld.Get(schema, "one") == 1 && fromOld.Get(schema, "three") == 1 &&
            fromOld.Get(schema, "five") == 0,
        "a version-1 code is read by the newer build: same switches, the new one off");
  check(schema.CodeVersion(fromOld) == 1 && er::EncodeCode(schema, fromOld) == oldCode,
        "settings an old build could make keep that build's code, character for character");
  er::Settings newer = fromOld;
  newer.Set(schema, "five", 2);
  const std::string newCode = er::EncodeCode(schema, newer);
  check(schema.CodeVersion(newer) == 3 && newCode.rfind("TST3-", 0) == 0,
        "the new switch on: a code of the version that added it");
  check(er::DecodeCode(schema, newCode, back, seedOnly, error) && back.values == newer.values && back.seed == newer.seed,
        "which round-trips");
  check(!er::DecodeCode(v1, newCode, back, seedOnly, error) && error.find("version 3") != std::string::npos,
        "a newer code is refused by an older build, naming its version");
  std::string forged = oldCode;
  forged[3] = '3';
  check(!er::DecodeCode(schema, forged, back, seedOnly, error), "a code with another version written on it is refused");
  check(!er::DecodeCode(schema, "TST0-2222", back, seedOnly, error), "version 0 refused");
  int oldRefused = 0, oldTried = 0;
  for (size_t i = 5; i < oldCode.size(); i++) {
    if (oldCode[i] == '-') continue;
    std::string t = oldCode;
    t[i] = oldCode[i] == 'Z' ? 'Y' : 'Z';
    er::Settings x;
    oldTried++;
    oldRefused += er::DecodeCode(schema, t, x, seedOnly, error) ? 0 : 1;
  }
  check(oldRefused == oldTried, "each changed character of a version-1 code refused");
  check(!er::DecodeCode(schema, oldCode + "A", back, seedOnly, error), "version-1 code with an extra character refused");

  er::Settings sh;
  sh.Set(schema, "five", 2);
  check(er::DecodeCode(schema, "12345678901234567890", sh, seedOnly, error) && seedOnly && sh.seed == 12345678901234567890ull &&
            sh.Get(schema, "five") == 2,
        "short code = seed, switches kept");
  check(!er::DecodeCode(schema, "99999999999999999999", sh, seedOnly, error), "seed too big refused");

  er::ModOutput m1, m2;
  m1.Set("b", 2, "x", 1);
  m1.Set("a", 10, "y", 2);
  m1.Set("a", 9, "z", 3);
  m2.Set("a", 9, "z", 3);
  m2.Set("b", 2, "x", 1);
  m2.Set("a", 10, "y", 2);
  check(m1.ToJson("n") == m2.ToJson("n") && m1.Count() == 3, "JSON the same whatever the order of Set");
  check(m1.ToJson("n").find("\"a\": {\n    \"#9\"") != std::string::npos, "entries by number, in order");

  std::vector<uint32_t> pool = {1, 2, 3, 4};
  std::vector<uint32_t> strength = {0, 100, 300, 310, 600};
  er::Rng r(9);
  bool similar = true;
  for (int i = 0; i < 200; i++) {
    const uint32_t p = er::PickSimilar(r, pool, strength, 305, 10, nullptr);
    similar &= p == 2 || p == 3;
  }
  check(similar, "similar strength picks within +-10%");
  std::vector<bool> used(5, false);
  used[2] = used[3] = true;
  const uint32_t widened = er::PickSimilar(r, pool, strength, 305, 10, &used);
  check(widened == 1 || widened == 4, "widens when the close ones are used");

  er::Schema grown;
  grown.prefix = "TST";
  grown.version = 3;
  grown.switches = {{"one", "One", {"off", "on"}},
                    {"two", "Two", {"off", "a", "b"}, 2},
                    {"levels", "Levels", {"kept", "+10%", "+20%", "+30%", "+50%", "by strength"}, 3},
                    {"items", "Items", {"kept", "random", "none"}, 3},
                    {"natures", "Natures", {"kept", "random"}, 3}};
  er::Schema grown2 = grown;
  grown2.version = 2;
  grown2.switches.resize(2);
  er::Settings g;
  g.seed = 987654321;
  g.Set(grown, "one", 1);
  g.Set(grown, "two", 2);
  const std::string code2 = er::EncodeCode(grown, g);
  check(code2.rfind("TST2-", 0) == 0 && code2 == er::EncodeCode(grown2, g),
        "new options at their defaults (kept): the code the build before them wrote, character for character");
  er::Settings g2;
  g2.Set(grown, "levels", 3);
  g2.Set(grown, "natures", 1);
  check(er::DecodeCode(grown, code2, g2, seedOnly, error) && g2.seed == g.seed && g2.Get(grown, "one") == 1 && g2.Get(grown, "two") == 2 &&
            g2.Get(grown, "levels") == 0 && g2.Get(grown, "items") == 0 && g2.Get(grown, "natures") == 0,
        "a version-2 code read by the version-3 build: the new options kept");
  bool allRound = true, allVersion = true;
  for (uint32_t levels = 0; levels < 6; levels++) {
    for (uint32_t items = 0; items < 3; items++) {
      for (uint32_t natures = 0; natures < 2; natures++) {
        er::Settings x = g, y;
        x.Set(grown, "levels", levels);
        x.Set(grown, "items", items);
        x.Set(grown, "natures", natures);
        const std::string c3 = er::EncodeCode(grown, x);
        allRound &= er::DecodeCode(grown, c3, y, seedOnly, error) && !seedOnly && y.seed == x.seed && y.values == x.values;
        const bool any = levels != 0 || items != 0 || natures != 0;
        allVersion &= c3.rfind(any ? "TST3-" : "TST2-", 0) == 0;
        if (any) allVersion &= !er::DecodeCode(grown2, c3, y, seedOnly, error);
      }
    }
  }
  check(allRound, "every choice of the new options round-trips through the code");
  check(allVersion, "any of them on: a version-3 code, refused by the build before; all kept: still version 2");
  er::Settings six = g;
  six.Set(grown, "levels", 6);
  check(six.Get(grown, "levels") == 0, "a choice that doesn't exist is not taken");

  auto picks = [](uint64_t seed, const char* stream) {
    er::Rng rng = er::Rng::Stream(seed, stream);
    std::vector<uint32_t> v;
    for (int i = 0; i < 50; i++) v.push_back(rng.Below(25));
    return v;
  };
  check(picks(77, "trainer_natures") == picks(77, "trainer_natures") && picks(77, "trainer_natures") != picks(78, "trainer_natures") &&
            picks(77, "trainer_natures") != picks(77, "trainer_items"),
        "natures and held items: same seed same picks, own stream each");
  bool natureRange = true;
  for (uint32_t v : picks(3, "trainer_natures")) natureRange &= v < 25;
  check(natureRange, "picks stay inside the list");

  bool levelsOk = true;
  for (uint32_t level = 1; level <= 100; level++) {
    levelsOk &= er::ScaleLevel(level, 0, 100) == level;
    for (uint32_t percent : {10u, 20u, 30u, 50u}) {
      const uint32_t now = er::ScaleLevel(level, percent, 100);
      levelsOk &= now >= 1 && now <= 100 && now >= level && (level == 100 || now > level);
      levelsOk &= now <= level + (level * percent + 99) / 100 + 1;
    }
  }
  check(levelsOk, "+N% levels: 1-100, never lower, at least one higher below 100");
  check(er::ScaleLevel(0, 50, 100) == 0 && er::ScaleLevel(20, 10, 100) == 22 && er::ScaleLevel(5, 10, 100) == 6 &&
            er::ScaleLevel(45, 50, 100) == 68 && er::ScaleLevel(80, 50, 100) == 100 && er::ScaleLevel(100, 50, 100) == 100,
        "+N% levels: fixed examples (20 +10% = 22, 5 +10% = 6, 45 +50% = 68, 80 +50% = 100)");
  bool strengthOk = true;
  for (uint32_t level = 1; level <= 100; level++) {
    for (uint32_t old : {180u, 300u, 405u, 600u, 680u}) {
      for (uint32_t now : {180u, 300u, 405u, 600u, 680u}) {
        const uint32_t l = er::LevelForStrength(level, old, now, 100);
        strengthOk &= l >= 1 && l <= 100 && l * 4 >= level * 3 && l * 4 <= level * 5;
        strengthOk &= now > old ? l <= level : now < old ? l >= level : l == level;
      }
    }
  }
  check(strengthOk, "levels by strength: 1-100, within a quarter, lower for a stronger Pokemon, higher for a weaker one");
  check(er::LevelForStrength(40, 400, 500, 100) == 32 && er::LevelForStrength(40, 500, 400, 100) == 50 &&
            er::LevelForStrength(40, 300, 600, 100) == 30 && er::LevelForStrength(40, 0, 600, 100) == 40 &&
            er::LevelForStrength(0, 300, 600, 100) == 0 && er::LevelForStrength(90, 600, 300, 100) == 100,
        "levels by strength: fixed examples");

  std::printf(sOk ? "ALL OK\n" : "FAILED\n");
  return sOk ? 0 : 1;
}
