#include "xd_mod_rules.h"

#include "../src/data/xd_battle_defs.h"
#include "../src/data/xd_data_mods.h"
#include "../src/data/xd_objects.h"
#include "../src/dol_tables.h"
#include "../src/xd_playthrough_files.h"
#include "../src/xd_xg.h"

#include <audio_codec.h>
#include <gs_formats.h>
#include <mod_layout.h>
#include "json.h"
#include <mods_disc.h>
#include <port_disc.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <cstring>
#include <fstream>
#include <set>
#include <sstream>

namespace xd::modrules {

namespace mc = engine::modcheck;

mc::GameRules Rules() {
  mc::GameRules r;
  r.gameName = "Pokemon XD";
  r.gameIds = {"GXXE01", "GXXP01", "GXXJ01"};
  r.scriptHooks = {"battle.damage", "battle.damage_predict", "battle.move_used", "battle.turn_start", "battle.turn_end", "battle.end",
                  "field.room_ready", "field.battle_start", "field.battle_end", "field.item_use", "field.step", "field.flag_changed", "field.money_changed", "field.pokemon_caught",
                  "field.menu_open", "battle.ability", "battle.ai_switch", "battle.ai_item", "field.replay_kept"};
  r.gameOnlyLua = {"story_function", "position", "room", "in_zone", "FLAG", "STORY", "set_trainer_team", "lend_team", "battle_tables"};
  r.checkMessageText = [](const std::string& text, std::string& why) { return xd::data::CheckMessageText(text, why); };
  r.looksOrSound = xd::playthrough::LooksOrSound;
  r.extraFolders = {{"rooms", "entry"}, {"objects", "json"}, {"texts", "json"}, {"common_script", "entry"}, {"battle", "json"}};
  r.checkEntryFile = [](const std::string& path, const std::vector<uint8_t>& data, mc::Report& report) {
    if (data.size() >= 4 && data[0] == 'T' && data[1] == 'C' && data[2] == 'O' && data[3] == 'D') {
      char why[160] = "";
      if (!Tiga_Validate(data.data(), data.size(), why, sizeof(why))) {
        report.Problem("script-invalid", path, 0, std::string("this story script is not laid out as the game reads scripts: ") + why +
                                                       " (the game switches it off and keeps its own)");
      }
    }
    const auto be32 = [&](size_t at) { return static_cast<uint32_t>(data[at]) << 24 | static_cast<uint32_t>(data[at + 1]) << 16 | static_cast<uint32_t>(data[at + 2]) << 8 | data[at + 3]; };
    const bool pkx = data.size() > 0x200 && data[0x1A] == 0x00 && data[0x1B] == 0x0C;
    const bool hsd = data.size() > 0x40 && be32(0) == data.size();
    if (pkx && data.size() > 614400) {
      report.Warn("model-too-big", path, 0, "this model is " + std::to_string(data.size() >> 10) + " KB; the game's model memory holds about 600 KB (the biggest of the game's own is 614,272 bytes): make the model smaller (fewer triangles, a smaller picture)");
    }
    if (hsd && data.size() > 614400) {
      report.Warn("model-too-big", path, 0, "this model is " + std::to_string(data.size() >> 10) + " KB; the game's model memory holds about 600 KB: make the model smaller");
    }
    if (pkx && be32(0) + 0x84u + 0xD0u > data.size()) {
      report.Problem("model-invalid", path, 0, "the header of this model says its archive is " + std::to_string(be32(0)) + " bytes, but the file is only " + std::to_string(data.size()) + ": it is damaged or cut short");
    }
  };
  r.extra.push_back({"species faces", [](const mc::GameRules::Context& context, mc::Report& report) {
    for (const engine::mods::LayoutFile& f : engine::mods::LayoutFiles(context.folder, std::string_view(), "data", true)) {
      if ((!f.from.empty() && f.from.rfind("GXX", 0) != 0) || std::filesystem::path(f.rel).extension() != ".json") continue;
      std::ifstream in(f.path, std::ios::binary);
      std::stringstream text;
      text << in.rdbuf();
      engine::mods::json::Value root;
      const std::string body = text.str();
      engine::mods::json::Reader reader(body);
      if (!reader.Document(root) || root.kind != engine::mods::json::Value::Kind::Object) continue;
      const auto add = root.fields.find("add");
      if (add == root.fields.end() || add->second.kind != engine::mods::json::Value::Kind::Object) continue;
      for (const char* table : {"pokemon", "pokemon_data"}) {
        const auto rows = add->second.fields.find(table);
        if (rows == add->second.fields.end() || rows->second.kind != engine::mods::json::Value::Kind::Object) continue;
        for (const auto& [name, row] : rows->second.fields) {
          static const struct { const char* key; const char* ext; const char* code; const char* what; const char* example; } kFiles[] = {
              {"face", ".png", "face-missing", "picture", "textures/faces/x.png"}, {"model", ".pkx", "model-missing", "model file", "models/x.pkx"},
              {"cry", ".wav", "cry-missing", "sound file", "sounds/x.wav"}};
          for (const auto& k : kFiles) {
            const auto face = row.kind == engine::mods::json::Value::Kind::Object ? row.fields.find(k.key) : row.fields.end();
            if (face == row.fields.end() || face->second.kind != engine::mods::json::Value::Kind::String) continue;
            std::string lower = face->second.text;
            for (char& c : lower) c = c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
            const size_t n = std::strlen(k.ext);
            if (lower.size() < n || lower.compare(lower.size() - n, n, k.ext) != 0) continue;
            bool there = false;
            for (const std::filesystem::path& root2 : engine::mods::ContentRoots(context.folder, std::string_view())) {
              std::error_code ec;
              there = there || (std::filesystem::is_regular_file(root2 / face->second.text, ec) && face->second.text.find("..") == std::string::npos);
            }
            if (!there) {
              report.Problem(k.code, (f.from.empty() ? std::string() : "games/" + f.from + "/") + "data/" + f.rel, static_cast<int>(face->second.line),
                             std::string("the ") + k.key + " of \"" + name + "\" is " + face->second.text + ", but the mod has no such " + k.what + " (the path starts inside the mod, like " + k.example + ")");
            }
          }
        }
      }
    }
  }});
  r.extra.push_back({"battle effects", [](const mc::GameRules::Context& context, mc::Report& report) {
    for (const engine::mods::LayoutFile& f : engine::mods::LayoutFiles(context.folder, std::string_view(), "battle", true)) {
      if ((!f.from.empty() && f.from.rfind("GXX", 0) != 0) || std::filesystem::path(f.rel).extension() != ".json") continue;
      std::ifstream in(f.path, std::ios::binary);
      std::stringstream text;
      text << in.rdbuf();
      xd::battle::Defs defs;
      xd::battle::ParseFile(context.manifest.id, "battle/" + f.rel, text.str(),
                            [](const std::string&, const std::string&, int64_t& v) { v = 1; return true; }, defs);
      xd::battle::Finish(defs);
      for (const std::string& problem : defs.problems) report.Problem("battle-effects", "battle/" + f.rel, 0, problem);
    }
  }});
  r.extra.push_back({"story objects", [](const mc::GameRules::Context& context, mc::Report& report) {
    std::vector<std::filesystem::path> files;
    for (const engine::mods::LayoutFile& f : engine::mods::LayoutFiles(context.folder, std::string_view(), "objects", false)) {
      if ((f.from.empty() || f.from.rfind("GXX", 0) == 0) && std::filesystem::path(f.rel).extension() == ".json") files.push_back(f.path);
    }
    std::sort(files.begin(), files.end());
    for (const auto& file : files) {
      std::ifstream in(file, std::ios::binary);
      std::stringstream text;
      text << in.rdbuf();
      xd::objects::Room room;
      std::vector<std::string> problems;
      xd::objects::Parse(text.str(), room, problems);
      const std::string shown = std::filesystem::relative(file, context.folder).generic_string();
      for (const std::string& p : problems) {
        int line = 0;
        std::string what = p;
        if (p.rfind("line ", 0) == 0) {
          line = std::atoi(p.c_str() + 5);
          const size_t colon = p.find(": ");
          if (colon != std::string::npos) what = p.substr(colon + 2);
        }
        report.Problem("objects-invalid", shown, line, what);
      }
    }
  }});
  r.extra.push_back({"sounds", [](const mc::GameRules::Context& context, mc::Report& report) {
    namespace js = engine::mods::json;
    for (const char* name : {"sounds/effects.json", "sounds/songs.json"}) {
      const bool effects = std::strcmp(name, "sounds/effects.json") == 0;
      for (const std::filesystem::path& root : engine::mods::ContentRoots(context.folder, std::string_view())) {
        std::error_code ec;
        const std::filesystem::path file = root / name;
        if (!std::filesystem::is_regular_file(file, ec)) continue;
        const std::string shown = std::filesystem::relative(file, context.folder).generic_string();
        std::ifstream in(file, std::ios::binary);
        std::stringstream text;
        text << in.rdbuf();
        js::Value doc;
        const std::string body = text.str();
        js::Reader reader(body);
        if (!reader.Document(doc) || doc.kind != js::Value::Kind::Object) {
          report.Problem("sounds-invalid", shown, 0, "this is not a JSON object like {\"" + std::string(effects ? "effects" : "songs") + "\": [ ... ]}");
          continue;
        }
        const auto list = doc.fields.find(effects ? "effects" : "songs");
        if (list == doc.fields.end() || list->second.kind != js::Value::Kind::Array) {
          report.Problem("sounds-invalid", shown, static_cast<int>(doc.line), std::string("the file needs \"") + (effects ? "effects" : "songs") + "\": [ ... ]");
          continue;
        }
        std::set<std::string> names;
        for (const js::Value& e : list->second.items) {
          const int line = static_cast<int>(e.line);
          if (e.kind != js::Value::Kind::Object) {
            report.Problem("sounds-invalid", shown, line, "each entry must be an object");
            continue;
          }
          const auto get = [&](const char* k) { const auto it = e.fields.find(k); return it == e.fields.end() ? nullptr : &it->second; };
          const js::Value* f = get("file");
          if (f == nullptr || f->kind != js::Value::Kind::String) {
            report.Problem("sounds-invalid", shown, line, "\"file\" (a WAV or OGG of the mod) is missing");
            continue;
          }
          std::filesystem::path sound;
          if (!engine::mods::ResolveLayoutFile(context.folder, std::string_view(), f->text, sound) && !engine::mods::ResolveLayoutFile(context.folder, std::string_view(), "sounds/" + f->text, sound)) {
            report.Problem("sound-missing", shown, static_cast<int>(f->line), "the sound file \"" + f->text + "\" is not in the mod (the path starts inside the mod, like sounds/blip.wav)");
          } else {
            std::ifstream sin(sound, std::ios::binary);
            std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(sin)), std::istreambuf_iterator<char>());
            engine::audio::Pcm pcm;
            engine::audio::LoopPoints loop;
            std::string error;
            if (!engine::audio::ReadSoundFile(bytes, pcm, loop, error)) report.Problem("sound-unreadable", shown, static_cast<int>(f->line), "\"" + f->text + "\" can't be read as a sound (" + error + "): the port reads WAV (16/24/32-bit or float) and OGG Vorbis");
            else if (pcm.Frames() == 0) report.Problem("sound-unreadable", shown, static_cast<int>(f->line), "\"" + f->text + "\" holds no sound");
            else if (effects && pcm.Frames() > static_cast<size_t>(pcm.rate) * 30) report.Warn("sound-long", shown, static_cast<int>(f->line), "\"" + f->text + "\" is over 30 seconds long: a sound effect that long is not what the game's effects are (it must fit the game's audio memory)");
          }
          if (effects) {
            const js::Value* fx = get("fx");
            const js::Value* sample = get("sample");
            if ((fx == nullptr) == (sample == nullptr)) report.Problem("sounds-invalid", shown, line, "give exactly one of \"fx\" (an effect number) or \"sample\" (a sample number): xd_audio_tool sfx-list shows them");
            for (const js::Value* v : {fx, sample}) {
              if (v != nullptr && (v->kind != js::Value::Kind::Number || v->number < 0 || v->number != static_cast<double>(static_cast<long>(v->number)))) report.Problem("sounds-invalid", shown, static_cast<int>(v->line), "the number must be a whole number");
            }
          } else {
            const js::Value* n = get("name");
            if (n == nullptr || n->kind != js::Value::Kind::String || n->text.empty() || n->text.size() > 48) {
              report.Problem("sounds-invalid", shown, line, "a song needs a \"name\" (letters, digits, - _ .; up to 48)");
            } else {
              for (char c : n->text) {
                if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.')) {
                  report.Problem("sounds-invalid", shown, static_cast<int>(n->line), "the song name \"" + n->text + "\" may have only letters, digits, - _ .");
                  break;
                }
              }
              if (!names.insert(n->text).second) report.Problem("sounds-invalid", shown, static_cast<int>(n->line), "two songs are called \"" + n->text + "\"");
            }
          }
        }
        if (list->second.items.empty()) report.Warn("sounds-empty", shown, static_cast<int>(list->second.line), "the list is empty: the mod changes no sound");
      }
    }
  }});
  return r;
}

bool OpenDisc(const std::string& discPath, engine::mods::LoadOptions& load, std::string& error) {
  char buf[512];
  if (!PortDisc_Open(discPath.c_str(), buf, sizeof(buf))) {
    error = buf;
    return false;
  }
  size_t dolSize = 0;
  const uint8_t* dol = PortDisc_Dol(&dolSize);
  if (dol == nullptr || !PortXD_LoadDolTables(dol, dolSize, buf, sizeof(buf))) {
    error = buf;
    PortDisc_Close();
    return false;
  }
  load.disc = engine::mods::OpenDiscView();
  load.disc.gameId = engine::mods::MakeTarget(load.disc.gameId, xd::xg::Edition());
  load.finishRows = xd::data::BuildRooms;
  load.checkEntry = xd::data::CheckModEntry;
  load.dataCatalog = xd::data::BuildDataCatalog;
  return true;
}

void CloseDisc() { PortDisc_Close(); }

}
