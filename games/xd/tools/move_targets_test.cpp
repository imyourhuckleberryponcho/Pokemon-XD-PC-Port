#include "../src/xd_move_targets.h"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>

namespace {
int sFailed = 0;
void Check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAILED: %s\n", what);
    sFailed++;
  }
}
std::string Read(const char* path) {
  std::ifstream in(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}
}

int main(int argc, char** argv) {
  using namespace xd::movetargets;
  if (argc >= 4 && std::string(argv[1]) == "--patch") {
    std::string text = Read(argv[2]);
    const bool g57 = argc >= 5 && std::string(argv[4]) == "--gen57";
    const int n = PatchPackMoves(text, g57);
    std::ofstream(argv[3], std::ios::binary) << text;
    std::printf("%d field(s) changed\n", n);
    return 0;
  }
  {
    std::string t = "{\n \"add\": {\"moves\": {\n  \"Aura Sphere\": {\n   \"copy\": \"SWIFT\",\n   \"rangeId\": 4,\n   \"text\": {\"name\": \"AURA SPHERE\", \"rangeId\": 9}\n  },\n"
                    "  \"Mud Bomb\": {\n   \"copy\": \"MUDDY WATER\",\n   \"pp\": 10\n  }\n }}}";
    Check(SetField(t, "Aura Sphere", "rangeId", 0) == 1, "replace reports a change");
    Check(t.find("\"rangeId\": 0,") != std::string::npos, "replaced value");
    Check(t.find("\"rangeId\": 9") != std::string::npos, "the nested rangeId is not touched");
    Check(SetField(t, "Aura Sphere", "rangeId", 0) == 0, "second time: nothing to change");
    Check(SetField(t, "Mud Bomb", "rangeId", 0) == 1, "insert reports a change");
    const size_t mud = t.find("\"Mud Bomb\"");
    Check(mud != std::string::npos && t.find("\"rangeId\": 0,", mud) != std::string::npos, "inserted into the right entry");
    Check(SetField(t, "Nothing", "rangeId", 0) == 0, "no such entry");
    Check(SetField(t, "SWIFT", "rangeId", 3) == 0, "a name that is only a value is no entry");
  }
  {
    std::string t = "{\"Sticky Web\": {\"copy\": \"Splash\", \"rangeId\": 5}}";
    Check(SetField(t, "Sticky Web", "rangeId", 7) == 1 && t == "{\"Sticky Web\": {\"copy\": \"Splash\", \"rangeId\": 7}}", "last field replaced");
    std::string u = "{\"Chatter\": {\"copy\": \"Psybeam\", \"bouon\": 0, \"avg\": 100}}";
    Check(SetField(u, "Chatter", "bouon", 1) == 1 && u == "{\"Chatter\": {\"copy\": \"Psybeam\", \"bouon\": 1, \"avg\": 100}}", "middle field replaced");
  }
  {
    std::string t = "{\"add\": {\"moves\": {\"Aura Sphere\": {\"copy\": \"SWIFT\"}, \"Dark Void\": {\"copy\": \"SING\", \"bouon\": 1}, \"Pound2\": {}}}}";
    Check(PatchPackMoves(t, false) == 3, "Aura Sphere range, Dark Void range and sound");
    Check(t.find("\"rangeId\": 0") != std::string::npos && t.find("\"rangeId\": 4") != std::string::npos, "Aura Sphere 0, Dark Void 4");
    Check(PatchPackMoves(t, false) == 0, "idempotent");
    Check(PatchPackMoves(t, true) == 0, "the Gen 5-7 table does not know these");
  }
  std::printf(sFailed == 0 ? "move_targets_test: all passed\n" : "move_targets_test: %d failed\n", sFailed);
  return sFailed == 0 ? 0 : 1;
}
