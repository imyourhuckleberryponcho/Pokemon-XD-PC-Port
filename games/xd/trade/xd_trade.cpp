#include <xd_trade.h>

#include <xd_save_convert.h>
#include <xd_save_write.h>

#include <algorithm>
#include <cstring>

namespace xd::trade {

using xd::save::kPokemonSize;
using xd::save::Read16;
using xd::save::Read32;
using xd::save::Write16;
using xd::save::Write32;

namespace {

constexpr size_t kHeroParty = 0x30;
constexpr size_t kBoxStride = 0x14 + xd::save::kBoxSlots * kPokemonSize;
constexpr size_t kBoxPokemon = 0x14;
constexpr uint16_t kEverstoneEffect = 0x26;
constexpr uint16_t kTradeFriendship = 0x46;
constexpr uint8_t kEvoTrade = 5, kEvoTradeItem = 6;

const xd::save::ByteOrder kOrder = xd::save::ByteOrder::GameCube;

std::u16string Widen(const std::string& s) {
  std::u16string r;
  for (unsigned char c : s) r.push_back(c);
  return r;
}

}

std::string Trader::Name(uint16_t species) const {
  std::string n = cfg_.names ? cfg_.names->Species(species) : std::string();
  return n.empty() ? "Pokemon #" + std::to_string(species) : n;
}

bool Trader::Load(const Bytes& file, std::string& error) {
  loaded_ = false;
  prepared_.clear();
  Bytes f = file;
  xd::save::Save s;
  xd::save::ReadReport rep;
  if (!xd::save::ReadSave(f, s, rep, error)) return false;
  if (s.order != kOrder) {
    xd::save::ConvertReport cr;
    if (!xd::save::ConvertFile(f, kOrder, cr, error)) return false;
    rep = xd::save::ReadReport{};
    s = xd::save::Save{};
    if (!xd::save::ReadSave(f, s, rep, error)) return false;
    if (s.order != kOrder) { error = "The save could not be read in the GameCube order."; return false; }
  }
  file_ = std::move(f);
  save_ = std::move(s);
  report_ = rep;
  trainer_ = xd::save::ToUtf8(save_.trainerName);
  loaded_ = true;
  return true;
}

Card Trader::Describe(const xd::save::Pokemon& p) const {
  Card c;
  c.speciesNumber = p.species;
  c.species = Name(p.species);
  c.name = p.nickname.empty() ? c.species : xd::save::ToUtf8(p.nickname);
  c.level = p.level;
  c.nature = cfg_.names ? cfg_.names->Nature(p.Nature()) : std::string();
  if (p.heldItem != 0) {
    c.item = cfg_.names ? cfg_.names->Item(p.heldItem) : std::string();
    if (c.item.empty()) c.item = "item #" + std::to_string(p.heldItem);
  }
  if (cfg_.rules && p.species < cfg_.rules->species.size()) {
    const uint8_t a = cfg_.rules->species[p.species].abilities[p.SecondAbility() ? 1 : 0];
    if (a != 0) {
      c.ability = cfg_.names ? cfg_.names->Ability(a) : std::string();
      if (c.ability.empty()) c.ability = "ability #" + std::to_string(a);
    }
  }
  for (int i = 0; i < 4; i++) {
    const uint16_t m = p.moves[static_cast<size_t>(i)].id;
    if (m != 0) {
      c.moves[i] = cfg_.names ? cfg_.names->Move(m) : std::string();
      if (c.moves[i].empty()) c.moves[i] = "move #" + std::to_string(m);
    }
  }
  c.shiny = p.IsShiny();
  c.egg = p.IsEgg();
  c.ot = xd::save::ToUtf8(p.otName);
  c.otId = p.otId;
  c.hp = p.hp;
  c.maxHp = p.stats[0];
  return c;
}

Bytes Trader::Wire(const xd::save::Pokemon& p) const {
  Bytes w(p.raw.begin(), p.raw.end());
  ShadowState st = ShadowState::Never;
  if (const xd::save::ShadowEntry* e = save_.ShadowOf(p)) st = e->Purified() ? ShadowState::Purified : ShadowState::Unpurified;
  w.push_back(static_cast<uint8_t>(st));
  w.push_back(cfg_.fromCheats ? 1 : 0);
  return w;
}

bool Trader::Parse(const Bytes& wire, xd::save::Pokemon& out, ShadowState& shadow) {
  if (wire.size() != kWireSize || wire[kPokemonSize] > 2 || wire[kPokemonSize + 1] > 1) return false;
  out = xd::save::ParsePokemon(wire.data(), kOrder);
  shadow = static_cast<ShadowState>(wire[kPokemonSize]);
  return true;
}

Card Trader::DescribeWire(const Bytes& wire) const {
  xd::save::Pokemon p;
  ShadowState sh;
  if (!Parse(wire, p, sh)) return Card{};
  Card c = Describe(p);
  c.fromCheats = wire[kPokemonSize + 1] != 0;
  return c;
}

Verdict Trader::Judge(const xd::legality::Rules& rules, const xd::save::Pokemon& p, ShadowState sh) {
  if (p.IsEmpty()) return {false, "There is no Pokemon there."};
  if (sh == ShadowState::Unpurified) return {false, "A Shadow Pokemon can't be traded until it is purified."};
  if (p.shadowId >= xd::save::kShadowEntries) return {false, "An extra Shadow Pokemon of More Shadow Pokemon (a Shadow row past the disc's 127, kept only in its own save's extension) can't be traded yet."};
  if (p.IsEgg()) return {false, "Eggs can't be traded."};
  const xd::legality::Result r = xd::legality::Check(p, rules, xd::legality::Level::Legal);
  if (!r.Ok()) return {false, r.Text()};
  return {true, ""};
}

Verdict Trader::JudgeFor(const xd::save::Pokemon& p, ShadowState sh) const {
  const bool strict = cfg_.original && cfg_.originalRules != nullptr;
  Verdict v = Judge(strict ? *cfg_.originalRules : *cfg_.rules, p, sh);
  if (!v.ok && cfg_.original) {
    const bool gen4 = v.text.find("Gen 4") != std::string::npos;
    const bool other = v.text.find("Shadow") != std::string::npos || v.text.find("Eggs") != std::string::npos || v.text.find("no Pokemon") != std::string::npos;
    if (!other) {
      v.text = std::string("This save plays the original game: it can't receive a ") + (gen4 ? "Generation 4" : "randomized / modified") +
               " Pokemon (" + v.text + ")";
    }
  }
  return v;
}

std::vector<Entry> Trader::List() const {
  std::vector<Entry> out;
  if (!loaded_ || !cfg_.rules) return out;
  auto add = [&](int box, int slot, const xd::save::Pokemon& p) {
    if (p.IsEmpty()) return;
    Entry e;
    e.box = box;
    e.slot = slot;
    e.pokemon = p;
    e.wire = Wire(p);
    e.card = Describe(p);
    const Verdict v = JudgeFor(p, static_cast<ShadowState>(e.wire[kPokemonSize]));
    e.ok = v.ok;
    e.reason = v.text;
    out.push_back(std::move(e));
  };
  for (int i = 0; i < xd::save::kPartySize; i++) add(-1, i, save_.party[static_cast<size_t>(i)]);
  for (int b = 0; b < xd::save::kBoxCount; b++)
    for (int j = 0; j < xd::save::kBoxSlots; j++) add(b, j, save_.boxes[static_cast<size_t>(b)].slots[static_cast<size_t>(j)]);
  return out;
}

Verdict Trader::CheckIncoming(const Bytes& wire) const {
  xd::save::Pokemon p;
  ShadowState sh;
  if (!cfg_.rules || !Parse(wire, p, sh)) return {false, "Your friend's Pokemon could not be read."};
  return JudgeFor(p, sh);
}

uint16_t Trader::TradeEvolution(const xd::legality::Rules& rules, const xd::save::Pokemon& p, uint8_t& kind) {
  kind = 0;
  if (p.species >= rules.species.size()) return 0;
  if (p.heldItem != 0 && p.heldItem < rules.items.size() && rules.items[p.heldItem].soubi == kEverstoneEffect) return 0;
  uint16_t result = 0;
  for (const xd::legality::Evolution& e : rules.species[p.species].evolutions) {
    if (e.kind == kEvoTrade) { result = e.to; kind = e.kind; }
    else if (e.kind == kEvoTradeItem && e.param == p.heldItem) { result = e.to; kind = e.kind; }
  }
  return result;
}

Verdict Trader::Prepare(const Bytes& give, const Bytes& get) {
  prepared_.clear();
  result_ = Result{};
  if (!loaded_ || !cfg_.rules) return {false, "No save is loaded."};
  if (const Verdict v = CheckIncoming(get); !v.ok) return v;
  xd::save::Pokemon incoming;
  ShadowState inShadow;
  Parse(get, incoming, inShadow);
  result_.receivedFromCheats = get[kPokemonSize + 1] != 0;

  xd::save::Save work = save_;
  int box = -2, slot = -1;
  for (int i = 0; i < xd::save::kPartySize && slot < 0; i++) {
    const auto& p = work.party[static_cast<size_t>(i)];
    if (!p.IsEmpty() && Wire(p) == give) { box = -1; slot = i; }
  }
  for (int b = 0; b < xd::save::kBoxCount && slot < 0; b++)
    for (int j = 0; j < xd::save::kBoxSlots && slot < 0; j++) {
      const auto& p = work.boxes[static_cast<size_t>(b)].slots[static_cast<size_t>(j)];
      if (!p.IsEmpty() && Wire(p) == give) { box = b; slot = j; }
    }
  if (slot < 0) return {false, "The Pokemon you offered is no longer in your save."};
  const xd::save::Pokemon outgoing = box == -1 ? work.party[static_cast<size_t>(slot)]
                                               : work.boxes[static_cast<size_t>(box)].slots[static_cast<size_t>(slot)];

  uint8_t* data = work.slotBytes.data();
  const size_t hero = xd::save::SectionOffsetInSlot(work, 2);
  const size_t boxes = xd::save::SectionOffsetInSlot(work, 3);
  const size_t shadowList = xd::save::SectionOffsetInSlot(work, 15);
  const size_t memo = xd::save::SectionOffsetInSlot(work, 12);
  if (hero == 0 || boxes == 0 || shadowList == 0 || memo == 0 || work.slotBytes.size() != xd::save::kSlotSize)
    return {false, "The save is missing a section the trade needs."};

  {
    const uint16_t sid = outgoing.shadowId;
    if (sid != 0 && sid < xd::save::kShadowEntries) {
      uint8_t* e = data + shadowList + sid * xd::save::kShadowEntrySize;
      Write32(e, (Read32(e, kOrder) & ~0x38000u) | ((5u << 15) & 0x38000u), kOrder);
      e[0x11] = 0;
    }
  }

  uint8_t rec[kPokemonSize];
  std::memcpy(rec, get.data(), kPokemonSize);
  Write16(rec + 0xBA, 0, kOrder);
  Write16(rec + 0x06, kTradeFriendship, kOrder);
  xd::save::Pokemon received = xd::save::ParsePokemon(rec, kOrder);
  result_.received = Name(received.species);
  result_.where = box == -1 ? "party slot " + std::to_string(slot + 1)
                            : "PC box " + std::to_string(box + 1) + ", place " + std::to_string(slot + 1);

  auto memoSet = [&](uint16_t species, uint32_t otId, uint32_t pid) {
    uint8_t* mm = data + memo;
    const uint16_t count = Read16(mm, kOrder);
    for (uint16_t i = 0; i < count && i < 500; i++)
      if ((Read16(mm + 4 + i * 12, kOrder) & 0x3FFF) == species) return;
    if (count >= 500) return;
    uint8_t* e = mm + 4 + count * 12;
    Write16(e, static_cast<uint16_t>(species | 0x8000), kOrder);
    Write32(e + 4, otId, kOrder);
    Write32(e + 8, pid, kOrder);
    Write16(mm, static_cast<uint16_t>(count + 1), kOrder);
    result_.newEntry = true;
  };
  memoSet(received.species, received.otId, received.pid);

  uint8_t kind = 0;
  const uint16_t to = TradeEvolution(*cfg_.rules, received, kind);
  if (to != 0 && to < cfg_.rules->species.size() && cfg_.rules->species[to].exists) {
    const std::string oldName = Name(received.species), newName = Name(to);
    const bool hadDefaultName = xd::save::ToUtf8(received.nickname) == oldName;
    const int oldMax = received.stats[0], oldHp = received.hp;
    Write16(rec + 0x00, to, kOrder);
    if (kind == kEvoTradeItem) Write16(rec + 0x02, 0, kOrder);
    if (hadDefaultName) {
      std::u16string n = Widen(newName);
      n.resize(10);
      for (size_t i = 0; i < 11; i++) Write16(rec + 0x4E + i * 2, i < n.size() ? static_cast<uint16_t>(n[i]) : 0, kOrder);
    }
    xd::save::Pokemon evolved = xd::save::ParsePokemon(rec, kOrder);
    std::array<uint16_t, 6> stats{};
    if (xd::legality::ExpectedStats(*cfg_.rules, evolved, stats)) {
      for (size_t i = 0; i < 6; i++) Write16(rec + 0x90 + i * 2, stats[i], kOrder);
      int hp = oldHp == 0 ? 0 : static_cast<int>(stats[0]) - (oldMax - oldHp);
      if (oldHp != 0 && hp < 1) hp = 1;
      Write16(rec + 0x04, static_cast<uint16_t>(hp), kOrder);
    }
    for (const xd::legality::LevelUpMove& m : cfg_.rules->species[to].levelUp) {
      if (m.move == 0 || m.level != evolved.level) continue;
      int free = -1;
      bool known = false;
      for (int i = 0; i < 4; i++) {
        const uint16_t id = Read16(rec + 0x80 + i * 4, kOrder);
        if (id == m.move) known = true;
        if (id == 0 && free < 0) free = i;
      }
      if (known || free < 0) continue;
      Write16(rec + 0x80 + free * 4, m.move, kOrder);
      rec[0x80 + free * 4 + 2] = m.move < cfg_.rules->moves.size() ? cfg_.rules->moves[m.move].basePp : 0;
      rec[0x80 + free * 4 + 3] = 0;
    }
    received = xd::save::ParsePokemon(rec, kOrder);
    memoSet(received.species, received.otId, received.pid);
    result_.evolved = true;
    result_.evolvedFrom = oldName;
    result_.evolvedInto = newName;
  }

  uint8_t* at = box == -1 ? data + hero + kHeroParty + static_cast<size_t>(slot) * kPokemonSize
                          : data + boxes + static_cast<size_t>(box) * kBoxStride + kBoxPokemon + static_cast<size_t>(slot) * kPokemonSize;
  std::memcpy(at, rec, kPokemonSize);
  if (box == -1) work.party[static_cast<size_t>(slot)] = received;
  else work.boxes[static_cast<size_t>(box)].slots[static_cast<size_t>(slot)] = received;

  bool able = false;
  for (const auto& p : work.party) if (!p.IsEmpty() && p.hp != 0) able = true;
  if (!able) return {false, "After this trade you would have no Pokemon in your party that can battle."};

  std::string err;
  prepared_ = xd::save::WriteSaveFile(file_, work, report_, true, err);
  if (prepared_.empty()) return {false, err.empty() ? "The new save could not be built." : err};
  xd::save::Save check;
  xd::save::ReadReport rr;
  if (!xd::save::ReadSave(prepared_, check, rr, err)) { prepared_.clear(); return {false, "The new save did not pass its checks: " + err}; }
  const xd::save::Pokemon& back = box == -1 ? check.party[static_cast<size_t>(slot)]
                                            : check.boxes[static_cast<size_t>(box)].slots[static_cast<size_t>(slot)];
  if (back.raw != received.raw) { prepared_.clear(); return {false, "The new save did not hold the Pokemon it should."}; }
  return {true, ""};
}

Verdict Trader::Commit(const std::function<bool(const Bytes&, std::string&)>& writer) {
  if (prepared_.empty()) return {false, "Nothing is prepared."};
  std::string err;
  if (!writer(prepared_, err)) return {false, err.empty() ? "The save could not be written." : err};
  return {true, ""};
}

}
