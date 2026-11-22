#ifndef ENGINE_LABELS_H
#define ENGINE_LABELS_H

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace engine::mods {

constexpr const char* kBaseNamespace = "base";
constexpr size_t kMaxLabelLength = 96;

struct Label {
  std::string kind;
  std::string ns;
  std::string slug;
  std::string Text() const { return kind + ":" + ns + "/" + slug; }
  bool operator==(const Label& o) const { return kind == o.kind && ns == o.ns && slug == o.slug; }
};

bool ValidKind(std::string_view kind);
bool ValidSlug(std::string_view slug);
bool ValidNamespace(std::string_view ns);
bool ParseLabel(std::string_view text, Label& out, std::string& why);
bool LooksLikeLabel(std::string_view text);

std::string Slugify(std::string_view name);

class BaseLabels {
public:
  BaseLabels() = default;
  BaseLabels(std::string kind, const std::vector<std::pair<std::string, uint32_t>>& names);
  const std::string& Kind() const { return mKind; }
  bool Find(const std::string& slug, uint32_t& number) const;
  bool SlugOf(uint32_t number, std::string& slug) const;
  size_t Size() const { return mBySlug.size(); }
  const std::map<std::string, uint32_t>& Slugs() const { return mBySlug; }

private:
  std::string mKind;
  std::map<std::string, uint32_t> mBySlug;
  std::map<uint32_t, std::string> mByNumber;
};

struct LabelRow {
  std::string label;
  uint32_t number = 0;
  std::string legacy;
  std::string describe;
};

struct RemapStep {
  std::string kind;
  std::string label;
  uint32_t from = 0, to = 0;
};

struct RemapPlan {
  std::vector<std::string> problems;
  std::vector<RemapStep> steps;
  bool Refused() const { return !problems.empty(); }
  bool Needed() const { return problems.empty() && !steps.empty(); }
  std::map<uint32_t, uint32_t> Map(const std::string& kind) const;
};

RemapPlan PlanRemap(const std::vector<LabelRow>& saved, const std::vector<LabelRow>& now);

std::string KindOf(const std::string& label);

}

#endif
