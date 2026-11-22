#include <dolphin/card.h>
#include <xd_save_convert.h>

#include "data/common_rel.h"
#include "i18n.h"

#include <port_message.h>

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {
using engine::i18n::Tr;
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
}

namespace {

bool IsXdSave(const CARDFileInfo* fi) {
  CARDStat stat{};
  if (fi == nullptr || CARDGetStatus(fi->chan, fi->fileNo, &stat) != CARD_RESULT_READY) {
    return false;
  }
  return std::memcmp(stat.gameName, "GXXE", 4) == 0 && std::strncmp(stat.fileName, "PokemonXD", CARD_FILENAME_MAX) == 0 &&
         stat.length == xd::save::kFileSize;
}

bool ReadStored(const CARDFileInfo* fi, std::vector<uint8_t>& stored, s32* result) {
  stored.assign(xd::save::kFileSize, 0);
  *result = CARDRead(fi, stored.data(), static_cast<s32>(stored.size()), 0);
  return *result == CARD_RESULT_READY;
}

uint64_t sGuardHash = 0;
bool sGuardChecked = false;

void GuardUnknownIds(const std::vector<uint8_t>& stored) {
  uint32_t species = 0, moves = 0, items = 0;
  if (!PortXD_CommonTableCounts(&species, &moves, &items)) return;
  uint64_t h = 1469598103934665603ull;
  for (uint8_t b : stored) h = (h ^ b) * 1099511628211ull;
  if (sGuardChecked && h == sGuardHash) return;
  xd::save::Save save;
  xd::save::ReadReport report;
  std::string error;
  if (xd::save::ReadSave(stored, save, report, error)) {
    const std::vector<std::string> unknown = xd::save::UnknownIds(save, {species, moves, items});
    if (!unknown.empty()) {
      std::string list;
      for (const std::string& u : unknown) list += "  " + u + "\n";
      const std::string text = TrFmt(Tr("This save holds Pokemon, moves or items that this game does not have (the game has %d Pokemon, "
                                        "%d moves, %d items):\n\n%s\nIt was probably saved with a mod that adds Pokemon, moves or items. "
                                        "Start the game with that mod switched on (same mods, same order). Nothing was read into the "
                                        "game and nothing on the card was changed. The game stops here."),
                                     static_cast<int>(species), static_cast<int>(moves), static_cast<int>(items), list.c_str());
      std::fprintf(stderr, "[save guard] STOP: %s\n", text.c_str());
      std::fflush(stderr);
      PortMessage_Show(PORT_MESSAGE_ERROR, Tr("Pokemon XD - this save needs its mods"), text.c_str());
      std::fprintf(stderr, "[save guard] exit code 6\n");
      std::fflush(stderr);
      std::_Exit(6);
    }
  }
  sGuardHash = h;
  sGuardChecked = true;
}

struct SaveExtHooks {
  int (*beforeWrite)(const uint8_t*, size_t) = nullptr;
  void (*afterWrite)(int) = nullptr;
  int (*onRead)(const uint8_t*, size_t, size_t, char*, size_t) = nullptr;
};
SaveExtHooks sExt;

void RefuseSave(const char* text) {
  std::fprintf(stderr, "[save guard] STOP: %s\n", text);
  std::fflush(stderr);
  PortMessage_Show(PORT_MESSAGE_ERROR, Tr("Pokemon XD - this save needs its extra data"), text);
  std::fprintf(stderr, "[save guard] exit code 6\n");
  std::fflush(stderr);
  std::_Exit(6);
}

uint32_t sSaveWrites = 0;

}

extern "C" {

uint32_t PortXD_SaveWriteCount(void) { return sSaveWrites; }

static void (*sSaveWritten)(const uint8_t* file, size_t size) = nullptr;
void PortXD_SetSaveWrittenHook(void (*hook)(const uint8_t* file, size_t size)) { sSaveWritten = hook; }
void PortXD_SetSaveExtHooks(int (*before)(const uint8_t*, size_t), void (*after)(int), int (*onRead)(const uint8_t*, size_t, size_t, char*, size_t)) {
  sExt.beforeWrite = before;
  sExt.afterWrite = after;
  sExt.onRead = onRead;
}

int PortXD_SaveCardRead(const CARDFileInfo* fi, void* addr, s32 length, s32 offset, s32* result) {
  if (!IsXdSave(fi) || offset < 0 || length < 0 || static_cast<size_t>(offset) + length > xd::save::kFileSize) {
    return 0;
  }
  std::vector<uint8_t> stored;
  if (!ReadStored(fi, stored, result)) {
    return 1;
  }
  GuardUnknownIds(stored);
  if (sExt.onRead != nullptr) {
    char text[1536] = {};
    if (sExt.onRead(stored.data(), stored.size(), static_cast<size_t>(length), text, sizeof text) != 0) RefuseSave(text);
  }
  const std::vector<uint8_t> view = xd::save::GameViewOf(stored);
  std::memcpy(addr, view.data() + offset, static_cast<size_t>(length));
  return 1;
}

int PortXD_SaveCardWrite(const CARDFileInfo* fi, const void* addr, s32 length, s32 offset, s32* result) {
  if (!IsXdSave(fi) || offset < 0 || length < 0 || static_cast<size_t>(offset) + length > xd::save::kFileSize) {
    return 0;
  }
  sSaveWrites++;
  std::vector<uint8_t> stored;
  if (!ReadStored(fi, stored, result)) {
    return 1;
  }
  std::vector<std::pair<size_t, size_t>> changed;
  xd::save::ApplyGameWrite(stored, static_cast<size_t>(offset), static_cast<const uint8_t*>(addr),
                           static_cast<size_t>(length), changed);
  *result = CARD_RESULT_READY;
  const bool hooked = sExt.beforeWrite != nullptr && !changed.empty();
  const bool staged = hooked && sExt.beforeWrite(stored.data(), stored.size()) != 0;
  for (const auto& [at, size] : changed) {
    *result = CARDWrite(fi, stored.data() + at, static_cast<s32>(size), static_cast<s32>(at));
    if (*result != CARD_RESULT_READY) {
      break;
    }
  }
  if (hooked && sExt.afterWrite != nullptr) sExt.afterWrite(*result == CARD_RESULT_READY ? 1 : 0);
  (void)staged;
  if (*result == CARD_RESULT_READY && sSaveWritten != nullptr) sSaveWritten(stored.data(), stored.size());
  return 1;
}

}
