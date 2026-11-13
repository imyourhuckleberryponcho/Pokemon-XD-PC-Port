#include "update_internal.h"

#if !defined(_WIN32)
#include <sys/stat.h>
#include <unistd.h>
#endif

#include <cstdio>

namespace engine::update::detail {

namespace fs = std::filesystem;

std::string Utf8(const fs::path& p) {
  const std::u8string u8 = p.u8string();
  return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
}

fs::path PathOf(const std::string& utf8) {
  return fs::path(std::u8string(reinterpret_cast<const char8_t*>(utf8.data()), utf8.size()));
}

bool SafeName(std::string name, std::string& out) {
  for (char& c : name) {
    if (c == '\\') {
      c = '/';
    }
  }
  out.clear();
  if (name.empty() || name.front() == '/' || name.find(':') != std::string::npos) {
    return false;
  }
  size_t start = 0;
  while (start < name.size()) {
    size_t end = name.find('/', start);
    if (end == std::string::npos) {
      end = name.size();
    }
    const std::string part = name.substr(start, end - start);
    if (part.empty() || part == "." || part == "..") {
      return false;
    }
    for (unsigned char c : part) {
      if (c < 0x20 || c == '<' || c == '>' || c == '"' || c == '|' || c == '?' || c == '*') {
        return false;
      }
    }
    if (part.back() == '.' || part.back() == ' ') {
      return false;
    }
    if (!out.empty()) {
      out += '/';
    }
    out += part;
    start = end + 1;
  }
  return !out.empty();
}

bool RemoveFolder(const fs::path& folder) {
  std::error_code ec;
  fs::remove_all(folder, ec);
  return !fs::exists(folder, ec) && !fs::is_symlink(folder, ec);
}

bool HoldsPlayerData(const fs::path& folder) {
  std::error_code ec;
  return fs::exists(folder / "data_location.json", ec) || fs::exists(folder / "portable", ec);
}

bool FinishUnpackedFile(const fs::path& file, uint32_t unixMode) {
#if defined(_WIN32)
  (void)file, (void)unixMode;
  return true;
#else
  if (unixMode == 0) {
    return true;
  }
  const mode_t mode = static_cast<mode_t>((unixMode & 0777) | S_IRUSR | S_IWUSR);
  return ::chmod(file.c_str(), mode) == 0;
#endif
}

bool AncestorIsLink(const fs::path& root, const std::string& relative) {
  std::error_code ec;
  fs::path walked = root;
  size_t start = 0;
  while (true) {
    const size_t slash = relative.find('/', start);
    if (slash == std::string::npos) {
      return false;
    }
    walked /= PathOf(relative.substr(start, slash - start));
    if (fs::is_symlink(walked, ec)) {
      return true;
    }
    start = slash + 1;
  }
}

bool MakeUnpackedLink(const fs::path& root, const std::string& relative, const std::string& target,
                      std::string& error) {
#if defined(_WIN32)
  (void)root, (void)relative, (void)target;
  error = "the package holds a link, which this system doesn't take: " + relative;
  return false;
#else
  std::string safe;
  if (target.empty() || target.size() > 1024 || target.front() == '/' || target.find('\0') != std::string::npos) {
    error = "the package holds a link that isn't allowed: " + relative;
    return false;
  }
  int depth = 0;
  for (size_t i = 0, slashes = 0; i < relative.size(); i++) {
    if (relative[i] == '/') {
      slashes++;
      depth = static_cast<int>(slashes);
    }
  }
  size_t start = 0;
  while (start <= target.size()) {
    size_t end = target.find('/', start);
    if (end == std::string::npos) {
      end = target.size();
    }
    const std::string part = target.substr(start, end - start);
    if (part == "..") {
      if (--depth < 0) {
        error = "the package holds a link that leaves its folder: " + relative;
        return false;
      }
    } else if (!part.empty() && part != ".") {
      depth++;
    }
    start = end + 1;
  }
  std::error_code ec;
  fs::create_symlink(PathOf(target), root / PathOf(relative), ec);
  if (ec) {
    error = "a link couldn't be made: " + relative + " (" + ec.message() + ")";
    return false;
  }
  return true;
#endif
}

std::string ReadSmallFile(const fs::path& file) {
  std::string out;
  if (std::FILE* f = std::fopen(file.string().c_str(), "rb")) {
    char buffer[2048];
    const size_t n = std::fread(buffer, 1, sizeof buffer, f);
    out.assign(buffer, n);
    std::fclose(f);
  }
  return out;
}

}
