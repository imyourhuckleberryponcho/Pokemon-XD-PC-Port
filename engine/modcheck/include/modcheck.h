#ifndef ENGINE_MODCHECK_H
#define ENGINE_MODCHECK_H

#include "mod_layout.h"
#include "mods.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace engine::modcheck {

enum class Severity { Problem, Warning, Note };
enum class Category { Mod, Safety };

struct Finding {
  Severity severity = Severity::Problem;
  std::string code;
  std::string file;
  int line = 0;
  std::string text;
  Category category = Category::Mod;
};

struct Permission {
  std::string code;
  std::string text;
};

struct Report {
  std::filesystem::path folder;
  engine::mods::Manifest manifest;
  bool manifestOk = false;
  bool deepDone = false;
  std::vector<Finding> findings;
  std::vector<Permission> permissions;
  std::vector<engine::mods::GameSupport> support;

  void Add(Severity severity, std::string code, std::string file, int line, std::string text, Category category = Category::Mod);
  void Unsafe(Severity severity, std::string code, std::string file, int line, std::string text) {
    Add(severity, std::move(code), std::move(file), line, std::move(text), Category::Safety);
  }
  bool HasPermission(const std::string& code) const;
  void Problem(std::string code, std::string file, int line, std::string text) { Add(Severity::Problem, std::move(code), std::move(file), line, std::move(text)); }
  void Warn(std::string code, std::string file, int line, std::string text) { Add(Severity::Warning, std::move(code), std::move(file), line, std::move(text)); }
  void Note(std::string code, std::string file, int line, std::string text) { Add(Severity::Note, std::move(code), std::move(file), line, std::move(text)); }
  size_t Count(Severity severity) const;
  size_t Count(Severity severity, Category category) const;
  size_t UnsafeProblems() const { return Count(Severity::Problem, Category::Safety); }
  bool Has(const std::string& code) const;
  int ExitCode(bool strict) const;
  std::string Format() const;
};

struct GameRules {
  std::string gameName;
  std::vector<std::string> gameIds;
  std::vector<std::string> scriptHooks;
  std::vector<std::string> gameOnlyLua;
  std::function<bool(const std::string& text, std::string& why)> checkMessageText;
  std::function<void(const std::string& path, const std::vector<uint8_t>& data, Report& report)> checkEntryFile;
  struct ExtraFolder {
    std::string name;
    std::string kind;
  };
  std::vector<ExtraFolder> extraFolders;
  struct Context {
    const std::filesystem::path& folder;
    const engine::mods::Manifest& manifest;
    bool hasDisc;
  };
  struct Extra {
    std::string name;
    std::function<void(const Context& context, Report& report)> run;
  };
  std::vector<Extra> extra;
};

struct Options {
  std::filesystem::path folder;
  const GameRules* rules = nullptr;
  std::filesystem::path modsFolder;
  const engine::mods::LoadOptions* deep = nullptr;
};

Report CheckMod(const Options& options);

bool IsJunkPath(const std::string& path);
std::string UnsafeNameReason(const std::string& name);
std::string Closest(const std::string& word, const std::vector<std::string>& choices);

}

#endif
