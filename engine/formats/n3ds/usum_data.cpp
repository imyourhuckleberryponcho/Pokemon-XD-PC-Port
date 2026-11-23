#include "n3ds_data.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <regex>
#include <sstream>

#include "usum_data_internal.h"

namespace fs = std::filesystem;

namespace n3ds {

void PackFiles::write(const std::string& rel, const std::string& text, bool binary_lf, bool ansi) const {
  fs::path p = fs::path(root) / rel;
  fs::create_directories(p.parent_path());
  std::string t = text;
  if (crlf && ansi) {
    std::u32string u = to_u32(t);
    std::string o;
    for (char32_t c : u) o += c < 0x100 ? (char)c : '?';
    t.swap(o);
  }
  if (crlf && !binary_lf) {
    std::string o;
    o.reserve(t.size() + t.size() / 20);
    for (char c : t) { if (c == '\n') o += '\r'; o += c; }
    t.swap(o);
  }
  std::ofstream f(p, std::ios::binary);
  f.write(t.data(), (std::streamsize)t.size());
  if (!f) throw Fail("cannot write " + p.string());
}
void PackFiles::write_bytes(const std::string& rel, const Bytes& b) const {
  fs::path p = fs::path(root) / rel;
  fs::create_directories(p.parent_path());
  std::ofstream f(p, std::ios::binary);
  f.write((const char*)b.data(), (std::streamsize)b.size());
  if (!f) throw Fail("cannot write " + p.string());
}
std::string slurp_text(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) throw Fail("cannot read " + path);
  std::string s((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  return s;
}
Bytes slurp_bytes(const std::string& path) {
  std::string s = slurp_text(path);
  return Bytes(s.begin(), s.end());
}
Json read_json(const std::string& path) { return Json::parse(slurp_text(path)); }
std::string slurp_lines_lf(const std::string& path) {
  std::string s = slurp_text(path), o;
  for (size_t i = 0; i < s.size(); i++) {
    if (s[i] == '\r') { if (i + 1 < s.size() && s[i + 1] == '\n') continue; o += '\n'; continue; }
    o += s[i];
  }
  return o;
}

const Json& jget(const Json& o, const char* k) {
  const Json* v = o.find(k);
  if (!v) throw Fail(std::string("a table is missing the key ") + k);
  return *v;
}
bool jtruthy(const Json* v) {
  if (!v) return false;
  switch (v->kind) {
    case Json::Null: return false;
    case Json::Bool: return v->b;
    case Json::Int: return v->i != 0;
    case Json::Num: return std::strtod(v->s.c_str(), nullptr) != 0;
    case Json::Str: return !v->s.empty();
    case Json::Arr: return !v->a.empty();
    case Json::Obj: return !v->o.empty();
  }
  return false;
}
Json jints(const std::vector<int>& v) {
  Json a = Json::array();
  for (int x : v) a.push(Json(x));
  return a;
}
Json jstrs(const std::vector<std::string>& v) {
  Json a = Json::array();
  for (auto& x : v) a.push(Json(x));
  return a;
}

static std::string run_capture(const std::string& cmd, int* code) {
  std::string full = cmd;
#ifdef _WIN32
  full = "\"" + cmd + "\"";
  FILE* p = _popen(full.c_str(), "rb");
#else
  FILE* p = popen(full.c_str(), "r");
#endif
  if (!p) throw Fail("xd_data_mods could not be started");
  std::string out;
  char buf[65536];
  size_t n;
  while ((n = fread(buf, 1, sizeof buf, p)) > 0) out.append(buf, n);
#ifdef _WIN32
  *code = _pclose(p);
#else
  *code = pclose(p);
#endif
  return out;
}

static int parse_show(const std::vector<std::string>& lines, Json& out) {
  out = Json::object();
  const std::string& h = lines[0];
  int index = -1;
  std::string name;
  size_t hp = h.find(" #");
  if (hp != std::string::npos) {
    size_t q = hp + 2, e = q;
    while (e < h.size() && isdigit((unsigned char)h[e])) e++;
    if (e > q) {
      index = atoi(h.substr(q, e - q).c_str());
      std::string rest = h.substr(e);
      size_t a = rest.find(" ("), b = rest.rfind(')');
      if (a != std::string::npos && b != std::string::npos && b > a) name = rest.substr(a + 2, b - a - 2);
    }
  }
  for (size_t i = 1; i < lines.size(); i++) {
    std::string ln = py_strip(lines[i]);
    size_t w = 0;
    while (w < ln.size() && (isalnum((unsigned char)ln[w]) || ln[w] == '_')) w++;
    if (w == 0 || w >= ln.size() || !isspace((unsigned char)ln[w])) continue;
    std::string key = ln.substr(0, w), rest = py_strip(ln.substr(w));
    if (rest.empty()) continue;
    Json v;
    if (rest[0] == '[' && rest.rfind(']') != std::string::npos) {
      v = Json::parse(rest.substr(0, rest.rfind(']') + 1));
    } else {
      size_t k = rest[0] == '-' ? 1 : 0, e = k;
      while (e < rest.size() && isdigit((unsigned char)rest[e])) e++;
      if (e > k) v = Json((int64_t)std::stoll(rest.substr(0, e)));
      else v = Json(rest);
    }
    out.set(key, v);
  }
  out.set("_name", Json(name));
  return index;
}

static std::map<int, Json> show_range(const std::string& exe, const std::string& disc, const char* table, int first, int last, const std::string& settings) {
#ifdef _WIN32
  _putenv_s("XD_SETTINGS_DIR", settings.c_str());
#else
  setenv("XD_SETTINGS_DIR", settings.c_str(), 1);
#endif
  int code = 0;
  std::string text = run_capture("\"" + exe + "\" \"" + disc + "\" --show-range " + table + " " + std::to_string(first) + " " + std::to_string(last), &code);
  if (code) throw Fail("XD's tables could not be read from your XD disc (xd_data_mods, exit " + std::to_string(code) + ").");
  std::map<int, Json> out;
  std::vector<std::string> cur;
  auto flush = [&]() {
    if (cur.empty()) return;
    Json j;
    int i = parse_show(cur, j);
    out[i] = j;
    cur.clear();
  };
  std::istringstream is(text);
  std::string ln;
  while (std::getline(is, ln)) {
    if (!ln.empty() && ln.back() == '\r') ln.pop_back();
    if (!ln.empty() && !isspace((unsigned char)ln[0])) { flush(); cur.push_back(ln); }
    else if (!cur.empty()) cur.push_back(ln);
  }
  flush();
  return out;
}

XdTablesCache load_xd_tables(const std::string& exe, const std::string& disc, const std::string& work) {
  XdTablesCache c;
  fs::create_directories(work);
  const std::string mc = work + "/xd_moves_cache.json", tm = work + "/xd_machines.json";
  if (fs::exists(mc) && fs::exists(tm)) {
    c.moves = jget(read_json(mc), "xd");
    Json m = read_json(tm);
    for (auto& x : jget(m, "tm_moves").a) c.tm_moves.push_back((int)x.i);
    for (auto& x : jget(m, "tutors").a) c.tutors.push_back((int)x.i);
    return c;
  }
  if (exe.empty() || !fs::exists(exe)) throw Fail("xd_data_mods (it ships beside usum_models) was not found, so XD's tables cannot be read.");
  const std::string settings = work + "/dm_settings";
  fs::create_directories(settings);
  auto moves = show_range(exe, disc, "waza_data", 0, 374, settings);
  auto tms = show_range(exe, disc, "tm_moves", 0, 57, settings);
  auto tut = show_range(exe, disc, "wazaoshie_data", 0, 11, settings);
  if (moves.size() != 375 || tms.size() != 58 || tut.size() != 12)
    throw Fail("XD's move tables read from your XD disc have an unexpected size (" + std::to_string(moves.size()) + " / " + std::to_string(tms.size()) + " / " +
               std::to_string(tut.size()) + ").");
  Json mj = Json::object();
  for (auto& kv : moves) mj.set(std::to_string(kv.first), kv.second);
  Json top = Json::object();
  top.set("xd", mj);
  Json t2 = Json::object(), a = Json::array(), b = Json::array();
  for (int i = 0; i < 58; i++) { a.push(jget(tms[i], "wazaID")); c.tm_moves.push_back((int)jget(tms[i], "wazaID").i); }
  for (int i = 0; i < 12; i++) { b.push(jget(tut[i], "id")); c.tutors.push_back((int)jget(tut[i], "id").i); }
  t2.set("tm_moves", a);
  t2.set("tutors", b);
  PackFiles w{work, false};
  w.write("xd_moves_cache.json", top.dump(-1));
  w.write("xd_machines.json", t2.dump(-1));
  c.moves = mj;
  return c;
}

namespace {

int type_xd(int t) {
  static const int T[18] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 10, 11, 12, 13, 14, 15, 16, 17, -1};
  return t >= 0 && t < 18 ? T[t] : -1;
}

struct MoveTables {
  const Json& t;
  std::set<std::string> features_done, ok_pure, partial_ok;
  explicit MoveTables(const Json& all) : t(jget(all, "moves")) {
    for (auto& x : jget(t, "features_done").a) features_done.insert(x.s);
    for (auto& x : jget(t, "ok_pure").a) ok_pure.insert(x.s);
    for (auto& x : jget(t, "partial_ok").a) partial_ok.insert(x.s);
  }
  bool missing_feature(const Json* needs) const {
    if (!needs) return false;
    for (auto& n : needs->a) if (!features_done.count(n.s)) return true;
    return false;
  }
};

bool is_z(int mid) { return (mid >= 622 && mid <= 658) || (mid >= 695 && mid <= 703) || mid == 719 || (mid >= 723 && mid <= 728); }

std::string ascii_only(const std::string& name) {
  std::string o;
  for (char32_t c : to_u32(name)) o += (c >= 0x20 && c <= 0x7e) ? (char)c : '\'';
  return o;
}

struct Blocks { std::vector<Json> v; std::vector<std::string> notes; };

Blocks derive_blocks(const MoveRec& m, const Json& stat_map, const Json& inflict_map) {
  Blocks r;
  const bool status = m.power == 0;
  for (int k = 0; k < 3; k++) {
    const int stat = m.stats[k][0], stages = m.stats[k][1], pct = m.stats[k][2];
    if (stat == 0 || stages == 0) continue;
    const Json* sn = stat_map.find(std::to_string(stat));
    if (!sn) { r.notes.push_back("stat group " + std::to_string(stat) + " is not data-driven"); continue; }
    const bool user = ((m.quality == 7) || (status && (m.target == 6 || m.target == 7))) && !(m.quality == 6);
    Json b = Json::object();
    b.set("stat", *sn);
    b.set("stages", Json(stages));
    b.set("target", Json(user ? "user" : "target"));
    if (pct > 0 && pct < 100) b.set("chance", Json(pct));
    r.v.push_back(b);
  }
  const Json* inf = inflict_map.find(std::to_string(m.inflict));
  if (inf && (m.quality == 1 || m.quality == 4 || m.quality == 5)) {
    Json b = Json::object();
    b.set("status", *inf);
    b.set("target", Json("target"));
    if (m.inflict_percent > 0 && m.inflict_percent < 100) b.set("chance", Json(m.inflict_percent));
    r.v.push_back(b);
  }
  if (m.flinch) {
    Json b = Json::object();
    b.set("status", Json("flinch"));
    b.set("target", Json("target"));
    if (m.flinch < 100) b.set("chance", Json(m.flinch));
    r.v.push_back(b);
  }
  if (m.heal > 0 && status) {
    Json b = Json::object();
    b.set("heal", Json(m.heal));
    b.set("target", Json((m.target == 6 || m.target == 7) ? "user" : "target"));
    r.v.push_back(b);
  }
  if (m.recoil < 0) { Json b = Json::object(); b.set("recoil", Json(-m.recoil)); r.v.push_back(b); }
  else if (m.recoil > 0 && !status) { Json b = Json::object(); b.set("drain", Json(m.recoil)); r.v.push_back(b); }
  return r;
}

Json strip_needs(const Json& b) {
  Json o = Json::object();
  for (auto& kv : b.o) if (kv.first != "needs") o.set(kv.first, kv.second);
  return o;
}

}

MovesBuilt build_all_moves(Usum& u, const Json& tables, const Json& xd_cache, const std::vector<std::string>& langs) {
  MoveTables T(tables);
  const Json& mt = T.t;
  const Json& plans = jget(mt, "plan");
  const Json& stat_map = jget(mt, "stat");
  const Json& inflict_map = jget(mt, "inflict");
  const Json& anim_map = jget(mt, "anim");
  const Json& anim_phys = jget(mt, "anim_phys");
  const Json& anim_spec = jget(mt, "anim_spec");
  const Json& anim_status = jget(mt, "anim_status");
  const Json& range_map = jget(mt, "range");
  const Json& force_partial = jget(mt, "force_partial");
  const Json& partial = jget(mt, "partial");
  const Json& feature_of_prop = jget(mt, "feature_of_prop");
  std::map<std::vector<int>, std::vector<int>> groups;
  for (int i = 1; i < 355; i++) groups[u.move(i).signature()].push_back(i);

  MovesBuilt out;
  out.rows.resize(langs.size());
  out.battle = Json::object();
  out.report = Json::array();
  std::set<std::string> used_keys;
  auto xd_field = [&](int i, const char* f) -> int64_t { const Json* v = jget(xd_cache, std::to_string(i).c_str()).find(f); return v ? v->i : 0; };

  for (int mid = 468; mid <= 728; mid++) {
    const MoveRec& m = u.move(mid);
    std::string name = ascii_only(u.move_name(mid, "en"));
    std::string key = name;
    int again = 2;
    while (used_keys.count(key)) key = name + " (" + std::to_string(again++) + ")";
    used_keys.insert(key);
    static const Json kEmpty = Json::object();
    const Json* planp = plans.find(name);
    const Json& plan = planp ? *planp : kEmpty;
    const bool zmove = is_z(mid);
    Blocks bl = derive_blocks(m, stat_map, inflict_map);
    std::vector<Json> blocks = bl.v;
    std::vector<std::string> notes = bl.notes;
    std::string donor, status = "full";
    const std::vector<int> sig = m.signature();
    auto git = groups.find(sig);
    const bool sig_in = git != groups.end();
    const Json* pblocks = plan.find("blocks");
    if (plan.find("donor")) donor = plan.find("donor")->s;
    else if (sig_in && m.power > 0 && !jtruthy(pblocks) && !zmove && m.recoil >= 0) {
      int best = -1;
      double bs = 0;
      for (int i : git->second) {
        const int64_t kavg = xd_field(i, "addFightKoukaAvg");
        double s = 0;
        bool any_pct = false;
        for (int k = 0; k < 3; k++) if (m.stats[k][2] && kavg == m.stats[k][2]) any_pct = true;
        if (kavg == m.inflict_percent || kavg == (int64_t)m.flinch || any_pct) s += 4;
        if (xd_field(i, "dageki") == (m.contact() ? 1 : 0)) s += 2;
        if (type_xd(m.type) == (int)xd_field(i, "zokuseiDataId")) s += 1;
        if (xd_field(i, "pad37") == 0 && m.category == ((xd_field(i, "zokuseiDataId") >= 0 && xd_field(i, "zokuseiDataId") < 9) ? 1 : 2)) s += 0.5;
        s = s - (double)i / 10000.0;
        if (best < 0 || s > bs) { best = i; bs = s; }
      }
      donor = jget(jget(xd_cache, std::to_string(best).c_str()), "_name").s;
      blocks.clear();
    } else if (m.power > 0 && (m.hits == 0x52)) { donor = "Fury Swipes"; blocks.clear(); }
    else if (m.power > 0 && (m.hits == 0x22)) { donor = "Double Kick"; blocks.clear(); }
    else if (m.power > 0) donor = "Pound";
    else { const Json* d = plan.find("donor_status"); donor = d ? d->s : jget(mt, "donor_status_default").s; }
    if (pblocks) { blocks.clear(); for (auto& b : pblocks->a) blocks.push_back(b); }
    if (jtruthy(plan.find("no_blocks"))) blocks.clear();
    std::vector<std::string> missing;
    if (const Json* nd = plan.find("needs")) {
      std::set<std::string> ms;
      for (auto& n : nd->a) if (!T.features_done.count(n.s)) ms.insert(n.s);
      missing.assign(ms.begin(), ms.end());
    }
    if (!missing.empty()) {
      status = m.power > 0 ? "partial" : "name only";
      if (m.power > 0 && blocks.empty()) status = "damage only";
      std::string j;
      for (size_t i = 0; i < missing.size(); i++) j += (i ? ", " : "") + missing[i];
      notes.push_back("missing: " + j);
      std::vector<Json> nb;
      for (auto& b : blocks) if (!T.missing_feature(b.find("needs"))) nb.push_back(b);
      blocks.swap(nb);
    }
    for (auto& b : blocks) b = strip_needs(b);
    if (zmove) {
      status = "damage only";
      notes.push_back("Z-move: not usable in this game (no Z-crystals); the row exists so move numbers stay in step");
    }
    const Json* pnote = plan.find("note");
    if (jtruthy(pnote)) notes.push_back(pnote->s);
    if (m.power == 0 && blocks.empty() && !jtruthy(plan.find("own_effect")) && !plan.find("donor")) status = "name only";
    if (const Json* fp = force_partial.find(name)) {
      if (status == "full") { status = "partial"; notes.push_back(fp->s); }
    }
    if (status == "full" && jtruthy(plan.find("own_effect")) && jtruthy(pnote) && !zmove) status = "partial";
    const Json* pp = partial.find(name);
    const bool in_ok_extra = T.partial_ok.count(name) != 0;
    auto drop_effect_notes = [&]() {
      std::vector<std::string> n2;
      for (auto& n : notes) if (n.compare(0, 7, "effect ") != 0) n2.push_back(n);
      notes.swap(n2);
    };
    if (pp && (status == "full" || status == "damage only" || status == "partial" || status == "name only") && !zmove) {
      status = "partial";
      drop_effect_notes();
      notes.push_back(pp->s);
    } else if (in_ok_extra && !zmove && status == "damage only") {
      status = "full";
      drop_effect_notes();
    }
    const bool matched = sig_in && m.power > 0 && !jtruthy(pblocks) && m.recoil >= 0;
    if (status == "full" && !zmove && !matched && !jtruthy(plan.find("ok")) && !T.ok_pure.count(name) && !in_ok_extra && m.effect_seq >= 277 &&
        !(jtruthy(plan.find("needs")) && missing.empty())) {
      status = !blocks.empty() ? "partial" : (m.power > 0 ? "damage only" : "name only");
      notes.push_back("effect " + std::to_string(m.effect_seq) + " of the dump is not built");
    }
    const Json* ppr = plan.find("power_rules");
    const Json* pacc = plan.find("accuracy_rules");
    const Json* pprops = plan.find("props");
    if (!blocks.empty() || jtruthy(ppr) || jtruthy(pacc) || jtruthy(pprops)) {
      Json e = Json::object();
      if (!blocks.empty()) { Json a = Json::array(); for (auto& b : blocks) a.push(b); e.set("effects", a); }
      Json pr = Json::array();
      if (ppr) for (auto& r : ppr->a) if (!T.missing_feature(r.find("needs"))) pr.push(strip_needs(r));
      if (!pr.a.empty()) e.set("power_rules", pr);
      if (jtruthy(pacc)) e.set("accuracy_rules", *pacc);
      if (jtruthy(pprops)) {
        for (auto& kv : pprops->o) {
          const Json* fo = feature_of_prop.find(kv.first);
          if (fo && !T.features_done.count(fo->s)) {
            if (status == "full") status = "partial";
            notes.push_back("missing: " + fo->s);
            continue;
          }
          e.set(kv.first, kv.second);
        }
      }
      if (!e.o.empty()) out.battle.set(key, e);
    }
    auto status_kind = [&]() -> std::string {
      bool up = false, down = false;
      for (int k = 0; k < 3; k++) { if (m.stats[k][1] > 0) up = true; if (m.stats[k][1] < 0) down = true; }
      return up ? anim_status.a.at(0).s : down ? anim_status.a.at(1).s : anim_status.a.at(2).s;
    };
    for (size_t li = 0; li < langs.size(); li++) {
      const int q = type_xd(m.type);
      Json d = Json::object();
      d.set("copy", Json(donor));
      d.set("pp", Json(m.pp));
      d.set("zokuseiDataId", Json(q >= 0 ? q : 0));
      d.set("iryoku", Json(m.power));
      int avg = m.accuracy;
      if (m.accuracy == 0 || m.accuracy == 101)
        avg = (m.power > 0 || m.target == 6 || m.target == 7 || m.target == 8 || m.target == 10 || m.target == 11 || m.target == 12) ? 0 : 100;
      d.set("avg", Json(avg));
      d.set("pri", Json(m.priority & 0xFF));
      d.set("pad37", Json(m.category == 0 ? 0 : m.category == 1 ? 1 : 2));
      const Json* rg = range_map.find(std::to_string(m.target));
      d.set("rangeId", Json(rg ? (int)rg->i : 0));
      const int f = m.flags;
      d.set("dageki", Json((f & 1) ? 1 : 0));
      d.set("mamoru", Json((f & 8) ? 1 : 0));
      d.set("majikku", Json((f & 0x10) ? 1 : 0));
      d.set("yokodori", Json((f & 0x20) ? 1 : 0));
      d.set("oumugaesi", Json((f & 0x40) ? 1 : 0));
      d.set("bouon", Json((f & 0x100) ? 1 : 0));
      d.set("oujanosirusi", Json((m.power > 0 && m.flinch == 0) ? 1 : 0));
      int chance = m.inflict_percent ? m.inflict_percent : m.flinch ? m.flinch : std::max({m.stats[0][2], m.stats[1][2], m.stats[2][2], 0});
      d.set("addFightKoukaAvg", Json((chance && m.power > 0) ? chance : 0));
      if (zmove) d.set("yubiwohuru", Json(0));
      std::string anim;
      std::string stk = plan.find("anim_status") ? plan.find("anim_status")->s : status_kind();
      if (const Json* an = anim_map.find(name)) anim = an->s;
      else if (m.power == 0) anim = stk;
      else {
        const Json& table = m.category == 1 ? anim_phys : anim_spec;
        const Json* a = table.find(std::to_string(m.type));
        anim = a ? a->s : jget(table, "0").s;
      }
      d.set("animation", Json(anim));
      Json text = Json::object();
      text.set("name", Json(move_upper(u.move_name(mid, langs[li]))));
      text.set("doc", Json(move_desc(u.move_desc_text(mid, langs[li]))));
      d.set("text", text);
      if (m.type == 17) d.set("_fairy", Json(true));
      out.rows[li].push_back({key, d});
    }
    Json r = Json::object();
    r.set("id", Json(mid));
    r.set("row", Json(mid + 20));
    r.set("name", Json(key));
    r.set("type", Json(m.type));
    r.set("power", Json(m.power));
    r.set("status", Json(status));
    r.set("donor", Json(donor));
    r.set("effects", Json((int)blocks.size()));
    r.set("notes", jstrs(notes));
    out.report.push(r);
  }
  return out;
}

void build_all_abilities(Usum& u, const Json& tables, Json& defs, Json& report) {
  const Json& at = jget(tables, "abilities");
  const Json& table = jget(at, "table");
  const Json& nb = jget(at, "not_built_why");
  const Json& ft = jget(at, "form_triggers");
  defs = Json::object();
  report = Json::array();
  for (int aid = 124; aid < u.ability_count(); aid++) {
    const std::string name = u.ability_name(aid, "en");
    const Json* row = table.find(std::to_string(aid));
    Json r = Json::object();
    if (row) {
      Json d = Json::object();
      d.set("id", Json(aid));
      for (auto& kv : row->a.at(1).o) d.set(kv.first, kv.second);
      defs.set(row->a.at(0).s, d);
      r.set("id", Json(aid));
      r.set("name", Json(name));
      r.set("status", row->a.at(2));
      r.set("note", row->a.at(3));
    } else {
      const Json* why = nb.find(std::to_string(aid));
      r.set("id", Json(aid));
      r.set("name", Json(name));
      r.set("status", Json("none"));
      r.set("note", why ? *why : Json("not built"));
    }
    if (const Json* f = ft.find(std::to_string(aid))) r.set("form_trigger", *f);
    report.push(r);
  }
}

std::string ability_lines(Usum& u, const Json& tables, const std::vector<int>& ids, const std::set<int>& built) {
  static const char* langs[] = {"en", "de", "fr", "it", "es", "ja"};
  const Json& ne = jget(jget(tables, "all"), "no_effect");
  std::string out = "format 1";
  for (int a : ids) {
    std::string line = std::to_string(a);
    for (const char* l : langs) {
      std::string d = desc_text(u.ability_desc(a, l));
      if (!built.count(a)) d += jget(ne, l).s;
      line += "\t" + shown(u.ability_name(a, l)) + "\t" + d;
    }
    out += "\n" + line;
  }
  return out + "\n";
}

std::vector<std::pair<int, int>> build_hidden(Usum& u, int last) {
  std::vector<std::pair<int, int>> out;
  for (int nat = 1; nat <= last; nat++) {
    Personal p = u.personal(nat);
    const int h = p.abilities[2];
    if (h == 0 || h == p.abilities[0] || h == p.abilities[1] || h >= u.ability_count()) continue;
    out.push_back({nat, h});
  }
  if (last >= 718 && std::none_of(out.begin(), out.end(), [](const std::pair<int, int>& e) { return e.first == 718; })) out.push_back({718, 211});
  std::sort(out.begin(), out.end());
  return out;
}

}
