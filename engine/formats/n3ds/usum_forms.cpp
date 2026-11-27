#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <sstream>

#include "usum_data_internal.h"

namespace fs = std::filesystem;

namespace n3ds {

namespace {
const char* kLangs[6] = {"us", "ge", "fr", "it", "sp", "jp"};
const char* kDumpLang[6] = {"en", "de", "fr", "it", "es", "ja"};
constexpr int FIRST_NEW_PKX_MIN = 524;
constexpr uint32_t PKX_RES_ID = 0x00011E00;

int type_xd(int t) {
  static const int T[18] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 10, 11, 12, 13, 14, 15, 16, 17, -1};
  return t >= 0 && t < 18 ? T[t] : -1;
}
int move_row(int usum_id) {
  if (usum_id >= 1 && usum_id <= 354) return usum_id;
  if (usum_id > 354 && usum_id <= 467) return usum_id + 20;
  if (usum_id >= 468 && usum_id <= 728) return usum_id + 20;
  return -1;
}

std::string form_key(const std::string& name, const std::string& kind, int form, int nat, const std::string& label) {
  if (kind == "alola") return name + " (Alola)";
  if (kind == "mega") {
    std::string suffix;
    if (nat == 6 || nat == 150) suffix = form == 1 ? " X" : " Y";
    return name + " (Mega" + suffix + ")";
  }
  if (kind == "primal") return name + " (Primal)";
  return name + " (" + (label.empty() ? kind : label) + ")";
}
std::string shown_form(const std::string& name, const std::string& kind, int nat, int form) {
  const std::string n = shown(name);
  if (kind == "mega") {
    std::string suffix;
    if (nat == 6 || nat == 150) suffix = form == 1 ? " X" : " Y";
    return "MEGA " + n + suffix;
  }
  if (kind == "primal") return "PRIMAL " + n;
  return n;
}
bool ends_with(const std::string& s, const std::string& t) { return s.size() >= t.size() && s.compare(s.size() - t.size(), t.size(), t) == 0; }
bool strip_ite(const std::string& nm, std::string& base) {
  for (const char* suf : {"ite X", "ite Y", "ite"}) {
    if (ends_with(nm, suf)) { base = nm.substr(0, nm.size() - strlen(suf)); return true; }
  }
  return false;
}
std::string lower(std::string s) { for (auto& c : s) c = (char)tolower((unsigned char)c); return s; }

struct StoneHit { int id = -1; std::string name; };
StoneHit stone_for(const std::vector<std::pair<int, std::string>>& items, const std::string& name, int form, int nat) {
  std::vector<std::tuple<int, int, std::string>> cands;
  for (auto& it : items) {
    std::string base;
    strip_ite(it.second, base);
    size_t k = 0;
    while (k < base.size() && k < name.size() && tolower((unsigned char)base[k]) == tolower((unsigned char)name[k])) k++;
    if (k >= 4) cands.emplace_back((int)k, it.first, it.second);
  }
  if (nat == 6 || nat == 150) {
    const std::string want = form == 1 ? " X" : " Y";
    std::vector<std::tuple<int, int, std::string>> keep;
    for (auto& c : cands) if (ends_with(std::get<2>(c), want)) keep.push_back(c);
    cands.swap(keep);
  }
  std::sort(cands.begin(), cands.end(), [](auto& a, auto& b) { return a > b; });
  StoneHit h;
  if (!cands.empty()) { h.id = std::get<1>(cands[0]); h.name = std::get<2>(cands[0]); }
  return h;
}

std::vector<std::string> split_lines(const std::string& s) {
  std::vector<std::string> v;
  std::istringstream is(s);
  std::string l;
  while (std::getline(is, l)) v.push_back(l);
  return v;
}

struct ItemRow { std::string name, copy, plate_type; int price = 0, id = -1, nat = 0; bool mega = false; };

}

void make_gen57_forms_data(const DataOptions& o, const Plan& plan, const ModelResults& mr) {
  (void)plan;
  Usum u(o.rom);
  const Json tables = read_json(o.data_dir + "/gen57_tables.json");
  const Json fplan = read_json(o.data_dir + "/gen57_forms.json");
  const XdTablesCache xd = load_xd_tables(o.data_mods_exe, o.xd_disc, o.work);
  const Json& ft = jget(tables, "forms");
  const std::string pack = fs::absolute(o.out).string();
  PackFiles pf{pack, o.eol_crlf};
  const Json* sc = fplan.find("stone_copy");
  const std::string stone_copy = sc ? sc->s : "Moon Stone";
  const Json* sp = fplan.find("stone_price");
  const int stone_price = sp ? (int)sp->i : 0;
  const int stone_coupon = fplan.find("stone_coupon") ? (int)fplan.find("stone_coupon")->i : (int)jget(ft, "stone_coupon").i;
  const int charm_coupon = fplan.find("charm_coupon") ? (int)fplan.find("charm_coupon")->i : (int)jget(ft, "charm_coupon").i;

  std::set<int> pre;
  for (int n = 1; n <= 807; n++) for (auto& e : u.evolutions(n)) pre.insert(e.species);

  std::vector<std::pair<int, std::string>> stone_items;
  for (int i = 0; i < u.item_count(); i++) {
    const std::string nm = u.item_name(i, "en");
    std::string b;
    if (strip_ite(nm, b) && i >= 656 && i <= 770) stone_items.push_back({i, nm});
  }
  std::vector<std::string> pkx_old, other;
  std::string tables_in = slurp_lines_lf(pack + "/tables.txt");
  if (o.eol_crlf) {
    std::string u8;
    for (unsigned char ch : tables_in) { if (ch < 0x80) u8 += (char)ch; else { u8 += (char)(0xC0 | (ch >> 6)); u8 += (char)(0x80 | (ch & 0x3F)); } }
    tables_in.swap(u8);
  }
  for (auto& ln : split_lines(tables_in)) {
    if (ln.compare(0, 4, "pkx ") == 0) pkx_old.push_back(ln);
    else if (!py_strip(ln).empty() && ln.compare(0, 6, "format") != 0 && ln.compare(0, 8, "pkxcount") != 0) other.push_back(ln);
  }
  int first_pkx = FIRST_NEW_PKX_MIN;
  for (auto& l : pkx_old) first_pkx = std::max(first_pkx, atoi(l.c_str() + 4));
  first_pkx += 1;

  struct Prep { const Json* f; std::string key, kind, name; int nat, form, record, k; Json row, rep; std::vector<std::string> notes; bool no_pkx = false, look = false; };
  std::vector<Prep> prepared;
  const auto& forms = jget(fplan, "forms").a;
  for (size_t k = 0; k < forms.size(); k++) {
    const Json& f = forms[k];
    const int nat = (int)jget(f, "nat").i, form = (int)jget(f, "form").i;
    const std::string kind = jget(f, "kind").s;
    if (!o.only_kinds.empty() && !o.only_kinds.count(kind)) continue;
    Prep p;
    p.f = &f; p.nat = nat; p.form = form; p.kind = kind; p.k = (int)prepared.size();
    const Personal base = u.personal(nat);
    p.record = base.form_stats_index ? base.form_stats_index + form - 1 : nat;
    p.name = u.species_name(nat, "en");
    const Json* lab = f.find("label");
    p.key = form_key(p.name, kind, form, nat, lab ? lab->s : "");
    RowBridge rb = build_row_bridge(u, tables, xd, pre, nat, p.record, first_pkx + p.k, 0);
    p.row = rb.row;
    p.rep = rb.rep;
    p.notes = rb.notes;
    p.row.set("copy", Json(p.name));
    if (const Json* lk = f.find("look"); lk && jtruthy(lk)) {
      Json lr = Json::object();
      lr.set("copy", Json(p.name));
      if (const Json* px = p.row.find("pkxDataId")) lr.set("pkxDataId", *px);
      p.row = lr;
      p.look = true;
    }
    if (const Json* tj = f.find("type")) {
      const int t = (int)tj->i, x = type_xd(t) < 0 ? 0 : type_xd(t);
      Json zt = Json::array();
      zt.push(Json(x)); zt.push(Json(x));
      p.row.set("zokuseiDataId", zt);
      p.rep.set("types", jints({t, t}));
    }
    if (kind == "alt" && nat == 479) {
      const Json* sg = jget(ft, "rotom_signature").find(std::to_string(form));
      if (sg) {
        const int mrow = move_row((int)sg->i);
        Json* wd = p.row.find("waza.dataId");
        Json* wl = p.row.find("waza.level");
        bool have = false;
        for (auto& x : wd->a) if (x.i == mrow) have = true;
        if (mrow >= 0 && !have) {
          for (size_t i = 0; i < wd->a.size(); i++) if (wd->a[i].i == 0) { wd->a[i] = Json(mrow); wl->a[i] = Json(1); break; }
        }
      }
    }
    prepared.push_back(std::move(p));
  }
  std::vector<std::pair<uint32_t, std::string>> toc_rows;
  std::vector<std::string> pkx_new;
  std::map<std::string, Json> mreport;
  for (auto& p : prepared) {
    const std::string nm = "n3f_" + std::to_string(p.nat) + "_" + std::to_string(p.form);
    const uint32_t gid = (uint32_t)(4600 + p.k);
    if (!mr.present) { pkx_new.push_back("pkx " + std::to_string((int)jget(p.row, "pkxDataId").i) + " 0 0"); continue; }
    auto it = mr.forms.find(nm);
    std::string st = "error";
    Json info;
    if (it != mr.forms.end()) { info = it->second; const Json* s = info.find("status"); st = s ? s->s : "ok"; }
    if (st == "ok") {
      toc_rows.push_back({gid, "pkx_" + nm});
      pkx_new.push_back("pkx " + std::to_string((int)jget(p.row, "pkxDataId").i) + " " + std::to_string(gid) + " " + std::to_string(PKX_RES_ID));
      Json mi = Json::object();
      for (const char* k : {"tris", "size", "group"}) if (const Json* v = info.find(k)) mi.set(k, *v);
      mreport[p.key] = mi;
    } else {
      std::string code = "00";
      const Json* mj = p.f->find("model");
      if (mj && !mj->s.empty()) code = mj->s;
      else if (p.kind == "alola") code = "61";
      else if (p.kind == "mega" || p.kind == "primal") code = "5" + std::to_string(p.form);
      if (st == "nogroup") p.notes.push_back("no model group for " + p.name + " form " + code + ": the form keeps its base species model");
      else if (st == "noshell") p.notes.push_back("no XD model shell for " + p.name + ": base model kept");
      else if (st == "overcap") p.notes.push_back("model over the 614400-byte file cap (" + std::to_string(info.find("size") ? info.find("size")->i : 0) + " bytes): base model kept");
      else p.notes.push_back("model conversion failed (" + (info.find("error") ? info.find("error")->s : std::string("not made")) + "): base model kept");
      p.no_pkx = true;
    }
  }
  std::vector<std::string> form_lines = {"format 1"};
  std::vector<ItemRow> item_rows;
  std::set<int> ability_ids;
  std::set<std::string> spawn_species;
  for (auto& p : prepared) {
    if (const Json* tk = p.row.find("tokuseiDataId")) for (auto& x : tk->a) if (x.i > 123) ability_ids.insert((int)x.i);
    if (p.no_pkx) {
      auto& o2 = p.row.o;
      o2.erase(std::remove_if(o2.begin(), o2.end(), [](auto& kv) { return kv.first == "pkxDataId"; }), o2.end());
    }
    std::string line = "form " + p.key + "|" + p.name + "|" + p.kind;
    if (p.kind == "mega") {
      StoneHit h = stone_for(stone_items, p.name, p.form, p.nat);
      if (h.id < 0) p.notes.push_back("no Mega Stone found");
      else {
        ItemRow it;
        it.name = h.name; it.copy = stone_copy; it.price = stone_price; it.id = h.id; it.nat = p.nat; it.mega = true;
        bool replaced = false;
        for (auto& x : item_rows) if (x.name == it.name) { x = it; replaced = true; }
        if (!replaced) item_rows.push_back(it);
        line += "|" + h.name;
      }
    } else if (p.kind == "plate") {
      const Json* lab = p.f->find("label");
      const Json* pn = lab ? jget(ft, "plate_names").find(lab->s) : nullptr;
      int pid = -1;
      if (pn) for (int i = 0; i < u.item_count(); i++) if (u.item_name(i, "en") == pn->s) { pid = i; break; }
      if (!pn || pid < 0) p.notes.push_back("no Plate item found");
      else {
        ItemRow it;
        it.name = pn->s; it.copy = stone_copy; it.price = stone_price; it.id = pid; it.nat = p.nat; it.plate_type = lab->s;
        bool replaced = false;
        for (auto& x : item_rows) if (x.name == it.name) { x = it; replaced = true; }
        if (!replaced) item_rows.push_back(it);
        line += "|" + it.name;
      }
    } else if (p.kind == "primal") {
      const int oid = p.nat == 382 ? 535 : 534;
      ItemRow it;
      it.name = u.item_name(oid, "en"); it.copy = stone_copy; it.price = 0; it.id = oid; it.nat = p.nat;
      bool replaced = false;
      for (auto& x : item_rows) if (x.name == it.name) { x = it; replaced = true; }
      if (!replaced) item_rows.push_back(it);
      line += "|" + it.name;
    }
    if (const Json* sw = p.f->find("spawn"); sw && sw->i > 0 && !p.no_pkx) {
      if (std::count(line.begin(), line.end(), '|') < 3) line += "|";
      line += "|" + std::to_string(sw->i);
      spawn_species.insert(p.name);
    }
    form_lines.push_back(line);
  }
  {
    const Json* sb = fplan.find("spawn_base");
    if (sb) for (const std::string& nm : spawn_species) {
      for (int n = 1; n <= 807; n++) {
        if (u.species_name(n, "en") != nm) continue;
        if (const Json* w = sb->find(std::to_string(n))) form_lines.push_back("spawnbase " + nm + "|" + std::to_string(w->i));
        break;
      }
    }
  }
  if (std::any_of(prepared.begin(), prepared.end(), [](auto& p) { return p.kind == "alola"; })) {
    ItemRow it;
    it.name = "Alola Charm"; it.copy = stone_copy; it.price = 0; it.id = -1; it.nat = 0;
    item_rows.push_back(it);
    form_lines.push_back("charm Alola Charm");
  }
  for (int li = 0; li < 6; li++) {
    const std::string lg = kLangs[li], dl = kDumpLang[li];
    const std::string folder = lg == "us" ? "data" : "data_" + lg;
    Json species = Json::object(), items = Json::object();
    for (auto& p : prepared) {
      Json row = p.row;
      Json text = Json::object();
      text.set("name", Json(shown_form(u.species_name(p.nat, dl), p.kind, p.nat, p.form)));
      std::string cls = u.species_class(p.nat, dl);
      {
        static const char* words[] = {"Pok\xC3\xA9mon", "Pok\xC3\xA9mons", "Pokemon", "\xE3\x83\x9D\xE3\x82\xB1\xE3\x83\xA2\xE3\x83\xB3"};
        std::u32string c32 = to_u32(cls);
        while (!c32.empty() && (c32.back() == U' ' || (c32.back() >= 9 && c32.back() <= 13) || c32.back() == 0x3000 || c32.back() == 0xA0)) c32.pop_back();
        std::string s = to_utf8(c32);
        for (const char* w : words) { const size_t n = strlen(w); if (s.size() >= n && s.compare(s.size() - n, n, w) == 0) { s = s.substr(0, s.size() - n); break; } }
        text.set("typeName", Json(shown(py_strip(s))));
      }
      row.set("text", text);
      if (p.look) { species.set(p.key, row); continue; }
      Json evo = *row.find("sinka.pokemonDataId");
      const Json& ekind = jget(row, "sinka.kind");
      size_t slot = 0;
      for (const EvoRec& e : u.evolutions(p.record)) {
        if (slot >= 5 || ekind.a[slot].i == 0) break;
        std::string target;
        if (e.form && p.kind == "alola") target = form_key(u.species_name(e.species, "en"), "alola", e.form, e.species, "");
        else target = u.species_name(e.species, "en");
        if (p.kind == "alt") {
          const Json* mylab = p.f->find("label");
          if (mylab) {
            for (auto& q : prepared)
              if (q.nat == e.species && q.kind == "alt" && q.f->find("label") && q.f->find("label")->s == mylab->s) { target = q.key; break; }
          }
        }
        evo.a[slot] = Json(target);
        slot++;
      }
      row.set("sinka.pokemonDataId", evo);
      species.set(p.key, row);
    }
    Json plate_items = Json::object();
    for (auto& it : item_rows) {
      Json x = Json::object();
      if (it.id < 0) {
        const Json& cd = jget(jget(ft, "charm_doc"), dl.c_str());
        x.set("copy", Json(it.copy));
        Json t = Json::object();
        t.set("name", cd.a.at(0));
        t.set("doc", cd.a.at(1));
        x.set("text", t);
        x.set("coupon", Json(charm_coupon));
        items.set(it.name, x);
        continue;
      }
      std::string doc = jget(jget(ft, "stone_doc"), dl.c_str()).s;
      if (!it.plate_type.empty()) {
        doc = jget(jget(ft, "plate_doc"), dl.c_str()).s;
        const std::string tn = jget(jget(jget(ft, "plate_types"), it.plate_type.c_str()), dl.c_str()).s;
        for (size_t pos; (pos = doc.find("{T}")) != std::string::npos;) doc.replace(pos, 3, tn);
      }
      const std::string sn = shown(u.species_name(it.nat, dl));
      for (size_t pos; (pos = doc.find("{S}")) != std::string::npos;) doc.replace(pos, 3, sn);
      x.set("copy", Json(it.copy));
      Json t = Json::object();
      t.set("name", Json(shown(u.item_name(it.id, dl))));
      t.set("doc", Json(doc));
      x.set("text", t);
      if (it.price) x.set("price", Json(it.price));
      if (it.mega) x.set("coupon", Json(stone_coupon));
      else if (!it.plate_type.empty()) x.set("coupon", jget(ft, "plate_coupon"));
      (it.plate_type.empty() ? items : plate_items).set(it.name, x);
    }
    Json add = Json::object();
    if (!items.o.empty()) add.set("items", items);
    add.set("pokemon", species);
    Json top = Json::object();
    top.set("_note", Json("made by gen57_forms.py from the player's own dump: game data, never in the repository"));
    top.set("add", add);
    fs::create_directories(pack + "/" + folder);
    pf.write(folder + "/03_forms.json", top.dump(1, false));
    if (!plate_items.o.empty()) {
      Json padd = Json::object(), ptop = Json::object();
      padd.set("items", plate_items);
      ptop.set("_note", Json("made by gen57_forms from the player's own dump: the 17 Arceus Plates (0.8.0; placeholder price): game data, never in the repository"));
      ptop.set("add", padd);
      pf.write(folder + "/08_arceus_plates.json", ptop.dump(1, false));
    }
  }
  {
    std::string t = "format 1\npkxcount 419";
    for (auto& l : pkx_old) t += "\n" + l;
    for (auto& l : pkx_new) if (!ends_with(l, " 0 0") || !mr.present) t += "\n" + l;
    for (auto& l : other) t += "\n" + l;
    pf.write("tables.txt", t + "\n", false, true);
  }
  if (mr.present && !o.models_dir.empty()) {
    for (auto& r : toc_rows) {
      const fs::path src = fs::path(o.models_dir) / "gen57" / "files" / (r.second + ".fsys");
      if (fs::exists(src)) fs::copy_file(src, fs::path(pack) / "files" / src.filename(), fs::copy_options::overwrite_existing);
    }
  }
  if (!toc_rows.empty()) {
    std::string toc_path;
    for (auto& e : fs::directory_iterator(pack + "/files")) if (lower(e.path().filename().string()) == "gsfsys.toc") toc_path = e.path().string();
    pf.write_bytes("files/" + fs::path(toc_path).filename().string(), pbr::pack_extend_toc(slurp_bytes(toc_path), toc_rows));
  }
  {
    const std::string ab_path = pack + "/abilities.txt";
    std::vector<std::string> lines = fs::exists(ab_path) ? split_lines(slurp_lines_lf(ab_path)) : std::vector<std::string>{"format 1"};
    std::set<int> have;
    for (size_t i = 1; i < lines.size(); i++) have.insert(atoi(lines[i].c_str()));
    std::vector<int> add_ids;
    for (int a : ability_ids) if (!have.count(a)) add_ids.push_back(a);
    if (!add_ids.empty()) {
      auto more = split_lines(ability_lines(u, tables, add_ids, {}));
      for (size_t i = 1; i < more.size(); i++) lines.push_back(more[i]);
      std::string t;
      for (auto& l : lines) t += l + "\n";
      pf.write("abilities.txt", t);
    }
  }
  {
    std::string t;
    for (auto& l : form_lines) t += l + "\n";
    while (!t.empty() && t.back() == '\n') t.pop_back();
    if (t.find("\npatch ") == std::string::npos) t += "\npatch Ability Patch\n";
    else t += "\n";
    {
      const Json& memories = jget(jget(tables, "patch"), "memories");
      if (t.find("\nmemory ") == std::string::npos && memories.kind == Json::Arr) {
        while (!t.empty() && t.back() == '\n') t.pop_back();
        t += "\n";
        for (const Json& m : memories.a) t += "memory " + m.s + " Memory" "\n";
      }
    }
    pf.write("forms.txt", t, true);
  }
  if (std::any_of(prepared.begin(), prepared.end(), [](auto& p) { return p.kind == "plate"; })) {
    Json mv = Json::object(), jd = Json::object(), top = Json::object();
    jd.set("type_from_user", Json(true));
    mv.set("Judgment", jd);
    top.set("_note", Json("made by gen57_forms: Judgment takes its user's type (Arceus Plates, 0.8.0)"));
    top.set("moves", mv);
    pf.write("battle/plates.json", top.dump(1, true));
  }
  {
    Json info = read_json(pack + "/gen57.json");
    info.set("forms", Json((int)prepared.size()));
    info.set("forms_version", Json(1));
    pf.write("gen57.json", info.dump(1, true));
    Json rep = Json::array();
    for (auto& p : prepared) {
      Json r = Json::object();
      r.set("key", Json(p.key));
      r.set("record", Json(p.record));
      r.set("kind", Json(p.kind));
      r.set("types", jget(p.rep, "types"));
      r.set("abilities", jget(p.rep, "abilities"));
      r.set("bst", jget(p.rep, "bst"));
      r.set("notes", jstrs(p.notes));
      auto m = mreport.find(p.key);
      r.set("model", m != mreport.end() ? m->second : Json());
      rep.push(r);
    }
    pf.write("forms_report.json", rep.dump(1, true));
  }
  {
    const Json& pt = jget(tables, "patch");
    for (auto& kv : jget(pt, "folders").o) {
      if (!fs::is_directory(pack + "/" + kv.first)) continue;
      const Json& d = jget(jget(pt, "doc"), kv.second.s.c_str());
      Json it = Json::object(), text = Json::object();
      text.set("name", d.a.at(0));
      text.set("doc", d.a.at(1));
      it.set("copy", Json("Moon Stone"));
      it.set("text", text);
      it.set("coupon", jget(pt, "coupon"));
      Json items = Json::object(), add = Json::object(), top = Json::object();
      items.set("Ability Patch", it);
      add.set("items", items);
      top.set("_note", Json("made by gen57_patch.py: the Ability Patch (0.7.1; placeholder price)"));
      top.set("add", add);
      pf.write(kv.first + "/04_ability_patch.json", top.dump(1, false));
      {
        Json mitems = Json::object();
        for (const Json& ty : jget(pt, "memories").a) {
          std::string up = ty.s;
          for (char& c : up) c = (char)toupper((unsigned char)c);
          Json mt = Json::object(), mtext = Json::object();
          mtext.set("name", Json(up + " MEMORY"));
          mtext.set("doc", Json("Held by Silvally:{00}its type changes{00}to " + ty.s + "."));
          mt.set("copy", Json("Moon Stone"));
          mt.set("text", mtext);
          mt.set("coupon", jget(pt, "memory_coupon"));
          mitems.set(ty.s + " Memory", mt);
        }
        Json madd = Json::object(), mtop = Json::object();
        madd.set("items", mitems);
        mtop.set("_note", Json("made by gen57_patch.py: the 17 RKS System Memories (0.7.1 round 3; placeholder price)"));
        mtop.set("add", madd);
        pf.write(kv.first + "/05_rks_memories.json", mtop.dump(1, false));
      }
    }
  }
}

}
