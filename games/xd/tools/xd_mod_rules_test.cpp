#include "xd_mod_rules.h"

#include "../src/data/xd_data_mods.h"

#include <modcheck.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <regex>
#include <set>
#include <string>

namespace fs = std::filesystem;
namespace mc = engine::modcheck;

namespace {
bool sOk = true;
void check(bool cond, const std::string& what) {
  std::printf("%s %s\n", cond ? "ok  " : "FAIL", what.c_str());
  sOk &= cond;
}
const fs::path kFixtures = MODCHECK_FIXTURES;
}

int main() {
  const mc::GameRules rules = xd::modrules::Rules();
  const auto run = [&](const fs::path& folder) {
    mc::Options o;
    o.folder = folder;
    o.rules = &rules;
    return mc::CheckMod(o);
  };

  {
    std::ifstream in(XD_SCRIPT_HOOKS_CPP);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::set<std::string> game;
    const std::regex re(R"re(constexpr const char\* kHook\w+ = "([a-z_.]+)";)re");
    for (std::sregex_iterator it(text.begin(), text.end(), re), end; it != end; ++it) game.insert((*it)[1].str());
    const std::set<std::string> mine(rules.scriptHooks.begin(), rules.scriptHooks.end());
    check(!game.empty() && game == mine, "the checker's script hooks are the ones games/xd/src/script_hooks.cpp offers (" + std::to_string(game.size()) + ")");
  }

  const mc::Report data = run(kFixtures / "good-data");
  check(data.Count(mc::Severity::Problem) == 0 && data.Count(mc::Severity::Warning) == 0, "good-data is clean under XD's rules");
  const mc::Report scripts = run(kFixtures / "good-scripts");
  check(scripts.Count(mc::Severity::Problem) == 0 && scripts.Count(mc::Severity::Warning) == 0, "good-scripts is clean (battle.damage is a real hook)");
  const mc::Report bad = run(kFixtures / "bad-data");
  bool textLine = false;
  for (const mc::Finding& f : bad.findings) textLine |= f.code == "data-text" && f.line == 10 && f.text.find("closing }") != std::string::npos;
  check(textLine, "the game's message rule: \"{00 code\" is refused with its reason, at its line");
  check(run(kFixtures / "bad-lua").Has("lua-unknown-hook"), "bad-lua: battle.dmage is not an XD hook");

  std::string why;
  check(xd::data::CheckMessageText("Sharply raises{00}the ATTACK stat.", why) && xd::data::CheckMessageText("a {{ brace {07}", why) == false, "message text: {00} fine, {07} needs its argument byte");
  check(!xd::data::CheckMessageText("oops {zz}", why) && why.find("hex bytes") != std::string::npos, "message text: not hex says so");
  check(!xd::data::CheckMessageText("open {00", why) && why.find("closing }") != std::string::npos, "message text: unclosed {");
  check(xd::data::CheckMessageText("caf\xC3\xA9 ok", why) && !xd::data::CheckMessageText("emoji \xF0\x9F\x98\x80", why), "message text: 16-bit characters fine, others refused");

  const fs::path d = fs::temp_directory_path() / "xd_mod_rules_test";
  fs::remove_all(d);
  fs::create_directories(d / "rooms/hall");
  {
    std::ofstream(d / "mod.json") << "{ \"id\": \"t\", \"name\": \"T\", \"version\": \"1.0\", \"author\": \"x\", \"description\": \"x\", \"game\": \"GXXE01\" }";
    std::ofstream(d / "rooms/hall/script", std::ios::binary) << "TCOD" << std::string(40, '\0');
    std::ofstream(d / "objects.txt") << "x";
  }
  const mc::Report story = run(d);
  check(story.Has("script-invalid") && story.HasPermission("adds-story"), "a story script that is not laid out as the game reads it is a problem; rooms/ counts as story");
  fs::remove_all(d);

  {
    fs::create_directories(d / "objects");
    std::ofstream(d / "mod.json") << "{ \"id\": \"t\", \"name\": \"T\", \"version\": \"1.0\", \"author\": \"x\", \"description\": \"x\", \"game\": \"GXXE01\" }";
    std::ofstream(d / "objects/D1_labo_1F.json") << "{ \"people\": [ { \"name\": \"guide\", \"model\": 2, \"x\": 1, \"y\": 0, \"z\": 2, \"talk\": \"guide_talk\" } ] }";
    check(run(d).Count(mc::Severity::Problem) == 0 && run(d).HasPermission("adds-story"), "a good objects file is clean and counts as adds-story");
    std::ofstream(d / "objects/D1_labo_2F.json") << "{ \"people\": [ { \"model\": 2, \"bogus_field\": 1 } ] }";
    const mc::Report objs = run(d);
    bool named = false;
    for (const mc::Finding& f : objs.findings) named |= f.code == "objects-invalid" && f.file == "objects/D1_labo_2F.json" && f.line > 0;
    check(named, "a person without a name is reported as objects-invalid with the file and its line");
    std::ofstream(d / "objects/broken.json") << "{ \"people\": [ ";
    check(run(d).Has("json-invalid") || run(d).Has("objects-invalid"), "an objects file that is not JSON is a problem");
    fs::remove_all(d);
  }

  {
    fs::create_directories(d / "games" / "GXX" / "data");
    fs::create_directories(d / "textures" / "faces");
    std::ofstream(d / "mod.json") << "{ \"id\": \"t\", \"name\": \"T\", \"version\": \"1.0\", \"author\": \"x\", \"description\": \"x\", \"game\": [\"GXX\", \"GC6\"], \"scripts\": true }";
    std::ofstream(d / "games" / "GXX" / "data" / "add.json") << "{ \"add\": { \"pokemon\": { \"Zorblat\": { \"copy\": \"Eevee\", \"face\": \"textures/faces/z.png\" }, \"Yak\": { \"copy\": \"Eevee\", \"face\": \"Umbreon\" } } } }";
    const mc::Report missing = run(d);
    bool named = false;
    for (const mc::Finding& f : missing.findings) named |= f.code == "face-missing" && f.file == "games/GXX/data/add.json" && f.line > 0 && f.text.find("Zorblat") != std::string::npos;
    check(named && !missing.Has("manifest-game"), "a face that names a missing picture is a problem (a species name is not looked up here); a mod for XD and Colosseum is fine");
    std::ofstream(d / "textures" / "faces" / "z.png") << "x";
    check(!run(d).Has("face-missing"), "...and clean once the picture is in the mod (the shared textures folder counts)");
    fs::create_directories(d / "scripts");
    std::ofstream(d / "scripts" / "a.lua") << "local p = position()" << char(10);
    check(run(d).Has("lua-game-only"), "XD-only Lua in the shared scripts of a mod that also names Colosseum is a warning");
    fs::remove_all(d);
  }

  std::printf(sOk ? "xd_mod_rules_test: all passed\n" : "xd_mod_rules_test: FAILED\n");
  return sOk ? 0 : 1;
}
