#include "room_recipe.h"

#include "json.h"
#include "room_mesh.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>

namespace engine::rooms {

namespace json = engine::mods::json;

namespace {

using Problems = std::vector<std::string>;

void PutBe32(uint8_t* p, uint32_t v) { p[0] = uint8_t(v >> 24), p[1] = uint8_t(v >> 16), p[2] = uint8_t(v >> 8), p[3] = uint8_t(v); }
void PutBeF(uint8_t* p, float f) {
  uint32_t u;
  std::memcpy(&u, &f, 4);
  PutBe32(p, u);
}

struct Ctx {
  Problems& problems;
  std::string where;
  void Bad(const std::string& s) { problems.push_back("room recipe: " + where + ": " + s); }
};

bool IsObject(const json::Value& v) { return v.kind == json::Value::Kind::Object; }
bool IsArray(const json::Value& v) { return v.kind == json::Value::Kind::Array; }

void Known(const json::Value& o, std::initializer_list<const char*> names, Ctx& c) {
  for (const std::string& k : o.order) {
    bool ok = false;
    for (const char* n : names) ok = ok || k == n;
    if (!ok) c.Bad("unknown field \"" + k + "\" (a typo? it would have been ignored)");
  }
}

const json::Value* Field(const json::Value& o, const char* name) {
  const auto it = o.fields.find(name);
  return it == o.fields.end() ? nullptr : &it->second;
}

bool Num(const json::Value* v, float& out, Ctx& c, const char* name, float lo = -1e6f, float hi = 1e6f) {
  if (v == nullptr) return true;
  if (v->kind != json::Value::Kind::Number || !std::isfinite(v->number)) {
    c.Bad(std::string("\"") + name + "\" must be a number");
    return false;
  }
  if (v->number < lo || v->number > hi) {
    c.Bad(std::string("\"") + name + "\" = " + json::Number(v->number) + " is outside " + json::Number(lo) + " .. " + json::Number(hi));
    return false;
  }
  out = float(v->number);
  return true;
}
bool Int(const json::Value* v, int& out, Ctx& c, const char* name, int lo, int hi) {
  float f = float(out);
  if (v == nullptr) return true;
  if (!Num(v, f, c, name, float(lo), float(hi))) return false;
  if (f != std::floor(f)) {
    c.Bad(std::string("\"") + name + "\" must be a whole number");
    return false;
  }
  out = int(f);
  return true;
}
bool Nums(const json::Value* v, float* out, size_t n, Ctx& c, const char* name) {
  if (v == nullptr) return true;
  if (!IsArray(*v) || v->items.size() != n) {
    c.Bad(std::string("\"") + name + "\" must be a list of " + std::to_string(n) + " numbers");
    return false;
  }
  for (size_t i = 0; i < n; i++) {
    if (v->items[i].kind != json::Value::Kind::Number || !std::isfinite(v->items[i].number) || std::fabs(v->items[i].number) > 1e6) {
      c.Bad(std::string("\"") + name + "\" holds something that is not a usable number");
      return false;
    }
    out[i] = float(v->items[i].number);
  }
  return true;
}
bool Str(const json::Value* v, std::string& out, Ctx& c, const char* name) {
  if (v == nullptr) return true;
  if (v->kind != json::Value::Kind::String) {
    c.Bad(std::string("\"") + name + "\" must be text in quotes");
    return false;
  }
  out = v->text;
  return true;
}

bool NameOk(const std::string& s) {
  if (s.empty() || s.size() > 40) return false;
  for (char ch : s) {
    if (!(std::isalnum(static_cast<unsigned char>(ch)) || ch == '_' || ch == '-')) return false;
  }
  return true;
}

}

bool ParseRecipe(const std::string& text, RoomRecipe& r, Problems& problems) {
  r = RoomRecipe{};
  const size_t before = problems.size();
  json::Value doc;
  json::Reader reader(text);
  if (!reader.Document(doc) || !IsObject(doc)) {
    problems.push_back("room recipe: not a JSON { ... } block (" + reader.Error() + ")");
    return false;
  }
  Ctx c{problems, "the recipe"};
  Known(doc, {"format", "version", "name", "base", "base_hash", "hide", "move", "pieces", "walls", "entrances", "camera", "comment"}, c);
  std::string format;
  Str(Field(doc, "format"), format, c, "format");
  if (!format.empty() && format != "xd-room") c.Bad("\"format\" must be \"xd-room\"");
  Int(Field(doc, "version"), r.version, c, "version", 0, 1000);
  if (r.version != kRecipeVersion) c.Bad("\"version\" must be 1 (this game reads format version 1; the file says " + std::to_string(r.version) + ")");
  Str(Field(doc, "name"), r.name, c, "name");
  Str(Field(doc, "base"), r.base, c, "base");
  Str(Field(doc, "base_hash"), r.baseHash, c, "base_hash");
  if (!NameOk(r.name)) c.Bad("\"name\" must be letters, digits, _ or - (up to 40)");
  if (r.base.empty()) c.Bad("\"base\" is needed: the archive name of the room of the disc that is the shell");

  const auto list = [&](const char* key, const json::Value& from, auto&& each) {
    const json::Value* v = Field(from, key);
    if (v == nullptr) return;
    if (!IsArray(*v)) { c.Bad(std::string("\"") + key + "\" must be a list [ ... ]"); return; }
    size_t i = 0;
    for (const json::Value& item : v->items) {
      Ctx ic{problems, std::string(key) + " #" + std::to_string(i++)};
      if (!IsObject(item)) { ic.Bad("must be a { ... } block"); continue; }
      each(item, ic);
    }
  };
  list("hide", doc, [&](const json::Value& o, Ctx& ic) {
    Known(o, {"model", "joint", "children"}, ic);
    RecipeHide h;
    Int(Field(o, "model"), h.model, ic, "model", 0, 63);
    if (!Field(o, "joint")) ic.Bad("\"joint\" is needed");
    Int(Field(o, "joint"), h.joint, ic, "joint", 0, 8191);
    if (const json::Value* ch = Field(o, "children")) h.children = ch->kind == json::Value::Kind::Bool ? ch->boolean : true;
    r.hide.push_back(h);
  });
  list("move", doc, [&](const json::Value& o, Ctx& ic) {
    Known(o, {"model", "joint", "by", "turn", "collision"}, ic);
    RecipeMove m;
    Int(Field(o, "model"), m.model, ic, "model", 0, 63);
    if (!Field(o, "joint")) ic.Bad("\"joint\" is needed");
    Int(Field(o, "joint"), m.joint, ic, "joint", 0, 8191);
    Nums(Field(o, "by"), m.by, 3, ic, "by");
    Num(Field(o, "turn"), m.turn, ic, "turn", -3600, 3600);
    if (const json::Value* col = Field(o, "collision")) {
      if (!IsObject(*col)) ic.Bad("\"collision\" must be { \"record\": n } or { \"area\": [x0, z0, x1, z1] }");
      else {
        Known(*col, {"record", "area"}, ic);
        Int(Field(*col, "record"), m.record, ic, "record", 0, 63);
        if (Field(*col, "area")) m.hasArea = true;
        Nums(Field(*col, "area"), m.area, 4, ic, "area");
        if (m.record >= 0 && m.hasArea) ic.Bad("\"collision\" names a record or an area, not both");
      }
    }
    r.move.push_back(m);
  });
  list("pieces", doc, [&](const json::Value& o, Ctx& ic) {
    Known(o, {"from", "model", "joint", "at", "turn", "hash", "collide", "height", "gltf", "scale"}, ic);
    RecipePiece p;
    Str(Field(o, "gltf"), p.gltf, ic, "gltf");
    Num(Field(o, "scale"), p.scale, ic, "scale", 0.0001, 10000);
    if (!p.gltf.empty()) {
      if (Field(o, "from") || Field(o, "model") || Field(o, "joint") || Field(o, "hash")) ic.Bad("a piece is either \"gltf\" (your own model) or \"from\" + \"joint\" (a piece of a room of the game), not both");
      if (p.gltf.find("..") != std::string::npos || p.gltf[0] == '/' || p.gltf[0] == '\\' || p.gltf.find(':') != std::string::npos) ic.Bad("\"gltf\" is a path below the room's folder (parts/name.glb)");
    } else {
      Str(Field(o, "from"), p.from, ic, "from");
      if (p.from.empty()) ic.Bad("\"from\" is needed: the archive name of the room the piece comes from (or \"gltf\": your own model)");
      Int(Field(o, "model"), p.model, ic, "model", 0, 63);
      if (!Field(o, "joint")) ic.Bad("\"joint\" is needed");
      Int(Field(o, "joint"), p.joint, ic, "joint", 0, 8191);
    }
    Nums(Field(o, "at"), p.at, 3, ic, "at");
    Num(Field(o, "turn"), p.turn, ic, "turn", -3600, 3600);
    Str(Field(o, "hash"), p.hash, ic, "hash");
    Str(Field(o, "collide"), p.collide, ic, "collide");
    if (p.collide != "none" && p.collide != "box" && p.collide != "mesh") ic.Bad("\"collide\" must be \"none\", \"box\" or \"mesh\"");
    Num(Field(o, "height"), p.height, ic, "height", 0, 500);
    r.pieces.push_back(p);
  });
  if (const json::Value* w = Field(doc, "walls")) {
    Ctx wc{problems, "walls"};
    if (!IsObject(*w)) wc.Bad("must be a { ... } block");
    else {
      Known(*w, {"add", "boxes", "remove"}, wc);
      list("add", *w, [&](const json::Value& o, Ctx& ic) {
        Known(o, {"line", "y", "height", "flip"}, ic);
        RecipeWall wl;
        float line[4] = {};
        if (!Field(o, "line")) ic.Bad("\"line\" is needed: [x0, z0, x1, z1]");
        Nums(Field(o, "line"), line, 4, ic, "line");
        wl.x0 = line[0], wl.z0 = line[1], wl.x1 = line[2], wl.z1 = line[3];
        Num(Field(o, "y"), wl.y, ic, "y");
        Num(Field(o, "height"), wl.height, ic, "height", 1, 500);
        if (const json::Value* f = Field(o, "flip")) wl.flip = f->kind == json::Value::Kind::Bool && f->boolean;
        if (wl.x0 == wl.x1 && wl.z0 == wl.z1) ic.Bad("the line has no length");
        r.wallsAdd.push_back(wl);
      });
      list("boxes", *w, [&](const json::Value& o, Ctx& ic) {
        Known(o, {"cx", "cz", "sx", "sz", "y", "height"}, ic);
        RecipeBox b;
        float sx = 10, sz = 10;
        Num(Field(o, "cx"), b.cx, ic, "cx"), Num(Field(o, "cz"), b.cz, ic, "cz");
        Num(Field(o, "sx"), sx, ic, "sx", 0.5f, 2000), Num(Field(o, "sz"), sz, ic, "sz", 0.5f, 2000);
        b.hx = sx / 2, b.hz = sz / 2;
        Num(Field(o, "y"), b.y, ic, "y");
        Num(Field(o, "height"), b.height, ic, "height", 1, 500);
        r.boxes.push_back(b);
      });
      list("remove", *w, [&](const json::Value& o, Ctx& ic) {
        Known(o, {"area"}, ic);
        RecipeArea a;
        float v[4] = {};
        if (!Field(o, "area")) ic.Bad("\"area\" is needed: [x0, z0, x1, z1]");
        Nums(Field(o, "area"), v, 4, ic, "area");
        a.x0 = v[0], a.z0 = v[1], a.x1 = v[2], a.z1 = v[3];
        r.wallsRemove.push_back(a);
      });
    }
  }
  list("entrances", doc, [&](const json::Value& o, Ctx& ic) {
    Known(o, {"x", "y", "z", "angle", "exit_of"}, ic);
    RecipeEntrance e;
    if (Field(o, "exit_of") != nullptr) Str(Field(o, "exit_of"), e.exitOf, ic, "exit_of");
    Num(Field(o, "x"), e.x, ic, "x"), Num(Field(o, "y"), e.y, ic, "y"), Num(Field(o, "z"), e.z, ic, "z");
    Num(Field(o, "angle"), e.angle, ic, "angle", -360, 360);
    r.entrances.push_back(e);
  });
  r.entrancesGiven = Field(doc, "entrances") != nullptr;
  if (r.entrancesGiven && r.entrances.empty()) c.Bad("\"entrances\" is empty: a room needs at least one entrance");
  if (r.entrances.size() > 32) c.Bad("more than 32 entrances");
  list("camera", doc, [&](const json::Value& o, Ctx& ic) {
    Known(o, {"kind", "x", "y", "z", "distance", "height", "rotY", "radius", "ratio", "angle", "pow", "blend", "fov"}, ic);
    RecipeCamera cz;
    Int(Field(o, "kind"), cz.kind, ic, "kind", 0, 3);
    Num(Field(o, "x"), cz.pos[0], ic, "x"), Num(Field(o, "y"), cz.pos[1], ic, "y"), Num(Field(o, "z"), cz.pos[2], ic, "z");
    Num(Field(o, "distance"), cz.distance, ic, "distance", 1, 2000), Num(Field(o, "height"), cz.height, ic, "height", -500, 2000);
    Num(Field(o, "rotY"), cz.rotY, ic, "rotY", -3600, 3600), Num(Field(o, "radius"), cz.radius, ic, "radius", 1, 5000);
    Num(Field(o, "ratio"), cz.ratio, ic, "ratio", 0.01f, 100), Num(Field(o, "angle"), cz.angle, ic, "angle", -3600, 3600);
    Num(Field(o, "pow"), cz.powValue, ic, "pow", 0.01f, 100), Num(Field(o, "blend"), cz.blendTime, ic, "blend", 0, 1000);
    Num(Field(o, "fov"), cz.fov, ic, "fov", 5, 120);
    r.camera.push_back(cz);
  });
  r.cameraGiven = Field(doc, "camera") != nullptr;
  if (r.cameraGiven && r.camera.empty()) c.Bad("\"camera\" is empty: a room needs at least one camera zone (the first is the default)");
  if (r.camera.size() > 32) c.Bad("more than 32 camera zones");
  return problems.size() == before;
}

std::string RecipeToJson(const RoomRecipe& r) {
  const auto f = [](float v) { return json::Number(std::round(double(v) * 1000.0) / 1000.0); };
  std::string s = "{\n  \"format\": \"xd-room\",\n  \"version\": 1,\n  \"name\": \"" + r.name + "\",\n  \"base\": \"" + r.base + "\"";
  if (!r.baseHash.empty()) s += ",\n  \"base_hash\": \"" + r.baseHash + "\"";
  const auto sep = [&](bool& first) { s += first ? "\n    " : ",\n    "; first = false; };
  if (!r.hide.empty()) {
    s += ",\n  \"hide\": [";
    bool first = true;
    for (const auto& h : r.hide) sep(first), s += "{ \"model\": " + std::to_string(h.model) + ", \"joint\": " + std::to_string(h.joint) + (h.children ? "" : ", \"children\": false") + " }";
    s += "\n  ]";
  }
  if (!r.move.empty()) {
    s += ",\n  \"move\": [";
    bool first = true;
    for (const auto& m : r.move) {
      sep(first);
      s += "{ \"model\": " + std::to_string(m.model) + ", \"joint\": " + std::to_string(m.joint) + ", \"by\": [" + f(m.by[0]) + ", " + f(m.by[1]) + ", " + f(m.by[2]) + "]";
      if (m.turn != 0) s += ", \"turn\": " + f(m.turn);
      if (m.record >= 0) s += ", \"collision\": { \"record\": " + std::to_string(m.record) + " }";
      if (m.hasArea) s += ", \"collision\": { \"area\": [" + f(m.area[0]) + ", " + f(m.area[1]) + ", " + f(m.area[2]) + ", " + f(m.area[3]) + "] }";
      s += " }";
    }
    s += "\n  ]";
  }
  if (!r.pieces.empty()) {
    s += ",\n  \"pieces\": [";
    bool first = true;
    for (const auto& p : r.pieces) {
      sep(first);
      if (!p.gltf.empty()) s += "{ \"gltf\": \"" + p.gltf + "\"" + (p.scale != 1 ? ", \"scale\": " + f(p.scale) : std::string()) + ", \"at\": [" + f(p.at[0]) + ", " + f(p.at[1]) + ", " + f(p.at[2]) + "]";
      else s += "{ \"from\": \"" + p.from + "\", \"model\": " + std::to_string(p.model) + ", \"joint\": " + std::to_string(p.joint) + ", \"at\": [" + f(p.at[0]) + ", " + f(p.at[1]) + ", " + f(p.at[2]) + "]";
      if (p.turn != 0) s += ", \"turn\": " + f(p.turn);
      if (p.collide != "none") s += ", \"collide\": \"" + p.collide + "\", \"height\": " + f(p.height);
      if (!p.hash.empty()) s += ", \"hash\": \"" + p.hash + "\"";
      s += " }";
    }
    s += "\n  ]";
  }
  if (!r.wallsAdd.empty() || !r.boxes.empty() || !r.wallsRemove.empty()) {
    s += ",\n  \"walls\": {";
    bool any = false;
    if (!r.wallsAdd.empty()) {
      s += "\n    \"add\": [";
      bool first = true;
      for (const auto& w : r.wallsAdd) {
        s += first ? "\n      " : ",\n      ", first = false;
        s += "{ \"line\": [" + f(w.x0) + ", " + f(w.z0) + ", " + f(w.x1) + ", " + f(w.z1) + "], \"y\": " + f(w.y) + ", \"height\": " + f(w.height) + (w.flip ? ", \"flip\": true" : "") + " }";
      }
      s += "\n    ]", any = true;
    }
    if (!r.boxes.empty()) {
      s += any ? ",\n    \"boxes\": [" : "\n    \"boxes\": [";
      bool first = true;
      for (const auto& b : r.boxes) {
        s += first ? "\n      " : ",\n      ", first = false;
        s += "{ \"cx\": " + f(b.cx) + ", \"cz\": " + f(b.cz) + ", \"sx\": " + f(b.hx * 2) + ", \"sz\": " + f(b.hz * 2) + ", \"y\": " + f(b.y) + ", \"height\": " + f(b.height) + " }";
      }
      s += "\n    ]", any = true;
    }
    if (!r.wallsRemove.empty()) {
      s += any ? ",\n    \"remove\": [" : "\n    \"remove\": [";
      bool first = true;
      for (const auto& a : r.wallsRemove) {
        s += first ? "\n      " : ",\n      ", first = false;
        s += "{ \"area\": [" + f(a.x0) + ", " + f(a.z0) + ", " + f(a.x1) + ", " + f(a.z1) + "] }";
      }
      s += "\n    ]";
    }
    s += "\n  }";
  }
  if (r.entrancesGiven) {
    s += ",\n  \"entrances\": [";
    bool first = true;
    for (const auto& e : r.entrances) sep(first), s += "{ \"x\": " + f(e.x) + ", \"y\": " + f(e.y) + ", \"z\": " + f(e.z) + ", \"angle\": " + f(e.angle) + (e.exitOf.empty() ? std::string() : ", \"exit_of\": \"" + e.exitOf + "\"") + " }";
    s += "\n  ]";
  }
  if (r.cameraGiven) {
    s += ",\n  \"camera\": [";
    bool first = true;
    for (const auto& c : r.camera) {
      sep(first);
      s += "{ \"kind\": " + std::to_string(c.kind) + ", \"x\": " + f(c.pos[0]) + ", \"y\": " + f(c.pos[1]) + ", \"z\": " + f(c.pos[2]) + ", \"distance\": " + f(c.distance) + ", \"height\": " + f(c.height) +
           ", \"rotY\": " + f(c.rotY) + ", \"radius\": " + f(c.radius) + ", \"ratio\": " + f(c.ratio) + ", \"angle\": " + f(c.angle) + ", \"pow\": " + f(c.powValue) + ", \"blend\": " + f(c.blendTime) +
           ", \"fov\": " + f(c.fov) + " }";
    }
    s += "\n  ]";
  }
  return s + "\n}\n";
}

std::vector<uint8_t> EntranceBytes(const std::vector<RecipeEntrance>& e) {
  std::vector<uint8_t> out(e.size() * 16, 0);
  for (size_t i = 0; i < e.size(); i++) {
    uint8_t* p = out.data() + i * 16;
    const int a = int(std::lround(e[i].angle));
    p[0] = uint8_t((a >> 8) & 0xFF), p[1] = uint8_t(a & 0xFF);
    PutBeF(p + 4, e[i].x), PutBeF(p + 8, e[i].y), PutBeF(p + 12, e[i].z);
  }
  return out;
}

std::vector<uint8_t> CameraBytes(const std::vector<RecipeCamera>& c) {
  std::vector<uint8_t> out(c.size() * 0x34, 0);
  for (size_t i = 0; i < c.size(); i++) {
    uint8_t* p = out.data() + i * 0x34;
    p[0] = uint8_t(c[i].kind);
    const float v[11] = {c[i].pos[0], c[i].pos[1], c[i].pos[2], c[i].distance, c[i].height, c[i].rotY, c[i].radius, c[i].ratio, c[i].angle, c[i].powValue, c[i].blendTime};
    for (int k = 0; k < 11; k++) PutBeF(p + 4 + 4 * k, v[k]);
    PutBeF(p + 0x30, c[i].fov);
  }
  return out;
}

bool ApplyRecipe(const RoomRecipe& r, const RoomInputs& in, RoomOutputs& out, Problems& problems) {
  const size_t before = problems.size();
  const auto bad = [&](const std::string& s) { problems.push_back("room \"" + r.name + "\": " + s); };
  HsdArchive map;
  if (!HsdParse(in.map.data(), in.map.size(), map, problems)) return false;
  SceneInfo scene;
  if (!HsdListScene(map, scene, problems)) return false;
  if (!r.baseHash.empty() && in.hashOf) {
    const std::string h = in.hashOf(in.map);
    if (h != r.baseHash) bad("the base room's map is not the one the recipe was made for (its hash is " + h + ", the recipe says " + r.baseHash + "): a different disc version?");
  }
  Ccd ccd;
  std::string why;
  if (!CcdRead(in.collision.data(), in.collision.size(), ccd, why)) {
    bad("the base room's collision can not be read (" + why + ")");
    return false;
  }
  const auto joint = [&](int model, int j, const char* what) -> const JointInfo* {
    if (model < 0 || size_t(model) >= scene.models.size()) {
      bad(std::string(what) + ": the base map has " + std::to_string(scene.models.size()) + " model(s); there is no model " + std::to_string(model));
      return nullptr;
    }
    const ModelInfo& m = scene.models[size_t(model)];
    if (j < 0 || size_t(j) >= m.joints.size()) {
      bad(std::string(what) + ": model " + std::to_string(model) + " has " + std::to_string(m.joints.size()) + " joints; there is no joint " + std::to_string(j));
      return nullptr;
    }
    return &m.joints[size_t(j)];
  };
  char line[300];
  for (const RecipeHide& h : r.hide) {
    const JointInfo* j = joint(h.model, h.joint, "hide");
    if (j == nullptr) continue;
    const int n = HsdHideJoint(map, j->offset, h.children);
    std::snprintf(line, sizeof line, "hid joint %d of model %d (%d mesh list%s cleared)", h.joint, h.model, n, n == 1 ? "" : "s");
    out.report.push_back(line);
  }
  for (const RecipeMove& m : r.move) {
    const JointInfo* j = joint(m.model, m.joint, "move");
    if (j == nullptr) continue;
    HsdSetTranslation(map, j->offset, j->trans[0] + m.by[0], j->trans[1] + m.by[1], j->trans[2] + m.by[2]);
    if (m.turn != 0) HsdAddTurnY(map, j->offset, m.turn);
    size_t moved = 0;
    if (m.record >= 0) {
      if (size_t(m.record) >= ccd.records.size()) {
        bad("move: the collision has " + std::to_string(ccd.records.size()) + " record(s); there is no record " + std::to_string(m.record));
      } else {
        CcdRecord& rec = ccd.records[size_t(m.record)];
        float cx = 0, cz = 0;
        size_t n = 0;
        for (const CcdBlock& b : rec.block) {
          for (const CcdTri& t : b.tris) cx += t.v[0] + t.v[3] + t.v[6], cz += t.v[2] + t.v[5] + t.v[8], n += 3;
        }
        if (n != 0) cx /= float(n), cz /= float(n);
        CcdTransform(rec, 0, -cx, 0, -cz);
        CcdTransform(rec, m.turn, cx + m.by[0], m.by[1], cz + m.by[2]);
        for (const CcdBlock& b : rec.block) moved += b.tris.size();
      }
    } else if (m.hasArea) {
      const float x0 = std::min(m.area[0], m.area[2]), x1 = std::max(m.area[0], m.area[2]), z0 = std::min(m.area[1], m.area[3]), z1 = std::max(m.area[1], m.area[3]);
      const float cx = (x0 + x1) / 2, cz = (z0 + z1) / 2;
      for (CcdRecord& rec : ccd.records) {
        for (int kind = 0; kind < kCcdKinds; kind++) {
          if (kind == kCcdSun || kind == kCcdThru || kind == kCcdCheck) continue;
          for (CcdTri& t : rec.block[kind].tris) {
            bool inside = true;
            for (int i = 0; i < 3; i++) inside = inside && t.v[i * 3] >= x0 - 0.01f && t.v[i * 3] <= x1 + 0.01f && t.v[i * 3 + 2] >= z0 - 0.01f && t.v[i * 3 + 2] <= z1 + 0.01f;
            if (!inside) continue;
            CcdRecord tmp;
            tmp.block[0].present = true;
            tmp.block[0].tris.push_back(t);
            CcdTransform(tmp, 0, -cx, 0, -cz);
            CcdTransform(tmp, m.turn, cx + m.by[0], m.by[1], cz + m.by[2]);
            t = tmp.block[0].tris[0];
            moved++;
          }
        }
      }
    }
    std::snprintf(line, sizeof line, "moved joint %d of model %d by (%g, %g, %g)%s%s", m.joint, m.model, m.by[0], m.by[1], m.by[2], m.turn != 0 ? " and turned it" : "",
                  moved != 0 ? (", with " + std::to_string(moved) + " collision triangle(s)").c_str() : "");
    out.report.push_back(line);
  }
  std::map<std::string, std::vector<uint8_t>> sources;
  struct ParsedSource { HsdArchive arc; SceneInfo scene; };
  std::map<std::string, ParsedSource> parsedSources;
  HsdPieceCache pieceCache;
  std::vector<RecipeBox> pendingBoxes;
  std::vector<std::array<float, 9>> pendingTris;
  std::map<std::string, MeshModel> parts;
  for (const RecipePiece& p : r.pieces) {
    if (!p.gltf.empty()) {
      const std::string what = "own model " + p.gltf;
      auto pit = parts.find(p.gltf);
      if (pit == parts.end()) {
        std::vector<uint8_t> bytes;
        if (!in.readPart || !in.readPart(p.gltf, bytes)) {
          bad(what + ": the file is not in the room's folder");
          continue;
        }
        MeshModel mm;
        std::string why;
        if (!GltfRead(bytes.data(), bytes.size(), mm, why)) {
          bad(what + ": " + why);
          continue;
        }
        pit = parts.emplace(p.gltf, std::move(mm)).first;
      }
      std::string problem;
      uint32_t added = 0;
      if (!HsdAddMeshModel(map, pit->second, p.scale, p.turn, p.at[0], p.at[1], p.at[2], problem, nullptr, &added)) {
        bad(what + ": " + problem);
        continue;
      }
      out.pieces++;
      out.textureBytesAdded += added;
      size_t ntri = 0;
      for (const MeshPrim& mp : pit->second.prims) ntri += mp.idx.size() / 3;
      std::snprintf(line, sizeof line, "added your model %s at (%g, %g, %g): %zu triangles, %u bytes", p.gltf.c_str(), p.at[0], p.at[1], p.at[2], ntri, added);
      out.report.push_back(line);
      if (p.collide != "none") {
        const auto tris = MeshWorldTriangles(pit->second, p.scale, p.turn, p.at[0], p.at[1], p.at[2]);
        if (p.collide == "mesh") {
          pendingTris.insert(pendingTris.end(), tris.begin(), tris.end());
        } else {
          float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
          for (const auto& t : tris) {
            for (int i = 0; i < 3; i++) {
              for (int k = 0; k < 3; k++) lo[k] = std::min(lo[k], t[size_t(i * 3 + k)]), hi[k] = std::max(hi[k], t[size_t(i * 3 + k)]);
            }
          }
          const float h = p.height > 0 ? p.height : std::max(hi[1] - lo[1], 2.f);
          pendingBoxes.push_back({(lo[0] + hi[0]) / 2, (lo[2] + hi[2]) / 2, (hi[0] - lo[0]) / 2, (hi[2] - lo[2]) / 2, lo[1], h});
          std::snprintf(line, sizeof line, "  its collision: a box %.1f x %.1f around (%.1f, %.1f), %.1f high", hi[0] - lo[0], hi[2] - lo[2], (lo[0] + hi[0]) / 2, (lo[2] + hi[2]) / 2, h);
          out.report.push_back(line);
        }
      }
      continue;
    }
    const std::string what = "piece from " + p.from + " (model " + std::to_string(p.model) + ", joint " + std::to_string(p.joint) + ")";
    if (!in.sourceMap) {
      bad(what + ": this tool can not read other rooms");
      continue;
    }
    auto it = sources.find(p.from);
    if (it == sources.end()) {
      std::vector<uint8_t> bytes;
      if (!in.sourceMap(p.from, bytes)) {
        bad(what + ": no room of the disc is called " + p.from);
        continue;
      }
      it = sources.emplace(p.from, std::move(bytes)).first;
    }
    if (!p.hash.empty() && in.hashOf && in.hashOf(it->second) != p.hash) {
      bad(what + ": that room's map is not the one the recipe was made for (a different disc version?)");
      continue;
    }
    auto parsed = parsedSources.find(p.from);
    if (parsed == parsedSources.end()) {
      ParsedSource ps;
      std::vector<std::string> sp;
      if (!HsdParse(it->second.data(), it->second.size(), ps.arc, sp) || !HsdListScene(ps.arc, ps.scene, sp)) {
        bad(what + ": the source map can not be read" + (sp.empty() ? "" : " (" + sp[0] + ")"));
        continue;
      }
      parsed = parsedSources.emplace(p.from, std::move(ps)).first;
    }
    const HsdArchive& src = parsed->second.arc;
    const SceneInfo& ss = parsed->second.scene;
    if (p.model < 0 || size_t(p.model) >= ss.models.size() || p.joint < 0 || size_t(p.joint) >= ss.models[size_t(p.model)].joints.size()) {
      bad(what + ": no such model or joint in that room");
      continue;
    }
    std::string problem;
    uint32_t added = 0;
    const uint32_t pieceRoot = ss.models[size_t(p.model)].joints[size_t(p.joint)].offset;
    float lo[3] = {}, hi[3] = {};
    const bool haveBounds = HsdSubtreeBounds(src, pieceRoot, lo, hi);
    const float ta = p.turn * 3.14159265358979f / 180.f, tc = std::cos(ta), ts = std::sin(ta);
    float place[3] = {p.at[0], p.at[1], p.at[2]};
    if (haveBounds) {
      const float mx = (lo[0] + hi[0]) / 2, mz = (lo[2] + hi[2]) / 2;
      place[0] = p.at[0] - (mx * tc + mz * ts), place[1] = p.at[1] - lo[1], place[2] = p.at[2] - (-mx * ts + mz * tc);
    }
    if (!HsdAddPiece(map, src, pieceRoot, place[0], place[1], place[2], p.turn, problem, nullptr, &added, &pieceCache)) {
      bad(what + ": " + problem);
      continue;
    }
    out.pieces++;
    out.textureBytesAdded += added;
    std::snprintf(line, sizeof line, "added a piece from %s (model %d, joint %d) at (%g, %g, %g): %u bytes", p.from.c_str(), p.model, p.joint, p.at[0], p.at[1], p.at[2], added);
    out.report.push_back(line);
    if (p.collide == "mesh") {
      bad(what + ": \"collide\": \"mesh\" is not built yet; use \"box\" (a box round the piece) or \"none\" and your own walls");
    } else if (p.collide == "box") {
      if (!haveBounds) {
        bad(what + ": no vertices could be read from the piece, so no box can be made round it; use \"collide\": \"none\"");
      } else {
        float x0 = 1e30f, x1 = -1e30f, z0 = 1e30f, z1 = -1e30f;
        const float mx = (lo[0] + hi[0]) / 2, mz = (lo[2] + hi[2]) / 2;
        for (int k = 0; k < 4; k++) {
          const float x = ((k & 1) ? hi[0] : lo[0]) - mx, z = ((k & 2) ? hi[2] : lo[2]) - mz;
          const float rx = x * tc + z * ts, rz = -x * ts + z * tc;
          x0 = std::min(x0, rx), x1 = std::max(x1, rx), z0 = std::min(z0, rz), z1 = std::max(z1, rz);
        }
        const float h = p.height > 0 ? p.height : std::max(hi[1] - lo[1], 2.f);
        pendingBoxes.push_back({p.at[0], p.at[2], (x1 - x0) / 2, (z1 - z0) / 2, p.at[1], h});
        std::snprintf(line, sizeof line, "  its collision: a box %.1f x %.1f around (%.1f, %.1f), %.1f high", x1 - x0, z1 - z0, p.at[0], p.at[2], h);
        out.report.push_back(line);
      }
    }
  }
  size_t removed = 0;
  for (const RecipeArea& a : r.wallsRemove) removed += CcdRemoveHitIn(ccd, a.x0, a.z0, a.x1, a.z1);
  if (!r.wallsRemove.empty()) out.report.push_back("removed " + std::to_string(removed) + " wall triangle(s)");
  std::vector<RecipeBox> allBoxes = r.boxes;
  allBoxes.insert(allBoxes.end(), pendingBoxes.begin(), pendingBoxes.end());
  if (!r.wallsAdd.empty() || !allBoxes.empty() || !pendingTris.empty()) {
    size_t best = 0, bestN = 0;
    bool found = false;
    for (size_t i = 0; i < ccd.records.size(); i++) {
      if (ccd.records[i].block[kCcdHit].present && (!found || ccd.records[i].block[kCcdHit].tris.size() > bestN)) best = i, bestN = ccd.records[i].block[kCcdHit].tris.size(), found = true;
    }
    if (!found) {
      if (ccd.records.size() >= kCcdMaxRecords) bad("walls: the collision already has 64 records");
      else ccd.records.push_back(CcdNewRecord()), best = ccd.records.size() - 1;
    }
    CcdRecord& rec = ccd.records[best];
    for (const RecipeWall& w : r.wallsAdd) CcdAddWall(rec, w.x0, w.z0, w.x1, w.z1, w.y, w.y + w.height, w.flip);
    for (const RecipeBox& b : allBoxes) CcdAddBox(rec, b.cx, b.cz, b.hx, b.hz, b.y, b.y + b.height);
    out.report.push_back("added " + std::to_string(r.wallsAdd.size()) + " wall(s) and " + std::to_string(allBoxes.size()) + " box(es) to collision record " + std::to_string(best));
    if (!pendingTris.empty()) {
      if (!rec.block[kCcdWalk].present) rec.block[kCcdWalk].present = true;
      const MeshCollisionCount mc = CcdAddMeshTriangles(rec, pendingTris);
      out.report.push_back("added the collision of your model(s) to record " + std::to_string(best) + ": " + std::to_string(mc.walls) + " wall triangle(s), " + std::to_string(mc.floor) +
                           " ground triangle(s)" + (mc.skipped != 0 ? ", " + std::to_string(mc.skipped) + " empty one(s) left out" : std::string()));
    }
  }
  if (problems.size() != before) return false;
  out.map = HsdWrite(map);
  out.collision = CcdWrite(ccd);
  std::vector<std::string> v;
  if (!HsdValidateMap(out.map.data(), out.map.size(), v) || !CcdValidate(out.collision.data(), out.collision.size(), v)) {
    for (const std::string& s : v) bad("the result is not usable: " + s);
    return false;
  }
  out.mapBytes = uint32_t(out.map.size()), out.collisionBytes = uint32_t(out.collision.size());
  if (in.maxArchiveBytes != 0 && in.baseArchiveBytes != 0) {
    const uint64_t total = uint64_t(in.baseArchiveBytes) + out.map.size() + out.collision.size();
    if (total > in.maxArchiveBytes) {
      bad("the room would be " + std::to_string(total) + " bytes; the largest room of the game is " + std::to_string(in.maxArchiveBytes) + " (too much for the game's memory)");
      return false;
    }
  }
  return true;
}

}
