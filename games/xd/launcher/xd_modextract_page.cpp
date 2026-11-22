#include "xd_modextract_page.h"

#include <gc_memcard.h>
#include <xd_extract.h>
#include <xd_legality_disc.h>
#include <xd_save.h>
#include <xd_save_write.h>

#include <algorithm>
#include <cstring>
#include <ctime>
#include <fstream>
#include <memory>
#include <mutex>

#ifndef _WIN32
#define localtime_s(tmOut, timeIn) localtime_r((timeIn), (tmOut))
#endif

namespace xd::modextractpage {

namespace fs = std::filesystem;
namespace mc = engine::memcard;
using launcher::GameInfo;

namespace {

std::string Utf8(const fs::path& p) {
  const std::u8string u = p.u8string();
  return std::string(reinterpret_cast<const char*>(u.data()), u.size());
}

std::mutex sMutex;
std::shared_ptr<xd::legality::DiscRules> sRules;
std::string sRulesDisc;

std::shared_ptr<xd::legality::DiscRules> RulesFor(const std::string& disc, std::string& error) {
  const std::lock_guard<std::mutex> lock(sMutex);
  if (sRules && sRulesDisc == disc) return sRules;
  if (disc.empty()) {
    error = "Choose your Pokemon XD disc on the Disc page first: what is plain is decided by its tables.";
    return nullptr;
  }
  auto r = std::make_shared<xd::legality::DiscRules>();
  if (!xd::legality::LoadRulesFromDisc(disc, *r, error)) {
    error = "The disc can't be read: " + error;
    return nullptr;
  }
  sRules = r;
  sRulesDisc = disc;
  return sRules;
}

bool ReadBytes(const fs::path& p, std::vector<uint8_t>& out) {
  std::ifstream in(p, std::ios::binary);
  if (!in) return false;
  out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  return true;
}
bool WriteBytes(const fs::path& p, const std::vector<uint8_t>& bytes) {
  std::ofstream out(p, std::ios::binary | std::ios::trunc);
  out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  out.close();
  return static_cast<bool>(out);
}

struct XdSide {
  fs::path file;
  mc::CardFile card;
  xd::save::Save save;
  xd::save::ReadReport report;
  std::vector<uint8_t> data;
};

bool LoadXd(const fs::path& cardRoot, XdSide& out, std::string& error) {
  std::error_code ec;
  for (const auto& e : fs::directory_iterator(cardRoot / "USA" / "Card A", ec)) {
    std::vector<uint8_t> bytes;
    mc::CardFile card;
    std::string why;
    if (!e.is_regular_file(ec) || !ReadBytes(e.path(), bytes) || !mc::ReadGci(bytes, card, why)) continue;
    if (card.entry.fileName != "PokemonXD" || card.entry.gameCode != "GXXE" || card.data.size() < xd::save::kFileSize) continue;
    out.file = e.path();
    out.card = std::move(card);
    out.data.assign(out.card.data.begin(), out.card.data.begin() + static_cast<std::ptrdiff_t>(xd::save::kFileSize));
    if (!xd::save::ReadSave(out.data, out.save, out.report, why)) {
      error = "The Pokemon XD save can't be read: " + why;
      return false;
    }
    return true;
  }
  error = "This save is empty: there is no Pokemon XD save to put Pokemon into yet. Play and save once first.";
  return false;
}

std::string Stamp() {
  std::time_t t = std::time(nullptr);
  std::tm tm{};
  localtime_s(&tm, &t);
  char buf[32];
  std::strftime(buf, sizeof(buf), "%Y-%m-%d_%H-%M-%S", &tm);
  return buf;
}

std::string Replace(std::string s, const std::string& from, const std::string& to) {
  for (size_t at = s.find(from); at != std::string::npos; at = s.find(from, at + to.size())) s.replace(at, from.size(), to);
  return s;
}

xd::extract::Maps LauncherMaps() {
  xd::extract::Maps m;
  m.dropMoves = true;
  m.dropItem = true;
  return m;
}

std::string PlainWords(std::string s) {
  s = Replace(s, " (--drop-moves)", "");
  s = Replace(s, " (--drop-item)", "");
  const size_t cut = s.find(" Use --species-map");
  if (cut != std::string::npos) s = s.substr(0, cut) + " It can only come out as another, plain Pokemon: that needs the command line tool xd_mod_extract (--species-map).";
  return s;
}

std::string WhereOf(const xd::extract::Place& p) {
  return p.box < 0 ? "Party " + std::to_string(p.slot + 1) : "Box " + std::to_string(p.box + 1) + ", place " + std::to_string(p.slot + 1);
}

int FreePlaces(const xd::save::Save& s) {
  int n = 0;
  for (const auto& box : s.boxes) {
    for (const auto& p : box.slots) n += p.IsEmpty() ? 1 : 0;
  }
  return n;
}

}

bool Open(const fs::path& cardRoot, const fs::path& file, const std::string& discPath, GameInfo::Saves::MonsView& out, std::string& error) {
  const auto rules = RulesFor(discPath, error);
  if (!rules) return false;
  auto xdPtr = std::make_unique<XdSide>();
  XdSide& xd = *xdPtr;
  if (!LoadXd(cardRoot, xd, error)) return false;
  auto srcPtr = std::make_unique<xd::extract::Source>();
  xd::extract::Source& src = *srcPtr;
  if (!xd::extract::LoadSource(Utf8(file), src, error)) {
    error = "That file is not a Pokemon XD save the program can read (" + error + "). Choose the save's .gci file.";
    return false;
  }
  xd::trade::CardSave tgt;
  tgt.data = xd.data;
  out = GameInfo::Saves::MonsView{};
  out.who = xd::save::ToUtf8(src.save.trainerName);
  out.room = FreePlaces(xd.save);
  for (size_t i = 0; i < src.places.size(); i++) {
    const xd::extract::Place& pl = src.places[i];
    GameInfo::Saves::MonRow row;
    row.where = WhereOf(pl);
    const bool known = pl.pokemon.species < rules->rules.species.size() && rules->rules.species[pl.pokemon.species].exists;
    row.species = known ? rules->names->Species(pl.pokemon.species) : "species #" + std::to_string(pl.pokemon.species) + " (only in a mod)";
    row.nickname = xd::save::ToUtf8(pl.pokemon.nickname);
    row.level = std::to_string(pl.pokemon.level);
    const xd::extract::TakeResult r = xd::extract::Take(*rules, src, i, tgt, xd.save, xd.report, LauncherMaps(), false);
    if (!r.done) {
      row.problem = r.refuse.empty() ? std::string("it can't be made to fit the plain game") : PlainWords(r.refuse[0]);
    } else {
      for (const std::string& note : r.notes) row.lost.push_back(PlainWords(note));
    }
    out.rows.push_back(std::move(row));
  }
  out.notes.push_back("The Pokemon are COPIED into the PC boxes of this save: the modded save is only read, so they stay in it too.");
  out.notes.push_back("A move or held item that only the mod has is left out (listed above). A Pokemon of a species only the mod has, or a Shadow Pokemon, stays behind.");
  return true;
}

GameInfo::Saves::TradeResult Import(const fs::path& cardRoot, const fs::path& file, const std::string& discPath, const std::vector<int>& picked) {
  GameInfo::Saves::TradeResult result;
  auto fail = [&](const std::string& why) {
    result.ok = false;
    result.message = "Nothing was taken: " + why + " Nothing was written.";
    return result;
  };
  std::string error;
  const auto rules = RulesFor(discPath, error);
  if (!rules) return fail(error);
  auto xdPtr = std::make_unique<XdSide>();
  XdSide& xd = *xdPtr;
  if (!LoadXd(cardRoot, xd, error)) return fail(error);
  auto srcPtr = std::make_unique<xd::extract::Source>();
  xd::extract::Source& src = *srcPtr;
  if (!xd::extract::LoadSource(Utf8(file), src, error)) return fail("the modded save can't be read (" + error + ").");
  if (picked.empty()) return fail("Choose at least one Pokemon.");
  std::vector<uint8_t> data = xd.data;
  auto savePtr = std::make_unique<xd::save::Save>(xd.save);
  xd::save::ReadReport report = xd.report;
  size_t taken = 0;
  for (int i : picked) {
    if (i < 0 || i >= static_cast<int>(src.places.size())) return fail("That Pokemon is not in the list.");
    xd::trade::CardSave tgt;
    tgt.data = data;
    const xd::extract::TakeResult r = xd::extract::Take(*rules, src, static_cast<size_t>(i), tgt, *savePtr, report, LauncherMaps(), false);
    if (!r.done) return fail(WhereOf(src.places[static_cast<size_t>(i)]) + " can't come: " + (r.refuse.empty() ? std::string("it does not fit.") : PlainWords(r.refuse[0])));
    data = r.file;
    savePtr = std::make_unique<xd::save::Save>();
    report = xd::save::ReadReport();
    if (!xd::save::ReadSave(data, *savePtr, report, error)) return fail("the new save did not pass the program's own check (" + error + ").");
    taken++;
  }
  const fs::path backup = cardRoot / "save_backups" / ("modextract_" + Stamp());
  std::error_code ec;
  fs::create_directories(backup, ec);
  if (ec || !fs::copy_file(xd.file, backup / xd.file.filename(), ec)) {
    return fail("the backup copy could not be made in " + Utf8(backup) + " (" + ec.message() + ").");
  }
  mc::CardFile out = xd.card;
  std::memcpy(out.data.data(), data.data(), xd::save::kFileSize);
  fs::path tmp = xd.file;
  tmp += ".extract.tmp";
  if (!WriteBytes(tmp, mc::MakeGci(out))) {
    fs::remove(tmp, ec);
    return fail("the new save could not be written.");
  }
  fs::rename(tmp, xd.file, ec);
  if (ec) {
    fs::remove(tmp, ec);
    return fail("the save could not be replaced.");
  }
  result.ok = true;
  result.message = "Took " + std::to_string(taken) + " Pokemon out of the modded save into the PC boxes of this save (the first free places). The modded save was only read. A backup of this save as it was: " +
                   Utf8(backup) + ".";
  return result;
}

}
