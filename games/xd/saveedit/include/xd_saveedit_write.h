#ifndef XD_SAVEEDIT_WRITE_H
#define XD_SAVEEDIT_WRITE_H

#include <xd_saveedit.h>

#include <filesystem>
#include <functional>
#include <string>

namespace xd::saveedit {

struct CommitRequest {
  std::filesystem::path cardRoot;
  std::filesystem::path gciFile;
  std::filesystem::path setupFolder;
  std::string writer = "Pokemon XD PC port save editor";
  std::function<bool()> cardIsFree;
};

struct CommitResult {
  bool ok = false;
  std::string message;
  std::filesystem::path backupFolder;
  bool marked = false;
  std::string oldKey, newKey;
  bool extensionRepaired = false;
};

CommitResult Commit(const Editor& editor, const Tables& tables, const CommitRequest& request);

std::string WhyNotNow(const CommitRequest& request);

std::string MarkLine(const std::string& when, const std::string& reasons);

}

#endif
