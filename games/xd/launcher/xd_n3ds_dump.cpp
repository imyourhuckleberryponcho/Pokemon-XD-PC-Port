#include "xd_n3ds_dump.h"

#include <i18n.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace xd {

namespace {

using engine::i18n::Tr;

uint32_t Le32(const unsigned char* p) {
  return static_cast<uint32_t>(p[0]) | static_cast<uint32_t>(p[1]) << 8 | static_cast<uint32_t>(p[2]) << 16 |
         static_cast<uint32_t>(p[3]) << 24;
}

const char* TitleOf(const std::string& code) {
  static const struct { const char* code; const char* title; } kKnown[] = {
      {"CTR-P-A2A", "Pokemon Ultra Sun"}, {"CTR-P-A2B", "Pokemon Ultra Moon"}, {"CTR-P-BNE", "Pokemon Moon"}, {"CTR-P-BND", "Pokemon Sun"},
      {"CTR-P-EGD", "Pokemon X"},         {"CTR-P-EGE", "Pokemon Y"},          {"CTR-P-ECR", "Pokemon Omega Ruby"},
      {"CTR-P-ECL", "Pokemon Alpha Sapphire"},
  };
  for (const auto& k : kKnown) {
    if (code == k.code) return k.title;
  }
  return "";
}

}

bool IdentifyN3dsDump(const std::string& path, std::string& gameId, std::string& title, std::string& error) {
  namespace fs = std::filesystem;
  std::string ext = fs::path(path).extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  std::error_code ec;
  if (fs::is_directory(fs::path(path), ec)) {
    error = Tr("That is a folder. The converter needs the cartridge dump as one file (.3ds or .cci); an unpacked folder can't be used yet.");
    return false;
  }
  if (ext == ".cia") {
    error = Tr("That is a .cia file (an installable copy). The converter needs the decrypted cartridge dump (.3ds or .cci); a .cia can't be used yet.");
    return false;
  }
  std::ifstream in(fs::path(path), std::ios::binary);
  if (!in) {
    error = Tr("That file can't be opened.");
    return false;
  }
  unsigned char head[0x200] = {};
  in.read(reinterpret_cast<char*>(head), sizeof head);
  if (in.gcount() < static_cast<std::streamsize>(sizeof head) || std::memcmp(head + 0x100, "NCSD", 4) != 0) {
    error = Tr("That is not a 3DS cartridge dump (.3ds or .cci). A .cia, an .nds / .gba file or a disc image can't be used here.");
    return false;
  }
  const uint64_t offset = static_cast<uint64_t>(Le32(head + 0x120)) * 0x200u;
  if (offset == 0) {
    error = Tr("That dump has no game in it (its first partition is empty).");
    return false;
  }
  unsigned char ncch[0x200] = {};
  in.seekg(static_cast<std::streamoff>(offset));
  in.read(reinterpret_cast<char*>(ncch), sizeof ncch);
  if (in.gcount() < static_cast<std::streamsize>(sizeof ncch) || std::memcmp(ncch + 0x100, "NCCH", 4) != 0) {
    error = Tr("That dump looks cut short or damaged: the game's data is not where the header says.");
    return false;
  }
  if ((ncch[0x188 + 7] & 4) == 0) {
    error = Tr("That dump is encrypted. The converter only reads decrypted dumps and does not decrypt anything itself: use a decrypted copy of your own cartridge.");
    return false;
  }
  std::string code(reinterpret_cast<const char*>(ncch + 0x150), 16);
  code.resize(std::min<size_t>(code.find('\0') == std::string::npos ? code.size() : code.find('\0'), 16));
  if (code.size() < 9) {
    error = Tr("That dump has no readable game code.");
    return false;
  }
  gameId = code.substr(0, 9);
  title = TitleOf(gameId);
  return true;
}

}
