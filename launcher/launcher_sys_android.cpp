#include "launcher_sys.h"

#include "i18n.h"

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_system.h>

#include <cerrno>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

namespace launcher::sys {

namespace fs = std::filesystem;

namespace {

fs::path InternalFolder() {
  const char* internal = SDL_GetAndroidInternalStoragePath();
  return internal != nullptr ? fs::path(internal) : fs::path("/data/data/net.pokemonxdpc.port/files");
}

void CopyAsset(const char* assetName, const fs::path& target) {
  size_t size = 0;
  void* data = SDL_LoadFile(assetName, &size);
  if (data == nullptr) {
    return;
  }
  std::error_code ec;
  if (!(fs::exists(target, ec) && fs::file_size(target, ec) == size)) {
    fs::create_directories(target.parent_path(), ec);
    if (std::FILE* f = std::fopen(target.c_str(), "wb")) {
      std::fwrite(data, 1, size, f);
      std::fclose(f);
    }
  }
  SDL_free(data);
}

}

void Start() {
  const fs::path fonts = ProgramFolder() / "fonts";
  static const char* const kFiles[] = {"Inter-Regular.ttf", "Inter-SemiBold.ttf", "InterDisplay-Bold.ttf", "NotoSansJP-Regular-subset.ttf"};
  for (const char* name : kFiles) {
    CopyAsset((std::string("fonts/") + name).c_str(), fonts / name);
  }
}
void End() {}

fs::path ProgramFile() { return ProgramFolder() / "xd"; }
fs::path ProgramFolder() {
  const fs::path folder = InternalFolder() / "program";
  std::error_code ec;
  fs::create_directories(folder, ec);
  return folder;
}

std::FILE* OpenFile(const fs::path& file, const char* mode) { return std::fopen(file.c_str(), mode); }

fs::path EnvPath(const char* name) {
  const char* value = std::getenv(name);
  return value != nullptr && value[0] != '\0' ? fs::path(value) : fs::path();
}

std::string ProgramName(const std::string& stem) { return stem; }

std::vector<FontSet> SystemFonts() {
  const fs::path f = "/system/fonts";
  return {{f / "Roboto-Regular.ttf", f / "Roboto-Medium.ttf", f / "Roboto-Bold.ttf"},
          {f / "DroidSans.ttf", f / "DroidSans-Bold.ttf", f / "DroidSans-Bold.ttf"}};
}

namespace {
BuiltinPrograms sBuiltin;
std::string ProgramStem(const fs::path& program) {
  std::string name = program.filename().string();
  if (name.size() > 4 && name.compare(name.size() - 4, 4, ".exe") == 0) name.resize(name.size() - 4);
  return name;
}
}

void SetBuiltinPrograms(const BuiltinPrograms& programs) { sBuiltin = programs; }
bool HasBuiltinProgram(const fs::path& program) { return sBuiltin.has != nullptr && sBuiltin.has(ProgramStem(program)); }

struct Process {
  struct State {
    std::mutex mutex;
    std::condition_variable changed;
    bool ended = false;
    int code = 1;
  };
  std::shared_ptr<State> state;
  std::thread thread;
};

Process* StartProgram(const StartRequest& request, std::string& error) {
  if (!HasBuiltinProgram(request.program)) {
    error = engine::i18n::Tr("a phone app cannot start another program");
    return nullptr;
  }
  auto* p = new Process;
  p->state = std::make_shared<Process::State>();
  p->thread = std::thread([state = p->state, name = ProgramStem(request.program), arguments = request.arguments, out = request.outputFile] {
    int code = 1;
    try {
      code = sBuiltin.run(name, arguments, out);
    } catch (...) {
      code = 1;
    }
    std::lock_guard<std::mutex> lock(state->mutex);
    state->code = code;
    state->ended = true;
    state->changed.notify_all();
  });
  return p;
}
bool Ended(Process* p, int waitMs) {
  if (p == nullptr) return true;
  std::unique_lock<std::mutex> lock(p->state->mutex);
  if (!p->state->ended && waitMs > 0) {
    p->state->changed.wait_for(lock, std::chrono::milliseconds(waitMs), [&] { return p->state->ended; });
  }
  return p->state->ended;
}
int ExitCode(Process* p) { return p != nullptr ? p->state->code : 1; }
unsigned long IdOf(Process*) { return 0; }
void Stop(Process*) {}
void Close(Process* p) {
  if (p == nullptr) return;
  if (p->thread.joinable()) {
    bool ended;
    {
      std::lock_guard<std::mutex> lock(p->state->mutex);
      ended = p->state->ended;
    }
    if (ended) p->thread.join();
    else p->thread.detach();
  }
  delete p;
}

bool OpenInSystem(const fs::path&) { return false; }

fs::path KnownFolder(Folder which, bool create) {
  if (which == Folder::Programs) {
    return {};
  }
  const fs::path folder = InternalFolder() / "exports";
  std::error_code ec;
  if (create) {
    fs::create_directories(folder, ec);
  }
  return folder;
}

const char* RecycleBinName() { return "the app's trash folder"; }
const char* UserNameWords() { return "user name"; }
const char* HomeFolderExample() { return "/storage/emulated/0/..."; }

bool ToRecycleBin(const fs::path& folder, std::string& error) {
  std::error_code ec;
  const fs::path trash = InternalFolder() / "trash";
  fs::create_directories(trash, ec);
  const std::string base = folder.filename().string();
  const std::string stamp = std::to_string(static_cast<long long>(std::time(nullptr)));
  const fs::path target = trash / (base + "." + stamp);
  fs::rename(folder, target, ec);
  if (ec) {
    error = std::string(engine::i18n::Tr("It couldn't be moved aside:")) + " " + ec.message();
    return false;
  }
  return true;
}

const char* ProgramsFolderName() { return "home screen"; }
bool ShortcutsHere() { return false; }
bool HasIcons() { return false; }

bool MakeShortcut(const fs::path&, const std::string&, const std::string&, const fs::path&, const fs::path&, std::string& message) {
  message = engine::i18n::Tr("The app is its own shortcut on a phone.");
  return false;
}

icon::Image ReadIcoSize(const fs::path&, int) { return {}; }

}
