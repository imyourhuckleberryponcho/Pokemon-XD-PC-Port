#include <xd_legality_disc.h>
#include <xd_save_names.h>
#include <xd_saveedit.h>
#include <xd_saveedit_write.h>

#include <gc_memcard.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <sstream>

namespace fs = std::filesystem;
namespace se = xd::saveedit;
namespace xs = xd::save;

namespace {

std::vector<uint8_t> ReadAll(const fs::path& p) {
  std::ifstream in(p, std::ios::binary);
  return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

std::vector<std::string> Split(const std::string& s, char c) {
  std::vector<std::string> out;
  std::stringstream ss(s);
  std::string part;
  while (std::getline(ss, part, c)) out.push_back(part);
  return out;
}

std::u16string W(const std::string& s) { return std::u16string(s.begin(), s.end()); }

bool IsNumber(const std::string& s) { return !s.empty() && s.find_first_not_of("0123456789") == std::string::npos; }

}

int main(int argc, char** argv) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: xd_save_edit <save.gci> <disc> [--set key=value]... [--out new.gci | --commit <card root>]\n");
    return 2;
  }
  const fs::path gci = argv[1];
  const std::string disc = argv[2];
  std::vector<std::string> sets;
  fs::path out, commitRoot;
  for (int i = 3; i < argc; i++) {
    const std::string a = argv[i];
    if (a == "--set" && i + 1 < argc) sets.push_back(argv[++i]);
    else if (a == "--out" && i + 1 < argc) out = argv[++i];
    else if (a == "--commit" && i + 1 < argc) commitRoot = argv[++i];
    else {
      std::fprintf(stderr, "unknown argument: %s\n", a.c_str());
      return 2;
    }
  }
  std::string error;
  xd::legality::DiscRules dr;
  if (!xd::legality::LoadRulesFromDisc(disc, dr, error)) {
    std::fprintf(stderr, "the disc's tables can't be read: %s\n", error.c_str());
    return 1;
  }
  const std::vector<uint8_t> gciBytes = ReadAll(gci);
  engine::memcard::CardFile card;
  if (!engine::memcard::ReadGci(gciBytes, card, error) || card.data.size() < xs::kFileSize) {
    std::fprintf(stderr, "not a .gci save: %s\n", error.c_str());
    return 1;
  }
  const std::vector<uint8_t> data(card.data.begin(), card.data.begin() + static_cast<std::ptrdiff_t>(xs::kFileSize));
  se::Editor ed;
  if (!ed.Open(data, {}, error)) {
    std::fprintf(stderr, "the save can't be read: %s\n", error.c_str());
    return 1;
  }
  ed.SetSpeciesNames([&](uint16_t id) { return dr.names->Species(id); });
  se::Tables t;
  t.rules = &dr.rules;
  const auto species = [&](const std::string& v, uint16_t& id) {
    if (IsNumber(v)) {
      id = static_cast<uint16_t>(std::atoi(v.c_str()));
      return true;
    }
    for (size_t i = 1; i < dr.rules.species.size(); i++) {
      if (dr.names->Species(static_cast<uint16_t>(i)) == v) {
        id = static_cast<uint16_t>(i);
        return true;
      }
    }
    return false;
  };
  int failures = 0;
  const auto report = [&](const std::string& key, const std::string& why) {
    if (!why.empty()) {
      std::fprintf(stderr, "%s: %s\n", key.c_str(), why.c_str());
      failures++;
    }
  };
  for (const std::string& set : sets) {
    const size_t eq = set.find('=');
    if (eq == std::string::npos) {
      report(set, "expected key=value");
      continue;
    }
    const std::string key = set.substr(0, eq), value = set.substr(eq + 1);
    const std::vector<std::string> k = Split(key, '.');
    const long n = IsNumber(value) ? std::atol(value.c_str()) : 0;
    if (key == "money") report(key, ed.SetMoney(static_cast<uint32_t>(n)));
    else if (key == "coupons") report(key, ed.SetCoupons(static_cast<uint32_t>(n)));
    else if (key == "playtime") report(key, ed.SetPlayTime(static_cast<double>(n)));
    else if (key == "name") report(key, ed.SetTrainerName(W(value)));
    else if (key == "add") {
      const auto parts = Split(value, ':');
      uint16_t id = 0;
      se::Spot where;
      if (parts.size() < 2 || !species(parts[0], id)) report(key, "add=<species>:<level>[:party]");
      else report(key, ed.Add(id, std::atoi(parts[1].c_str()), parts.size() > 2 && parts[2] == "party", t, where));
    } else if (k.size() == 3 && k[0] == "bag" && k[2] == "add") {
      const auto parts = Split(value, ':');
      report(key, parts.size() == 2 ? ed.AddBagItem(static_cast<se::Pocket>(std::atoi(k[1].c_str())), static_cast<uint16_t>(std::atoi(parts[0].c_str())), std::atoi(parts[1].c_str()), t)
                                    : "bag.<pocket>.add=<item>:<count>");
    } else if (k.size() >= 3 && (k[0] == "party" || k[0] == "box")) {
      se::Spot s;
      size_t at = 2;
      if (k[0] == "party") {
        s = {se::Area::Party, 0, std::atoi(k[1].c_str()) - 1};
      } else {
        s = {se::Area::Box, std::atoi(k[1].c_str()) - 1, std::atoi(k[2].c_str()) - 1};
        at = 3;
      }
      if (k.size() <= at) {
        report(key, "which field?");
        continue;
      }
      const std::string f = k[at];
      uint16_t id = 0;
      if (f == "level") report(key, ed.SetLevel(s, static_cast<int>(n), t));
      else if (f == "species") report(key, species(value, id) ? ed.SetSpecies(s, id, t) : "unknown species");
      else if (f == "nick") report(key, ed.SetNickname(s, W(value)));
      else if (f == "item") report(key, ed.SetHeldItem(s, static_cast<uint16_t>(n), t));
      else if (f == "friendship") report(key, ed.SetFriendship(s, static_cast<int>(n)));
      else if (f == "heal") report(key, ed.Heal(s, t));
      else if (f == "iv" && k.size() > at + 1) report(key, ed.SetIv(s, std::atoi(k[at + 1].c_str()), static_cast<int>(n), t));
      else if (f == "ev" && k.size() > at + 1) report(key, ed.SetEv(s, std::atoi(k[at + 1].c_str()), static_cast<int>(n), t));
      else if (f == "move" && k.size() > at + 1) {
        const auto parts = Split(value, ':');
        report(key, ed.SetMove(s, std::atoi(k[at + 1].c_str()), static_cast<uint16_t>(std::atoi(parts[0].c_str())), parts.size() > 1 ? std::atoi(parts[1].c_str()) : 0, t));
      } else if (f == "nature" || f == "female" || f == "shiny") {
        se::PidWish w;
        (f == "nature" ? w.nature : f == "female" ? w.female : w.shiny) = static_cast<int>(n);
        report(key, ed.SetPid(s, w, t));
      } else report(key, "unknown field");
    } else {
      report(key, "unknown key");
    }
  }
  {
    const xs::Save& sv = ed.save();
    std::printf("trainer %s money %u coupons %u\n", xs::ToUtf8(sv.trainerName).c_str(), sv.money, sv.coupons);
    for (int i = 0; i < xs::kPartySize; i++) {
      const xs::Pokemon& p = sv.party[static_cast<size_t>(i)];
      if (!p.IsEmpty()) {
        std::printf("party.%d species %u level %d exp %u hp %u/%u pid %08x\n", i + 1, static_cast<unsigned>(p.species), static_cast<int>(p.level), p.exp,
                    static_cast<unsigned>(p.hp), static_cast<unsigned>(p.stats[0]), p.pid);
      }
    }
  }
  std::printf("playthrough key: %s\n", se::PlaythroughKey(ed.save().trainerOtId, ed.save().trainerName).c_str());
  for (const se::Change& c : ed.changes()) std::printf("%s%s\n", c.marks ? "[marks] " : "[      ] ", c.text.c_str());
  for (const std::string& p : ed.Problems(t)) std::printf("BLOCKED: %s\n", p.c_str());
  for (const std::string& p : ed.Notes(t)) std::printf("note: %s\n", p.c_str());
  std::printf("%s\n", ed.WillMark() ? "saving would mark the playthrough (cheats used)" : "saving would not mark the playthrough");
  if (failures != 0) return 1;
  if (!commitRoot.empty()) {
    se::CommitRequest req;
    req.cardRoot = commitRoot;
    req.gciFile = gci;
    const se::CommitResult r = se::Commit(ed, t, req);
    std::printf("%s\n", r.message.c_str());
    return r.ok ? 0 : 1;
  }
  if (!out.empty()) {
    const std::vector<uint8_t> built = ed.Build(error);
    if (built.empty()) {
      std::fprintf(stderr, "%s\n", error.c_str());
      return 1;
    }
    engine::memcard::CardFile edited = card;
    std::memcpy(edited.data.data(), built.data(), xs::kFileSize);
    const auto bytes = engine::memcard::MakeGci(edited);
    std::ofstream o(out, std::ios::binary | std::ios::trunc);
    o.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    std::printf("written: %s\n", out.string().c_str());
  }
  return 0;
}
