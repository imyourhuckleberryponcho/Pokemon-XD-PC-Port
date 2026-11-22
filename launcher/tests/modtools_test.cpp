#include "launcher_modtools.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <thread>

namespace fs = std::filesystem;
namespace mt = launcher::modtools;
namespace em = engine::mods;

static int failures = 0;
static void Check(bool ok, const char* what) {
  std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
  if (!ok) failures++;
}

static void Put(const fs::path& file, const std::string& text) {
  fs::create_directories(file.parent_path());
  std::ofstream(file, std::ios::binary) << text;
}

static void Wait(mt::Tools& tools) {
  for (int i = 0; i < 600 && tools.Busy(); i++) std::this_thread::sleep_for(std::chrono::milliseconds(20));
  tools.Poll();
}

int main() {
  const fs::path root = fs::temp_directory_path() / "launcher_modtools_test";
  std::error_code ec;
  fs::remove_all(root, ec);
  const fs::path mods = root / "mods";
  const auto manifest = [](const char* id) {
    return std::string("{\"id\": \"") + id + "\", \"name\": \"" + id + "\", \"version\": \"1.0\", \"author\": \"t\", \"description\": \"d\", \"game\": \"GXXE01\"}";
  };
  Put(mods / "good" / "mod.json", manifest("good"));
  Put(mods / "good" / "data" / "moves.json", "{\"moves\": {\"Tackle\": {\"pp\": 30}}}");
  Put(mods / "broken" / "mod.json", manifest("broken"));
  Put(mods / "broken" / "data" / "moves.json", "{\"moves\": {\"Tackle\": {\"pp\": 30}");
  Put(mods / "gamedata" / "mod.json", manifest("gamedata"));
  Put(mods / "gamedata" / "files" / "disc.iso", std::string(4096, 'x'));

  std::vector<em::Message> messages;
  std::vector<em::ModFolder> found = em::FindMods(mods, messages);
  Check(found.size() == 3, "three made-up mods are found");

  std::vector<std::string> said, turnedOn, logged;
  fs::path chosenZip;
  mt::Host host;
  host.modsFolder = mods;
  host.found = &found;
  host.scratchFolder = root / "scratch";
  host.say = [&](const std::string& t) { said.push_back(t); };
  host.log = [&](const std::string& t) { logged.push_back(t); };
  host.turnOn = [&](const std::string& id) { turnedOn.push_back(id); };
  host.chooseZip = [&](const std::string& name, std::function<void(const std::string&)> done) {
    chosenZip = root / "out" / name;
    done(chosenZip.string());
  };
  mt::Tools tools;
  tools.Init(host);

  tools.StartCheck("good");
  Wait(tools);
  mt::Tools::Last last = tools.LastResult();
  Check(last.id == "good" && !last.problems && last.verdict.rfind("Nothing wrong found", 0) == 0, "Check this mod: a good mod gets \"Nothing wrong found\"");
  Check(tools.WaitPending(), "a finished check has a window about to open (scripts wait for that)");

  tools.StartCheck("broken");
  Wait(tools);
  last = tools.LastResult();
  Check(last.id == "broken" && last.problems && last.verdict.rfind("Problems: 1", 0) == 0, "a cut-off data file is a problem, in plain words");

  turnedOn.clear();
  said.clear();
  tools.BeforeOn("good");
  Wait(tools);
  Check(turnedOn.size() == 1 && turnedOn[0] == "good" && said.size() == 1, "before switching on: a clean mod is switched on at once, with one line");
  tools.BeforeOn("good");
  Check(turnedOn.size() == 2, "a mod checked clean in this run is not checked again");
  tools.BeforeOn("broken");
  Wait(tools);
  Check(turnedOn.size() == 2, "before switching on: a mod with a problem is NOT switched on until the player answers");
  tools.BeforeOn("not-there");
  Check(turnedOn.size() == 3 && turnedOn[2] == "not-there", "a mod that is not found just goes on (the list's own rules decide)");

  tools.StartPack("good");
  Wait(tools);
  last = tools.LastResult();
  Check(last.packed && fs::is_regular_file(last.zip) && last.zip.filename() == "good-1.0.zip", "Pack for sharing: the zip is where the player chose");
  tools.StartPack("gamedata");
  Wait(tools);
  last = tools.LastResult();
  Check(!last.packed, "Pack for sharing: game data is refused, nothing is written");
  tools.StartPack("broken");
  Wait(tools);
  Check(!tools.LastResult().packed, "Pack for sharing: a mod with a problem is not packed");

  fs::remove_all(root, ec);
  std::printf(failures == 0 ? "launcher_modtools_test: all passed\n" : "launcher_modtools_test: %d FAILED\n", failures);
  return failures == 0 ? 0 : 1;
}
