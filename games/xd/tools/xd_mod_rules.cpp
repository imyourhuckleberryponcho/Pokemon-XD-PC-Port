#include "xd_mod_rules.h"

#include "../src/data/xd_data_mods.h"
#include "../src/data/xd_objects.h"
#include "../src/dol_tables.h"

#include <gs_formats.h>
#include <mod_layout.h>
#include "json.h"
#include <mods_disc.h>
#include <port_disc.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace xd::modrules {

namespace mc = engine::modcheck;

mc::GameRules Rules() {
  mc::GameRules r;
  r.gameName = "Pokemon XD";
  r.gameIds = {"GXXE01", "GXXP01", "GXXJ01"};
  r.scriptHooks = {"battle.damage", "battle.damage_predict", "battle.move_used", "battle.turn_start", "battle.turn_end", "battle.end",
                  "field.room_ready", "field.battle_start", "field.battle_end", "field.item_use", "field.step"};
  r.gameOnlyLua = {"story_function", "position", "room", "in_zone", "FLAG", "STORY"};
  r.checkMessageText = [](const std::string& text, std::string& why) { return xd::data::CheckMessageText(text, why); };
  r.extraFolders = {{"rooms", "entry"}, {"objects", "json"}, {"texts", "json"}};
  r.checkEntryFile = [](const std::string& path, const std::vector<uint8_t>& data, mc::Report& report) {
    if (data.size() >= 4 && data[0] == 'T' && data[1] == 'C' && data[2] == 'O' && data[3] == 'D') {
      char why[160] = "";
      if (!Tiga_Validate(data.data(), data.size(), why, sizeof(why))) {
        report.Problem("script-invalid", path, 0, std::string("this story script is not laid out as the game reads scripts: ") + why +
                                                       " (the game switches it off and keeps its own)");
      }
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
          const auto face = row.kind == engine::mods::json::Value::Kind::Object ? row.fields.find("face") : row.fields.end();
          if (face == row.fields.end() || face->second.kind != engine::mods::json::Value::Kind::String) continue;
          std::string lower = face->second.text;
          for (char& c : lower) c = c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
          if (lower.size() < 4 || lower.compare(lower.size() - 4, 4, ".png") != 0) continue;
          std::filesystem::path found;
          bool there = false;
          for (const std::filesystem::path& root2 : engine::mods::ContentRoots(context.folder, std::string_view())) {
            std::error_code ec;
            there = there || (std::filesystem::is_regular_file(root2 / face->second.text, ec) && face->second.text.find("..") == std::string::npos);
          }
          if (!there) {
            report.Problem("face-missing", (f.from.empty() ? std::string() : "games/" + f.from + "/") + "data/" + f.rel, static_cast<int>(face->second.line),
                           "the face of \"" + name + "\" is " + face->second.text + ", but the mod has no such picture (the path starts inside the mod, like textures/faces/x.png)");
          }
        }
      }
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
  load.finishRows = xd::data::BuildRooms;
  load.checkEntry = xd::data::CheckModEntry;
  load.dataCatalog = xd::data::BuildDataCatalog;
  return true;
}

void CloseDisc() { PortDisc_Close(); }

}
