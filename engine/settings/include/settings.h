#ifndef ENGINE_SETTINGS_H
#define ENGINE_SETTINGS_H

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace engine::settings {

enum class Type { Bool, Int, Float, Enum, Text };

enum class Preset { Original, Enhanced, Custom };

enum class Category { Video, Audio, Gameplay, Controls, Accessibility, System };

const char* PresetId(Preset preset);
const char* CategoryId(Category category);
const char* CategoryName(Category category);
const char* TypeId(Type type);
bool ParsePreset(std::string_view text, Preset& out);

struct Choice {
  std::string id;
  std::string name;
};

struct Value {
  int32_t i = 0;
  float f = 0.0f;
  std::string s;
};

struct Definition {
  std::string id;
  Type type = Type::Bool;
  Category category = Category::Gameplay;
  std::string name;
  std::string description;
  int32_t minInt = 0, maxInt = 0;
  float minFloat = 0.0f, maxFloat = 0.0f;
  std::vector<Choice> choices;
  std::function<bool(std::string_view)> validate;
  Value original;
  Value enhanced;
  bool preference = false;
  bool testing = false;
  bool implemented = false;
  std::vector<std::string> games;
  std::vector<std::string> formerIds;
  std::string originalNote;
  std::vector<std::pair<int32_t, std::string>> intNames;

  static Definition Bool(std::string id, Category category, std::string name, std::string description,
                         bool original, bool enhanced);
  static Definition Int(std::string id, Category category, std::string name, std::string description,
                        int32_t min, int32_t max, int32_t original, int32_t enhanced);
  static Definition Float(std::string id, Category category, std::string name, std::string description,
                          float min, float max, float original, float enhanced);
  static Definition Enum(std::string id, Category category, std::string name, std::string description,
                         std::vector<Choice> choices, std::string_view original, std::string_view enhanced);
  static Definition Text(std::string id, Category category, std::string name, std::string description,
                         std::string original, std::string enhanced,
                         std::function<bool(std::string_view)> validate = {});
  Definition& Preference();
  Definition& Testing();
  Definition& Implemented(bool yes = true);
  Definition& ForGames(std::vector<std::string> gameIds);
  Definition& FormerIds(std::vector<std::string> ids);
  Definition& OriginalNote(std::string note);
  Definition& IntNames(std::vector<std::pair<int32_t, std::string>> names);
  std::string IntName(int32_t value) const;

  bool AppliesTo(std::string_view gameId) const;
};

struct Slot {
  std::atomic<int32_t> i{0};
  std::atomic<float> f{0.0f};
};
static_assert(sizeof(std::atomic<int32_t>) == sizeof(int32_t) && std::atomic<int32_t>::is_always_lock_free &&
                  sizeof(std::atomic<float>) == sizeof(float) && std::atomic<float>::is_always_lock_free,
              "settings_c.h hands C code the slots as plain 32-bit words");

inline constexpr size_t kNoSetting = static_cast<size_t>(-1);

template <Type T>
class Handle {
 public:
  using ValueType = std::conditional_t<T == Type::Bool, bool, std::conditional_t<T == Type::Float, float, int32_t>>;

  ValueType Get() const {
    if constexpr (T == Type::Bool) {
      return slot_->i.load(std::memory_order_relaxed) != 0;
    } else if constexpr (T == Type::Float) {
      return slot_->f.load(std::memory_order_relaxed);
    } else {
      return slot_->i.load(std::memory_order_relaxed);
    }
  }
  bool Valid() const { return index_ != kNoSetting; }
  size_t Index() const { return index_; }

 private:
  friend class Registry;
  static inline const Slot kInvalid{};
  const Slot* slot_ = &kInvalid;
  size_t index_ = kNoSetting;
};

using BoolSetting = Handle<Type::Bool>;
using IntSetting = Handle<Type::Int>;
using FloatSetting = Handle<Type::Float>;
using EnumSetting = Handle<Type::Enum>;

struct LoadReport {
  bool found = false;
  std::filesystem::path path;
  int format = 0;
  bool migrated = false;
  bool newerFormat = false;
  std::vector<std::string> warnings;
  std::vector<std::string> unknownIds;
  std::vector<std::pair<std::string, std::string>> renamed;
};

class Registry {
 public:
  Registry();
  Registry(const Registry&) = delete;
  Registry& operator=(const Registry&) = delete;

  bool Add(const Definition& definition);

  size_t Count() const { return entries_.size(); }
  size_t Find(std::string_view id) const;
  const Definition& Get(size_t index) const { return entries_[index].def; }

  BoolSetting Bool(std::string_view id) const;
  IntSetting Int(std::string_view id) const;
  FloatSetting Float(std::string_view id) const;
  EnumSetting Enum(std::string_view id) const;
  int32_t ChoiceIndex(std::string_view id, std::string_view choiceId) const;

  std::string Text(std::string_view id) const;

  Value ValueOf(size_t index) const;
  Value SavedValueOf(size_t index) const;
  Value DefaultOf(size_t index, Preset preset) const;
  std::string ValueText(size_t index) const;
  std::string TextOf(size_t index, const Value& value) const;
  const Slot* SlotOf(size_t index) const { return &entries_[index].slot; }

  bool Set(size_t index, Value value);
  bool SetBool(std::string_view id, bool value);
  bool SetInt(std::string_view id, int32_t value);
  bool SetFloat(std::string_view id, float value);
  bool SetEnum(std::string_view id, std::string_view choiceId);
  bool SetText(std::string_view id, std::string_view text);
  bool SetFromText(std::string_view id, std::string_view text);

  Preset CurrentPreset() const { return preset_; }
  Preset BasePreset() const { return base_; }
  void ApplyPreset(Preset preset);

  bool IsOriginal() const { return original_.load(std::memory_order_relaxed); }
  std::vector<size_t> NotOriginal() const;
  std::string Summary() const;
  std::string ReplayText() const;

  uint32_t Changes() const { return changes_.load(std::memory_order_relaxed); }

  bool Override(std::string_view id, std::string_view text);
  void OverridePreset(Preset preset);
  int ApplyOverrides(std::string_view list);
  void ClearOverrides();
  void ClearOverride(size_t index);
  bool IsOverridden(size_t index) const { return entries_[index].over.has_value(); }

  bool Load(const std::filesystem::path& folder);
  bool LoadFile(const std::filesystem::path& file);
  bool Save(const std::filesystem::path& folder) const;
  bool SaveFile(const std::filesystem::path& file) const;
  std::string FileText() const;
  static constexpr const char* kFileName = "settings.ini";
  static constexpr int kFormatVersion = 1;
  std::vector<std::string> UnknownIds() const;
  const LoadReport& LastLoad() const { return report_; }

  void SetWarningSink(std::function<void(const std::string&)> sink) { warn_ = std::move(sink); }

 private:
  struct Entry {
    explicit Entry(const Definition& d) : def(d) {}
    Definition def;
    Slot slot;
    std::string text;
    Value own;
    std::optional<Value> over;
  };

  template <Type T>
  Handle<T> MakeHandle(std::string_view id) const;
  bool ParseValue(const Definition& def, std::string_view text, Value& out) const;
  bool Fits(const Definition& def, const Value& value) const;
  static bool SameValue(const Definition& def, const Value& a, const Value& b);
  void StoreOwn(size_t index, const Value& value);
  void Publish(size_t index);
  void Touched();
  void Warn(const std::string& message) const;
  void ParseFile(std::string_view text);
  std::string WriteFile() const;
  size_t FindFormer(std::string_view id) const;

  std::deque<Entry> entries_;
  std::unordered_map<std::string, size_t> byId_;
  std::unordered_map<std::string, size_t> byFormerId_;
  Preset preset_ = Preset::Original;
  Preset base_ = Preset::Original;
  std::atomic<uint32_t> changes_{0};
  std::atomic<bool> original_{true};
  std::map<std::string, std::string> unknown_;
  mutable LoadReport report_;
  bool loading_ = false;
  std::function<void(const std::string&)> warn_;
};

Registry& Global();

void RegisterStandardSettings(Registry& registry);

std::vector<std::string> CheckRules(const Registry& registry);

std::string SettingsMarkdown(const Registry& registry, std::string_view title, std::string_view intro);

}

#endif
