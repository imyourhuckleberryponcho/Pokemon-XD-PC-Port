#include <gc_dsp.h>
#include <port_disc.h>

#include <cstdio>
#include <set>
#include <vector>

namespace {
constexpr uint32_t kDspSlave = 0x80409040;
constexpr uint32_t kDspSlaveSize = 0x1EC0;
struct DataRange {
  uint16_t start, count;
};
constexpr DataRange kJumpTables[] = {{0x0C86, 20}, {0x0C9A, 16}, {0x0CAA, 32}};
bool in_jump_table(size_t pc) {
  for (const DataRange& r : kJumpTables) {
    if (pc >= r.start && pc < static_cast<size_t>(r.start + r.count)) {
      return true;
    }
  }
  return false;
}
bool ok = true;
void check(bool cond, const char* what) {
  std::printf("%s %s\n", cond ? "ok  " : "FAIL", what);
  ok &= cond;
}
}

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "Usage: %s <disc image> [listing file]\n", argv[0]);
    return 2;
  }
  char error[512];
  if (!PortDisc_Open(argv[1], error, sizeof(error))) {
    std::fprintf(stderr, "FAILED: %s\n", error);
    return 1;
  }
  size_t dolSize = 0;
  const uint8_t* dol = PortDisc_Dol(&dolSize);
  std::vector<uint8_t> bytes(kDspSlaveSize);
  if (!PortDol_Read(dol, dolSize, kDspSlave, bytes.data(), kDspSlaveSize)) {
    std::fprintf(stderr, "FAILED: MusyX's DSP program isn't in main.dol\n");
    return 1;
  }
  std::vector<uint16_t> words(kDspSlaveSize / 2);
  for (size_t i = 0; i < words.size(); i++) {
    words[i] = static_cast<uint16_t>(bytes[i * 2] << 8 | bytes[i * 2 + 1]);
  }

  FILE* listing = argc >= 3 ? std::fopen(argv[2], "w") : nullptr;
  std::set<uint16_t> starts;
  std::vector<std::pair<uint16_t, uint16_t>> branches;
  int unknown = 0;
  for (size_t pc = 0; pc < words.size();) {
    if (in_jump_table(pc)) {
      if (listing != nullptr) {
        std::fprintf(listing, "%04X  .word 0x%04X  (jump table)\n", static_cast<unsigned>(pc), words[pc]);
      }
      pc++;
      continue;
    }
    starts.insert(static_cast<uint16_t>(pc));
    char text[96];
    const int n = GcDsp_Disassemble(&words[pc], words.size() - pc, static_cast<uint16_t>(pc), text, sizeof(text));
    if (GcDsp_DecodeMain(words[pc]) < 0) {
      unknown++;
    }
    const uint16_t w = words[pc];
    if ((w & 0xFFF0) == 0x0290 || (w & 0xFFF0) == 0x02B0) {
      if (pc + 1 < words.size()) {
        branches.emplace_back(static_cast<uint16_t>(pc), words[pc + 1]);
      }
    }
    if (listing != nullptr) {
      std::fprintf(listing, "%04X  %s\n", static_cast<unsigned>(pc), text);
    }
    pc += n;
  }
  if (listing != nullptr) {
    std::fclose(listing);
  }
  int badTargets = 0, romCalls = 0;
  for (const auto& [from, target] : branches) {
    if (target >= 0x8000) {
      romCalls++;
    } else if (!starts.count(target)) {
      std::printf("     jump at %04X goes to %04X, which isn't an instruction start\n", from, target);
      badTargets++;
    }
  }
  int badEntries = 0;
  for (const DataRange& r : kJumpTables) {
    for (uint16_t i = 0; i < r.count; i++) {
      if (!starts.count(words[r.start + i])) {
        std::printf("     jump table %04X entry %u is %04X, which isn't an instruction start\n", r.start, i,
                    words[r.start + i]);
        badEntries++;
      }
    }
  }
  std::printf("%zu words, %zu instructions, %zu jumps/calls (%d into the ROM)\n", words.size(), starts.size(),
              branches.size(), romCalls);
  check(badEntries == 0, "every jump-table entry is an instruction start");
  check(unknown == 0, "every word of MusyX's DSP program is a listed instruction");
  check(badTargets == 0, "every jump lands on an instruction start");
  return ok ? 0 : 1;
}
