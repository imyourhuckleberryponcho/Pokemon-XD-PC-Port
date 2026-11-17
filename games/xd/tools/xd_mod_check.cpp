#include "xd_mod_rules.h"

#include <modcheck.h>
#include <modsafety.h>

#include <cstdio>
#include <cstring>
#include <string>

namespace mc = engine::modcheck;
namespace fs = std::filesystem;

namespace {
void Usage() {
  std::fprintf(stderr,
               "Usage: xd_mod_check <mod folder | mod.zip> [--disc <disc image>] [--mods-folder <folder>] [--strict] [--json]\n"
               "  Lists everything wrong with a mod. Without --disc, table and field names and number ranges are not checked.\n"
               "  A .zip is checked the way the launcher will check a download (safety scan, unpacked into a scratch folder, then the checks).\n"
               "  --strict: warnings give exit code 2.  --json: the findings as JSON.\n"
               "  Exit: 0 fine, 1 problems, 2 warnings (with --strict), 3 could not run.\n");
}

std::string Esc(const std::string& s) {
  std::string out;
  for (unsigned char c : s) {
    if (c == '"' || c == '\\') {
      out += '\\';
      out += static_cast<char>(c);
    } else if (c == '\n') {
      out += "\\n";
    } else if (c < 0x20) {
      char b[8];
      std::snprintf(b, sizeof b, "\\u%04x", c);
      out += b;
    } else {
      out += static_cast<char>(c);
    }
  }
  return out;
}
}

int main(int argc, char** argv) {
  std::string folder, disc, modsFolder;
  bool strict = false, json = false;
  for (int i = 1; i < argc; i++) {
    const std::string a = argv[i];
    if (a == "--disc" && i + 1 < argc) disc = argv[++i];
    else if (a == "--mods-folder" && i + 1 < argc) modsFolder = argv[++i];
    else if (a == "--strict") strict = true;
    else if (a == "--json") json = true;
    else if (a == "--help" || a == "-h") {
      Usage();
      return 0;
    } else if (!a.empty() && a[0] == '-') {
      Usage();
      return 3;
    } else if (folder.empty()) {
      folder = a;
    } else {
      Usage();
      return 3;
    }
  }
  if (folder.empty()) {
    Usage();
    return 3;
  }
  const mc::GameRules rules = xd::modrules::Rules();
  engine::mods::LoadOptions load;
  mc::Options options;
  options.folder = fs::path(folder);
  options.rules = &rules;
  options.modsFolder = modsFolder.empty() ? fs::path() : fs::path(modsFolder);
  if (!disc.empty()) {
    std::string error;
    if (!xd::modrules::OpenDisc(disc, load, error)) {
      std::fprintf(stderr, "Could not use the disc: %s\n", error.c_str());
      return 3;
    }
    options.deep = &load;
  }
  mc::Report report;
  fs::path scratch;
  std::error_code ec;
  if (fs::is_regular_file(options.folder, ec)) {
    std::vector<uint8_t> zip;
    if (!mc::ReadFileBytes(options.folder, zip)) {
      std::fprintf(stderr, "Could not read %s\n", folder.c_str());
      return 3;
    }
    scratch = fs::temp_directory_path(ec) / ("xd_mod_check_" + mc::Sha256Hex(zip.data(), zip.size()).substr(0, 16));
    fs::remove_all(scratch, ec);
    mc::CheckAndExtractArchive(zip, scratch, options, mc::SafetyLimits(), report);
    report.folder = fs::path(folder);
  } else {
    report = mc::CheckMod(options);
  }
  if (json) {
    std::printf("{\"folder\": \"%s\", \"ok\": %s, \"findings\": [", Esc(mc::PathText(report.folder)).c_str(),
                report.Count(mc::Severity::Problem) == 0 ? "true" : "false");
    for (size_t i = 0; i < report.findings.size(); i++) {
      const mc::Finding& f = report.findings[i];
      std::printf("%s\n  {\"severity\": \"%s\", \"category\": \"%s\", \"code\": \"%s\", \"file\": \"%s\", \"line\": %d, \"text\": \"%s\"}",
                  i ? "," : "", f.severity == mc::Severity::Problem ? "problem" : f.severity == mc::Severity::Warning ? "warning" : "note",
                  f.category == mc::Category::Safety ? "safety" : "mod", f.code.c_str(), Esc(f.file).c_str(), f.line, Esc(f.text).c_str());
    }
    std::printf("\n], \"permissions\": [");
    for (size_t i = 0; i < report.permissions.size(); i++) std::printf("%s\"%s\"", i ? ", " : "", report.permissions[i].code.c_str());
    std::printf("]}\n");
  } else {
    std::fputs(report.Format().c_str(), stdout);
  }
  if (!scratch.empty()) fs::remove_all(scratch, ec);
  if (!disc.empty()) xd::modrules::CloseDisc();
  return report.ExitCode(strict);
}
