#include <launcher.h>

#include "disc_verify.h"
#include "dol_tables.h"
#include "xd_settings.h"
#include "xd_user_folder.h"
#include "xd_playthrough_files.h"

#include <gc_memcard.h>
#include <gs_formats.h>
#include <port_disc.h>
#include <save_backups.h>
#include <update.h>
#include <xd_save.h>
#include <xd_save_names.h>
#include <playthrough.h>
#include <randomizer.h>
#include <xd_randomizer.h>

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_main.h>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <dxgi.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <iterator>
#include <mutex>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;
using launcher::DiscVerdict;
using launcher::SaveSummary;

std::string Utf8(const fs::path& p) {
  const std::u8string u8 = p.u8string();
  return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
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

xd::save::GameNames sNames;
std::string sNamesDisc;

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
  std::snprintf(buf, sizeof(buf), "%lld hours %02lld minutes", s / 3600, (s / 60) % 60);
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
    row.who = "empty";
    row.setup = "no save yet: the game offers New Game";
    return row;
  }
  xd::save::Save save;
  xd::save::ReadReport report;
  std::string error;
  if (!xd::save::ReadSave(data, save, report, error)) {
    row.who = "a save that can't be read";
    return row;
  }
  row.found = true;
  row.who = xd::save::ToUtf8(save.trainerName) + (save.trainerGender == 0 ? " (boy)" : " (girl)");
  row.time = PlayTime(save.playTimeSeconds);
  const xd::playthrough::SaveId id = xd::playthrough::SaveIdOf(data);
  std::error_code ec;
  ep::Setup setup;
  if (!id.found || !fs::exists(xd::playthrough::SetupFileFor(id), ec)) {
    row.setup = "Faithful \xC2\xB7 Vanilla (no setup file)";
  } else if (!ep::ReadFile(xd::playthrough::SetupFileFor(id), setup, error)) {
    row.setup = "its setup file can't be read";
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

bool CheckImport(const fs::path& file, launcher::GameInfo::ImportCheck& out, std::string& error) {
  namespace mc = engine::memcard;
  std::vector<uint8_t> bytes;
  if (!ReadBytes(file, bytes)) {
    error = "The file can't be read: " + Utf8(file);
    return false;
  }
  mc::CardFile card;
  std::string why;
  if (!mc::ReadGci(bytes, card, why)) {
    error = "This is not a GameCube save file (.gci): " + why + ".";
    return false;
  }
  if (card.entry.fileName != "PokemonXD" || card.entry.gameCode.compare(0, 3, "GXX") != 0) {
    error = "This save file is not a Pokemon XD save (it is \"" + card.entry.fileName + "\" of game " +
            card.entry.gameCode + ").";
    return false;
  }
  if (card.entry.gameCode != "GXXE") {
    error = "This Pokemon XD save is from another region's game (" + card.entry.gameCode +
            "); the game here is the USA version (GXXE).";
    return false;
  }
  xd::save::Save save;
  xd::save::ReadReport report;
  if (!xd::save::ReadSave(card.data, save, report, why)) {
    error = "The Pokemon XD save in this file can't be read: " + why;
    return false;
  }
  out.name = xd::save::ToUtf8(save.trainerName);
  out.cardFolder = fs::path(L"USA") / L"Card A";
  out.cardFileName = card.entry.makerCode + "-" + card.entry.gameCode + "-" + card.entry.fileName + ".gci";
  return true;
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
    out.setup = {{"Setup", "Faithful, no mods (no setup file)"}};
    out.setupNotes.push_back("This save was started before the port kept setups (or was copied in from Dolphin or a "
                             "GameCube), so there is no record of how it began. It plays as Faithful with no mods.");
    return;
  }
  ep::Setup setup;
  std::string error;
  if (!ep::ReadFile(file, setup, error)) {
    out.setup = {{"Setup", "can't be read"}};
    out.setupNotes.push_back("The setup file next to this save can't be read (" + error + "): " + Utf8(file));
    return;
  }
  out.setup.push_back({"Game engine", std::string(ep::EngineName(setup.engine)) +
                                          (setup.engineChanged.empty() ? "" : std::string(" (started as ") + ep::EngineName(setup.StartedAs()) + ")")});
  std::string world, looks;
  for (const ep::ModUse& m : setup.mods) {
    std::string& list = m.applies == engine::mods::Applies::NewPlaythrough ? world : looks;
    list += (list.empty() ? "" : ", ") + (m.name.empty() ? m.id : m.name) + (m.version.empty() ? "" : " " + m.version);
  }
  out.setup.push_back({"Mods", world.empty() ? std::string("none") : world});
  if (!looks.empty()) {
    out.setup.push_back({"Looks and sound", looks});
  }
  if (!setup.randomizer.empty()) {
    const std::string& code = setup.seed;
    const std::string known = RandomizerStyle(code);
    const std::string style = known.empty() ? std::string("unknown style") : known;
    out.setup.push_back({"Randomizer", style});
    out.setup.push_back({"Randomizer code", code.empty() ? std::string("(none kept)") : code});
    if (!code.empty()) {
      out.shareCode = code;
      const fs::path spoiler = xd::CardRoot() / L"playthroughs" / L"randomizer" / fs::path(code) / L"spoiler.txt";
      if (fs::is_regular_file(spoiler, ec)) {
        out.spoilerFile = spoiler;
      }
    }
  } else {
    out.setup.push_back({"Randomizer", "off"});
  }
  if (!setup.started.empty()) {
    out.setup.push_back({"Started", setup.started + (setup.portVersion.empty() ? "" : " (port " + setup.portVersion + ")")});
  }
  if (setup.chosenBy == "run") {
    out.setupNotes.push_back("Nobody chose this setup on the New Game screen: the game was started without it (the "
                             "Original preset, or a test), so it quietly kept the setup it was running with.");
  }
  for (const std::string& c : setup.engineChanged) {
    out.setupNotes.push_back("Game engine: " + c + ".");
  }
  if (!setup.engineChanged.empty()) {
    out.setupNotes.push_back("Because the engine was switched, this playthrough is not Faithful from start to end.");
  }
  for (const std::string& c : setup.setupChanged) {
    out.setupNotes.push_back("Carried on without its full setup: " + c);
  }
}

SaveSummary ReadSave(const std::string& discPath) {
  namespace mc = engine::memcard;
  namespace xs = xd::save;
  static std::mutex sOneReader;
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

  if (!discPath.empty() && discPath != sNamesDisc) {
    std::string nameError;
    sNamesDisc = sNames.Load(discPath, nameError) ? discPath : std::string();
  }
  const bool named = !sNamesDisc.empty() && sNames.Loaded();
  const auto species = [&](uint16_t id) {
    const std::string n = named ? sNames.Species(id) : std::string();
    return n.empty() ? "Pokemon #" + std::to_string(id) : n;
  };
  const auto item = [&](uint16_t id) {
    if (id == 0) {
      return std::string("-");
    }
    const std::string n = named ? sNames.Item(id) : std::string();
    return n.empty() ? "item #" + std::to_string(id) : n;
  };

  out.state = SaveSummary::State::Found;
  char id[16];
  std::snprintf(id, sizeof(id), "%05u", save.TrainerId());
  out.facts = {
      {"Trainer", xs::ToUtf8(save.trainerName) + (save.trainerGender == 0 ? " (boy)" : " (girl)")},
      {"Trainer ID", id},
      {"Play time", PlayTime(save.playTimeSeconds)},
      {"Money", "P" + Number(save.money)},
      {"Poke Coupons", Number(save.coupons)},
      {"Saved", Number(save.gameSaveCount) + (save.gameSaveCount == 1 ? " time" : " times")},
  };
  out.partyColumns = {"Pokemon", "Nickname", "Level", "Held item", ""};
  for (const xs::Pokemon& p : save.party) {
    if (p.IsEmpty()) {
      continue;
    }
    std::string notes;
    if (p.IsEgg()) {
      notes = "Egg";
    }
    if (p.IsShiny()) {
      notes += std::string(notes.empty() ? "" : ", ") + "Shiny";
    }
    if (const xs::ShadowEntry* s = save.ShadowOf(p)) {
      notes += std::string(notes.empty() ? "" : ", ") +
               (s->Purified() ? std::string("was a Shadow Pokemon") : "Shadow Pokemon (heart gauge " + std::to_string(s->heartGauge) + ")");
    }
    out.party.push_back({species(p.species), xs::ToUtf8(p.nickname), std::to_string(p.level), item(p.heldItem), notes});
  }
  if (!named) {
    out.notes.push_back("Pokemon and item names come from your disc: they appear once the disc is chosen and checked.");
  }
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
constexpr DiscTexture kDiscTextures[] = {
    {"shadow_lugia_face", "Shadow Lugia (face)", "poke_face.fsys", "face249dark", DiscTexture::Part::TopSquare, false},
    {"shadow_lugia_icon", "Shadow Lugia (party icon)", "poke_dance.fsys", "darklugia",
     DiscTexture::Part::TopSquareTrimmed, true},
    {"lugia_body", "Lugia", "poke_body.fsys", "body249", DiscTexture::Part::Trimmed, false},
    {"eevee_face", "Eevee (face)", "poke_face.fsys", "face133", DiscTexture::Part::TopSquare, false},
    {"eevee_icon", "Eevee (party icon)", "poke_dance.fsys", "eievui", DiscTexture::Part::TopSquareTrimmed, true},
    {"espeon_face", "Espeon (face)", "poke_face.fsys", "face196", DiscTexture::Part::TopSquare, false},
    {"umbreon_face", "Umbreon (face)", "poke_face.fsys", "face197", DiscTexture::Part::TopSquare, false},
    {"hero_face", "The hero (face)", "chara_small.fsys", "charaface_small_l_rinto_0101", DiscTexture::Part::Trimmed,
     false},
};

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
      pictures.push_back({"save_icon", "Save-file icon (Eevee)",
                          launcher::icon::DecodeCI8(t + kFrame * kShown, t + kFrame * kFrames, 32, 32)});
    }
  } else {
    error = std::string("main.dol: ") + err;
  }
  for (const DiscTexture& t : kDiscTextures) {
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
    pictures.insert(pictures.begin(), {"banner", "Disc banner", banner, launcher::icon::Crop(banner, 46, 0, 39, 32)});
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
    pictures.insert(pictures.begin(), {"full_logo", "Full logo", li::AlphaFromBrightness(logo), xd});
    pictures.insert(pictures.begin() + 1, {"xd_logo", "XD logo", xd});
  }
  const launcher::icon::Image map =
      ReadDiscTexture({"orre_map", "", "world_map.fsys", "menu_039", DiscTexture::Part::Whole, false});
  if (!map.Empty()) {
    pictures.push_back({"bg_orre_map", "The map of Orre", map, {}, true});
  }
  const launcher::icon::Image balls =
      ReadDiscTexture({"menu", "", "title.fsys", "menu_042", DiscTexture::Part::Whole, false});
  if (!balls.Empty()) {
    pictures.push_back({"bg_menu", "The menus' Poke Ball pattern", balls, {}, true});
  }
  return !pictures.empty();
}

fs::path ProgramFolder() {
  wchar_t buf[MAX_PATH * 4];
  const DWORD n = GetModuleFileNameW(nullptr, buf, static_cast<DWORD>(std::size(buf)));
  return fs::path(std::wstring(buf, n)).parent_path();
}

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
  u.starterName = "Pokemon XD.exe";
  u.requiredFiles = {"xd_launcher.exe", "xd.exe"};
}

fs::path sGameProgram;

bool StartHiddenGame(const std::vector<std::wstring>& extra, const std::string& discPath, DWORD priority,
                     PROCESS_INFORMATION& pi) {
  std::wstring env;
  if (wchar_t* block = GetEnvironmentStringsW()) {
    for (const wchar_t* p = block; *p != L'\0'; p += wcslen(p) + 1) {
      if (_wcsnicmp(p, L"XD_RENDER_", 10) != 0 && _wcsnicmp(p, L"XD_INPUT_SCRIPT=", 16) != 0 &&
          _wcsnicmp(p, L"XD_REPLAY", 9) != 0 && _wcsnicmp(p, L"XD_PREPARE_SHADERS", 18) != 0) {
        env.append(p).push_back(L'\0');
      }
    }
    FreeEnvironmentStringsW(block);
  }
  for (const auto& e : extra) {
    env.append(e).push_back(L'\0');
  }
  env.push_back(L'\0');
  std::wstring disc(discPath.size() + 1, L'\0');
  disc.resize(std::max(0, MultiByteToWideChar(CP_UTF8, 0, discPath.c_str(), -1, disc.data(), (int)disc.size()) - 1));
  std::wstring command = L"\"" + sGameProgram.wstring() + L"\" \"" + disc + L"\"";
  STARTUPINFOW si{};
  si.cb = sizeof si;
  pi = {};
  return CreateProcessW(sGameProgram.c_str(), command.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT | priority, env.data(),
                        sGameProgram.parent_path().c_str(), &si, &pi) != 0;
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
}

fs::path ShaderCacheFolder() {
  if (const wchar_t* dir = _wgetenv(L"XD_CACHE_DIR"); dir != nullptr && dir[0] != L'\0') {
    return fs::path(dir);
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
    error = "no folder for the shader caches";
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
  PROCESS_INFORMATION pi{};
  if (!StartHiddenGame({L"XD_PREPARE_SHADERS=" + stamp.wstring(), L"XD_PREPARE_SHADERS_KEY=" + std::wstring(key.begin(), key.end())},
                       discPath, NORMAL_PRIORITY_CLASS, pi)) {
    error = "could not start the game program";
    return false;
  }
  CloseHandle(pi.hThread);
  for (int waited = 0; WaitForSingleObject(pi.hProcess, 100) == WAIT_TIMEOUT; waited += 100) {
    if (std::ifstream in(progressFile); in) {
      uint32_t done = 0, total = 0;
      if (in >> done >> total) progress(done, total);
    }
    if (stop() || waited > 20 * 60 * 1000) {
      TerminateProcess(pi.hProcess, 1);
      WaitForSingleObject(pi.hProcess, 5000);
      CloseHandle(pi.hProcess);
      error = stop() ? "stopped (the launcher closed)" : "took too long";
      return false;
    }
  }
  DWORD code = 1;
  GetExitCodeProcess(pi.hProcess, &code);
  CloseHandle(pi.hProcess);
  if (std::ifstream in(progressFile); in) {
    uint32_t done = 0, total = 0;
    if (in >> done >> total) progress(done, total);
  }
  fs::remove(progressFile, ec);
  std::string have;
  if (std::ifstream in(stamp); in) std::getline(in, have);
  if (code != 0 || have != key) {
    error = "the game's shader preparation ended with code " + std::to_string(code) + " (see its log in " +
            Utf8(xd::UserFolder() / L"logs") + ")";
    return false;
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
  if (fs::exists(done, ec)) {
    return true;
  }
  if (!fs::exists(sGameProgram, ec)) {
    error = "the game program isn't there: " + sGameProgram.string();
    return false;
  }
  fs::create_directories(folder, ec);
  starting();
  PROCESS_INFORMATION pi{};
  if (!StartHiddenGame({L"XD_RENDER_ICON=all"}, discPath, BELOW_NORMAL_PRIORITY_CLASS, pi)) {
    error = "could not start the game program";
    return false;
  }
  CloseHandle(pi.hThread);
  DWORD code = STILL_ACTIVE;
  for (int waited = 0; WaitForSingleObject(pi.hProcess, 250) == WAIT_TIMEOUT; waited += 250) {
    if (stop() || waited > 5 * 60 * 1000) {
      TerminateProcess(pi.hProcess, 1);
      WaitForSingleObject(pi.hProcess, 5000);
      CloseHandle(pi.hProcess);
      error = stop() ? "stopped (the launcher closed, or Play was pressed)" : "the game took too long";
      return false;
    }
  }
  GetExitCodeProcess(pi.hProcess, &code);
  CloseHandle(pi.hProcess);
  if (FILE* f = _wfopen(done.c_str(), L"w")) {
    std::fprintf(f, "xd.exe XD_RENDER_ICON=all exit code %lu (0: all made; see render_icon.txt)\n", code);
    std::fclose(f);
  }
  if (code != 0) {
    error = "the game's render mode ended with code " + std::to_string(code) + " (render_icon.txt in " +
            folder.string() + ")";
    return false;
  }
  return true;
}

}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  launcher::GameInfo game;
  game.title = "Pokemon XD";
  game.windowTitle = "Pok\xC3\xA9mon XD";
  game.subtitle = "Gale of Darkness - PC";
  game.gameId = xd_settings::kGameId;
  game.discHelp =
      "The game needs your own copy of Pokemon XD: Gale of Darkness (USA version, game ID GXXE01), saved as a disc "
      "image file (.iso, .rvz, .gcm, ...). Nothing from the game comes with this program.";
  const fs::path settings = xd::SettingsFolder();
  game.settingsFolder = settings;
  game.modsFolder = settings / L"mods";
  game.modListFile = settings / L"mods.ini";
  game.playerFilesReadOnly = xd::PlayerFilesReadOnly();
  game.readOnlyNote = xd::kPlayerFilesReadOnlyNote;
  const fs::path own = ProgramFolder();
  std::error_code ec;
  game.program = own / L"xd.exe";
  if (!fs::exists(game.program, ec) && fs::exists(own / L"game" / L"xd.exe", ec)) {
    game.program = own / L"game" / L"xd.exe";
  }
  game.shortcutTarget = own / L"Pokemon XD.exe";
  if (fs::exists(own.parent_path() / L"Pokemon XD.exe", ec) &&
      engine::update::IsGameFolderName(own.filename().string())) {
    game.shortcutTarget = own.parent_path() / L"Pokemon XD.exe";
  } else if (!fs::exists(game.shortcutTarget, ec)) {
    wchar_t self[MAX_PATH * 4];
    game.shortcutTarget = fs::path(std::wstring(self, GetModuleFileNameW(nullptr, self, MAX_PATH * 4)));
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
    game.saves.exportName = "01-GXXE-PokemonXD.gci";
    if (const wchar_t* bin = _wgetenv(L"XD_SAVE_BIN_DIR"); bin != nullptr && bin[0] != L'\0') {
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
  game.ownIcons = {{"original", "XD (the program's own)", 1}, {"original_shadow", "XD, shadow aura", 2},
                   {"original_minimal", "XD, minimal", 3}, {"original_swirl", "XD, shadow swirl", 4},
                   {"original_light", "XD, light", 5}};
  if (const wchar_t* dir = _wgetenv(L"XD_SHORTCUT_DIR")) {
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
  SetUpUpdates(game.updates, own);
  return launcher::Run(game);
}
