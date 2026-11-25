#include <xd_legality_tables.h>
#include <xd_saveedit.h>
#include <xd_saveedit_write.h>

#include <xd_save_write.h>

#include <gc_memcard.h>
#include <playthrough.h>
#include <save_backups.h>
#include <saveext.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace fs = std::filesystem;
namespace xs = xd::save;
namespace se = xd::saveedit;
using xs::ByteOrder;

namespace {

bool sOk = true;
void check(bool cond, const std::string& what) {
  std::printf("%s %s\n", cond ? "ok  " : "FAIL", what.c_str());
  sOk &= cond;
}

struct Layout {
  int id;
  uint32_t sizeAt, offsetAt, size, stride;
};
const Layout kOrder[] = {
    {22, 0x36, 0x74, 0xC800, 0xC810}, {3, 0x1C, 0x40, 0xBC50, 0xBC60}, {15, 0x26, 0x54, 0x2400, 0x2410},
    {2, 0x1A, 0x3C, 0x978, 0x990},    {10, 0x1E, 0x44, 0x4A, 0x60},     {21, 0x34, 0x70, 0x2298, 0x22B0},
    {11, 0x20, 0x48, 0xCC, 0xE0},     {12, 0x22, 0x4C, 0x1774, 0x1790}, {14, 0x24, 0x50, 0x364, 0x380},
    {23, 0x28, 0x58, 0x6B4, 0x6D0},   {16, 0x2A, 0x5C, 0xB, 0x20},      {17, 0x2C, 0x60, 0x400, 0x410},
    {18, 0x2E, 0x64, 0x800, 0x810},   {19, 0x30, 0x68, 0x1000, 0x1010}, {20, 0x32, 0x6C, 0x2000, 0x2010},
};
constexpr uint32_t kTrainerOt = 0xBEEF0000u | 12345u;

void Str(uint8_t* p, const char* s, ByteOrder o) {
  for (; *s; s++, p += 2) xs::Write16(p, static_cast<uint8_t>(*s), o);
}

void MakePokemon(uint8_t* p, ByteOrder o, uint16_t species, uint8_t level, uint32_t pid, const char* nick, uint16_t shadowId, uint32_t ot = kTrainerOt) {
  xs::Write16(p + 0x00, species, o);
  xs::Write16(p + 0x04, 30, o);
  xs::Write16(p + 0x06, 70, o);
  p[0x0E] = 10;
  p[0x11] = level;
  p[0x1D] = 0x40;
  xs::Write32(p + 0x20, static_cast<uint32_t>(level) * level * level, o);
  xs::Write32(p + 0x24, ot, o);
  xs::Write32(p + 0x28, pid, o);
  Str(p + 0x38, "WES", o);
  Str(p + 0x4E, nick, o);
  Str(p + 0x64, nick, o);
  for (int i = 0; i < 4; i++) {
    xs::Write16(p + 0x80 + i * 4, static_cast<uint16_t>(33 + i), o);
    p[0x82 + i * 4] = 25;
  }
  for (int i = 0; i < 6; i++) {
    xs::Write16(p + 0x90 + i * 2, 40, o);
    p[0xA8 + i] = 15;
  }
  xs::Write16(p + 0xBA, shadowId, o);
}

int gScenario = 0;

std::vector<uint8_t> MakeSlot(ByteOrder o, int32_t count, const char* name) {
  std::vector<uint8_t> slot(xs::kSlotSize, 0);
  slot[0] = slot[1] = slot[2] = 1;
  xs::Write32(slot.data() + 4, static_cast<uint32_t>(count), o);
  uint8_t* d = slot.data() + xs::kSlotHeaderSize;
  for (int k = 0; k < 4; k++) xs::Write16(d + k * 2, static_cast<uint16_t>(count * 7919u + k * 104729u), o);
  xs::Write16(d + 0x18, 0x88, o);
  uint8_t* g = d + xs::kSectionBase;
  xs::Write32(g + 0x10, 42, o);
  xs::Write32(g + 0x1C, 0x8A, o);
  {
    const double play = 3 * 3600 + 25 * 60 + 7;
    uint64_t bits;
    std::memcpy(&bits, &play, 8);
    if (o == ByteOrder::GameCube) {
      xs::Write32(g + 0x30, static_cast<uint32_t>(bits >> 32), o);
      xs::Write32(g + 0x34, static_cast<uint32_t>(bits), o);
    } else {
      xs::Write32(g + 0x30, static_cast<uint32_t>(bits), o);
      xs::Write32(g + 0x34, static_cast<uint32_t>(bits >> 32), o);
    }
  }
  uint32_t headSum = 0;
  for (int i = 0; i < 8; i++) headSum += slot[static_cast<size_t>(i)];
  xs::Write32(g + 0x38, headSum, o);
  uint32_t off = 0xA0;
  for (const Layout& l : kOrder) {
    xs::Write16(d + l.sizeAt, static_cast<uint16_t>(l.size), o);
    xs::Write16(d + l.offsetAt, static_cast<uint16_t>(off), o);
    xs::Write16(d + l.offsetAt + 2, static_cast<uint16_t>(off >> 16), o);
    uint8_t* s = d + xs::kSectionBase + off;
    if (l.id == 2) {
      Str(s, name, o);
      xs::Write32(s + 0x2C, kTrainerOt, o);
      MakePokemon(s + 0x30, o, 1, 5, 0x12345678, "BULBY", gScenario ? 7 : 0);
      MakePokemon(s + 0x30 + xs::kPokemonSize, o, 25, 50, 0xBEEF3039u ^ 0x00010002u, "SPARKY", 7);
      MakePokemon(s + 0x30 + 2 * xs::kPokemonSize, o, 133, 20, 0x2468ACE0, "EEVEE", gScenario ? 7 : 0);
      if (gScenario) {
        for (int i = 0; i < 3; i++) {
          s[0x30 + i * xs::kPokemonSize + 0x0F] = 3;
          s[0x30 + i * xs::kPokemonSize + 0xC0] = 0xAA;
        }
      }
      xs::Write16(s + 0x4C8, 13, o);
      xs::Write16(s + 0x4CA, 4, o);
      xs::Write16(s + 0x4CC, 14, o);
      xs::Write16(s + 0x4CE, 6, o);
      xs::Write16(s + 0x5EC, 4, o);
      xs::Write16(s + 0x5EE, 10, o);
      xs::Write32(s + 0x8E4, 98765, o);
      xs::Write32(s + 0x8E8, 321, o);
      xs::Write32(s + 0x8EC, 4321, o);
    } else if (l.id == 3) {
      Str(s + 2 * 0x170C, "BOX3", o);
      if (gScenario == 0) {
        MakePokemon(s + 2 * 0x170C + 0x14 + 4 * xs::kPokemonSize, o, 150, 70, 0xCAFEBABE, "MEW2", 0, 0x00010002);
      } else if (gScenario <= 2) {
        MakePokemon(s + 2 * 0x170C + 0x14 + 4 * xs::kPokemonSize, o, 150, 70, 0xCAFEBABE, "MEW2", 0);
        s[2 * 0x170C + 0x14 + 4 * xs::kPokemonSize + 0x0F] = 9;
      }
    } else if (l.id == 15) {
      s[7 * xs::kShadowEntrySize] = static_cast<uint8_t>((gScenario == 1 || gScenario == 3 ? 4 : 3) << 5);
      xs::Write16(s + 7 * xs::kShadowEntrySize + 0x1A, 25, o);
      xs::Write32(s + 7 * xs::kShadowEntrySize + 0x1C, 0xBEEF3039u ^ 0x00010002u, o);
      xs::Write32(s + 7 * xs::kShadowEntrySize + 0x24, 1500, o);
    }
    off += l.stride;
  }
  xs::EncryptSaveData(d, o);
  for (size_t i = xs::kSlotHeaderSize + xs::kDataSize; i < xs::kSlotSize; i++) slot[i] = static_cast<uint8_t>(i * 31);
  return slot;
}

std::vector<uint8_t> MakeFile(ByteOrder o, const char* name) {
  std::vector<uint8_t> f(xs::kFileSize, 0);
  const auto a = MakeSlot(o, 5, name), b = MakeSlot(o, 4, name);
  std::memcpy(f.data() + xs::SlotOffset(0), a.data(), xs::kSlotSize);
  std::memcpy(f.data() + xs::SlotOffset(1), b.data(), xs::kSlotSize);
  return f;
}

xd::legality::Rules MakeRules() {
  xd::legality::Rules r;
  r.species.resize(300);
  for (uint16_t id : {1, 2, 25, 26, 133, 134, 150, 251}) {
    auto& s = r.species[id];
    s.exists = true;
    s.growth = 0;
    s.abilities[0] = 1;
    s.abilities[1] = id == 26 ? 0 : 2;
    for (auto& b : s.baseStats) b = 50;
    s.levelUp = {{1, 33}, {5, 34}, {10, 35}, {15, 36}, {20, 37}};
    s.machine.fill(1);
    s.sexRatio = id == 150 ? 0xFF : 127;
  }
  r.moves.resize(120);
  for (uint16_t id = 33; id < 60; id++) {
    r.moves[id].exists = true;
    r.moves[id].basePp = 25;
  }
  r.items.resize(60);
  for (uint16_t id = 1; id < 40; id++) {
    r.items[id].exists = true;
    r.items[id].bagKind = id < 20 ? 2 : id < 30 ? 1 : 4;
  }
  r.vanillaSpecies = 300;
  r.vanillaMoves = 120;
  r.vanillaItems = 60;
  r.expCurves.resize(8);
  for (auto& c : r.expCurves) {
    for (int l = 0; l <= 100; l++) c[static_cast<size_t>(l)] = static_cast<uint32_t>(l * l * l);
  }
  r.speciesName = [](uint16_t id) { return "SPECIES" + std::to_string(id); };
  r.moveName = [](uint16_t id) { return "MOVE" + std::to_string(id); };
  r.itemName = [](uint16_t id) { return "ITEM" + std::to_string(id); };
  return r;
}

std::u16string U(const char* s) { return std::u16string(s, s + std::strlen(s)); }

se::Tables MakeTables(const xd::legality::Rules& r) {
  se::Tables t;
  t.rules = &r;
  return t;
}

std::string Dump(const xs::Save& s) {
  return xs::ToUtf8(s.trainerName) + "/" + std::to_string(s.money) + "/" + std::to_string(s.party[0].level);
}

void TestAddTemplate(ByteOrder order) {
  const std::string tag = std::string(" [") + xs::ByteOrderName(order) + "]";
  const xd::legality::Rules rules = MakeRules();
  const se::Tables t = MakeTables(rules);
  for (int sc = 1; sc <= 4; sc++) {
    gScenario = sc;
    const std::vector<uint8_t> file = MakeFile(order, "WES");
    se::Editor ed;
    std::string error;
    check(ed.Open(file, {}, error), "add template: opens scenario " + std::to_string(sc) + tag);
    ed.Seed(99);
    se::Spot added;
    const std::string why = ed.Add(133, 20, sc == 1, t, added);
    const std::string w = " (scenario " + std::to_string(sc) + ")" + tag;
    if (sc == 4) {
      check(!why.empty() && !ed.Dirty(), "only Shadow Pokemon still held: refused, nothing changed" + w);
      continue;
    }
    check(why.empty(), "Add works" + w + (why.empty() ? "" : ": " + why));
    if (!why.empty()) continue;
    const xs::Pokemon& p = *ed.At(added);
    check(p.shadowId == 0 && !ed.IsShadow(added) && p.otId == kTrainerOt && p.ribbons == 0 && p.level == 20 && p.species == 133, "the new Pokemon is a plain one of the trainer's" + w);
    if (sc == 3) {
      check(p.raw[0x0F] == 3 && p.raw[0xC0] == 0, "purified template: its layout is used, its Shadow tail is cleared" + w);
    } else {
      check(p.raw[0x0F] == 9, "the trainer's own Pokemon in the PC is the template, not a purified one in the party" + w);
    }
  }
  gScenario = 0;
}

void TestOps(ByteOrder order) {
  const std::string tag = std::string(" [") + xs::ByteOrderName(order) + "]";
  const xd::legality::Rules rules = MakeRules();
  const se::Tables t = MakeTables(rules);
  const std::vector<uint8_t> file = MakeFile(order, "WES");
  se::Editor ed;
  std::string error;
  check(ed.Open(file, {}, error), "opens a built save" + tag);
  ed.Seed(1234);
  check(!ed.Dirty() && !ed.WillMark(), "nothing changed: no changes, no mark" + tag);
  check(ed.save().slotBytes == ed.originalSave().slotBytes, "the working copy starts byte for byte as the file's slot" + tag);
  check(ed.Build(error).empty(), "nothing changed: nothing to build" + tag);

  check(ed.SetMoney(ed.save().money).empty() && ed.SetLevel({se::Area::Party, 0, 0}, 5, t).empty() && ed.SetTrainerName(U("WES")).empty() &&
            !ed.Dirty(),
        "setting a value to what it is changes nothing" + tag);

  check(!ed.SetLevel({se::Area::Party, 0, 0}, 101, t).empty() && !ed.SetLevel({se::Area::Party, 0, 5}, 10, t).empty() &&
            !ed.SetSpecies({se::Area::Party, 0, 0}, 9, t).empty() && !ed.SetMoney(1000000).empty() && !ed.SetCoupons(100000).empty() &&
            !ed.SetPlayTime(4e6).empty() && !ed.SetTrainerName(U("TOOLONGNAME")).empty() && !ed.SetNickname({se::Area::Party, 0, 0}, U("")).empty() &&
            !ed.SetIv({se::Area::Party, 0, 0}, 0, 32, t).empty() && !ed.Dirty(),
        "refusals change nothing" + tag);

  const se::Spot p1{se::Area::Party, 0, 0}, p2{se::Area::Party, 0, 1}, b3{se::Area::Box, 2, 4};
  check(ed.SetLevel(p1, 12, t).empty(), "level change" + tag);
  {
    const xs::Pokemon& p = *ed.At(p1);
    uint32_t exp = 0;
    xd::legality::ExpForLevel(rules, 0, 12, exp);
    std::array<uint16_t, 6> st{};
    xd::legality::ExpectedStats(rules, p, st);
    check(p.level == 12 && p.exp == exp && p.stats == st, "level 12: EXP from the curve, stats from the formula" + tag);
    check(p.hp <= p.stats[0] && p.hp >= 1, "HP clamped to the new maximum" + tag);
  }
  check(ed.WillMark(), "a level change marks" + tag);
  check(ed.SetIv(p1, 2, 31, t).empty() && ed.At(p1)->ivs[2] == 31, "IV" + tag);
  check(ed.SetEv(p1, 1, 100, t).empty() && ed.At(p1)->evs[1] == 100, "EV" + tag);
  check(ed.SetFriendship(p1, 255).empty() && ed.At(p1)->friendship == 255, "friendship" + tag);
  check(ed.SetHeldItem(p1, 15, t).empty() && ed.At(p1)->heldItem == 15, "held item" + tag);
  check(!ed.SetHeldItem(p1, 50, t).empty(), "an item the tables lack is refused" + tag);
  check(ed.SetMove(p1, 1, 40, 2, t).empty() && ed.At(p1)->moves[1].id == 40 && ed.At(p1)->moves[1].ppUps == 2 && ed.At(p1)->moves[1].pp == 25 * 7 / 5,
        "a move with 2 PP Ups, PP = base x 7/5" + tag);
  check(!ed.SetMove(p1, 2, 40, 0, t).empty(), "the same move twice is refused" + tag);
  check(ed.SetMove(p1, 0, 0, 0, t).empty() && ed.At(p1)->moves[0].id == 40, "an empty move closes the list up" + tag);
  check(ed.SetAbilitySlot(p1, 0, t).empty() && !ed.At(p1)->SecondAbility(), "ability place" + tag);
  check(!ed.SetSpecies(p2, 26, t).empty(), "a Shadow Pokemon keeps its species" + tag);
  check(ed.SetSpecies(p1, 2, t).empty() && ed.At(p1)->species == 2 && xs::ToUtf8(ed.At(p1)->nickname) == "BULBY", "species change keeps a custom nickname" + tag);
  check(ed.SetSpecies({se::Area::Party, 0, 2}, 134, t).empty() && ed.At({se::Area::Party, 0, 2})->species == 134, "species change on another" + tag);
  check(ed.SetMoney(999999).empty() && ed.save().money == 999999, "money" + tag);
  check(ed.SetCoupons(99999).empty() && ed.save().coupons == 99999, "coupons" + tag);
  check(ed.SetPlayTime(100 * 3600 + 30 * 60 + 9).empty() && ed.save().playTimeSeconds == 100 * 3600 + 30 * 60 + 9, "play time" + tag);
  check(ed.SetBagSlot(se::Pocket::Items, 0, 13, 50, t).empty() && ed.save().items[0].count == 50, "bag: change a count" + tag);
  check(ed.AddBagItem(se::Pocket::Items, 16, 3, t).empty() && ed.save().items[2].item == 16, "bag: add an item (first free place)" + tag);
  check(ed.AddBagItem(se::Pocket::Items, 16, 2, t).empty() && ed.save().items[2].count == 5, "bag: joins a stack" + tag);
  check(!ed.AddBagItem(se::Pocket::Items, 35, 1, t).empty(), "bag: an item of another pocket is refused" + tag);
  check(ed.RemoveBagSlot(se::Pocket::Items, 1).empty() && ed.save().items[1].item == 16, "bag: remove closes up" + tag);
  se::Spot added;
  check(ed.Add(133, 20, false, t, added).empty() && added.area == se::Area::Box && added.box == 0 && added.slot == 0, "a new Pokemon goes to the first free box place" + tag);
  {
    const xs::Pokemon& p = *ed.At(added);
    check(p.species == 133 && p.level == 20 && p.otId == kTrainerOt && xs::ToUtf8(p.otName) == "WES" && xs::ToUtf8(p.nickname) == "SPECIES133" &&
              p.shadowId == 0 && !p.IsEgg() && p.hp == p.stats[0] && p.moves[0].id != 0,
          "the new Pokemon: its OT is the trainer, nickname = species, full HP, moves" + tag);
  }
  se::PidWish w;
  w.nature = 7;
  w.female = 1;
  w.shiny = 1;
  check(ed.SetPid(added, w, t).empty(), "PID search" + tag);
  {
    const xs::Pokemon& p = *ed.At(added);
    check(p.Nature() == 7 && p.IsShiny() && ((127 > static_cast<int>(p.pid & 0xFF)) ? 1 : 0) == 1, "nature, gender and shiny as asked" + tag);
  }
  check(!ed.SetPid(p2, w, t).empty(), "PID edits are locked on an unpurified Shadow Pokemon" + tag);
  check(!ed.SetPid({se::Area::Box, 2, 4}, {-1, 1, -1}, t).empty(), "a species with no gender choice is refused" + tag);
  check(ed.Heal(p1, t).empty(), "heal" + tag);
  check(ed.Release(added).empty() && ed.At(added)->IsEmpty(), "release" + tag);
  check(!ed.Release(p2).empty(), "an unpurified Shadow Pokemon can't be released" + tag);

  check(ed.Problems(t).empty(), "no problems found for sensible changes" + tag);
  const std::vector<uint8_t> out = ed.Build(error);
  check(!out.empty(), "builds the new file: " + error + tag);
  xs::Save back;
  xs::ReadReport rep;
  check(!out.empty() && xs::ReadSave(out, back, rep, error) && back.order == order && back.slot == 1 &&
            back.saveCount == 6,
        "the file reads back, in the other slot with counter + 1" + tag);
  check(back.money == 999999 && back.coupons == 99999 && back.party[0].species == 2 && back.party[0].level == 12 && back.party[0].moves[0].id == 40 &&
            back.items[0].count == 50,
        "the changes are in the new file" + tag);
  check(std::memcmp(out.data() + xs::SlotOffset(0), file.data() + xs::SlotOffset(0), xs::kSlotSize) == 0, "the older slot (the game's fallback) is untouched" + tag);
  {
    xs::Save a = ed.originalSave();
    size_t differing = 0;
    for (size_t i = 0; i < a.slotBytes.size(); i++) differing += a.slotBytes[i] != back.slotBytes[i] ? 1 : 0;
    check(differing < 4000, "only some thousand bytes differ in the slot (the edited records, bag, checksums): " + std::to_string(differing) + tag);
    const uint8_t* sa = a.SectionData(15);
    const uint8_t* sb = back.SectionData(15);
    check(sa != nullptr && sb != nullptr && std::memcmp(sa, sb, 0x2400) == 0, "the Shadow list is untouched" + tag);
    const uint8_t* fa = a.SectionData(23);
    const uint8_t* fb = back.SectionData(23);
    check(fa != nullptr && fb != nullptr && std::memcmp(fa, fb, 0x6B4) == 0, "the story flags are untouched" + tag);
  }
}

void TestMarks() {
  const xd::legality::Rules rules = MakeRules();
  const se::Tables t = MakeTables(rules);
  const std::vector<uint8_t> file = MakeFile(ByteOrder::GameCube, "WES");
  struct Case {
    const char* what;
    bool marks;
    std::function<std::string(se::Editor&)> run;
  };
  const se::Spot a{se::Area::Party, 0, 0}, b{se::Area::Party, 0, 1}, box{se::Area::Box, 0, 3};
  const Case cases[] = {
      {"nickname", false, [&](se::Editor& e) { return e.SetNickname(a, U("ZIPPY")); }},
      {"trainer name", false, [&](se::Editor& e) { return e.SetTrainerName(U("ZED")); }},
      {"swap party and box", false, [&](se::Editor& e) { return e.Swap(a, box); }},
      {"move inside the party", false, [&](se::Editor& e) { return e.Swap(a, b); }},
      {"box name", false, [&](se::Editor& e) { return e.SetBoxName(1, U("FIRE")); }},
      {"PC marks", false, [&](se::Editor& e) { return e.SetPcMarks(a, 3); }},
      {"bag reorder", false, [&](se::Editor& e) { return e.MoveBagSlot(se::Pocket::Items, 0, 1); }},
      {"level", true, [&](se::Editor& e) { return e.SetLevel(a, 9, t); }},
      {"money", true, [&](se::Editor& e) { return e.SetMoney(5); }},
      {"play time", true, [&](se::Editor& e) { return e.SetPlayTime(5); }},
      {"heal", true, [&](se::Editor& e) { return e.SetFriendship(a, 1); }},
      {"bag count", true, [&](se::Editor& e) { return e.SetBagSlot(se::Pocket::Items, 0, 13, 9, t); }},
      {"release", true, [&](se::Editor& e) { return e.Release({se::Area::Party, 0, 2}); }},
  };
  for (const Case& c : cases) {
    se::Editor ed;
    std::string error;
    ed.Open(file, {}, error);
    const std::string why = c.run(ed);
    check(why.empty() && ed.Dirty() && ed.WillMark() == c.marks, std::string("mark: ") + c.what + (c.marks ? " marks" : " does not mark") + (why.empty() ? "" : " (" + why + ")"));
  }
  se::Editor ed;
  std::string error;
  ed.Open(file, {}, error);
  ed.SetNickname(a, U("X"));
  check(!ed.WillMark(), "only unmarked changes: no mark");
  ed.SetMoney(7);
  check(ed.WillMark() && ed.MarkReason() == "money", "one marking change in the list marks: reason '" + ed.MarkReason() + "'");
  se::Editor e2;
  e2.Open(file, {}, error);
  check(e2.Release({se::Area::Party, 0, 2}).empty(), "release the third Pokemon");
  check(e2.Swap({se::Area::Party, 0, 0}, {se::Area::Box, 0, 0}).empty(), "move the first Pokemon to a box");
  check(e2.At({se::Area::Party, 0, 0})->species == 25 && e2.At({se::Area::Party, 0, 1})->IsEmpty() && e2.At({se::Area::Box, 0, 0})->species == 1, "the party is packed after a move");
  check(!e2.Swap({se::Area::Party, 0, 0}, {se::Area::Box, 5, 5}).empty(), "the party is never left empty");
}

void TestTrainerRename() {
  const std::vector<uint8_t> file = MakeFile(ByteOrder::GameCube, "WES");
  se::Editor ed;
  std::string error;
  ed.Open(file, {}, error);
  check(ed.SetTrainerName(U("ZED")).empty(), "rename");
  check(xs::ToUtf8(ed.save().trainerName) == "ZED", "the trainer's name is the new one");
  check(xs::ToUtf8(ed.At({se::Area::Party, 0, 0})->otName) == "ZED" && xs::ToUtf8(ed.At({se::Area::Party, 0, 1})->otName) == "ZED", "the trainer's own Pokemon follow the name");
  check(xs::ToUtf8(ed.At({se::Area::Box, 2, 4})->otName) == "WES", "a Pokemon from another trainer (other ID) keeps its OT name");
  check(!ed.WillMark(), "renaming does not mark");
  check(se::PlaythroughKey(kTrainerOt, U("WES")) != se::PlaythroughKey(kTrainerOt, U("ZED")), "the playthrough key follows the name");
  check(se::PlaythroughKey(0xBEEF3039u, U("")).rfind("pt_beef3039_", 0) == 0, "key format");
}

std::vector<uint8_t> ReadAll(const fs::path& p) {
  std::ifstream in(p, std::ios::binary);
  return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void MakeGciFile(const fs::path& path, const std::vector<uint8_t>& data) {
  engine::memcard::CardFile f;
  f.entry.raw.fill(0);
  std::memcpy(f.entry.raw.data(), "GXXE", 4);
  std::memcpy(f.entry.raw.data() + 4, "01", 2);
  std::memcpy(f.entry.raw.data() + 8, "PokemonXD", 9);
  f.entry.raw[0x39] = 43;
  f.entry.raw[0x3C] = f.entry.raw[0x3D] = f.entry.raw[0x3E] = f.entry.raw[0x3F] = 0xFF;
  f.entry.raw[0x2C] = f.entry.raw[0x2D] = f.entry.raw[0x2E] = f.entry.raw[0x2F] = 0xFF;
  f.data = data;
  std::ofstream out(path, std::ios::binary);
  const auto gci = engine::memcard::MakeGci(f);
  out.write(reinterpret_cast<const char*>(gci.data()), static_cast<std::streamsize>(gci.size()));
}

void TestCommit() {
  const fs::path root = fs::temp_directory_path() / "xd_saveedit_test_card";
  std::error_code ec;
  fs::remove_all(root, ec);
  const fs::path cardFolder = root / "USA" / "Card A";
  const fs::path setupFolder = root / "playthroughs" / "Card A";
  fs::create_directories(cardFolder);
  fs::create_directories(setupFolder);
  const fs::path gci = cardFolder / "01-GXXE-PokemonXD.gci";
  const std::vector<uint8_t> file = MakeFile(ByteOrder::GameCube, "WES");
  MakeGciFile(gci, file);
  const std::string key = se::PlaythroughKey(kTrainerOt, U("WES"));
  engine::playthrough::Setup setup;
  setup.trainer = "WES";
  setup.trainerId = 12345;
  setup.secretId = 0xBEEF;
  setup.started = "2026-11-01 10:00";
  engine::playthrough::WriteFile(engine::playthrough::FileFor(setupFolder, key), setup);
  { std::ofstream(setupFolder / (key + ".png"), std::ios::binary) << "PNGDATA"; }
  {
    engine::saveext::File ext;
    ext.saveHash = engine::saveext::Hash64(file.data(), xs::kFileSize);
    ext.writer = "test";
    ext.sections.push_back({"memo", 1, true, {1, 2, 3, 4}});
    check(engine::saveext::WriteAtomic(engine::saveext::FileFor(setupFolder, key), engine::saveext::Encode(ext)), "(test setup: an extension that pairs with the save)");
  }
  const xd::legality::Rules rules = MakeRules();
  const se::Tables t = MakeTables(rules);
  se::CommitRequest req;
  req.cardRoot = root;
  req.gciFile = gci;
  bool free = true;
  req.cardIsFree = [&] { return free; };

  {
    se::Editor ed;
    std::string error;
    ed.Open(file, {}, error);
    ed.SetNickname({se::Area::Party, 0, 0}, U("ZIPPY"));
    free = false;
    se::CommitResult bad = se::Commit(ed, t, req);
    check(!bad.ok && ReadAll(gci).size() > 0, "a running game: nothing written (" + bad.message + ")");
    free = true;
    const se::CommitResult r = se::Commit(ed, t, req);
    check(r.ok, "commit: " + r.message);
    check(!r.marked, "a nickname does not mark");
    engine::playthrough::Setup s2;
    std::string err;
    check(engine::playthrough::ReadFile(engine::playthrough::FileFor(setupFolder, key), s2, err) && s2.cheatsUsed.empty(), "the setup file has no cheat mark after a nickname");
    const auto bytes = ReadAll(gci);
    engine::memcard::CardFile card;
    check(engine::memcard::ReadGci(bytes, card, err), "the .gci is a valid .gci");
    xs::Save sv;
    xs::ReadReport rep;
    const std::vector<uint8_t> data(card.data.begin(), card.data.begin() + static_cast<std::ptrdiff_t>(xs::kFileSize));
    check(xs::ReadSave(data, sv, rep, err) && xs::ToUtf8(sv.party[0].nickname) == "ZIPPY", "the card file holds the new nickname");
    check(r.extensionRepaired && engine::saveext::PairsWith(ReadAll(engine::saveext::FileFor(setupFolder, key)), data), "the save extension pairs with the NEW save (re-paired)");
    check(!fs::exists(engine::saveext::StagedFor(setupFolder, key)), "no staged extension left");
    check(!r.backupFolder.empty() && fs::exists(r.backupFolder / "01-GXXE-PokemonXD.gci") && fs::exists(r.backupFolder / (key + ".ext")), "a backup of the old save and extension was made");
    const auto oldGci = ReadAll(r.backupFolder / "01-GXXE-PokemonXD.gci");
    engine::memcard::CardFile oc;
    check(engine::memcard::ReadGci(oldGci, oc, err) && std::memcmp(oc.data.data(), file.data(), xs::kFileSize) == 0, "the backup is the save as it was");
    check(!fs::exists(gci.string() + ".edit.tmp"), "no temporary file left");
  }
  std::vector<uint8_t> now;
  {
    const auto bytes = ReadAll(gci);
    engine::memcard::CardFile card;
    std::string err;
    engine::memcard::ReadGci(bytes, card, err);
    now.assign(card.data.begin(), card.data.begin() + static_cast<std::ptrdiff_t>(xs::kFileSize));
    se::Editor ed;
    ed.Open(now, {}, err);
    ed.SetMoney(1);
    const se::CommitResult r = se::Commit(ed, t, req);
    check(r.ok && r.marked, "a money change commits and marks: " + r.message);
    engine::playthrough::Setup s2;
    check(engine::playthrough::ReadFile(engine::playthrough::FileFor(setupFolder, key), s2, err) && s2.cheatsUsed.size() == 1 && s2.cheatsUsed[0].find("save editor (money)") != std::string::npos,
          "the cheat mark with the reason is in the setup file: " + (s2.cheatsUsed.empty() ? std::string("none") : s2.cheatsUsed[0]));
    const auto backups = engine::memcard::backups::List(engine::memcard::backups::FolderFor(root));
    check(!backups.empty(), "the backups list shows the saves made before the edits");
    std::string rerr;
    const bool restored = !backups.empty() && engine::memcard::backups::Restore(backups.back(), "01-GXXE-PokemonXD.gci", cardFolder, engine::memcard::backups::FolderFor(root),
                                                                              engine::memcard::backups::kKeep, rerr, setupFolder);
    check(restored, "an old backup is restored: " + rerr);
    engine::playthrough::Setup s3;
    check(engine::playthrough::ReadFile(engine::playthrough::FileFor(setupFolder, key), s3, err) && s3.cheatsUsed.size() == 1, "after the restore the cheat mark is still there");
    const auto b2 = ReadAll(gci);
    engine::memcard::CardFile c2;
    engine::memcard::ReadGci(b2, c2, err);
    check(std::memcmp(c2.data.data(), file.data(), xs::kFileSize) == 0, "the restored card file is the save as it was before the first edit");
    MakeGciFile(gci, now);
  }
  {
    std::string err;
    se::Editor ed;
    ed.Open(now, {}, err);
    engine::saveext::File ext;
    ext.saveHash = engine::saveext::Hash64(now.data(), xs::kFileSize);
    ext.sections.push_back({"memo", 1, true, {9}});
    engine::saveext::WriteAtomic(engine::saveext::FileFor(setupFolder, key), engine::saveext::Encode(ext));
    check(ed.SetTrainerName(U("ZED")).empty(), "rename for the write test");
    const se::CommitResult r = se::Commit(ed, t, req);
    check(r.ok && !r.marked, "a rename commits and does not mark: " + r.message);
    const std::string newKey = se::PlaythroughKey(kTrainerOt, U("ZED"));
    check(r.newKey == newKey && r.oldKey == key, "the result names both keys");
    engine::playthrough::Setup s2;
    check(engine::playthrough::ReadFile(engine::playthrough::FileFor(setupFolder, newKey), s2, err) && s2.trainer == "ZED" && s2.cheatsUsed.size() == 1,
          "the setup file moved to the new key, with the mark and the new name");
    check(fs::exists(setupFolder / (newKey + ".png")) && !fs::exists(setupFolder / (key + ".png")), "the picture follows");
    const auto data = [&] {
      engine::memcard::CardFile c;
      std::string e;
      engine::memcard::ReadGci(ReadAll(gci), c, e);
      return std::vector<uint8_t>(c.data.begin(), c.data.begin() + static_cast<std::ptrdiff_t>(xs::kFileSize));
    }();
    check(engine::saveext::PairsWith(ReadAll(engine::saveext::FileFor(setupFolder, newKey)), data) && !fs::exists(engine::saveext::FileFor(setupFolder, key)),
          "the extension follows and pairs with the new save");
    engine::playthrough::Setup oldS;
    check(engine::playthrough::ReadFile(engine::playthrough::FileFor(setupFolder, key), oldS, err) && !oldS.cheatsUsed.empty(), "the old key's setup stays (for old backups), with the mark");
  }
  {
    std::string err;
    se::Editor ed;
    ed.Open(now, {}, err);
    ed.SetMoney(3);
    const se::CommitResult r = se::Commit(ed, t, req);
    check(!r.ok && r.message.find("changed since") != std::string::npos, "an editor opened on an older state refuses to write: " + r.message);
  }
  fs::remove_all(root, ec);
}

void TestTablesFile() {
  xd::legality::TablesFile t;
  t.rules = MakeRules();
  t.stamp = "0123456789abcdef";
  t.names = std::make_shared<xd::legality::TableNames>();
  t.names->species = {"", "BULBA", "IVY"};
  t.names->moves = {"", "POUND"};
  t.names->items = {"", "POTION"};
  t.names->abilities = {"", "STENCH"};
  for (int i = 0; i < 25; i++) t.names->natures.push_back("NATURE" + std::to_string(i));
  t.rules.species[25].sexRatio = 31;
  t.rules.species[25].initFriend = 90;
  t.rules.items[3].bagKind = 5;
  t.rules.giftMoves.push_back({133, 44});
  const std::vector<uint8_t> bytes = xd::legality::Serialize(t);
  xd::legality::TablesFile back;
  std::string error;
  check(xd::legality::Parse(bytes, back, error), "a tables file reads back: " + error);
  check(back.stamp == t.stamp && back.rules.species.size() == t.rules.species.size() && back.rules.moves.size() == t.rules.moves.size() &&
            back.rules.items.size() == t.rules.items.size() && back.rules.expCurves == t.rules.expCurves && back.rules.machineMove == t.rules.machineMove,
        "stamp, table sizes and curves survive");
  check(back.rules.species[25].sexRatio == 31 && back.rules.species[25].initFriend == 90 && back.rules.species[25].levelUp.size() == 5 &&
            back.rules.species[25].levelUp[2].level == 10 && back.rules.species[25].machine == t.rules.species[25].machine && back.rules.items[3].bagKind == 5 &&
            back.rules.giftMoves.size() == 1 && back.rules.giftMoves[0].second == 44 && back.rules.moves[34].basePp == 25,
        "species, learnsets, items and gift moves survive");
  check(back.Species(1) == "BULBA" && back.Nature(3) == "NATURE3" && back.rules.speciesName(2) == "IVY" && back.Species(99).empty(), "names survive and are bound to the rules");
  std::vector<uint8_t> bad = bytes;
  bad[bad.size() / 2] ^= 0x55;
  check(!xd::legality::Parse(bad, back, error), "a damaged file is refused: " + error);
  bad = bytes;
  bad.resize(bad.size() - 9);
  check(!xd::legality::Parse(bad, back, error), "a cut file is refused");
  check(!xd::legality::Parse({}, back, error) && !xd::legality::Parse({'X', 'D', 'T', 'B', 1, 0, 0, 0, 0, 0, 0, 0}, back, error), "nonsense is refused");
}

void TestCareful() {
  xd::legality::Rules rules = MakeRules();
  rules.items[25].bagKind = 5;
  const se::Tables t = MakeTables(rules);
  const std::vector<uint8_t> file = MakeFile(ByteOrder::GameCube, "WES");
  se::Editor ed;
  std::string error;
  ed.Open(file, {}, error);
  ed.Seed(5);
  const se::Spot shadow{se::Area::Party, 0, 1}, plain{se::Area::Party, 0, 0};
  check(ed.save().shadows[7].heartGauge == 1500 && ed.IsShadow(shadow), "the test save has a caught Shadow Pokemon with a closed heart");
  check(!ed.OpenHeart(plain).empty(), "a Pokemon that is not a Shadow Pokemon has no heart to open");
  check(ed.OpenHeart(shadow).empty() && ed.save().shadows[7].heartGauge == 0, "the heart opens (gauge -> 0)");
  check(ed.save().shadows[7].Snagged() && !ed.save().shadows[7].Purified() && ed.IsShadow(shadow), "it stays caught and not purified: never the snag / purify state");
  check(ed.WillMark() && ed.OpenHeart(shadow).empty() && ed.changes().size() == 1, "opening a heart marks; again = nothing more");
  check(!ed.SetPid(shadow, {3, -1, -1}, t).empty(), "its personality value stays locked");
  check(ed.MemoSpecies().empty() && ed.AddMemoSpecies(133, t).empty() && ed.MemoSpecies().size() == 1 && ed.MemoSpecies()[0] == 133, "a species is added to the memo");
  check(ed.AddMemoSpecies(133, t).empty() && ed.MemoSpecies().size() == 1, "twice = once");
  check(!ed.AddMemoSpecies(9, t).empty(), "a species the tables lack is refused");
  check(ed.RemoveMemoSpecies(133, t).empty() && ed.MemoSpecies().empty(), "and removed again");
  check(ed.AddBagItem(se::Pocket::KeyItems, 25, 1, t).empty() && ed.save().keyItems[0].item == 25, "a key item is added");
  check(ed.RemoveBagSlot(se::Pocket::KeyItems, 0).empty() && ed.save().keyItems[0].item == 0, "and removed");
  std::string e2;
  const std::vector<uint8_t> out = ed.Build(e2);
  xs::Save back;
  xs::ReadReport rep;
  check(!out.empty() && xs::ReadSave(out, back, rep, e2) && back.shadows[7].heartGauge == 0, "the opened heart is in the written file: " + e2);
  se::Editor e3;
  e3.Open(file, {}, error);
  check(e3.UnpurifiedShadow(*e3.At(shadow)), "an unpurified Shadow Pokemon is recognised");
}

void TestRealSave() {
  const char* path = std::getenv("XD_SAVEEDIT_TEST_SAVE");
  if (path == nullptr) {
    std::printf("skip real-save test (XD_SAVEEDIT_TEST_SAVE not set)\n");
    return;
  }
  std::vector<uint8_t> bytes = ReadAll(path);
  engine::memcard::CardFile card;
  std::string err;
  if (!engine::memcard::ReadGci(bytes, card, err)) {
    check(false, "the real save reads as a .gci: " + err);
    return;
  }
  const std::vector<uint8_t> data(card.data.begin(), card.data.begin() + static_cast<std::ptrdiff_t>(xs::kFileSize));
  se::Editor ed;
  check(ed.Open(data, {}, err), "the real save opens: " + err);
  check(ed.SetMoney(ed.save().money == 12345 ? 12346 : 12345).empty(), "a real save: money changed");
  const std::vector<uint8_t> out = ed.Build(err);
  xs::Save back;
  xs::ReadReport rep;
  check(!out.empty() && xs::ReadSave(out, back, rep, err) && back.money == ed.save().money, "the real save: written file reads back (" + err + ")");
  std::printf("real save: trainer %s, party %s\n", xs::ToUtf8(ed.save().trainerName).c_str(), Dump(ed.save()).c_str());
}

}

int main() {
  TestOps(ByteOrder::GameCube);
  TestOps(ByteOrder::PC);
  TestAddTemplate(ByteOrder::GameCube);
  TestAddTemplate(ByteOrder::PC);
  TestMarks();
  TestTrainerRename();
  TestCommit();
  TestTablesFile();
  TestCareful();
  TestRealSave();
  std::printf(sOk ? "ALL OK\n" : "FAILED\n");
  return sOk ? 0 : 1;
}
