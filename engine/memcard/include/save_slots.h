#ifndef ENGINE_SAVE_SLOTS_H
#define ENGINE_SAVE_SLOTS_H

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace engine::memcard::slots {

constexpr const char* kMainId = "main";
constexpr const char* kMainName = "Save 1";
constexpr size_t kMaxNameBytes = 60;

struct Slot {
  std::string id;
  std::string name;
};

struct Index {
  std::string active = kMainId;
  std::vector<Slot> slots;
  const Slot* Find(const std::string& id) const;
  const Slot& Active() const;
};

std::filesystem::path IndexFile(const std::filesystem::path& dataFolder);
std::filesystem::path RootOf(const std::filesystem::path& dataFolder, const std::string& id);

Index Load(const std::filesystem::path& dataFolder, std::string* note = nullptr);
bool Save(const std::filesystem::path& dataFolder, const Index& index, std::string& error);

std::filesystem::path ActiveRoot(const std::filesystem::path& dataFolder, std::string* name = nullptr);

std::string CleanName(const std::string& name);
std::string NextName(const Index& index);

bool SetActive(const std::filesystem::path& dataFolder, const std::string& id, std::string& error);
bool Create(const std::filesystem::path& dataFolder, const std::string& name, std::string& newId, std::string& error);
bool Rename(const std::filesystem::path& dataFolder, const std::string& id, const std::string& name,
            std::string& error);
bool Duplicate(const std::filesystem::path& dataFolder, const std::string& id, std::string& newId, std::string& error);
using Bin = std::function<bool(const std::filesystem::path& folder, std::string& error)>;
bool Delete(const std::filesystem::path& dataFolder, const std::string& id, const Bin& bin, std::string& error);

std::vector<std::filesystem::path> RegionFolders(const std::filesystem::path& cardRoot);

bool Import(const std::filesystem::path& dataFolder, const std::filesystem::path& file, const std::string& name,
            const std::filesystem::path& cardFolder, const std::string& cardFileName, std::string& newId,
            std::string& error);
bool Export(const std::filesystem::path& cardFile, const std::filesystem::path& to, std::string& error);

}

#endif
