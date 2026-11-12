#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <unistd.h>

#include <elf.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

extern "C" void (*PortInput_SetFrameHook(void (*hook)(uint32_t frame)))(uint32_t frame);
extern "C" uint32_t* __hsd_rand_seed_hold;
extern void* MEM1Start;

namespace {

struct Section {
  const uint8_t* data = nullptr;
  size_t size = 0;
};

struct Abbrev {
  uint32_t tag = 0;
  bool children = false;
  struct Attr {
    uint32_t name, form;
    int64_t implicit;
  };
  std::vector<Attr> attrs;
};
using AbbrevTable = std::unordered_map<uint64_t, Abbrev>;

struct Unit {
  uint64_t start = 0, end = 0, firstDie = 0;
  uint16_t version = 0;
  uint8_t addressSize = 8;
  const AbbrevTable* abbrevs = nullptr;
  uint64_t strOffsetsBase = 8, addrBase = 8;
};

struct Die {
  uint64_t offset = 0, next = 0;
  uint32_t tag = 0;
  bool children = false, declaration = false, artificial = false;
  const char* name = nullptr;
  uint64_t type = 0, specification = 0, sibling = 0;
  uint64_t address = 0;
  bool hasAddress = false;
  uint64_t byteSize = 0, memberOffset = 0, count = 0;
  bool hasByteSize = false, hasCount = false, hasUpper = false;
};

enum : uint32_t {
  kTagArray = 0x01, kTagClass = 0x02, kTagParameter = 0x05, kTagMember = 0x0d, kTagPointer = 0x0f,
  kTagStruct = 0x13, kTagTypedef = 0x16, kTagUnion = 0x17, kTagSubrange = 0x21, kTagConst = 0x26,
  kTagSubprogram = 0x2e, kTagVariable = 0x34, kTagVolatile = 0x35,
};

Section sInfo, sAbbrev, sStr, sStrOffsets, sAddr, sSymtab, sStrtab;
uint64_t sWritableLo = UINT64_MAX, sWritableHi = 0;
std::vector<Unit> sUnits;
std::unordered_map<uint64_t, AbbrevTable> sAbbrevTables;
struct Named {
  uint64_t die;
  uint64_t address;
  uint32_t depth;
};
std::unordered_multimap<std::string_view, Named> sVariables, sRecords, sFunctions;
bool sLoaded = false, sUsable = false;

uint64_t uleb(const uint8_t*& p) {
  uint64_t v = 0;
  int shift = 0;
  for (;;) {
    const uint8_t b = *p++;
    if (shift < 64) v |= static_cast<uint64_t>(b & 0x7f) << shift;
    shift += 7;
    if ((b & 0x80) == 0) break;
  }
  return v;
}

int64_t sleb(const uint8_t*& p) {
  int64_t v = 0;
  int shift = 0;
  uint8_t b;
  do {
    b = *p++;
    if (shift < 64) v |= static_cast<int64_t>(b & 0x7f) << shift;
    shift += 7;
  } while ((b & 0x80) != 0);
  if (shift < 64 && (b & 0x40) != 0) v |= -(static_cast<int64_t>(1) << shift);
  return v;
}

template <typename T> T rd(const uint8_t*& p) {
  T v;
  std::memcpy(&v, p, sizeof(T));
  p += sizeof(T);
  return v;
}

const AbbrevTable* abbrev_table(uint64_t offset) {
  auto found = sAbbrevTables.find(offset);
  if (found != sAbbrevTables.end()) return &found->second;
  AbbrevTable& table = sAbbrevTables[offset];
  const uint8_t* p = sAbbrev.data + offset;
  const uint8_t* end = sAbbrev.data + sAbbrev.size;
  while (p < end) {
    const uint64_t code = uleb(p);
    if (code == 0) break;
    Abbrev a;
    a.tag = static_cast<uint32_t>(uleb(p));
    a.children = *p++ != 0;
    for (;;) {
      const uint32_t name = static_cast<uint32_t>(uleb(p));
      const uint32_t form = static_cast<uint32_t>(uleb(p));
      int64_t implicit = 0;
      if (form == 0x21) implicit = sleb(p);
      if (name == 0 && form == 0) break;
      a.attrs.push_back({name, form, implicit});
    }
    table.emplace(code, std::move(a));
  }
  return &table;
}

const Unit* unit_of(uint64_t offset) {
  size_t lo = 0, hi = sUnits.size();
  while (lo + 1 < hi) {
    const size_t mid = (lo + hi) / 2;
    if (sUnits[mid].start <= offset) lo = mid;
    else hi = mid;
  }
  return lo < sUnits.size() && offset >= sUnits[lo].start && offset < sUnits[lo].end ? &sUnits[lo] : nullptr;
}

const char* strx(const Unit& u, uint64_t index) {
  const uint64_t at = u.strOffsetsBase + index * 4;
  if (at + 4 > sStrOffsets.size) return nullptr;
  uint32_t offset;
  std::memcpy(&offset, sStrOffsets.data + at, 4);
  return offset < sStr.size ? reinterpret_cast<const char*>(sStr.data + offset) : nullptr;
}

bool addrx(const Unit& u, uint64_t index, uint64_t* out) {
  const uint64_t at = u.addrBase + index * u.addressSize;
  if (at + 8 > sAddr.size) return false;
  std::memcpy(out, sAddr.data + at, 8);
  return true;
}

bool read_die(const Unit& u, uint64_t offset, Die& d) {
  d = Die();
  d.offset = offset;
  const uint8_t* p = sInfo.data + offset;
  const uint64_t code = uleb(p);
  if (code == 0) {
    d.next = static_cast<uint64_t>(p - sInfo.data);
    return false;
  }
  const auto found = u.abbrevs->find(code);
  if (found == u.abbrevs->end()) {
    d.next = u.end;
    return false;
  }
  const Abbrev& a = found->second;
  d.tag = a.tag;
  d.children = a.children;
  for (const Abbrev::Attr& at : a.attrs) {
    uint32_t form = at.form;
    if (form == 0x16) form = static_cast<uint32_t>(uleb(p));
    uint64_t value = 0;
    const uint8_t* block = nullptr;
    uint64_t blockSize = 0;
    const char* text = nullptr;
    bool reference = false;
    switch (form) {
      case 0x01: value = u.addressSize == 8 ? rd<uint64_t>(p) : rd<uint32_t>(p); break;
      case 0x03: blockSize = rd<uint16_t>(p); block = p; p += blockSize; break;
      case 0x04: blockSize = rd<uint32_t>(p); block = p; p += blockSize; break;
      case 0x05: value = rd<uint16_t>(p); break;
      case 0x06: value = rd<uint32_t>(p); break;
      case 0x07: value = rd<uint64_t>(p); break;
      case 0x08: text = reinterpret_cast<const char*>(p); p += std::strlen(text) + 1; break;
      case 0x09: case 0x18: blockSize = uleb(p); block = p; p += blockSize; break;
      case 0x0a: blockSize = rd<uint8_t>(p); block = p; p += blockSize; break;
      case 0x0b: case 0x0c: value = rd<uint8_t>(p); break;
      case 0x0d: value = static_cast<uint64_t>(sleb(p)); break;
      case 0x0e: value = rd<uint32_t>(p); text = value < sStr.size ? reinterpret_cast<const char*>(sStr.data + value) : nullptr; break;
      case 0x0f: value = uleb(p); break;
      case 0x10: value = rd<uint32_t>(p); reference = true; break;
      case 0x11: value = u.start + rd<uint8_t>(p); reference = true; break;
      case 0x12: value = u.start + rd<uint16_t>(p); reference = true; break;
      case 0x13: value = u.start + rd<uint32_t>(p); reference = true; break;
      case 0x14: value = u.start + rd<uint64_t>(p); reference = true; break;
      case 0x15: value = u.start + uleb(p); reference = true; break;
      case 0x17: case 0x1c: case 0x1d: case 0x1f: value = rd<uint32_t>(p); break;
      case 0x19: value = 1; break;
      case 0x1a: text = strx(u, uleb(p)); break;
      case 0x1b: case 0x22: case 0x23: value = uleb(p); break;
      case 0x1e: p += 16; break;
      case 0x20: case 0x24: p += 8; break;
      case 0x21: value = static_cast<uint64_t>(at.implicit); break;
      case 0x25: text = strx(u, rd<uint8_t>(p)); break;
      case 0x26: text = strx(u, rd<uint16_t>(p)); break;
      case 0x27: value = rd<uint16_t>(p); value |= static_cast<uint64_t>(rd<uint8_t>(p)) << 16; text = strx(u, value); break;
      case 0x28: text = strx(u, rd<uint32_t>(p)); break;
      case 0x29: value = rd<uint8_t>(p); break;
      case 0x2a: value = rd<uint16_t>(p); break;
      case 0x2b: value = rd<uint16_t>(p); value |= static_cast<uint64_t>(rd<uint8_t>(p)) << 16; break;
      case 0x2c: value = rd<uint32_t>(p); break;
      default:
        std::fprintf(stderr, "[state dump] debug information: form 0x%x not known to this reader\n", form);
        d.next = u.end;
        return false;
    }
    switch (at.name) {
      case 0x01: if (reference) d.sibling = value; break;
      case 0x02:
        if (block != nullptr && blockSize >= 1) {
          const uint8_t* q = block + 1;
          if (block[0] == 0x03 && blockSize == 1u + u.addressSize) {
            d.address = u.addressSize == 8 ? rd<uint64_t>(q) : rd<uint32_t>(q);
            d.hasAddress = true;
          } else if (block[0] == 0xa1) {
            const uint64_t index = uleb(q);
            if (q == block + blockSize) d.hasAddress = addrx(u, index, &d.address);
          }
        }
        break;
      case 0x03: d.name = text; break;
      case 0x0b: d.byteSize = value; d.hasByteSize = block == nullptr; break;
      case 0x2f: d.count = value + 1; d.hasUpper = block == nullptr && value != UINT64_MAX; break;
      case 0x34: d.artificial = value != 0; break;
      case 0x37: d.count = value; d.hasCount = block == nullptr; break;
      case 0x38: d.memberOffset = block == nullptr ? value : 0; break;
      case 0x3c: d.declaration = value != 0; break;
      case 0x47: if (reference) d.specification = value; break;
      case 0x49: if (reference) d.type = value; break;
      case 0x72: break;
      default: break;
    }
  }
  d.next = static_cast<uint64_t>(p - sInfo.data);
  return true;
}

bool read_die(uint64_t offset, Die& d) {
  const Unit* u = unit_of(offset);
  return u != nullptr && read_die(*u, offset, d);
}

void unit_bases(Unit& u) {
  const uint8_t* p = sInfo.data + u.firstDie;
  const uint64_t code = uleb(p);
  const auto found = u.abbrevs->find(code);
  if (found == u.abbrevs->end()) return;
  for (const Abbrev::Attr& at : found->second.attrs) {
    uint64_t value = 0;
    switch (at.form) {
      case 0x01: p += u.addressSize; break;
      case 0x05: p += 2; break;
      case 0x06: case 0x0e: case 0x1f: p += 4; break;
      case 0x07: p += 8; break;
      case 0x08: p += std::strlen(reinterpret_cast<const char*>(p)) + 1; break;
      case 0x0b: case 0x0c: case 0x25: case 0x29: p += 1; break;
      case 0x26: case 0x2a: p += 2; break;
      case 0x27: case 0x2b: p += 3; break;
      case 0x28: case 0x2c: p += 4; break;
      case 0x0d: sleb(p); break;
      case 0x0f: case 0x1a: case 0x1b: case 0x22: case 0x23: uleb(p); break;
      case 0x17: value = rd<uint32_t>(p); break;
      case 0x19: case 0x21: break;
      case 0x1e: p += 16; break;
      default: return;
    }
    if (at.name == 0x72) u.strOffsetsBase = value;
    if (at.name == 0x73) u.addrBase = value;
  }
}

bool load_sections() {
  const int fd = open("/proc/self/exe", O_RDONLY | O_CLOEXEC);
  if (fd < 0) return false;
  struct stat st {};
  if (fstat(fd, &st) != 0) {
    close(fd);
    return false;
  }
  void* map = mmap(nullptr, static_cast<size_t>(st.st_size), PROT_READ, MAP_PRIVATE, fd, 0);
  close(fd);
  if (map == MAP_FAILED) return false;
  const auto* file = static_cast<const uint8_t*>(map);
  const auto* eh = reinterpret_cast<const Elf64_Ehdr*>(file);
  if (std::memcmp(eh->e_ident, ELFMAG, SELFMAG) != 0 || eh->e_ident[EI_CLASS] != ELFCLASS64) return false;
  const auto* sh = reinterpret_cast<const Elf64_Shdr*>(file + eh->e_shoff);
  const char* names = reinterpret_cast<const char*>(file + sh[eh->e_shstrndx].sh_offset);
  for (unsigned i = 0; i < eh->e_shnum; i++) {
    const std::string_view name(names + sh[i].sh_name);
    if ((sh[i].sh_flags & SHF_ALLOC) != 0 && (sh[i].sh_flags & SHF_WRITE) != 0 && (sh[i].sh_flags & SHF_TLS) == 0) {
      sWritableLo = std::min<uint64_t>(sWritableLo, sh[i].sh_addr);
      sWritableHi = std::max<uint64_t>(sWritableHi, sh[i].sh_addr + sh[i].sh_size);
    }
    Section* into = name == ".debug_info"          ? &sInfo
                    : name == ".debug_abbrev"      ? &sAbbrev
                    : name == ".debug_str"         ? &sStr
                    : name == ".debug_str_offsets" ? &sStrOffsets
                    : name == ".debug_addr"        ? &sAddr
                    : name == ".symtab"            ? &sSymtab
                    : name == ".strtab"            ? &sStrtab
                                                   : nullptr;
    if (into == nullptr) continue;
    if ((sh[i].sh_flags & SHF_COMPRESSED) != 0) {
      std::fprintf(stderr, "[state dump] %s is compressed: not read by this reader\n", names + sh[i].sh_name);
      continue;
    }
    into->data = file + sh[i].sh_offset;
    into->size = sh[i].sh_size;
  }
  return true;
}

void load() {
  if (sLoaded) return;
  sLoaded = true;
  if (!load_sections() || sInfo.data == nullptr || sAbbrev.data == nullptr) {
    std::fprintf(stderr, "[state dump] this program has no debug information (.debug_info): names can't be "
                         "found (a build with -g, not stripped, has it)\n");
    return;
  }
  uint64_t at = 0;
  while (at + 11 < sInfo.size) {
    const uint8_t* p = sInfo.data + at;
    const uint32_t length = rd<uint32_t>(p);
    if (length == 0xffffffffu) {
      std::fprintf(stderr, "[state dump] 64-bit debug information: not read by this reader\n");
      return;
    }
    Unit u;
    u.start = at;
    u.end = at + 4 + length;
    u.version = rd<uint16_t>(p);
    uint32_t abbrevOffset;
    bool compile = true;
    if (u.version >= 5) {
      const uint8_t type = rd<uint8_t>(p);
      u.addressSize = rd<uint8_t>(p);
      abbrevOffset = rd<uint32_t>(p);
      compile = type == 0x01 || type == 0x03;
    } else {
      abbrevOffset = rd<uint32_t>(p);
      u.addressSize = rd<uint8_t>(p);
    }
    if (compile && u.version >= 2 && u.version <= 5) {
      u.firstDie = static_cast<uint64_t>(p - sInfo.data);
      u.abbrevs = abbrev_table(abbrevOffset);
      unit_bases(u);
      sUnits.push_back(u);
    }
    at = u.end;
  }
  for (const Unit& u : sUnits) {
    uint32_t depth = 0;
    uint64_t offset = u.firstDie;
    Die d;
    while (offset < u.end) {
      if (!read_die(u, offset, d)) {
        if (d.next >= u.end) break;
        offset = d.next;
        if (depth > 0) depth--;
        continue;
      }
      if (d.tag == kTagVariable && d.hasAddress) {
        const char* name = d.name;
        if (name == nullptr && d.specification != 0) {
          Die spec;
          if (read_die(d.specification, spec)) name = spec.name;
        }
        if (name != nullptr) sVariables.emplace(name, Named{d.offset, d.address, depth});
      } else if ((d.tag == kTagStruct || d.tag == kTagClass || d.tag == kTagUnion) && d.name != nullptr &&
                 !d.declaration) {
        sRecords.emplace(d.name, Named{d.offset, 0, depth});
      } else if (d.tag == kTagSubprogram && d.name != nullptr) {
        sFunctions.emplace(d.name, Named{d.offset, 0, depth});
      }
      if (d.children) depth++;
      offset = d.next;
    }
  }
  sUsable = true;
  if (std::getenv("XD_STATE_DEBUG") != nullptr) {
    std::fprintf(stderr, "[state dump]   debug information: %zu units, %zu variables, %zu records, %zu functions\n",
                 sUnits.size(), sVariables.size(), sRecords.size(), sFunctions.size());
  }
}

uint64_t after(const Die& d) {
  if (!d.children) return d.next;
  if (d.sibling != 0) return d.sibling;
  uint64_t offset = d.next;
  Die child;
  for (;;) {
    const Unit* u = unit_of(offset);
    if (u == nullptr) return 0;
    if (!read_die(*u, offset, child)) return child.next >= u->end ? 0 : child.next;
    offset = after(child);
    if (offset == 0) return 0;
  }
}

std::vector<Die> children_of(const Die& parent) {
  std::vector<Die> out;
  if (!parent.children) return out;
  uint64_t offset = parent.next;
  Die child;
  while (offset != 0 && read_die(offset, child)) {
    out.push_back(child);
    offset = after(child);
  }
  return out;
}

struct Type {
  uint64_t die = 0;
  uint32_t dimension = 0;
};

bool plain(Type t, Die& d) {
  for (int guard = 0; guard < 16; guard++) {
    if (t.die == 0 || !read_die(t.die, d)) return false;
    if (d.tag != kTagTypedef && d.tag != kTagConst && d.tag != kTagVolatile) return true;
    t = Type{d.type, 0};
  }
  return false;
}

std::vector<uint64_t> dimensions(const Die& array) {
  std::vector<uint64_t> out;
  for (const Die& c : children_of(array)) {
    if (c.tag == kTagSubrange) out.push_back(c.hasCount || c.hasUpper ? c.count : 0);
  }
  return out;
}

uint64_t size_of(Type t) {
  Die d;
  if (!plain(t, d)) return 0;
  if (d.tag == kTagPointer) return 8;
  if (d.tag == kTagArray) {
    uint64_t n = size_of(Type{d.type, 0});
    const std::vector<uint64_t> dims = dimensions(d);
    for (size_t i = t.dimension; i < dims.size(); i++) n *= dims[i];
    return n;
  }
  return d.hasByteSize ? d.byteSize : 0;
}

struct Member {
  uint64_t offset;
  Type type;
};

void find_members(const Die& record, const std::string& name, uint64_t base, std::vector<Member>& out, int depth = 0) {
  for (const Die& m : children_of(record)) {
    if (m.tag != kTagMember) continue;
    if (m.name != nullptr && name == m.name) {
      out.push_back({base + m.memberOffset, Type{m.type, 0}});
    } else if (m.name == nullptr && depth < 4) {
      Die inner;
      if (plain(Type{m.type, 0}, inner) && (inner.tag == kTagStruct || inner.tag == kTagUnion || inner.tag == kTagClass)) {
        find_members(inner, name, base + m.memberOffset, out, depth + 1);
      }
    }
  }
}

size_t member_count(const Die& record) {
  size_t n = 0;
  for (const Die& m : children_of(record)) n += m.tag == kTagMember ? 1 : 0;
  return n;
}

std::vector<uint64_t> records_named(const std::string& name, uint64_t except) {
  std::vector<std::pair<size_t, uint64_t>> found;
  std::set<std::pair<size_t, uint64_t>> shapes;
  const auto range = sRecords.equal_range(name);
  std::vector<uint64_t> offsets;
  for (auto it = range.first; it != range.second; ++it) offsets.push_back(it->second.die);
  std::sort(offsets.begin(), offsets.end());
  for (const uint64_t offset : offsets) {
    if (offset == except) continue;
    Die d;
    if (!read_die(offset, d)) continue;
    const size_t members = member_count(d);
    if (members == 0) continue;
    if (!shapes.insert({members, d.byteSize}).second) continue;
    found.push_back({members, offset});
  }
  std::stable_sort(found.begin(), found.end(), [](auto& a, auto& b) { return a.first > b.first; });
  std::vector<uint64_t> out;
  for (auto& f : found) out.push_back(f.second);
  return out;
}

std::vector<Die> candidates(Type type) {
  std::vector<Die> out;
  Die d;
  if (!plain(type, d)) return out;
  out.push_back(d);
  if ((d.tag == kTagStruct || d.tag == kTagClass || d.tag == kTagUnion) && d.name != nullptr) {
    for (const uint64_t other : records_named(d.name, d.offset)) {
      Die o;
      if (read_die(other, o)) out.push_back(o);
    }
  }
  return out;
}

bool resolve_from(const std::string& s, size_t i, Type type, std::vector<uintptr_t>& offsets, std::string& error) {
  if (i == std::string::npos || i >= s.size()) return true;
  if (s[i] == '@') {
    const size_t next = s.find_first_of(".[-@", i + 1);
    const std::string name = s.substr(i + 1, next == std::string::npos ? std::string::npos : next - i - 1);
    std::vector<Type> list;
    if (name.size() > 2 && name.front() == '(' && name.back() == ')') {
      const auto range = sFunctions.equal_range(name.substr(1, name.size() - 2));
      std::vector<uint64_t> functions;
      for (auto it = range.first; it != range.second; ++it) functions.push_back(it->second.die);
      std::sort(functions.begin(), functions.end());
      std::set<uint64_t> seen;
      for (const uint64_t f : functions) {
        Die fn;
        if (!read_die(f, fn)) continue;
        for (const Die& p : children_of(fn)) {
          if (p.tag != kTagParameter || p.artificial) continue;
          Die pointer;
          if (plain(Type{p.type, 0}, pointer) && pointer.tag == kTagPointer && pointer.type != 0 &&
              seen.insert(pointer.type).second) {
            list.push_back(Type{pointer.type, 0});
          }
          break;
        }
      }
    } else {
      for (const uint64_t r : records_named(name, 0)) list.push_back(Type{r, 0});
    }
    if (list.empty()) {
      error = "type " + name + " not found";
      return false;
    }
    for (const Type t : list) {
      for (const Die& candidate : candidates(t)) {
        if (resolve_from(s, next, Type{candidate.offset, 0}, offsets, error)) return true;
      }
    }
    return false;
  }
  if (s[i] == '[') {
    const size_t close = s.find(']', i);
    const uint32_t index = static_cast<uint32_t>(std::strtoul(s.c_str() + i + 1, nullptr, 0));
    Die d;
    if (!plain(type, d) || d.tag != kTagArray) {
      error = "[" + std::to_string(index) + "] on something that isn't an array";
      return false;
    }
    const std::vector<uint64_t> dims = dimensions(d);
    const Type element = type.dimension + 1 < dims.size() ? Type{d.offset, type.dimension + 1} : Type{d.type, 0};
    const uint64_t length = size_of(element);
    if (length == 0) {
      error = "[" + std::to_string(index) + "]: the element's size is not known";
      return false;
    }
    offsets.back() += static_cast<uintptr_t>(index * length);
    if (resolve_from(s, close == std::string::npos ? close : close + 1, element, offsets, error)) return true;
    offsets.back() -= static_cast<uintptr_t>(index * length);
    return false;
  }
  const bool arrow = s.compare(i, 2, "->") == 0;
  const size_t from = i + (arrow ? 2 : 1);
  const size_t next = s.find_first_of(".[-@", from);
  const std::string name = s.substr(from, next == std::string::npos ? std::string::npos : next - from);
  if (arrow) {
    Die d;
    if (!plain(type, d) || d.tag != kTagPointer) {
      error = "->" + name + " on something that isn't a pointer";
      return false;
    }
    type = Type{d.type, 0};
    offsets.push_back(0);
    if (!name.empty() && name[0] == '+') {
      offsets.back() += static_cast<uintptr_t>(std::strtoul(name.c_str() + 1, nullptr, 0));
      if (resolve_from(s, next, type, offsets, error)) return true;
      offsets.pop_back();
      return false;
    }
    if (name.empty()) {
      if (resolve_from(s, next, type, offsets, error)) return true;
      offsets.pop_back();
      return false;
    }
  }
  bool found = false;
  for (const Die& candidate : candidates(type)) {
    std::vector<Member> members;
    find_members(candidate, name, 0, members);
    for (const Member& m : members) {
      found = true;
      offsets.back() += static_cast<uintptr_t>(m.offset);
      if (resolve_from(s, next, m.type, offsets, error)) return true;
      offsets.back() -= static_cast<uintptr_t>(m.offset);
    }
  }
  if (!found) error = "member " + name + " not found";
  if (arrow) offsets.pop_back();
  return false;
}

uint64_t symbol_address(const std::string& name) {
  if (sSymtab.data == nullptr || sStrtab.data == nullptr) return 0;
  const auto* symbols = reinterpret_cast<const Elf64_Sym*>(sSymtab.data);
  const size_t count = sSymtab.size / sizeof(Elf64_Sym);
  for (size_t i = 0; i < count; i++) {
    if (ELF64_ST_TYPE(symbols[i].st_info) != STT_OBJECT || symbols[i].st_name >= sStrtab.size) continue;
    if (name == reinterpret_cast<const char*>(sStrtab.data) + symbols[i].st_name) return symbols[i].st_value;
  }
  return 0;
}

struct Probe {
  std::string label, path, type;
  uint32_t count = 1;
  std::vector<uintptr_t> offsets;
  uint32_t size = 4;
  std::string fields;
  const uint8_t* address = nullptr;
};

std::vector<Probe> sProbes;
std::set<uint32_t> sFrames;
uint32_t sRangeFrom = 0, sRangeTo = 0, sRangeStep = 0;
FILE* sOut = nullptr;
uint32_t sSeedValue = 0, sSeedFrame = 0;
bool sSeedPending = false;

constexpr uint32_t kMem1Size = 0x1800000;

uint32_t fnv1a(const uint8_t* data, uint32_t size) {
  uint32_t h = 2166136261u;
  for (uint32_t i = 0; i < size; i++) h = (h ^ data[i]) * 16777619u;
  return h;
}

std::string expand_fields(const std::string& f) {
  std::string out;
  for (size_t i = 0; i < f.size();) {
    const char c = f[i++];
    size_t n = 0, digits = 0;
    while (i < f.size() && f[i] >= '0' && f[i] <= '9') n = n * 10 + static_cast<size_t>(f[i++] - '0'), digits++;
    out.append(digits != 0 ? n : 1, c);
  }
  return out;
}

uint32_t type_size(const std::string& t) {
  if (t.rfind("rec:", 0) == 0) {
    uint32_t n = 0;
    for (char c : t.substr(4)) n += c == 'H' ? 2 : c == 'I' ? 4 : 1;
    return n;
  }
  if (t == "u8" || t == "s8") return 1;
  if (t == "u16" || t == "s16") return 2;
  return 4;
}

void resolve(Probe& p, bool quiet) {
  if (p.path == "@mem1") {
    p.type = "hash";
    p.count = p.count > 1 ? p.count : 96;
    p.size = kMem1Size / p.count;
    p.address = reinterpret_cast<const uint8_t*>(1);
    return;
  }
  load();
  if (p.path == "@data") {
    if (sWritableLo < sWritableHi) {
      p.type = "hash";
      p.size = 0x10000;
      p.count = static_cast<uint32_t>((sWritableHi - sWritableLo) / p.size);
      p.address = reinterpret_cast<const uint8_t*>(sWritableLo);
      std::fprintf(stderr, "[state dump] %s: writable sections at 0x%llx..0x%llx, %u blocks of 64 KB\n",
                   p.label.c_str(), static_cast<unsigned long long>(sWritableLo),
                   static_cast<unsigned long long>(sWritableHi), p.count);
      if (const char* rvas = std::getenv("XD_STATE_NAME_RVAS"); rvas != nullptr && rvas[0] != '\0') {
        std::fprintf(stderr, "[state dump] XD_STATE_NAME_RVAS: not on Linux yet\n");
      }
    }
    return;
  }
  const std::string& s = p.path;
  const size_t i = s.find_first_of(".[-@");
  const std::string symbol = s.substr(0, i);
  std::string error = "symbol " + symbol + " not found";
  if (sUsable) {
    std::vector<Named> globals;
    const auto range = sVariables.equal_range(symbol);
    for (auto it = range.first; it != range.second; ++it) globals.push_back(it->second);
    std::sort(globals.begin(), globals.end(), [](const Named& a, const Named& b) {
      return a.depth != b.depth ? a.depth < b.depth : a.address != b.address ? a.address < b.address : a.die < b.die;
    });
    for (const Named& g : globals) {
      Die variable;
      if (!read_die(g.die, variable)) continue;
      uint64_t type = variable.type;
      if (type == 0 && variable.specification != 0) {
        Die spec;
        if (read_die(variable.specification, spec)) type = spec.type;
      }
      std::vector<uintptr_t> offsets{static_cast<uintptr_t>(g.address)};
      if (resolve_from(s, i, Type{type, 0}, offsets, error)) {
        p.offsets = offsets;
        if (!quiet) {
          std::fprintf(stderr, "[state dump] %s:", p.label.c_str());
          for (size_t k = 0; k < offsets.size(); k++) {
            std::fprintf(stderr, k == 0 ? " +0x%llx" : " -> +0x%llx",
                         static_cast<unsigned long long>(k == 0 ? offsets[0] - g.address : offsets[k]));
          }
          std::fprintf(stderr, "\n");
        }
        return;
      }
    }
  }
  if (i == std::string::npos) {
    if (const uint64_t address = symbol_address(symbol); address != 0) {
      p.offsets = {static_cast<uintptr_t>(address)};
      if (!quiet) std::fprintf(stderr, "[state dump] %s: +0x0 (symbol table)\n", p.label.c_str());
      return;
    }
  }
  std::fprintf(stderr, "[state dump] %s: %s\n", p.label.c_str(), error.c_str());
}

bool safe_copy(void* to, uintptr_t from, size_t n) {
  iovec local{to, n};
  iovec remote{reinterpret_cast<void*>(from), n};
  return process_vm_readv(getpid(), &local, 1, &remote, 1, 0) == static_cast<ssize_t>(n);
}

const uint8_t* locate(const Probe& p) {
  if (p.offsets.empty()) return nullptr;
  uintptr_t a = p.offsets[0];
  for (size_t i = 1; i < p.offsets.size(); i++) {
    uintptr_t next = 0;
    if (!safe_copy(&next, a, sizeof(next)) || next == 0) return nullptr;
    a = next + p.offsets[i];
  }
  uint8_t probe = 0;
  if (!safe_copy(&probe, a, 1)) return nullptr;
  return reinterpret_cast<const uint8_t*>(a);
}

bool wanted(uint32_t frame) {
  if (sFrames.count(frame) != 0) return true;
  return sRangeStep != 0 && frame >= sRangeFrom && frame <= sRangeTo && (frame - sRangeFrom) % sRangeStep == 0;
}

void on_frame(uint32_t frame) {
  if (sSeedPending && frame >= sSeedFrame && __hsd_rand_seed_hold != nullptr) {
    *__hsd_rand_seed_hold = sSeedValue;
    sSeedPending = false;
    std::fprintf(stderr, "[state dump] game frame %u: random-number state set to 0x%08x (XD_RNG_SEED)\n", frame,
                 sSeedValue);
  }
  if (sProbes.empty() || !wanted(frame)) return;
  std::fprintf(sOut, "frame %u\n", frame);
  for (const Probe& p : sProbes) {
    std::fprintf(sOut, "%s", p.label.c_str());
    const uint8_t* address = p.path == "@mem1"   ? static_cast<const uint8_t*>(MEM1Start)
                             : p.path == "@data" ? p.address
                                                 : locate(p);
    if (address == nullptr) {
      std::fprintf(sOut, " ?\n");
      continue;
    }
    for (uint32_t i = 0; i < p.count; i++) {
      const uint8_t* a = address + i * p.size;
      if (!p.fields.empty()) {
        for (char c : p.fields) {
          uint32_t v = 0;
          const uint32_t n = c == 'H' ? 2 : c == 'I' ? 4 : 1;
          std::memcpy(&v, a, n);
          a += n;
          if (c != 'x') std::fprintf(sOut, " %x", v);
        }
        continue;
      }
      uint32_t v = 0;
      if (p.type == "hash") v = fnv1a(a, p.size);
      else if (p.size == 1) v = *a;
      else if (p.size == 2) std::memcpy(&v, a, 2);
      else std::memcpy(&v, a, 4);
      std::fprintf(sOut, " %x", v);
    }
    std::fprintf(sOut, "\n");
    const char* raw = std::getenv("XD_STATE_RAW_DIR");
    if (p.type == "hash" && raw != nullptr && raw[0] != '\0') {
      const std::string path = std::string(raw) + "/" + p.label + "_" + std::to_string(frame) + ".bin";
      if (FILE* f = std::fopen(path.c_str(), "wb")) {
        std::fwrite(address, 1, static_cast<size_t>(p.size) * p.count, f);
        std::fclose(f);
      }
    }
  }
  std::fflush(sOut);
}

void parse_frames(const char* spec) {
  const std::string s(spec);
  const size_t dash = s.find('-');
  if (dash != std::string::npos && s.find(',') == std::string::npos) {
    sRangeFrom = static_cast<uint32_t>(std::strtoul(s.c_str(), nullptr, 10));
    sRangeTo = static_cast<uint32_t>(std::strtoul(s.c_str() + dash + 1, nullptr, 10));
    const size_t slash = s.find('/');
    sRangeStep = slash != std::string::npos ? static_cast<uint32_t>(std::strtoul(s.c_str() + slash + 1, nullptr, 10)) : 1;
    if (sRangeStep == 0) sRangeStep = 1;
    return;
  }
  size_t pos = 0;
  while (pos < s.size()) {
    sFrames.insert(static_cast<uint32_t>(std::strtoul(s.c_str() + pos, nullptr, 10)));
    const size_t comma = s.find(',', pos);
    if (comma == std::string::npos) break;
    pos = comma + 1;
  }
}

}

extern "C" void PortXD_StateDumpStart(void) {
  const char* seed = std::getenv("XD_RNG_SEED");
  if (seed != nullptr && seed[0] != '\0') {
    sSeedValue = static_cast<uint32_t>(std::strtoul(seed, nullptr, 16));
    const char* at = std::strchr(seed, '@');
    sSeedFrame = at != nullptr ? static_cast<uint32_t>(std::strtoul(at + 1, nullptr, 10)) : 1;
    sSeedPending = true;
  }
  const char* probes = std::getenv("XD_STATE_PROBES");
  if (probes != nullptr && probes[0] != '\0') {
    FILE* f = std::fopen(probes, "r");
    if (f == nullptr) {
      std::fprintf(stderr, "[state dump] can't read %s\n", probes);
    } else {
      char line[2048];
      while (std::fgets(line, sizeof(line), f) != nullptr) {
        char label[128], path[512], type[512];
        if (line[0] == '#' || std::sscanf(line, "%127s %511s %511s", label, path, type) != 3) continue;
        Probe p;
        p.label = label;
        p.path = path;
        p.type = type;
        if (p.type.rfind("rec:", 0) == 0) p.type = "rec:" + expand_fields(p.type.substr(4));
        p.size = type_size(p.type);
        if (p.type.rfind("rec:", 0) == 0) p.fields = p.type.substr(4);
        char rest[256] = "";
        int n = 0;
        if (std::sscanf(line, "%*s %*s %*s %n", &n) >= 0 && n > 0) {
          std::strncpy(rest, line + n, sizeof(rest) - 1);
        }
        unsigned count = 0;
        if (std::sscanf(rest, "%u", &count) == 1 && count > 0) p.count = count;
        resolve(p, false);
        sProbes.push_back(p);
      }
      std::fclose(f);
      const char* out = std::getenv("XD_STATE_OUT");
      sOut = out != nullptr && out[0] != '\0' ? std::fopen(out, "w") : stdout;
      if (sOut == nullptr) sOut = stdout;
      const char* frames = std::getenv("XD_STATE_FRAMES");
      parse_frames(frames != nullptr ? frames : "");
      std::fprintf(stderr, "[state dump] %zu probes from %s\n", sProbes.size(), probes);
    }
  }
  if (!sProbes.empty() || sSeedPending) {
    PortInput_SetFrameHook(on_frame);
  }
}

extern "C" const void* PortXD_FindVariable(const char* path) {
  Probe p;
  p.label = path;
  p.path = path;
  p.type = "u8";
  p.size = 1;
  resolve(p, false);
  return locate(p);
}
