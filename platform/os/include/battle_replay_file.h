#ifndef PORT_BATTLE_REPLAY_FILE_H
#define PORT_BATTLE_REPLAY_FILE_H

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace port_replay {

constexpr int kFormat = 1;

struct File {
  std::vector<std::pair<std::string, std::string>> header;
  std::string state;
  std::vector<std::string> body;

  const std::string* Find(const std::string& key) const;
  std::string Get(const std::string& key, const std::string& fallback = "") const;
  void Set(const std::string& key, const std::string& value);
};

bool Write(const File& file, std::string& bytes, std::string* why = nullptr);
bool Parse(const std::string& bytes, File& file, std::string* why = nullptr);
bool ReadHeader(const std::string& bytes, File& file, std::string* why = nullptr);

struct Difference {
  std::string key, wanted, have, sentence;
};
std::vector<Difference> Differences(const File& file, const std::vector<std::pair<std::string, std::string>>& mine,
                                    const std::vector<std::string>& mustMatch);

std::string Hex(uint64_t value);
uint32_t Crc32(const std::string& data);

}

#endif
