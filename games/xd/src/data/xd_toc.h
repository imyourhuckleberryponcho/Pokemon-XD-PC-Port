#ifndef XD_TOC_H
#define XD_TOC_H

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace xd::toc {

inline uint32_t Be32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) << 24 | static_cast<uint32_t>(p[1]) << 16 | static_cast<uint32_t>(p[2]) << 8 | p[3];
}
inline void PutBe32(uint8_t* p, uint32_t v) {
  p[0] = static_cast<uint8_t>(v >> 24), p[1] = static_cast<uint8_t>(v >> 16), p[2] = static_cast<uint8_t>(v >> 8), p[3] = static_cast<uint8_t>(v);
}

struct Toc {
  std::vector<uint8_t> header, names, groups, trailer;
  uint32_t count = 0;
};

inline bool Read(const std::vector<uint8_t>& d, Toc& t) {
  if (d.size() < 0x20) return false;
  t.count = Be32(d.data() + 8);
  const uint32_t go = Be32(d.data() + 0x10);
  if (go < 0x20 || go > d.size() || static_cast<uint64_t>(go) + 16ull * t.count > d.size()) return false;
  t.header.assign(d.begin(), d.begin() + 0x20);
  t.names.assign(d.begin() + 0x20, d.begin() + go);
  t.groups.assign(d.begin() + go, d.begin() + go + 16 * t.count);
  t.trailer.assign(d.begin() + go + 16 * t.count, d.end());
  return true;
}

inline std::string Name(const std::vector<uint8_t>& d, const Toc& t, uint32_t id, uint32_t* row = nullptr) {
  for (uint32_t i = 0; i < t.count; i++) {
    const uint8_t* g = t.groups.data() + i * 16;
    if (Be32(g) != id) continue;
    const uint32_t at = Be32(g + 4);
    if (at >= d.size()) return "";
    if (row != nullptr) *row = i;
    return std::string(reinterpret_cast<const char*>(d.data() + at));
  }
  return "";
}

inline uint32_t TopId(const Toc& t) {
  uint32_t top = 0;
  for (uint32_t i = 0; i < t.count; i++) top = std::max(top, Be32(t.groups.data() + i * 16));
  return top;
}

inline std::vector<uint8_t> Add(const Toc& t, const std::vector<std::pair<uint32_t, std::string>>& rows) {
  std::vector<uint8_t> names = t.names, newRows;
  for (const auto& [id, name] : rows) {
    uint8_t g[16] = {0};
    PutBe32(g, id);
    PutBe32(g + 4, static_cast<uint32_t>(0x20 + names.size()));
    names.insert(names.end(), name.begin(), name.end());
    names.push_back(0);
    newRows.insert(newRows.end(), g, g + 16);
  }
  while (names.size() % 16 != 0) names.push_back(0);
  std::vector<uint8_t> out = t.header;
  PutBe32(out.data() + 8, t.count + static_cast<uint32_t>(rows.size()));
  PutBe32(out.data() + 0x10, static_cast<uint32_t>(0x20 + names.size()));
  out.insert(out.end(), names.begin(), names.end());
  out.insert(out.end(), t.groups.begin(), t.groups.end());
  out.insert(out.end(), newRows.begin(), newRows.end());
  out.insert(out.end(), t.trailer.begin(), t.trailer.end());
  return out;
}

struct Member {
  uint32_t word0 = 0;
  uint32_t type = 0;
  std::string name;
  std::vector<uint8_t> data;
};

inline std::vector<uint8_t> MakeFsys(uint32_t archiveId, const std::vector<Member>& members) {
  const auto align = [](size_t v, size_t n) { return (v + n - 1) / n * n; };
  const size_t n = members.size(), table = 0x60, namesAt = align(table + 4 * n, 16);
  std::vector<uint8_t> names;
  std::vector<size_t> nameOff;
  for (const Member& m : members) {
    nameOff.push_back(namesAt + names.size());
    names.insert(names.end(), m.name.begin(), m.name.end());
    names.push_back(0);
  }
  const size_t hdrsAt = align(namesAt + names.size(), 16), dataAt = align(hdrsAt + 0x70 * n, 32);
  size_t pos = dataAt;
  std::vector<size_t> offs;
  for (const Member& m : members) {
    offs.push_back(pos);
    pos = align(pos + m.data.size(), 32);
  }
  const size_t total = pos + 0x20;
  std::vector<uint8_t> out(total, 0);
  out[0] = 'F', out[1] = 'S', out[2] = 'Y', out[3] = 'S';
  const uint32_t head[8] = {0x201, archiveId, static_cast<uint32_t>(n), 0x80000000u, 3, 0x40, static_cast<uint32_t>(dataAt), static_cast<uint32_t>(total)};
  for (int k = 0; k < 8; k++) PutBe32(&out[4 + 4 * k], head[k]);
  PutBe32(&out[0x40], static_cast<uint32_t>(table)), PutBe32(&out[0x44], static_cast<uint32_t>(namesAt)), PutBe32(&out[0x48], static_cast<uint32_t>(dataAt));
  std::copy(names.begin(), names.end(), out.begin() + static_cast<std::ptrdiff_t>(namesAt));
  for (size_t i = 0; i < n; i++) {
    const size_t h = hdrsAt + 0x70 * i;
    PutBe32(&out[table + 4 * i], static_cast<uint32_t>(h));
    const uint32_t w[10] = {members[i].word0, static_cast<uint32_t>(offs[i]), static_cast<uint32_t>(members[i].data.size()), 0, 0,
                            static_cast<uint32_t>(members[i].data.size()), 0, 0, members[i].type, static_cast<uint32_t>(nameOff[i])};
    for (int k = 0; k < 10; k++) PutBe32(&out[h + 4 * static_cast<size_t>(k)], w[k]);
    std::copy(members[i].data.begin(), members[i].data.end(), out.begin() + static_cast<std::ptrdiff_t>(offs[i]));
  }
  out[total - 4] = 'F', out[total - 3] = 'S', out[total - 2] = 'Y', out[total - 1] = 'S';
  return out;
}

}

#endif
