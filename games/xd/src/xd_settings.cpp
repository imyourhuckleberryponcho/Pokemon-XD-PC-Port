#include "xd_settings.h"

#include <controls.h>

#include <algorithm>
#include <cstdlib>
#include <string>
#include <string_view>

namespace xd_settings {

namespace es = engine::settings;

static void RegisterCheats(es::Registry& registry);

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
  registry.Add(D::Enum("system.language", C::System, "Language",
                       "The language of the game's texts, menus and pictures. English (US) is the USA game as it is. The "
                       "others come from the European (PAL) copy of the game, added on the launcher's Extra assets page "
                       "(\"Languages (from the European disc)\"), and Japanese from the Japanese copy (\"Japanese (from the Japanese disc)\"); a language the pack doesn't have falls back to English. "
                       "Applies the next time the game starts. It changes only text and pictures, not how the game plays.",
                       {{"us", "English (US)"}, {"ge", "German"}, {"fr", "French"}, {"it", "Italian"}, {"sp", "Spanish"}, {"jp", "Japanese"}}, "us", "us")
                   .Preference()
                   .ForGames({kGameId})
                   .Implemented());
  registry.Add(D::Enum("playthrough.new_rule_set", C::System, "New playthroughs: rule set",
                       "The rule set the New Game screen starts with: XD original (the GameCube game's battle rules) or Gen 4 (needs the Gen 4 pack). Each playthrough keeps its own.",
                       {{"xd", "XD original (Gen 3)"}, {"gen4", "Gen 4"}}, "xd", "xd")
                   .Preference().ForGames({kGameId}).Implemented());
  registry.Add(D::Enum("playthrough.new_pokemon", C::System, "New playthroughs: Pokemon",
                       "Which Pokemon the New Game screen starts with: Gen 1-3 (XD's own) or Gen 1-4 (needs the Gen 4 pack). Each playthrough keeps its own.",
                       {{"3", "Gen 1-3"}, {"4", "Gen 1-4"}}, "3", "3")
                   .Preference().ForGames({kGameId}).Implemented());
  registry.Add(D::Enum("playthrough.new_looks", C::System, "New playthroughs: looks",
                       "Whose models the New Game screen starts with: the original XD look, the most modern models you have, or Battle Revolution first. Only pictures change. Each playthrough keeps its own.",
                       {{"original_xd", "Original XD look"}, {"most_modern", "Most modern"}, {"br_first", "Battle Revolution first"}}, "original_xd", "original_xd")
                   .Preference().ForGames({kGameId}).Implemented());
  registry.Add(D::Text("system.extra_disc.languages", C::System, "European disc image (for languages)",
                       "The European (PAL) Pokemon XD disc image the launcher's Extra assets page makes the language "
                       "pack from (empty: none chosen yet).",
                       "", "")
                   .Preference()
                   .ForGames({kGameId})
                   .Implemented());
  registry.Add(D::Text("system.extra_disc.languages_jp", C::System, "Japanese disc image (for Japanese)",
                       "The Japanese Pokemon XD disc image the launcher's Extra assets page makes the Japanese "
                       "pack from (empty: none chosen yet).",
                       "", "")
                   .Preference()
                   .ForGames({kGameId})
                   .Implemented());
  registry.Add(D::Text("system.extra_disc.usum", C::System, "Ultra Sun / Ultra Moon dump file (for models)",
                       "The decrypted Pokemon Ultra Sun or Ultra Moon dump (.3ds / .cci / .cia) the launcher's Extra assets page makes the "
                       "Ultra Sun / Ultra Moon models from (empty: none chosen yet).",
                       "", "")
                   .Preference()
                   .ForGames({kGameId})
                   .Implemented());
  registry.Add(D::Bool("system.mods_hot_reload", C::System, "Reload mods while playing (developers)",
                       "For people who make mods. A changed script mod or texture pack is picked up while the game runs: F6, "
                       "\"Reload mods\" on the in-game MODS page, or by itself a moment after a mod file changed. Data values, "
                       "text and replaced files still need a restart (the game says so). Only at a quiet moment (not in a "
                       "battle or while a map loads); never in Original, in replays or in test runs. Runs that reloaded are "
                       "marked in their recording.",
                       false, false)
                   .ForGames({kGameId})
                   .Implemented());
  registry.Add(D::Bool("gameplay.replay_bar", C::Gameplay, "Replay bar",
                       "While you watch a battle replay: the bar with the turn slider, the speed and the pause state, at the battle menu's place. "
                       "On: shown (the Replay bar button, H or Y by default, hides it and shows it again). Off: only a small tab with the turn "
                       "number at the top. It only exists while a replay is watched.",
                       true, true)
                   .Preference()
                   .ForGames({kGameId})
                   .Implemented());
  registry.Add(D::Bool("system.dev_menu", C::System, "Developer menu (cheats)",
                       "F9 opens a menu of the developers' own debug tools left in the game: go to any place, any "
                       "point of the story, set story flags, start battles, get items and Pokemon. For testing; "
                       "runs that used it are marked in their recording and bug reports.",
                       false, false)
                   .ForGames({kGameId})
                   .Implemented());
  RegisterCheats(registry);
}

static void RegisterCheats(es::Registry& registry) {
  using D = es::Definition;
  using C = es::Category;
  registry.Add(D::Bool("cheats.enabled", C::System, "Enable cheats",
                       "Turns the cheat menu on: a hot key (F5, or hold L + R and press Start on the controller; both can be changed in the controls) opens "
                       "a menu of cheats over the game. Off: no cheat does anything. A playthrough that used a cheat is marked as such "
                       "for good (it can't join competitive online rooms, and a speedrun with it is invalid). Never in Original.",
                       false, false)
                   .ForGames({kGameId})
                   .Implemented());
  registry.Add(D::Bool("cheats.catch_always", C::System, "Cheat: every Poke Ball catches",
                       "A ball thrown at a wild Pokemon always catches it (Shadow Pokemon still need the Snag Ball).",
                       false, false)
                   .ForGames({kGameId})
                   .Implemented());
  registry.Add(D::Bool("cheats.catch_trainers", C::System, "Cheat: catch other trainers' Pokemon",
                       "Poke Balls can be thrown at another trainer's Pokemon (not Shadow ones: those are always snagged). It is caught when the ball works, "
                       "and that Pokemon is out of the battle, as a snagged Shadow Pokemon is.",
                       false, false)
                   .ForGames({kGameId})
                   .Implemented());
  registry.Add(D::Bool("cheats.balls_infinite", C::System, "Cheat: Poke Balls are never used up",
                       "A ball thrown in battle stays in the bag.",
                       false, false)
                   .ForGames({kGameId})
                   .Implemented());
  registry.Add(D::Enum("cheats.exp", C::System, "Cheat: experience multiplier",
                       "Experience Pokemon earn in battle is multiplied (x2, x5 or x10).",
                       {{"off", "Off"}, {"x2", "x2"}, {"x5", "x5"}, {"x10", "x10"}}, "off", "off")
                   .ForGames({kGameId})
                   .Implemented());
  registry.Add(D::Bool("cheats.level_cap_off", C::System, "Cheat: ignore the level cap",
                       "The level-cap challenge rule does not hold Pokemon back (they level up to 100 as in the plain game).",
                       false, false)
                   .ForGames({kGameId})
                   .Implemented());
  registry.Add(D::Bool("cheats.money_infinite", C::System, "Cheat: infinite money",
                       "Money is topped up and buying or paying never takes any.",
                       false, false)
                   .ForGames({kGameId})
                   .Implemented());
  registry.Add(D::Bool("cheats.coupons_infinite", C::System, "Cheat: infinite Poke Coupons",
                       "Poke Coupons are topped up and spending them never takes any.",
                       false, false)
                   .ForGames({kGameId})
                   .Implemented());
  registry.Add(D::Enum("cheats.shiny", C::System, "Cheat: shiny odds",
                       "How often a Pokemon you receive from now on is shiny (caught, gifts, Shadow Pokemon, Poke Spot Pokemon).",
                       {{"off", "Off"}, {"1_in_512", "1 in 512"}, {"1_in_64", "1 in 64"}, {"1_in_8", "1 in 8"}, {"always", "Always"}}, "off", "off")
                   .ForGames({kGameId})
                   .Implemented());
  registry.Add(D::Bool("cheats.perfect_ivs", C::System, "Cheat: perfect IVs",
                       "Pokemon you receive from now on have all IVs at 31.",
                       false, false)
                   .ForGames({kGameId})
                   .Implemented());
  registry.Add(D::Bool("cheats.max_evs", C::System, "Cheat: maxed EVs",
                       "Pokemon you receive from now on have every effort value at the game's maximum (100).",
                       false, false)
                   .ForGames({kGameId})
                   .Implemented());
  registry.Add(D::Bool("cheats.no_damage", C::System, "Cheat: no damage taken",
                       "Your Pokemon take no damage in battle.",
                       false, false)
                   .ForGames({kGameId})
                   .Implemented());
  registry.Add(D::Bool("cheats.win_battles", C::System, "Cheat: win every battle",
                       "The first hit on a foe's Pokemon makes it faint (a Shadow Pokemon is left with 1 HP so it can still be snagged). "
                       "Nothing is forfeited: the battle goes on as a won one.",
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
  if (const std::string_view cheats = get("XD_CHEATS"); !cheats.empty() && cheats != "0") {
    if (get("XD_PRESET") == "original" || get("XD_REQUIRE_ORIGINAL") == "1") {
      used.push_back("XD_CHEATS=" + std::string(cheats) + " ignored: Original runs the pure game");
    } else if (registry.Override("cheats.enabled", "on")) {
      std::string note = "XD_CHEATS=" + std::string(cheats) + " -> cheats.enabled=on";
      for (size_t at = 0; at <= cheats.size();) {
        const size_t end = std::min(cheats.find(';', at), cheats.size());
        std::string part(cheats.substr(at, end - at));
        at = end + 1;
        if (part.empty() || part == "1" || part == "all" || part == "on") continue;
        const size_t eq = part.find('=');
        const std::string key = "cheats." + part.substr(0, eq);
        const std::string value = eq == std::string::npos ? "on" : part.substr(eq + 1);
        if (registry.Override(key, value)) note += ", " + key + "=" + value;
      }
      used.push_back(note);
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
         "  they are the player's in every preset and never make a run \"not Original\". Discord presence is one: on by\n"
         "  default for everyone (Original too), a preference and not part of the presets; it only shows a line in\n"
         "  the Discord app on the same PC and never touches the game (docs/PRESENCE.md).\n"
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
