#include "xd_colosseum_page.h"

#include <card_sources.h>
#include <colo_save.h>
#include <gc_memcard.h>
#include <xd_colosseum.h>
#include <xd_legality.h>
#include <xd_legality_disc.h>
#include <xd_playthrough_files.h>
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

namespace xd::colosseumpage {

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
    error = "Choose your Pokemon XD disc on the Disc page first: the import takes its rules and names from it.";
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

bool LoadColo(const fs::path& file, engine::colo::Save& out, std::string& error) {
  namespace cs = engine::memcard::sources;
  std::vector<uint8_t> bytes;
  if (!ReadBytes(file, bytes)) {
    error = "The file can't be read: " + Utf8(file);
    return false;
  }
  std::vector<mc::CardFile> files;
  cs::Container kind;
  if (!cs::ReadAnyCardFile(bytes, files, kind, error)) return false;
  const mc::CardFile* found = nullptr;
  std::string other;
  for (const mc::CardFile& f : files) {
    if (f.entry.gameCode.compare(0, 3, "GC6") == 0) {
      if (f.entry.gameCode == "GC6E") {
        found = &f;
        break;
      }
      other = f.entry.gameCode;
    }
  }
  if (found == nullptr) {
    if (!other.empty()) {
      error = "This is a Pokemon Colosseum save of another region (" + other + "); only the USA game's (GC6E) is understood.";
    } else if (files.size() == 1 && files[0].entry.fileName == "PokemonXD") {
      error = "This is a Pokemon XD save, not a Pokemon Colosseum one. (Pokemon XD saves are added with \"Import from Dolphin / GameCube\".)";
    } else if (files.size() == 1) {
      error = "This is not a Pokemon Colosseum save (it is \"" + files[0].entry.fileName + "\" of game " + files[0].entry.gameCode + ").";
    } else {
      error = "This memory card holds " + std::to_string(files.size()) + " files, none of them a Pokemon Colosseum save.";
    }
    return false;
  }
  if (found->data.size() < engine::colo::kFileSize) {
    error = "The Pokemon Colosseum save file is too short.";
    return false;
  }
  std::array<engine::colo::SlotReport, engine::colo::kSlotCount> report;
  return engine::colo::ReadSave(std::vector<uint8_t>(found->data.begin(), found->data.begin() + static_cast<std::ptrdiff_t>(engine::colo::kFileSize)),
                                out, report, error);
}

std::string Stamp() {
  std::time_t t = std::time(nullptr);
  std::tm tm{};
  localtime_s(&tm, &t);
  char buf[32];
  std::strftime(buf, sizeof(buf), "%Y-%m-%d_%H-%M-%S", &tm);
  return buf;
}

}

bool Open(const fs::path& cardRoot, const fs::path& file, const std::string& discPath, GameInfo::Saves::MonsView& out,
          std::string& error) {
  const auto rules = RulesFor(discPath, error);
  if (!rules) return false;
  XdSide xd;
  if (!LoadXd(cardRoot, xd, error)) return false;
  engine::colo::Save colo;
  if (!LoadColo(file, colo, error)) return false;
  out = GameInfo::Saves::MonsView{};
  out.who = xd::save::ToUtf8(colo.trainerName);
  out.room = xd::colosseum::FreeBoxPlaces(xd.save);
  for (const xd::colosseum::Candidate& c : xd::colosseum::Candidates(colo, rules->rules, xd.save.order)) {
    GameInfo::Saves::MonRow row;
    row.where = c.where;
    const std::string n = rules->names->Species(c.core.species);
    row.species = n.empty() ? "Pokemon #" + std::to_string(c.core.species) : n;
    row.nickname = xd::save::ToUtf8(c.core.nickname);
    row.level = std::to_string(c.core.level);
    row.problem = c.problem;
    row.lost = c.lost;
    out.rows.push_back(std::move(row));
  }
  out.notes.push_back("The Pokemon are COPIED into the PC boxes of this save: the Colosseum save is only read, so they stay in it too.");
  out.notes.push_back("Unpurified Shadow Pokemon stay behind; Colosseum-only items, places and Shadow records are left out (listed above).");
  return true;
}

GameInfo::Saves::TradeResult Import(const fs::path& cardRoot, const fs::path& file, const std::string& discPath,
                                    const std::vector<int>& picked) {
  GameInfo::Saves::TradeResult result;
  auto fail = [&](const std::string& why) {
    result.ok = false;
    result.message = "Nothing was imported: " + why + " Nothing was written.";
    return result;
  };
  std::string error;
  const auto rules = RulesFor(discPath, error);
  if (!rules) return fail(error);
  XdSide xd;
  if (!LoadXd(cardRoot, xd, error)) return fail(error);
  engine::colo::Save colo;
  if (!LoadColo(file, colo, error)) return fail(error);
  const auto cands = xd::colosseum::Candidates(colo, rules->rules, xd.save.order);
  if (picked.empty()) return fail("Choose at least one Pokemon.");
  std::vector<std::array<uint8_t, xd::save::kPokemonSize>> records;
  for (int i : picked) {
    if (i < 0 || i >= static_cast<int>(cands.size())) return fail("That Pokemon is not in the list.");
    const auto& c = cands[static_cast<size_t>(i)];
    if (!c.problem.empty()) return fail(c.where + " can't come: " + c.problem);
    records.push_back(c.xd);
  }
  if (!xd::colosseum::PlaceInBoxes(xd.save, records, error)) return fail(error);
  const std::vector<uint8_t> newData = xd::save::WriteSaveFile(xd.data, xd.save, xd.report, false, error);
  if (newData.empty()) return fail("the new save could not be made (" + error + ").");
  {
    xd::save::Save check;
    xd::save::ReadReport rep;
    std::string err;
    if (!xd::save::ReadSave(newData, check, rep, err) || check.slot == xd.save.slot) {
      return fail("the new XD save did not pass the program's own check (" + err + ").");
    }
    int found = 0;
    for (const auto& box : check.boxes) {
      for (const auto& p : box.slots) {
        for (const auto& r : records) {
          if (std::memcmp(p.raw.data(), r.data(), xd::save::kPokemonSize) == 0) {
            found++;
            break;
          }
        }
      }
    }
    if (found < static_cast<int>(records.size())) return fail("the new XD save does not hold all the Pokemon.");
  }
  const fs::path backup = cardRoot / "save_backups" / ("colosseum_import_" + Stamp());
  std::error_code ec;
  fs::create_directories(backup, ec);
  if (ec || !fs::copy_file(xd.file, backup / xd.file.filename(), ec)) {
    return fail("the backup copy could not be made in " + Utf8(backup) + " (" + ec.message() + ").");
  }
  mc::CardFile out = xd.card;
  std::memcpy(out.data.data(), newData.data(), xd::save::kFileSize);
  fs::path tmp = xd.file;
  tmp += ".import.tmp";
  if (!WriteBytes(tmp, mc::MakeGci(out))) {
    fs::remove(tmp, ec);
    return fail("the new XD save could not be written.");
  }
  fs::rename(tmp, xd.file, ec);
  if (ec) {
    fs::remove(tmp, ec);
    return fail("the XD save could not be replaced.");
  }
  xd::playthrough::KeepExtensionPaired(std::vector<uint8_t>(xd.card.data.begin(), xd.card.data.begin() + static_cast<std::ptrdiff_t>(xd::save::kFileSize)), newData);
  result.ok = true;
  result.message = "Copied " + std::to_string(records.size()) + " Pokemon from the Colosseum save into the PC boxes of this save (the " +
                   "first free places). The Colosseum save was only read. A backup of this save as it was: " + Utf8(backup) + ".";
  return result;
}

}
