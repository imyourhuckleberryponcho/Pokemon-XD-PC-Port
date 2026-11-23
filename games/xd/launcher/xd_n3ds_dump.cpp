#include "xd_n3ds_dump.h"

#include <i18n.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

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

namespace {

uint32_t Be32(const unsigned char* p) {
  return static_cast<uint32_t>(p[0]) << 24 | static_cast<uint32_t>(p[1]) << 16 | static_cast<uint32_t>(p[2]) << 8 | static_cast<uint32_t>(p[3]);
}
uint64_t AlignUp(uint64_t n, uint64_t a) { return (n + a - 1) & ~(a - 1); }

bool OpenCia(std::ifstream& in, uint64_t fileSize, uint64_t& ncchOffset, std::string& error) {
  const std::string cut = Tr("That dump looks cut short or damaged: the game's data is not where the header says.");
  unsigned char h[0x2020] = {};
  in.clear();
  in.seekg(0);
  in.read(reinterpret_cast<char*>(h), sizeof h);
  if (in.gcount() < static_cast<std::streamsize>(sizeof h)) {
    error = cut;
    return false;
  }
  auto le32 = [&](size_t at) { return Le32(h + at); };
  const uint64_t cert = le32(8), tik = le32(12), tmdSize = le32(16), meta = le32(20);
  const uint64_t tmdOff = AlignUp(0x2020, 0x40) + AlignUp(cert, 0x40) + AlignUp(tik, 0x40);
  const uint64_t contentOff = tmdOff + AlignUp(tmdSize, 0x40) + AlignUp(meta, 0x40);
  if (tmdSize < 0x100 || tmdSize > 0x10000 || tmdOff + tmdSize > fileSize) {
    error = cut;
    return false;
  }
  std::vector<unsigned char> t(static_cast<size_t>(tmdSize));
  in.seekg(static_cast<std::streamoff>(tmdOff));
  in.read(reinterpret_cast<char*>(t.data()), static_cast<std::streamsize>(t.size()));
  if (in.gcount() < static_cast<std::streamsize>(t.size())) {
    error = cut;
    return false;
  }
  size_t sig = 0;
  switch (Be32(t.data())) {
    case 0x10000: case 0x10003: sig = 0x240; break;
    case 0x10001: case 0x10004: sig = 0x140; break;
    case 0x10002: case 0x10005: sig = 0x80; break;
    default: break;
  }
  if (sig == 0 || t.size() < sig + 0xC4 + 0x900) {
    error = cut;
    return false;
  }
  const uint32_t titleHigh = Be32(&t[sig + 0x4C]);
  if (titleHigh == 0x0004000E) {
    error = Tr("That .cia is the game's update, not the game itself. Choose the game's own .cia (or .3ds / .cci) file.");
    return false;
  }
  if (titleHigh == 0x0004008C) {
    error = Tr("That .cia is the game's extra content (DLC), not the game itself. Choose the game's own .cia (or .3ds / .cci) file.");
    return false;
  }
  if (titleHigh != 0x00040000) {
    error = Tr("That .cia is not a game (it is an app, a system file or something else).");
    return false;
  }
  const unsigned count = static_cast<unsigned>(t[sig + 0x9E]) << 8 | t[sig + 0x9F];
  const size_t records = sig + 0xC4 + 0x900;
  if (count == 0 || t.size() < records + static_cast<size_t>(count) * 0x30) {
    error = cut;
    return false;
  }
  uint64_t offset = contentOff;
  for (unsigned i = 0; i < count; ++i) {
    const unsigned char* r = &t[records + static_cast<size_t>(i) * 0x30];
    const unsigned index = static_cast<unsigned>(r[4]) << 8 | r[5];
    const unsigned type = static_cast<unsigned>(r[6]) << 8 | r[7];
    const uint64_t size = static_cast<uint64_t>(Be32(r + 8)) << 32 | Be32(r + 12);
    if ((h[0x20 + (index >> 3)] & (0x80u >> (index & 7))) == 0) continue;
    if (index == 0) {
      if (type & 1) {
        error = Tr("That .cia is encrypted. The converter only reads decrypted copies and does not decrypt anything itself: "
                   "export the game again as a decrypted .cia with your own dumping tool.");
        return false;
      }
      if (offset + 0x200 > fileSize) {
        error = cut;
        return false;
      }
      ncchOffset = offset;
      return true;
    }
    offset += size;
  }
  error = cut;
  return false;
}

}

bool IdentifyN3dsDump(const std::string& path, std::string& gameId, std::string& title, std::string& error) {
  namespace fs = std::filesystem;
  std::error_code ec;
  if (fs::is_directory(fs::path(path), ec)) {
    error = Tr("That is a folder. The converter needs the game as one file (.3ds, .cci or .cia); an unpacked folder can't be used yet.");
    return false;
  }
  std::ifstream in(fs::path(path), std::ios::binary);
  if (!in) {
    error = Tr("That file can't be opened.");
    return false;
  }
  unsigned char head[0x200] = {};
  in.read(reinterpret_cast<char*>(head), sizeof head);
  const std::streamsize got = in.gcount();
  uint64_t offset = 0;
  bool isCia = false;
  if (got >= 8 && std::memcmp(head + 0x100, "NCSD", 4) != 0 && Le32(head) == 0x2020 && head[4] == 0 && head[5] == 0) {
    isCia = true;
    const uint64_t fileSize = fs::file_size(fs::path(path), ec);
    if (ec || !OpenCia(in, fileSize, offset, error)) {
      if (ec) error = Tr("That file can't be opened.");
      return false;
    }
  } else {
    if (got < static_cast<std::streamsize>(sizeof head) || std::memcmp(head + 0x100, "NCSD", 4) != 0) {
      error = Tr("That is not a 3DS game file (.3ds, .cci or .cia). An .nds / .gba file or a disc image can't be used here.");
      return false;
    }
    offset = static_cast<uint64_t>(Le32(head + 0x120)) * 0x200u;
    if (offset == 0) {
      error = Tr("That dump has no game in it (its first partition is empty).");
      return false;
    }
  }
  unsigned char ncch[0x200] = {};
  in.clear();
  in.seekg(static_cast<std::streamoff>(offset));
  in.read(reinterpret_cast<char*>(ncch), sizeof ncch);
  if (in.gcount() < static_cast<std::streamsize>(sizeof ncch) || std::memcmp(ncch + 0x100, "NCCH", 4) != 0) {
    error = Tr("That dump looks cut short or damaged: the game's data is not where the header says.");
    return false;
  }
  if ((ncch[0x188 + 7] & 4) == 0) {
    if (isCia) {
      error = Tr("That .cia is encrypted. The converter only reads decrypted copies and does not decrypt anything itself: "
                 "export the game again as a decrypted .cia with your own dumping tool.");
      return false;
    }
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
