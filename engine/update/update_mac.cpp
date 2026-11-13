#include "update_internal.h"

#if !defined(_WIN32)
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace engine::update {

namespace fs = std::filesystem;

namespace {

constexpr const char* kStateName = ".Pokemon XD updates";

std::string Quote(const std::string& text) {
  std::string out = "'";
  for (char c : text) {
    if (c == '\'') {
      out += "'\\''";
    } else {
      out += c;
    }
  }
  return out + "'";
}

bool AppUsable(const fs::path& app, const std::vector<std::string>& required) {
  return FolderUsable(app / "Contents" / "MacOS", required);
}

}

fs::path MacStateFolder(const Layout& l) { return l.root / kStateName; }

std::string MacHelperScript(const fs::path& app, const fs::path& pending, const fs::path& previous, long waitForPid,
                            bool openAfter) {
  const std::string a = Quote(detail::Utf8(app)), n = Quote(detail::Utf8(pending)), p = Quote(detail::Utf8(previous));
  std::string s = "#!/bin/sh\n"
                  "# Written by the launcher's updater (docs/UPDATER.md): puts a new Pokemon XD.app in place once the\n"
                  "# launcher has ended, and opens it. Any failure puts the old app back.\n";
  s += "PID=" + std::to_string(waitForPid) + "\nAPP=" + a + "\nPENDING=" + n + "\nPREV=" + p + "\n";
  s += "i=0\n"
       "while kill -0 \"$PID\" 2>/dev/null && [ \"$i\" -lt 120 ]; do sleep 1; i=$((i+1)); done\n"
       "if kill -0 \"$PID\" 2>/dev/null; then exit 1; fi\n"
       "[ -d \"$PENDING\" ] || exit 1\n"
       "mkdir -p \"$(dirname \"$PREV\")\" || exit 1\n"
       "rm -rf \"$PREV\"\n"
       "if mv \"$APP\" \"$PREV\"; then\n"
       "  if ! mv \"$PENDING\" \"$APP\"; then mv \"$PREV\" \"$APP\"; fi\n"
       "fi\n"
       "rmdir \"$(dirname \"$PENDING\")\" 2>/dev/null\n"
       "rm -rf \"$(dirname \"$PENDING\")\"\n"
       "rm -f \"$0\"\n";
  if (openAfter) {
    s += "[ -d \"$APP\" ] && open \"$APP\"\n";
  }
  return s;
}

namespace detail {

bool LooksLikeMacApp(const fs::path& programFolder) {
  const fs::path macos = programFolder.filename() == "" ? programFolder.parent_path() : programFolder;
  const fs::path contents = macos.parent_path();
  const fs::path app = contents.parent_path();
  return macos.filename() == "MacOS" && contents.filename() == "Contents" && app.extension() == ".app";
}

Layout FindMacLayout(const fs::path& programFolder) {
  Layout l;
  std::error_code ec;
  const fs::path macos = fs::weakly_canonical(programFolder, ec);
  l.macApp = true;
  l.appPath = macos.parent_path().parent_path();
  l.root = l.appPath.parent_path();
  l.running = "app";
  const std::string where = Utf8(l.appPath);
  if (where.find("/AppTranslocation/") != std::string::npos || where.rfind("/Volumes/", 0) == 0) {
    l.why = "This copy runs from a disk image or from a temporary place the Mac made for it (" + where +
            "). Drag Pokemon XD to your Applications folder (or any folder of yours), open it from there, and "
            "updates can be installed.";
    l.root.clear();
    return l;
  }
#if !defined(_WIN32)
  if (::access(l.root.c_str(), W_OK | X_OK) != 0) {
    l.why = "Pokemon XD is in a folder this account can't write to (" + Utf8(l.root) + "), so an update can't be put in "
            "place. Move the app into a folder of yours (or install the new version by hand).";
    l.root.clear();
    return l;
  }
#endif
  l.valid = true;
  const fs::path state = l.root / kStateName;
  const fs::path name = l.appPath.filename();
  const bool pending = fs::is_directory(state / "pending" / name, ec);
  const bool previous = fs::is_directory(state / "previous" / name, ec);
  l.current = pending ? "pending" : "app";
  l.previous = pending ? "app" : (previous ? "previous" : "");
  return l;
}

fs::path MacProgramFolder(const Layout& l, const std::string& name) {
  const fs::path state = l.root / kStateName;
  const fs::path app = l.appPath.filename();
  if (name == "pending") {
    return state / "pending" / app / "Contents" / "MacOS";
  }
  if (name == "previous") {
    return state / "previous" / app / "Contents" / "MacOS";
  }
  return l.appPath / "Contents" / "MacOS";
}

bool InstallMacApp(const InstallRequest& r, std::string& installed, std::string& error,
                   const std::function<bool()>& stop) {
  installed.clear();
  const Layout& l = r.layout;
  std::error_code ec;
  const fs::path state = l.root / kStateName;
  const fs::path partial = state / "pending.partial";
  const fs::path pending = state / "pending";
  const fs::path name = l.appPath.filename();
  const auto fail = [&](const std::string& why) {
    error = why;
    RemoveFolder(partial);
    return false;
  };
  if (!RemoveFolder(partial)) {
    error = "an earlier unfinished update couldn't be removed: " + Utf8(partial);
    return false;
  }
  fs::create_directories(state, ec);
  std::string why;
  if (!ExtractZip(r.package, partial, why, stop)) {
    return fail(why);
  }
  fs::path found;
  int apps = 0;
  for (const auto& e : fs::directory_iterator(partial, ec)) {
    if (e.is_directory(ec) && e.path().extension() == ".app") {
      found = e.path();
      apps++;
    } else {
      return fail("the package holds something besides the app: " + Utf8(e.path().filename()));
    }
  }
  if (apps != 1) {
    return fail("the package should hold exactly one app");
  }
  if (!AppUsable(found, r.requiredFiles)) {
    return fail("the package is incomplete (the app should hold the launcher and the game)");
  }
  const std::string version = FolderVersion(found / "Contents" / "MacOS");
  if (version != r.manifest.version) {
    return fail("the package holds version " + version + ", not " + r.manifest.version);
  }
  if (!RemoveFolder(pending)) {
    return fail("an earlier waiting update couldn't be removed");
  }
  fs::create_directories(pending, ec);
  fs::rename(found, pending / name, ec);
  if (ec) {
    RemoveFolder(pending);
    return fail("the new app couldn't be put in its waiting place: " + ec.message());
  }
  RemoveFolder(partial);
  installed = "pending";
  return true;
}

bool GoBackMac(const Layout& l, const std::vector<std::string>& requiredFiles, std::string& error) {
  std::error_code ec;
  const fs::path state = l.root / kStateName;
  if (l.current == "pending") {
    if (!RemoveFolder(state / "pending")) {
      error = "the waiting update couldn't be removed";
      return false;
    }
    return true;
  }
  if (l.previous != "previous" || !FolderUsable(MacProgramFolder(l, "previous"), requiredFiles)) {
    error = "there is no previous version to go back to";
    return false;
  }
  if (!RemoveFolder(state / "pending")) {
    error = "the waiting update couldn't be removed";
    return false;
  }
  fs::rename(state / "previous", state / "pending", ec);
  if (ec) {
    error = "the previous version couldn't be made ready: " + ec.message();
    return false;
  }
  return true;
}

}

bool MacApplyPending(const Layout& l, std::string& error) {
  error.clear();
  if (!l.macApp || !l.valid) {
    return false;
  }
  std::error_code ec;
  const fs::path state = MacStateFolder(l);
  const fs::path name = l.appPath.filename();
  const fs::path pending = state / "pending" / name;
  if (!fs::is_directory(pending, ec)) {
    return false;
  }
#if defined(_WIN32)
  return false;
#else
  const fs::path script = state / "swap.sh";
  const std::string text = MacHelperScript(l.appPath, pending, state / "previous" / name, static_cast<long>(::getpid()),
                                         std::getenv("XD_UPDATE_NO_OPEN") == nullptr);
  if (std::FILE* f = std::fopen(script.c_str(), "wb")) {
    const bool ok = std::fwrite(text.data(), 1, text.size(), f) == text.size();
    if (std::fclose(f) != 0 || !ok) {
      error = "the update helper couldn't be written";
      return false;
    }
  } else {
    error = "the update helper couldn't be written";
    return false;
  }
  const pid_t child = ::fork();
  if (child < 0) {
    error = "the update helper couldn't be started";
    return false;
  }
  if (child == 0) {
    ::setsid();
    const int null = ::open("/dev/null", O_RDWR);
    if (null >= 0) {
      ::dup2(null, 0);
      ::dup2(null, 1);
      ::dup2(null, 2);
    }
    ::execl("/bin/sh", "sh", script.c_str(), static_cast<char*>(nullptr));
    ::_exit(127);
  }
  return true;
#endif
}

}
