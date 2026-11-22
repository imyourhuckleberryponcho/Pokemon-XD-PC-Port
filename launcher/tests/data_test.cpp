
#include "launcher_data.h"

#include <cstdio>
#include <fstream>
#include <string>

namespace fs = std::filesystem;
namespace df = launcher::datafolder;

static int sFailures = 0;
#define CHECK(cond)                                                    \
  do {                                                                 \
    if (!(cond)) {                                                     \
      std::fprintf(stderr, "FAILED line %d: %s\n", __LINE__, #cond);   \
      sFailures++;                                                     \
    }                                                                  \
  } while (0)

static void Put(const fs::path& file, const std::string& text) {
  fs::create_directories(file.parent_path());
  std::ofstream(file, std::ios::binary) << text;
}

int main() {
  const fs::path root = fs::temp_directory_path() / "xd_launcher_data_test";
  std::error_code ec;
  fs::remove_all(root, ec);
  const fs::path a = root / "from", b = root / "to";
  Put(a / "settings.ini", "[x]\n");
  Put(a / "USA" / "Card A" / "01-GXXE-PokemonXD.gci", std::string(5000, 'S'));
  Put(a / "saves" / "s2" / "USA" / "Card A" / "x.gci", "second");
  Put(a / "mods" / "m" / "mod.json", "{}");
  fs::create_directories(a / "empty folder");

  const df::Tally t = df::Measure(a);
  CHECK(t.files == 4 && t.bytes == 4 + 5000 + 6 + 2);
  CHECK(df::IsEmpty(root / "nothing") && !df::IsEmpty(a));
  CHECK(df::IsInside(a / "mods", a) && df::IsInside(a, a) && !df::IsInside(a, a / "mods") && !df::IsInside(b, a));

  std::atomic<bool> stop{false};
  std::string error;
  uint64_t seenFiles = 0;
  CHECK(df::Copy(a, b, stop, [&](uint64_t f, uint64_t) { seenFiles = f; }, error));
  CHECK(error.empty() && seenFiles == 4);
  CHECK(df::Verify(a, b, error));
  CHECK(fs::is_directory(b / "empty folder"));
  CHECK(fs::exists(a / "settings.ini"));


  CHECK(!df::Copy(a, b, stop, nullptr, error) && !error.empty() && fs::exists(b / "settings.ini"));

  CHECK(!df::Copy(a, a / "inside", stop, nullptr, error));
  CHECK(!fs::exists(a / "inside"));


  Put(b / "USA" / "Card A" / "01-GXXE-PokemonXD.gci", std::string(5000, 'X'));
  CHECK(!df::Verify(a, b, error) && error.find(".gci") != std::string::npos);
  fs::remove(b / "mods" / "m" / "mod.json");
  CHECK(!df::Verify(a, b, error));


  const fs::path c = root / "stopped";
  stop = true;
  CHECK(!df::Copy(a, c, stop, nullptr, error) && !fs::exists(c));
  stop = false;


  std::vector<fs::path> left;
  CHECK(df::RemoveContents(b, left, error) && left.empty() && fs::is_directory(b) && df::IsEmpty(b));
  CHECK(fs::exists(a / "settings.ini"));

  fs::remove_all(root, ec);
  std::printf("launcher_data_test: %s\n", sFailures == 0 ? "all passed" : "FAILED");
  return sFailures == 0 ? 0 : 1;
}
