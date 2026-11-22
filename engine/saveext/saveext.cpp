#include "saveext.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <string_view>
#include <system_error>

namespace fs = std::filesystem;

namespace engine::saveext {

namespace {

constexpr char kMagic[4] = {'X', 'D', 'E', 'X'};

void Put16(std::vector<uint8_t>& v, uint16_t x) {
  v.push_back(static_cast<uint8_t>(x));
  v.push_back(static_cast<uint8_t>(x >> 8));
}
void Put32(std::vector<uint8_t>& v, uint32_t x) {
  for (int i = 0; i < 4; i++) v.push_back(static_cast<uint8_t>(x >> (8 * i)));
}
void Put64(std::vector<uint8_t>& v, uint64_t x) {
  for (int i = 0; i < 8; i++) v.push_back(static_cast<uint8_t>(x >> (8 * i)));
}

struct Reader {
  const uint8_t* p;
  size_t left;
  bool ok = true;
  bool Take(void* out, size_t n) {
    if (!ok || n > left) return ok = false;
    if (n > 0) std::memcpy(out, p, n);
    p += n;
    left -= n;
    return true;
  }
  uint16_t U16() {
    uint8_t b[2] = {};
    Take(b, 2);
    return static_cast<uint16_t>(b[0] | (b[1] << 8));
  }
  uint32_t U32() {
    uint8_t b[4] = {};
    Take(b, 4);
    return static_cast<uint32_t>(b[0]) | (static_cast<uint32_t>(b[1]) << 8) | (static_cast<uint32_t>(b[2]) << 16) |
           (static_cast<uint32_t>(b[3]) << 24);
  }
  uint64_t U64() {
    const uint64_t lo = U32();
    return lo | (static_cast<uint64_t>(U32()) << 32);
  }
};

std::string Clean(std::string_view key) {
  std::string out(key);
  for (char& c : out) {
    const bool good = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '-' || c == '_';
    if (!good) c = '_';
  }
  return out;
}

}

const Section* File::Find(const std::string& name) const {
  for (const Section& s : sections) {
    if (s.name == name) return &s;
  }
  return nullptr;
}

void File::Set(Section section) {
  for (Section& s : sections) {
    if (s.name == section.name) {
      s = std::move(section);
      return;
    }
  }
  sections.push_back(std::move(section));
}

std::vector<std::string> File::RequiredNames() const {
  std::vector<std::string> out;
  for (const Section& s : sections) {
    if (s.required && !s.data.empty()) out.push_back(s.name);
  }
  return out;
}

uint64_t Hash64(const uint8_t* data, size_t size) {
  uint64_t h = 1469598103934665603ull;
  for (size_t i = 0; i < size; i++) h = (h ^ data[i]) * 1099511628211ull;
  return h;
}

uint32_t Crc32(const uint8_t* data, size_t size) {
  static const std::array<uint32_t, 256> table = [] {
    std::array<uint32_t, 256> t{};
    for (uint32_t i = 0; i < 256; i++) {
      uint32_t c = i;
      for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
      t[i] = c;
    }
    return t;
  }();
  uint32_t c = 0xFFFFFFFFu;
  for (size_t i = 0; i < size; i++) c = table[(c ^ data[i]) & 0xFF] ^ (c >> 8);
  return c ^ 0xFFFFFFFFu;
}

std::vector<uint8_t> Encode(const File& file) {
  std::vector<uint8_t> v;
  v.insert(v.end(), kMagic, kMagic + 4);
  Put32(v, file.format);
  Put64(v, file.saveHash);
  const size_t writerLen = std::min<size_t>(file.writer.size(), 0xFFFF);
  Put16(v, static_cast<uint16_t>(writerLen));
  v.insert(v.end(), file.writer.begin(), file.writer.begin() + writerLen);
  Put32(v, static_cast<uint32_t>(file.sections.size()));
  for (const Section& s : file.sections) {
    Put16(v, static_cast<uint16_t>(s.name.size()));
    v.insert(v.end(), s.name.begin(), s.name.end());
    Put16(v, s.version);
    v.push_back(s.required ? 1 : 0);
    Put32(v, static_cast<uint32_t>(s.data.size()));
    v.insert(v.end(), s.data.begin(), s.data.end());
  }
  Put32(v, Crc32(v.data(), v.size()));
  return v;
}

Decoded Decode(const uint8_t* data, size_t size, File& out) {
  if (size < 4 || std::memcmp(data, kMagic, 4) != 0) return Decoded::BadMagic;
  if (size < 8) return Decoded::Truncated;
  Reader head{data + 4, size - 4};
  const uint32_t format = head.U32();
  if (format > kFormat) return Decoded::TooNew;
  if (size < 8 + 4) return Decoded::Truncated;
  const uint8_t* end = data + size - 4;
  const uint32_t want = static_cast<uint32_t>(end[0]) | (static_cast<uint32_t>(end[1]) << 8) |
                        (static_cast<uint32_t>(end[2]) << 16) | (static_cast<uint32_t>(end[3]) << 24);
  if (Crc32(data, size - 4) != want) return Decoded::BadChecksum;
  Reader r{data + 8, size - 8 - 4};
  File f;
  f.format = format;
  f.saveHash = r.U64();
  const uint16_t writerLen = r.U16();
  f.writer.resize(writerLen);
  r.Take(f.writer.data(), writerLen);
  const uint32_t count = r.U32();
  if (!r.ok || count > 4096) return Decoded::Truncated;
  for (uint32_t i = 0; i < count; i++) {
    Section s;
    const uint16_t nameLen = r.U16();
    s.name.resize(nameLen);
    r.Take(s.name.data(), nameLen);
    s.version = r.U16();
    uint8_t req = 0;
    r.Take(&req, 1);
    s.required = req != 0;
    const uint32_t len = r.U32();
    if (!r.ok || len > r.left) return Decoded::Truncated;
    s.data.assign(r.p, r.p + len);
    r.p += len;
    r.left -= len;
    f.sections.push_back(std::move(s));
  }
  if (!r.ok || r.left != 0) return Decoded::Truncated;
  out = std::move(f);
  return Decoded::Ok;
}

const char* DecodedText(Decoded d) {
  switch (d) {
  case Decoded::Ok: return "ok";
  case Decoded::BadMagic: return "not an extension file";
  case Decoded::TooNew: return "written by a newer version";
  case Decoded::Truncated: return "cut short";
  case Decoded::BadChecksum: return "checksum does not match";
  }
  return "?";
}

fs::path FileFor(const fs::path& folder, std::string_view key) { return folder / (Clean(key) + ".ext"); }
fs::path StagedFor(const fs::path& folder, std::string_view key) { return folder / (Clean(key) + ".ext.new"); }

bool ReadAll(const fs::path& path, std::vector<uint8_t>& out) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return false;
  in.seekg(0, std::ios::end);
  const std::streamoff size = in.tellg();
  if (size < 0) return false;
  in.seekg(0);
  out.resize(static_cast<size_t>(size));
  if (size > 0) in.read(reinterpret_cast<char*>(out.data()), size);
  return static_cast<bool>(in);
}

bool WriteAtomic(const fs::path& path, const std::vector<uint8_t>& bytes) {
  std::error_code ec;
  fs::create_directories(path.parent_path(), ec);
  const fs::path temp = path.string() + ".tmp";
  {
    std::ofstream out(temp, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    out.close();
    if (!out) {
      fs::remove(temp, ec);
      return false;
    }
  }
  fs::rename(temp, path, ec);
  if (ec) {
    fs::remove(path, ec);
    fs::rename(temp, path, ec);
    if (ec) {
      fs::remove(temp, ec);
      return false;
    }
  }
  return true;
}

bool Stage(const fs::path& folder, std::string_view key, const File& file) {
  return WriteAtomic(StagedFor(folder, key), Encode(file));
}

bool Commit(const fs::path& folder, std::string_view key) {
  std::error_code ec;
  const fs::path staged = StagedFor(folder, key), real = FileFor(folder, key);
  if (!fs::exists(staged, ec)) return false;
  fs::rename(staged, real, ec);
  if (ec) {
    fs::remove(real, ec);
    fs::rename(staged, real, ec);
  }
  return !ec;
}

void DropStaged(const fs::path& folder, std::string_view key) {
  std::error_code ec;
  fs::remove(StagedFor(folder, key), ec);
}

const char* StateName(State s) {
  switch (s) {
  case State::Missing: return "missing";
  case State::Paired: return "paired";
  case State::Stale: return "stale";
  case State::Damaged: return "damaged";
  case State::TooNew: return "too new";
  }
  return "?";
}

Loaded Load(const fs::path& folder, std::string_view key, uint64_t saveHashNow) {
  Loaded out;
  std::error_code ec;
  const fs::path staged = StagedFor(folder, key);
  if (fs::exists(staged, ec)) {
    std::vector<uint8_t> bytes;
    File f;
    if (ReadAll(staged, bytes) && Decode(bytes.data(), bytes.size(), f) == Decoded::Ok && f.saveHash == saveHashNow) {
      if (Commit(folder, key)) out.promoted = true;
    } else {
      DropStaged(folder, key);
    }
  }
  const fs::path real = FileFor(folder, key);
  std::vector<uint8_t> bytes;
  if (!fs::exists(real, ec) || !ReadAll(real, bytes)) return out;
  switch (Decode(bytes.data(), bytes.size(), out.file)) {
  case Decoded::Ok: out.state = out.file.saveHash == saveHashNow ? State::Paired : State::Stale; break;
  case Decoded::TooNew: out.state = State::TooNew; break;
  default: out.state = State::Damaged; break;
  }
  return out;
}

bool PairsWith(const std::vector<uint8_t>& ext, const std::vector<uint8_t>& save) {
  File f;
  return Decode(ext.data(), ext.size(), f) == Decoded::Ok && f.saveHash == Hash64(save.data(), save.size());
}

bool PairsWithGci(const std::vector<uint8_t>& ext, const std::vector<uint8_t>& gci) {
  if (gci.size() <= kGciHeader) return false;
  File f;
  return Decode(ext.data(), ext.size(), f) == Decoded::Ok && f.saveHash == Hash64(gci.data() + kGciHeader, gci.size() - kGciHeader);
}

}
