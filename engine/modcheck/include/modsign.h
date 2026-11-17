#ifndef ENGINE_MODSIGN_H
#define ENGINE_MODSIGN_H

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace engine::modcheck {

inline constexpr const char* kUpdateSignedPrefix = "mod-update-1\n";
inline constexpr size_t kMaxUpdateFileBytes = 64 * 1024;

struct UpdateFile {
  bool isSigned = false;
  bool signatureValid = false;
  std::string publicKey;
  std::string id, version, url, sha256, notes, date;
  uint64_t size = 0;
  std::vector<std::string> games, permissions;
};

bool ParseUpdateFile(std::string_view text, UpdateFile& out, std::string& error);

std::string UpdateManifestText(const UpdateFile& fields);
std::string SignedUpdateText(const UpdateFile& fields, const uint8_t seed[32], std::string& publicKeyHex);

bool ReadKeyFile(const std::filesystem::path& file, uint8_t seed[32], std::string& publicKeyHex, std::string& error);
bool MakeKeyFile(const std::filesystem::path& file, std::string& publicKeyHex, std::string& error);

std::string KeyFingerprint(std::string_view publicKeyHex);

}

#endif
