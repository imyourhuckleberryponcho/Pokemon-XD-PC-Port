#ifndef ENGINE_MODS_MOD_VALUES_H
#define ENGINE_MODS_MOD_VALUES_H

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace engine::mods {

struct ModValueSet {
  std::string mod;
  std::string version;
  std::map<std::string, int64_t> values;
  bool operator==(const ModValueSet& o) const { return mod == o.mod && version == o.version && values == o.values; }
};

struct CurrencyInfo {
  std::string id, name;
  int64_t max = 999999;
};

class ModValues {
public:
  static constexpr int64_t kLimit = 999999999999ll;
  static constexpr size_t kMaxNames = 256;

  static bool ValidName(std::string_view name, std::string& why);

  void Clear();
  void Load(const std::vector<ModValueSet>& sets, size_t* dropped = nullptr);
  std::vector<ModValueSet> Export(const std::map<std::string, std::string>& versionOf) const;

  int64_t Get(const std::string& mod, const std::string& name) const;
  bool Has(const std::string& mod, const std::string& name) const;
  bool Set(const std::string& mod, const std::string& name, int64_t value, std::string& why);
  bool Add(const std::string& mod, const std::string& name, int64_t delta, int64_t& result, std::string& why);

  void ClearCurrencies() { mCurrencies.clear(); }
  bool Define(const std::string& mod, const std::string& id, const std::string& name, int64_t max, std::string& why);
  const CurrencyInfo* Currency(const std::string& mod, const std::string& id) const;
  std::vector<CurrencyInfo> Currencies(const std::string& mod) const;
  int64_t Balance(const std::string& mod, const std::string& id) const;
  bool Earn(const std::string& mod, const std::string& id, int64_t amount, int64_t& result, std::string& why);
  bool Spend(const std::string& mod, const std::string& id, int64_t amount, std::string& why);

  bool Empty() const;
  bool Changed() const { return mChanged; }
  void MarkSaved() { mChanged = false; }
  uint64_t Digest() const;

private:
  bool SetStored(const std::string& mod, const std::string& name, int64_t value, std::string& why);
  std::map<std::string, std::map<std::string, int64_t>> mValues;
  std::map<std::string, std::map<std::string, CurrencyInfo>> mCurrencies;
  bool mChanged = false;
};

}

#endif
