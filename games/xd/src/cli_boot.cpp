#include "cli_options.h"

#include <cstdio>
#include <string>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <io.h>
#include <windows.h>
#endif

namespace {
xd::cli::Options sOptions;
std::string sMessage;

}

extern "C" int PortXD_CommandLine(int argc, char** argv, const char* versionText, const char** message) {
  *message = nullptr;
  sOptions = xd::cli::Parse(argc, argv);
  if (sOptions.help) {
    std::fputs(xd::cli::HelpText(), stdout);
    return 0;
  }
  if (sOptions.version) {
    std::fprintf(stdout, "%s\n", versionText != nullptr ? versionText : "Pokemon XD PC");
    return 0;
  }
  if (sOptions.error.empty()) {
    sOptions.error = xd::cli::Apply(sOptions);
  }
  if (!sOptions.error.empty()) {
    sMessage = sOptions.error + "\n\nUse --help to see the options.";
    *message = sMessage.c_str();
    return 2;
  }
  const std::string summary = xd::cli::Summary(sOptions);
  if (!summary.empty()) {
    std::fprintf(stderr, "[options] started with: %s (for this run only; nothing is saved)\n", summary.c_str());
  }
  for (const std::string& note : sOptions.notes) std::fprintf(stderr, "[options] %s\n", note.c_str());
  return -1;
}

extern "C" const char* PortXD_CommandLineDisc(void) { return sOptions.disc.c_str(); }
