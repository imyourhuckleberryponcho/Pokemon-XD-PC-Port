#include "settings.h"
#include "settings_c.h"

#include <charconv>
#if defined(__APPLE__) || defined(PORT_TEST_MAC_HALVES)
#include <port_mac_from_chars.h>
#endif
#include <cstdio>
#include <cstring>

namespace engine::settings {

const char* PresetId(Preset preset) {
  switch (preset) {
  case Preset::Original: return "original";
  case Preset::Enhanced: return "enhanced";
  case Preset::Custom: return "custom";
  }
  return "original";
}

bool ParsePreset(std::string_view text, Preset& out) {
  for (Preset p : {Preset::Original, Preset::Enhanced, Preset::Custom}) {
    if (text == PresetId(p)) {
      out = p;
      return true;
    }
  }
  return false;
}

const char* CategoryId(Category category) {
  switch (category) {
  case Category::Video: return "video";
  case Category::Audio: return "audio";
  case Category::Gameplay: return "gameplay";
  case Category::Controls: return "controls";
  case Category::Accessibility: return "accessibility";
  case Category::System: return "system";
  }
  return "gameplay";
}

const char* CategoryName(Category category) {
  switch (category) {
  case Category::Video: return "Video";
  case Category::Audio: return "Audio";
  case Category::Gameplay: return "Gameplay";
  case Category::Controls: return "Controls";
  case Category::Accessibility: return "Accessibility";
  case Category::System: return "System";
  }
  return "Gameplay";
}

const char* TypeId(Type type) {
  switch (type) {
  case Type::Bool: return "bool";
  case Type::Int: return "int";
  case Type::Float: return "float";
  case Type::Enum: return "enum";
  case Type::Text: return "text";
  }
  return "bool";
}

Definition Definition::Bool(std::string id, Category category, std::string name, std::string description,
                            bool original, bool enhanced) {
  Definition d;
  d.id = std::move(id);
  d.type = Type::Bool;
  d.category = category;
  d.name = std::move(name);
  d.description = std::move(description);
  d.original.i = original ? 1 : 0;
  d.enhanced.i = enhanced ? 1 : 0;
  return d;
}

Definition Definition::Int(std::string id, Category category, std::string name, std::string description,
                           int32_t min, int32_t max, int32_t original, int32_t enhanced) {
  Definition d;
  d.id = std::move(id);
  d.type = Type::Int;
  d.category = category;
  d.name = std::move(name);
  d.description = std::move(description);
  d.minInt = min;
  d.maxInt = max;
  d.original.i = original;
  d.enhanced.i = enhanced;
  return d;
}

Definition Definition::Float(std::string id, Category category, std::string name, std::string description,
                             float min, float max, float original, float enhanced) {
  Definition d;
  d.id = std::move(id);
  d.type = Type::Float;
  d.category = category;
  d.name = std::move(name);
  d.description = std::move(description);
  d.minFloat = min;
  d.maxFloat = max;
  d.original.f = original;
  d.enhanced.f = enhanced;
  return d;
}

Definition Definition::Enum(std::string id, Category category, std::string name, std::string description,
                            std::vector<Choice> choices, std::string_view original, std::string_view enhanced) {
  Definition d;
  d.id = std::move(id);
  d.type = Type::Enum;
  d.category = category;
  d.name = std::move(name);
  d.description = std::move(description);
  d.choices = std::move(choices);
  d.original.i = d.enhanced.i = -1;
  for (size_t c = 0; c < d.choices.size(); c++) {
    if (d.choices[c].id == original) {
      d.original.i = static_cast<int32_t>(c);
    }
    if (d.choices[c].id == enhanced) {
      d.enhanced.i = static_cast<int32_t>(c);
    }
  }
  return d;
}

Definition Definition::Text(std::string id, Category category, std::string name, std::string description,
                            std::string original, std::string enhanced,
                            std::function<bool(std::string_view)> validate) {
  Definition d;
  d.id = std::move(id);
  d.type = Type::Text;
  d.category = category;
  d.name = std::move(name);
  d.description = std::move(description);
  d.original.s = std::move(original);
  d.enhanced.s = std::move(enhanced);
  d.validate = std::move(validate);
  return d;
}

Definition& Definition::Preference() {
  preference = true;
  enhanced = original;
  return *this;
}

Definition& Definition::Testing() {
  testing = true;
  return Preference();
}

Definition& Definition::Implemented(bool yes) {
  implemented = yes;
  return *this;
}

Definition& Definition::ForGames(std::vector<std::string> gameIds) {
  games = std::move(gameIds);
  return *this;
}

Definition& Definition::FormerIds(std::vector<std::string> ids) {
  formerIds = std::move(ids);
  return *this;
}

Definition& Definition::OriginalNote(std::string note) {
  originalNote = std::move(note);
  return *this;
}

Definition& Definition::IntNames(std::vector<std::pair<int32_t, std::string>> names) {
  intNames = std::move(names);
  return *this;
}

std::string Definition::IntName(int32_t value) const {
  for (const auto& [v, name] : intNames) {
    if (v == value) {
      return name;
    }
  }
  return {};
}

bool Definition::AppliesTo(std::string_view gameId) const {
  if (games.empty()) {
    return true;
  }
  for (const std::string& g : games) {
    if (g == gameId) {
      return true;
    }
  }
  return false;
}

namespace {

bool ValidName(std::string_view s, bool dots) {
  if (s.empty() || s.front() == '.' || s.back() == '.') {
    return false;
  }
  char prev = 0;
  for (char ch : s) {
    const bool ok = (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '_' || (dots && ch == '.');
    if (!ok || (ch == '.' && prev == '.')) {
      return false;
    }
    prev = ch;
  }
  return true;
}

bool ValidId(std::string_view s) { return ValidName(s, true) && s.find('.') != std::string_view::npos; }

std::string_view Trim(std::string_view s) {
  while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) {
    s.remove_prefix(1);
  }
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n')) {
    s.remove_suffix(1);
  }
  return s;
}

}

Registry::Registry() {
  warn_ = [](const std::string& message) {
    std::fprintf(stderr, "[engine/settings] %s\n", message.c_str());
    std::fflush(stderr);
  };
}

void Registry::Warn(const std::string& message) const {
  if (loading_) {
    report_.warnings.push_back(message);
  }
  if (warn_) {
    warn_(message);
  }
}

bool Registry::Fits(const Definition& def, const Value& value) const {
  switch (def.type) {
  case Type::Bool: return value.i == 0 || value.i == 1;
  case Type::Int: return value.i >= def.minInt && value.i <= def.maxInt;
  case Type::Float: return value.f >= def.minFloat && value.f <= def.maxFloat;
  case Type::Enum: return value.i >= 0 && value.i < static_cast<int32_t>(def.choices.size());
  case Type::Text: {
    const std::string& s = value.s;
    if (s.find_first_of("\r\n") != std::string::npos) {
      return false;
    }
    if (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.back() == ' ' || s.back() == '\t')) {
      return false;
    }
    return !def.validate || def.validate(s);
  }
  }
  return false;
}

bool Registry::SameValue(const Definition& def, const Value& a, const Value& b) {
  switch (def.type) {
  case Type::Float: return a.f == b.f;
  case Type::Text: return a.s == b.s;
  default: return a.i == b.i;
  }
}

void Registry::Publish(size_t index) {
  Entry& e = entries_[index];
  const Value& v = e.over ? *e.over : e.own;
  if (e.def.type == Type::Float) {
    e.slot.f.store(v.f, std::memory_order_relaxed);
  } else if (e.def.type == Type::Text) {
    e.text = v.s;
  } else {
    e.slot.i.store(v.i, std::memory_order_relaxed);
  }
}

void Registry::StoreOwn(size_t index, const Value& value) {
  entries_[index].own = value;
  Publish(index);
}

void Registry::Touched() {
  bool original = true;
  for (size_t i = 0; i < entries_.size() && original; i++) {
    const Entry& e = entries_[i];
    if (!e.def.preference && !SameValue(e.def, ValueOf(i), e.def.original)) {
      original = false;
    }
  }
  original_.store(original, std::memory_order_relaxed);
  changes_.fetch_add(1, std::memory_order_relaxed);
}

bool Registry::Add(const Definition& definition) {
  const Definition& d = definition;
  const std::string what = "setting \"" + d.id + "\" not registered: ";
  if (!ValidId(d.id)) {
    Warn(what + "the id must look like \"section.name\" (lowercase letters, digits, '_')");
    return false;
  }
  if (byId_.count(d.id) != 0 || byFormerId_.count(d.id) != 0) {
    Warn(what + "the id is already taken");
    return false;
  }
  for (const std::string& former : d.formerIds) {
    if (!ValidId(former) || byId_.count(former) != 0 || byFormerId_.count(former) != 0 || former == d.id) {
      Warn(what + "former id \"" + former + "\" is malformed or already taken");
      return false;
    }
  }
  if (d.type == Type::Enum) {
    for (size_t c = 0; c < d.choices.size(); c++) {
      if (!ValidName(d.choices[c].id, false)) {
        Warn(what + "choice \"" + d.choices[c].id + "\" has a malformed id");
        return false;
      }
      for (size_t o = 0; o < c; o++) {
        if (d.choices[o].id == d.choices[c].id) {
          Warn(what + "choice \"" + d.choices[c].id + "\" appears twice");
          return false;
        }
      }
    }
  }
  if (!Fits(d, d.original) || !Fits(d, d.enhanced)) {
    Warn(what + "a preset default is outside the allowed values");
    return false;
  }
  if ((d.preference || d.testing) && !SameValue(d, d.original, d.enhanced)) {
    Warn(what + "a preference must have the same default in every preset");
    return false;
  }

  entries_.emplace_back(d);
  const size_t index = entries_.size() - 1;
  Entry& e = entries_.back();
  if (e.def.testing) {
    e.def.preference = true;
  }
  byId_.emplace(d.id, index);
  for (const std::string& former : d.formerIds) {
    byFormerId_.emplace(former, index);
  }
  StoreOwn(index, DefaultOf(index, preset_));
  Touched();

  std::vector<std::string> names{d.id};
  names.insert(names.end(), d.formerIds.begin(), d.formerIds.end());
  for (const std::string& name : names) {
    auto it = unknown_.find(name);
    if (it == unknown_.end()) {
      continue;
    }
    const std::string text = std::move(it->second);
    unknown_.erase(it);
    if (name != d.id) {
      report_.renamed.emplace_back(name, d.id);
    }
    if (d.testing) {
      Warn("settings.ini: \"" + name + "\" can only be set for one run (not in settings.ini); ignored");
      continue;
    }
    Value v;
    if (ParseValue(e.def, text, v)) {
      Set(index, v);
    } else {
      Warn("settings.ini: \"" + name + " = " + text + "\" is not a valid value; using the default");
    }
  }
  return true;
}

size_t Registry::Find(std::string_view id) const {
  auto it = byId_.find(std::string(id));
  return it == byId_.end() ? kNoSetting : it->second;
}

size_t Registry::FindFormer(std::string_view id) const {
  auto it = byFormerId_.find(std::string(id));
  return it == byFormerId_.end() ? kNoSetting : it->second;
}

template <Type T>
Handle<T> Registry::MakeHandle(std::string_view id) const {
  Handle<T> h;
  const size_t index = Find(id);
  if (index != kNoSetting && entries_[index].def.type == T) {
    h.slot_ = &entries_[index].slot;
    h.index_ = index;
  }
  return h;
}

BoolSetting Registry::Bool(std::string_view id) const { return MakeHandle<Type::Bool>(id); }
IntSetting Registry::Int(std::string_view id) const { return MakeHandle<Type::Int>(id); }
FloatSetting Registry::Float(std::string_view id) const { return MakeHandle<Type::Float>(id); }
EnumSetting Registry::Enum(std::string_view id) const { return MakeHandle<Type::Enum>(id); }

int32_t Registry::ChoiceIndex(std::string_view id, std::string_view choiceId) const {
  const size_t index = Find(id);
  if (index == kNoSetting) {
    return -1;
  }
  const std::vector<Choice>& choices = entries_[index].def.choices;
  for (size_t c = 0; c < choices.size(); c++) {
    if (choices[c].id == choiceId) {
      return static_cast<int32_t>(c);
    }
  }
  return -1;
}

std::string Registry::Text(std::string_view id) const {
  const size_t index = Find(id);
  return index != kNoSetting && entries_[index].def.type == Type::Text ? entries_[index].text : std::string();
}

Value Registry::ValueOf(size_t index) const {
  const Entry& e = entries_[index];
  return Value{e.slot.i.load(std::memory_order_relaxed), e.slot.f.load(std::memory_order_relaxed), e.text};
}

Value Registry::SavedValueOf(size_t index) const { return entries_[index].own; }

Value Registry::DefaultOf(size_t index, Preset preset) const {
  const Definition& d = entries_[index].def;
  if (preset == Preset::Custom) {
    preset = base_;
  }
  return preset == Preset::Enhanced ? d.enhanced : d.original;
}

std::string Registry::TextOf(size_t index, const Value& value) const {
  const Definition& d = entries_[index].def;
  switch (d.type) {
  case Type::Bool: return value.i ? "on" : "off";
  case Type::Int: return std::to_string(value.i);
  case Type::Float: {
    char buf[32];
    const auto r = std::to_chars(buf, buf + sizeof(buf), value.f);
    return std::string(buf, r.ptr);
  }
  case Type::Enum:
    return value.i >= 0 && value.i < static_cast<int32_t>(d.choices.size()) ? d.choices[static_cast<size_t>(value.i)].id
                                                                            : std::string("?");
  case Type::Text: return value.s;
  }
  return {};
}

std::string Registry::ValueText(size_t index) const { return TextOf(index, ValueOf(index)); }

bool Registry::Set(size_t index, Value value) {
  if (index >= entries_.size()) {
    return false;
  }
  Entry& e = entries_[index];
  if (e.def.testing || !Fits(e.def, value)) {
    return false;
  }
  if (SameValue(e.def, e.own, value)) {
    return true;
  }
  if (!e.def.preference && preset_ != Preset::Custom && !SameValue(e.def, DefaultOf(index, preset_), value)) {
    base_ = preset_;
    preset_ = Preset::Custom;
  }
  StoreOwn(index, value);
  Touched();
  return true;
}

bool Registry::SetBool(std::string_view id, bool value) {
  const size_t index = Find(id);
  return index != kNoSetting && entries_[index].def.type == Type::Bool && Set(index, Value{value ? 1 : 0, 0.0f});
}

bool Registry::SetInt(std::string_view id, int32_t value) {
  const size_t index = Find(id);
  return index != kNoSetting && entries_[index].def.type == Type::Int && Set(index, Value{value, 0.0f});
}

bool Registry::SetFloat(std::string_view id, float value) {
  const size_t index = Find(id);
  return index != kNoSetting && entries_[index].def.type == Type::Float && Set(index, Value{0, value});
}

bool Registry::SetEnum(std::string_view id, std::string_view choiceId) {
  const size_t index = Find(id);
  if (index == kNoSetting || entries_[index].def.type != Type::Enum) {
    return false;
  }
  const int32_t c = ChoiceIndex(id, choiceId);
  return c >= 0 && Set(index, Value{c, 0.0f});
}

bool Registry::SetText(std::string_view id, std::string_view text) {
  const size_t index = Find(id);
  if (index == kNoSetting || entries_[index].def.type != Type::Text) {
    return false;
  }
  Value v;
  v.s = std::string(text);
  return Set(index, v);
}

bool Registry::SetFromText(std::string_view id, std::string_view text) {
  const size_t index = Find(id);
  Value v;
  return index != kNoSetting && ParseValue(entries_[index].def, text, v) && Set(index, v);
}

bool Registry::ParseValue(const Definition& def, std::string_view text, Value& out) const {
  out = Value{};
  switch (def.type) {
  case Type::Bool:
    if (text == "on" || text == "true" || text == "yes" || text == "1") {
      out.i = 1;
    } else if (text == "off" || text == "false" || text == "no" || text == "0") {
      out.i = 0;
    } else {
      return false;
    }
    break;
  case Type::Int: {
    const auto r = std::from_chars(text.data(), text.data() + text.size(), out.i);
    if (text.empty() || r.ec != std::errc() || r.ptr != text.data() + text.size()) {
      return false;
    }
    break;
  }
  case Type::Float: {
#if defined(__APPLE__) || defined(PORT_TEST_MAC_HALVES)
    const auto r = port_mac::from_chars(text.data(), text.data() + text.size(), out.f);
#else
    const auto r = std::from_chars(text.data(), text.data() + text.size(), out.f);
#endif
    if (text.empty() || r.ec != std::errc() || r.ptr != text.data() + text.size()) {
      return false;
    }
    break;
  }
  case Type::Enum: {
    out.i = -1;
    for (size_t c = 0; c < def.choices.size(); c++) {
      if (def.choices[c].id == text) {
        out.i = static_cast<int32_t>(c);
      }
    }
    break;
  }
  case Type::Text: out.s = std::string(text); break;
  }
  return Fits(def, out);
}

void Registry::ApplyPreset(Preset preset) {
  if (preset == Preset::Custom) {
    if (preset_ != Preset::Custom) {
      base_ = preset_;
      preset_ = Preset::Custom;
    }
    return;
  }
  preset_ = base_ = preset;
  bool changed = false;
  for (size_t i = 0; i < entries_.size(); i++) {
    Entry& e = entries_[i];
    if (!e.def.preference && !SameValue(e.def, e.own, DefaultOf(i, preset))) {
      StoreOwn(i, DefaultOf(i, preset));
      changed = true;
    }
  }
  if (changed) {
    Touched();
  }
}

std::vector<size_t> Registry::NotOriginal() const {
  std::vector<size_t> out;
  for (size_t i = 0; i < entries_.size(); i++) {
    const Entry& e = entries_[i];
    if (!e.def.preference && !SameValue(e.def, ValueOf(i), e.def.original)) {
      out.push_back(i);
    }
  }
  return out;
}

std::string Registry::Summary() const {
  std::string s = IsOriginal() ? "original" : "not original";
  s += std::string(" (preset ") + PresetId(preset_);
  if (preset_ == Preset::Custom) {
    s += std::string(", based on ") + PresetId(base_);
  }
  s += ")";
  const std::vector<size_t> differs = NotOriginal();
  for (size_t n = 0; n < differs.size(); n++) {
    s += (n == 0 ? ": " : ", ") + entries_[differs[n]].def.id + "=" + ValueText(differs[n]);
  }
  bool first = true;
  for (size_t i = 0; i < entries_.size(); i++) {
    const Entry& e = entries_[i];
    const bool overridden = e.over && !SameValue(e.def, *e.over, e.own);
    const bool testing = e.def.testing && !SameValue(e.def, ValueOf(i), e.def.original);
    if (overridden || testing) {
      s += (first ? "; for this run: " : ", ") + e.def.id + "=" + ValueText(i);
      first = false;
    }
  }
  return s;
}

std::string Registry::ReplayText() const {
  std::string s = "preset=original";
  for (size_t i : NotOriginal()) {
    s += ";" + entries_[i].def.id + "=" + ValueText(i);
  }
  return s;
}

bool Registry::Override(std::string_view id, std::string_view text) {
  size_t index = Find(id);
  if (index == kNoSetting) {
    index = FindFormer(id);
  }
  if (index == kNoSetting) {
    Warn("override \"" + std::string(id) + "\": no such setting; ignored");
    return false;
  }
  Entry& e = entries_[index];
  Value v;
  if (!ParseValue(e.def, text, v)) {
    Warn("override \"" + std::string(id) + "=" + std::string(text) + "\": not a valid value; ignored");
    return false;
  }
  e.over = v;
  Publish(index);
  Touched();
  return true;
}

void Registry::OverridePreset(Preset preset) {
  for (size_t i = 0; i < entries_.size(); i++) {
    Entry& e = entries_[i];
    if (e.def.preference) {
      continue;
    }
    if (preset == Preset::Custom) {
      e.over.reset();
    } else {
      e.over = preset == Preset::Enhanced ? e.def.enhanced : e.def.original;
    }
    Publish(i);
  }
  Touched();
}

int Registry::ApplyOverrides(std::string_view list) {
  int refused = 0;
  while (!list.empty()) {
    const size_t end = list.find(';');
    const std::string_view item = Trim(list.substr(0, end));
    list = end == std::string_view::npos ? std::string_view() : list.substr(end + 1);
    if (item.empty()) {
      continue;
    }
    const size_t eq = item.find('=');
    const std::string_view id = eq == std::string_view::npos ? item : Trim(item.substr(0, eq));
    const std::string_view value = eq == std::string_view::npos ? std::string_view() : Trim(item.substr(eq + 1));
    if (eq == std::string_view::npos || id.empty()) {
      Warn("override \"" + std::string(item) + "\" is not \"id=value\"; ignored");
      refused++;
    } else if (id == "preset") {
      Preset p;
      if (ParsePreset(value, p)) {
        OverridePreset(p);
      } else {
        Warn("override: unknown preset \"" + std::string(value) + "\"; ignored");
        refused++;
      }
    } else if (!Override(id, value)) {
      refused++;
    }
  }
  return refused;
}

void Registry::ClearOverrides() {
  for (size_t i = 0; i < entries_.size(); i++) {
    if (entries_[i].over) {
      entries_[i].over.reset();
      Publish(i);
    }
  }
  Touched();
}

void Registry::ClearOverride(size_t index) {
  if (index < entries_.size() && entries_[index].over) {
    entries_[index].over.reset();
    Publish(index);
    Touched();
  }
}

Registry& Global() {
  static Registry registry;
  return registry;
}

}

namespace {
using namespace engine::settings;

const int32_t* IntWord(const Slot* slot) { return reinterpret_cast<const int32_t*>(&slot->i); }
const float* FloatWord(const Slot* slot) { return reinterpret_cast<const float*>(&slot->f); }
}

extern "C" const int32_t* EngineSettings_IntSlot(const char* id) {
  const size_t index = Global().Find(id);
  if (index == kNoSetting || Global().Get(index).type == Type::Float || Global().Get(index).type == Type::Text) {
    return nullptr;
  }
  return IntWord(Global().SlotOf(index));
}

extern "C" const float* EngineSettings_FloatSlot(const char* id) {
  const size_t index = Global().Find(id);
  if (index == kNoSetting || Global().Get(index).type != Type::Float) {
    return nullptr;
  }
  return FloatWord(Global().SlotOf(index));
}

extern "C" int32_t EngineSettings_ChoiceIndex(const char* id, const char* choiceId) {
  return Global().ChoiceIndex(id, choiceId);
}

extern "C" int EngineSettings_IsOriginal(void) { return Global().IsOriginal() ? 1 : 0; }

namespace {
size_t CopyOut(const std::string& s, char* buffer, size_t size) {
  if (buffer != nullptr && size > 0) {
    const size_t n = s.size() < size - 1 ? s.size() : size - 1;
    std::memcpy(buffer, s.data(), n);
    buffer[n] = '\0';
  }
  return s.size();
}
}

extern "C" size_t EngineSettings_Summary(char* buffer, size_t size) { return CopyOut(Global().Summary(), buffer, size); }

extern "C" size_t EngineSettings_ReplayText(char* buffer, size_t size) {
  return CopyOut(Global().ReplayText(), buffer, size);
}
