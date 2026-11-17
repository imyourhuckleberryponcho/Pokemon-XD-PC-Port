#include "xd_languages.h"

#include <cstdlib>
#include <fstream>
#include <iterator>

namespace xd::languages {

const std::vector<Language>& All() {
  static const std::vector<Language> all = {
      {"us", "English (US)", 2, "US"}, {"ge", "German", 3, "GE"}, {"fr", "French", 4, "FR"},
      {"it", "Italian", 5, "IT"},      {"sp", "Spanish", 6, "SP"},
      {"jp", "Japanese", 2, "JP"},
  };
  return all;
}

const Language* Find(const std::string& id) {
  for (const Language& l : All()) {
    if (id == l.id) return &l;
  }
  return nullptr;
}

bool Pack::Has(const std::string& id) const {
  for (const Language* l : languages) {
    if (id == l->id) return true;
  }
  return false;
}

std::filesystem::path PackFolder(const std::filesystem::path& dataFolder) {
  return dataFolder / "extra_assets" / kFolderName;
}

namespace {
long long NumberOf(const std::string& json, const std::string& key) {
  const size_t k = json.find("\"" + key + "\"");
  if (k == std::string::npos) return -1;
  size_t i = json.find(':', k);
  if (i == std::string::npos) return -1;
  i++;
  while (i < json.size() && (json[i] == ' ' || json[i] == '\n' || json[i] == '\r' || json[i] == '\t')) i++;
  if (i >= json.size() || json[i] < '0' || json[i] > '9') return -1;
  return std::strtoll(json.c_str() + i, nullptr, 10);
}
std::vector<std::string> ListOf(const std::string& json, const std::string& key) {
  std::vector<std::string> out;
  const size_t k = json.find("\"" + key + "\"");
  if (k == std::string::npos) return out;
  const size_t open = json.find('[', k), close = json.find(']', k);
  if (open == std::string::npos || close == std::string::npos || close < open) return out;
  size_t i = open;
  while (true) {
    const size_t a = json.find('"', i);
    if (a == std::string::npos || a > close) break;
    const size_t b = json.find('"', a + 1);
    if (b == std::string::npos || b > close) break;
    out.push_back(json.substr(a + 1, b - a - 1));
    i = b + 1;
  }
  return out;
}
}

Pack ReadPack(const std::filesystem::path& dataFolder) {
  Pack pack;
  pack.folder = PackFolder(dataFolder);
  pack.jpFolder = dataFolder / "extra_assets" / kJpFolderName;
  std::error_code ec;
  if (std::filesystem::is_regular_file(pack.folder / kPackFile, ec)) {
    std::ifstream in(pack.folder / kPackFile, std::ios::binary);
    const std::string json((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const long long version = NumberOf(json, "version");
    if (version != kPackVersion) {
      pack.problem = "the language pack was made by another version (" + std::to_string(version) + ", this game wants " +
                     std::to_string(kPackVersion) + "): make it again on the Extra assets page";
    } else {
      for (const std::string& id : ListOf(json, "languages")) {
        const Language* l = Find(id);
        if (l != nullptr && id != "us" && id != kJpId) pack.languages.push_back(l);
      }
      pack.palFound = !pack.languages.empty();
      if (!pack.palFound) pack.problem = "the language pack holds no language";
    }
  }
  if (std::filesystem::is_regular_file(pack.jpFolder / kPackFile, ec)) {
    std::ifstream in(pack.jpFolder / kPackFile, std::ios::binary);
    const std::string json((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (NumberOf(json, "version") == kPackVersion) {
      pack.jp = true;
      pack.languages.push_back(Find(kJpId));
    } else if (pack.problem.empty()) {
      pack.problem = "the Japanese pack was made by another version: make it again on the Extra assets page";
    }
  }
  pack.found = pack.palFound || pack.jp;
  return pack;
}

std::vector<const Language*> Available(const Pack& pack) {
  std::vector<const Language*> out;
  out.push_back(Find("us"));
  if (pack.found) {
    for (const Language* l : pack.languages) out.push_back(l);
  }
  return out;
}

std::filesystem::path TableFile(const Pack& pack, const std::string& name) {
  return pack.folder / "tables" / (name + ".bin");
}

std::filesystem::path JpTableFile(const Pack& pack, const std::string& name) {
  return pack.jpFolder / "tables" / (name + ".bin");
}

std::string PackJson(const std::vector<std::string>& languageIds, int archives, const std::string& source) {
  std::string s = "{\n  \"id\": \"" + std::string(languageIds.size() == 1 && languageIds[0] == kJpId ? kJpManifestId : kManifestId) + "\",\n  \"version\": " + std::to_string(kPackVersion) +
                  ",\n  \"source\": \"" + source + "\",\n  \"archives\": " + std::to_string(archives) + ",\n  \"languages\": [";
  for (size_t i = 0; i < languageIds.size(); i++) s += std::string(i ? ", " : "") + "\"" + languageIds[i] + "\"";
  s += "]\n}\n";
  return s;
}

}
