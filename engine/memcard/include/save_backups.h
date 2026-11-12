#ifndef ENGINE_SAVE_BACKUPS_H
#define ENGINE_SAVE_BACKUPS_H

#include <cstdint>
#include <ctime>
#include <filesystem>
#include <string>
#include <vector>

namespace engine::memcard::backups {

constexpr size_t kKeep = 10;

inline std::filesystem::path FolderFor(const std::filesystem::path& cardRoot) { return cardRoot / L"save_backups"; }

struct File {
  std::string name;
  uint64_t size = 0;
  std::string comment;
  uint32_t savedAt = 0;
};

struct Backup {
  std::filesystem::path folder;
  std::string card;
  std::string name;
  std::string when;
  std::vector<File> files;
  uint64_t size = 0;
};

std::vector<Backup> List(const std::filesystem::path& backupsFolder);

std::filesystem::path Add(const std::filesystem::path& backupsFolder, const std::string& card,
                          const std::string& fileName, const std::vector<uint8_t>& bytes, std::time_t now,
                          size_t keep, std::string* error);

void Prune(const std::filesystem::path& cardBackups, size_t keep);

bool Restore(const Backup& backup, const std::string& fileName, const std::filesystem::path& cardFolder,
             const std::filesystem::path& backupsFolder, size_t keep, std::string& error);

std::string SizeText(uint64_t bytes);
std::string CardTimeText(uint32_t secondsSince2000);

}

#endif
