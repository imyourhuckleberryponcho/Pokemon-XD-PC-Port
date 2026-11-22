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

inline constexpr const char* kKeyChangePrefix = "mod-key-change-1\n";
struct KeyChange {
  std::string oldKey, newKey, signature;
};

struct UpdateFile {
  bool isSigned = false;
  bool signatureValid = false;
  std::string publicKey;
  std::string id, version, url, sha256, notes, date;
  uint64_t size = 0;
  std::vector<std::string> games, permissions;
  std::vector<KeyChange> keyChanges;
};

bool ParseUpdateFile(std::string_view text, UpdateFile& out, std::string& error);

std::string UpdateManifestText(const UpdateFile& fields);
std::string SignedUpdateText(const UpdateFile& fields, const uint8_t seed[32], std::string& publicKeyHex,
                             const std::vector<KeyChange>& keyChanges = {});

bool MakeKeyChange(const uint8_t oldSeed[32], const std::string& newPublicHex, KeyChange& out, std::string& error);
bool VerifyKeyChange(const KeyChange& change);
bool KeyChainReaches(const std::vector<KeyChange>& changes, const std::string& from, const std::string& to);
std::vector<KeyChange> ReadKeyChanges(const std::filesystem::path& keyFile);
bool RotateKeyFile(const std::filesystem::path& oldFile, const std::filesystem::path& newFile, std::string& message, std::string& error);

bool ReadKeyFile(const std::filesystem::path& file, uint8_t seed[32], std::string& publicKeyHex, std::string& error);
bool MakeKeyFile(const std::filesystem::path& file, std::string& publicKeyHex, std::string& error);

std::string KeyFingerprint(std::string_view publicKeyHex);

}

#endif
