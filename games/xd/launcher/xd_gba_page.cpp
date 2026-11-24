#include "xd_gba_page.h"

#include <gc_memcard.h>
#include <gen3_save.h>
#include <xd_gba.h>
#include <xd_gba_disc.h>
#include <xd_legality.h>
#include <xd_legality_disc.h>
#include <xd_playthrough_files.h>
#include <xd_save.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#ifndef _WIN32
#define localtime_s(tmOut, timeIn) localtime_r((timeIn), (tmOut))
#endif

namespace xd::gbapage {

namespace fs = std::filesystem;
namespace mc = engine::memcard;
namespace g3 = engine::gen3;
using launcher::GameInfo;

namespace {

std::string Utf8(const fs::path& p) {
  const std::u8string u = p.u8string();
  return std::string(reinterpret_cast<const char*>(u.data()), u.size());
}

struct Rules {
  std::string disc;
  xd::gba::DiscRules trade;
  xd::legality::DiscRules legal;
};

std::mutex sMutex;
std::shared_ptr<Rules> sRules;

std::shared_ptr<Rules> RulesFor(const std::string& disc, std::string& error) {
  const std::lock_guard<std::mutex> lock(sMutex);
  if (sRules && sRules->disc == disc) return sRules;
  if (disc.empty()) {
    error = "Choose your Pokemon XD disc on the Disc page first: the trade takes its rules and names from it.";
    return nullptr;
  }
  auto r = std::make_shared<Rules>();
  r->disc = disc;
  if (!r->trade.Load(disc, error)) {
    error = "The disc can't be read: " + error;
    return nullptr;
  }
  if (!xd::legality::LoadRulesFromDisc(disc, r->legal, error)) {
    error = "The disc's rules can't be read: " + error;
    return nullptr;
  }
  sRules = r;
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
  const fs::path folder = cardRoot / "USA" / "Card A";
  for (const auto& e : fs::directory_iterator(folder, ec)) {
    std::vector<uint8_t> bytes;
    mc::CardFile card;
    std::string why;
    if (!e.is_regular_file(ec) || !ReadBytes(e.path(), bytes) || !mc::ReadGci(bytes, card, why)) continue;
    if (card.entry.fileName != "PokemonXD" || card.entry.gameCode != "GXXE") continue;
    if (card.data.size() < xd::save::kFileSize) continue;
    out.file = e.path();
    out.card = std::move(card);
    out.data.assign(out.card.data.begin(), out.card.data.begin() + static_cast<std::ptrdiff_t>(xd::save::kFileSize));
    if (!xd::save::ReadSave(out.data, out.save, out.report, why)) {
      error = "The Pokemon XD save can't be read: " + why;
      return false;
    }
    return true;
  }
  error = "This save is empty: there is no Pokemon XD save to trade from yet. Play and save once first.";
  return false;
}

bool LoadGba(const fs::path& file, g3::Save& out, std::string& error) {
  std::vector<uint8_t> bytes;
  if (!ReadBytes(file, bytes)) {
    error = "The file can't be read: " + Utf8(file);
    return false;
  }
  if (bytes.size() < g3::kFlashSize || bytes.size() > g3::kFlashSize + 0x100) {
    error = "This is not a Game Boy Advance Pokemon save (those are 128 KB; this file is " +
            std::to_string(bytes.size() / 1024) + " KB).";
    return false;
  }
  std::vector<std::string> warnings;
  if (!g3::ReadSave(bytes, g3::Game::Unknown, out, error, warnings)) {
    return false;
  }
  return true;
}

xd::save::Pokemon AsPokemon(const xd::gba::XdMon& m) { return xd::save::ParsePokemon(m.raw.data(), m.order); }

std::string Problem(const Rules& r, const xd::gba::XdMon& mon, xd::gba::State state, bool fromXd,
                    const xd::save::Save* xdSave) {
  using xd::gba::State;
  if (state == State::Empty) return "empty";
  if (state != State::Tradeable) return xd::gba::StateText(state);
  xd::gba::XdMon arriving = mon;
  if (!fromXd) xd::gba::ResetStats(arriving, r.trade);
  const xd::save::Pokemon p = AsPokemon(arriving);
  if (fromXd) {
    if (p.species > 411 || r.trade.NationalDex(p.species) > 386 || r.trade.NationalDex(p.species) == 0) {
      return "A Generation 4 Pokemon can't go into a Game Boy Advance game: they don't have it.";
    }
  }
  xd::legality::Context ctx;
  ctx.movesNotJudged = !fromXd;
  if (fromXd && xdSave != nullptr) ctx.shadow = xdSave->ShadowOf(p);
  const auto res = xd::legality::Check(p, r.legal.rules, xd::legality::Level::Legal, ctx);
  if (!res.Ok()) return "Not possible in the game: " + res.Text(" ");
  return "";
}

GameInfo::Saves::TradeMon RowOf(const Rules& r, const xd::gba::XdMon& m, const std::string& problem) {
  GameInfo::Saves::TradeMon row;
  const auto& n = r.trade.Names();
  std::string s = n.Species(m.Species());
  row.species = s.empty() ? "Pokemon #" + std::to_string(m.Species()) : s;
  row.nickname = xd::save::ToUtf8(m.Text(0x4E));
  row.level = std::to_string(m.Level());
  row.item = m.Item() == 0 ? "-" : (n.Item(m.Item()).empty() ? "item #" + std::to_string(m.Item()) : n.Item(m.Item()));
  row.problem = problem;
  return row;
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

bool Open(const fs::path& cardRoot, const fs::path& gbaFile, const std::string& discPath, GameInfo::Saves::TradeView& out,
          std::string& error) {
  const auto rules = RulesFor(discPath, error);
  if (!rules) return false;
  XdSide xd;
  if (!LoadXd(cardRoot, xd, error)) return false;
  g3::Save gba;
  if (!LoadGba(gbaFile, gba, error)) return false;
  const xd::gba::PartyView xp = xd::gba::XdParty(xd.save, rules->trade);
  const xd::gba::PartyView gp = xd::gba::GbaParty(gba, rules->trade, xd.save.order);
  out = GameInfo::Saves::TradeView{};
  out.mineWho = xd::save::ToUtf8(xd.save.trainerName);
  out.theirsWho = std::string(g3::GameName(gba.game)) + " save";
  for (int i = 0; i < 6; i++) {
    out.mine.push_back(RowOf(*rules, xp.mons[static_cast<size_t>(i)],
                             Problem(*rules, xp.mons[static_cast<size_t>(i)], xp.states[static_cast<size_t>(i)], true, &xd.save)));
    if (i < gba.PartyCount()) {
      out.theirs.push_back(RowOf(*rules, gp.mons[static_cast<size_t>(i)],
                                 Problem(*rules, gp.mons[static_cast<size_t>(i)], gp.states[static_cast<size_t>(i)], false, nullptr)));
    }
  }
  out.notes.push_back("The trade is one for one between the two parties, as the GameCube's link-cable trade is. Pokemon in a "
                      "GBA game's PC boxes have to be moved into its party first.");
  out.notes.push_back("Your GBA save is never changed: the trade makes a new file next to it (<name>.traded.sav). Put it in "
                      "place of the original while the GBA game or emulator is closed.");
  return true;
}

GameInfo::Saves::TradeResult Trade(const fs::path& cardRoot, const fs::path& gbaFile, const std::string& discPath, int xdSlot,
                            int gbaSlot) {
  GameInfo::Saves::TradeResult result;
  auto fail = [&](const std::string& why) {
    result.ok = false;
    result.message = "No trade: " + why + " Nothing was written.";
    return result;
  };
  std::string error;
  const auto rules = RulesFor(discPath, error);
  if (!rules) return fail(error);
  XdSide xd;
  if (!LoadXd(cardRoot, xd, error)) return fail(error);
  g3::Save gba;
  if (!LoadGba(gbaFile, gba, error)) return fail(error);
  if (xdSlot < 0 || xdSlot > 5 || gbaSlot < 0 || gbaSlot > 5) return fail("Choose one Pokemon on each side.");
  const xd::gba::PartyView xp = xd::gba::XdParty(xd.save, rules->trade);
  const xd::gba::PartyView gp = xd::gba::GbaParty(gba, rules->trade, xd.save.order);
  if (const std::string why = Problem(*rules, xp.mons[static_cast<size_t>(xdSlot)], xp.states[static_cast<size_t>(xdSlot)], true, &xd.save); !why.empty()) {
    return fail("your Pokemon can't go: " + why);
  }
  if (gbaSlot >= gba.PartyCount()) return fail("There is no Pokemon in that place of the GBA party.");
  if (const std::string why = Problem(*rules, gp.mons[static_cast<size_t>(gbaSlot)], gp.states[static_cast<size_t>(gbaSlot)], false, nullptr); !why.empty()) {
    return fail("the GBA Pokemon can't come: " + why);
  }
  xd::gba::TradeRequest req;
  req.xdSlot = xdSlot;
  req.gbaSlot = gbaSlot;
  xd::gba::TradeResult r = xd::gba::Trade(xd.data, xd.save, xd.report, gba, req, rules->trade);
  if (!r.ok) return fail(r.error + ".");

  {
    xd::save::Save check;
    xd::save::ReadReport rep;
    std::string err;
    if (!xd::save::ReadSave(r.xdFile, check, rep, err) || check.slot == xd.save.slot || check.saveCount <= xd.save.saveCount) {
      return fail("the new XD save did not pass the program's own check (" + err + ").");
    }
    const xd::gba::PartyView np = xd::gba::XdParty(check, rules->trade);
    if (np.mons[static_cast<size_t>(xdSlot)].Pid() != gp.mons[static_cast<size_t>(gbaSlot)].Pid()) {
      return fail("the new XD save does not hold the Pokemon that should arrive.");
    }
    g3::Save gcheck;
    std::vector<std::string> warnings;
    if (!g3::ReadSave(r.gbaFile, gba.game, gcheck, err, warnings) || gcheck.counter != gba.counter + 1) {
      return fail("the new GBA save did not pass the program's own check (" + err + ").");
    }
    const g3::PlainMon got = gcheck.PartyMon(gbaSlot);
    if (!got.checksumOk || got.Pid() != xp.mons[static_cast<size_t>(xdSlot)].Pid()) {
      return fail("the new GBA save does not hold the Pokemon that should arrive.");
    }
  }

  const fs::path backup = cardRoot / "save_backups" / ("gba_trade_" + Stamp());
  std::error_code ec;
  fs::create_directories(backup, ec);
  if (ec || !fs::copy_file(xd.file, backup / xd.file.filename(), ec) || !fs::copy_file(gbaFile, backup / gbaFile.filename(), ec)) {
    return fail("the backup copies could not be made in " + Utf8(backup) + " (" + ec.message() + ").");
  }
  mc::CardFile out = xd.card;
  std::memcpy(out.data.data(), r.xdFile.data(), xd::save::kFileSize);
  fs::path tmp = xd.file;
  tmp += ".trade.tmp";
  fs::path gbaOut = gbaFile.parent_path() / (gbaFile.stem().string() + ".traded" + gbaFile.extension().string());
  if (!WriteBytes(gbaOut, r.gbaFile)) return fail("the new GBA save could not be written to " + Utf8(gbaOut) + ".");
  if (!WriteBytes(tmp, mc::MakeGci(out))) {
    fs::remove(tmp, ec);
    return fail("the new XD save could not be written.");
  }
  fs::rename(tmp, xd.file, ec);
  if (ec) {
    fs::remove(tmp, ec);
    return fail("the XD save could not be replaced.");
  }
  xd::playthrough::KeepExtensionPaired(std::vector<uint8_t>(xd.card.data.begin(), xd.card.data.begin() + static_cast<std::ptrdiff_t>(xd::save::kFileSize)), r.xdFile);
  const auto& n = rules->trade.Names();
  auto nameOf = [&](uint16_t s) { return n.Species(s).empty() ? "#" + std::to_string(s) : n.Species(s); };
  result.ok = true;
  result.message = "Traded: " + nameOf(xp.mons[static_cast<size_t>(xdSlot)].Species()) + " (XD party #" + std::to_string(xdSlot + 1) +
                   ") <-> " + nameOf(gp.mons[static_cast<size_t>(gbaSlot)].Species()) + " (GBA party #" + std::to_string(gbaSlot + 1) +
                   "). The XD save now holds the new Pokemon. The new GBA save is " + Utf8(gbaOut) +
                   " (your original is unchanged). Backups of both: " + Utf8(backup) + ".";
  return result;
}

}
