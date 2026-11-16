#include <launcher.h>
#include <mods.h>

#include "disc_verify.h"
#include "dol_tables.h"
#include "xd_extra_sources.h"
#include "xd_colosseum_page.h"
#include "xd_gba_page.h"
#include "xd_languages.h"
#include "xd_settings.h"
#include "xd_user_folder.h"
#include "xd_playthrough_files.h"

#include <card_sources.h>
#include <gc_memcard.h>
#include <i18n.h>
#include <gs_formats.h>
#include <port_disc.h>
#include <save_backups.h>
#include <update.h>
#include <xd_save.h>
#include <xd_save_convert.h>
#include <xd_save_names.h>
#include <playthrough.h>
#include <randomizer.h>
#include <xd_randomizer.h>

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_main.h>

#include <launcher_sys.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <dxgi.h>
#endif

#if defined(__APPLE__)
#include <unistd.h>
#endif

#include <algorithm>
#include <cctype>
#include <cstdarg>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <iterator>
#include <mutex>
#include <chrono>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;
using engine::i18n::Tr;
using launcher::DiscVerdict;
using launcher::SaveSummary;

std::string Utf8(const fs::path& p) {
  const std::u8string u8 = p.u8string();
  return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
}

std::string Fmt(const char* format, ...) {
  char buf[2048];
  va_list args;
  va_start(args, format);
  std::vsnprintf(buf, sizeof(buf), format, args);
  va_end(args);
  return buf;
}

std::string TrainerText(const std::string& name, int gender) {
  return Fmt(gender == 0 ? Tr("%s (boy)") : Tr("%s (girl)"), name.c_str());
}

bool ReadBytes(const fs::path& path, std::vector<uint8_t>& out) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return false;
  }
  out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  return !in.bad();
}

DiscVerdict CheckDisc(const std::string& path, const std::function<bool(uint64_t, uint64_t)>& progress) {
  PortXDDiscVerdict v;
  PortXD_JudgeDisc(
      path.c_str(),
      [](uint64_t done, uint64_t total, void* user) {
        return (*static_cast<const std::function<bool(uint64_t, uint64_t)>*>(user))(done, total);
      },
      const_cast<void*>(static_cast<const void*>(&progress)), &v);
  DiscVerdict out;
  switch (v.kind) {
  case XD_DISC_GOOD: out.kind = DiscVerdict::Kind::Good; break;
  case XD_DISC_USABLE: out.kind = DiscVerdict::Kind::Usable; break;
  case XD_DISC_WARNING: out.kind = DiscVerdict::Kind::Warning; break;
  default: out.kind = DiscVerdict::Kind::Problem; break;
  }
  out.text = v.text;
  out.gameId = v.gameId;
  out.title = v.title;
  return out;
}

bool IdentifyDisc(const std::string& path, std::string& gameId, std::string& title, std::string& error) {
  PortDiscInfo info;
  char reason[256] = {};
  const PortDiscKind kind = PortDisc_Probe(path.c_str(), &info, reason, sizeof reason);
  if (kind == PORT_DISC_UNREADABLE) {
    error = reason;
    return false;
  }
  if (kind == PORT_DISC_NOT_A_DISC) {
    error = Tr("it has no GameCube or Wii disc mark");
    return false;
  }
  gameId = info.gameId;
  title = info.title;
  return true;
}

xd::save::GameNames sNames;
std::string sNamesDisc;
std::mutex sOneReader;
void SummaryOf(const xd::save::Save& save, const std::string& discPath, launcher::SaveSummary& out);

std::string Number(uint32_t n) {
  std::string s = std::to_string(n);
  for (int i = static_cast<int>(s.size()) - 3; i > 0; i -= 3) {
    s.insert(static_cast<size_t>(i), ",");
  }
  return s;
}

std::string PlayTime(double seconds) {
  const long long s = static_cast<long long>(seconds);
  char buf[48];
  std::snprintf(buf, sizeof(buf), Tr("%lld hours %02lld minutes"), s / 3600, (s / 60) % 60);
  return buf;
}

fs::path CardFolder() {
  const fs::path root = xd::CardRoot();
  std::error_code ec;
  if (fs::is_directory(root / L"USA" / L"Card A", ec)) {
    return root / L"USA" / L"Card A";
  }
  for (const auto& e : fs::directory_iterator(root, ec)) {
    const fs::path name = e.path().filename();
    if (name == L"save_backups" || name == L"playthroughs" || name == L"saves") continue;
    if (e.is_directory(ec) && fs::is_directory(e.path() / L"Card A", ec)) {
      return e.path() / L"Card A";
    }
  }
  return root / L"USA" / L"Card A";
}

std::string RandomizerStyle(const std::string& code) {
  namespace xr = xd::randomizer;
  engine::randomizer::Settings rs;
  xr::ApplyStyle(rs, xr::Style::Classic);
  bool seedOnly = false;
  std::string decodeError;
  if (code.empty() || !engine::randomizer::DecodeCode(xr::GetSchema(), code, rs, seedOnly, decodeError)) {
    return {};
  }
  switch (xr::StyleOf(rs)) {
  case xr::Style::Light: return "Light";
  case xr::Style::Classic: return "Classic";
  case xr::Style::Chaos: return "Chaos";
  default: return "Custom";
  }
}

bool FindSave(const fs::path& folder, std::vector<uint8_t>& data, fs::path& file) {
  namespace mc = engine::memcard;
  std::error_code ec;
  data.clear();
  for (const auto& e : fs::directory_iterator(folder, ec)) {
    if (!e.is_regular_file(ec)) {
      continue;
    }
    std::vector<uint8_t> bytes;
    std::vector<mc::CardFile> files;
    mc::CardImageInfo info;
    std::string error;
    if (!ReadBytes(e.path(), bytes) || !mc::LoadCardFiles(bytes, files, info, error)) {
      continue;
    }
    for (const mc::CardFile& f : files) {
      if (f.entry.fileName == "PokemonXD" && f.entry.gameCode.compare(0, 3, "GXX") == 0) {
        data = f.data;
        file = e.path();
      }
    }
  }
  return !data.empty();
}

launcher::GameInfo::SaveRow SaveRowOf(const fs::path& cardRoot) {
  namespace ep = engine::playthrough;
  launcher::GameInfo::SaveRow row;
  const xd::CardRootScope scope(cardRoot);
  std::vector<uint8_t> data;
  fs::path file;
  if (!FindSave(CardFolder(), data, file)) {
    row.who = Tr("empty");
    row.setup = Tr("no save yet: the game offers New Game");
    return row;
  }
  xd::save::Save save;
  xd::save::ReadReport report;
  std::string error;
  if (!xd::save::ReadSave(data, save, report, error)) {
    row.who = Tr("a save that can't be read");
    return row;
  }
  row.found = true;
  row.who = TrainerText(xd::save::ToUtf8(save.trainerName), save.trainerGender);
  row.time = PlayTime(save.playTimeSeconds);
  const xd::playthrough::SaveId id = xd::playthrough::SaveIdOf(data);
  std::error_code ec;
  ep::Setup setup;
  if (!id.found || !fs::exists(xd::playthrough::SetupFileFor(id), ec)) {
    row.setup = Tr("Faithful \xC2\xB7 Vanilla (no setup file)");
  } else if (!ep::ReadFile(xd::playthrough::SetupFileFor(id), setup, error)) {
    row.setup = Tr("its setup file can't be read");
  } else {
    row.setup = ep::Label(setup);
    const std::string style = RandomizerStyle(setup.seed);
    const size_t at = row.setup.find("Randomizer ");
    if (!style.empty() && at != std::string::npos) {
      row.setup.insert(at + 11, style + " ");
    }
  }
  return row;
}

fs::path SaveFileOf(const fs::path& cardRoot) {
  const xd::CardRootScope scope(cardRoot);
  std::vector<uint8_t> data;
  fs::path file;
  return FindSave(CardFolder(), data, file) ? file : fs::path();
}

bool CheckImport(const fs::path& file, const std::string& discPath, launcher::GameInfo::ImportCheck& out,
                 std::string& error) {
  namespace mc = engine::memcard;
  namespace cs = engine::memcard::sources;
  std::vector<uint8_t> bytes;
  if (!ReadBytes(file, bytes)) {
    error = Fmt(Tr("The file can't be read: %s"), Utf8(file).c_str());
    return false;
  }
  std::vector<mc::CardFile> cardFiles;
  cs::Container kind;
  std::string why;
  if (!cs::ReadAnyCardFile(bytes, cardFiles, kind, why)) {
    error = why;
    return false;
  }
  const mc::CardFile* card = nullptr;
  bool otherRegion = false;
  std::string otherCode;
  for (const mc::CardFile& f : cardFiles) {
    if (f.entry.fileName == "PokemonXD" && f.entry.gameCode.compare(0, 3, "GXX") == 0) {
      if (f.entry.gameCode == "GXXE") {
        card = &f;
        break;
      }
      otherRegion = true;
      otherCode = f.entry.gameCode;
    }
  }
  if (card == nullptr) {
    if (otherRegion) {
      if (otherCode == "GXXP") {
        error = Tr("This is a European (PAL) Pokemon XD save (game GXXP). The USA game here can't use it. The save's layout is the "
                   "same as the USA one (the decomp's list of what differs in the European game leaves the save code alone), but "
                   "a European save asks for the European game's language (English, French, German, Italian or Spanish text) and "
                   "this USA disc doesn't have those texts, so it is refused rather than risked. A language pack from a "
                   "European disc could change that later.");
      } else if (otherCode == "GXXJ") {
        error = Tr("This is a Japanese Pokemon XD save (game GXXJ). The USA game here can't use it.");
      } else {
        error = Fmt(Tr("This Pokemon XD save is from another region's game (%s); the game here is the USA version (GXXE)."),
                    otherCode.c_str());
      }
    } else if (cardFiles.size() == 1) {
      error = Fmt(Tr("This save file is not a Pokemon XD save (it is \"%s\" of game %s)."), cardFiles[0].entry.fileName.c_str(),
                  cardFiles[0].entry.gameCode.c_str());
    } else {
      error = Fmt(Tr("This memory card holds %d files, none of them a Pokemon XD save."), static_cast<int>(cardFiles.size()));
    }
    return false;
  }
  xd::save::Save save;
  xd::save::ReadReport report;
  if (!xd::save::ReadSave(card->data, save, report, why)) {
    error = Fmt(Tr("The Pokemon XD save in this file can't be read: %s"), why.c_str());
    return false;
  }
  out.name = xd::save::ToUtf8(save.trainerName);
  out.cardFolder = fs::path(L"USA") / L"Card A";
  out.cardFileName = card->entry.makerCode + "-" + card->entry.gameCode + "-" + card->entry.fileName + ".gci";
  mc::CardFile put = *card;
  bool changed = kind != cs::Container::Gci;
  if (save.order == xd::save::ByteOrder::PC) {
    xd::save::ConvertReport conv;
    std::string convError;
    if (!xd::save::ConvertFile(put.data, xd::save::ByteOrder::GameCube, conv, convError)) {
      error = "The save is in the PC's byte order and can't be turned into the GameCube's: " + convError;
      return false;
    }
    changed = true;
    out.preview.notes.push_back("This save was in the PC port's byte order; it is stored in the GameCube's, as the "
                                "port's card holds it.");
  }
  if (changed) {
    out.gci = mc::MakeGci(put);
  }
  {
    const std::lock_guard<std::mutex> oneReader(sOneReader);
    SummaryOf(save, discPath, out.preview);
  }
  out.preview.where = Utf8(file) + (kind == cs::Container::Gci ? "" : std::string(" (") + cs::ContainerName(kind) + ")");
  return true;
}

std::vector<launcher::GameInfo::ImportOffer> FindImportable(const std::string& discPath) {
  namespace cs = engine::memcard::sources;
  const cs::Environment env = cs::RealEnvironment();
  std::vector<launcher::GameInfo::ImportOffer> offers;
  std::error_code ec;
  const fs::path own = fs::weakly_canonical(xd::CardRoot(), ec);
  int looked = 0;
  for (const cs::Found& f : cs::FindDolphinCardFiles(cs::DolphinUserFolders(env), "USA")) {
    if (++looked > 400) {
      break;
    }
    if (fs::file_size(f.path, ec) > (64u << 20)) {
      continue;
    }
    launcher::GameInfo::ImportOffer o;
    std::string error;
    if (!CheckImport(f.path, discPath, o.check, error)) {
      continue;
    }
    o.path = f.path;
    o.where = f.where;
    offers.push_back(std::move(o));
  }
  std::stable_sort(offers.begin(), offers.end(), [](const auto& a, const auto& b) {
    std::error_code e;
    return fs::last_write_time(a.path, e) > fs::last_write_time(b.path, e);
  });
  return offers;
}

void ReadSetup(const std::vector<uint8_t>& data, SaveSummary& out) {
  namespace ep = engine::playthrough;
  const xd::playthrough::SaveId id = xd::playthrough::SaveIdOf(data);
  if (!id.found) {
    return;
  }
  const fs::path file = xd::playthrough::SetupFileFor(id);
  std::error_code ec;
  if (!fs::exists(file, ec)) {
    out.setup = {{Tr("Setup"), Tr("Faithful, no mods (no setup file)")}};
    out.setupNotes.push_back(Tr("This save was started before the port kept setups (or was copied in from Dolphin or a "
                                "GameCube), so there is no record of how it began. It plays as Faithful with no mods."));
    return;
  }
  ep::Setup setup;
  std::string error;
  if (!ep::ReadFile(file, setup, error)) {
    out.setup = {{Tr("Setup"), Tr("can't be read")}};
    out.setupNotes.push_back(Fmt(Tr("The setup file next to this save can't be read (%s): %s"), error.c_str(), Utf8(file).c_str()));
    return;
  }
  out.setup.push_back({Tr("Game engine"),
                       setup.engineChanged.empty()
                           ? std::string(ep::EngineName(setup.engine))
                           : Fmt(Tr("%s (started as %s)"), ep::EngineName(setup.engine), ep::EngineName(setup.StartedAs()))});
  std::string world, looks;
  for (const ep::ModUse& m : setup.mods) {
    std::string& list = m.applies == engine::mods::Applies::NewPlaythrough ? world : looks;
    list += (list.empty() ? "" : ", ") + (m.name.empty() ? m.id : m.name) + (m.version.empty() ? "" : " " + m.version);
  }
  out.setup.push_back({Tr("Mods"), world.empty() ? std::string(Tr("none")) : world});
  if (!looks.empty()) {
    out.setup.push_back({Tr("Looks and sound"), looks});
  }
  if (!setup.randomizer.empty()) {
    const std::string& code = setup.seed;
    const std::string known = RandomizerStyle(code);
    const std::string style = known.empty() ? std::string(Tr("unknown style")) : known;
    out.setup.push_back({Tr("Randomizer"), style});
    out.setup.push_back({Tr("Randomizer code"), code.empty() ? std::string(Tr("(none kept)")) : code});
    if (!code.empty()) {
      out.shareCode = code;
      const fs::path spoiler = xd::CardRoot() / L"playthroughs" / L"randomizer" / fs::path(code) / L"spoiler.txt";
      if (fs::is_regular_file(spoiler, ec)) {
        out.spoilerFile = spoiler;
      }
    }
  } else {
    out.setup.push_back({Tr("Randomizer"), Tr("off")});
  }
  if (!setup.started.empty()) {
    out.setup.push_back({Tr("Started"), setup.portVersion.empty()
                                            ? setup.started
                                            : Fmt(Tr("%s (port %s)"), setup.started.c_str(), setup.portVersion.c_str())});
  }
  if (setup.chosenBy == "run") {
    out.setupNotes.push_back(Tr("Nobody chose this setup on the New Game screen: the game was started without it (the "
                                "Original preset, or a test), so it quietly kept the setup it was running with."));
  }
  for (const std::string& c : setup.engineChanged) {
    out.setupNotes.push_back(Fmt(Tr("Game engine: %s."), c.c_str()));
  }
  if (!setup.engineChanged.empty()) {
    out.setupNotes.push_back(Tr("Because the engine was switched, this playthrough is not Faithful from start to end."));
  }
  for (const std::string& c : setup.setupChanged) {
    out.setupNotes.push_back(Fmt(Tr("Carried on without its full setup: %s"), c.c_str()));
  }
}

void SummaryOf(const xd::save::Save& save, const std::string& discPath, SaveSummary& out) {
  namespace xs = xd::save;
  if (!discPath.empty() && discPath != sNamesDisc) {
    std::string nameError;
    sNamesDisc = sNames.Load(discPath, nameError) ? discPath : std::string();
  }
  const bool named = !sNamesDisc.empty() && sNames.Loaded();
  const auto species = [&](uint16_t id) {
    const std::string n = named ? sNames.Species(id) : std::string();
    return n.empty() ? Fmt(Tr("Pokemon #%d"), static_cast<int>(id)) : n;
  };
  const auto item = [&](uint16_t id) {
    if (id == 0) {
      return std::string("-");
    }
    const std::string n = named ? sNames.Item(id) : std::string();
    return n.empty() ? Fmt(Tr("item #%d"), static_cast<int>(id)) : n;
  };

  out.state = SaveSummary::State::Found;
  char id[16];
  std::snprintf(id, sizeof(id), "%05u", save.TrainerId());
  out.facts = {
      {Tr("Trainer"), TrainerText(xs::ToUtf8(save.trainerName), save.trainerGender)},
      {Tr("Trainer ID"), id},
      {Tr("Play time"), PlayTime(save.playTimeSeconds)},
      {Tr("Money"), "P" + Number(save.money)},
      {Tr("Poke Coupons"), Number(save.coupons)},
      {Tr("Saved"), Fmt(save.gameSaveCount == 1 ? Tr("%s time") : Tr("%s times"), Number(save.gameSaveCount).c_str())},
  };
  out.partyColumns = {Tr("Pokemon"), Tr("Nickname"), Tr("Level"), Tr("Held item"), ""};
  for (const xs::Pokemon& p : save.party) {
    if (p.IsEmpty()) {
      continue;
    }
    std::string notes;
    if (p.IsEgg()) {
      notes = Tr("Egg");
    }
    if (p.IsShiny()) {
      notes += std::string(notes.empty() ? "" : ", ") + Tr("Shiny");
    }
    if (const xs::ShadowEntry* s = save.ShadowOf(p)) {
      notes += std::string(notes.empty() ? "" : ", ") +
               (s->Purified() ? std::string(Tr("was a Shadow Pokemon"))
                              : Fmt(Tr("Shadow Pokemon (heart gauge %d)"), static_cast<int>(s->heartGauge)));
    }
    out.party.push_back({species(p.species), xs::ToUtf8(p.nickname), std::to_string(p.level), item(p.heldItem), notes});
  }
  if (!named) {
    out.notes.push_back(Tr("Pokemon and item names come from your disc: they appear once the disc is chosen and checked."));
  }
}

SaveSummary ReadSave(const std::string& discPath) {
  namespace mc = engine::memcard;
  namespace xs = xd::save;
  const std::lock_guard<std::mutex> oneReader(sOneReader);
  SaveSummary out;
  const fs::path folder = CardFolder();
  out.where = Utf8(folder);
  std::error_code ec;
  if (!fs::is_directory(folder, ec)) {
    out.message = "No save yet: the memory card is made the first time the game saves.";
    return out;
  }
  std::vector<uint8_t> data;
  fs::path saveFile;
  if (!FindSave(folder, data, saveFile)) {
    out.message = "No save yet: there is no Pokemon XD save on the memory card.";
    return out;
  }
  out.where = Utf8(saveFile);
  xs::Save save;
  xs::ReadReport report;
  std::string error;
  if (!xs::ReadSave(data, save, report, error)) {
    out.state = SaveSummary::State::Unreadable;
    out.message = "The save on the memory card can't be read: " + error;
    return out;
  }

  SummaryOf(save, discPath, out);
  ReadSetup(data, out);
  return out;
}

SaveSummary ReadSaveOf(const fs::path& cardRoot, const std::string& discPath) {
  const xd::CardRootScope scope(cardRoot);
  return ReadSave(discPath);
}

fs::path CardFolderOf(const fs::path& cardRoot, const std::string& card) {
  const fs::path a = cardRoot.empty() ? CardFolder() : (xd::CardRootScope(cardRoot), CardFolder());
  return card == "Card A" ? a : a.parent_path() / fs::path(std::u8string(card.begin(), card.end()));
}

struct DiscTexture {
  const char* id;
  const char* name;
  const char* archive;
  const char* entry;
  enum class Part { Whole, TopSquare, Trimmed, TopSquareTrimmed } part;
  bool animation;
};
std::vector<DiscTexture> DiscTextures() {
  return {
      {"shadow_lugia_face", Tr("Shadow Lugia (face)"), "poke_face.fsys", "face249dark", DiscTexture::Part::TopSquare, false},
      {"shadow_lugia_icon", Tr("Shadow Lugia (party icon)"), "poke_dance.fsys", "darklugia",
       DiscTexture::Part::TopSquareTrimmed, true},
      {"lugia_body", Tr("Lugia"), "poke_body.fsys", "body249", DiscTexture::Part::Trimmed, false},
      {"eevee_face", Tr("Eevee (face)"), "poke_face.fsys", "face133", DiscTexture::Part::TopSquare, false},
      {"eevee_icon", Tr("Eevee (party icon)"), "poke_dance.fsys", "eievui", DiscTexture::Part::TopSquareTrimmed, true},
      {"espeon_face", Tr("Espeon (face)"), "poke_face.fsys", "face196", DiscTexture::Part::TopSquare, false},
      {"umbreon_face", Tr("Umbreon (face)"), "poke_face.fsys", "face197", DiscTexture::Part::TopSquare, false},
      {"hero_face", Tr("The hero (face)"), "chara_small.fsys", "charaface_small_l_rinto_0101", DiscTexture::Part::Trimmed,
       false},
  };
}

launcher::icon::Image ReadDiscTexture(const DiscTexture& t) {
  namespace li = launcher::icon;
  li::Image img;
  size_t size = 0;
  uint8_t* archive = PortDisc_ReadOriginalFile(t.archive, &size);
  if (archive == nullptr) {
    return img;
  }
  FsysArchive fsys;
  if (Fsys_Open(archive, size, &fsys)) {
    const int index = t.entry != nullptr ? Fsys_Find(&fsys, t.entry) : 0;
    uint32_t n = 0;
    uint8_t* file = index >= 0 ? Fsys_Extract(&fsys, static_cast<uint32_t>(index), &n) : nullptr;
    if (file != nullptr) {
      uint32_t at = 0;
      if (t.animation && n >= 0x10) {
        at = static_cast<uint32_t>(file[0xC]) << 24 | file[0xD] << 16 | file[0xE] << 8 | file[0xF];
      }
      if (at < n) {
        img = li::DecodeGsTexture(file + at, n - at);
      }
      std::free(file);
    }
  }
  PortDisc_Free(archive);
  if (!img.Empty() && (t.part == DiscTexture::Part::TopSquare || t.part == DiscTexture::Part::TopSquareTrimmed)) {
    img = li::Crop(img, 0, 0, img.width, img.width);
  }
  if (!img.Empty() && (t.part == DiscTexture::Part::Trimmed || t.part == DiscTexture::Part::TopSquareTrimmed)) {
    img = li::Trim(img);
  }
  return img;
}

bool ReadDiscPictures(const std::string& discPath, std::vector<launcher::GameInfo::DiscPicture>& pictures,
                      launcher::icon::Image& banner, std::string& error) {
  char err[512] = {};
  if (!PortDisc_Open(discPath.c_str(), err, sizeof(err))) {
    error = err;
    return false;
  }
  size_t dolSize = 0;
  const uint8_t* dol = PortDisc_Dol(&dolSize);
  if (dol != nullptr && PortXD_LoadDolTables(dol, dolSize, err, sizeof(err))) {
    if (const uint8_t* t = PortXD_GetDolTable("_memcardIconImage")) {
      constexpr size_t kFrame = 32 * 32, kFrames = 8, kShown = 1;
      pictures.push_back({"save_icon", Tr("Save-file icon (Eevee)"),
                          launcher::icon::DecodeCI8(t + kFrame * kShown, t + kFrame * kFrames, 32, 32)});
    }
  } else {
    error = std::string("main.dol: ") + err;
  }
  const std::vector<DiscTexture> textures = DiscTextures();
  for (const DiscTexture& t : textures) {
    launcher::icon::Image img = ReadDiscTexture(t);
    if (!img.Empty()) {
      pictures.push_back({t.id, t.name, std::move(img)});
    }
  }
  size_t size = 0;
  if (uint8_t* bnr = PortDisc_ReadOriginalFile("opening.bnr", &size)) {
    constexpr size_t kPicture = 0x20, kBytes = 96 * 32 * 2;
    if (size >= kPicture + kBytes && std::memcmp(bnr, "BNR", 3) == 0) {
      banner = launcher::icon::DecodeRGB5A3(bnr + kPicture, 96, 32);
    }
    PortDisc_Free(bnr);
  }
  if (!banner.Empty()) {
    pictures.insert(pictures.begin(), {"banner", Tr("Disc banner"), banner, launcher::icon::Crop(banner, 46, 0, 39, 32)});
  }
  // The end credits' screen (staffroll.fsys, its only entry: 640x480 C8) has the game's logo large and sharp: the
  // logo is x 160-487, y 82-210 (the copyright lines below it are left out). "XD logo" (the default icon): its
  // "XD", x 352-478, on its own black ground (the words "Pokemon" and "Gale of Darkness" run into the X, so it
  // can't be cut out cleanly); "Full logo": the whole logo, its black ground made see-through. Also a background.
  const launcher::icon::Image credits =
      ReadDiscTexture({"credits", "", "staffroll.fsys", nullptr, DiscTexture::Part::Whole, false});
  if (credits.width == 640 && credits.height == 480) {
    namespace li = launcher::icon;
    const li::Image logo = li::Crop(credits, 160, 82, 328, 129);
    const li::Image xd = li::Crop(credits, 352, 82, 126, 124);
    pictures.insert(pictures.begin(), {"full_logo", Tr("Full logo"), li::AlphaFromBrightness(logo), xd});
    pictures.insert(pictures.begin() + 1, {"xd_logo", Tr("XD logo"), xd});
  }
  const launcher::icon::Image map =
      ReadDiscTexture({"orre_map", "", "world_map.fsys", "menu_039", DiscTexture::Part::Whole, false});
  if (!map.Empty()) {
    pictures.push_back({"bg_orre_map", Tr("The map of Orre"), map, {}, true});
  }
  const launcher::icon::Image balls =
      ReadDiscTexture({"menu", "", "title.fsys", "menu_042", DiscTexture::Part::Whole, false});
  if (!balls.Empty()) {
    pictures.push_back({"bg_menu", Tr("The menus' Poke Ball pattern"), balls, {}, true});
  }
  return !pictures.empty();
}

namespace sys = launcher::sys;

fs::path ProgramFolder() { return sys::ProgramFolder(); }

void SetUpUpdates(launcher::GameInfo::Updates& u, const fs::path& own) {
  u.version = engine::update::FolderVersion(own);
  if (u.version.empty()) {
    u.version = XD_PORT_VERSION;
  }
  u.product = "pokemon-xd-pc";
  u.feedUrl = XD_UPDATE_FEED_URL;
  u.publicKey = XD_UPDATE_PUBLIC_KEY;
  const char* feed = std::getenv("XD_UPDATE_FEED");
  const char* key = std::getenv("XD_UPDATE_KEY");
  if (feed != nullptr && key != nullptr) {
    const std::string f = feed;
    if (f.rfind("file:///", 0) == 0 || f.rfind("http://127.0.0.1:", 0) == 0 || f.rfind("http://127.0.0.1/", 0) == 0 ||
        f.rfind("http://localhost:", 0) == 0 || f.rfind("http://localhost/", 0) == 0) {
      u.feedUrl = f;
      u.publicKey = key;
      std::fprintf(stderr, "[launcher] testing: update feed %s\n", feed);
    } else {
      std::fprintf(stderr, "[launcher] XD_UPDATE_FEED ignored: only a feed on this PC can be used for tests\n");
    }
  }
  u.stagingFolder = xd::UserFolder() / L"updates";
#if defined(_WIN32)
  u.starterName = "Pokemon XD Launcher.exe|Pokemon XD.exe";
  u.requiredFiles = {"xd_launcher.exe", "xd.exe"};
#else
  u.starterName = "Pokemon XD Launcher.sh|Pokemon XD.sh";
  u.requiredFiles = {"xd_launcher", "xd"};
#endif
}

fs::path sGameProgram;

sys::Process* StartHiddenGame(const std::vector<std::string>& extra, const std::string& discPath, bool lowPriority) {
  sys::StartRequest request;
  request.program = sGameProgram;
  request.arguments = {discPath};
  request.folder = sGameProgram.parent_path();
  request.hidden = true;
  request.lowPriority = lowPriority;
  request.ownEnvironment = true;
  request.environmentWithout = {"XD_RENDER_", "XD_INPUT_SCRIPT=", "XD_REPLAY", "XD_PREPARE_SHADERS"};
  request.environmentAdd = extra;
  request.killTree = true;
#if defined(_WIN32)
  request.environmentAdd.push_back("XD_PARENT_PID=" + std::to_string(static_cast<unsigned long>(GetCurrentProcessId())));
#else
  request.environmentAdd.push_back("XD_PARENT_PID=" + std::to_string(static_cast<long>(getpid())));
#endif
  std::string why;
  sys::Process* process = sys::StartProgram(request, why);
  if (process == nullptr) {
    std::fprintf(stderr, "[launcher] the game program couldn't be started: %s\n", why.c_str());
  }
  return process;
}

std::string Fnv64File(const fs::path& file) {
  std::ifstream in(file, std::ios::binary);
  uint64_t h = 1469598103934665603ull;
  char buf[65536];
  while (in.read(buf, sizeof buf) || in.gcount() > 0) {
    for (std::streamsize i = 0; i < in.gcount(); ++i) {
      h = (h ^ static_cast<unsigned char>(buf[i])) * 1099511628211ull;
    }
  }
  char out[20];
  std::snprintf(out, sizeof out, "%016llx", static_cast<unsigned long long>(h));
  return out;
}

std::string GraphicsAdapters() {
#if !defined(_WIN32)
  return "adapters not listed on this system";
#else
  std::string out;
  IDXGIFactory1* factory = nullptr;
  if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory)))) {
    return "unknown";
  }
  IDXGIAdapter1* adapter = nullptr;
  for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
    DXGI_ADAPTER_DESC1 desc{};
    adapter->GetDesc1(&desc);
    LARGE_INTEGER driver{};
    adapter->CheckInterfaceSupport(__uuidof(IDXGIDevice), &driver);
    for (const wchar_t* c = desc.Description; *c != L'\0'; ++c) {
      out += (*c >= 0x20 && *c < 0x7F) ? static_cast<char>(*c) : '?';
    }
    char line[96];
    std::snprintf(line, sizeof line, " %04x:%04x driver %u.%u.%u.%u; ", desc.VendorId, desc.DeviceId,
                  HIWORD(driver.HighPart), LOWORD(driver.HighPart), HIWORD(driver.LowPart), LOWORD(driver.LowPart));
    out += line;
    adapter->Release();
  }
  factory->Release();
  return out;
#endif
}

fs::path ShaderCacheFolder() {
  if (const fs::path dir = sys::EnvPath("XD_CACHE_DIR"); !dir.empty()) {
    return dir;
  }
  char* pref = SDL_GetPrefPath(nullptr, "Pokemon XD");
  if (pref == nullptr) {
    return {};
  }
  const std::string utf8 = pref;
  SDL_free(pref);
  return fs::path(std::u8string(utf8.begin(), utf8.end()));
}

bool PrepareShaders(const std::string& discPath, const std::function<void(uint32_t, uint32_t)>& progress,
                    const std::function<bool()>& stop, std::string& error) {
  std::error_code ec;
  const fs::path list = sGameProgram.parent_path() / L"initial_pipeline_cache.db";
  if (!fs::exists(list, ec) || !fs::exists(sGameProgram, ec)) {
    return true;
  }
  const fs::path cache = ShaderCacheFolder();
  if (cache.empty()) {
    error = Tr("no folder for the shader caches");
    return false;
  }
  fs::create_directories(cache, ec);
  const fs::path stamp = cache / L"shaders_prepared.txt";
  std::string key = "1|" XD_PORT_VERSION "|list " + Fnv64File(list) + "|" + GraphicsAdapters();
  for (char& c : key) {
    if (static_cast<unsigned char>(c) < 0x20 || static_cast<unsigned char>(c) >= 0x7F) c = '?';
  }
  {
    std::ifstream in(stamp);
    std::string have;
    std::getline(in, have);
    if (have == key) {
      return true;
    }
  }
  std::fprintf(stderr, "[launcher] shaders: preparing for this PC (%s)\n", key.c_str());
  fs::remove(stamp, ec);
  const fs::path progressFile = cache / L"shaders_prepared.txt.progress";
  fs::remove(progressFile, ec);
  progress(0, 0);
  sys::Process* game =
      StartHiddenGame({"XD_PREPARE_SHADERS=" + Utf8(stamp), "XD_PREPARE_SHADERS_KEY=" + key}, discPath, false);
  if (game == nullptr) {
    error = Tr("could not start the game program");
    return false;
  }
  for (int waited = 0; !sys::Ended(game, 100); waited += 100) {
    if (std::ifstream in(progressFile); in) {
      uint32_t done = 0, total = 0;
      if (in >> done >> total) progress(done, total);
    }
    if (stop() || waited > 20 * 60 * 1000) {
      sys::Stop(game);
      sys::Close(game);
      error = stop() ? Tr("stopped (the launcher closed)") : Tr("took too long");
      return false;
    }
  }
  const int code = sys::ExitCode(game);
  sys::Close(game);
  if (std::ifstream in(progressFile); in) {
    uint32_t done = 0, total = 0;
    if (in >> done >> total) progress(done, total);
  }
  fs::remove(progressFile, ec);
  std::string have;
  if (std::ifstream in(stamp); in) std::getline(in, have);
  if (code != 0 || have != key) {
    error = Fmt(Tr("the game's shader preparation ended with code %d (see its log in %s)"), code,
                Utf8(xd::UserFolder() / L"logs").c_str());
    return false;
  }
  return true;
}

bool RenderTrainerPortraits(const std::string& discPath, const std::string& id, const std::function<bool()>& stop, std::string& error) {
  const fs::path folder = xd::SettingsFolder() / L"trainer_card" / L"icons";
  const fs::path done = folder / (L"portraits_" + fs::path(id).wstring() + L".done");
  std::error_code ec;
  if (fs::exists(done, ec)) return true;
  fs::create_directories(folder, ec);
  std::vector<int> rows;
  for (int r = 1; r <= 66; r++) {
    if (r != 0x2A && r != 0x40 && r != 0x42) rows.push_back(r);
  }
  unsigned long worst = 0;
  for (size_t i = 0; i < rows.size(); i += 20) {
    std::string items;
    for (size_t k = i; k < rows.size() && k < i + 20; k++) {
      items += (items.empty() ? "" : ",") + std::string("trainer:") + std::to_string(rows[k]) + ":xd_" + std::to_string(rows[k]) + ":anim2";
    }
    sys::Process* game = StartHiddenGame({"XD_RENDER_ICON=" + items, "XD_RENDER_ICON_DIR=" + folder.string(), "XD_RENDER_ICON_SIZE=512"}, discPath, true);
    if (game == nullptr) { error = Tr("could not start the game program"); return false; }
    for (int waited = 0; !sys::Ended(game, 250); waited += 250) {
      if (stop() || waited > 5 * 60 * 1000) { sys::Stop(game); sys::Close(game); error = Tr("the trainer pictures took too long or were stopped"); return false; }
    }
    const unsigned long code = static_cast<unsigned long>(static_cast<unsigned int>(sys::ExitCode(game)));
    sys::Close(game);
    if (code > worst) worst = code;
  }
  if (worst != 0 && worst != 2 && worst != 3) {
    error = Tr("the trainer pictures were interrupted");
    return false;
  }
  if (FILE* f = sys::OpenFile(done, "w")) {
    std::fprintf(f, "trainer portraits: worst exit code %lu\n", worst);
    std::fclose(f);
  }
  return true;
}

bool RenderOfferPictures(const std::string& discPath, const std::string& id, const std::function<void()>& starting,
                         const std::function<bool()>& stop, std::string& error) {
  std::error_code ec;
  const fs::path pack = xd::SettingsFolder() / L"extra_assets" / L"pbr_gen4" / L"gen4.json";
  if (!fs::exists(pack, ec) || !fs::exists(sGameProgram, ec)) return true;
  const fs::path folder = xd::UserFolder() / L"icons" / L"offer";
  const fs::path done = folder / (L"render_offer_" + fs::path(id).wstring() + L".done");
  const std::string stamp = std::to_string(static_cast<long long>(fs::last_write_time(pack, ec).time_since_epoch().count()));
  std::string have;
  if (std::ifstream in(done); in) std::getline(in, have);
  if (have == stamp) return true;
  fs::create_directories(folder, ec);
  starting();
  sys::Process* game = StartHiddenGame({"XD_RENDER_ICON=offer", "XD_CHALLENGES=gen4_rules"}, discPath, true);
  if (game == nullptr) {
    error = Tr("could not start the game program for the Eevee offer pictures");
    return false;
  }
  for (int waited = 0; !sys::Ended(game, 250); waited += 250) {
    if (stop() || waited > 3 * 60 * 1000) {
      sys::Stop(game);
      sys::Close(game);
      error = stop() ? Tr("stopped (the launcher closed, or Play was pressed)") : Tr("the game took too long for the Eevee offer pictures");
      return false;
    }
  }
  const int code = sys::ExitCode(game);
  sys::Close(game);
  if (code != 0) {
    {
      char eb[160];
      std::snprintf(eb, sizeof eb, Tr("the game's render mode for the Eevee offer ended with code %d"), code);
      error = eb;
    }
    return false;
  }
  if (FILE* f = sys::OpenFile(done, "w")) {
    std::fprintf(f, "%s\n", stamp.c_str());
    std::fclose(f);
  }
  return true;
}

bool RenderPictures(const std::string& discPath, const std::string& discId, const std::function<void()>& starting,
                    const std::function<bool()>& stop, std::string& error) {
  const fs::path folder = xd::UserFolder() / L"icons" / L"rendered";
  std::string id;
  for (char c : discId) {
    if (std::isalnum(static_cast<unsigned char>(c))) id += c;
  }
  const fs::path done = folder / (L"render_" + fs::path(id).wstring() + L".done");
  std::error_code ec;
  {
    const char* kRendererVersion = "2";
    const fs::path versionFile = folder / L"renderer_version.txt";
    std::string have;
    if (std::ifstream in(versionFile); in) std::getline(in, have);
    if (have != kRendererVersion) {
      fs::create_directories(folder, ec);
      fs::remove(done, ec);
      fs::remove(xd::SettingsFolder() / L"trainer_card" / L"icons" / (L"portraits_" + fs::path(id).wstring() + L".done"), ec);
      fs::remove(xd::UserFolder() / L"icons" / L"offer" / (L"render_offer_" + fs::path(id).wstring() + L".done"), ec);
      if (FILE* f = sys::OpenFile(versionFile, "w")) {
        std::fprintf(f, "%s\n", kRendererVersion);
        std::fclose(f);
      }
    }
  }
  if (fs::exists(done, ec)) {
    return RenderTrainerPortraits(discPath, id, stop, error) && RenderOfferPictures(discPath, id, starting, stop, error);
  }
  if (!fs::exists(sGameProgram, ec)) {
    error = Fmt(Tr("the game program isn't there: %s"), sGameProgram.string().c_str());
    return false;
  }
  fs::create_directories(folder, ec);
  starting();
  sys::Process* game = StartHiddenGame({"XD_RENDER_ICON=all"}, discPath, true);
  if (game == nullptr) {
    error = Tr("could not start the game program");
    return false;
  }
  for (int waited = 0; !sys::Ended(game, 250); waited += 250) {
    if (stop() || waited > 5 * 60 * 1000) {
      sys::Stop(game);
      sys::Close(game);
      error = stop() ? Tr("stopped (the launcher closed, or Play was pressed)") : Tr("the game took too long");
      return false;
    }
  }
  const unsigned long code = static_cast<unsigned long>(static_cast<unsigned int>(sys::ExitCode(game)));
  sys::Close(game);
  const fs::path tries = folder / (L"render_" + fs::path(id).wstring() + L".tries");
  if (code != 0 && code != 2 && code != 3) {
    int count = 0;
    if (std::ifstream in(tries); in) in >> count;
    if (count < 1) {
      if (FILE* f = sys::OpenFile(tries, "w")) {
        std::fprintf(f, "%d\n", count + 1);
        std::fclose(f);
      }
      error = Fmt(Tr("the game's render mode ended with code %lu (render_icon.txt in %s)"), code, folder.string().c_str());
      return false;
    }
  }
  fs::remove(tries, ec);
  if (FILE* f = sys::OpenFile(done, "w")) {
    std::fprintf(f, "xd.exe XD_RENDER_ICON=all exit code %lu (0: all made; see render_icon.txt)\n", code);
    std::fclose(f);
  }
  if (code != 0) {
    error = Fmt(Tr("the game's render mode ended with code %lu (render_icon.txt in %s)"), code, folder.string().c_str());
    return false;
  }
  return RenderTrainerPortraits(discPath, id, stop, error) && RenderOfferPictures(discPath, id, starting, stop, error);
}

}

int main(int argc, char** argv) {
#if defined(__APPLE__)
  if (const char* none = std::getenv("XD_NO_LAUNCHER"); none != nullptr && std::strcmp(none, "1") == 0) {
    const std::string game = (sys::ProgramFolder() / "xd").string();
    std::vector<char*> args{const_cast<char*>(game.c_str())};
    for (int i = 1; i < argc; i++) {
      if (std::strncmp(argv[i], "-psn_", 5) != 0) {
        args.push_back(argv[i]);
      }
    }
    args.push_back(nullptr);
    execv(game.c_str(), args.data());
    std::fprintf(stderr, "[launcher] XD_NO_LAUNCHER=1 but %s can't be started: %s\n", game.c_str(), std::strerror(errno));
    return 1;
  }
#else
  (void)argc;
  (void)argv;
#endif
  const auto Tr = [](const char* english) { return english; };
  launcher::GameInfo game;
  game.title = "Pokemon XD";
  game.windowTitle = "Pok\xC3\xA9mon XD";
  game.subtitle = "Gale of Darkness - PC";
  game.gameId = xd_settings::kGameId;
  game.textureGameId = "GXXE01";
  game.discHelp =
      Tr("The game needs your own copy of Pokemon XD: Gale of Darkness (USA version, game ID GXXE01), saved as a disc "
         "image file (.iso, .rvz, .gcm, ...). Nothing from the game comes with this program.");
  const fs::path settings = xd::SettingsFolder();
  game.settingsFolder = settings;
  engine::mods::SetTexturePackSource(settings / "textures", "GXXE01");
  game.modsFolder = settings / L"mods";
  game.modListFile = settings / L"mods.ini";
  game.playerFilesReadOnly = xd::PlayerFilesReadOnly();
  game.readOnlyNote = xd::kPlayerFilesReadOnlyNote;
  const fs::path own = ProgramFolder();
  std::error_code ec;
  const std::string xdName = sys::ProgramName("xd");
  game.program = own / xdName;
  if (!fs::exists(game.program, ec) && fs::exists(own / L"game" / xdName, ec)) {
    game.program = own / L"game" / xdName;
  }
#if defined(_WIN32)
  const wchar_t* const kStarters[] = {L"Pokemon XD Launcher.exe", L"Pokemon XD.exe"};
#else
  const char* const kStarters[] = {"Pokemon XD Launcher.sh", "Pokemon XD.sh"};
#endif
  game.shortcutTarget = own / kStarters[0];
  for (const auto* starter : kStarters) {
    if (fs::exists(own.parent_path() / starter, ec) &&
        engine::update::IsGameFolderName(own.filename().string())) {
      game.shortcutTarget = own.parent_path() / starter;
      break;
    }
    if (fs::exists(own / starter, ec)) {
      game.shortcutTarget = own / starter;
      break;
    }
  }
  if (!fs::exists(game.shortcutTarget, ec)) {
    game.shortcutTarget = sys::ProgramFile();
  }
  game.iconFolder = xd::UserFolder() / L"icons";
  game.backgroundFolder = xd::UserFolder() / L"backgrounds";
  game.reportsFolder = xd::UserFolder() / L"reports";
  game.logsFolder = xd::UserFolder() / L"logs";
  game.recordingsFolder = xd::UserFolder() / L"recordings";
  game.saveBackupsFolder = engine::memcard::backups::FolderFor(xd::CardRoot());
  game.cardFolder = [](const std::string& card) { return CardFolderOf(fs::path(), card); };
  if (xd::SaveSlotsInUse()) {
    game.saves.dataFolder = xd::UserFolder();
    game.saves.changed = [] { xd::ForgetActiveSave(); };
    game.saves.row = SaveRowOf;
    game.saves.read = ReadSaveOf;
    game.saves.cardFolder = CardFolderOf;
    game.saves.saveFile = SaveFileOf;
    game.saves.checkImport = CheckImport;
    game.saves.findImportable = FindImportable;
    game.saves.tradeOpen = xd::gbapage::Open;
    game.saves.tradeDo = xd::gbapage::Trade;
    game.saves.monsOpen = xd::colosseumpage::Open;
    game.saves.monsDo = xd::colosseumpage::Import;
    game.saves.exportName = "01-GXXE-PokemonXD.gci";
    if (const fs::path bin = sys::EnvPath("XD_SAVE_BIN_DIR"); !bin.empty()) {
      game.saves.binFolder = bin;
    }
  }
  game.appId = "PokemonXD.PCPort";
  game.readDiscPictures = ReadDiscPictures;
  sGameProgram = game.program;
  if (const char* render = std::getenv("XD_LAUNCHER_RENDER"); render == nullptr || std::strcmp(render, "0") != 0) {
    game.renderPictures = RenderPictures;
  }
  {
    const char* shaders = std::getenv("XD_LAUNCHER_SHADERS");
    const char* render = std::getenv("XD_LAUNCHER_RENDER");
    const bool off = shaders != nullptr ? std::strcmp(shaders, "0") == 0 : (render != nullptr && std::strcmp(render, "0") == 0);
    if (!off) {
      game.prepareShaders = PrepareShaders;
    }
  }
  game.ownIcons = {{"original", Tr("XD (the program's own)"), 1}, {"original_shadow", Tr("XD, shadow aura"), 2},
                   {"original_minimal", Tr("XD, minimal"), 3}, {"original_swirl", Tr("XD, shadow swirl"), 4},
                   {"original_light", Tr("XD, light"), 5}};
  if (const fs::path dir = sys::EnvPath("XD_SHORTCUT_DIR"); !dir.empty()) {
    game.shortcutFolder = dir;
  }
  if (const char* script = std::getenv("XD_LAUNCHER_SCRIPT")) {
    game.script = script;
  }
  const char* offscreen = std::getenv("XD_WINDOW_OFFSCREEN");
  game.offscreen = offscreen != nullptr && offscreen[0] == '1';
  game.registerSettings = [](engine::settings::Registry& r) { xd_settings::Register(r); };
  game.checkDisc = CheckDisc;
  game.readSave = ReadSave;
  game.modLooks = xd::playthrough::LooksOrSound;
  game.extra.sources = xd::ExtraSources();
  game.extra.identifyDisc = IdentifyDisc;
  game.extra.otherVersion = [](const std::string& id) { return id.rfind("GXX", 0) == 0 && id != "GXXE01"; };
  game.languages.setting = xd::languages::kSetting;
  game.languages.noPackHint = xd::languages::kNoPackHint;
  game.languages.whenHint = xd::languages::kWhenHint;
  game.languages.offered = [] {
    static std::vector<std::pair<std::string, std::string>> cached;
    static std::chrono::steady_clock::time_point at{};
    const auto now = std::chrono::steady_clock::now();
    if (at == std::chrono::steady_clock::time_point{} || now - at > std::chrono::seconds(1)) {
      cached.clear();
      for (const xd::languages::Language* l : xd::languages::Available(xd::languages::ReadPack(xd::SettingsFolder()))) {
        cached.emplace_back(l->id, l->name);
      }
      at = now;
    }
    return cached;
  };
  SetUpUpdates(game.updates, own);
  return launcher::Run(game);
}
