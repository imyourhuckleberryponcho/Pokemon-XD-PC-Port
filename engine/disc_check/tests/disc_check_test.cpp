#include <disc_check.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace fs = std::filesystem;

namespace {

bool sOk = true;

void check(bool cond, const char* what) {
  std::printf("%s %s\n", cond ? "ok  " : "FAIL", what);
  sOk &= cond;
}

std::string utf8(const fs::path& p) {
  const std::u8string u8 = p.u8string();
  return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
}

void write_file(const fs::path& p, const std::string& text) {
  std::ofstream(p, std::ios::binary | std::ios::trunc) << text;
}

std::string read_file(const fs::path& p) {
  std::ifstream in(p, std::ios::binary);
  return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

const char* kHashA = "c1b5218f832403d15aa500ac4d6aacc8865c792d";
const char* kHashB = "0123456789abcdef0123456789abcdef01234567";

}

int main(int argc, char** argv) {
  fs::path folder = argc >= 2 ? fs::path(argv[1]) : fs::temp_directory_path() / "xd_disc_check_test";
  std::error_code ec;
  fs::remove_all(folder, ec);
  fs::create_directories(folder);
#if !defined(_WIN32)
  folder = fs::canonical(folder);
#endif
  const std::string cache = utf8(folder / "settings dir" / "disc_checks.txt");
  const fs::path discPath = folder / u8"Pokémon XD (USA) copy.iso";
  write_file(discPath, "pretend disc");

  char full[4096];
  DiscFileStamp stamp{};
  check(DiscCheck_Stamp(utf8(discPath).c_str(), full, sizeof(full), &stamp), "stamp of an existing file");
  check(stamp.size == 12, "stamp has the file's size");
  check(fs::path(std::u8string(reinterpret_cast<const char8_t*>(full))).is_absolute(), "stamp gives a full path");
  check(!DiscCheck_Stamp(utf8(folder / "missing.iso").c_str(), full, sizeof(full), &stamp), "no stamp for a missing file");
  check(!DiscCheck_Stamp(utf8(folder).c_str(), full, sizeof(full), &stamp), "no stamp for a folder");
  check(!DiscCheck_Stamp("", full, sizeof(full), &stamp), "no stamp for an empty path");
  {
    const fs::path old = fs::current_path();
    fs::current_path(folder);
    char rel[4096];
    DiscFileStamp relStamp{};
    const bool okRel = DiscCheck_Stamp(utf8(discPath.filename()).c_str(), rel, sizeof(rel), &relStamp);
    fs::current_path(old);
    DiscCheck_Stamp(utf8(discPath).c_str(), full, sizeof(full), &stamp);
    check(okRel && std::strcmp(rel, full) == 0, "relative and absolute path give the same full path");
  }

  char sha1[41] = "";
  DiscCheck_Stamp(utf8(discPath).c_str(), full, sizeof(full), &stamp);
  check(!DiscCheck_Recall(cache.c_str(), full, &stamp, sha1), "nothing remembered before the file exists");
  check(DiscCheck_Remember(cache.c_str(), full, &stamp, kHashA), "remember (creates the folder and file)");
  check(DiscCheck_Recall(cache.c_str(), full, &stamp, sha1) && std::strcmp(sha1, kHashA) == 0,
        "recall gives the remembered hash");
  check(!fs::exists(cache + ".new"), "no temporary file left behind");

  DiscFileStamp changed = stamp;
  changed.size++;
  check(!DiscCheck_Recall(cache.c_str(), full, &changed, sha1), "different size: not recalled");
  changed = stamp;
  changed.modified++;
  check(!DiscCheck_Recall(cache.c_str(), full, &changed, sha1), "different time: not recalled");
  {
    write_file(discPath, "a different pretend disc");
    fs::last_write_time(discPath, fs::last_write_time(discPath) + std::chrono::hours(1));
    DiscFileStamp now{};
    DiscCheck_Stamp(utf8(discPath).c_str(), full, sizeof(full), &now);
    check(now.size != stamp.size && now.modified != stamp.modified, "editing the file changes its stamp");
    check(!DiscCheck_Recall(cache.c_str(), full, &now, sha1), "edited file: old hash not recalled");
    check(DiscCheck_Remember(cache.c_str(), full, &now, kHashB), "remember the edited file");
    check(DiscCheck_Recall(cache.c_str(), full, &now, sha1) && std::strcmp(sha1, kHashB) == 0,
          "edited file: new hash recalled");
    check(!DiscCheck_Recall(cache.c_str(), full, &stamp, sha1), "old stamp no longer matches");
    std::string text = read_file(cache);
    size_t lines = 0;
    for (size_t at = 0; (at = text.find(full, at)) != std::string::npos; at++) {
      lines++;
    }
    check(lines == 1, "the path's old line was replaced, not added to");
    stamp = now;
  }

  const std::string other = utf8(folder / "other disc.rvz");
  const DiscFileStamp otherStamp{1459978240, 1234};
  check(DiscCheck_Remember(cache.c_str(), other.c_str(), &otherStamp, kHashA), "remember a second path");
  check(DiscCheck_Recall(cache.c_str(), full, &stamp, sha1) && std::strcmp(sha1, kHashB) == 0,
        "first path still recalled");
  for (int i = 0; i < DISC_CHECK_MAX_ENTRIES; i++) {
    const std::string p = utf8(folder / ("filler " + std::to_string(i) + ".iso"));
    DiscCheck_Remember(cache.c_str(), p.c_str(), &otherStamp, kHashA);
  }
  check(!DiscCheck_Recall(cache.c_str(), full, &stamp, sha1), "oldest entries dropped past the limit");
  const std::string newest = utf8(folder / ("filler " + std::to_string(DISC_CHECK_MAX_ENTRIES - 1) + ".iso"));
  check(DiscCheck_Recall(cache.c_str(), newest.c_str(), &otherStamp, sha1), "newest entry kept");

  check(!DiscCheck_Remember(cache.c_str(), full, &stamp, "not a hash"), "refuses to remember a non-hash");
  check(!DiscCheck_Remember(cache.c_str(), "two\nlines.iso", &stamp, kHashA), "refuses a path with a line break");

  write_file(cache, std::string("# comment\r\ngarbage line\r\n\r\nzzzz 1 2 x.iso\r\n") + kHashA + " 12 34 " + full +
                        "\r\n" + kHashB + " notanumber 5 y.iso\n");
  const DiscFileStamp handStamp{12, 34};
  check(DiscCheck_Recall(cache.c_str(), full, &handStamp, sha1) && std::strcmp(sha1, kHashA) == 0,
        "damaged file: the good line is still recalled (Windows line ends too)");
  check(DiscCheck_Remember(cache.c_str(), other.c_str(), &otherStamp, kHashB), "damaged file can be rewritten");
  check(DiscCheck_Recall(cache.c_str(), full, &handStamp, sha1), "good line survives the rewrite");
  check(read_file(cache).find("garbage") == std::string::npos, "bad lines are dropped on rewrite");

  check(!DiscCheck_Recall(utf8(folder / "none.txt").c_str(), full, &stamp, sha1), "missing cache file: nothing recalled");

  fs::remove_all(folder, ec);
  std::printf(sOk ? "PASS\n" : "FAILED\n");
  return sOk ? 0 : 1;
}
