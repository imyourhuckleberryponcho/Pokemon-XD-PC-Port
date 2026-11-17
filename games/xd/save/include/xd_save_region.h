#ifndef XD_SAVE_REGION_H
#define XD_SAVE_REGION_H

#include <xd_save.h>

#include <cstdint>
#include <string>
#include <vector>

namespace xd::save {

enum class SaveKind {
  Unknown,
  UsaLayout,
  JapaneseLayout
};

SaveKind LayoutOf(const std::vector<uint8_t>& file, std::string& why);

int SaveLanguage(const Save& save);
const char* LanguageName(int number);
const char* LanguageId(int number);

std::string DisplayUtf8(const std::u16string& s);

struct JapaneseReport {
  int slotUsed = -1;
  std::vector<std::string> notes;
};

bool ConvertJapanese(const std::vector<uint8_t>& japanese, std::vector<uint8_t>& out, JapaneseReport& report, std::string& error);

}

#endif
