#include "../src/xd_settings.h"
#include "../src/xd_user_folder.h"

#include <settings.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace es = engine::settings;
namespace fs = std::filesystem;

namespace {

std::vector<std::string> sWarnings;

int Usage() {
  std::fprintf(stderr,
               "usage: xd_settings list [--all]\n"
               "       xd_settings show [settings.ini] [--env]\n"
               "       xd_settings check [settings.ini]\n"
               "       xd_settings docs <out.md> [--check]\n"
               "       xd_settings defaults <original|enhanced> <out.ini>\n");
  return 2;
}

void Setup(es::Registry& r) {
  r.SetWarningSink([](const std::string& m) { sWarnings.push_back(m); });
  xd_settings::Register(r);
}

fs::path DefaultFile() { return xd::SettingsFolder() / es::Registry::kFileName; }

bool IsPlayersFile(const fs::path& out) {
  std::error_code ec;
  if (fs::exists(out, ec) && fs::exists(DefaultFile(), ec) && fs::equivalent(out, DefaultFile(), ec)) {
    return true;
  }
  const fs::path a = fs::weakly_canonical(fs::absolute(out, ec), ec), b = fs::weakly_canonical(DefaultFile(), ec);
#ifdef _WIN32
  return _wcsicmp(a.c_str(), b.c_str()) == 0;
#else
  return a == b;
#endif
}

std::string PathText(const fs::path& p) {
  const std::u8string u8 = p.u8string();
  return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
}

const char* Kind(const es::Definition& d) { return d.testing ? "testing" : d.preference ? "preference" : "enhancement"; }

bool NumberedTwin(const es::Registry& r, const std::string& id) {
  const size_t dot = id.find('.');
  if (dot < 2 || id[dot - 1] < '2' || id[dot - 1] > '9') {
    return false;
  }
  std::string first = id;
  first[dot - 1] = '1';
  return r.Find(first) != es::kNoSetting;
}

int List(bool all) {
  es::Registry r;
  Setup(r);
  std::printf("%-36s %-11s %-6s %-16s %-16s %s\n", "id", "kind", "type", "original", "enhanced", "works yet");
  size_t skipped = 0;
  for (size_t i = 0; i < r.Count(); i++) {
    const es::Definition& d = r.Get(i);
    if (!all && NumberedTwin(r, d.id)) {
      skipped++;
      continue;
    }
    std::printf("%-36s %-11s %-6s %-16s %-16s %s\n", d.id.c_str(), Kind(d), es::TypeId(d.type),
                r.TextOf(i, d.original).c_str(), d.preference ? "same" : r.TextOf(i, d.enhanced).c_str(),
                d.implemented ? "yes" : "not yet");
  }
  std::printf("%zu settings", r.Count());
  if (skipped) {
    std::printf(" (%zu of controllers 2-4 not shown: --all)", skipped);
  }
  std::printf("\n");
  return 0;
}

int Show(const fs::path& file, bool env, bool quietValues) {
  es::Registry r;
  Setup(r);
  sWarnings.clear();
  if (!fs::exists(file)) {
    std::printf("%s: no such file (the game would create it with the Original preset)\n", PathText(file).c_str());
    return 2;
  }
  if (!r.LoadFile(file)) {
    std::printf("%s: can't be read\n", PathText(file).c_str());
    return 2;
  }
  const es::LoadReport& report = r.LastLoad();
  std::printf("file     %s\n", PathText(file).c_str());
  std::printf("format   %d%s\n", report.format,
              report.format == 0          ? " (from before format numbers; read as the current one)"
              : report.newerFormat        ? " (NEWER than this program knows)"
              : report.migrated           ? " (older; migrated)"
                                          : "");
  std::printf("preset   %s", es::PresetId(r.CurrentPreset()));
  if (r.CurrentPreset() == es::Preset::Custom) {
    std::printf(", based on %s", es::PresetId(r.BasePreset()));
  }
  std::printf("\n");
  if (env) {
    for (const std::string& line : xd_settings::ApplyEnvironment(r, [](const char* n) { return std::getenv(n); })) {
      std::printf("env      %s\n", line.c_str());
    }
  }
  std::printf("result   %s\n", r.Summary().c_str());

  if (!quietValues) {
    bool any = false;
    for (size_t i = 0; i < r.Count(); i++) {
      const es::Definition& d = r.Get(i);
      const es::Value def = r.DefaultOf(i, r.CurrentPreset());
      if (r.ValueText(i) == r.TextOf(i, def) && !r.IsOverridden(i)) {
        continue;
      }
      if (!any) {
        std::printf("\nnot at the preset's default:\n");
        any = true;
      }
      std::printf("  %-34s = %-16s (default %s; %s%s%s)\n", d.id.c_str(), r.ValueText(i).c_str(), r.TextOf(i, def).c_str(),
                  Kind(d), d.implemented ? "" : ", does nothing yet", r.IsOverridden(i) ? ", for this run" : "");
    }
    if (!any) {
      std::printf("\nevery value is the preset's default\n");
    }
  }
  for (const std::string& id : report.unknownIds) {
    std::printf("unknown  %s (kept in the file; misspelt, or from a newer version or a mod)\n", id.c_str());
  }
  for (const auto& [from, to] : report.renamed) {
    std::printf("renamed  %s -> %s\n", from.c_str(), to.c_str());
  }
  for (const std::string& w : sWarnings) {
    std::printf("warning  %s\n", w.c_str());
  }
  return sWarnings.empty() && report.unknownIds.empty() ? 0 : 1;
}

int Check(const fs::path& file) {
  int result = Show(file, false, true);
  es::Registry r;
  Setup(r);
  const std::vector<std::string> broken = es::CheckRules(r);
  for (const std::string& b : broken) {
    std::printf("RULE     %s\n", b.c_str());
  }
  if (!broken.empty() && result == 0) {
    result = 1;
  }
  std::printf("%s\n", result == 0 ? "fine" : result == 1 ? "has problems (see above)" : "can't be read");
  return result;
}

int Docs(const fs::path& out, bool checkOnly) {
  es::Registry r;
  Setup(r);
  const std::string text = es::SettingsMarkdown(r, "Settings", xd_settings::DocsIntro());
  if (checkOnly) {
    std::ifstream in(out, std::ios::binary);
    const std::string old((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const bool same = old == text;
    std::printf("%s %s\n", PathText(out).c_str(), same ? "is up to date" : "is OUT OF DATE (run: xd_settings docs <file>)");
    return same ? 0 : 1;
  }
  if (IsPlayersFile(out)) {
    std::fprintf(stderr, "refusing to write the player's own settings.ini; give another file\n");
    return 2;
  }
  std::ofstream f(out, std::ios::binary | std::ios::trunc);
  f.write(text.data(), static_cast<std::streamsize>(text.size()));
  if (!f) {
    std::fprintf(stderr, "could not write %s\n", PathText(out).c_str());
    return 2;
  }
  std::printf("wrote %s (%zu settings)\n", PathText(out).c_str(), r.Count());
  return 0;
}

int Defaults(const char* preset, const fs::path& out) {
  es::Preset p;
  if (!es::ParsePreset(preset, p) || p == es::Preset::Custom) {
    return Usage();
  }
  if (IsPlayersFile(out)) {
    std::fprintf(stderr, "refusing to write the player's own settings.ini; give another file\n");
    return 2;
  }
  es::Registry r;
  Setup(r);
  r.ApplyPreset(p);
  if (!r.SaveFile(out)) {
    return 2;
  }
  std::printf("wrote %s (preset %s)\n", PathText(out).c_str(), preset);
  return 0;
}

}

int main(int argc, char** argv) {
  if (argc < 2) {
    return Usage();
  }
  const std::string cmd = argv[1];
  std::vector<std::string> args, flags;
  for (int i = 2; i < argc; i++) {
    (std::strncmp(argv[i], "--", 2) == 0 ? flags : args).push_back(argv[i]);
  }
  const auto flag = [&](const char* f) {
    for (const std::string& x : flags) {
      if (x == f) {
        return true;
      }
    }
    return false;
  };
  const auto fileArg = [&]() { return args.empty() ? DefaultFile() : fs::u8path(args[0]); };

  if (cmd == "list") {
    return List(flag("--all"));
  }
  if (cmd == "show") {
    return Show(fileArg(), flag("--env"), false) == 2 ? 2 : 0;
  }
  if (cmd == "check") {
    return Check(fileArg());
  }
  if (cmd == "docs" && args.size() == 1) {
    return Docs(fs::u8path(args[0]), flag("--check"));
  }
  if (cmd == "defaults" && args.size() == 2) {
    return Defaults(args[0].c_str(), fs::u8path(args[1]));
  }
  return Usage();
}
