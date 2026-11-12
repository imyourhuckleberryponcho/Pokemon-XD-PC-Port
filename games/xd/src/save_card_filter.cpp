#include <dolphin/card.h>
#include <xd_save_convert.h>

#include <cstring>
#include <vector>

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

uint32_t sSaveWrites = 0;

}

extern "C" {

uint32_t PortXD_SaveWriteCount(void) { return sSaveWrites; }

static void (*sSaveWritten)(const uint8_t* file, size_t size) = nullptr;
void PortXD_SetSaveWrittenHook(void (*hook)(const uint8_t* file, size_t size)) { sSaveWritten = hook; }

int PortXD_SaveCardRead(const CARDFileInfo* fi, void* addr, s32 length, s32 offset, s32* result) {
  if (!IsXdSave(fi) || offset < 0 || length < 0 || static_cast<size_t>(offset) + length > xd::save::kFileSize) {
    return 0;
  }
  std::vector<uint8_t> stored;
  if (!ReadStored(fi, stored, result)) {
    return 1;
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
  for (const auto& [at, size] : changed) {
    *result = CARDWrite(fi, stored.data() + at, static_cast<s32>(size), static_cast<s32>(at));
    if (*result != CARD_RESULT_READY) {
      break;
    }
  }
  if (*result == CARD_RESULT_READY && sSaveWritten != nullptr) sSaveWritten(stored.data(), stored.size());
  return 1;
}

}
