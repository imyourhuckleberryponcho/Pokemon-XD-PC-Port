#include <dolphin/gx.h>
#include <aurora/texture.hpp>
#include <hd_textures.h>
#include <mods.h>
#include <settings.h>

#include <mod_layout.h>
#include "texture_observers.h"
#include "xd_user_folder.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

#include "xd_hd_packs.h"

namespace {

namespace fs = std::filesystem;
namespace at = aurora::texture;
namespace eh = engine::hdtex;

constexpr const char* kGameId = "GXXE01";

std::string Utf8(const fs::path& p) {
  const auto u = p.u8string();
  return std::string(u.begin(), u.end());
}

std::string LowerExt(const fs::path& p) {
  std::string e = Utf8(p.extension());
  for (char& c : e) {
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  }
  return e;
}

std::mutex sPackMutex;
std::vector<xd::hd::Pack> sPacks;

std::vector<at::ReplacementGroup> sGroups;
bool sLoaded = false;

bool ReadFile(void*, const char* path, std::vector<uint8_t>& out) {
  std::ifstream f(fs::u8path(path), std::ios::binary);
  if (!f) return false;
  f.seekg(0, std::ios::end);
  const std::streamoff size = f.tellg();
  if (size <= 0) return false;
  f.seekg(0);
  out.resize(static_cast<size_t>(size));
  f.read(reinterpret_cast<char*>(out.data()), size);
  return static_cast<bool>(f);
}

at::ReplacementGroup RegisterLoose(const fs::path& folder, const at::ReplacementOptions& options) {
  at::ReplacementGroup group;
  std::vector<fs::path> files;
  std::error_code ec;
  for (fs::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec)) {
    std::error_code e2;
    if (!it->is_regular_file(e2)) continue;
    const std::string ext = LowerExt(it->path());
    if (ext != ".png" && ext != ".dds") continue;
    const auto name = eh::ParseName(Utf8(it->path().stem()));
    if (!name || name->mipLevel != 0) continue;
    files.push_back(it->path());
  }
  std::sort(files.begin(), files.end());
  for (const fs::path& f : files) {
    std::string path = Utf8(f);
    for (char& c : path) {
      if (c == '\\') c = '/';
    }
    const at::ReplacementRegistration r = at::register_virtual_replacement(path, {ReadFile, nullptr}, options);
    if (r.id != 0) group.registrations.push_back(r);
  }
  return group;
}

void LoadPacks(const std::string& only = {}) {
  const auto start = std::chrono::steady_clock::now();
  std::vector<xd::hd::Pack> packs;
  {
    std::lock_guard lk(sPackMutex);
    packs = sPacks;
  }
  if (!only.empty()) {
    auto lower = [](std::string v) { for (char& c : v) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); return v; };
    const std::string want = lower(only);
    packs.erase(std::remove_if(packs.begin(), packs.end(),
                               [&](const xd::hd::Pack& p) {
                                 return lower(Utf8(p.folder.filename())) != want && lower(p.name) != want &&
                                        lower(p.name) != "textures: " + want;
                               }),
                packs.end());
  }
  int32_t priority = 0;
  for (const xd::hd::Pack& pack : packs) {
    const eh::PackInfo info = pack.loose ? eh::PackInfo{} : eh::ScanPack(pack.folder);
    std::fprintf(stderr, "[hd textures] on: %s (%s)%s\n", pack.name.c_str(), Utf8(pack.folder).c_str(),
                 pack.loose ? ", the pictures lying loose in it"
                            : (": " + std::to_string(info.textures) + " textures, " + std::to_string(info.mipFiles) +
                               " mipmap files").c_str());
    at::ReplacementOptions options;
    options.priority = priority++;
    sGroups.push_back(pack.loose ? RegisterLoose(pack.folder, options) : at::load_replacement_directory(pack.folder, options));
  }
  const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
  std::fprintf(stderr, "[hd textures] %zu pack(s), indexed in %.0f ms\n", packs.size(), ms);
  sLoaded = true;
}

using DumpKey = std::tuple<uint64_t, uint64_t, uint32_t, uint32_t, uint32_t, bool>;

struct Job {
  std::string name;
  uint32_t w, h;
  std::vector<uint8_t> rgba;
  DumpKey key;
};

std::mutex sDumpMutex;
std::condition_variable sDumpCv;
std::deque<Job> sQueue;
std::set<DumpKey> sSeen;
bool sDumpOn = false;
bool sStop = false;
std::thread sWriter;
fs::path sDumpFolder;
uint64_t sWritten = 0, sKept = 0, sFailed = 0;

DumpKey KeyOf(const at::TextureSourceKey& k) { return {k.textureHash, k.tlutHash, k.width, k.height, k.format, k.hasTlut}; }

void WriterMain() {
  std::unique_lock lk(sDumpMutex);
  while (true) {
    sDumpCv.wait(lk, [] { return sStop || !sQueue.empty(); });
    if (sQueue.empty() && sStop) return;
    Job job = std::move(sQueue.front());
    sQueue.pop_front();
    lk.unlock();
    const fs::path file = sDumpFolder / (job.name + ".png");
    std::error_code ec;
    bool ok = true, kept = false;
    if (fs::exists(file, ec)) {
      kept = true;
    } else {
      ok = eh::WritePngRgba(file, job.w, job.h, job.rgba.data());
    }
    lk.lock();
    if (kept) sKept++;
    else if (ok) sWritten++;
    else sFailed++;
    if (!ok) {
      sSeen.erase(job.key);
      std::fprintf(stderr, "[hd textures] dump: could not write %s\n", Utf8(file).c_str());
    }
  }
}

bool DumpWants(void*, const at::TextureSourceKey& key) {
  if (key.hasTlut && key.tlutHash == 0) return false;
  std::lock_guard lk(sDumpMutex);
  if (!sDumpOn) return false;
  if (sQueue.size() > 400) return false;
  return sSeen.insert(KeyOf(key)).second;
}

void DumpDecoded(void*, const at::TextureSourceKey& key, const uint8_t* rgba, uint32_t w, uint32_t h, bool mipmapped) {
  eh::Name n;
  n.width = key.width;
  n.height = key.height;
  n.mipmapped = mipmapped;
  n.texHash = key.textureHash;
  n.hasTlut = key.hasTlut;
  n.tlutHash = key.tlutHash;
  n.format = key.format;
  Job job;
  job.name = eh::FormatName(n);
  job.w = w;
  job.h = h;
  job.rgba.assign(rgba, rgba + static_cast<size_t>(w) * h * 4);
  job.key = KeyOf(key);
  {
    std::lock_guard lk(sDumpMutex);
    sQueue.push_back(std::move(job));
  }
  sDumpCv.notify_one();
}

void StartDump() {
  sDumpFolder = xd::SettingsFolder() / "texture_dump" / kGameId;
  std::error_code ec;
  fs::create_directories(sDumpFolder, ec);
  {
    std::lock_guard lk(sDumpMutex);
    sDumpOn = true;
    sStop = false;
  }
  if (!sWriter.joinable()) sWriter = std::thread(WriterMain);
  xd::texobs::Set(xd::texobs::kTextureDump, {DumpWants, DumpDecoded, nullptr});
  std::fprintf(stderr, "[hd textures] dump on: new textures go to %s\n", Utf8(sDumpFolder).c_str());
}

void StopDump() {
  xd::texobs::Set(xd::texobs::kTextureDump, {});
  {
    std::lock_guard lk(sDumpMutex);
    sDumpOn = false;
  }
  std::fprintf(stderr, "[hd textures] dump off (%llu written, %llu already there, %llu failed)\n",
               static_cast<unsigned long long>(sWritten), static_cast<unsigned long long>(sKept),
               static_cast<unsigned long long>(sFailed));
}

struct WriterJoiner {
  ~WriterJoiner() {
    {
      std::lock_guard lk(sDumpMutex);
      sStop = true;
    }
    sDumpCv.notify_all();
    if (sWriter.joinable()) sWriter.join();
  }
} sJoiner;

uint32_t sSeenChanges = ~0u;
uint32_t sHdSeenChanges = ~0u;
bool sFirst = true;
bool sPacksAllowed = false;
bool sWant = false;
std::string sFilter;

void UnloadPacks() {
  for (at::ReplacementGroup& group : sGroups) at::unregister_replacements(group);
  sGroups.clear();
}

}

namespace xd::hd {
void SetPacks(std::vector<Pack> packs) {
  std::lock_guard lk(sPackMutex);
  sPacks = std::move(packs);
}

bool ReloadPacks(std::vector<Pack> packs) {
  if (sFirst || !sPacksAllowed) return false;
  UnloadPacks();
  SetPacks(std::move(packs));
  if (sWant) LoadPacks(sFilter);
  return true;
}
}

extern "C" void PortXD_InstallTexturePackSource(void) {
  engine::mods::SetTexturePackSource(xd::SettingsFolder() / "textures", kGameId, {engine::mods::MakeTarget(kGameId, "xg")});
}

extern "C" void PortXD_HdTexturesUpdate(void) {
  engine::settings::Registry& reg = engine::settings::Global();
  const bool changed = sFirst || reg.Changes() != sHdSeenChanges;
  if (sFirst) {
    const char* off = std::getenv("XD_HD_TEXTURES");
    const bool disabled = off != nullptr && std::strcmp(off, "0") == 0;
    const bool original = reg.IsOriginal() || reg.CurrentPreset() == engine::settings::Preset::Original;
    sPacksAllowed = !disabled && !original;
    bool any;
    {
      std::lock_guard lk(sPackMutex);
      any = !sPacks.empty();
    }
    if (any && !sPacksAllowed) std::fprintf(stderr, "[hd textures] %s: no texture pack is used\n", disabled ? "XD_HD_TEXTURES=0" : "Original");
  }
  if (changed) {
    sHdSeenChanges = reg.Changes();
    const bool original = reg.IsOriginal() || reg.CurrentPreset() == engine::settings::Preset::Original;
    const char* off = std::getenv("XD_HD_TEXTURES");
    const bool disabled = off != nullptr && std::strcmp(off, "0") == 0;
    bool want = !disabled && !original;
    if (want && reg.Find("video.hd_textures") != engine::settings::kNoSetting) want = reg.Bool("video.hd_textures").Get();
    std::string filter;
    if (reg.Find("video.hd_pack") != engine::settings::kNoSetting) filter = reg.Text("video.hd_pack");
    sPacksAllowed = !disabled && !original;
    bool any;
    {
      std::lock_guard lk(sPackMutex);
      any = !sPacks.empty();
    }
    if (want != sWant || (want && filter != sFilter) || sFirst) {
      const bool wasOn = sWant;
      sWant = want;
      sFilter = filter;
      if (wasOn) UnloadPacks();
      if (want && any) LoadPacks(filter);
      else if (wasOn) std::fprintf(stderr, "[hd textures] off: the game's own pictures\n");
    }
    sFirst = false;
  }
  if (reg.Find("video.hd_textures_dump") == engine::settings::kNoSetting) return;
  if (reg.Changes() == sSeenChanges) return;
  sSeenChanges = reg.Changes();
  const bool dump = reg.Bool("video.hd_textures_dump").Get();
  bool dumping;
  {
    std::lock_guard lk(sDumpMutex);
    dumping = sDumpOn;
  }
  if (dump && !dumping) StartDump();
  if (!dump && dumping) StopDump();
}
