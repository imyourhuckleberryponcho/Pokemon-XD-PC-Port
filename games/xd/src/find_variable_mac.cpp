#ifdef __APPLE__
#include "macho_symbols.h"

#include <mach-o/dyld.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <set>
#include <string>

namespace {

struct Loaded {
  xd_macho::Table table;
  intptr_t slide = 0;
};

Loaded find_table() {
  Loaded l;
  l.slide = _dyld_get_image_vmaddr_slide(0);
  if (!xd_macho::table_in_memory(_dyld_get_image_header(0), l.slide, l.table)) {
    std::fprintf(stderr, "[games/xd] mac: the program's symbol table was not found (no variable can be found by name)\n");
  }
  return l;
}

const void* find_symbol(const std::string& name) {
  static const Loaded loaded = find_table();
  uint64_t value = 0;
  const int matches = xd_macho::find_variable(loaded.table, name, value);
  if (matches > 1) {
    std::fprintf(stderr, "[games/xd] mac: %d variables are called '%s'; the first one in the symbol table is used\n",
                 matches, name.c_str());
  }
  return matches == 0 ? nullptr : reinterpret_cast<const void*>(static_cast<uintptr_t>(value) + loaded.slide);
}

}

extern "C" const void* PortXD_FindVariable(const char* path) {
  if (path == nullptr || path[0] == '\0') return nullptr;
  static std::mutex lock;
  static std::set<std::string> said;
  static std::map<std::string, const void*> answers;
  std::lock_guard<std::mutex> guard(lock);
  if (const auto known = answers.find(path); known != answers.end()) return known->second;
  std::string name(path);
  bool needsTypes = false;
  const size_t end = name.find_first_of(".[-");
  if (end != std::string::npos) {
    std::string rest = name.substr(end);
    name.erase(end);
    while (rest.rfind("[0]", 0) == 0) rest.erase(0, 3);
    needsTypes = !rest.empty();
  }
  const void* address = needsTypes ? nullptr : find_symbol(name);
  answers[path] = address;
  if (address == nullptr && said.insert(path).second) {
    std::fprintf(stderr, "[games/xd] mac: variable '%s' %s\n", path,
                 needsTypes ? "needs the program's type information, which the Mac build does not read yet"
                            : "is not in the program's symbol table");
  }
  return address;
}
#endif
