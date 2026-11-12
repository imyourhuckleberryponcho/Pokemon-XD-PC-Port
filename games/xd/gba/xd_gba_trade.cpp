#include <xd_gba.h>
#include <xd_save_write.h>

#include <cstdio>
#include <cstring>

namespace xd::gba {

namespace {

using engine::gen3::PlainMon;
using xd::save::Read16;
using xd::save::Read32;
using xd::save::Write16;
using xd::save::Write32;

constexpr size_t kHeroParty = 0x30;
constexpr uint16_t kAbilityQuirkSpecies = 0x181;
constexpr uint16_t kEverstoneEffect = 0x26;

bool BattleAble(const PartyView& p, int i) {
  switch (p.states[i]) {
  case State::Empty:
  case State::BadEgg:
  case State::Egg:
    return false;
  default:
    return p.mons[i].Hp() != 0;
  }
}

int CountPartyQuantity(const PartyView& p) {
  int i = 0;
  while (i < 6 && p.states[i] != State::Empty) {
    i++;
  }
  return i;
}

uint16_t TradeEvolves(const XdMon& m, const GameRules& rules) {
  if (m.Species() == 0 || !rules.SpeciesValid(m.Species())) {
    return 0;
  }
  if (m.Item() != 0 && rules.ItemKnown(m.Item()) && rules.ItemHoldEffect(m.Item()) == kEverstoneEffect) {
    return 0;
  }
  return rules.TradeEvolution(m.Species(), m.Item());
}

std::string Num(int v) { return std::to_string(v); }

}

PartyView XdParty(const xd::save::Save& save, const GameRules& rules) {
  PartyView v;
  const size_t hero = xd::save::SectionOffsetInSlot(save, 2);
  for (int i = 0; i < 6; i++) {
    XdMon& m = v.mons[i];
    m.order = save.order;
    if (hero != 0) {
      std::memcpy(m.raw.data(), save.slotBytes.data() + hero + kHeroParty + i * xd::save::kPokemonSize,
                  xd::save::kPokemonSize);
    }
    const uint16_t sid = m.ShadowId();
    const int status = sid < xd::save::kShadowEntries ? save.shadows[sid].status : -1;
    v.states[i] = TradeState(m, Side::Xd, status, rules);
  }
  v.count = CountPartyQuantity(v);
  return v;
}

PartyView GbaParty(const engine::gen3::Save& save, const GameRules& rules, ByteOrder order) {
  PartyView v;
  const int count = save.PartyCount();
  for (int i = 0; i < 6; i++) {
    PlainMon g;
    if (i < count) {
      g = save.PartyMon(i);
      if (!g.checksumOk) {
        g.b[0x13] |= 1;
      }
    }
    v.mons[i] = FromGba(g, rules, order);
    v.mons[i].SetU16(0xB8, static_cast<uint16_t>(i));
    v.states[i] = TradeState(v.mons[i], Side::Gba, -1, rules);
  }
  v.count = CountPartyQuantity(v);
  return v;
}

TradeResult Trade(const std::vector<uint8_t>& xdFile, const xd::save::Save& xdIn, const xd::save::ReadReport& report,
                  const engine::gen3::Save& gbaIn, const TradeRequest& req, const GameRules& rules) {
  TradeResult res;
  const PartyView xp = XdParty(xdIn, rules);
  const PartyView gp = GbaParty(gbaIn, rules, xdIn.order);

  if (xp.count <= 1) {
    res.error = "the XD party needs at least 2 Pokemon to trade (the game says the same)";
    return res;
  }
  if (req.xdSlot < 0 || req.xdSlot >= xp.count) {
    res.error = "XD party slot " + Num(req.xdSlot + 1) + " can't be chosen (the XD party has " + Num(xp.count) +
                " Pokemon)";
    return res;
  }
  if (req.gbaSlot < 0 || req.gbaSlot >= gp.count) {
    res.error = "GBA party slot " + Num(req.gbaSlot + 1) + " can't be chosen (the GBA party has " + Num(gp.count) +
                " usable Pokemon)";
    return res;
  }
  for (int side = 0; side < 2; side++) {
    const PartyView& p = side == 0 ? xp : gp;
    const int slot = side == 0 ? req.xdSlot : req.gbaSlot;
    const char* who = side == 0 ? "the XD Pokemon" : "the GBA Pokemon";
    if (p.states[slot] != State::Tradeable) {
      res.error = std::string(who) + " in slot " + Num(slot + 1) + " can't be traded: " + StateText(p.states[slot]);
      return res;
    }
    int others = 0;
    for (int i = 0; i < 6; i++) {
      if (i != slot && BattleAble(p, i)) {
        others++;
      }
    }
    if (others == 0) {
      res.error = std::string("after giving away ") + who + ", its party would have no Pokemon left that can "
                  "battle (the game refuses this)";
      return res;
    }
  }

  XdMon outgoing = xp.mons[req.xdSlot];
  XdMon incoming = gp.mons[req.gbaSlot];
  outgoing.SetU16(0x06, 0x46);
  incoming.SetU16(0x06, 0x46);

  for (const XdMon* m : {&incoming, &outgoing}) {
    const uint16_t to = TradeEvolves(*m, rules);
    if (to != 0 && to != 0xFFFF) {
      char buf[200];
      std::snprintf(buf, sizeof(buf),
                    "this trade would make a Pokemon evolve (species %u into %u). Trade evolution isn't supported "
                    "by this tool yet, so the trade was not done (nothing was changed)",
                    m->Species(), to);
      res.error = buf;
      return res;
    }
  }

  xd::save::Save xd = xdIn;
  const ByteOrder order = xd.order;
  uint8_t* slot = xd.slotBytes.data();
  const size_t hero = xd::save::SectionOffsetInSlot(xd, 2);
  const size_t shadowList = xd::save::SectionOffsetInSlot(xd, 15);
  const size_t memo = xd::save::SectionOffsetInSlot(xd, 12);
  const size_t ribbons = xd::save::SectionOffsetInSlot(xd, 16);
  if (hero == 0 || shadowList == 0 || memo == 0 || ribbons == 0) {
    res.error = "the XD save is missing a section the trade needs";
    return res;
  }
  const uint8_t* gift = gbaIn.GiftRibbons();
  for (int i = 0; i < engine::gen3::kGiftRibbons; i++) {
    if (slot[ribbons + i] == 0 && gift[i] != 0) {
      slot[ribbons + i] = gift[i];
      res.notes.push_back("XD gift ribbon " + Num(i) + " taken from the GBA save (" + Num(gift[i]) + ")");
    }
  }
  {
    uint8_t* e0 = slot + shadowList;
    Write32(e0 + 0x24, 0xFFFFFFFFu, order);
    Write32(e0 + 0x04, Read32(e0 + 0x04, order) & 0x00000FFFu, order);
    Write16(e0 + 0x18, 0, order);
  }
  {
    const uint16_t sid = xp.mons[req.xdSlot].ShadowId();
    if (sid < xd::save::kShadowEntries) {
      uint8_t* e = slot + shadowList + sid * xd::save::kShadowEntrySize;
      Write32(e, (Read32(e, order) & ~0x38000u) | (5u << 15 & 0x38000u), order);
      e[0x11] = 0;
      if (sid != 0) {
        res.notes.push_back("the Shadow list marks the purified Pokemon (entry " + Num(sid) + ") as traded away");
      }
    }
  }
  std::memcpy(slot + hero + kHeroParty + req.xdSlot * xd::save::kPokemonSize, incoming.raw.data(),
              xd::save::kPokemonSize);
  {
    uint8_t* mm = slot + memo;
    const uint16_t count = Read16(mm, order);
    bool found = false;
    for (uint16_t i = 0; i < count && i < 500; i++) {
      if ((Read16(mm + 4 + i * 12, order) & 0x3FFF) == incoming.Species()) {
        found = true;
      }
    }
    if (!found && count < 500) {
      uint8_t* e = mm + 4 + count * 12;
      Write16(e, static_cast<uint16_t>(incoming.Species() | 0x8000), order);
      Write32(e + 4, incoming.OtId(), order);
      Write32(e + 8, incoming.Pid(), order);
      Write16(mm, static_cast<uint16_t>(count + 1), order);
      res.notes.push_back("new Strategy Memo entry for species " + Num(incoming.Species()));
    }
  }
  res.xdFile = xd::save::WriteSaveFile(xdFile, xd, report, true, res.error);
  if (res.xdFile.empty()) {
    return res;
  }

  engine::gen3::Save gba = gbaIn;
  XdMon sent = outgoing;
  if (sent.Species() == kAbilityQuirkSpecies) {
    const bool on = rules.SecondAbility(sent.Species()) != 0 && sent.AbilityBit();
    const bool flip = !on && rules.SecondAbility(sent.Species()) != 0;
    sent.SetU8(0x1D, static_cast<uint8_t>(flip ? (sent.U8(0x1D) | 0x40) : (sent.U8(0x1D) & ~0x40)));
    res.notes.push_back("species 0x181: ability bit flipped before sending, as the game does");
  }
  const PlainMon rec = ToGba(sent, rules);
  gba.SetPartyMon(req.gbaSlot, rec);
  if (!rec.IsEgg()) {
    const uint16_t nat = rules.NationalDex(rec.Species());
    if (nat != 0) {
      gba.RegisterInDex(nat, rec.Pid());
    }
  }
  uint8_t* giftOut = gba.GiftRibbons();
  for (int i = 0; i < engine::gen3::kGiftRibbons; i++) {
    const uint8_t x = slot[ribbons + i];
    if (giftOut[i] == 0 && x != 0) {
      giftOut[i] = x;
      res.notes.push_back("GBA gift ribbon " + Num(i) + " taken from XD (" + Num(x) + ")");
    }
  }
  std::string err;
  res.gbaFile = engine::gen3::WriteSave(gba, err);
  if (res.gbaFile.empty()) {
    res.error = err;
    res.xdFile.clear();
    return res;
  }
  res.ok = true;
  return res;
}

}
