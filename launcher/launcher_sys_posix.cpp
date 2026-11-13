#include "launcher_sys.h"

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_stdinc.h>

#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <thread>

#include <fcntl.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdint>

#if defined(__APPLE__)
#include <crt_externs.h>
#include <mach-o/dyld.h>
#define LAUNCHER_ENVIRON (*_NSGetEnviron())
#else
extern char** environ;
#define LAUNCHER_ENVIRON environ
#endif

namespace launcher::sys {

namespace fs = std::filesystem;

namespace {

std::string Utf8(const fs::path& p) { return p.string(); }

fs::path Home() {
  const char* home = std::getenv("HOME");
  return home != nullptr && home[0] != '\0' ? fs::path(home) : fs::path();
}

}

void Start() {}
void End() {}

fs::path ProgramFile() {
#if defined(__APPLE__)
  char buf[4096];
  uint32_t size = sizeof buf;
  if (_NSGetExecutablePath(buf, &size) == 0) {
    std::error_code ec;
    const fs::path real = fs::canonical(buf, ec);
    return ec ? fs::path(buf) : real;
  }
  return {};
#else
  char buf[4096];
  const ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
  return n > 0 ? fs::path(std::string(buf, static_cast<size_t>(n))) : fs::path();
#endif
}

fs::path ProgramFolder() { return ProgramFile().parent_path(); }

std::FILE* OpenFile(const fs::path& file, const char* mode) { return std::fopen(file.c_str(), mode); }

fs::path EnvPath(const char* name) {
  const char* value = std::getenv(name);
  return value != nullptr && value[0] != '\0' ? fs::path(value) : fs::path();
}

std::string ProgramName(const std::string& stem) { return stem; }

std::vector<FontSet> SystemFonts() {
#if defined(__APPLE__)
  return {{"/System/Library/Fonts/Supplemental/Arial.ttf", "/System/Library/Fonts/Supplemental/Arial Bold.ttf",
           "/System/Library/Fonts/Supplemental/Arial Bold.ttf"}};
#else
  const fs::path tt = "/usr/share/fonts/truetype", f = "/usr/share/fonts";
  return {
      {tt / "dejavu/DejaVuSans.ttf", tt / "dejavu/DejaVuSans-Bold.ttf", tt / "dejavu/DejaVuSans-Bold.ttf"},
      {tt / "liberation/LiberationSans-Regular.ttf", tt / "liberation/LiberationSans-Bold.ttf",
       tt / "liberation/LiberationSans-Bold.ttf"},
      {tt / "noto/NotoSans-Regular.ttf", tt / "noto/NotoSans-Bold.ttf", tt / "noto/NotoSans-Bold.ttf"},
      {f / "TTF/DejaVuSans.ttf", f / "TTF/DejaVuSans-Bold.ttf", f / "TTF/DejaVuSans-Bold.ttf"},
      {f / "dejavu/DejaVuSans.ttf", f / "dejavu/DejaVuSans-Bold.ttf", f / "dejavu/DejaVuSans-Bold.ttf"},
      {f / "dejavu-sans-fonts/DejaVuSans.ttf", f / "dejavu-sans-fonts/DejaVuSans-Bold.ttf",
       f / "dejavu-sans-fonts/DejaVuSans-Bold.ttf"},
  };
#endif
}

struct Process {
  pid_t pid = -1;
  bool ended = false;
  int code = 1;
  bool tree = false;
};

namespace {

void Look(Process* p, bool block) {
  if (p->ended) {
    return;
  }
  int status = 0;
  pid_t r;
  do {
    r = waitpid(p->pid, &status, block ? 0 : WNOHANG);
  } while (r < 0 && errno == EINTR);
  if (r == p->pid) {
    p->ended = true;
    p->code = WIFEXITED(status) ? WEXITSTATUS(status) : WIFSIGNALED(status) ? 128 + WTERMSIG(status) : 1;
  } else if (r < 0) {
    p->ended = true;
    p->code = 1;
  }
}

}

Process* StartProgram(const StartRequest& request, std::string& error) {
  const std::string program = request.program.string();
  std::vector<std::string> args{program};
  args.insert(args.end(), request.arguments.begin(), request.arguments.end());
  std::vector<char*> argv;
  for (std::string& a : args) {
    argv.push_back(a.data());
  }
  argv.push_back(nullptr);
  std::vector<std::string> env;
  std::vector<char*> envp;
  if (request.ownEnvironment) {
    for (char** e = LAUNCHER_ENVIRON; e != nullptr && *e != nullptr; ++e) {
      bool keep = true;
      for (const std::string& without : request.environmentWithout) {
        if (std::strncmp(*e, without.c_str(), without.size()) == 0) {
          keep = false;
        }
      }
      for (const std::string& add : request.environmentAdd) {
        const size_t name = add.find('=');
        if (name != std::string::npos && std::strncmp(*e, add.c_str(), name + 1) == 0) {
          keep = false;
        }
      }
      if (keep) {
        env.emplace_back(*e);
      }
    }
    env.insert(env.end(), request.environmentAdd.begin(), request.environmentAdd.end());
    for (std::string& e : env) {
      envp.push_back(e.data());
    }
    envp.push_back(nullptr);
  }
  const std::string folder = request.folder.string();
  const std::string outputFile = request.outputFile.string();
  int outFd = -1;
  if (!outputFile.empty()) {
    outFd = open(outputFile.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (outFd < 0) {
      error = std::string("its output file: ") + std::strerror(errno);
      return nullptr;
    }
  }
  int fds[2];
  if (pipe(fds) != 0) {
    error = std::string("no pipe: ") + std::strerror(errno);
    return nullptr;
  }
  fcntl(fds[0], F_SETFD, FD_CLOEXEC);
  fcntl(fds[1], F_SETFD, FD_CLOEXEC);
  const pid_t pid = fork();
  if (pid < 0) {
    error = std::string("the system didn't start a new process: ") + std::strerror(errno);
    close(fds[0]);
    close(fds[1]);
    if (outFd >= 0) close(outFd);
    return nullptr;
  }
  if (pid == 0) {
    int reason = 0;
    if (request.killTree) {
      setsid();
    }
    if (!folder.empty() && chdir(folder.c_str()) != 0) {
      reason = errno;
    }
    if (reason == 0) {
      if (outFd >= 0) {
        const int null = open("/dev/null", O_RDONLY);
        if (null >= 0) dup2(null, 0);
        dup2(outFd, 1);
        dup2(outFd, 2);
      } else if (!request.shareOutput) {
        const int null = open("/dev/null", O_RDWR);
        if (null >= 0) {
          dup2(null, 0);
          dup2(null, 1);
          dup2(null, 2);
        }
      }
      if (request.lowPriority) {
        setpriority(PRIO_PROCESS, 0, 10);
      }
      if (request.ownEnvironment) {
        execve(argv[0], argv.data(), envp.data());
      } else {
        execvp(argv[0], argv.data());
      }
      reason = errno;
    }
    const ssize_t ignored = write(fds[1], &reason, sizeof reason);
    (void)ignored;
    _exit(127);
  }
  close(fds[1]);
  if (outFd >= 0) close(outFd);
  int reason = 0;
  ssize_t got;
  do {
    got = read(fds[0], &reason, sizeof reason);
  } while (got < 0 && errno == EINTR);
  close(fds[0]);
  if (got == static_cast<ssize_t>(sizeof reason)) {
    int status = 0;
    waitpid(pid, &status, 0);
    error = std::strerror(reason);
    return nullptr;
  }
  Process* p = new Process;
  p->pid = pid;
  p->tree = request.killTree;
  return p;
}

bool Ended(Process* process, int waitMs) {
  if (process == nullptr) {
    return true;
  }
  const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(waitMs);
  for (;;) {
    Look(process, false);
    if (process->ended || std::chrono::steady_clock::now() >= until) {
      return process->ended;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
}

int ExitCode(Process* process) { return process != nullptr ? process->code : 1; }

unsigned long IdOf(Process* process) { return process != nullptr ? static_cast<unsigned long>(process->pid) : 0; }

void Stop(Process* process) {
  if (process == nullptr || process->ended) {
    return;
  }
  if (process->tree) {
    kill(-process->pid, SIGKILL);
  }
  kill(process->pid, SIGKILL);
  Ended(process, 5000);
}

void Close(Process* process) {
  if (process == nullptr) {
    return;
  }
  if (!process->ended) {
    Look(process, false);
  }
  if (!process->ended) {
    const pid_t pid = process->pid;
    std::thread([pid] {
      int status = 0;
      while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
      }
    }).detach();
  }
  delete process;
}

bool OpenInSystem(const fs::path& what) {
  StartRequest r;
#if defined(__APPLE__)
  r.program = "open";
#else
  r.program = "xdg-open";
#endif
  r.arguments = {Utf8(what)};
  std::string error;
  Process* p = StartProgram(r, error);
  if (p == nullptr) {
    return false;
  }
  const bool failed = Ended(p, 400) && ExitCode(p) != 0;
  Close(p);
  return !failed;
}

fs::path KnownFolder(Folder which, bool create) {
  fs::path folder;
  if (which == Folder::Programs) {
#if defined(__APPLE__)
    return {};
#else
    const char* data = std::getenv("XDG_DATA_HOME");
    const fs::path home = Home();
    if (data != nullptr && data[0] == '/') {
      folder = fs::path(data) / "applications";
    } else if (!home.empty()) {
      folder = home / ".local/share/applications";
    }
#endif
  } else if (const char* known =
                 SDL_GetUserFolder(which == Folder::Documents ? SDL_FOLDER_DOCUMENTS : SDL_FOLDER_DESKTOP)) {
    folder = known;
  } else if (which == Folder::Documents) {
    folder = Home();
  }
  std::error_code ec;
  if (create && !folder.empty()) {
    fs::create_directories(folder, ec);
  }
  return folder;
}

const char* RecycleBinName() { return "the trash"; }
const char* UserNameWords() { return "user name"; }
#if defined(__APPLE__)
const char* HomeFolderExample() { return "/Users/<you>/..."; }
#else
const char* HomeFolderExample() { return "/home/<you>/..."; }
#endif

bool ToRecycleBin(const fs::path& folder, std::string& error) {
#if defined(__APPLE__)
  std::error_code ec;
  const fs::path absolute = fs::absolute(folder, ec);
  {
    StartRequest r;
    r.program = "/usr/bin/osascript";
    r.arguments = {"-e", "on run argv", "-e", "tell application \"Finder\" to delete (POSIX file (item 1 of argv))",
                   "-e", "end run", Utf8(absolute)};
    std::string why;
    if (Process* p = StartProgram(r, why)) {
      const bool done = Ended(p, 60000);
      if (!done) {
        Stop(p);
      }
      Close(p);
      if (done && !fs::exists(absolute, ec)) {
        return true;
      }
    }
  }
  const fs::path home = Home();
  if (!home.empty() && fs::exists(home / ".Trash", ec)) {
    const std::string base = Utf8(folder.filename());
    for (int n = 0; n < 1000; n++) {
      const fs::path target = home / ".Trash" / (n == 0 ? base : base + " " + std::to_string(n));
      if (fs::exists(target, ec)) {
        continue;
      }
      if (rename(folder.c_str(), target.c_str()) == 0) {
        return true;
      }
      break;
    }
  }
  error = "It couldn't be moved to the trash (the Finder did not do it, and the Trash folder can't be written "
          "unless this app may control the Finder: System Settings > Privacy & Security > Automation): " +
          Utf8(folder);
  return false;
#else
  std::error_code ec;
  const char* data = std::getenv("XDG_DATA_HOME");
  const fs::path home = Home();
  fs::path trash;
  if (data != nullptr && data[0] == '/') {
    trash = fs::path(data) / "Trash";
  } else if (!home.empty()) {
    trash = home / ".local/share/Trash";
  }
  if (trash.empty()) {
    error = "The system didn't say where your home folder is, so there is no trash to move it to: " + Utf8(folder);
    return false;
  }
  fs::create_directories(trash / "files", ec);
  fs::create_directories(trash / "info", ec);
  const fs::path absolute = fs::absolute(folder, ec);
  const std::string base = Utf8(folder.filename());
  for (int n = 0; n < 1000; n++) {
    const std::string name = n == 0 ? base : base + "." + std::to_string(n);
    const fs::path info = trash / "info" / (name + ".trashinfo");
    const int fd = open(info.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (fd < 0) {
      if (errno == EEXIST) {
        continue;
      }
      error = "The trash folder can't be written (" + std::string(std::strerror(errno)) + "): " + Utf8(trash);
      return false;
    }
    std::string where;
    for (const unsigned char c : Utf8(absolute)) {
      if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '/' || c == '-' ||
          c == '_' || c == '.' || c == '~') {
        where += static_cast<char>(c);
      } else {
        char hex[4];
        std::snprintf(hex, sizeof hex, "%%%02X", c);
        where += hex;
      }
    }
    char when[32] = "";
    const std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_r(&now, &local);
    std::strftime(when, sizeof when, "%Y-%m-%dT%H:%M:%S", &local);
    const std::string text = "[Trash Info]\nPath=" + where + "\nDeletionDate=" + when + "\n";
    const bool written = write(fd, text.data(), text.size()) == static_cast<ssize_t>(text.size());
    close(fd);
    if (written && rename(folder.c_str(), (trash / "files" / name).c_str()) == 0) {
      return true;
    }
    const int reason = errno;
    fs::remove(info, ec);
    error = (reason == EXDEV ? std::string("The trash is on another drive than your saves, so it wasn't moved")
                             : "It couldn't be moved to the trash (" + std::string(std::strerror(reason)) + ")") +
            ": " + Utf8(folder);
    return false;
  }
  error = "The trash already holds too many things of that name: " + Utf8(folder);
  return false;
#endif
}

const char* ProgramsFolderName() { return "applications menu"; }
#if defined(__APPLE__)
bool ShortcutsHere() { return false; }
#else
bool ShortcutsHere() { return true; }
#endif
bool HasIcons() { return false; }

bool MakeShortcut(const fs::path& folder, const std::string& name, const std::string& description,
                  const fs::path& target, const fs::path& iconFile, std::string& message) {
#if defined(__APPLE__)
  (void)folder, (void)name, (void)description, (void)target, (void)iconFile;
  message = "On the Mac the app is its own shortcut: drag Pokemon XD to the Dock or into Applications.";
  return false;
#else
  std::error_code ec;
  fs::create_directories(folder, ec);
  const fs::path link = folder / (name + ".desktop");
  std::string exec = "\"";
  for (const char c : Utf8(target)) {
    if (c == '"' || c == '`' || c == '$' || c == '\\') {
      exec += '\\';
    }
    exec += c;
  }
  exec += '"';
  fs::path picture = iconFile;
  if (picture.extension() != ".png" || !fs::exists(picture, ec)) {
    picture = ProgramFolder() / "icon.png";
  }
  std::string text = "[Desktop Entry]\nType=Application\nName=" + name + "\nComment=" + description + "\nExec=" +
                     exec + "\nPath=" + Utf8(target.parent_path()) + "\nTerminal=false\nCategories=Game;\n";
  if (fs::exists(picture, ec)) {
    text += "Icon=" + Utf8(picture) + "\n";
  }
  std::FILE* f = std::fopen(link.c_str(), "wb");
  const bool ok = f != nullptr && std::fwrite(text.data(), 1, text.size(), f) == text.size();
  if (f != nullptr && (std::fclose(f) != 0 || !ok)) {
    fs::remove(link, ec);
    f = nullptr;
  }
  if (f == nullptr) {
    message = "The shortcut couldn't be written to " + Utf8(folder) + " (" + std::strerror(errno) + ").";
    return false;
  }
  chmod(link.c_str(), 0755);
  if (folder == KnownFolder(Folder::Desktop, false)) {
    const pid_t pid = fork();
    if (pid == 0) {
      const int null = open("/dev/null", O_RDWR);
      if (null >= 0) {
        dup2(null, 0);
        dup2(null, 1);
        dup2(null, 2);
      }
      execlp("gio", "gio", "set", link.c_str(), "metadata::trusted", "true", static_cast<char*>(nullptr));
      _exit(127);
    } else if (pid > 0) {
      int status = 0;
      for (int waited = 0; waitpid(pid, &status, WNOHANG) == 0 && waited < 3000; waited += 20) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
      }
    }
  }
  message = "Shortcut made: " + Utf8(link) +
            ". If your desktop shows it with a warning sign, right-click it and choose \"Allow launching\".";
  return true;
#endif
}

icon::Image ReadIcoSize(const fs::path&, int) { return {}; }

}
