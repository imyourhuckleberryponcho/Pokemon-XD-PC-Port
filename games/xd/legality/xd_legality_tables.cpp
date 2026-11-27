#include <xd_legality_tables.h>

#include <algorithm>
#include <cstring>

namespace xd::legality {

namespace {

constexpr uint32_t kVersion = 3;

struct Writer {
  std::vector<uint8_t> b;
  void u8(uint32_t v) { b.push_back(static_cast<uint8_t>(v)); }
  void u16(uint32_t v) { u8(v); u8(v >> 8); }
  void u32(uint32_t v) { u16(v); u16(v >> 16); }
  void str(const std::string& s) {
    u32(static_cast<uint32_t>(s.size()));
    b.insert(b.end(), s.begin(), s.end());
  }
  void strs(const std::vector<std::string>& v) {
    u32(static_cast<uint32_t>(v.size()));
    for (const std::string& s : v) str(s);
  }
};

struct Reader {
  const std::vector<uint8_t>& b;
  size_t at = 0;
  bool bad = false;
  explicit Reader(const std::vector<uint8_t>& bytes) : b(bytes) {}
  uint32_t u8() {
    if (at + 1 > b.size()) {
      bad = true;
      return 0;
    }
    return b[at++];
  }
  uint32_t u16() {
    const uint32_t lo = u8();
    return lo | u8() << 8;
  }
  uint32_t u32() {
    const uint32_t lo = u16();
    return lo | u16() << 16;
  }
  uint32_t count(size_t each) {
    const uint32_t n = u32();
    if (static_cast<uint64_t>(n) * each > b.size() - std::min(at, b.size())) {
      bad = true;
      return 0;
    }
    return n;
  }
  std::string str() {
    const uint32_t n = count(1);
    if (bad) return {};
    std::string s(reinterpret_cast<const char*>(b.data() + at), n);
    at += n;
    return s;
  }
  std::vector<std::string> strs() {
    std::vector<std::string> v;
    const uint32_t n = count(4);
    for (uint32_t i = 0; i < n && !bad; i++) v.push_back(str());
    return v;
  }
};

uint32_t Fnv(const uint8_t* p, size_t n) {
  uint32_t h = 2166136261u;
  for (size_t i = 0; i < n; i++) h = (h ^ p[i]) * 16777619u;
  return h;
}

void Bind(TablesFile& t) {
  const std::shared_ptr<TableNames> names = t.names;
  t.rules.speciesName = [names](uint16_t id) { return id < names->species.size() ? names->species[id] : std::string(); };
  t.rules.moveName = [names](uint16_t id) { return id < names->moves.size() ? names->moves[id] : std::string(); };
  t.rules.itemName = [names](uint16_t id) { return id < names->items.size() ? names->items[id] : std::string(); };
}

}

std::vector<uint8_t> Serialize(const TablesFile& t) {
  const Rules& r = t.rules;
  Writer w;
  w.b = {'X', 'D', 'T', 'B'};
  w.u32(kVersion);
  w.str(t.stamp);
  w.str(r.gameName);
  w.u32(r.vanillaSpecies);
  w.u32(r.vanillaMoves);
  w.u32(r.vanillaItems);
  w.u32(r.gen4Species);
  w.u32(r.gen4Moves);
  w.u32(r.gen4Items);
  w.u8(r.eggsExist ? 1 : 0);
  w.u32(r.maxEvTotal);
  for (uint16_t m : r.machineMove) w.u16(m);
  for (uint16_t m : r.tutorMove) w.u16(m);
  w.u32(static_cast<uint32_t>(r.expCurves.size()));
  for (const auto& c : r.expCurves) {
    for (uint32_t e : c) w.u32(e);
  }
  w.u32(static_cast<uint32_t>(r.giftMoves.size()));
  for (const auto& g : r.giftMoves) {
    w.u16(g.first);
    w.u16(g.second);
  }
  w.u32(static_cast<uint32_t>(r.species.size()));
  for (const SpeciesRules& s : r.species) {
    w.u8(s.exists ? 1 : 0);
    w.u16(s.national);
    w.u8(s.growth);
    w.u8(s.abilities[0]);
    w.u8(s.abilities[1]);
    for (uint16_t b : s.baseStats) w.u16(b);
    for (uint8_t m : s.machine) w.u8(m);
    for (uint8_t m : s.tutor) w.u8(m);
    w.u16(static_cast<uint32_t>(s.levelUp.size()));
    for (const LevelUpMove& l : s.levelUp) {
      w.u8(l.level);
      w.u16(l.move);
    }
    w.u16(static_cast<uint32_t>(s.eggMoves.size()));
    for (uint16_t m : s.eggMoves) w.u16(m);
    w.u8(static_cast<uint32_t>(s.evolutions.size()));
    for (const Evolution& e : s.evolutions) {
      w.u8(e.kind);
      w.u16(e.param);
      w.u16(e.to);
    }
    w.u16(s.evolvesFrom);
    w.u8(s.sexRatio);
    w.u16(s.initFriend);
  }
  w.u32(static_cast<uint32_t>(r.moves.size()));
  for (const MoveRules& m : r.moves) {
    w.u8((m.exists ? 1 : 0) | (m.shadow ? 2 : 0));
    w.u8(m.basePp);
  }
  w.u32(static_cast<uint32_t>(r.items.size()));
  for (const ItemRules& i : r.items) {
    w.u8(i.exists ? 1 : 0);
    w.u16(i.soubi);
    w.u8(i.bagKind);
  }
  const TableNames empty;
  const TableNames& n = t.names ? *t.names : empty;
  w.strs(n.species);
  w.strs(n.moves);
  w.strs(n.items);
  w.strs(n.abilities);
  w.strs(n.natures);
  w.strs(n.abilityText);
  w.strs(n.formKey);
  uint32_t extras = 0;
  for (const SpeciesRules& s : r.species) extras += (s.hiddenAbility != 0 || s.formOf != 0) ? 1 : 0;
  w.u32(extras);
  for (size_t i = 0; i < r.species.size(); i++) {
    const SpeciesRules& s = r.species[i];
    if (s.hiddenAbility == 0 && s.formOf == 0) continue;
    w.u16(static_cast<uint32_t>(i));
    w.u8(s.hiddenAbility);
    w.u16(s.formOf);
    w.u8(s.formKind);
    w.u16(s.plateItem);
  }
  w.u32(Fnv(w.b.data(), w.b.size()));
  return w.b;
}

bool Parse(const std::vector<uint8_t>& bytes, TablesFile& out, std::string& error) {
  if (bytes.size() < 12 || std::memcmp(bytes.data(), "XDTB", 4) != 0) {
    error = "not a tables file";
    return false;
  }
  {
    const size_t n = bytes.size() - 4;
    const uint32_t stored = bytes[n] | bytes[n + 1] << 8 | bytes[n + 2] << 16 | static_cast<uint32_t>(bytes[n + 3]) << 24;
    if (stored != Fnv(bytes.data(), n)) {
      error = "the tables file is damaged";
      return false;
    }
  }
  Reader rd(bytes);
  rd.at = 4;
  const uint32_t version = rd.u32();
  if (version != 1 && version != kVersion) {
    error = "the tables file is from another version";
    return false;
  }
  out = TablesFile{};
  Rules& r = out.rules;
  out.stamp = rd.str();
  r.gameName = rd.str();
  r.vanillaSpecies = rd.u32();
  r.vanillaMoves = rd.u32();
  r.vanillaItems = rd.u32();
  r.gen4Species = rd.u32();
  r.gen4Moves = rd.u32();
  r.gen4Items = rd.u32();
  r.eggsExist = rd.u8() != 0;
  r.maxEvTotal = rd.u32();
  for (uint16_t& m : r.machineMove) m = static_cast<uint16_t>(rd.u16());
  for (uint16_t& m : r.tutorMove) m = static_cast<uint16_t>(rd.u16());
  const uint32_t curves = rd.count(101 * 4);
  r.expCurves.resize(curves);
  for (auto& c : r.expCurves) {
    for (uint32_t& e : c) e = rd.u32();
  }
  const uint32_t gifts = rd.count(4);
  for (uint32_t i = 0; i < gifts && !rd.bad; i++) {
    const uint16_t a = static_cast<uint16_t>(rd.u16());
    const uint16_t b = static_cast<uint16_t>(rd.u16());
    r.giftMoves.push_back({a, b});
  }
  const uint32_t species = rd.count(60);
  r.species.resize(species);
  for (SpeciesRules& s : r.species) {
    if (rd.bad) break;
    s.exists = rd.u8() != 0;
    s.national = static_cast<uint16_t>(rd.u16());
    s.growth = static_cast<uint8_t>(rd.u8());
    s.abilities[0] = static_cast<uint8_t>(rd.u8());
    s.abilities[1] = static_cast<uint8_t>(rd.u8());
    for (uint16_t& b : s.baseStats) b = static_cast<uint16_t>(rd.u16());
    for (uint8_t& m : s.machine) m = static_cast<uint8_t>(rd.u8());
    for (uint8_t& m : s.tutor) m = static_cast<uint8_t>(rd.u8());
    const uint32_t lu = rd.u16();
    for (uint32_t i = 0; i < lu && !rd.bad; i++) {
      LevelUpMove l;
      l.level = static_cast<uint8_t>(rd.u8());
      l.move = static_cast<uint16_t>(rd.u16());
      s.levelUp.push_back(l);
    }
    const uint32_t eg = rd.u16();
    for (uint32_t i = 0; i < eg && !rd.bad; i++) s.eggMoves.push_back(static_cast<uint16_t>(rd.u16()));
    const uint32_t ev = rd.u8();
    for (uint32_t i = 0; i < ev && !rd.bad; i++) {
      Evolution e;
      e.kind = static_cast<uint8_t>(rd.u8());
      e.param = static_cast<uint16_t>(rd.u16());
      e.to = static_cast<uint16_t>(rd.u16());
      s.evolutions.push_back(e);
    }
    s.evolvesFrom = static_cast<uint16_t>(rd.u16());
    s.sexRatio = static_cast<uint8_t>(rd.u8());
    s.initFriend = static_cast<uint16_t>(rd.u16());
  }
  const uint32_t moves = rd.count(2);
  r.moves.resize(moves);
  for (MoveRules& m : r.moves) {
    const uint32_t f = rd.u8();
    m.exists = (f & 1) != 0;
    m.shadow = (f & 2) != 0;
    m.basePp = static_cast<uint8_t>(rd.u8());
  }
  const uint32_t items = rd.count(4);
  r.items.resize(items);
  for (ItemRules& i : r.items) {
    i.exists = rd.u8() != 0;
    i.soubi = static_cast<uint16_t>(rd.u16());
    i.bagKind = static_cast<uint8_t>(rd.u8());
  }
  out.names = std::make_shared<TableNames>();
  out.names->species = rd.strs();
  out.names->moves = rd.strs();
  out.names->items = rd.strs();
  out.names->abilities = rd.strs();
  out.names->natures = rd.strs();
  out.hasExtras = version >= 2;
  if (version >= 2) {
    out.names->abilityText = rd.strs();
    out.names->formKey = rd.strs();
    const uint32_t extras = rd.count(version >= 3 ? 8 : 6);
    for (uint32_t i = 0; i < extras && !rd.bad; i++) {
      const uint32_t row = rd.u16();
      const uint8_t hidden = static_cast<uint8_t>(rd.u8());
      const uint16_t formOf = static_cast<uint16_t>(rd.u16());
      const uint8_t kind = static_cast<uint8_t>(rd.u8());
      const uint16_t plate = version >= 3 ? static_cast<uint16_t>(rd.u16()) : 0;
      if (row < r.species.size()) {
        r.species[row].hiddenAbility = hidden;
        r.species[row].formOf = formOf;
        r.species[row].formKind = kind;
        r.species[row].plateItem = plate;
      }
    }
  }
  if (rd.bad || rd.at + 4 != bytes.size()) {
    error = "the tables file is cut short";
    return false;
  }
  Bind(out);
  return true;
}

}
