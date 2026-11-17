#ifndef ENGINE_SCRIPT_NAMES_H
#define ENGINE_SCRIPT_NAMES_H

#include "script_host.h"

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace engine::script {

struct NameEntry {
  std::string name;
  int64_t value = 0;
};

std::string NameIdentifier(std::string_view text);
std::string NameKey(std::string_view text);

bool ParseNameFile(std::string_view text, std::vector<NameEntry>& out, std::vector<std::string>& problems);

class Names {
public:
  void Add(const std::string& category, const std::vector<NameEntry>& entries);

  bool Has(const std::string& category) const;
  size_t Count(const std::string& category) const;
  std::vector<std::string> Categories() const;
  bool Find(const std::string& category, std::string_view text, int64_t& value) const;
  std::string NameOf(const std::string& category, int64_t value) const;
  const std::vector<NameEntry>* Entries(const std::string& category) const;
  const std::vector<std::string>& Notes() const { return mNotes; }
  std::string Summary() const;

  void Install(Host& host) const;

private:
  struct Category {
    std::vector<NameEntry> entries;
    std::map<std::string, int64_t> byKey;
    std::map<int64_t, std::string> byValue;
    std::map<std::string, bool> used;
  };
  std::map<std::string, Category> mCategories;
  std::vector<std::string> mNotes;
};

}

#endif
