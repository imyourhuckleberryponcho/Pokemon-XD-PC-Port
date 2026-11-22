#include "launcher_data.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <system_error>

namespace fs = std::filesystem;

namespace launcher::datafolder {
namespace {

std::string Utf8(const fs::path& path) {
  const std::u8string text = path.u8string();
  return std::string(text.begin(), text.end());
}

bool MustMatchExactly(const fs::path& file) {
  std::string ext = Utf8(file.extension());
  for (char& c : ext) c = static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
  const std::string name = Utf8(file.filename());
  return ext == ".gci" || ext == ".ini" || ext == ".json" || name == "MC_SYSTEM_AREA";
}

bool SameBytes(const fs::path& a, const fs::path& b) {
  std::ifstream x(a, std::ios::binary), y(b, std::ios::binary);
  if (!x || !y) return false;
  char bx[65536], by[65536];
  while (true) {
    x.read(bx, sizeof bx);
    y.read(by, sizeof by);
    if (x.gcount() != y.gcount()) return false;
    if (x.gcount() == 0) return true;
    if (std::memcmp(bx, by, static_cast<size_t>(x.gcount())) != 0) return false;
  }
}

fs::path Absolute(const fs::path& path) {
  std::error_code ec;
  const fs::path full = fs::weakly_canonical(fs::absolute(path, ec), ec);
  return ec ? path : full;
}

}

Tally Measure(const fs::path& folder) {
  Tally t;
  std::error_code ec;
  for (fs::recursive_directory_iterator it(folder, fs::directory_options::skip_permission_denied, ec), end;
       !ec && it != end; it.increment(ec)) {
    std::error_code e2;
    if (it->is_regular_file(e2)) {
      t.files++;
      t.bytes += it->file_size(e2);
    }
  }
  return t;
}

bool IsEmpty(const fs::path& folder) {
  std::error_code ec;
  if (!fs::exists(folder, ec)) return true;
  return fs::is_directory(folder, ec) && fs::directory_iterator(folder, ec) == fs::directory_iterator();
}

bool IsInside(const fs::path& inner, const fs::path& outer) {
  const fs::path a = Absolute(inner), b = Absolute(outer);
  auto ai = a.begin();
  for (auto it = b.begin(); it != b.end(); ++it) {
    if (it->empty()) continue;
    if (ai == a.end()) return false;
#ifdef _WIN32
    std::wstring x = ai->wstring(), y = it->wstring();
    std::transform(x.begin(), x.end(), x.begin(), ::towlower);
    std::transform(y.begin(), y.end(), y.begin(), ::towlower);
    if (x != y) return false;
#else
    if (*ai != *it) return false;
#endif
    ++ai;
  }
  return true;
}

bool Copy(const fs::path& from, const fs::path& to, const std::atomic<bool>& stop,
          const std::function<void(uint64_t, uint64_t)>& progress, std::string& error) {
  std::error_code ec;
  if (!fs::is_directory(from, ec)) {
    error = "The folder to copy from isn't there: " + Utf8(from);
    return false;
  }
  if (IsInside(to, from) || IsInside(from, to)) {
    error = "The two folders are inside each other (" + Utf8(from) + " and " + Utf8(to) + "); nothing was copied.";
    return false;
  }
  if (!IsEmpty(to)) {
    error = "The folder " + Utf8(to) +
            " already has something in it, so nothing was copied (a copy never mixes with older files). "
            "Empty it or choose another place.";
    return false;
  }
  const bool madeTarget = !fs::exists(to, ec);
  fs::create_directories(to, ec);
  if (ec || !fs::is_directory(to, ec)) {
    error = "Can't make the folder " + Utf8(to) + (ec ? " (" + ec.message() + ")" : std::string());
    return false;
  }
  const auto undo = [&] {
    std::error_code e;
    if (madeTarget) {
      fs::remove_all(to, e);
    } else {
      std::vector<fs::path> entries;
      for (const fs::directory_entry& entry : fs::directory_iterator(to, e)) entries.push_back(entry.path());
      for (const fs::path& entry : entries) fs::remove_all(entry, e);
    }
  };
  uint64_t files = 0, bytes = 0;
  for (fs::recursive_directory_iterator it(from, fs::directory_options::skip_permission_denied, ec), end;
       !ec && it != end; it.increment(ec)) {
    if (stop.load()) {
      undo();
      error = "Stopped. Nothing was moved; your data is where it was.";
      return false;
    }
    std::error_code e2;
    const fs::path target = to / fs::relative(it->path(), from, e2);
    if (it->is_directory(e2) && !it->is_symlink(e2)) {
      fs::create_directories(target, e2);
      if (e2) {
        undo();
        error = "Can't make the folder " + Utf8(target) + " (" + e2.message() + ")";
        return false;
      }
    } else if (it->is_regular_file(e2)) {
      fs::create_directories(target.parent_path(), e2);
      fs::copy_file(it->path(), target, fs::copy_options::none, e2);
      if (e2) {
        undo();
        error = "Can't copy " + Utf8(it->path()) + " (" + e2.message() + "). Nothing was moved; your data is where it was.";
        return false;
      }
      files++;
      bytes += it->file_size(e2);
      if (progress && (files % 20 == 0)) progress(files, bytes);
    }
  }
  if (ec) {
    undo();
    error = "Couldn't read " + Utf8(from) + " (" + ec.message() + "). Nothing was moved; your data is where it was.";
    return false;
  }
  if (progress) progress(files, bytes);
  return true;
}

bool Verify(const fs::path& from, const fs::path& to, std::string& error) {
  std::error_code ec;
  for (fs::recursive_directory_iterator it(from, fs::directory_options::skip_permission_denied, ec), end;
       !ec && it != end; it.increment(ec)) {
    std::error_code e2;
    if (!it->is_regular_file(e2)) continue;
    const fs::path target = to / fs::relative(it->path(), from, e2);
    if (!fs::is_regular_file(target, e2) || fs::file_size(target, e2) != it->file_size(e2)) {
      error = "The copy of " + Utf8(it->path()) + " is missing or a different size.";
      return false;
    }
    if (MustMatchExactly(it->path()) && !SameBytes(it->path(), target)) {
      error = "The copy of " + Utf8(it->path()) + " is not the same as the original.";
      return false;
    }
  }
  if (ec) {
    error = "Couldn't read " + Utf8(from) + " to check the copy (" + ec.message() + ").";
    return false;
  }
  return true;
}

bool RemoveContents(const fs::path& folder, std::vector<fs::path>& left, std::string& error) {
  std::error_code ec;
  std::vector<fs::path> entries;
  for (const fs::directory_entry& entry : fs::directory_iterator(folder, ec)) entries.push_back(entry.path());
  for (const fs::path& entry : entries) {
    std::error_code e2;
    fs::remove_all(entry, e2);
    if (e2 || fs::exists(entry, e2)) {
      bool listed = false;
      std::error_code e3;
      for (fs::recursive_directory_iterator it(entry, e3), end; !e3 && it != end; it.increment(e3)) {
        std::error_code e4;
        if (!it->is_directory(e4)) {
          left.push_back(it->path());
          listed = true;
        }
      }
      if (!listed) left.push_back(entry);
    }
  }
  if (!left.empty()) {
    error = std::to_string(left.size()) + " file(s) could not be removed (in use?), first: " + Utf8(left.front());
    return false;
  }
  return true;
}

}
