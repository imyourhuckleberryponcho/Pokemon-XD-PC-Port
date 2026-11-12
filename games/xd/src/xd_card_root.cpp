#include "xd_user_folder.h"

#include <save_slots.h>

#include <mutex>

namespace xd {
namespace {

namespace fs = std::filesystem;

std::mutex sLock;
bool sKnown = false;
fs::path sRoot;
std::string sName;
bool sSlots = false;
std::string sRootUtf8, sNameC;

thread_local bool tOverride = false;
thread_local fs::path tRoot;

void LookUp() {
  if (sKnown) return;
  sKnown = true;
#ifdef _WIN32
  const wchar_t* dir = _wgetenv(L"XD_CARD_DIR");
#else
  const char* dir = std::getenv("XD_CARD_DIR");
#endif
  if (dir != nullptr && dir[0] != 0) {
    sRoot = fs::path(dir);
    sName.clear();
    sSlots = false;
    return;
  }
  sRoot = engine::memcard::slots::ActiveRoot(UserFolder(), &sName);
  sSlots = true;
}

}

std::filesystem::path CardRoot() {
  if (tOverride) return tRoot;
  std::lock_guard<std::mutex> lock(sLock);
  LookUp();
  return sRoot;
}

std::string ActiveSaveName() {
  std::lock_guard<std::mutex> lock(sLock);
  LookUp();
  return sName;
}

bool SaveSlotsInUse() {
  std::lock_guard<std::mutex> lock(sLock);
  LookUp();
  return sSlots;
}

void ForgetActiveSave() {
  std::lock_guard<std::mutex> lock(sLock);
  sKnown = false;
}

CardRootScope::CardRootScope(const std::filesystem::path& root) : mBefore(tRoot), mHadBefore(tOverride) {
  tRoot = root;
  tOverride = true;
}

CardRootScope::~CardRootScope() {
  tRoot = mBefore;
  tOverride = mHadBefore;
}

}

extern "C" const char* PortXD_CardRootUtf8(void) {
  const std::u8string u8 = xd::CardRoot().u8string();
  std::lock_guard<std::mutex> lock(xd::sLock);
  if (xd::sRootUtf8.empty()) xd::sRootUtf8.assign(u8.begin(), u8.end());
  return xd::sRootUtf8.c_str();
}

extern "C" const char* PortXD_CardSaveName(void) {
  const bool slots = xd::SaveSlotsInUse();
  const std::string name = xd::ActiveSaveName();
  std::lock_guard<std::mutex> lock(xd::sLock);
  if (xd::sNameC.empty()) xd::sNameC = slots ? name : std::string("XD_CARD_DIR");
  return xd::sNameC.c_str();
}
