#include "pack_match.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <system_error>

namespace xd::packmatch {

namespace fs = std::filesystem;

namespace {
bool ReadAll(const fs::path& p, std::string& out) {
  std::ifstream in(p, std::ios::binary);
  if (!in) return false;
  out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  return true;
}
bool Exists(const fs::path& p) {
  std::error_code ec;
  return fs::exists(p, ec);
}
bool JsonText(const std::string& json, const char* key, std::string& out) {
  const std::string k = std::string("\"") + key + "\"";
  size_t at = json.find(k);
  if (at == std::string::npos) return false;
  at = json.find(':', at);
  if (at == std::string::npos) return false;
  at++;
  while (at < json.size() && (json[at] == ' ' || json[at] == '\t' || json[at] == '\n' || json[at] == '\r')) at++;
  if (at < json.size() && json[at] == '"') {
    const size_t end = json.find('"', at + 1);
    if (end == std::string::npos) return false;
    out = json.substr(at + 1, end - at - 1);
    return true;
  }
  size_t end = at;
  while (end < json.size() && json[end] != ',' && json[end] != '}' && json[end] != '\n' && json[end] != '\r') end++;
  out = json.substr(at, end - at);
  return true;
}
}

std::string Fnv64Hex(const void* data, size_t size) {
  uint64_t h = 14695981039346656037ull;
  const auto* p = static_cast<const uint8_t*>(data);
  for (size_t i = 0; i < size; i++) h = (h ^ p[i]) * 1099511628211ull;
  char buf[17];
  std::snprintf(buf, sizeof buf, "%016llx", static_cast<unsigned long long>(h));
  return buf;
}

Gen4Pack ReadGen4(const fs::path& folder) {
  Gen4Pack g;
  g.folder = folder;
  g.usable = Exists(folder / L"gen4.json");
  if (!g.usable) return g;
  std::string tables;
  if (ReadAll(folder / L"tables.txt", tables)) {
    for (size_t at = 0; at < tables.size();) {
      size_t end = tables.find('\n', at);
      if (end == std::string::npos) end = tables.size();
      if (tables.compare(at, 13, "row snd_file ") == 0) g.cryRows++;
      else if (tables.compare(at, 5, "wave ") == 0) g.waves++;
      at = end + 1;
    }
  }
  std::string toc;
  if (ReadAll(folder / L"files" / L"GSfsys.toc", toc)) g.tocHash = Fnv64Hex(toc.data(), toc.size());
  return g;
}

Gen57Pack ReadGen57(const fs::path& folder) {
  Gen57Pack g;
  std::string json;
  if (!ReadAll(folder / L"gen57.json", json)) return g;
  g.usable = true;
  std::string v;
  g.ownCries = JsonText(json, "cry_archive", v) && v == "own";
  if (JsonText(json, "cry_rows_reserved", v)) g.reserved = static_cast<uint32_t>(std::strtoul(v.c_str(), nullptr, 10));
  JsonText(json, "gen4_toc", g.gen4Toc);
  std::string tables;
  if (ReadAll(folder / L"tables.txt", tables)) {
    const size_t at = tables.find("row snd_file ");
    if (at != std::string::npos) g.firstCryRow = static_cast<uint32_t>(std::strtoul(tables.c_str() + at + 13, nullptr, 10));
  }
  return g;
}

fs::path Gen4Folder(const fs::path& assets, bool gen57Wanted) {
  const fs::path b = assets / L"pbr_gen4", u = assets / L"usum_gen4";
  const bool haveB = Exists(b / L"gen4.json"), haveU = Exists(u / L"gen4.json");
  const fs::path preferred = (!haveB && haveU) ? u : b;
  if (!gen57Wanted) return preferred;
  const Gen57Pack g57 = ReadGen57(assets / L"usum_gen57");
  if (!g57.usable || g57.ownCries || g57.firstCryRow < kDiscSndFile) return preferred;
  const uint32_t need = g57.firstCryRow - kDiscSndFile;
  for (const fs::path& c : {b, u}) {
    const Gen4Pack g = ReadGen4(c);
    if (g.usable && g.cryRows == need) return c;
  }
  return preferred;
}

Problem CheckGen57(const fs::path& assets) {
  const Gen57Pack g57 = ReadGen57(assets / L"usum_gen57");
  if (!g57.usable) return Problem::None;
  const Gen4Pack g4 = ReadGen4(Gen4Folder(assets, true));
  if (!g4.usable) return Problem::None;
  if (g57.ownCries) {
    if (!g57.gen4Toc.empty() && !g4.tocHash.empty() && g57.gen4Toc != g4.tocHash) return Problem::TocDiffers;
    const uint32_t room = g57.reserved != 0 ? g57.reserved : kReservedCries;
    if (g4.cryRows > room || g4.waves > room) return Problem::TooManyCries;
    return Problem::None;
  }
  if (g57.firstCryRow >= kDiscSndFile && g4.cryRows != g57.firstCryRow - kDiscSndFile) return Problem::LegacyGen4Gone;
  const Gen4Pack preferred = ReadGen4(Gen4Folder(assets, false));
  if (g57.firstCryRow >= kDiscSndFile && preferred.usable && preferred.folder != g4.folder) return Problem::LegacyOtherGen4;
  return Problem::None;
}

AssetState StateOfAsset(const std::string& id, const fs::path& assets) {
  const auto versionOf = [](const fs::path& file, int& version) {
    std::string json, v;
    if (!ReadAll(file, json)) return false;
    version = JsonText(json, "version", v) ? std::atoi(v.c_str()) : 0;
    return true;
  };
  const auto modelsReady = [&](const char* folder, const char* modId) {
    std::string json, v;
    if (!ReadAll(assets / folder / L"mod.json", json)) return AssetState::NotMade;
    if (!JsonText(json, "id", v) || v != modId) return AssetState::NotMade;
    std::error_code ec;
    for (auto it = fs::directory_iterator(assets / folder / L"fsys", ec); !ec && it != fs::directory_iterator(); it.increment(ec)) return AssetState::Ready;
    return AssetState::NotMade;
  };
  if (id == "br_models") return modelsReady("pbr", "battle-revolution-models");
  if (id == "usum_models") return modelsReady("usum", "ultra-sun-moon-models");
  if (id == "gen4_pack") {
    const fs::path folder = Gen4Folder(assets, false);
    int version = 0;
    if (!versionOf(folder / L"gen4.json", version)) return AssetState::NotMade;
    return version == kGen4PackVersion ? AssetState::Ready : AssetState::OtherVersion;
  }
  if (id == "gen57_pack") {
    int version = 0;
    if (!versionOf(assets / L"usum_gen57" / L"gen57.json", version)) return AssetState::NotMade;
    if (version != kGen57PackVersion) return AssetState::OtherVersion;
    int v4 = 0;
    if (!versionOf(Gen4Folder(assets, true) / L"gen4.json", v4) || v4 != kGen4PackVersion) return AssetState::DoesNotFit;
    return Works(CheckGen57(assets)) ? AssetState::Ready : AssetState::DoesNotFit;
  }
  return AssetState::UnknownId;
}

}
