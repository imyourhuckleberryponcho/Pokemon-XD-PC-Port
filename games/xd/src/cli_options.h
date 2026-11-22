#ifndef XD_CLI_OPTIONS_H
#define XD_CLI_OPTIONS_H

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace xd::cli {

struct Options {
  std::string disc;
  std::string dataFolder;
  std::string preset;
  std::string save;
  std::string replay;
  std::string replayTurn;
  int fullscreen = -1;
  bool noLauncher = false;
  bool help = false;
  bool version = false;
  std::string error;
  std::vector<std::string> notes;

  bool ChangesRun() const {
    return !dataFolder.empty() || !preset.empty() || !save.empty() || fullscreen >= 0;
  }
};

inline bool Equals(const char* a, const char* b) { return std::strcmp(a, b) == 0; }

inline Options Parse(int argc, const char* const* argv) {
  Options o;
  const auto fail = [&](const std::string& text) {
    if (o.error.empty()) o.error = text;
  };
  bool plainOnly = false;
  for (int i = 1; i < argc; i++) {
    const char* arg = argv[i] != nullptr ? argv[i] : "";
    if (!plainOnly && Equals(arg, "/?")) {
      o.help = true;
      continue;
    }
    if (plainOnly || arg[0] != '-' || arg[1] == '\0') {
      if (arg[0] == '\0') continue;
      if (o.disc.empty()) {
        o.disc = arg;
      } else {
        o.notes.push_back(std::string("extra argument left out: ") + arg);
      }
      continue;
    }
    if (Equals(arg, "--")) {
      plainOnly = true;
      continue;
    }
    if (std::strncmp(arg, "-psn_", 5) == 0) continue;
    std::string name = arg;
    std::string value;
    bool hasValue = false;
    if (name.rfind("--", 0) == 0) {
      const size_t eq = name.find('=');
      if (eq != std::string::npos) {
        value = name.substr(eq + 1);
        name.resize(eq);
        hasValue = true;
      }
    }
    const auto take = [&](std::string& into) {
      if (!hasValue) {
        if (i + 1 >= argc || argv[i + 1] == nullptr) {
          fail("The option " + name + " needs a value after it.");
          return;
        }
        value = argv[++i];
      }
      if (value.empty()) {
        fail("The option " + name + " needs a value after it.");
        return;
      }
      into = value;
    };
    const auto flag = [&](bool& into) {
      if (hasValue) {
        fail("The option " + name + " takes no value.");
        return;
      }
      into = true;
    };
    if (name == "--disc") {
      take(o.disc);
    } else if (name == "--data-folder") {
      take(o.dataFolder);
    } else if (name == "--save") {
      take(o.save);
    } else if (name == "--preset") {
      std::string text;
      take(text);
      if (!text.empty()) {
        for (char& c : text) c = static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
        if (text != "original" && text != "enhanced") {
          fail("--preset must be \"original\" or \"enhanced\" (not \"" + value + "\").");
        } else {
          o.preset = text;
        }
      }
    } else if (name == "--replay") {
      take(o.replay);
    } else if (name == "--turn") {
      take(o.replayTurn);
      if (!o.replayTurn.empty() && o.replayTurn.find_first_not_of("0123456789") != std::string::npos) {
        fail("--turn needs a turn number (not \"" + value + "\").");
      }
    } else if (name == "--fullscreen" || name == "--windowed") {
      bool on = false;
      flag(on);
      if (on) {
        const int want = name == "--fullscreen" ? 1 : 0;
        if (o.fullscreen >= 0 && o.fullscreen != want) fail("--fullscreen and --windowed can't be used together.");
        o.fullscreen = want;
      }
    } else if (name == "--no-launcher") {
      flag(o.noLauncher);
    } else if (name == "--help" || name == "-h" || name == "-?") {
      o.help = true;
    } else if (name == "--version" || name == "-v") {
      o.version = true;
    } else {
      fail("Unknown option " + std::string(arg) + " (--help lists the options).");
    }
  }
  return o;
}

inline std::vector<std::pair<std::string, std::string>> EnvFor(const Options& o, const std::string& currentSettings) {
  std::vector<std::pair<std::string, std::string>> out;
  if (!o.dataFolder.empty()) out.emplace_back("XD_DATA_FOLDER", o.dataFolder);
  if (!o.preset.empty()) out.emplace_back("XD_PRESET", o.preset);
  if (!o.save.empty()) out.emplace_back("XD_SAVE_NAME", o.save);
  if (!o.replay.empty()) out.emplace_back("XD_BATTLE_REPLAY", o.replay);
  if (!o.replayTurn.empty()) out.emplace_back("XD_BATTLE_REPLAY_TURN", o.replayTurn);
  if (o.fullscreen >= 0) {
    const std::string entry = o.fullscreen == 1 ? "video.fullscreen=on" : "video.fullscreen=off";
    std::string list = currentSettings;
    const bool already = list == entry || (list.size() > entry.size() && list.compare(list.size() - entry.size(), entry.size(), entry) == 0 &&
                                           list[list.size() - entry.size() - 1] == ';');
    if (!already) {
      if (!list.empty() && list.back() != ';') list += ';';
      list += entry;
    }
    out.emplace_back("XD_SETTINGS", list);
  }
  return out;
}

inline std::string Summary(const Options& o) {
  std::string s;
  const auto add = [&](const std::string& text) { s += (s.empty() ? "" : " ") + text; };
  if (!o.disc.empty()) add("--disc " + o.disc);
  if (o.fullscreen == 1) add("--fullscreen");
  if (o.fullscreen == 0) add("--windowed");
  if (!o.dataFolder.empty()) add("--data-folder " + o.dataFolder);
  if (!o.preset.empty()) add("--preset " + o.preset);
  if (!o.save.empty()) add("--save " + o.save);
  if (!o.replay.empty()) add("--replay " + o.replay);
  if (!o.replayTurn.empty()) add("--turn " + o.replayTurn);
  if (o.noLauncher) add("--no-launcher");
  return s;
}

inline void SetEnvUtf8(const std::string& name, const std::string& value) {
#ifdef _WIN32
  const auto widen = [](const std::string& text) {
    std::wstring out;
    for (size_t i = 0; i < text.size();) {
      const unsigned char c = static_cast<unsigned char>(text[i]);
      uint32_t cp = c;
      size_t n = 1;
      if (c >= 0xF0 && i + 3 < text.size()) { cp = c & 7; n = 4; }
      else if (c >= 0xE0 && i + 2 < text.size()) { cp = c & 15; n = 3; }
      else if (c >= 0xC0 && i + 1 < text.size()) { cp = c & 31; n = 2; }
      for (size_t k = 1; k < n; k++) cp = (cp << 6) | (static_cast<unsigned char>(text[i + k]) & 63);
      i += n;
      if (cp >= 0x10000) {
        cp -= 0x10000;
        out += static_cast<wchar_t>(0xD800 + (cp >> 10));
        out += static_cast<wchar_t>(0xDC00 + (cp & 0x3FF));
      } else {
        out += static_cast<wchar_t>(cp);
      }
    }
    return out;
  };
  _wputenv_s(widen(name).c_str(), widen(value).c_str());
#else
  setenv(name.c_str(), value.c_str(), 1);
#endif
}

inline std::string Apply(Options& o) {
  if (!o.dataFolder.empty()) {
    std::error_code ec;
    std::filesystem::path folder(std::u8string(o.dataFolder.begin(), o.dataFolder.end()));
    folder = std::filesystem::absolute(folder, ec).lexically_normal();
    if (ec) return "The data folder " + o.dataFolder + " can't be used: " + ec.message();
    std::filesystem::create_directories(folder, ec);
    if (!std::filesystem::is_directory(folder, ec)) {
      return "The data folder " + o.dataFolder + " can't be made or isn't a folder.";
    }
    const std::u8string text = folder.u8string();
    o.dataFolder.assign(text.begin(), text.end());
  }
  const char* settings = std::getenv("XD_SETTINGS");
  for (const auto& [name, value] : EnvFor(o, settings != nullptr ? settings : "")) SetEnvUtf8(name, value);
  return "";
}

inline const char* HelpText() {
  return "Pokemon XD PC port - options\n"
         "\n"
         "  --disc <file>            use this disc image (a disc that loads is remembered, as always)\n"
         "  --fullscreen             full screen, for this run\n"
         "  --windowed               a window, for this run\n"
         "  --data-folder <folder>   keep saves, settings, logs, cache and mods in this folder, for this run\n"
         "  --preset original|enhanced   play with this preset, for this run\n"
         "  --save <name>            load this save (a name from the launcher's Save page), for this run\n"
         "  --replay <file>          watch a battle replay file (xd only), for this run\n"
         "  --turn <number>          with --replay: start at that turn\n"
         "  --no-launcher            start the game at once with the remembered disc (the launcher and the\n"
         "                           start script; xd itself never opens the launcher)\n"
         "  --help                   this text\n"
         "  --version                the version\n"
         "\n"
         "An option is for that run only and is never saved. The old way still works: xd <disc image>.\n"
         "More: docs/COMMAND_LINE.md\n";
}

}

#endif
