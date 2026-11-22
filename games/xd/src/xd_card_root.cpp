#include "xd_user_folder.h"

#include <save_slots.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <mutex>

namespace xd {
namespace {

namespace fs = std::filesystem;

std::mutex sLock;
bool sKnown = false;
fs::path sRoot;
std::string sName;
bool sSlots = false;
bool sXG = false;
std::string sRootUtf8, sNameC;
std::string sMissingSave;
std::string sSaveList;

std::string Lower(std::string text) {
  for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return text;
}

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
  sMissingSave.clear();
  if (const char* want = std::getenv("XD_SAVE_NAME"); want != nullptr && want[0] != 0) {
    namespace sl = engine::memcard::slots;
    const sl::Index index = sl::Load(SaveDataFolder(sXG));
    const std::string wanted = Lower(sl::CleanName(want));
    sSaveList.clear();
    for (const sl::Slot& slot : index.slots) {
      sSaveList += (sSaveList.empty() ? "" : ", ") + std::string("\"") + slot.name + "\"";
      if (Lower(slot.name) == wanted || Lower(slot.id) == wanted) {
        sRoot = sl::RootOf(SaveDataFolder(sXG), slot.id);
        sName = slot.name;
        sSlots = true;
        return;
      }
    }
    sMissingSave = want;
  }
  sRoot = engine::memcard::slots::ActiveRoot(SaveDataFolder(sXG), &sName);
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

std::string MissingSave(std::string& have) {
  std::lock_guard<std::mutex> lock(sLock);
  LookUp();
  have = sSaveList;
  return sMissingSave;
}

bool UsingXGSaves() {
  std::lock_guard<std::mutex> lock(sLock);
  return sXG;
}

void UseXGSaves(bool xg) {
  std::lock_guard<std::mutex> lock(sLock);
  if (sXG != xg) {
    sXG = xg;
    sKnown = false;
    sRootUtf8.clear();
    sNameC.clear();
  }
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

extern "C" const char* PortXD_SaveChoiceProblem(void) {
  static std::string text;
  std::string have;
  const std::string wanted = xd::MissingSave(have);
  if (wanted.empty()) return nullptr;
  text = "There is no save named \"" + wanted + "\" (--save). The saves here: " + (have.empty() ? "none" : have) +
         ".\n\nThe names are the ones on the launcher's Save page.";
  return text.c_str();
}

extern "C" void PortXD_UseXGSaves(int xg) { xd::UseXGSaves(xg != 0); }

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
