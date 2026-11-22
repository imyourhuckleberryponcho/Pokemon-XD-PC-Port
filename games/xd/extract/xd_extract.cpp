#include "xd_extract.h"

#include <xd_trade_online.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <sstream>

namespace xd::extract {

namespace {

using xd::save::kPokemonSize;
using xd::save::Read16;
using xd::save::Read32;
using xd::save::Write16;
using xd::save::Write32;

constexpr size_t kHeroParty = 0x30;
constexpr size_t kBoxStride = 0x14 + xd::save::kBoxSlots * kPokemonSize;
constexpr size_t kBoxPokemon = 0x14;

std::vector<Place> PlacesOf(const xd::save::Save& s) {
  std::vector<Place> out;
  for (int i = 0; i < xd::save::kPartySize; i++) {
    if (!s.party[static_cast<size_t>(i)].IsEmpty()) out.push_back({-1, i, s.party[static_cast<size_t>(i)]});
  }
  for (int b = 0; b < xd::save::kBoxCount; b++) {
    for (int j = 0; j < xd::save::kBoxSlots; j++) {
      const auto& p = s.boxes[static_cast<size_t>(b)].slots[static_cast<size_t>(j)];
      if (!p.IsEmpty()) out.push_back({b, j, p});
    }
  }
  return out;
}

}

std::string Names::Species(uint16_t id) const {
  if (id < rules->species.size() && rules->species[id].exists) return names->Species(id) + " (#" + std::to_string(id) + ")";
  return "species #" + std::to_string(id) + " (only in a mod)";
}
std::string Names::Move(uint16_t id) const {
  if (id < rules->moves.size() && rules->moves[id].exists) return names->Move(id) + " (#" + std::to_string(id) + ")";
  return "move #" + std::to_string(id) + " (only in a mod)";
}
std::string Names::Item(uint16_t id) const {
  if (id < rules->items.size() && rules->items[id].exists) return names->Item(id) + " (#" + std::to_string(id) + ")";
  return "item #" + std::to_string(id) + " (only in a mod)";
}
bool Names::SpeciesOk(uint16_t id) const { return id < rules->species.size() && rules->species[id].exists; }
bool Names::MoveOk(uint16_t id) const { return id < rules->moves.size() && rules->moves[id].exists && !rules->moves[id].shadow; }
bool Names::ItemOk(uint16_t id) const { return id < rules->items.size() && rules->items[id].exists; }

std::map<uint16_t, uint16_t> ParseMap(const std::string& text) {
  std::map<uint16_t, uint16_t> m;
  std::stringstream ss(text);
  std::string part;
  while (std::getline(ss, part, ',')) {
    const size_t eq = part.find('=');
    if (eq == std::string::npos) continue;
    m[static_cast<uint16_t>(std::atoi(part.substr(0, eq).c_str()))] = static_cast<uint16_t>(std::atoi(part.substr(eq + 1).c_str()));
  }
  return m;
}

bool LoadSource(const std::string& path, Source& out, std::string& error) {
  if (!xd::trade::LoadCardSave(path, out.card, error)) return false;
  xd::save::ReadReport rep;
  if (!xd::save::ReadSave(out.card.data, out.save, rep, error)) return false;
  out.places = PlacesOf(out.save);
  return true;
}

std::vector<std::string> NotPlain(const Names& N, const xd::save::Pokemon& p) {
  std::vector<std::string> out;
  if (p.shadowId != 0) out.push_back("a Shadow Pokemon (its record is part of the save's Shadow list)");
  if (!N.SpeciesOk(p.species)) out.push_back(N.Species(p.species));
  for (const auto& m : p.moves) {
    if (m.id != 0 && !N.MoveOk(m.id)) out.push_back(N.Move(m.id));
  }
  if (p.heldItem != 0 && !N.ItemOk(p.heldItem)) out.push_back(N.Item(p.heldItem));
  return out;
}

std::string WhereText(const Place& p) { return p.box < 0 ? "party" : "box" + std::to_string(p.box + 1); }

TakeResult Take(const xd::legality::DiscRules& disc, const Source& source, size_t index, const xd::trade::CardSave& tgt, const xd::save::Save& target,
                const xd::save::ReadReport& trep, const Maps& maps, bool toParty) {
  TakeResult R;
  const Names N{&disc.rules, disc.names.get()};
  if (index >= source.places.size()) {
    R.fatal = true;
    R.refuse.push_back("there is no Pokemon " + std::to_string(index) + " in the source save");
    return R;
  }
  const Place& pl = source.places[index];
  const xd::save::Pokemon& p = pl.pokemon;
  R.fromWhere = WhereText(pl) + " " + std::to_string(pl.slot + 1);
  R.level = p.level;
  std::string error;
  std::vector<std::string>& refuse = R.refuse;
  std::vector<std::string>& notes = R.notes;
  if (p.shadowId != 0) {
    refuse.push_back("This is a Shadow Pokemon. Its record (status, heart gauge, order of meeting) is part of the source save's Shadow list and cannot be moved to another save.");
  }
  uint16_t species = p.species;
  const bool speciesChanged = !N.SpeciesOk(p.species);
  if (speciesChanged) {
    const auto m = maps.species.find(p.species);
    if (m == maps.species.end()) {
      refuse.push_back(N.Species(p.species) + " does not exist in the plain game. Use --species-map " + std::to_string(p.species) + "=<a plain species number> to take it out as another Pokemon.");
    } else if (!N.SpeciesOk(m->second)) {
      refuse.push_back("--species-map " + std::to_string(p.species) + "=" + std::to_string(m->second) + ": the plain game has no species #" + std::to_string(m->second) + ".");
    } else {
      species = m->second;
      notes.push_back(N.Species(p.species) + " becomes " + N.Species(species) + "; its experience, stats and HP are made again for the new species.");
    }
  }
  R.species = N.Species(speciesChanged && species != p.species ? species : p.species);
  R.speciesId = species;
  std::vector<std::pair<uint16_t, uint8_t>> moves;
  for (const auto& m : p.moves) {
    if (m.id == 0) continue;
    if (N.MoveOk(m.id)) {
      moves.push_back({m.id, m.ppUps});
      continue;
    }
    const auto mm = maps.moves.find(m.id);
    if (mm != maps.moves.end() && N.MoveOk(mm->second)) {
      moves.push_back({mm->second, 0});
      notes.push_back(N.Move(m.id) + " becomes " + N.Move(mm->second) + ".");
    } else if (mm != maps.moves.end()) {
      refuse.push_back("--move-map " + std::to_string(m.id) + "=" + std::to_string(mm->second) + ": the plain game has no (usable) move #" + std::to_string(mm->second) + ".");
    } else if (maps.dropMoves) {
      notes.push_back(N.Move(m.id) + " is forgotten (--drop-moves).");
    } else {
      refuse.push_back(N.Move(m.id) + " does not exist in the plain game. Use --move-map " + std::to_string(m.id) + "=<a plain move number>, or --drop-moves to forget it.");
    }
  }
  if (moves.empty() && refuse.empty()) refuse.push_back("No move would be left. Map at least one move (--move-map) so the Pokemon can battle.");
  uint16_t item = p.heldItem;
  if (item != 0 && !N.ItemOk(item)) {
    const auto im = maps.items.find(item);
    if (im != maps.items.end() && N.ItemOk(im->second)) {
      notes.push_back(N.Item(item) + " becomes " + N.Item(im->second) + ".");
      item = im->second;
    } else if (im != maps.items.end()) {
      refuse.push_back("--item-map " + std::to_string(item) + "=" + std::to_string(im->second) + ": the plain game has no item #" + std::to_string(im->second) + ".");
    } else if (maps.dropItem) {
      notes.push_back(N.Item(item) + " is taken away (--drop-item); it is not put into the bag.");
      item = 0;
    } else {
      refuse.push_back("The held " + N.Item(item) + " does not exist in the plain game. Use --item-map " + std::to_string(item) + "=<a plain item number>, or --drop-item.");
    }
  }
  if (!refuse.empty()) return R;

  const auto O = target.order;
  uint8_t rec[kPokemonSize];
  std::memcpy(rec, p.raw.data(), kPokemonSize);
  if (source.save.order != O) {
    refuse.push_back("the source save and the target save use different byte orders (GameCube / PC); convert one with xd_save_convert first.");
    R.fatal = true;
    return R;
  }
  Write16(rec + 0x00, species, O);
  Write16(rec + 0x02, item, O);
  for (size_t i = 0; i < 4; i++) {
    const uint16_t id = i < moves.size() ? moves[i].first : 0;
    Write16(rec + 0x80 + i * 4, id, O);
    rec[0x80 + i * 4 + 2] = id != 0 ? disc.rules.moves[id].basePp : 0;
    rec[0x80 + i * 4 + 3] = id != 0 ? moves[i].second : 0;
  }
  Write16(rec + 0xBA, 0, O);
  if (speciesChanged) {
    uint32_t exp = 0;
    xd::legality::ExpForLevel(disc.rules, disc.rules.species[species].growth, p.level, exp);
    Write32(rec + 0x20, exp, O);
    rec[0x1D] = static_cast<uint8_t>(rec[0x1D] & ~0x40);
    xd::save::Pokemon tmp = xd::save::ParsePokemon(rec, O);
    std::array<uint16_t, 6> st{};
    if (xd::legality::ExpectedStats(disc.rules, tmp, st)) {
      const int oldLoss = p.stats[0] > p.hp ? p.stats[0] - p.hp : 0;
      for (size_t i = 0; i < 6; i++) Write16(rec + 0x90 + i * 2, st[i], O);
      int hp = p.hp == 0 ? 0 : static_cast<int>(st[0]) - oldLoss;
      if (p.hp != 0 && hp < 1) hp = 1;
      Write16(rec + 0x04, static_cast<uint16_t>(hp), O);
    }
  }
  xd::save::Pokemon out = xd::save::ParsePokemon(rec, O);

  {
    xd::trade::Config cfg;
    cfg.rules = &disc.rules;
    cfg.names = disc.names.get();
    cfg.originalRules = &disc.rules;
    cfg.original = true;
    auto judgePtr = std::make_unique<xd::trade::Trader>(cfg);
    xd::trade::Trader& judge = *judgePtr;
    if (!judge.Load(tgt.data, error)) {
      R.fatal = true;
      refuse.push_back("target save: " + error);
      return R;
    }
    xd::trade::Bytes wire(rec, rec + kPokemonSize);
    wire.push_back(0);
    const auto v = judge.CheckIncoming(wire);
    if (!v.ok) {
      refuse.push_back("the plain game would not accept this Pokemon after the changes: " + v.text);
      return R;
    }
  }

  int boxAt = -2, slotAt = -1;
  if (toParty) {
    for (int i = 0; i < xd::save::kPartySize && slotAt < 0; i++) {
      if (target.party[static_cast<size_t>(i)].IsEmpty()) boxAt = -1, slotAt = i;
    }
    if (slotAt < 0) {
      refuse.push_back("the target's party is full (leave out --party to use the PC boxes).");
      return R;
    }
  } else {
    for (int b = 0; b < xd::save::kBoxCount && slotAt < 0; b++) {
      for (int j = 0; j < xd::save::kBoxSlots && slotAt < 0; j++) {
        if (target.boxes[static_cast<size_t>(b)].slots[static_cast<size_t>(j)].IsEmpty()) boxAt = b, slotAt = j;
      }
    }
    if (slotAt < 0) {
      refuse.push_back("the target's PC boxes are full.");
      return R;
    }
  }
  R.toWhere = boxAt == -1 ? "party place " + std::to_string(slotAt + 1) : "PC box " + std::to_string(boxAt + 1) + ", place " + std::to_string(slotAt + 1);
  if (p.metLocation > 0x7FF) R.metNote = "it was met at place #" + std::to_string(p.metLocation) + ", which may be a room only the mod has: its summary may show a wrong place name.";

  auto workPtr = std::make_unique<xd::save::Save>(target);
  xd::save::Save& work = *workPtr;
  uint8_t* data = work.slotBytes.data();
  const size_t hero = xd::save::SectionOffsetInSlot(work, 2);
  const size_t boxes = xd::save::SectionOffsetInSlot(work, 3);
  const size_t memo = xd::save::SectionOffsetInSlot(work, 12);
  if (hero == 0 || boxes == 0 || memo == 0 || work.slotBytes.size() != xd::save::kSlotSize) {
    refuse.push_back("the target save is missing a section.");
    R.fatal = true;
    return R;
  }
  uint8_t* at = boxAt == -1 ? data + hero + kHeroParty + static_cast<size_t>(slotAt) * kPokemonSize
                            : data + boxes + static_cast<size_t>(boxAt) * kBoxStride + kBoxPokemon + static_cast<size_t>(slotAt) * kPokemonSize;
  std::memcpy(at, rec, kPokemonSize);
  if (boxAt == -1) work.party[static_cast<size_t>(slotAt)] = out;
  else work.boxes[static_cast<size_t>(boxAt)].slots[static_cast<size_t>(slotAt)] = out;
  {
    uint8_t* mm = data + memo;
    const uint16_t count = Read16(mm, O);
    bool have = false;
    for (uint16_t i = 0; i < count && i < 500; i++) have = have || (Read16(mm + 4 + i * 12, O) & 0x3FFF) == species;
    if (!have && count < 500) {
      uint8_t* e = mm + 4 + count * 12;
      Write16(e, static_cast<uint16_t>(species | 0x8000), O);
      Write32(e + 4, out.otId, O);
      Write32(e + 8, out.pid, O);
      Write16(mm, static_cast<uint16_t>(count + 1), O);
      R.memoNote = "the Strategy Memo got a new entry for " + disc.names->Species(species) + ".";
    }
  }
  std::string err;
  const std::vector<uint8_t> file = xd::save::WriteSaveFile(tgt.data, work, trep, true, err);
  if (file.empty()) {
    refuse.push_back("the new save could not be built: " + err);
    R.fatal = true;
    return R;
  }
  auto checkPtr = std::make_unique<xd::save::Save>();
  xd::save::Save& check = *checkPtr;
  xd::save::ReadReport crep;
  if (!xd::save::ReadSave(file, check, crep, err)) {
    refuse.push_back("the new save did not pass its own checks: " + err);
    R.fatal = true;
    return R;
  }
  const xd::save::Pokemon& back = boxAt == -1 ? check.party[static_cast<size_t>(slotAt)] : check.boxes[static_cast<size_t>(boxAt)].slots[static_cast<size_t>(slotAt)];
  if (back.raw != out.raw) {
    refuse.push_back("the new save did not hold the Pokemon it should.");
    R.fatal = true;
    return R;
  }
  R.done = true;
  R.file = file;
  R.record.assign(rec, rec + kPokemonSize);
  return R;
}

}
