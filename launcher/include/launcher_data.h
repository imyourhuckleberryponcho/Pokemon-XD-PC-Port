#ifndef LAUNCHER_DATA_H
#define LAUNCHER_DATA_H

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace launcher::datafolder {

struct Tally {
  uint64_t files = 0;
  uint64_t bytes = 0;
};

Tally Measure(const std::filesystem::path& folder);

bool IsEmpty(const std::filesystem::path& folder);

bool IsInside(const std::filesystem::path& inner, const std::filesystem::path& outer);

bool Copy(const std::filesystem::path& from, const std::filesystem::path& to, const std::atomic<bool>& stop,
          const std::function<void(uint64_t files, uint64_t bytes)>& progress, std::string& error);

bool Verify(const std::filesystem::path& from, const std::filesystem::path& to, std::string& error);

bool RemoveContents(const std::filesystem::path& folder, std::vector<std::filesystem::path>& left, std::string& error);

}

#endif
