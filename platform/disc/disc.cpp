#include "port_disc.h"

#include <nod.h>

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace {
NodHandle* sDisc;
NodHandle* sPartition;
char sGameId[7];
std::vector<uint8_t> sDol;
std::unordered_map<std::string, uint32_t> sFileOffsets;

std::string lower_path(const char* path) {
  std::string p;
  for (const char* c = path; *c != '\0'; ++c) {
    p += static_cast<char>(std::tolower(static_cast<unsigned char>(*c)));
  }
  while (!p.empty() && (p[0] == '/' || p[0] == '\\')) p.erase(0, 1);
  return p;
}

uint32_t be32(const uint8_t* p) { return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3]; }

void read_file_offsets(const uint8_t* fst, size_t size) {
  sFileOffsets.clear();
  if (fst == nullptr || size < 12) return;
  const uint32_t count = be32(fst + 8);
  if (count == 0 || static_cast<uint64_t>(count) * 12 > size) return;
  const char* names = reinterpret_cast<const char*>(fst + count * 12);
  const size_t namesSize = size - count * 12;
  std::vector<std::pair<uint32_t, std::string>> dirs;
  for (uint32_t i = 1; i < count; ++i) {
    while (!dirs.empty() && i >= dirs.back().first) dirs.pop_back();
    const uint8_t* e = fst + i * 12;
    const uint32_t nameOffset = be32(e) & 0xFFFFFF;
    if (nameOffset >= namesSize) return;
    const std::string name(names + nameOffset, strnlen(names + nameOffset, namesSize - nameOffset));
    const std::string path = dirs.empty() ? name : dirs.back().second + "/" + name;
    if (e[0] != 0) {
      dirs.emplace_back(be32(e + 8), path);
    } else {
      sFileOffsets[lower_path(path.c_str())] = be32(e + 4);
    }
  }
}

void set_error(char* error, size_t errorSize, const char* what) {
  if (error != nullptr && errorSize > 0) {
    const char* detail = nod_error_message();
    std::snprintf(error, errorSize, "%s%s%s", what, detail ? ": " : "", detail ? detail : "");
  }
}

struct ListState {
  PortDiscFileCallback callback;
  void* user;
  std::vector<std::pair<uint32_t, std::string>> dirs;
};

uint32_t list_entry(uint32_t index, NodNodeKind kind, const char* name, uint32_t size, void* user) {
  auto* st = static_cast<ListState*>(user);
  while (!st->dirs.empty() && index >= st->dirs.back().first) {
    st->dirs.pop_back();
  }
  std::string path = st->dirs.empty() ? std::string() : st->dirs.back().second;
  if (index == 0) {
    st->dirs.emplace_back(size, "");
    return 1;
  }
  path += path.empty() ? name : "/" + std::string(name);
  if (kind == NOD_NODE_KIND_DIRECTORY) {
    st->dirs.emplace_back(size, path);
  } else {
    st->callback(path.c_str(), size, st->user);
  }
  return index + 1;
}
}

extern "C" {

bool PortDisc_Open(const char* path, char* error, size_t errorSize) {
  PortDisc_Close();

  if (nod_disc_open(path, nullptr, &sDisc) != NOD_RESULT_OK) {
    set_error(error, errorSize, "Could not open the disc image");
    sDisc = nullptr;
    return false;
  }
  if (nod_disc_open_partition_kind(sDisc, NOD_PARTITION_KIND_DATA, nullptr, &sPartition) != NOD_RESULT_OK) {
    set_error(error, errorSize, "Could not read the disc's game data");
    PortDisc_Close();
    return false;
  }
  NodPartitionMeta meta{};
  if (nod_partition_meta(sPartition, &meta) != NOD_RESULT_OK || meta.raw_boot.data == nullptr ||
      meta.raw_boot.size < 6 || meta.raw_dol.data == nullptr) {
    set_error(error, errorSize, "The disc has no main program (main.dol)");
    PortDisc_Close();
    return false;
  }
  std::memcpy(sGameId, meta.raw_boot.data, 6);
  sGameId[6] = '\0';
  sDol.assign(meta.raw_dol.data, meta.raw_dol.data + meta.raw_dol.size);
  read_file_offsets(meta.raw_fst.data, meta.raw_fst.size);
  return true;
}

bool PortDisc_FileDiscOffset(const char* path, uint32_t* offset) {
  if (path == nullptr) return false;
  const auto it = sFileOffsets.find(lower_path(path));
  if (it == sFileOffsets.end()) return false;
  if (offset != nullptr) *offset = it->second;
  return true;
}

void PortDisc_Close(void) {
  nod_free(sPartition);
  nod_free(sDisc);
  sPartition = nullptr;
  sDisc = nullptr;
  sGameId[0] = '\0';
  sDol.clear();
  sDol.shrink_to_fit();
}

const char* PortDisc_GameId(void) { return sGameId; }

const uint8_t* PortDisc_Dol(size_t* size) {
  if (size != nullptr) {
    *size = sDol.size();
  }
  return sDol.empty() ? nullptr : sDol.data();
}

void PortDisc_ListFiles(PortDiscFileCallback callback, void* user) {
  if (sPartition == nullptr) {
    return;
  }
  ListState st{callback, user, {}};
  nod_partition_iterate_fst(sPartition, &list_entry, &st);
}

uint8_t* PortDisc_ReadOriginalFile(const char* path, size_t* size) {
  if (sPartition == nullptr) {
    return nullptr;
  }
  NodNodeKind kind;
  uint32_t length = 0;
  const uint32_t index = nod_partition_find_file(sPartition, path, &kind, &length);
  if (index == NOD_FST_STOP || kind != NOD_NODE_KIND_FILE) {
    return nullptr;
  }
  NodHandle* file = nullptr;
  if (nod_partition_open_file(sPartition, index, &file) != NOD_RESULT_OK) {
    return nullptr;
  }
  auto* data = static_cast<uint8_t*>(std::malloc(length ? length : 1));
  size_t done = 0;
  while (data != nullptr && done < length) {
    const int64_t n = nod_read(file, data + done, length - done);
    if (n <= 0) {
      std::free(data);
      data = nullptr;
      break;
    }
    done += static_cast<size_t>(n);
  }
  nod_free(file);
  if (data != nullptr && size != nullptr) {
    *size = length;
  }
  return data;
}

void PortDisc_Free(void* data) { std::free(data); }

}
