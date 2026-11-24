#ifndef ENGINE_SAVEEXT_H
#define ENGINE_SAVEEXT_H

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace engine::saveext {

constexpr uint32_t kFormat = 1;

struct Section {
  std::string name;
  uint16_t version = 1;
  bool required = false;
  std::vector<uint8_t> data;
};

struct File {
  uint32_t format = kFormat;
  uint64_t saveHash = 0;
  std::string writer;
  std::vector<Section> sections;

  const Section* Find(const std::string& name) const;
  void Set(Section section);
  std::vector<std::string> RequiredNames() const;
};

uint64_t Hash64(const uint8_t* data, size_t size);
uint32_t Crc32(const uint8_t* data, size_t size);

std::vector<uint8_t> Encode(const File& file);
enum class Decoded { Ok, BadMagic, TooNew, Truncated, BadChecksum };
Decoded Decode(const uint8_t* data, size_t size, File& out);
const char* DecodedText(Decoded d);

std::filesystem::path FileFor(const std::filesystem::path& folder, std::string_view key);
std::filesystem::path StagedFor(const std::filesystem::path& folder, std::string_view key);

bool WriteAtomic(const std::filesystem::path& path, const std::vector<uint8_t>& bytes);
bool ReadAll(const std::filesystem::path& path, std::vector<uint8_t>& out);

bool Stage(const std::filesystem::path& folder, std::string_view key, const File& file);
bool Commit(const std::filesystem::path& folder, std::string_view key);
void DropStaged(const std::filesystem::path& folder, std::string_view key);

enum class State {
  Missing,
  Paired,
  Stale,
  Damaged,
  TooNew,
};
const char* StateName(State s);
struct Loaded {
  State state = State::Missing;
  File file;
  bool promoted = false;
};
Loaded Load(const std::filesystem::path& folder, std::string_view key, uint64_t saveHashNow);

bool Repair(const std::filesystem::path& folder, std::string_view key, const std::vector<uint8_t>& oldSave,
            const std::vector<uint8_t>& newSave);

bool PairsWith(const std::vector<uint8_t>& ext, const std::vector<uint8_t>& save);
constexpr size_t kGciHeader = 0x40;
bool PairsWithGci(const std::vector<uint8_t>& ext, const std::vector<uint8_t>& gci);

}

#endif
