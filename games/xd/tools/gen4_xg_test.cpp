
#include "../src/xd_gen4_xg.h"

#include "../../../engine/mods/json.h"

#include <cstdio>
#include <string>

using namespace xd::gen4::xg;
namespace J = engine::mods::json;

static int sFailed = 0, sChecks = 0;
#define CHECK(c)                                                                  \
  do {                                                                            \
    sChecks++;                                                                    \
    if (!(c)) {                                                                   \
      sFailed++;                                                                  \
      std::printf("FAILED line %d: %s\n", __LINE__, #c);                          \
    }                                                                             \
  } while (0)

static Tables FakeXg() {
  Tables t;
  const char* moves[] = {"", "Bullet Punch", "Tackle", "Recover", "Roost", "Protect", "Protect", "Skill Swap", "Trick Room", "Slash", "Wish", "Spikes", "Splash"};
  const int pp[] = {0, 30, 35, 20, 20, 1, 30, 10, 5, 20, 10, 20, 40};
  for (size_t i = 0; i < sizeof moves / sizeof moves[0]; i++) t.moves.push_back(moves[i]), t.movePp.push_back(pp[i]);
  t.abilities = {"", "Drizzle", "Technician", "Inner Focus"};
  t.packAbilities.assign(124, "");
  t.packAbilities[101] = "Technician";
  t.packAbilities[77] = "Tangled Feet";
  t.items = {"", "Thunder Stone", "Everstone"};
  t.machineMoves = {9, 8};
  t.tutorMoves = {10};
  t.firstNewMove = 13;
  return t;
}

static J::Value Parse(const std::string& text) {
  J::Value v;
  J::Reader r(text);
  CHECK(r.Document(v));
  return v;
}

int main() {
  Translator tr(FakeXg());
  std::vector<engine::mods::DataModFile> files(2);
  files[0].label = "gen4/01_moves.json";
  files[0].text = R"({"add":{"moves":{
    "Roost":{"copy":"RECOVER","pp":10,"wazawzxdataId":105,"seqId":55},
    "Wake-Up Slap":{"copy":"POUND","pp":10,"zokuseiDataId":1,"iryoku":60,"wazawzxdataId":3,"unk16":[164,0],"text":{"name":"WAKE-UP SLAP"}},
    "Gravity":{"copy":"SAFEGUARD","pp":5}}}})";
  files[1].label = "gen4/02_species.json";

  files[1].text = R"({"add":{"pokemon":{"Togekiss":{"zokuseiDataId":[0,2],"tokuseiDataId":[101,0],
    "waza.level":[1,1,5,9,13,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0],"waza.dataId":[33,1,375,376,377,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0],
    "wazaMcn":[0,0,0,0],"teachWaza":[0]}}}})";
  std::string error;
  CHECK(tr.Run(files, "", error));
  CHECK(error.empty());

  const J::Value m = Parse(files[0].text);
  const J::Value& rows = m.fields.at("add").fields.at("moves");
  CHECK(rows.order.size() == 1 && rows.order[0] == "Wake-Up Slap");
  const J::Value& w = rows.fields.at("Wake-Up Slap");
  CHECK(w.fields.at("copy").number == 2);
  CHECK(w.fields.at("pp").number == 10 && w.fields.at("iryoku").number == 60);
  CHECK(w.fields.find("wazawzxdataId") == w.fields.end() && w.fields.find("unk16") == w.fields.end());
  CHECK(w.fields.at("text").fields.at("name").text == "WAKE-UP SLAP");
  CHECK(tr.AddedMoveNumber("Wake-Up Slap") == 13);

  const J::Value s = Parse(files[1].text);
  const J::Value& tk = s.fields.at("add").fields.at("pokemon").fields.at("Togekiss");
  CHECK(tk.fields.at("zokuseiDataId").items[0].number == 9 && tk.fields.at("zokuseiDataId").items[1].number == 2);
  CHECK(tk.fields.at("tokuseiDataId").items[0].number == 2);
  const J::Value& moves = tk.fields.at("waza.dataId");
  CHECK(moves.items[0].number == 2);
  bool roost = false, wake = false, trickRoom = false, tackleTwice = false;
  int tackles = 0;
  for (const J::Value& v : moves.items) {
    if (v.kind == J::Value::Kind::Number && v.number == 4) roost = true;
    if (v.kind == J::Value::Kind::String && v.text == "Wake-Up Slap") wake = true;
    if (v.kind == J::Value::Kind::Number && v.number == 8) trickRoom = true;
    if (v.kind == J::Value::Kind::Number && v.number == 2) tackles++;
  }
  tackleTwice = tackles > 1;
  CHECK(roost && wake && trickRoom && !tackleTwice);

  const J::Value& mcn = tk.fields.at("wazaMcn");
  CHECK(mcn.items.size() == 58 && mcn.items[1].number == 1);

  uint32_t level[20] = {1, 1, 5, 9, 13}, move[20] = {33, 1, 375, 376, 377};
  tr.MapLearnset(level, move);
  CHECK(move[0] == 2 && move[1] == 4 && move[2] == 13 && move[3] == 8 && move[4] == 0);
  uint32_t types[2] = {14, 14};
  tr.MapTypes("Mime Jr.", types);
  CHECK(types[0] == 14 && types[1] == 9);
  CHECK(tr.MapAbility(77) == 0);

  {
    std::vector<engine::mods::DataModFile> g4(2), g5(3);
    g4[0].label = "gen4/01_moves.json";
    g4[0].text = R"({"add":{"moves":{"Roost":{"copy":"RECOVER","pp":10}}}})";
    g4[1].label = "gen4/02_species.json";
    g4[1].text = R"({"add":{"pokemon":{"Togekiss":{"zokuseiDataId":[0,2],"tokuseiDataId":[101,0],"waza.level":[1],"waza.dataId":[375],"wazaMcn":[0],"teachWaza":[0]}}}})";
    Tables fx = FakeXg();
    fx.packAbilities.resize(256);
    fx.packAbilities[130] = "Gen7 Ability";
    fx.packAbilities[131] = "Another";
    Translator t(fx);
    CHECK(t.Run(g4, "", error));
    g5[0].label = "gen57/01_moves.json";
    g5[0].text = R"({"add":{"moves":{
      "Slash":{"copy":"Pound","pp":20},
      "Smack Down":{"copy":"Pound","pp":15,"iryoku":50,"zokuseiDataId":5,"animation":"Rock Throw","text":{"name":"SMACK DOWN"}},
      "Celebrate":{"copy":"Splash","pp":40,"iryoku":0,"animation":"Splash"},
      "Fairy Lock":{"copy":"Splash","pp":10,"iryoku":0,"zokuseiDataId":0}}}})";
    g5[1].label = "gen57/02_species.json";
    g5[1].text = R"({"add":{"pokemon":{"Cutiefly":{"numPokemon":742,"zokuseiDataId":[6,6],"tokuseiDataId":[101,130],"face":"x.png","nakigoeId":5,"voice":1,
      "waza.level":[1,1,5,0],"waza.dataId":[377,378,379,0],"wazaMcn":[0,0],"teachWaza":[0]}}}})";
    g5[2].label = "gen57/03_forms.json";
    g5[2].text = R"j({"add":{"items":{"Venusaurite":{"copy":"Everstone"},"Odd Stone":{"copy":"Nothing Here"}},"pokemon":{"Rattata (Alola)":{"zokuseiDataId":[16,0],"tokuseiDataId":[130,130],"waza.level":[1],"waza.dataId":[376],"wazaMcn":[0],"teachWaza":[0]}}}})j";
    Gen57Info info;
    info.portCodeMoves.insert("Smack Down");
    info.fairyMoves.insert("Fairy Lock");
    info.portCodeAbilities.insert(130);
    info.abilityIds = {130, 131};
    info.types[742] = {6, 17};
    CHECK(t.RunGen57(g5, info, error));
    const J::Value m5 = Parse(g5[0].text);
    const J::Value& r5 = m5.fields.at("add").fields.at("moves");
    CHECK(r5.order.size() == 3 && r5.order[0] == "Smack Down");
    const J::Value& sd = r5.fields.at("Smack Down");
    CHECK(sd.fields.at("copy").number == 2);
    CHECK(sd.fields.find("animation") == sd.fields.end());
    CHECK(r5.fields.at("Celebrate").fields.at("copy").number == 0 || r5.fields.at("Celebrate").fields.at("copy").kind == J::Value::Kind::Number);
    CHECK(r5.fields.at("Fairy Lock").fields.at("zokuseiDataId").number == 9);
    CHECK(t.AddedMoveNumber("Smack Down") == 13 && t.AddedMoveNumber("Fairy Lock") == 15);
    const Translator::Counts& c = t.Gen57Counts();
    CHECK(c.movesByName == 1 && c.movesPortCode == 1 && c.movesStandIn == 2);
    const J::Value sp = Parse(g5[1].text);
    const J::Value& cf = sp.fields.at("add").fields.at("pokemon").fields.at("Cutiefly");
    CHECK(cf.fields.at("zokuseiDataId").items[0].number == 6 && cf.fields.at("zokuseiDataId").items[1].number == 9);
    CHECK(cf.fields.at("tokuseiDataId").items[0].number == 2 && cf.fields.at("tokuseiDataId").items[1].number == 130);
    CHECK(cf.fields.find("face") == cf.fields.end() && cf.fields.find("voice") == cf.fields.end());
    const J::Value fm = Parse(g5[2].text);
    CHECK(fm.fields.at("add").fields.at("items").fields.at("Venusaurite").fields.at("copy").text == "#2");
    CHECK(fm.fields.at("add").fields.at("items").fields.at("Odd Stone").fields.at("copy").text == "Everstone");
    CHECK(fm.fields.at("add").fields.at("pokemon").fields.at("Rattata (Alola)").fields.at("waza.dataId").items[0].number == 9);
  }
  std::printf("gen4_xg_test: %d checks, %d failed\n", sChecks, sFailed);
  return sFailed == 0 ? 0 : 1;
}
