#include "../src/data/common_rel.h"
#include <dol_tables.h>
#include <gs_formats.h>
#include <port_disc.h>

#include <game/pxdvs/app/pokemon/pokemonDB.hpp>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

bool PortXD_LinkDataModule(const uint8_t* file, size_t size, std::vector<std::pair<void*, uint32_t>>* out,
                           std::string& problem);

extern "C" {
#define RELGLOBAL(addr, name) void* name = nullptr;
#include "../src/data/relglobal_vars.inc"
#undef RELGLOBAL
#define CODEREF(addr, name) void name(void) {}
#include "../src/data/main_program_functions.inc"
#undef CODEREF
unsigned char relglobal_unk270[0x1BFC];
#define VARREF(addr, size, name) unsigned char name[size];
#include "../src/data/main_program_variables.inc"
#undef VARREF
uint32_t PortXD_GameAddressOf(const void* p);

int OSLinkFixed(void* module, void* bss);
int OSUnlink(void* module);
void PortXD_RegisterModuleHandlers(void);
void PortXD_RegisterMainProgramMap(void);
void* PortXD_GameAddress(uint32_t gcAddress);
bool PortMem_ReserveMEM1(void);
}

namespace {
struct Named {
  const char* name;
  void** where;
};
const Named kVariables[] = {
#define RELGLOBAL(addr, name) {#name, &name},
#include "../src/data/relglobal_vars.inc"
#undef RELGLOBAL
};
bool ok = true;
void check(bool cond, const char* what) {
  std::printf("%s %s\n", cond ? "ok  " : "FAIL", what);
  ok &= cond;
}
}

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "Usage: %s <disc image> [--touch NAME]\n", argv[0]);
    return 2;
  }
  char error[512];
  check(PortMem_ReserveMEM1(), "GameCube memory at 0x80000000 (as in the game program)");
  if (!PortDisc_Open(argv[1], error, sizeof(error))) {
    std::fprintf(stderr, "FAILED: %s\n", error);
    return 1;
  }

  size_t dolSize = 0;
  const uint8_t* dol = PortDisc_Dol(&dolSize);
  check(PortXD_LoadDolTables(dol, dolSize, error, sizeof(error)), "main.dol loads into GameCube memory");
  uint8_t* tex = PortXD_GetDolTable("tex_warning");
  std::vector<uint8_t> expect(0x21480);
  PortDol_Read(dol, dolSize, 0x803D5960, expect.data(), (uint32_t)expect.size());
  check(tex == reinterpret_cast<uint8_t*>(0x803D5960) && std::memcmp(tex, expect.data(), expect.size()) == 0,
        "tables the decomp asks for are at their GameCube address, with the disc's bytes (tex_warning)");
  bool allSix = true;
  for (const char* name : {"uv_cmn_win_00", "tex_warning_pstar", "uv_cmn_csr_00", "uv_common_icon_00", "_memcardBannerImage"}) {
    allSix &= PortXD_GetDolTable(name) != nullptr;
  }
  check(allSix, "all six tables from docs/notes/port-dol-tables.md are available");
  uint32_t bssStart, bssEnd, imageEnd;
  PortXD_DolImage(&bssStart, &bssEnd, &imageEnd);
  std::printf("main.dol image ends at %08X (variables %08X-%08X); heap starts at %08X\n", imageEnd, bssStart, bssEnd,
              PortXD_ArenaLo());
  check(PortXD_ArenaLo() > imageEnd + 0x18000 && (PortXD_ArenaLo() & 0x1F) == 0,
        "the heap starts above the image and its 96 KB of stacks, 32-byte aligned (XD's linker script)");
  size_t size = 0;
  uint8_t* file = PortDisc_ReadFile("common.fsys", &size);
  FsysArchive fsys;
  int index = -1;
  if (file == nullptr || !Fsys_Open(file, size, &fsys) || (index = Fsys_Find(&fsys, "common_rel")) < 0) {
    std::fprintf(stderr, "FAILED: common_rel not found\n");
    return 1;
  }
  uint32_t relSize = 0;
  uint8_t* rel = Fsys_Extract(&fsys, (uint32_t)index, &relSize);
  check(Rel_FileExtent(rel) == relSize, "a loaded module's size is worked out from its header");

  PortXD_RegisterMainProgramMap();
  PortXD_RegisterModuleHandlers();
  check(OSLinkFixed(rel, nullptr) == 1, "OSLinkFixed links common_rel");

  if (argc >= 4 && std::strcmp(argv[2], "--touch") == 0) {
    for (const Named& v : kVariables) {
      if (std::strcmp(v.name, argv[3]) == 0) {
        std::printf("reading the first byte of %s...\n", v.name);
        std::fflush(stdout);
        volatile uint8_t b = *static_cast<volatile uint8_t*>(*v.where);
        std::printf("read %u: this table is converted\n", b);
        return 0;
      }
    }
    std::fprintf(stderr, "no variable %s\n", argv[3]);
    return 2;
  }

  int set = 0;
  for (const Named& v : kVariables) {
    set += *v.where != nullptr;
  }
  std::printf("%d of %zu variables set\n", set, sizeof(kVariables) / sizeof(kVariables[0]));
  check(set == 132, "common_rel sets its 132 relglobal variables");

  const auto* count = static_cast<const uint32_t*>(pokemon_data_number);
  const auto* pokemon = static_cast<const PokemonData*>(pokemon_data);
  check(count != nullptr && *count == 415, "pokemon_data_number points to the count (415)");
  const BasisStatus& b = pokemon[1].basisStatus[0];
  check(b.maxHp == 45 && b.phyAtk == 49 && b.phyDef == 49 && b.speAtk == 65 && b.speDef == 65 && b.nimbleness == 45,
        "pokemon_data[1] is Bulbasaur, converted (45/49/49/65/65/45)");
  check(*static_cast<const uint32_t*>(waza_data_number) > 300, "other counts are converted too (moves)");

  bool low = true;
  for (const Named& v : kVariables) {
    low &= reinterpret_cast<uintptr_t>(*v.where) < 0x100000000ull;
  }
  check(low, "every table address fits in 32 bits (game code keeps some in u32 variables)");

  const auto* legend = static_cast<const uint16_t*>(legendPokemonData);
  const auto* balls = static_cast<const uint16_t*>(mball_data);
  std::printf("legendPokemonData: %u %u %u ...  mball_data: %u %u %u ...  msg_palette: %u %u %u ...\n", legend[0],
              legend[1], legend[2], balls[0], balls[1], balls[2], static_cast<const uint8_t*>(msg_palette)[0],
              static_cast<const uint8_t*>(msg_palette)[1], static_cast<const uint8_t*>(msg_palette)[2]);
  check(legend[0] >= 1 && legend[0] < 415 && legend[1] >= 1 && legend[1] < 415,
        "legendPokemonData holds species numbers (byte order converted)");

  const uint16_t slot = static_cast<const uint16_t*>(item_data_index)[13];
  const uint8_t* potion = static_cast<const uint8_t*>(item_data_prime) + slot * 0x28;
  uint16_t price;
  uint32_t fieldUse, battleUse;
  std::memcpy(&price, potion + 0x06, 2);
  std::memcpy(&fieldUse, potion + 0x1C, 4);
  std::memcpy(&battleUse, potion + 0x20, 4);
  std::printf("item 13: price %u, field use %08X, battle use %08X (menuUseItemRecovery is at %p)\n", price, fieldUse,
              battleUse, reinterpret_cast<void*>(&menuUseItemRecovery));
  check(price == 300, "item 13 (Potion) costs 300 (byte order converted)");
  check(fieldUse == static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&menuUseItemRecovery)),
        "its use handler is the PC's menuUseItemRecovery (GameCube function address mapped)");

  const auto* tackle = reinterpret_cast<const uint8_t*>(waza_data) + 33 * 0x38;
  int16_t power;
  std::memcpy(&power, tackle + 0x18, 2);
  std::printf("move 33: power %d, accuracy %u, PP %u\n", power, tackle[0x04], tackle[0x01]);
  check(power == 35 && tackle[0x04] == 95 && tackle[0x01] == 35, "move 33 (Tackle) converts: 35 power, 95 accuracy, 35 PP");

  check(PortXD_GameAddress(0x803D5960) == tex, "PortXD_GameAddress: data inside main.dol stays at its address");
  check(PortXD_GameAddress(0x804E8A38) == relglobal_unk270, "PortXD_GameAddress: a mapped variable -> the PC one");
  check(PortXD_GameAddress(0) == nullptr, "PortXD_GameAddress: 0 stays a null pointer");
  check(PortXD_GameAddress(0x804EB95C) == fight_seq_ushort_buff,
        "a battle-script variable (fight_seq_ushort_buff, 0x804EB95C) -> the PC one");
  check(PortXD_GameAddressOf(fight_seq_ushort_buff) == 0x804EB95C && PortXD_GameAddressOf(tex) == 0x803D5960 &&
            PortXD_GameAddressOf(nullptr) == 0,
        "PortXD_GameAddressOf turns PC pointers back into GameCube addresses");

  check(OSUnlink(rel) == 1, "OSUnlink unlinks common_rel");
  int cleared = 0;
  for (const Named& v : kVariables) {
    cleared += *v.where == nullptr;
  }
  check(cleared == (int)(sizeof(kVariables) / sizeof(kVariables[0])), "unlinking clears the variables, as _epilog does");

  {
    std::vector<std::string> fsysPaths;
    PortDisc_ListFiles(
        [](const char* path, uint32_t, void* user) {
          const size_t n = std::strlen(path);
          if (n > 5 && std::strcmp(path + n - 5, ".fsys") == 0)
            static_cast<std::vector<std::string>*>(user)->emplace_back(path);
        },
        &fsysPaths);
    std::map<uint32_t, std::string> seen;
    std::map<std::string, int> reasons;
    int linkable = 0, menuTables = 0;
    std::string firstOk;
    for (const std::string& path : fsysPaths) {
      size_t size = 0;
      uint8_t* data = PortDisc_ReadFile(path.c_str(), &size);
      FsysArchive fsys{};
      if (data != nullptr && Fsys_Open(data, size, &fsys)) {
        for (uint32_t i = 0; i < fsys.count; i++) {
          FsysEntry e{};
          if (!Fsys_Entry(&fsys, i, &e) || e.type != 14) continue;
          uint32_t relSize = 0;
          uint8_t* relData = Fsys_Extract(&fsys, i, &relSize);
          if (relData != nullptr && relSize >= 4) {
            const uint32_t id = static_cast<uint32_t>(relData[0]) << 24 | relData[1] << 16 | relData[2] << 8 | relData[3];
            if (id != 88 && !seen.count(id)) {
              seen[id] = e.name != nullptr ? e.name : "?";
              std::string problem;
              if (PortXD_LinkDataModule(relData, relSize, nullptr, problem)) {
                linkable++;
                const std::string& nm = seen[id];
                if (nm == "pocket_menu" || nm == "mail" || nm == "M3_out" || nm == "worldmap_menu_rel" ||
                    nm == "pda_menu" || nm == "toolbattle_menu") {
                  static XDCommonTables t;
                  char err[512];
                  if (PortXD_ConvertCommonRel(relData, relSize, &t, err, sizeof(err))) {
                    std::string conv, guarded;
                    for (uint32_t g = 0; g < t.numGlobals; g++) {
                      std::string gn = t.globals[g].name;
                      if (gn.size() > 7 && gn.compare(gn.size() - 7, 7, "_number") == 0) continue;
                      (t.globals[g].converted ? conv : guarded) += " " + gn;
                    }
                    std::printf("     %s tables: converted [%s ] guarded [%s ]\n", nm.c_str(), conv.c_str(),
                                guarded.c_str());
                    menuTables++;
                    auto global = [&](const char* g) -> void* {
                      for (uint32_t k = 0; k < t.numGlobals; k++)
                        if (std::strcmp(t.globals[k].name, g) == 0) return t.globals[k].value;
                      return nullptr;
                    };
                    if (nm == "worldmap_menu_rel") {
                      const auto* cam = static_cast<const float*>(global("worldmap_cameradata"));
                      const auto* n = static_cast<const uint32_t*>(global("worldmap_cameradata_number"));
                      std::printf("       %u world map cameras; camera 0 fov %.2f\n", *n, cam[6]);
                      check(*n > 0 && *n < 1000 && cam[6] > 1.0f && cam[6] < 180.0f,
                            "world map cameras convert to sensible numbers");
                    } else if (nm == "pocket_menu") {
                      const auto* shops = static_cast<const uint8_t*>(global("shopData"));
                      const uint32_t nShops = *static_cast<const uint32_t*>(global("shopData_number"));
                      const uint32_t nGoods = *static_cast<const uint32_t*>(global("shopGoods_number"));
                      bool inside = nShops > 0 && nShops < 1000;
                      for (uint32_t k = 0; inside && k < nShops; k++)
                        inside = *reinterpret_cast<const uint16_t*>(shops + 4 * k + 2) < nGoods;
                      std::printf("       %u shops, %u goods entries\n", nShops, nGoods);
                      check(inside, "every shop's item list starts inside shopGoods");
                    }
                  } else {
                    std::printf("     %s: converter failed: %s\n", nm.c_str(), err);
                  }
                }
                if (firstOk.empty()) firstOk = seen[id];
              } else {
                reasons[seen[id] + ": " + problem]++;
              }
            }
          }
          std::free(relData);
        }
      }
      PortDisc_Free(data);
    }
    std::printf("     other REL modules: %zu distinct; the generic linker takes %d now%s%s\n", seen.size(), linkable,
                firstOk.empty() ? "" : ", e.g. ", firstOk.c_str());
    for (const auto& [why, count] : reasons) std::printf("       %4d: %s ...\n", count, why.c_str());
    check(!seen.empty(), "the disc's other REL modules were all checked against the generic linker");
    check(menuTables == 6, "the 6 menu modules' tables go through the table converter");
  }
  return ok ? 0 : 1;
}
