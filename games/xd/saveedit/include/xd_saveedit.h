#ifndef XD_SAVEEDIT_H
#define XD_SAVEEDIT_H

#include <xd_legality.h>
#include <xd_save.h>
#include <xd_shadow_orders.h>

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace xd::saveedit {

enum class Area { Party, Box };

struct Spot {
  Area area = Area::Party;
  int box = 0;
  int slot = 0;
  bool operator==(const Spot& o) const { return area == o.area && box == o.box && slot == o.slot; }
};
std::string SpotText(const Spot& s);

enum class Pocket { Items, KeyItems, Balls, TmHm, Berries, Colognes, BattleCds };
const char* PocketName(Pocket p);
int PocketSize(Pocket p);

struct Tables {
  const xd::legality::Rules* rules = nullptr;
  std::function<int(uint16_t species)> sexRatio;
  std::function<int(uint16_t item)> pocketOf;
  std::function<std::string(uint16_t ability)> abilityName;
  std::function<std::string(uint16_t row)> formName;
  std::function<int(uint16_t species)> baseFriendship;
  std::function<bool(uint32_t row)> shadowRowFree;
  bool moreShadows = false;
};

struct ShadowWish {
  uint16_t moves[4] = {xd::save::kShadowRush, 0, 0, 0};
  int gauge = 4000;
  int revers = 2;
};

struct Options {
  bool japanese = false;
};

struct Change {
  std::string text;
  bool marks = true;
  std::string kind;
};

struct PidWish {
  int nature = -1;
  int female = -1;
  int shiny = -1;
};

class Editor {
public:
  bool Open(const std::vector<uint8_t>& file, const Options& options, std::string& error);

  const xd::save::Save& save() const { return work_; }
  const std::vector<uint8_t>& original() const { return file_; }
  const xd::save::Save& originalSave() const { return orig_; }
  const std::vector<Change>& changes() const { return log_; }
  bool Dirty() const { return !log_.empty(); }
  bool WillMark() const;
  std::string MarkReason() const;

  const xd::save::Pokemon* At(const Spot& s) const;
  bool IsShadow(const Spot& s) const;
  bool UnpurifiedShadow(const xd::save::Pokemon& p) const;

  std::string SetSpecies(const Spot& s, uint16_t species, const Tables& t);
  std::string SetLevel(const Spot& s, int level, const Tables& t);
  std::string SetNickname(const Spot& s, const std::u16string& name);
  std::string SetHeldItem(const Spot& s, uint16_t item, const Tables& t);
  std::string SetFriendship(const Spot& s, int value);
  std::string SetIv(const Spot& s, int stat, int value, const Tables& t);
  std::string SetEv(const Spot& s, int stat, int value, const Tables& t);
  std::string SetMove(const Spot& s, int index, uint16_t move, int ppUps, const Tables& t);
  std::string SetPpUps(const Spot& s, int index, int ppUps, const Tables& t);
  std::string SetAbilitySlot(const Spot& s, int slot, const Tables& t);
  std::string SetForm(const Spot& s, uint16_t row, const Tables& t);
  std::string SetPid(const Spot& s, const PidWish& wish, const Tables& t);
  std::string Heal(const Spot& s, const Tables& t);
  std::string SetPcMarks(const Spot& s, uint8_t marks);
  std::string Release(const Spot& s);
  std::string Add(uint16_t species, int level, bool intoParty, const Tables& t, Spot& where);
  std::string Swap(const Spot& a, const Spot& b);

  std::string SetBoxName(int box, const std::u16string& name);
  std::string SetTrainerName(const std::u16string& name);
  std::string SetMoney(uint32_t money);
  std::string SetCoupons(uint32_t coupons);
  std::string SetPlayTime(double seconds);

  std::string SetBagSlot(Pocket p, int index, uint16_t item, int count, const Tables& t);
  std::string RemoveBagSlot(Pocket p, int index);
  std::string AddBagItem(Pocket p, uint16_t item, int count, const Tables& t);
  std::string MoveBagSlot(Pocket p, int from, int to);

  std::string OpenHeart(const Spot& s);
  std::vector<uint16_t> MemoSpecies() const;
  std::string AddMemoSpecies(uint16_t species, const Tables& t);
  std::string RemoveMemoSpecies(uint16_t species, const Tables& t);

  void SetShadowOrders(const std::vector<std::string>& fromSetup) { shadowOrders_ = shadowOrdersOrig_ = fromSetup; }
  const std::vector<std::string>& ShadowOrders() const { return shadowOrders_; }
  bool ShadowOrdersChanged() const { return shadowOrders_ != shadowOrdersOrig_; }
  std::string MakeShadow(const Spot& s, const ShadowWish& wish, const Tables& t);
  std::string MakeNormal(const Spot& s);
  bool EditorMadeShadow(const Spot& s) const;
  bool ShadowOrderOf(const xd::save::Pokemon& p, xd::save::ShadowOrder& out, bool* savedBefore = nullptr) const;
  void ShadowRowStats(const Tables& t, int& free, int& total) const;
  void SetExtraShadows(const std::vector<uint8_t>& section);
  std::vector<uint8_t> ExtraShadowSection() const;
  bool ExtraShadowsChanged() const { return extra_ != extraOrig_; }
  const xd::save::ShadowEntry* ShadowEntryOf(const xd::save::Pokemon& p) const;

  std::vector<std::string> Problems(const Tables& t) const;
  std::vector<std::string> Notes(const Tables& t) const;

  std::vector<uint8_t> Build(std::string& error) const;

  void Seed(uint64_t seed) { rng_ = seed; }
  void SetSpeciesNames(std::function<std::string(uint16_t)> names) { speciesNames_ = std::move(names); }

  const std::u16string& OriginalTrainerName() const { return orig_.trainerName; }
  bool TrainerRenamed() const { return work_.trainerName != orig_.trainerName; }

private:
  uint8_t* Rec(const Spot& s);
  const uint8_t* Rec(const Spot& s) const;
  bool Valid(const Spot& s) const;
  void Refresh();
  void Log(const std::string& text, bool marks, const char* kind);
  std::string Name(const Spot& s, const Tables* t) const;
  std::string Refuse(const Spot& s) const;
  bool FullRecompute(const Spot& s, const Tables& t, bool keepHpShare, std::string& why);
  std::vector<std::pair<Spot, xd::save::Pokemon>> ChangedPokemon() const;
  void PackParty();
  size_t PocketOffset(Pocket p) const;

  Options options_;
  std::vector<uint8_t> file_;
  xd::save::ReadReport report_;
  xd::save::Save orig_, work_;
  std::vector<Change> log_;
  std::vector<std::string> shadowOrders_, shadowOrdersOrig_;
  std::vector<std::array<uint8_t, xd::save::kShadowEntrySize>> extra_, extraOrig_;
  std::vector<xd::save::ShadowEntry> extraEntries_;
  uint8_t* ShadowRecMut(uint32_t row);
  const uint8_t* ShadowRec(uint32_t row) const;
  bool EnsureExtra(uint32_t row);
  void ShadowWhere(const xd::save::Pokemon& p, const Spot& at);
  uint64_t rng_ = 0x9E3779B97F4A7C15ull;
  std::function<std::string(uint16_t)> speciesNames_;
  uint32_t Rand();
};

int MaxTrainerName(const Options& o);
int MaxNickname(const Options& o);
int MaxBoxName(const Options& o);
std::string CheckName(const std::u16string& name, int maxLen);

std::string PlaythroughKey(uint32_t trainerOtId, const std::u16string& name);

constexpr uint32_t kMaxMoney = 999999;
constexpr uint32_t kMaxCoupons = 99999;
constexpr double kMaxPlayTime = 999.0 * 3600 + 59 * 60 + 59;
constexpr int kMaxBagCount = 999;

}

#endif
