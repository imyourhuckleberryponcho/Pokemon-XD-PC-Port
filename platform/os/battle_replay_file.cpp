#include "battle_replay_file.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <zlib.h>

namespace port_replay {

namespace {

bool KeyOk(const std::string& key) {
  if (key.empty() || key.size() > 64) return false;
  for (char c : key) {
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '.')) return false;
  }
  return true;
}

bool Deflate(const std::string& raw, std::string& packed) {
  uLongf n = compressBound(static_cast<uLong>(raw.size()));
  packed.assign(n, '\0');
  if (compress2(reinterpret_cast<Bytef*>(&packed[0]), &n, reinterpret_cast<const Bytef*>(raw.data()), static_cast<uLong>(raw.size()), 9) != Z_OK) return false;
  packed.resize(n);
  return true;
}

bool Inflate(const std::string& packed, size_t rawSize, std::string& raw) {
  if (rawSize > (256u << 20)) return false;
  raw.assign(rawSize, '\0');
  uLongf n = static_cast<uLongf>(rawSize);
  if (uncompress(reinterpret_cast<Bytef*>(&raw[0]), &n, reinterpret_cast<const Bytef*>(packed.data()), static_cast<uLong>(packed.size())) != Z_OK) return false;
  return n == rawSize;
}

void Why(std::string* why, const std::string& text) {
  if (why) *why = text;
}

bool Line(const std::string& bytes, size_t& at, std::string& line) {
  if (at >= bytes.size()) return false;
  const size_t nl = bytes.find('\n', at);
  if (nl == std::string::npos) return false;
  line = bytes.substr(at, nl - at);
  at = nl + 1;
  return true;
}

bool ParseHeader(const std::string& bytes, size_t& at, File& file, std::string* why) {
  std::string line;
  if (!Line(bytes, at, line) || line.rfind("XDREPLAY ", 0) != 0) {
    Why(why, "This is not a battle replay file.");
    return false;
  }
  const int format = std::atoi(line.c_str() + 9);
  if (format < 1) {
    Why(why, "This is not a battle replay file.");
    return false;
  }
  if (format > kFormat) {
    Why(why, "This replay was made by a newer version of the game (replay format " + std::to_string(format) + ", this one reads up to " + std::to_string(kFormat) + "). Update the game to watch it.");
    return false;
  }
  for (;;) {
    if (!Line(bytes, at, line)) {
      Why(why, "The replay file is cut short (the header does not end).");
      return false;
    }
    if (line.empty()) return true;
    const size_t colon = line.find(": ");
    if (colon == std::string::npos || !KeyOk(line.substr(0, colon))) {
      Why(why, "The replay file's header is damaged.");
      return false;
    }
    file.Set(line.substr(0, colon), line.substr(colon + 2));
  }
}

}

const std::string* File::Find(const std::string& key) const {
  for (const auto& kv : header) {
    if (kv.first == key) return &kv.second;
  }
  return nullptr;
}
std::string File::Get(const std::string& key, const std::string& fallback) const {
  const std::string* v = Find(key);
  return v ? *v : fallback;
}
void File::Set(const std::string& key, const std::string& value) {
  for (auto& kv : header) {
    if (kv.first == key) {
      kv.second = value;
      return;
    }
  }
  header.emplace_back(key, value);
}

std::string Hex(uint64_t value) {
  char buf[24];
  std::snprintf(buf, sizeof buf, "%llx", static_cast<unsigned long long>(value));
  return buf;
}

uint32_t Crc32(const std::string& data) {
  return static_cast<uint32_t>(crc32(crc32(0L, Z_NULL, 0), reinterpret_cast<const Bytef*>(data.data()), static_cast<uInt>(data.size())));
}

bool Write(const File& file, std::string& bytes, std::string* why) {
  bytes = "XDREPLAY " + std::to_string(kFormat) + "\n";
  for (const auto& kv : file.header) {
    if (!KeyOk(kv.first) || kv.second.find('\n') != std::string::npos || kv.second.find('\r') != std::string::npos) {
      Why(why, "header entry '" + kv.first + "' is not allowed");
      return false;
    }
    bytes += kv.first + ": " + kv.second + "\n";
  }
  bytes += "\n";
  std::string body;
  for (const std::string& l : file.body) {
    if (l.find('\n') != std::string::npos) {
      Why(why, "a body line has a newline in it");
      return false;
    }
    body += l + "\n";
  }
  const std::pair<const char*, const std::string*> sections[] = {{"state", &file.state}, {"body", &body}};
  for (const auto& s : sections) {
    std::string packed;
    if (!Deflate(*s.second, packed)) {
      Why(why, "compressing failed");
      return false;
    }
    bytes += std::string("SECTION ") + s.first + " " + std::to_string(s.second->size()) + " " + std::to_string(packed.size()) + "\n";
    bytes += packed;
    bytes += "\n";
  }
  char tail[32];
  std::snprintf(tail, sizeof tail, "END %08x\n", Crc32(bytes));
  bytes += tail;
  return true;
}

bool ReadHeader(const std::string& bytes, File& file, std::string* why) {
  file = File();
  size_t at = 0;
  return ParseHeader(bytes, at, file, why);
}

bool Parse(const std::string& bytes, File& file, std::string* why) {
  file = File();
  size_t at = 0;
  if (!ParseHeader(bytes, at, file, why)) return false;
  bool haveState = false, haveBody = false;
  for (;;) {
    const size_t lineStart = at;
    std::string line;
    if (!Line(bytes, at, line)) {
      Why(why, "The replay file is cut short (it has no end mark). It was probably not copied completely.");
      return false;
    }
    if (line.rfind("END ", 0) == 0) {
      const uint32_t want = static_cast<uint32_t>(std::strtoul(line.c_str() + 4, nullptr, 16));
      if (Crc32(bytes.substr(0, lineStart)) != want) {
        Why(why, "The replay file is damaged (its check number does not match).");
        return false;
      }
      if (!haveState || !haveBody) {
        Why(why, "The replay file is incomplete (a part is missing).");
        return false;
      }
      return true;
    }
    char name[16] = {};
    unsigned long raw = 0, packed = 0;
    if (std::sscanf(line.c_str(), "SECTION %15s %lu %lu", name, &raw, &packed) != 3 || at + packed + 1 > bytes.size() || bytes[at + packed] != '\n') {
      Why(why, "The replay file is damaged (a part does not fit).");
      return false;
    }
    std::string rawBytes;
    if (!Inflate(bytes.substr(at, packed), raw, rawBytes)) {
      Why(why, "The replay file is damaged (a part cannot be unpacked).");
      return false;
    }
    at += packed + 1;
    if (std::strcmp(name, "state") == 0) {
      file.state = std::move(rawBytes);
      haveState = true;
    } else if (std::strcmp(name, "body") == 0) {
      size_t p = 0;
      std::string l;
      while (Line(rawBytes, p, l)) file.body.push_back(l);
      haveBody = true;
    }
  }
}

std::vector<Difference> Differences(const File& file, const std::vector<std::pair<std::string, std::string>>& mine,
                                    const std::vector<std::string>& mustMatch) {
  std::vector<Difference> out;
  for (const std::string& key : mustMatch) {
    const std::string wanted = file.Get(key);
    std::string have;
    for (const auto& kv : mine) {
      if (kv.first == key) have = kv.second;
    }
    if (wanted == have) continue;
    Difference d;
    d.key = key;
    d.wanted = wanted;
    d.have = have;
    d.sentence = key + ": the replay needs \"" + (wanted.empty() ? "none" : wanted) + "\", this game has \"" + (have.empty() ? "none" : have) + "\"";
    out.push_back(d);
  }
  return out;
}

}
