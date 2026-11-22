
#include "battle_replay_file.h"

#include <cstdio>
#include <string>

static int sFailures = 0;
#define CHECK(cond)                                                                                                    \
  do {                                                                                                                 \
    if (!(cond)) {                                                                                                     \
      std::fprintf(stderr, "FAILED line %d: %s\n", __LINE__, #cond);                                                   \
      sFailures++;                                                                                                     \
    }                                                                                                                  \
  } while (0)

using port_replay::File;

static File Sample() {
  File f;
  f.Set("kind", "story");
  f.Set("port_version", "0.7.0");
  f.Set("snapshot", "9313893b9a");
  f.Set("mods", "none");
  f.state.assign(0x27FD0, '\0');
  for (size_t i = 0; i < f.state.size(); i += 97) f.state[i] = static_cast<char>(i * 31);
  f.body = {"start encounter=221 seed=1a2b3c4d", "choice 0 side 0 ai 0 r 1 seed 55 | A 0 1 1 2 3 4 0 0 0 0", "turn 1 seed 77 digest abcdef", "end result 2 turns 1"};
  return f;
}

int main() {
  File f = Sample();
  std::string bytes, why;
  CHECK(port_replay::Write(f, bytes, &why));
  CHECK(bytes.rfind("XDREPLAY 1\n", 0) == 0);
  CHECK(bytes.size() < 20000);
  File g;
  CHECK(port_replay::Parse(bytes, g, &why));
  CHECK(g.header == f.header);
  CHECK(g.state == f.state);
  CHECK(g.body == f.body);
  CHECK(g.Get("kind") == "story" && g.Get("nothing", "x") == "x");


  File h;
  CHECK(port_replay::ReadHeader(bytes, h, &why) && h.Get("snapshot") == "9313893b9a" && h.state.empty() && h.body.empty());


  std::string bad = bytes;
  bad[bad.size() / 2] ^= 0x40;
  CHECK(!port_replay::Parse(bad, g, &why) && !why.empty());
  CHECK(port_replay::Write(f, bytes, &why));
  CHECK(!port_replay::Parse(bytes.substr(0, bytes.rfind("END ")), g, &why) && why.find("cut short") != std::string::npos);
  CHECK(!port_replay::Parse(bytes.substr(0, bytes.size() - 20), g, &why) && !why.empty());
  CHECK(!port_replay::Parse("hello", g, &why) && why.find("not a battle replay") != std::string::npos);
  std::string newer = bytes;
  newer[9] = '9';
  CHECK(!port_replay::Parse(newer, g, &why) && why.find("newer version") != std::string::npos);


  File odd = f;
  odd.Set("kind", "two\nlines");
  CHECK(!port_replay::Write(odd, bytes, &why));
  odd = f;
  odd.Set("Bad Key", "x");
  CHECK(!port_replay::Write(odd, bytes, &why));
  odd = f;
  odd.body.push_back("a\nb");
  CHECK(!port_replay::Write(odd, bytes, &why));


  const std::vector<std::pair<std::string, std::string>> mine = {{"snapshot", "ffffffffff"}, {"mods", "none"}, {"kind", "story"}};
  const auto diff = port_replay::Differences(f, mine, {"snapshot", "mods", "kind", "absent"});
  CHECK(diff.size() == 1 && diff[0].key == "snapshot" && diff[0].sentence.find("9313893b9a") != std::string::npos &&
        diff[0].sentence.find("ffffffffff") != std::string::npos);

  CHECK(port_replay::Hex(0xabc) == "abc");
  std::printf(sFailures == 0 ? "battle_replay_file_test: all passed\n" : "battle_replay_file_test: %d FAILED\n", sFailures);
  return sFailures == 0 ? 0 : 1;
}
