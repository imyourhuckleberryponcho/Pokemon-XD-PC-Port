#include "xd_builtin_converters.h"

#include "launcher_sys.h"

#include <port_android_fd.h>

#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_system.h>

#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

int pbr_models_entry(int argc, char** argv);
int colo_models_entry(int argc, char** argv);
int xd_languages_entry(int argc, char** argv);
int usum_models_entry(int argc, char** argv);
extern std::function<int(const std::string&, const std::vector<std::string>&, std::string&)> g_usum_builtin_run;

namespace fs = std::filesystem;

namespace xd {
namespace {

struct Program {
  const char* name;
  int (*entry)(int, char**);
};
const Program kPrograms[] = {
    {"pbr_models", pbr_models_entry},
    {"colo_models", colo_models_entry},
    {"xd_languages", xd_languages_entry},
    {"usum_models_cpp", usum_models_entry},
};

const Program* Find(const std::string& name) {
  for (const Program& p : kPrograms) {
    if (name == p.name) return &p;
  }
  return nullptr;
}

std::mutex sRunning;

fs::path DataFolder() {
  static const char* const kFiles[] = {"usum_gen4_plan.json",      "gen57_forms.json",  "gen57_model_numbers.json", "gen57_cry_waves.json",
                                       "gen57_speed_numbers.json", "usum_dex_map.json", "gen57_tables.json",        "gen4_tables.json"};
  const char* internal = SDL_GetAndroidInternalStoragePath();
  const fs::path folder = fs::path(internal != nullptr ? internal : "/data/data/net.pokemonxdpc.port/files") / "n3ds_data";
  std::error_code ec;
  fs::create_directories(folder, ec);
  for (const char* f : kFiles) {
    size_t size = 0;
    void* data = SDL_LoadFile((std::string("n3ds_data/") + f).c_str(), &size);
    if (data == nullptr) continue;
    const fs::path target = folder / f;
    if (!fs::exists(target, ec) || fs::file_size(target, ec) != size) {
      std::ofstream out(target, std::ios::binary | std::ios::trunc);
      out.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
    }
    SDL_free(data);
  }
  return folder;
}

int RunRedirected(const Program& program, std::vector<std::string> args, const fs::path& file) {
  std::fflush(stdout);
  std::fflush(stderr);
  const int savedOut = dup(1), savedErr = dup(2);
  const int target = open(file.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (target >= 0) {
    dup2(target, 1);
    dup2(target, 2);
    close(target);
  }
  std::vector<std::string> store;
  store.push_back(program.name);
  for (std::string& a : args) store.push_back(std::move(a));
  std::vector<char*> argv;
  for (std::string& s : store) argv.push_back(s.data());
  argv.push_back(nullptr);
  int code = 1;
  try {
    code = program.entry(static_cast<int>(store.size()), argv.data());
  } catch (const XdLanguagesExit& e) {
    code = e.code;
  } catch (const std::exception& e) {
    std::printf("error %s\n", e.what());
    code = 1;
  } catch (...) {
    std::printf("error the converter stopped unexpectedly\n");
    code = 1;
  }
  std::fflush(stdout);
  std::fflush(stderr);
  if (savedOut >= 0) {
    dup2(savedOut, 1);
    close(savedOut);
  }
  if (savedErr >= 0) {
    dup2(savedErr, 2);
    close(savedErr);
  }
  return code;
}

bool Has(const std::string& name) { return Find(name) != nullptr; }

int Run(const std::string& name, const std::vector<std::string>& arguments, const fs::path& outputFile) {
  const Program* program = Find(name);
  if (program == nullptr) return 1;
  const bool versionOnly = !arguments.empty() && arguments[0] == "--version";
  std::unique_lock<std::mutex> lock(sRunning, std::defer_lock);
  if (versionOnly) {
    if (!lock.try_lock()) return 1;
  } else {
    lock.lock();
  }
  std::vector<std::string> args = arguments;
  std::vector<int> openFds;
  for (size_t i = 0; i + 1 < args.size(); i++) {
    if (args[i] == "--rom" && args[i + 1].compare(0, 10, "content://") == 0) {
      int fd = -1;
      const std::string real = PortAndroid_ResolveContent(args[i + 1], fd);
      if (real.empty()) {
        std::ofstream(outputFile, std::ios::binary | std::ios::trunc) << "error The dump file can't be opened (the file window's permission is gone: choose it again).\n";
        return 1;
      }
      args[i + 1] = real;
      openFds.push_back(fd);
    }
  }
  if (name == "usum_models_cpp" && !versionOnly) {
    args.push_back("--data");
    args.push_back(DataFolder().string());
    args.push_back("--pbr-models");
    args.push_back("pbr_models");
    const unsigned hw = std::thread::hardware_concurrency();
    args.push_back("--jobs");
    args.push_back(std::to_string(std::max(2u, std::min(4u, hw))));
  }
  const int code = RunRedirected(*program, args, outputFile);
  for (int fd : openFds) PortAndroid_CloseResolved(fd);
  return code;
}

int RunNested(const std::string& name, const std::vector<std::string>& args, std::string& output) {
  const Program* program = Find(name);
  if (program == nullptr) return 127;
  const char* cache = SDL_GetAndroidCachePath();
  const fs::path temp = fs::path(cache != nullptr ? cache : "/data/data/net.pokemonxdpc.port/cache") / "xd_builtin_nested.txt";
  const int code = RunRedirected(*program, args, temp);
  std::ifstream in(temp, std::ios::binary);
  output.assign((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  std::error_code ec;
  fs::remove(temp, ec);
  return code;
}

}

void RegisterBuiltinConverters() {
  launcher::sys::BuiltinPrograms programs;
  programs.has = Has;
  programs.run = Run;
  launcher::sys::SetBuiltinPrograms(programs);
  g_usum_builtin_run = RunNested;
}

}
