#include "../disc/disc_overlay.hpp"
#include <port_disc.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

namespace {
bool sOk = true;
void check(bool cond, const char* what) {
  std::printf("%s %s\n", cond ? "ok  " : "FAIL", what);
  sOk &= cond;
}

std::string read(const char* path) {
  size_t size = 0;
  uint8_t* data = PortDisc_ReadFile(path, &size);
  if (data == nullptr) return "<none>";
  std::string s(reinterpret_cast<char*>(data), size);
  PortDisc_Free(data);
  return s;
}
}

int main() {
  const std::filesystem::path folder = std::filesystem::temp_directory_path() / "xd_disc_overlay_test";
  std::filesystem::create_directories(folder);
  const std::filesystem::path local = folder / "replacement.bin";
  std::ofstream(local, std::ios::binary | std::ios::trunc) << "bytes from a PC file";
  const std::u8string localU8 = local.u8string();
  const char* localPath = reinterpret_cast<const char*>(localU8.c_str());

  check(PortDisc_OverlayCount() == 0 && read("common.fsys") == "<none>", "nothing overlaid at first (and no disc open)");

  const uint8_t memory[] = {'i', 'n', ' ', 'm', 'e', 'm', 'o', 'r', 'y'};
  PortDiscOverlayFile files[] = {
      {"common.fsys", nullptr, memory, sizeof memory},
      {"/Sound/SE.samp", localPath, nullptr, 0},
      {"empty.bin", nullptr, nullptr, 0},
  };
  check(PortDisc_SetOverlay(files, 3) && PortDisc_OverlayCount() == 3, "three files overlaid");
  check(read("common.fsys") == "in memory", "a file from memory");
  check(read("COMMON.FSYS") == "in memory" && read("/common.fsys") == "in memory", "any case, leading '/'");
  check(read("sound/se.samp") == "bytes from a PC file" && read("sound\\SE.samp") == "bytes from a PC file",
        "a file from a PC file, in a disc folder, '\\' as '/'");
  check(read("empty.bin").empty(), "an empty file");
  check(read("other.fsys") == "<none>", "other paths still go to the disc");

  {
    const port_disc::OverlayEntry* e = port_disc::FindOverlay("sound/se.samp");
    check(e != nullptr && e->size == 20 && e->path == "Sound/SE.samp", "found, with its size and the path as given");
    port_disc::OverlayReader r(*e);
    uint8_t buf[8] = {};
    check(r.Seek(6, 0) == 6 && r.Read(buf, 4) == 4 && std::memcmp(buf, "from", 4) == 0, "seek and read a piece");
    check(r.Seek(-2, 2) == 18 && r.Read(buf, 8) == 2 && std::memcmp(buf, "le", 2) == 0, "read stops at the end");
    check(r.Read(buf, 8) == 0, "at the end: nothing more");
    check(r.Seek(-30, 1) == -1, "seeking before the start is refused");
  }

  PortDiscOverlayFile twice[] = {{"a.bin", nullptr, memory, 1}, {"A.BIN", nullptr, memory, 2}};
  check(!PortDisc_SetOverlay(twice, 2) && read("common.fsys") == "in memory", "the same path twice: refused, old overlay kept");
  PortDiscOverlayFile missing[] = {{"a.bin", "no such file here.bin", nullptr, 0}};
  check(!PortDisc_SetOverlay(missing, 1) && PortDisc_OverlayCount() == 3, "a missing PC file: refused");
  check(PortDisc_SetOverlay(nullptr, 0) && PortDisc_OverlayCount() == 0 && read("common.fsys") == "<none>",
        "an empty list: nothing overlaid any more");

  std::error_code ec;
  std::filesystem::remove_all(folder, ec);
  std::printf(sOk ? "disc_overlay_test: all passed\n" : "disc_overlay_test: FAILED\n");
  return sOk ? 0 : 1;
}
