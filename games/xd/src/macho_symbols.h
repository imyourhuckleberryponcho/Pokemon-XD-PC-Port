#ifndef XD_MACHO_SYMBOLS_H
#define XD_MACHO_SYMBOLS_H
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

namespace xd_macho {

struct Header64 {
  uint32_t magic, cputype, cpusubtype, filetype, ncmds, sizeofcmds, flags, reserved;
};
struct LoadCommand {
  uint32_t cmd, cmdsize;
};
struct Segment64 {
  uint32_t cmd, cmdsize;
  char segname[16];
  uint64_t vmaddr, vmsize, fileoff, filesize;
  uint32_t maxprot, initprot, nsects, flags;
};
struct Symtab {
  uint32_t cmd, cmdsize, symoff, nsyms, stroff, strsize;
};
struct Nlist64 {
  uint32_t n_strx;
  uint8_t n_type, n_sect;
  uint16_t n_desc;
  uint64_t n_value;
};
static_assert(sizeof(Header64) == 32 && sizeof(Segment64) == 72 && sizeof(Symtab) == 24 && sizeof(Nlist64) == 16,
              "Mach-O structures");

constexpr uint32_t kMagic64 = 0xFEEDFACFu;
constexpr uint32_t kSegment64 = 0x19, kSymtabCmd = 0x2;
constexpr uint8_t kStabMask = 0xE0, kTypeMask = 0x0E, kTypeSect = 0x0E;

struct Table {
  const Nlist64* symbols = nullptr;
  uint32_t count = 0;
  const char* strings = nullptr;
  uint32_t stringsSize = 0;
};

template <typename F> inline bool for_each_command(const Header64* h, size_t limit, F each) {
  if (h == nullptr || h->magic != kMagic64) return false;
  if (limit != 0 && (limit < sizeof(Header64) || h->sizeofcmds > limit - sizeof(Header64))) return false;
  const uint8_t* p = reinterpret_cast<const uint8_t*>(h + 1);
  const uint8_t* end = p + h->sizeofcmds;
  for (uint32_t i = 0; i < h->ncmds; ++i) {
    if (static_cast<size_t>(end - p) < sizeof(LoadCommand)) return false;
    LoadCommand c;
    std::memcpy(&c, p, sizeof c);
    if (c.cmdsize < sizeof(LoadCommand) || c.cmdsize > static_cast<size_t>(end - p)) return false;
    if (!each(c, p)) break;
    p += c.cmdsize;
  }
  return true;
}

inline bool table_in_file(const uint8_t* data, size_t size, Table& out) {
  out = Table();
  if (data == nullptr || size < sizeof(Header64)) return false;
  Symtab st{};
  bool have = false;
  if (!for_each_command(reinterpret_cast<const Header64*>(data), size, [&](const LoadCommand& c, const uint8_t* p) {
        if (c.cmd == kSymtabCmd && c.cmdsize >= sizeof(Symtab)) {
          std::memcpy(&st, p, sizeof st);
          have = true;
          return false;
        }
        return true;
      }) || !have) {
    return false;
  }
  if (st.symoff > size || st.nsyms > (size - st.symoff) / sizeof(Nlist64)) return false;
  if (st.stroff > size || st.strsize > size - st.stroff) return false;
  out.symbols = reinterpret_cast<const Nlist64*>(data + st.symoff);
  out.count = st.nsyms;
  out.strings = reinterpret_cast<const char*>(data + st.stroff);
  out.stringsSize = st.strsize;
  return true;
}

inline bool table_in_memory(const void* header, intptr_t slide, Table& out) {
  out = Table();
  Segment64 linkedit{};
  Symtab st{};
  bool haveSeg = false, haveTab = false;
  if (!for_each_command(static_cast<const Header64*>(header), 0, [&](const LoadCommand& c, const uint8_t* p) {
        if (c.cmd == kSegment64 && c.cmdsize >= sizeof(Segment64)) {
          Segment64 s;
          std::memcpy(&s, p, sizeof s);
          if (std::strncmp(s.segname, "__LINKEDIT", sizeof s.segname) == 0) {
            linkedit = s;
            haveSeg = true;
          }
        } else if (c.cmd == kSymtabCmd && c.cmdsize >= sizeof(Symtab)) {
          std::memcpy(&st, p, sizeof st);
          haveTab = true;
        }
        return true;
      }) || !haveSeg || !haveTab) {
    return false;
  }
  const uint64_t symEnd = static_cast<uint64_t>(st.symoff) + static_cast<uint64_t>(st.nsyms) * sizeof(Nlist64);
  const uint64_t strEnd = static_cast<uint64_t>(st.stroff) + st.strsize;
  const uint64_t segEnd = linkedit.fileoff + linkedit.filesize;
  if (st.symoff < linkedit.fileoff || symEnd > segEnd || st.stroff < linkedit.fileoff || strEnd > segEnd) return false;
  const char* base = reinterpret_cast<const char*>(static_cast<uintptr_t>(linkedit.vmaddr) + slide) - linkedit.fileoff;
  out.symbols = reinterpret_cast<const Nlist64*>(base + st.symoff);
  out.count = st.nsyms;
  out.strings = base + st.stroff;
  out.stringsSize = st.strsize;
  return true;
}

inline int find_variable(const Table& t, const std::string& name, uint64_t& value) {
  if (t.symbols == nullptr) return 0;
  const std::string plain = "_" + name;
  const std::string fileStatic = "__ZL" + std::to_string(name.size()) + name;
  int matches = 0;
  for (uint32_t i = 0; i < t.count; ++i) {
    Nlist64 s;
    std::memcpy(&s, &t.symbols[i], sizeof s);
    if ((s.n_type & kStabMask) != 0 || (s.n_type & kTypeMask) != kTypeSect) continue;
    if (s.n_strx >= t.stringsSize) continue;
    const char* text = t.strings + s.n_strx;
    const size_t room = t.stringsSize - s.n_strx;
    const size_t length = strnlen(text, room);
    if (length == room) continue;
    if (!(length == plain.size() && plain.compare(0, length, text, length) == 0) &&
        !(length == fileStatic.size() && fileStatic.compare(0, length, text, length) == 0)) {
      continue;
    }
    if (matches++ == 0) value = s.n_value;
  }
  return matches;
}

}
#endif
