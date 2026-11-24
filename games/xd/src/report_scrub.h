#ifndef XD_REPORT_SCRUB_H
#define XD_REPORT_SCRUB_H

#include <filesystem>
#include <string>
#include <vector>

namespace xd::report {

struct Home {
  std::vector<std::string> folders;
  std::string userName;
  bool windows = false;
};
Home HomeOfThisAccount();
std::string Scrub(const std::string& text, const Home& who);
void ScrubReportFolder(const std::filesystem::path& folder);

}

#endif
