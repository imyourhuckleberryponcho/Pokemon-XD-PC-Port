#ifndef ENGINE_MODPACK_H
#define ENGINE_MODPACK_H

#include "modcheck.h"
#include "modsign.h"

#include <set>
#include <string>
#include <vector>

namespace engine::modcheck {

struct Refusal {
  std::string path;
  std::string code;
  std::string reason;
};

struct PackOptions {
  Options check;
  std::filesystem::path outDir;
  const engine::mods::DiscView* disc = nullptr;
  std::set<std::string> refuseHashes;
  bool allowSameSizeTextures = false;
  std::string urlBase;
  std::string notes;
  bool strict = false;
  std::filesystem::path signKey;
};

struct PackResult {
  bool ok = false;
  Report report;
  std::vector<Refusal> refused;
  std::vector<std::string> included;
  std::vector<std::string> leftOut;
  std::filesystem::path zip, updateFile;
  std::string sha256;
  uint64_t size = 0;
  std::vector<std::string> messages;
  std::string error;
  std::string Format() const;
};

PackResult PackMod(const PackOptions& options);

std::vector<Refusal> GameDataReasons(const std::string& path, const std::vector<uint8_t>& head);
std::vector<uint8_t> MakeZip(const std::vector<std::pair<std::string, std::vector<uint8_t>>>& files);
std::string UpdateJson(const std::string& id, const std::string& version, const std::string& url, const std::string& sha256, uint64_t size,
                       const std::string& notes, const std::vector<std::string>& games, const std::vector<std::string>& permissions);

}

#endif
