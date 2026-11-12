#include "xd_settings.h"

#include <controls.h>

#include <cstdlib>
#include <string_view>

namespace xd_settings {

namespace es = engine::settings;

void Register(es::Registry& registry) {
  es::RegisterStandardSettings(registry);
  engine::controls::RegisterSettings(registry);

  using D = es::Definition;
  using C = es::Category;
  registry.Add(D::Int("gameplay.purification_speed", C::Gameplay, "Purification speed",
                      "How many times faster a Shadow Pokemon's heart opens (walking, battling, the Purify "
                      "Chamber). 1 is the original.",
                      1, 10, 1, 1)
                   .ForGames({kGameId}));
  registry.Add(D::Bool("system.dev_menu", C::System, "Developer menu (cheats)",
                       "F9 opens a menu of the developers' own debug tools left in the game: go to any place, any "
                       "point of the story, set story flags, start battles, get items and Pokemon. For testing; "
                       "runs that used it are marked in their recording and bug reports.",
                       false, false)
                   .ForGames({kGameId})
                   .Implemented());
}

std::vector<std::string> ApplyEnvironment(es::Registry& registry,
                                          const std::function<const char*(const char*)>& getenv) {
  std::vector<std::string> used;
  const auto get = [&](const char* name) -> std::string_view {
    const char* v = getenv(name);
    return v != nullptr ? std::string_view(v) : std::string_view();
  };

  if (const std::string_view preset = get("XD_PRESET"); !preset.empty()) {
    if (registry.ApplyOverrides("preset=" + std::string(preset)) == 0) {
      used.push_back("XD_PRESET=" + std::string(preset) + " -> the " + std::string(preset) + " preset for this run");
    }
  }
  if (const std::string_view disc = get("XD_DISC_TIMING"); !disc.empty()) {
    if (registry.Override("system.disc_timing", disc)) {
      used.push_back("XD_DISC_TIMING=" + std::string(disc) + " -> system.disc_timing=" + std::string(disc));
    }
  }
  if (const std::string_view card = get("XD_CARD_TIMING"); !card.empty()) {
    if (registry.Override("system.card_timing", card)) {
      used.push_back("XD_CARD_TIMING=" + std::string(card) + " -> system.card_timing=" + std::string(card));
    }
  }
  if (const std::string_view clock = get("XD_GAME_CLOCK"); !clock.empty()) {
    if (registry.Override("system.game_clock", clock)) {
      used.push_back("XD_GAME_CLOCK=" + std::string(clock) + " -> system.game_clock=" + std::string(clock));
    }
  }
  if (const std::string_view cpu = get("XD_CPU_TIMING"); !cpu.empty()) {
    if (registry.Override("system.cpu_timing", cpu)) {
      used.push_back("XD_CPU_TIMING=" + std::string(cpu) + " -> system.cpu_timing=" + std::string(cpu));
    }
  }
  if (const std::string_view turbo = get("XD_TURBO"); !turbo.empty()) {
    float speed = static_cast<float>(std::atof(std::string(turbo).c_str()));
    speed = speed < 1.0f ? 1.0f : speed > 16.0f ? 16.0f : speed;
    const size_t index = registry.Find("system.game_speed");
    es::Value v;
    v.f = speed;
    if (index != es::kNoSetting && registry.Override("system.game_speed", registry.TextOf(index, v))) {
      used.push_back("XD_TURBO=" + std::string(turbo) + " -> system.game_speed=" + registry.ValueText(index));
    }
  }
  if (const std::string_view dev = get("XD_DEV_MENU"); !dev.empty() && dev != "0") {
    if (get("XD_PRESET") == "original") {
      used.push_back("XD_DEV_MENU=" + std::string(dev) + " ignored: XD_PRESET=original runs the pure game");
    } else if (registry.Override("system.dev_menu", "on")) {
      used.push_back("XD_DEV_MENU=" + std::string(dev) + " -> system.dev_menu=on");
    }
  }
  if (const std::string_view photo = get("XD_PHOTO_SCRIPT"); !photo.empty()) {
    if (get("XD_PRESET") == "original") {
      used.push_back("XD_PHOTO_SCRIPT ignored: XD_PRESET=original runs the pure game");
    } else if (registry.Override("video.photo_mode", "on")) {
      used.push_back("XD_PHOTO_SCRIPT -> video.photo_mode=on");
    }
  }
  if (const std::string_view list = get("XD_SETTINGS"); !list.empty()) {
    const int refused = registry.ApplyOverrides(list);
    used.push_back("XD_SETTINGS=" + std::string(list) +
                   (refused ? " (" + std::to_string(refused) + " refused, see above)" : std::string()));
  }
  return used;
}

std::string DocsIntro() {
  return "Every setting the port has, generated from the list in the code (engine/settings/standard_settings.cpp,\n"
         "engine/controls, games/xd/src/xd_settings.cpp) by `build\\release\\bin\\xd_settings.exe docs docs/SETTINGS.md`.\n"
         "Don't edit this file by hand: change the code and run that again.\n"
         "\n"
         "- **Original** is exactly the GameCube game. Every improvement is off there.\n"
         "- **Enhanced** is the recommended set: better pictures and less waiting, but the game's own rules\n"
         "  (bug fixes, battle speed, purification) stay as they were.\n"
         "- **Custom** is whatever the player changed, starting from one of the two.\n"
         "- **Preferences** (\"same\" in the Enhanced column: volumes, window, controls) don't change the game, so\n"
         "  they are the player's in every preset and never make a run \"not Original\".\n"
         "- **Works yet**: \"not yet\" means the setting is planned and listed, but changing it does nothing so far.\n"
         "\n"
         "The settings live in `%APPDATA%\\Pokemon XD\\settings.ini` (Notepad works). `xd_settings show` prints what a\n"
         "settings.ini means, `xd_settings check` checks one (F10 bug reports include the player's file).\n"
         "\n"
         "For one run only (testing; never saved): `XD_PRESET=original` (pure GameCube whatever settings.ini says),\n"
         "`XD_DISC_TIMING=fast`, `XD_TURBO=<speed>`, `XD_SETTINGS=id=value;id=value`. `XD_REQUIRE_ORIGINAL=1` makes\n"
         "xd.exe refuse to start unless the run is pure Original (for check runs against Dolphin).\n";
}

}
