#include "../src/xd_settings.h"
#include "../src/xd_test_run.h"

#include <settings.h>

#include <cstdio>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

namespace es = engine::settings;

namespace {

bool sOk = true;
std::vector<std::string> sWarnings;

void check(bool cond, const char* what) {
  std::printf("%s %s\n", cond ? "ok  " : "FAIL", what);
  sOk &= cond;
}

void setup(es::Registry& r) {
  r.SetWarningSink([](const std::string& m) { sWarnings.push_back(m); });
  xd_settings::Register(r);
}

}

int main(int argc, char** argv) {
  es::Registry r;
  setup(r);
  check(sWarnings.empty(), "XD's settings register without warnings");
  check(r.Count() > 100, "the generic list, the controls and XD's own registered");

  const std::vector<std::string> broken = es::CheckRules(r);
  for (const std::string& b : broken) {
    std::printf("     broken: %s\n", b.c_str());
  }
  check(broken.empty(), "every setting keeps the rules (every enhancement off in Original)");
  check(r.IsOriginal() && r.CurrentPreset() == es::Preset::Original, "starts as pure Original");
  r.ApplyPreset(es::Preset::Enhanced);
  r.ApplyPreset(es::Preset::Original);
  check(r.IsOriginal(), "Enhanced and back: Original");

  const size_t purify = r.Find("gameplay.purification_speed");
  check(purify != es::kNoSetting && r.Get(purify).AppliesTo("xd") && !r.Get(purify).AppliesTo("colo"),
        "purification speed is XD's");
  const size_t devMenu = r.Find("system.dev_menu");
  check(devMenu != es::kNoSetting && r.Get(devMenu).AppliesTo("xd") && !r.Get(devMenu).AppliesTo("colo") &&
            r.Get(devMenu).original.i == 0 && r.Get(devMenu).enhanced.i == 0 && !r.Get(devMenu).preference,
        "the developer menu is XD's, off in every preset, and makes a run not Original");
  bool sharedForAll = true, controlsWork = true;
  for (size_t i = 0; i < r.Count(); i++) {
    const es::Definition& d = r.Get(i);
    if (i != purify && i != devMenu) {
      sharedForAll &= d.games.empty();
    }
    if (d.category == es::Category::Controls && d.id.rfind("controls.", 0) != 0) {
      controlsWork &= d.implemented;
    }
  }
  check(sharedForAll, "the shared settings apply to every game");
  check(controlsWork, "the controls are marked as working");

  {
    es::Registry e;
    setup(e);
    e.ApplyPreset(es::Preset::Enhanced);
    std::map<std::string, std::string> env{{"XD_PRESET", "original"},
                                           {"XD_DISC_TIMING", "fast"},
                                           {"XD_TURBO", "40"},
                                           {"XD_SETTINGS", "video.widescreen=on; gameplay.purification_speed=3"}};
    const auto lines = xd_settings::ApplyEnvironment(e, [&](const char* n) -> const char* {
      auto it = env.find(n);
      return it == env.end() ? nullptr : it->second.c_str();
    });
    for (const std::string& l : lines) {
      std::printf("     %s\n", l.c_str());
    }
    check(lines.size() == 4, "four switches reported");
    check(e.Enum("system.disc_timing").Get() == e.ChoiceIndex("system.disc_timing", "fast"), "XD_DISC_TIMING=fast");
    check(e.Float("system.game_speed").Get() == 16.0f, "XD_TURBO=40 read as 16, like boot.c");
    check(e.Bool("video.widescreen").Get() && e.Int("gameplay.purification_speed").Get() == 3, "XD_SETTINGS applied");
    check(!e.Bool("video.photo_mode").Get(), "XD_PRESET=original under the rest");
    check(!e.IsOriginal() && e.CurrentPreset() == es::Preset::Enhanced, "not Original; the player's preset kept");

    es::Registry o;
    setup(o);
    o.ApplyPreset(es::Preset::Enhanced);
    std::map<std::string, std::string> env2{{"XD_PRESET", "original"}, {"XD_TURBO", "4"}};
    xd_settings::ApplyEnvironment(o, [&](const char* n) -> const char* {
      auto it = env2.find(n);
      return it == env2.end() ? nullptr : it->second.c_str();
    });
    check(o.IsOriginal(), "XD_PRESET=original + XD_TURBO: pure Original (check runs)");
    check(o.Summary().find("system.game_speed=4") != std::string::npos, "... with the game speed in the summary");

    es::Registry none;
    setup(none);
    check(xd_settings::ApplyEnvironment(none, [](const char*) -> const char* { return nullptr; }).empty() &&
              none.Summary() == "original (preset original)",
          "no switches: nothing changes");

    for (const bool original : {false, true}) {
      es::Registry dev;
      setup(dev);
      std::map<std::string, std::string> env3{{"XD_DEV_MENU", "1"}};
      if (original) env3["XD_PRESET"] = "original";
      xd_settings::ApplyEnvironment(dev, [&](const char* n) -> const char* {
        auto it = env3.find(n);
        return it == env3.end() ? nullptr : it->second.c_str();
      });
      if (original) {
        check(dev.IsOriginal() && !dev.Bool("system.dev_menu").Get(), "XD_DEV_MENU=1 + XD_PRESET=original: no menu");
      } else {
        check(!dev.IsOriginal() && dev.Bool("system.dev_menu").Get(), "XD_DEV_MENU=1: the menu on, not Original");
      }
    }
  }

  {
    using Env = std::map<std::string, std::string>;
    const auto readOnly = [](const Env& env) {
      return xd::PlayerFilesReadOnly([&](const char* n) -> const char* {
        auto it = env.find(n);
        return it == env.end() ? nullptr : it->second.c_str();
      });
    };
    const auto testRun = [](const Env& env) {
      return xd::TestRunSwitchSet([&](const char* n) -> const char* {
        auto it = env.find(n);
        return it == env.end() ? nullptr : it->second.c_str();
      }) != nullptr;
    };
    check(!testRun({}) && !readOnly({}), "a player's run (no switches): not a test run, the player's files are written");
    check(!readOnly({{"XD_DEV_MENU", "1"}, {"XD_TURBO", "4"}, {"XD_PRESET", "enhanced"}, {"XD_SETTINGS", "video.widescreen=on"},
                     {"XD_CARD_DIR", "x"}, {"XD_NEWGAME_SETUP", "f.json"}, {"XD_MODS", "0"}}),
          "a player's own switches (dev menu, turbo, preset, card folder, New Game restart) don't make a test run");
    bool each = true;
    for (const xd::TestRunSwitch& s : xd::kTestRunSwitches) {
      const bool on = readOnly({{s.name, "1"}});
      const bool withFolder = readOnly({{s.name, "1"}, {"XD_SETTINGS_DIR", "F:/somewhere"}});
      const bool empty = readOnly({{s.name, ""}});
      if (!on || withFolder || empty) {
        std::printf("     %s: alone %d (want 1), with XD_SETTINGS_DIR %d (want 0), empty %d (want 0)\n", s.name, on, withFolder, empty);
        each = false;
      }
    }
    check(each, "every test switch alone: the player's files are read only; with XD_SETTINGS_DIR: written; empty: not set");
    check(readOnly({{"XD_INPUT_SCRIPT", "600:start"}}) && readOnly({{"XD_REPLAY", "r.txt"}}) &&
              readOnly({{"XD_MENU_SCRIPT", "0:press a"}}) && readOnly({{"XD_LAUNCHER_SCRIPT", "page:settings|quit"}}) &&
              readOnly({{"XD_WARP", "0x64"}}) && readOnly({{"XD_RENDER_ICON", "all"}}),
          "scripted input, replay, menu script, launcher script, warp, icon render: read only");
    check(!readOnly({{"XD_NO_MESSAGE_BOXES", "0"}}) && !readOnly({{"XD_WINDOW_OFFSCREEN", "0"}}) &&
              !readOnly({{"XD_REQUIRE_ORIGINAL", "0"}}) && !readOnly({{"XD_TEST_RUN", "0"}}),
          "the on/off switches set to 0 don't count");
    check(readOnly({{"XD_INPUT_SCRIPT", "600:start"}, {"XD_SETTINGS_DIR", ""}}), "an empty XD_SETTINGS_DIR is no folder");
    check(testRun({{"XD_INPUT_SCRIPT", "600:start"}, {"XD_SETTINGS_DIR", "F:/t"}}),
          "a test run with its own folder is still a test run (it only may write there)");
  }

  if (argc > 1) {
    std::ifstream in(argv[1], std::ios::binary);
    const std::string have((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    check(have == es::SettingsMarkdown(r, "Settings", xd_settings::DocsIntro()),
          "docs/SETTINGS.md is up to date (else: xd_settings docs docs/SETTINGS.md)");
  }

  std::printf("%s\n", sOk ? "all passed" : "FAILED");
  return sOk ? 0 : 1;
}
