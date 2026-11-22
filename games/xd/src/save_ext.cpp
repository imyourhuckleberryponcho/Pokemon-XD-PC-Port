#include "xd_save_ext.h"

#include "i18n.h"
#include "xd_playthrough_files.h"

#include <playthrough.h>
#include <saveext.h>

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <filesystem>
#include <system_error>

#ifndef XD_PORT_VERSION
#define XD_PORT_VERSION "dev"
#endif

namespace fs = std::filesystem;
namespace se = engine::saveext;
namespace xp = xd::playthrough;

namespace {

using engine::i18n::Tr;

constexpr size_t kSlotBytes = 0x28000;

std::string TrFmt(const char* format, ...) {
  va_list args, again;
  va_start(args, format);
  va_copy(again, args);
  const int n = std::vsnprintf(nullptr, 0, format, args);
  va_end(args);
  std::string out(static_cast<size_t>(n > 0 ? n : 0), '\0');
  if (n > 0) std::vsnprintf(out.data(), out.size() + 1, format, again);
  va_end(again);
  return out;
}

std::vector<xd::saveext::Handler>& Handlers() {
  static std::vector<xd::saveext::Handler> h;
  return h;
}

std::string sLastState = "none";
std::string sStagedKey;
fs::path sStagedFolder;
std::vector<std::string> sRequiredNow;
uint64_t sLastReadHash = 0;
bool sHaveRead = false;
uint64_t sSyncedHash = 0;
bool sHaveSynced = false;
uint64_t sPendingHash = 0;
bool sPendingValid = false;

const xd::saveext::Handler* HandlerFor(const std::string& name) {
  for (const auto& h : Handlers()) {
    if (name == h.name) return &h;
  }
  return nullptr;
}

void ResetAll() {
  for (const auto& h : Handlers()) {
    if (h.reset != nullptr) h.reset();
  }
}

std::string Join(const std::vector<std::string>& v) {
  std::string out;
  for (size_t i = 0; i < v.size(); i++) out += (i > 0 ? ", " : "") + v[i];
  return out;
}

}

namespace xd::saveext {

void Register(const Handler& handler) {
  for (auto& h : Handlers()) {
    if (std::string(h.name) == handler.name) {
      h = handler;
      return;
    }
  }
  Handlers().push_back(handler);
}

const std::string& LastState() { return sLastState; }
std::vector<std::string> RequiredNow() { return sRequiredNow; }

}

extern "C" {

int PortXD_SaveExtBeforeWrite(const uint8_t* image, size_t size) {
  sStagedKey.clear();
  sPendingValid = false;
  if (Handlers().empty()) return 0;
  const std::vector<uint8_t> bytes(image, image + size);
  const xp::SaveId id = xp::SaveIdOf(bytes);
  if (!id.found) return 0;
  sPendingHash = se::Hash64(image, size);
  sPendingValid = true;
  se::File file;
  file.saveHash = sPendingHash;
  file.writer = std::string("Pokemon XD PC port ") + XD_PORT_VERSION;
  std::vector<std::string> required;
  for (const auto& h : Handlers()) {
    if (h.save == nullptr) continue;
    std::vector<uint8_t> data = h.save();
    if (data.empty()) continue;
    se::Section s;
    s.name = h.name;
    s.version = h.version;
    s.required = h.required;
    s.data = std::move(data);
    if (h.required) required.push_back(h.name);
    file.sections.push_back(std::move(s));
  }
  sRequiredNow = required;
  const fs::path folder = xp::SetupFolder();
  std::error_code ec;
  if (file.sections.empty() && !fs::exists(se::FileFor(folder, id.key), ec)) return 0;
  if (!se::Stage(folder, id.key, file)) {
    std::fprintf(stderr, "[save ext] could not write the extension file for %s\n", id.key.c_str());
    return 0;
  }
  sStagedKey = id.key;
  sStagedFolder = folder;
  return 1;
}

void PortXD_SaveExtAfterWrite(int ok) {
  if (ok && sPendingValid) {
    sSyncedHash = sPendingHash;
    sHaveSynced = true;
  }
  sPendingValid = false;
  if (sStagedKey.empty()) return;
  if (ok) {
    if (se::Commit(sStagedFolder, sStagedKey)) {
      std::fprintf(stderr, "[save ext] kept next to the save: %s.ext (%s)\n", sStagedKey.c_str(),
                   sRequiredNow.empty() ? "optional sections only" : ("required: " + Join(sRequiredNow)).c_str());
    } else {
      std::fprintf(stderr, "[save ext] could not commit %s.ext\n", sStagedKey.c_str());
    }
  } else {
    se::DropStaged(sStagedFolder, sStagedKey);
    std::fprintf(stderr, "[save ext] the card write failed: %s.ext left as it was\n", sStagedKey.c_str());
  }
  sStagedKey.clear();
}

int PortXD_SaveExtOnRead(const uint8_t* image, size_t size, size_t length, char* text, size_t textSize) {
  const uint64_t hash = se::Hash64(image, size);
  const bool load = length >= kSlotBytes && !(sHaveSynced && hash == sSyncedHash);
  if (!load && sHaveRead && hash == sLastReadHash) return 0;
  const std::vector<uint8_t> bytes(image, image + size);
  const xp::SaveId id = xp::SaveIdOf(bytes);
  if (!id.found) return 0;
  const fs::path folder = xp::SetupFolder();
  const se::Loaded loaded = se::Load(folder, id.key, hash);
  sLastState = se::StateName(loaded.state);
  std::vector<std::string> needs;
  {
    engine::playthrough::Setup setup;
    std::string error;
    if (engine::playthrough::ReadFile(xp::SetupFileFor(id), setup, error)) needs = setup.extRequired;
  }
  if (load) ResetAll();
  std::string refuse;
  switch (loaded.state) {
  case se::State::Paired: {
    for (const se::Section& s : loaded.file.sections) {
      const xd::saveext::Handler* h = HandlerFor(s.name);
      const bool ok = h != nullptr && h->load != nullptr && (!load || h->load(s.data.data(), s.data.size(), s.version));
      if (!ok) {
        if (s.required) {
          refuse = TrFmt(Tr("The save needs data of the kind %s that this game does not know."), s.name.c_str());
        } else if (load) {
          std::fprintf(stderr, "[save ext] section \"%s\" not used (%s)\n", s.name.c_str(), h == nullptr ? "unknown" : "cannot read it");
          if (h != nullptr && h->reset != nullptr) h->reset();
        }
      }
    }
    break;
  }
  case se::State::TooNew:
    refuse = Tr("The extra data kept next to this save was written by a newer version of the game.");
    break;
  case se::State::Missing:
  case se::State::Stale:
  case se::State::Damaged:
    if (!needs.empty()) {
      refuse = TrFmt(Tr("The extra data this save needs (%s) is %s."), Join(needs).c_str(),
                     loaded.state == se::State::Missing ? Tr("missing") : loaded.state == se::State::Stale ? Tr("from another version of the save")
                                                                                                           : Tr("damaged"));
    }
    break;
  }
  if (load) {
    sRequiredNow.clear();
    for (const se::Section& s : loaded.file.sections) {
      if (loaded.state == se::State::Paired && s.required && !s.data.empty()) sRequiredNow.push_back(s.name);
    }
  }
  if (load || !sHaveRead || hash != sLastReadHash) {
    std::fprintf(stderr, "[save ext] %s: extension %s%s%s%s\n", id.key.c_str(), sLastState.c_str(), loaded.promoted ? " (finished a save that was cut short)" : "",
                 loaded.state == se::State::Paired ? (" with " + std::to_string(loaded.file.sections.size()) + " section(s)").c_str() : "",
                 load ? ", loaded" : "");
  }
  sLastReadHash = hash;
  sHaveRead = true;
  if (load && refuse.empty()) {
    sSyncedHash = hash;
    sHaveSynced = true;
  }
  if (refuse.empty()) return 0;
  const std::string out = TrFmt(Tr("%s Nothing was read into the game and nothing on the card was changed. Use the same version and mods that made the save, or restore a backup from the launcher's Save page."),
                                refuse.c_str());
  std::snprintf(text, textSize, "%s", out.c_str());
  return 1;
}

}
