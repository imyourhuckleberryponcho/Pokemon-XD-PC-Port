#include "ui_kit2.h"

#include <aurora/imgui.h>
#include <gs_formats.h>
#include <gx_texture_decode.h>
#include <dolphin/dvd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace xd::kit2 {
namespace {

const char* const kArchives[] = {"common.fsys",     "field_common.fsys", "pocket_menu.fsys",  "name_entry_menu.fsys",
                                 "orre_menu.fsys",  "pcbox_menu.fsys",   "pcbox_pocket_menu.fsys", "fight_common.fsys",
                                 "topmenu.fsys", "title.fsys", "pokemonchange_menu.fsys", "waza_menu.fsys", "pda_menu.fsys", "menu_common.fsys",
                                 "pda_menu_fr.fsys", "pda_menu_ge.fsys", "pda_menu_it.fsys", "pda_menu_sp.fsys"};

struct Archive {
  std::vector<uint8_t> bytes;
  FsysArchive fsys{};
  std::map<uint32_t, uint32_t> textures;
  bool ok = false;
};
std::vector<std::unique_ptr<Archive>> sArchives;
bool sIndexed = false;
std::map<uint32_t, Image> sImages;
Image sNone;

std::vector<uint8_t> ReadDiscFile(const char* name) {
  std::vector<uint8_t> out;
  DVDFileInfo fi;
  std::memset(&fi, 0, sizeof fi);
  if (!DVDOpen(name, &fi)) return out;
  const uint32_t len = fi.length;
  out.resize((size_t(len) + 63) & ~size_t(31));
  const s32 got = DVDReadPrio(&fi, out.data(), static_cast<s32>((len + 31) & ~31u), 0, 2);
  DVDClose(&fi);
  if (got < static_cast<s32>(len)) out.clear();
  else out.resize(len);
  return out;
}

uint32_t Be32(const uint8_t* p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; }
uint16_t Be16(const uint8_t* p) { return uint16_t(p[0] << 8 | p[1]); }

void Index() {
  if (sIndexed) return;
  sIndexed = true;
  for (const char* name : kArchives) {
    auto a = std::make_unique<Archive>();
    a->bytes = ReadDiscFile(name);
    if (a->bytes.empty()) continue;
    if (!Fsys_Open(a->bytes.data(), a->bytes.size(), &a->fsys)) continue;
    for (uint32_t i = 0; i < a->fsys.count; i++) {
      FsysEntry e;
      if (Fsys_Entry(&a->fsys, i, &e) && e.type == 9 && a->textures.find(e.id) == a->textures.end()) a->textures[e.id] = i;
    }
    a->ok = true;
    sArchives.push_back(std::move(a));
  }
  std::fprintf(stderr, "[kit2] %zu menu archives indexed from the disc\n", sArchives.size());
}

int GxFormat(int gs) {
  switch (gs) {
  case 0x00: return 8;
  case 0x01: return 9;
  case 0x30: return 10;
  case 0x40: return 0;
  case 0x41: return 2;
  case 0x42: return 1;
  case 0x43: return 3;
  case 0x44: return 4;
  case 0x45: return 6;
  case 0x90: return 5;
  case 0xB0: return 14;
  default: return -1;
  }
}

Image Decode(const uint8_t* t, size_t size) {
  Image r;
  if (size < 0x80) return r;
  const int w = Be16(t), h = Be16(t + 2);
  const int gs = static_cast<int32_t>(Be32(t + 8));
  const uint32_t tlutFmt = Be32(t + 0x0C), mip0 = Be32(t + 0x28), tlut = Be32(t + 0x48);
  const int fmt = GxFormat(gs);
  if (w <= 0 || h <= 0 || fmt < 0 || mip0 == 0 || mip0 >= size) return r;
  const uint32_t gxTlut = tlutFmt == 2 ? 1 : tlutFmt == 3 ? 2 : 0;
  std::vector<uint8_t> rgba(size_t(w) * h * 4);
  if (!PortGX_DecodeTexture(t + mip0, uint32_t(w), uint32_t(h), uint32_t(fmt), tlut != 0 && tlut < size ? t + tlut : nullptr, gxTlut,
                            rgba.data())) {
    return r;
  }
  r.id = aurora_imgui_add_texture(uint32_t(w), uint32_t(h), rgba.data());
  r.w = w, r.h = h, r.ok = true;
  return r;
}

}

std::vector<uint8_t> DiscFile(const char* name) { return ReadDiscFile(name); }

const Image& Texture(uint32_t resId) {
  const auto it = sImages.find(resId);
  if (it != sImages.end()) return it->second;
  Index();
  Image img;
  for (auto& a : sArchives) {
    const auto f = a->textures.find(resId);
    if (f == a->textures.end()) continue;
    uint32_t size = 0;
    if (uint8_t* data = Fsys_Extract(&a->fsys, f->second, &size)) {
      img = Decode(data, size);
      std::free(data);
    }
    break;
  }
  if (!img.ok) {
    if (const char* list = std::getenv("XD_KIT2_LIST")) {
      std::string names;
      if (FILE* lf = std::fopen(list, "rb")) {
        char buf[4096];
        size_t n;
        while ((n = std::fread(buf, 1, sizeof buf, lf)) > 0) names.append(buf, n);
        std::fclose(lf);
      }
      size_t pos = 0;
      while (pos < names.size()) {
        size_t e = names.find('\n', pos);
        if (e == std::string::npos) e = names.size();
        std::string nm = names.substr(pos, e - pos);
        pos = e + 1;
        while (!nm.empty() && (nm.back() == '\r' || nm.back() == ' ')) nm.pop_back();
        if (nm.size() < 6 || nm.compare(nm.size() - 5, 5, ".fsys") != 0) continue;
        Archive probe;
        probe.bytes = ReadDiscFile(nm.c_str());
        if (probe.bytes.empty() || !Fsys_Open(probe.bytes.data(), probe.bytes.size(), &probe.fsys)) continue;
        for (uint32_t i = 0; i < probe.fsys.count; i++) {
          FsysEntry fe;
          if (Fsys_Entry(&probe.fsys, i, &fe) && fe.id == resId) {
            std::fprintf(stderr, "[kit2] id 0x%X is in %s (type %u)\n", resId, nm.c_str(), unsigned(fe.type));
            break;
          }
        }
      }
    }
    std::fprintf(stderr, "[kit2] texture 0x%X not found on the disc\n", resId);
  }
  return sImages.emplace(resId, img).first->second;
}

bool Loaded(uint32_t resId) {
  const auto it = sImages.find(resId);
  return it != sImages.end() && it->second.ok;
}

bool DrawPiece(ImDrawList* dl, ImVec2 a, ImVec2 b, const Piece& p, bool flipX, bool flipY, ImU32 col) {
  const Image& img = Texture(p.tex);
  if (!img.ok || p.w <= 0 || p.h <= 0) return false;
  float s0 = (float(p.u) + 0.5f) / img.w, s1 = (float(p.u + p.w) - 0.5f) / img.w;
  float t0 = (float(p.v) + 0.5f) / img.h, t1 = (float(p.v + p.h) - 0.5f) / img.h;
  if (flipX) std::swap(s0, s1);
  if (flipY) std::swap(t0, t1);
  dl->AddImage(img.id, a, b, ImVec2(s0, t0), ImVec2(s1, t1), col);
  return true;
}

void Reset() {
  sImages.clear();
  sArchives.clear();
  sIndexed = false;
}
int Count() { return static_cast<int>(sImages.size()); }

}
