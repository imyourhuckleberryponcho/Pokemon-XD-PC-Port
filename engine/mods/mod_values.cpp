#include "mod_values.h"

namespace engine::mods {

namespace {
std::string CurrencyKey(const std::string& id) { return "currency:" + id; }
bool ValidStored(std::string_view name) {
  std::string why;
  if (name.rfind("currency:", 0) == 0) return ModValues::ValidName(name.substr(9), why);
  return ModValues::ValidName(name, why);
}
}

bool ModValues::ValidName(std::string_view name, std::string& why) {
  if (name.empty() || name.size() > 40) {
    why = "a name is 1 to 40 characters";
    return false;
  }
  if (name[0] < 'a' || name[0] > 'z') {
    why = "a name starts with a small letter (a-z)";
    return false;
  }
  for (const char c : name) {
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '.')) {
      why = "a name has only small letters, digits, underscore and dot (\"" + std::string(name) + "\" has something else)";
      return false;
    }
  }
  return true;
}

void ModValues::Clear() {
  mValues.clear();
  mCurrencies.clear();
  mChanged = false;
}

void ModValues::Load(const std::vector<ModValueSet>& sets, size_t* dropped) {
  mValues.clear();
  mChanged = false;
  size_t bad = 0;
  for (const ModValueSet& s : sets) {
    for (const auto& [name, value] : s.values) {
      if (!ValidStored(name) || value < -kLimit || value > kLimit || mValues[s.mod].size() >= kMaxNames) {
        bad++;
        continue;
      }
      mValues[s.mod][name] = value;
    }
  }
  if (dropped != nullptr) *dropped = bad;
}

std::vector<ModValueSet> ModValues::Export(const std::map<std::string, std::string>& versionOf) const {
  std::vector<ModValueSet> out;
  for (const auto& [mod, values] : mValues) {
    if (values.empty()) continue;
    ModValueSet s;
    s.mod = mod;
    const auto it = versionOf.find(mod);
    s.version = it != versionOf.end() ? it->second : std::string();
    s.values = values;
    out.push_back(std::move(s));
  }
  return out;
}

int64_t ModValues::Get(const std::string& mod, const std::string& name) const {
  const auto m = mValues.find(mod);
  if (m == mValues.end()) return 0;
  const auto v = m->second.find(name);
  return v == m->second.end() ? 0 : v->second;
}

bool ModValues::Has(const std::string& mod, const std::string& name) const {
  const auto m = mValues.find(mod);
  return m != mValues.end() && m->second.count(name) != 0;
}

bool ModValues::Set(const std::string& mod, const std::string& name, int64_t value, std::string& why) {
  if (!ValidName(name, why)) return false;
  return SetStored(mod, name, value, why);
}

bool ModValues::SetStored(const std::string& mod, const std::string& name, int64_t value, std::string& why) {
  if (value < -kLimit || value > kLimit) {
    why = "a saved number is between -999999999999 and 999999999999";
    return false;
  }
  auto& m = mValues[mod];
  const auto it = m.find(name);
  if (it == m.end()) {
    if (m.size() >= kMaxNames) {
      why = "a mod keeps at most 256 saved names";
      return false;
    }
    m[name] = value;
    mChanged = true;
  } else if (it->second != value) {
    it->second = value;
    mChanged = true;
  }
  return true;
}

bool ModValues::Add(const std::string& mod, const std::string& name, int64_t delta, int64_t& result, std::string& why) {
  if (!ValidName(name, why)) return false;
  if (delta > kLimit * 2 || delta < -kLimit * 2) {
    why = "the amount is too big";
    return false;
  }
  int64_t next = Get(mod, name) + delta;
  if (next > kLimit) next = kLimit;
  if (next < -kLimit) next = -kLimit;
  result = next;
  return Set(mod, name, next, why);
}

bool ModValues::Define(const std::string& mod, const std::string& id, const std::string& name, int64_t max, std::string& why) {
  if (!ValidName(id, why)) return false;
  if (name.empty() || name.size() > 24) {
    why = "a currency's display name is 1 to 24 characters";
    return false;
  }
  if (max < 1 || max > kLimit) {
    why = "a currency's cap is 1 to 999999999999";
    return false;
  }
  if (mCurrencies[mod].size() >= 8 && mCurrencies[mod].count(id) == 0) {
    why = "a mod has at most 8 currencies";
    return false;
  }
  mCurrencies[mod][id] = CurrencyInfo{id, name, max};
  return true;
}

const CurrencyInfo* ModValues::Currency(const std::string& mod, const std::string& id) const {
  const auto m = mCurrencies.find(mod);
  if (m == mCurrencies.end()) return nullptr;
  const auto c = m->second.find(id);
  return c == m->second.end() ? nullptr : &c->second;
}

std::vector<CurrencyInfo> ModValues::Currencies(const std::string& mod) const {
  std::vector<CurrencyInfo> out;
  const auto m = mCurrencies.find(mod);
  if (m != mCurrencies.end()) {
    for (const auto& [id, c] : m->second) out.push_back(c);
  }
  return out;
}

int64_t ModValues::Balance(const std::string& mod, const std::string& id) const { return Get(mod, CurrencyKey(id)); }

bool ModValues::Earn(const std::string& mod, const std::string& id, int64_t amount, int64_t& result, std::string& why) {
  const CurrencyInfo* c = Currency(mod, id);
  if (c == nullptr) {
    why = "no currency called \"" + id + "\" (declare it first with define_currency)";
    return false;
  }
  if (amount < 0 || amount > kLimit) {
    why = "earn an amount of 0 or more";
    return false;
  }
  int64_t next = Balance(mod, id) + amount;
  if (next > c->max) next = c->max;
  result = next;
  return SetStored(mod, CurrencyKey(id), next, why);
}

bool ModValues::Spend(const std::string& mod, const std::string& id, int64_t amount, std::string& why) {
  const CurrencyInfo* c = Currency(mod, id);
  if (c == nullptr) {
    why = "no currency called \"" + id + "\" (declare it first with define_currency)";
    return false;
  }
  if (amount < 0 || amount > kLimit) {
    why = "spend an amount of 0 or more";
    return false;
  }
  const int64_t now = Balance(mod, id);
  if (now < amount) {
    why = "not enough " + c->name + " (have " + std::to_string(now) + ", need " + std::to_string(amount) + ")";
    return false;
  }
  return SetStored(mod, CurrencyKey(id), now - amount, why);
}

bool ModValues::Empty() const {
  for (const auto& [mod, values] : mValues) {
    if (!values.empty()) return false;
  }
  return true;
}

uint64_t ModValues::Digest() const {
  uint64_t h = 1469598103934665603ull;
  const auto mix = [&](const std::string& s) {
    for (const char c : s) h = (h ^ static_cast<uint8_t>(c)) * 1099511628211ull;
    h = (h ^ 0xFF) * 1099511628211ull;
  };
  for (const auto& [mod, values] : mValues) {
    for (const auto& [name, v] : values) {
      mix(mod);
      mix(name);
      mix(std::to_string(v));
    }
  }
  return h;
}

}
