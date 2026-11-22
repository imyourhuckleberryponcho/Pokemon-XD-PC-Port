#ifndef ENGINE_SCRIPT_HOST_H
#define ENGINE_SCRIPT_HOST_H

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace engine::script {

struct Value {
  enum class Kind { Nil, Bool, Int, String, Table, List } kind = Kind::Nil;
  bool boolean = false;
  int64_t integer = 0;
  std::string text;
  std::vector<std::string> fieldNames;
  std::vector<Value> fieldValues;

  static Value Bool(bool b) { Value v; v.kind = Kind::Bool; v.boolean = b; return v; }
  static Value Int(int64_t i) { Value v; v.kind = Kind::Int; v.integer = i; return v; }
  static Value Text(std::string s) { Value v; v.kind = Kind::String; v.text = std::move(s); return v; }
  static Value Table() { Value v; v.kind = Kind::Table; return v; }
  static Value List() { Value v; v.kind = Kind::List; return v; }
  Value& Add(Value item) { fieldValues.push_back(std::move(item)); return *this; }
  Value& Set(std::string key, Value value) { fieldNames.push_back(std::move(key)); fieldValues.push_back(std::move(value)); return *this; }
  Value& Set(std::string key, int64_t value) { return Set(std::move(key), Int(value)); }
};

struct Limits {
  uint32_t callInstructions = 200000;
  uint32_t frameInstructions = 1000000;
  uint32_t loadInstructions = 2000000;
  size_t memoryBytes = 16u << 20;
  uint32_t logLinesPerCall = 16;
  uint32_t logLinesTotal = 2000;
};

struct Source {
  std::string name;
  std::string text;
};

struct Failure {
  std::string modId;
  std::string message;
};

using Logger = std::function<void(const std::string& line)>;

using ApiFunction = std::function<Value(const std::string& modId, const std::vector<Value>& args, std::string& error)>;

bool CheckSyntax(const Source& source, std::string& error);

class Host {
public:
  explicit Host(std::vector<std::string> hooks, Limits limits = {}, Logger logger = {});
  ~Host();
  Host(const Host&) = delete;
  Host& operator=(const Host&) = delete;

  void SetPrelude(const std::string& luaSource);

  void SetConstants(const std::string& name, const Value& table);

  bool AddMod(const std::string& modId, const std::vector<Source>& sources, std::string& error);

  void SetApi(const std::string& name, ApiFunction function);

  bool HasFunction(const std::string& name) const;
  bool CallFunction(const std::string& name, const std::vector<Value>& args, int64_t& result, std::string& error);

  void SetFrameClock(std::function<uint64_t()> clock);
  struct BudgetStats {
    uint64_t cut = 0, skipped = 0;
  };
  BudgetStats Budget() const;

  bool HasHandlers(const std::string& hook) const;

  int64_t CallInt(const std::string& hook, const Value& event, const std::string& resultKey, int64_t current);

  std::vector<Failure> TakeFailures();
  size_t LiveMods() const;
  std::vector<std::string> LiveModIds() const;
  uint64_t Digest() const;

private:
  struct Impl;
  std::unique_ptr<Impl> mImpl;
};

}

#endif
