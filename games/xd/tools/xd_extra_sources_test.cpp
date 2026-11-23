#include "../launcher/xd_extra_sources.h"
#include "../launcher/xd_n3ds_dump.h"
#include "../src/xd_battle_models.h"
#include "../src/xd_settings.h"

#include <settings.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <vector>
#include <cstring>
#include <set>
#include <string>

namespace es = engine::settings;

namespace {

bool sOk = true;

void check(bool cond, const std::string& what) {
  std::printf("%s %s\n", cond ? "ok  " : "FAIL", what.c_str());
  sOk &= cond;
}

}

int main() {
  es::Registry r;
  r.SetWarningSink([](const std::string& m) { std::printf("     warning: %s\n", m.c_str()); });
  xd_settings::Register(r);

  const auto& sources = xd::ExtraSources();
  check(sources.size() == 5, "five sources: Pokemon Battle Revolution, Pokemon Colosseum, the European disc (languages), the Japanese disc, Ultra Sun / Ultra Moon");
  std::set<std::string> ids;
  for (const auto& s : sources) {
    const std::string who = "source " + s.id + ": ";
    check(!s.id.empty() && ids.insert(s.id).second, who + "a unique id");
    check(!s.name.empty() && !s.provides.empty() && !s.how.empty(), who + "name, what it provides and how it is made are said");
    check(!s.gameIds.empty(), who + "accepts at least one game ID");
    for (const std::string& g : s.gameIds) check(s.dump ? g.rfind("CTR-P-", 0) == 0 : g.size() == 6, who + "game ID " + g + (s.dump ? " is a 3DS product code" : " has six characters"));
    check(!s.dump || s.identify, who + "a dump source says how it tells its file");
    check(!s.converterStem.empty() && !s.converterArguments.empty(), who + "has a converter and its arguments");
    bool disc = false, game = false, out = false;
    for (const std::string& a : s.converterArguments) {
      disc |= a.find("{disc}") != std::string::npos;
      game |= a.find("{game_disc}") != std::string::npos;
      out |= a.find("{out}") != std::string::npos;
    }
    check(disc && (game || !s.needsGameDisc) && out, who + "the arguments name the extra disc, the player's disc (when it needs it) and the folder to make");
    const size_t i = r.Find(s.discSetting);
    check(i != es::kNoSetting && r.Get(i).type == es::Type::Text && r.Get(i).preference,
          who + "its disc path is a text preference setting (" + s.discSetting + ")");
    check(s.discSetting.rfind("system.extra_disc.", 0) == 0, who + "the disc setting is under system.extra_disc.");
    if (!s.switchSetting.empty()) {
      const size_t sw = r.Find(s.switchSetting);
      check(sw != es::kNoSetting && r.Get(sw).type == es::Type::Bool, who + "its switch is a Bool setting");
    }
    check(!s.manifestId.empty() && engine::mods::ValidModId(s.manifestId), who + "the description file's id is valid");
  }
  const auto& pbr = sources[0];
  check(pbr.id == "pbr" && pbr.gameIds.size() == 1 && pbr.gameIds[0] == "RPBE01", "Battle Revolution: id pbr, game ID RPBE01 (USA)");
  check(pbr.manifestId == "battle-revolution-models" && pbr.manifestId == xd::battle_models::kManifestId &&
            std::string(xd::battle_models::kLayerId) == pbr.id,
        "its folder is extra_assets\pbr (the layer id is the source id), description id battle-revolution-models");
  check(pbr.converterFolder == "pbr_models" && pbr.converterStem == "pbr_models", "converter: <game folder>\\pbr_models\\pbr_models");

  check(pbr.needsPlainGameDisc, "Battle Revolution: needs the plain XD disc (an XG disc is refused as {game_disc})");
  const auto& colo = sources[1];
  check(colo.id == "colosseum" && colo.gameIds.size() == 1 && colo.gameIds[0] == "GC6E01", "Colosseum: id colosseum, game ID GC6E01 (USA)");
  check(colo.manifestId == "colosseum-characters" && colo.converterStem == "colo_models" && !colo.needsGameDisc,
        "Colosseum: description id colosseum-characters, converter colo_models, no XD disc needed");
  check(colo.modelsFolder == "models" && colo.contentsFile == "characters.json", "Colosseum: counts models/, lists characters.json");
  {
    check(sources[4].tab == "models" && sources[4].tabOrder < sources[0].tabOrder && sources[0].tab == "models" && sources[0].tabOrder < sources[1].tabOrder &&
              sources[1].tab == "models" && sources[2].tab == "languages" && sources[3].tab == "languages",
          "tabs: models (Ultra Sun / Moon, Battle Revolution, Colosseum) and languages (European, Japanese)");
  }
  const auto& usum = sources[4];
  check(usum.id == "usum" && usum.dump && usum.manifestId == "ultra-sun-moon-models" && usum.converterStem == "usum_models" &&
            usum.discSetting == "system.extra_disc.usum" && usum.needsGameDisc,
        "Ultra Sun / Ultra Moon: id usum, a dump, description id ultra-sun-moon-models, converter usum_models, needs the XD disc");
  {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "xd_n3ds_dump_test";
    fs::create_directories(dir);
    auto make = [&](const char* name, bool ncsd, bool ncch, bool plain, const char* code, size_t size = 0x800) {
      std::vector<char> b(size, 0);
      auto put = [&b](size_t at, const void* p, size_t n) {
        if (at + n <= b.size()) std::memcpy(&b[at], p, n);
      };
      if (ncsd) put(0x100, "NCSD", 4);
      if (b.size() > 0x120) b[0x120] = 2;
      if (ncch) put(0x400 + 0x100, "NCCH", 4);
      put(0x400 + 0x150, code, std::strlen(code));
      if (plain && b.size() > 0x400 + 0x188 + 7) b[0x400 + 0x188 + 7] = 4;
      const fs::path f = dir / name;
      std::ofstream(f, std::ios::binary).write(b.data(), static_cast<std::streamsize>(b.size()));
      return f.string();
    };
    std::string id, title, err;
    check(xd::IdentifyN3dsDump(make("us.3ds", true, true, true, "CTR-P-A2AE"), id, title, err) && id == "CTR-P-A2A" && title == "Pokemon Ultra Sun",
          "dump check: a decrypted Ultra Sun dump is recognised (code CTR-P-A2A, Pokemon Ultra Sun)");
    check(xd::IdentifyN3dsDump(make("um.cci", true, true, true, "CTR-P-A2BJ"), id, title, err) && id == "CTR-P-A2B", "dump check: Ultra Moon (another region) is CTR-P-A2B");
    check(xd::IdentifyN3dsDump(make("x.3ds", true, true, true, "CTR-P-EGDE"), id, title, err) && id == "CTR-P-EGD" && title == "Pokemon X" &&
              std::find(usum.gameIds.begin(), usum.gameIds.end(), id) == usum.gameIds.end(),
          "dump check: Pokemon X is recognised but the row does not take it");
    check(!xd::IdentifyN3dsDump(make("enc.3ds", true, true, false, "CTR-P-A2AE"), id, title, err) && err.find("encrypted") != std::string::npos,
          "dump check: an encrypted dump is refused and says so");
    check(!xd::IdentifyN3dsDump(make("nope.3ds", false, false, true, "CTR-P-A2AE"), id, title, err) && !err.empty(), "dump check: a file without the NCSD mark is refused");
    check(!xd::IdentifyN3dsDump(make("cut.3ds", true, false, true, "CTR-P-A2AE"), id, title, err) && !err.empty(), "dump check: a dump whose game data is missing is refused");
    check(!xd::IdentifyN3dsDump(make("small.3ds", true, true, true, "CTR-P-A2AE", 0x180), id, title, err) && !err.empty(), "dump check: a file shorter than a header is refused");
    struct CiaOpt { uint32_t titleHigh = 0x00040000; bool encryptedContent = false; bool ncchPlain = true; bool cut = false; bool sigEcdsa = false; };
    auto makeCia = [&](const char* name, const CiaOpt& o, const char* code) {
      auto be32 = [](std::vector<char>& v, size_t at, uint32_t x) { for (int i = 0; i < 4; ++i) v[at + i] = static_cast<char>(x >> (24 - 8 * i)); };
      auto le32 = [](std::vector<char>& v, size_t at, uint32_t x) { for (int i = 0; i < 4; ++i) v[at + i] = static_cast<char>(x >> (8 * i)); };
      const size_t sig = o.sigEcdsa ? 0x80 : 0x240;
      const size_t tmdSize = sig + 0xC4 + 0x900 + 2 * 0x30;
      const size_t cert = 0x100, tik = 0x2C4, meta = 0;
      auto al = [](size_t n) { return (n + 0x3F) & ~size_t(0x3F); };
      const size_t tmdOff = al(0x2020) + al(cert) + al(tik), contentOff = tmdOff + al(tmdSize) + al(meta);
      std::vector<char> b(contentOff + 0x1000, 0);
      le32(b, 0, 0x2020); le32(b, 8, static_cast<uint32_t>(cert)); le32(b, 12, static_cast<uint32_t>(tik)); le32(b, 16, static_cast<uint32_t>(tmdSize));
      b[0x20] = static_cast<char>(0xC0);
      if (o.sigEcdsa) be32(b, tmdOff, 0x10005); else be32(b, tmdOff, 0x10000);
      be32(b, tmdOff + sig + 0x4C, o.titleHigh); be32(b, tmdOff + sig + 0x50, 0x00123400);
      b[tmdOff + sig + 0x9F] = 2;
      const size_t rec = tmdOff + sig + 0xC4 + 0x900;
      b[rec + 7] = o.encryptedContent ? 1 : 0; b[rec + 0x0F] = 0x00; be32(b, rec + 8, 0); be32(b, rec + 12, 0x1000);
      b[rec + 0x30 + 5] = 1;
      const size_t n = contentOff;
      std::memcpy(&b[n + 0x100], "NCCH", 4);
      std::memcpy(&b[n + 0x150], code, std::strlen(code));
      if (o.ncchPlain) b[n + 0x188 + 7] = 4;
      if (o.cut) b.resize(contentOff + 0x80);
      const fs::path f = dir / name;
      std::ofstream(f, std::ios::binary).write(b.data(), static_cast<std::streamsize>(b.size()));
      return f.string();
    };
    check(xd::IdentifyN3dsDump(makeCia("ok.cia", CiaOpt{}, "CTR-P-A2AE"), id, title, err) && id == "CTR-P-A2A" && title == "Pokemon Ultra Sun",
          "cia: a decrypted Ultra Sun .cia is recognised like the .cci");
    CiaOpt ecdsa; ecdsa.sigEcdsa = true;
    check(xd::IdentifyN3dsDump(makeCia("ok2.cia", ecdsa, "CTR-P-A2BJ"), id, title, err) && id == "CTR-P-A2B", "cia: another signature block size, Ultra Moon");
    CiaOpt enc; enc.encryptedContent = true;
    check(!xd::IdentifyN3dsDump(makeCia("enc.cia", enc, "CTR-P-A2AE"), id, title, err) && err.find("encrypted") != std::string::npos && err.find("dumping tool") != std::string::npos,
          "cia: an encrypted .cia (TMD content type) is refused with the encrypted sentence");
    CiaOpt ncchEnc; ncchEnc.ncchPlain = false;
    check(!xd::IdentifyN3dsDump(makeCia("enc2.cia", ncchEnc, "CTR-P-A2AE"), id, title, err) && err.find(".cia is encrypted") != std::string::npos,
          "cia: an NCCH that is not NoCrypto is refused with the encrypted sentence");
    CiaOpt upd; upd.titleHigh = 0x0004000E; upd.encryptedContent = true;
    check(!xd::IdentifyN3dsDump(makeCia("upd.cia", upd, "CTR-P-A2AE"), id, title, err) && err.find("update") != std::string::npos,
          "cia: an update .cia is refused with its own sentence (even when encrypted)");
    CiaOpt dlc; dlc.titleHigh = 0x0004008C;
    check(!xd::IdentifyN3dsDump(makeCia("dlc.cia", dlc, "CTR-P-A2AE"), id, title, err) && err.find("DLC") != std::string::npos, "cia: a DLC .cia is refused with its own sentence");
    CiaOpt sys; sys.titleHigh = 0x00040010;
    check(!xd::IdentifyN3dsDump(makeCia("sys.cia", sys, "CTR-P-A2AE"), id, title, err) && err.find("not a game") != std::string::npos, "cia: a system title is refused");
    CiaOpt cut; cut.cut = true;
    check(!xd::IdentifyN3dsDump(makeCia("cut.cia", cut, "CTR-P-A2AE"), id, title, err) && err.find("cut short") != std::string::npos, "cia: a cut-short .cia is refused");
    check(!xd::IdentifyN3dsDump(make("tiny.cia", false, false, false, "x", 0x100), id, title, err) && !err.empty(), "cia: a tiny file named .cia is refused");
    check(xd::IdentifyN3dsDump(make("renamed.cia", true, true, true, "CTR-P-A2AE"), id, title, err) && id == "CTR-P-A2A", "a cartridge dump with the wrong extension is still read by what it holds");
    check(!xd::IdentifyN3dsDump(dir.string(), id, title, err) && err.find("folder") != std::string::npos, "dump check: a folder is refused and named");
    check(!xd::IdentifyN3dsDump((dir / "missing.3ds").string(), id, title, err) && !err.empty(), "dump check: a missing file is refused");
    fs::remove_all(dir);
  }
  const auto& lang = sources[2];
  check(lang.id == "languages" && lang.gameIds.size() == 1 && lang.gameIds[0] == "GXXP01", "languages: id languages, game ID GXXP01 (European)");
  check(lang.manifestId == "xd-languages" && lang.converterStem == "xd_languages", "languages: description id xd-languages, converter xd_languages");
  {
    const size_t li = r.Find("system.language");
    check(li != es::kNoSetting && r.Get(li).type == es::Type::Enum && r.Get(li).preference && r.Get(li).implemented,
          "the Language setting is an enum preference that works");
    check(li != es::kNoSetting && r.Get(li).choices.size() == 6 && r.Get(li).choices[0].id == "us", "Language: us first, then five more (German, French, Italian, Spanish, Japanese)");
    check(li != es::kNoSetting && r.DefaultOf(li, es::Preset::Original).i == 0 && r.DefaultOf(li, es::Preset::Enhanced).i == 0,
          "English (US) in Original and in Enhanced");
  }

  const size_t i = r.Find(xd::battle_models::kSetting);
  check(i != es::kNoSetting && r.Get(i).implemented && !r.Get(i).preference, "the setting exists, works and is not a preference");
  check(r.DefaultOf(i, es::Preset::Original).i == 0 && r.DefaultOf(i, es::Preset::Enhanced).i == 0, "off in Original and in Enhanced");
  check(r.Get(i).description.find("next time the game starts") != std::string::npos,
        "its text says it acts from the next start");
  check(!r.SetBool(xd::battle_models::kSetting, true) || r.CurrentPreset() != es::Preset::Original,
        "turning it on leaves Original (it is a change from the GameCube game)");

  std::printf(sOk ? "xd_extra_sources_test: all passed\n" : "xd_extra_sources_test: FAILED\n");
  return sOk ? 0 : 1;
}
