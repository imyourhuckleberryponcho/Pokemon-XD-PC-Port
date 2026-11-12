#ifndef ENGINE_UPDATE_H
#define ENGINE_UPDATE_H

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace engine::update {

inline constexpr std::string_view kSignedPrefix = "port-update-manifest-1\n";
inline constexpr uint64_t kMaxManifestBytes = 64 * 1024;
inline constexpr uint64_t kMaxPackageBytes = 2ull * 1024 * 1024 * 1024;

bool ParseVersion(std::string_view text, std::vector<uint32_t>& parts);
int CompareVersions(std::string_view a, std::string_view b);
bool IsNewer(std::string_view candidate, std::string_view current);

struct Manifest {
  std::string product;
  std::string version;
  std::string date;
  std::string notes;
  std::string url;
  uint64_t size = 0;
  std::string sha256;
};

bool ParseSignedManifest(std::string_view fileText, std::string_view publicKeyHex, std::string_view product,
                         Manifest& out, std::string& error);

bool HexDecode(std::string_view hex, std::vector<uint8_t>& out);
std::string HexEncode(const uint8_t* data, size_t size);
bool VerifyEd25519(const uint8_t publicKey[32], const uint8_t signature[64], std::string_view message);
void SignEd25519(const uint8_t seed[32], std::string_view message, uint8_t signature[64], uint8_t publicKey[32]);

struct FetchOptions {
  uint64_t maxBytes = kMaxManifestBytes;
  std::string userAgent = "PortUpdater/1";
  int timeoutMs = 20000;
  std::function<bool(uint64_t done, uint64_t total)> progress;
};
bool FetchToString(const std::string& url, const FetchOptions& options, std::string& out, std::string& error);
bool FetchToFile(const std::string& url, const std::filesystem::path& file, const FetchOptions& options,
                 std::string& error);

bool Sha256File(const std::filesystem::path& file, std::string& hex, std::string& error);

bool ExtractZip(const std::filesystem::path& zip, const std::filesystem::path& folder, std::string& error,
                const std::function<bool()>& stop = {});

inline constexpr const char* kPointerFile = "game_folder.txt";
inline constexpr const char* kFirstFolder = "game";
inline constexpr const char* kVersionFile = "version.txt";

bool IsGameFolderName(std::string_view name);

struct Layout {
  bool valid = false;
  std::string why;
  std::filesystem::path root;
  std::string running;
  std::string current;
  std::string previous;
};
Layout FindLayout(const std::filesystem::path& programFolder, const std::string& starterName);
bool ReadPointer(const std::filesystem::path& root, std::string& current, std::string& previous);
bool WritePointer(const std::filesystem::path& root, const std::string& current, const std::string& previous,
                  std::string& error);
std::string FolderVersion(const std::filesystem::path& folder);
bool FolderUsable(const std::filesystem::path& folder, const std::vector<std::string>& requiredFiles);

struct InstallRequest {
  Layout layout;
  Manifest manifest;
  std::filesystem::path package;
  std::vector<std::string> requiredFiles;
};
bool Install(const InstallRequest& request, std::string& installed, std::string& error,
             const std::function<bool()>& stop = {});
bool GoBack(const Layout& layout, const std::vector<std::string>& requiredFiles, std::string& error);

}

#endif
