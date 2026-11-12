#include "xd_settings.h"
#include "xd_user_folder.h"

#include <settings.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>

namespace {
std::string sSummary = "settings not loaded";
}

extern "C" void PortXD_LoadSettings(void) {
  namespace es = engine::settings;
  es::Registry& registry = es::Global();
  xd_settings::Register(registry);

  const std::filesystem::path folder = xd::SettingsFolder();
  if (registry.Load(folder)) {
    const es::LoadReport& report = registry.LastLoad();
    std::fprintf(stderr, "[settings] read %ls%s\n", (folder / es::Registry::kFileName).wstring().c_str(),
                 report.migrated ? " (an older format, read as the current one; the file is left as it is)" : "");
    for (const std::string& id : report.unknownIds) {
      std::fprintf(stderr, "[settings] settings.ini has \"%s\", which this version doesn't know (kept)\n", id.c_str());
    }
    for (const auto& [from, to] : report.renamed) {
      std::fprintf(stderr, "[settings] settings.ini's \"%s\" is now called \"%s\"\n", from.c_str(), to.c_str());
    }
  } else if (xd::SaveSettings(registry) == xd::SettingsSave::Saved) {
    std::fprintf(stderr, "[settings] created %ls with the defaults\n", (folder / es::Registry::kFileName).wstring().c_str());
  }

  for (const std::string& line : xd_settings::ApplyEnvironment(registry, [](const char* n) { return std::getenv(n); })) {
    std::fprintf(stderr, "[settings] %s\n", line.c_str());
  }
  sSummary = registry.Summary();
  std::fprintf(stderr, "[settings] %s\n", sSummary.c_str());
  std::fflush(stderr);

  const char* require = std::getenv("XD_REQUIRE_ORIGINAL");
  if (require != nullptr && std::strcmp(require, "1") == 0 && !registry.IsOriginal()) {
    std::fprintf(stderr, "[settings] XD_REQUIRE_ORIGINAL=1, but this run is not pure Original; stopping. "
                         "(XD_PRESET=original runs Original whatever settings.ini says.)\n");
    std::fflush(stderr);
    std::exit(3);
  }
}

extern "C" void PortXD_SaveSettings(void) {
  namespace es = engine::settings;
  if (xd::SaveSettings(es::Global()) == xd::SettingsSave::Saved) {
    std::fprintf(stderr, "[settings] saved to %ls\n", (xd::SettingsFolder() / es::Registry::kFileName).wstring().c_str());
  }
}

xd::SettingsSave xd::SaveSettings(const engine::settings::Registry& registry) {
  if (xd::PlayerFilesReadOnly()) {
    std::fprintf(stderr, "[settings] %s\n", xd::kPlayerFilesReadOnlyNote);
    std::fflush(stderr);
    return xd::SettingsSave::Skipped;
  }
  return registry.Save(xd::SettingsFolder()) ? xd::SettingsSave::Saved : xd::SettingsSave::Failed;
}

extern "C" bool PortXD_TestRun(void) { return xd::TestRun(); }

extern "C" const char* PortXD_SettingsSummary(void) { return sSummary.c_str(); }
