#include "i18n.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <unordered_map>

namespace engine::i18n {

struct EmbeddedLocale {
  const char* code;
  const unsigned char* text;
  size_t size;
};
extern const EmbeddedLocale kEmbeddedLocales[];

namespace {

std::unordered_map<std::string, std::string> sTable;
const char* sLanguage = "en";
bool sLog = false;

std::string Unescape(std::string_view s) {
  std::string out;
  out.reserve(s.size());
  for (size_t i = 0; i < s.size(); i++) {
    if (s[i] == '\\' && i + 1 < s.size()) {
      const char n = s[i + 1];
      if (n == 'n') {
        out += '\n';
        i++;
        continue;
      }
      if (n == '\\') {
        out += '\\';
        i++;
        continue;
      }
    }
    out += s[i];
  }
  return out;
}

size_t Parse(std::string_view text) {
  size_t entries = 0;
  std::string key;
  bool haveKey = false;
  size_t pos = 0;
  while (pos < text.size()) {
    size_t end = text.find('\n', pos);
    if (end == std::string_view::npos) end = text.size();
    std::string_view line = text.substr(pos, end - pos);
    pos = end + 1;
    if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
    if (line.size() >= 2 && line[0] == '=' && line[1] == ' ') {
      key = Unescape(line.substr(2));
      haveKey = true;
    } else if (line.size() >= 1 && line[0] == '>' && haveKey) {
      std::string_view v = line.substr(line.size() >= 2 && line[1] == ' ' ? 2 : 1);
      if (!v.empty()) {
        sTable[key] = Unescape(v);
        entries++;
      }
      haveKey = false;
    }
  }
  return entries;
}

const char* CodeOf(std::string_view id) {
  if (id == "ge" || id == "de") return "de";
  if (id == "fr") return "fr";
  if (id == "it") return "it";
  if (id == "sp" || id == "es") return "es";
  if (id == "jp" || id == "ja") return "ja";
  return "en";
}

}

bool SetLanguage(std::string_view id) {
  sTable.clear();
  sLanguage = CodeOf(id);
  sLog = std::getenv("XD_LOCALE_LOG") != nullptr;
  if (std::string_view(sLanguage) == "en") return false;
  if (const char* dir = std::getenv("XD_LOCALE_DIR"); dir != nullptr && *dir != '\0') {
    std::ifstream f(std::string(dir) + "/" + sLanguage + ".txt", std::ios::binary);
    if (f) {
      std::stringstream ss;
      ss << f.rdbuf();
      return Parse(ss.str()) > 0;
    }
  }
  for (const EmbeddedLocale* e = kEmbeddedLocales; e->code != nullptr; e++) {
    if (std::string_view(e->code) == sLanguage) {
      return Parse(std::string_view(reinterpret_cast<const char*>(e->text), e->size)) > 0;
    }
  }
  return false;
}

const char* Language() { return sLanguage; }

std::string_view EmbeddedLocaleText(std::string_view code) {
  for (const EmbeddedLocale* e = kEmbeddedLocales; e->code != nullptr; e++) {
    if (code == e->code) return std::string_view(reinterpret_cast<const char*>(e->text), e->size);
  }
  return {};
}

const char* LanguageName(std::string_view id) {
  const std::string_view c = CodeOf(id);
  if (c == "de") return "Deutsch";
  if (c == "fr") return "Fran\xC3\xA7"
                        "ais";
  if (c == "it") return "Italiano";
  if (c == "es") return "Espa\xC3\xB1"
                        "ol";
  if (c == "ja") return "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E";
  return "English";
}

const char* Tr(const char* english) {
  if (english == nullptr) return "";
  if (sTable.empty()) return english;
  const auto it = sTable.find(english);
  if (it != sTable.end()) return it->second.c_str();
  if (sLog) std::fprintf(stderr, "[locale] no %s text for: %s\n", sLanguage, english);
  return english;
}

std::string Tr(const std::string& english) {
  if (sTable.empty()) return english;
  const auto it = sTable.find(english);
  return it != sTable.end() ? it->second : english;
}

size_t ParseForTest(std::string_view text) {
  sTable.clear();
  return Parse(text);
}

}
