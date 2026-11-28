#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <regex>
#include <set>
#include <sstream>

#include "n3ds_gf.h"
#include "n3ds_image.h"
#include "usum_data_internal.h"

namespace fs = std::filesystem;

namespace n3ds {

namespace {

constexpr int FIRST = 494, LAST = 807, ROW_SHIFT = 26, PKX_SHIFT = 31, PKX_GROUP_FIRST = 4200, PACK_VERSION = 1;
constexpr uint32_t PKX_RES_ID = 0x00011E00;
constexpr int SND_DATA_COUNT = 1507, SND_FILE_COUNT = 516, WAVE_OLD = 389;
constexpr int CRY_RESERVED = 106, CRY57_GROUP = 4600;
static const char* CRY57_ARCHIVE = "stm_se_nakigoe_gen57";
static std::string fnv64_hex(const Bytes& d) {
  uint64_t h = 14695981039346656037ull;
  for (uint8_t b : d) h = (h ^ b) * 1099511628211ull;
  char buf[17];
  snprintf(buf, sizeof buf, "%016llx", (unsigned long long)h);
  return buf;
}
const char* kNote = "made by gen57_all.py from the player's own dump: game data, never in the repository";
const char* kLangs[6] = {"us", "ge", "fr", "it", "sp", "jp"};
const char* kDumpLang[6] = {"en", "de", "fr", "it", "es", "ja"};

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

const Json* evo_rule(const Json& tables, const std::string& source, const std::string& target) {
  for (const Json& r : jget(jget(tables, "evolutions"), "rules").a) {
    if (r.a.at(0).s == source && r.a.at(1).s == target) return &r;
  }
  return nullptr;
}
std::string evo_item_ref(const Json& tables, const std::string& name) {
  const Json* c = jget(jget(tables, "evolutions"), "classic").find(name.c_str());
  if (c) return "#" + std::to_string(c->i);
  std::string s = name;
  std::replace(s.begin(), s.end(), ' ', '_');
  return s;
}
std::string evo_note(const Json& tables, const std::string& tname, const Json& rule) {
  const std::string how = rule.a.at(2).s, item = rule.a.at(3).s;
  if (how == "item") return "evolution to " + tname + ": use the " + item + " (evolutions.txt)";
  if (how == "held") return "evolution to " + tname + ": use the " + item + ", or trade holding it (evolutions.txt)";
  return "evolution to " + tname + ": " + jget(jget(jget(tables, "evolutions"), "how_text"), how.c_str()).s + (how == "trade" ? "" : " (evolutions.txt)");
}
int evo_move_row(int usum_id) { return usum_id >= 1 && usum_id <= 354 ? usum_id : usum_id + 20; }

struct RowOut {
  Json row, rep;
  std::vector<std::string> notes;
};

struct Ctx {
  Usum& u;
  const Json& tables;
  const XdTablesCache& xd;
  std::set<int> pre;
};

RowOut build_row(Ctx& c, int nat, int standin, int rec, int pkx_row, std::vector<std::array<int, 3>>* pending) {
  Usum& u = c.u;
  const Personal p = u.personal(rec);
  RowOut r;
  int t1 = type_xd(p.type1), t2 = type_xd(p.type2);
  if (t1 < 0) {
    t1 = t2 >= 0 ? t2 : 0;
    r.notes.push_back("Fairy type: shown as the other type (Normal when Fairy only); Modern types shows the real one");
  }
  if (t2 < 0) t2 = t1;
  const int ab[2] = {p.abilities[0], p.abilities[1] ? p.abilities[1] : 0};
  std::vector<std::pair<int, int>> learn;
  std::set<int> seen;
  for (auto& lm : u.levelup(rec)) {
    const int lv = lm.first, mv = lm.second;
    const int row = move_row(mv);
    if (row < 0) {
      if (mv >= 468 && pending) pending->push_back({nat, std::max(lv, 1), mv});
      continue;
    }
    if (seen.count(row)) continue;
    seen.insert(row);
    learn.push_back({std::max(lv, 1), row});
  }
  std::sort(learn.begin(), learn.end());
  if (learn.size() > 20) learn.resize(20);
  struct Ev { int kind, buff; std::string name; };
  std::vector<Ev> evo;
  const std::string sname = u.species_name(nat);
  for (const EvoRec& e : u.evolutions(rec)) {
    const std::string tname = u.species_name(e.species);
    const int m = e.method;
    int kind, buff;
    const Json* rule = rec == nat ? evo_rule(c.tables, sname, tname) : nullptr;
    if (rule && rule->a.at(2).s == "trade") {
      r.notes.push_back(evo_note(c.tables, tname, *rule));
      evo.push_back({5, 0, tname});
      continue;
    }
    if (rule) {
      r.notes.push_back(evo_note(c.tables, tname, *rule));
      continue;
    }
    if (m >= 1 && m <= 3) { kind = 1; buff = 0; }
    else if (m == 4) { kind = 4; buff = e.level; }
    else if (m == 5) { kind = 5; buff = 0; }
    else if (m >= 9 && m <= 11) { kind = m - 1; buff = e.arg; }
    else if (m == 12 || m == 13) { kind = m - 1; buff = e.level ? e.level : 20; }
    else {
      const int lvl = e.level ? e.level : (c.pre.count(nat) ? 40 : 22);
      kind = 4;
      buff = lvl;
      r.notes.push_back("evolution to " + tname + ": USUM method " + std::to_string(m) + " (" + std::to_string(e.arg) + ") has no XD kind -> level " + std::to_string(lvl));
    }
    evo.push_back({kind, buff, tname});
  }
  if (evo.size() > 5) evo.resize(5);
  while (evo.size() < 5) evo.push_back({0, 0, ""});
  std::vector<int> mcn, teach;
  for (int m : c.xd.tm_moves) mcn.push_back(std::any_of(learn.begin(), learn.end(), [&](auto& l) { return l.second == m; }) ? 1 : 0);
  for (int m : c.xd.tutors) teach.push_back(std::any_of(learn.begin(), learn.end(), [&](auto& l) { return l.second == m; }) ? 1 : 0);
  Json row = Json::object();
  row.set("copy", Json(standin));
  row.set("growDataId", Json(p.growth));
  row.set("get", Json(p.catch_rate));
  row.set("sexRatio", Json(p.gender));
  row.set("giveExp", Json(std::min(p.base_exp, 255)));
  row.set("initFriend", Json(p.friendship));
  row.set("height", Json(std::max(1, (int)std::nearbyint((double)p.height_cm / 10.0))));
  row.set("weight", Json(p.weight_hg));
  row.set("numPokemon", Json(nat));
  row.set("numZukan", Json(nat));
  row.set("zokuseiDataId", jints({t1, t2}));
  row.set("tokuseiDataId", jints({ab[0], ab[1]}));
  row.set("wazaMcn", jints(mcn));
  row.set("teachWaza", jints(teach));
  row.set("itemDataId", jints({0, 0}));
  row.set("basisStatus", jints({p.hp, p.atk, p.defn, p.spa, p.spd, p.spe, p.ev_hp, p.ev_atk, p.ev_def, p.ev_spa, p.ev_spd, p.ev_spe}));
  std::vector<int> ek, eb;
  Json en = Json::array();
  for (auto& e : evo) { ek.push_back(e.kind); eb.push_back(e.buff); en.push(e.kind || e.buff || !e.name.empty() ? Json(e.name) : Json(0)); }
  row.set("sinka.kind", jints(ek));
  row.set("sinka.buff", jints(eb));
  row.set("sinka.pokemonDataId", en);
  std::vector<int> wl, wd;
  for (auto& l : learn) { wl.push_back(l.first); wd.push_back(l.second); }
  while (wl.size() < 20) { wl.push_back(0); wd.push_back(0); }
  row.set("waza.level", jints(wl));
  row.set("waza.dataId", jints(wd));
  row.set("pkxDataId", Json(pkx_row));
  r.row = row;
  Json rep = Json::object();
  rep.set("types", jints({p.type1, p.type2}));
  rep.set("abilities", jints({p.abilities[0], p.abilities[1], p.abilities[2]}));
  rep.set("mapped", jints({ab[0], ab[1]}));
  rep.set("bst", Json(p.bst()));
  Json mv = Json::array();
  for (auto& l : learn) mv.push(jints({l.first, l.second}));
  rep.set("moves", mv);
  r.rep = rep;
  return r;
}

Json text_for(Usum& u, int nat, const std::string& lang) {
  Json t = Json::object();
  t.set("name", Json(shown(u.species_name(nat, lang))));
  std::string cls = u.species_class(nat, lang);
  static const char* words[] = {"Pok\xC3\xA9mon", "Pok\xC3\xA9mons", "Pokemon", "\xE3\x83\x9D\xE3\x82\xB1\xE3\x83\xA2\xE3\x83\xB3"};
  std::u32string c32 = to_u32(cls);
  while (!c32.empty() && (c32.back() == U' ' || (c32.back() >= 9 && c32.back() <= 13) || c32.back() == 0x3000 || c32.back() == 0xA0)) c32.pop_back();
  std::string stripped = to_utf8(c32);
  for (const char* w : words) {
    const size_t n = strlen(w);
    if (stripped.size() >= n && stripped.compare(stripped.size() - n, n, w) == 0) { stripped = stripped.substr(0, stripped.size() - n); break; }
  }
  t.set("typeName", Json(shown(py_strip(stripped))));
  return t;
}

std::string join_ints(const std::vector<int>& v) {
  std::string s;
  for (size_t i = 0; i < v.size(); i++) s += (i ? " " : "") + std::to_string(v[i]);
  return s;
}
std::vector<int> ints_of(const Json& a) {
  std::vector<int> v;
  for (auto& x : a.a) v.push_back((int)x.i);
  return v;
}

struct WaveBytes { Bytes bytes; };

uint32_t le16(const Bytes& b, size_t o) { return b.at(o) | b.at(o + 1) << 8; }
uint32_t le32(const Bytes& b, size_t o) { return le16(b, o) | le16(b, o + 2) << 16; }

Bytes cwar_first(const Bytes& w) {
  if (w.size() < 4 || memcmp(w.data(), "CWAR", 4)) throw Fail("a cry archive is not a CWAR");
  const uint32_t io = le32(w, 0x18), fo = le32(w, 0x24);
  const uint32_t n = le32(w, io + 8);
  if (n < 1) throw Fail("an empty cry archive");
  const uint32_t off = le32(w, io + 16), size = le32(w, io + 20);
  if ((size_t)fo + 8 + off + size > w.size()) throw Fail("a cry archive is cut short");
  return Bytes(w.begin() + fo + 8 + off, w.begin() + fo + 8 + off + size);
}

pbr::CryStream parse_cwav(const Bytes& v) {
  if (v.size() < 4 || memcmp(v.data(), "CWAV", 4)) throw Fail("a cry is not a CWAV");
  const uint32_t io = le32(v, 0x18), dof = le32(v, 0x24);
  const int enc = v.at(io + 8);
  const uint32_t rate = le32(v, io + 12), lend = le32(v, io + 20);
  if (enc != 2) throw Fail("a cry is not DSP-ADPCM");
  const uint32_t nch = le32(v, io + 0x1c);
  pbr::CryStream s;
  s.rate = rate;
  s.nsamp = lend;
  const uint32_t nframes = (lend + 13) / 14;
  for (uint32_t c = 0; c < nch; c++) {
    const uint32_t off = le32(v, io + 0x20 + 8 * c + 4);
    const uint32_t ci = io + 0x1c + off;
    const uint32_t samp_off = le32(v, ci + 4), codec_off = le32(v, ci + 8 + 4);
    std::array<uint16_t, 16> co;
    for (int k = 0; k < 16; k++) co[(size_t)k] = (uint16_t)le16(v, ci + codec_off + 2 * (uint32_t)k);
    s.coefs.push_back(co);
    const size_t start = dof + 8 + samp_off;
    size_t end = std::min(v.size(), start + (size_t)nframes * 8);
    s.data.emplace_back(v.begin() + (long)std::min(start, v.size()), v.begin() + (long)end);
  }
  return s;
}

}

Bytes mega_jingle_wav(const RomFs& r, std::string* why) {
  auto no = [&](const char* w) { if (why) *why = w; return Bytes{}; };
  const char* path = "data/sound/me_xy_mega_stone.dspadpcm.bcstm";
  if (!r.has(path)) return no("the dump has no data/sound/me_xy_mega_stone stream");
  const Bytes b = r.read(path);
  auto s16 = [&](size_t o) { return (int16_t)le16(b, o); };
  try {
    if (b.size() < 0x40 || memcmp(b.data(), "CSTM", 4)) return no("not a CSTM stream");
    const uint32_t nb = le16(b, 0x10);
    uint32_t info = 0, data = 0;
    for (uint32_t i = 0; i < nb; i++) {
      const uint32_t t = le16(b, 0x14 + 12 * i), off = le32(b, 0x14 + 12 * i + 4);
      if (t == 0x4000) info = off;
      if (t == 0x4002) data = off;
    }
    if (!info || !data || memcmp(b.data() + info, "INFO", 4)) return no("a stream without its INFO / DATA blocks");
    const uint32_t si = info + 8 + le32(b, info + 8 + 4), ct = info + 8 + le32(b, info + 8 + 16 + 4);
    const int codec = b.at(si), nch = b.at(si + 2);
    const uint32_t rate = le32(b, si + 4), bc = le32(b, si + 16), bs = le32(b, si + 20), bsamp = le32(b, si + 24), lbsamp = le32(b, si + 32),
                   lbpad = le32(b, si + 36);
    const uint32_t samples0 = data + 8 + le32(b, si + 48 + 4);
    if (codec != 2 || nch < 1 || nch > 2 || rate < 8000 || rate > 48000 || bc == 0) return no("not a DSP-ADPCM stream of one or two channels");
    if (le32(b, ct) < (uint32_t)nch) return no("a stream with fewer channel records than channels");
    std::vector<std::array<int16_t, 16>> coef((size_t)nch);
    for (int c = 0; c < nch; c++) {
      const uint32_t ci = ct + le32(b, ct + 4 + 8 * (uint32_t)c + 4);
      const uint32_t di = ci + le32(b, ci + 4);
      for (int k = 0; k < 16; k++) coef[(size_t)c][(size_t)k] = s16(di + 2 * (uint32_t)k);
    }
    std::vector<std::vector<int16_t>> pcm((size_t)nch);
    std::vector<std::array<int32_t, 2>> hist((size_t)nch, {0, 0});
    for (uint32_t blk = 0; blk < bc; blk++) {
      const bool last = blk + 1 == bc;
      const uint32_t size = last ? lbpad : bs, nsamp = last ? lbsamp : bsamp;
      for (int c = 0; c < nch; c++) {
        size_t p = samples0 + (size_t)blk * bs * (uint32_t)nch + (size_t)c * size;
        int32_t h1 = hist[(size_t)c][0], h2 = hist[(size_t)c][1];
        for (uint32_t got = 0; got < nsamp;) {
          const uint8_t ps = b.at(p);
          const int32_t scale = 1 << (ps & 15), c1 = coef[(size_t)c][(size_t)(ps >> 4) * 2], c2 = coef[(size_t)c][(size_t)(ps >> 4) * 2 + 1];
          for (int k = 0; k < 14 && got < nsamp; k++, got++) {
            const uint8_t byte = b.at(p + 1 + (size_t)k / 2);
            int32_t nib = (k % 2 == 0) ? byte >> 4 : byte & 15;
            if (nib >= 8) nib -= 16;
            int32_t v = (((nib * scale) << 11) + 1024 + c1 * h1 + c2 * h2) >> 11;
            v = std::clamp(v, -32768, 32767);
            pcm[(size_t)c].push_back((int16_t)v);
            h2 = h1;
            h1 = v;
          }
          p += 8;
        }
        hist[(size_t)c] = {h1, h2};
      }
    }
    const uint32_t frames = (uint32_t)pcm[0].size(), bytes = frames * (uint32_t)nch * 2;
    Bytes w(44 + bytes);
    auto put32 = [&](size_t o, uint32_t v) { for (int i = 0; i < 4; i++) w[o + (size_t)i] = (uint8_t)(v >> (8 * i)); };
    auto put16 = [&](size_t o, uint32_t v) { w[o] = (uint8_t)v; w[o + 1] = (uint8_t)(v >> 8); };
    memcpy(w.data(), "RIFF", 4); put32(4, 36 + bytes); memcpy(w.data() + 8, "WAVEfmt ", 8); put32(16, 16); put16(20, 1); put16(22, (uint32_t)nch);
    put32(24, rate); put32(28, rate * (uint32_t)nch * 2); put16(32, (uint32_t)nch * 2); put16(34, 16); memcpy(w.data() + 36, "data", 4); put32(40, bytes);
    for (uint32_t i = 0; i < frames; i++)
      for (int c = 0; c < nch; c++) put16(44 + ((size_t)i * (uint32_t)nch + (size_t)c) * 2, (uint16_t)pcm[(size_t)c][i]);
    return w;
  } catch (const std::exception&) {
    return no("the stream is cut short");
  }
}

bool write_pack_sounds(const RomFs& r, const std::string& pack_dir, std::string* why) {
  const Bytes w = mega_jingle_wav(r, why);
  if (w.empty()) return false;
  PackFiles{pack_dir, false}.write_bytes("sound/mega_evolution.wav", w);
  std::string why2;
  const Bytes fx = mega_fx_bin(r, &why2);
  if (!fx.empty()) PackFiles{pack_dir, false}.write_bytes("mega/usum_mega.bin", fx);
  else if (why) *why = "no mega/usum_mega.bin: " + why2;
  return true;
}

RowBridge build_row_bridge(Usum& u, const Json& tables, const XdTablesCache& xd, const std::set<int>& pre, int nat, int record, int pkx_row, int standin) {
  Ctx c{u, tables, xd, pre};
  RowOut r = build_row(c, nat, standin, record, pkx_row, nullptr);
  return {r.row, r.rep, r.notes};
}

std::map<int, DumpCry> dump_cries(Usum& u, const std::string& data_dir, const std::vector<int>& nats) {
  const Json cw = read_json(data_dir + "/gen57_cry_waves.json");
  const Garc& cg = u.garc("a/0/7/3");
  std::map<int, DumpCry> out;
  for (int nat : nats) {
    const Json* wj = cw.find(std::to_string(nat));
    if (!wj) continue;
    const size_t w = (size_t)wj->i;
    if (4368 + w >= cg.count()) continue;
    DumpCry c;
    c.wave = (int)w;
    std::vector<std::string> notes;
    pbr::cry_build_xd(parse_cwav(cwar_first(cg.read(4368 + w))), c.hdr, c.body, notes);
    out[nat] = std::move(c);
  }
  return out;
}

void make_gen57_data(const DataOptions& o, const Plan& plan, const ModelResults& mr) {
  auto log = [&](const std::string& s) { if (o.log) o.log(s); };
  Usum u(o.rom);
  const Json tables = read_json(o.data_dir + "/gen57_tables.json");
  const Json cw = read_json(o.data_dir + "/gen57_cry_waves.json");
  const XdTablesCache xd = load_xd_tables(o.data_mods_exe, o.xd_disc, o.work);
  std::vector<int> nats;
  for (int n = FIRST; n <= LAST; n++) if (o.only.empty() || o.only.count(n)) nats.push_back(n);
  std::map<int, int> standin;
  for (auto& it : plan.items) if (it.pack == "g57") standin[it.nat] = it.standin_nat;
  Ctx c{u, tables, xd, {}};
  for (int n = 1; n <= LAST; n++) for (auto& e : u.evolutions(n)) c.pre.insert(e.species);

  const std::string out = fs::absolute(o.out).string();
  const std::string tmp = out + ".tmp";
  std::error_code ec;
  fs::remove_all(tmp, ec);
  PackFiles pf{tmp, o.eol_crlf};
  fs::create_directories(tmp + "/files");
  fs::create_directories(tmp + "/data");
  fs::create_directories(tmp + "/reports");
  fs::create_directories(tmp + "/textures/faces");

  std::map<int, Json> mrep;
  std::vector<int> ok;
  auto disc = open_iso(o.xd_disc);
  std::string toc_name;
  for (auto& f : disc->files()) { std::string l = f.first; for (auto& ch : l) ch = (char)tolower((unsigned char)ch); if (l == "gsfsys.toc") toc_name = f.first; }
  if (toc_name.empty()) throw Fail("the XD disc has no GSfsys.toc", 2);
  if (mr.present) {
    for (int n : nats) {
      auto it = mr.species.find(n);
      if (it == mr.species.end()) continue;
      Json r = it->second;
      if (r.find("error")) { mrep[n] = r; continue; }
      mrep[n] = r;
      const Json* sz = r.find("size");
      if (sz && sz->i > 614400) { r.set("left_out", Json("over the 614400-byte file cap (" + std::to_string(sz->i) + " bytes): stand-in model")); mrep[n] = r; continue; }
      ok.push_back(n);
    }
  }
  if (mr.present && !o.models_dir.empty()) {
    for (int n : nats) {
      const fs::path src = fs::path(o.models_dir) / "gen57" / "files" / ("pkx_n3_" + std::to_string(n) + ".fsys");
      if (fs::exists(src)) fs::copy_file(src, fs::path(tmp) / "files" / src.filename(), fs::copy_options::overwrite_existing);
    }
  }
  for (auto& kv : mrep) pf.write("reports/" + std::to_string(kv.first) + ".json", kv.second.dump(-1));
  const Bytes g4toc = slurp_bytes(o.gen4_pack + "/files/GSfsys.toc");
  {
    std::vector<std::pair<uint32_t, std::string>> rows;
    for (int n : ok) rows.push_back({(uint32_t)(PKX_GROUP_FIRST + n - FIRST), "pkx_n3_" + std::to_string(n)});
    if (o.cries) rows.push_back({(uint32_t)CRY57_GROUP, CRY57_ARCHIVE});
    pf.write_bytes("files/" + toc_name, pbr::pack_extend_toc(g4toc, rows));
  }
  std::set<int> okset(ok.begin(), ok.end());
  std::vector<std::string> pkx_rows;
  for (int n = FIRST; n <= LAST; n++)
    pkx_rows.push_back(okset.count(n) ? "pkx " + std::to_string(n + PKX_SHIFT) + " " + std::to_string(PKX_GROUP_FIRST + n - FIRST) + " " + std::to_string(PKX_RES_ID)
                                      : "pkx " + std::to_string(n + PKX_SHIFT) + " 0 0");

  std::vector<std::string> cry_rows;
  std::map<int, std::pair<int, int>> cry_edit;
  std::map<int, Json> cry_notes;
  if (o.cries) {
    const std::string g4tables = slurp_text(o.gen4_pack + "/tables.txt");
    int n4 = 0, w4 = 0;
    {
      std::istringstream is(g4tables);
      std::string ln;
      while (std::getline(is, ln)) {
        if (ln.compare(0, 13, "row snd_file ") == 0) n4++;
        if (ln.compare(0, 5, "wave ") == 0) w4++;
      }
    }
    const Garc& cg = u.garc("a/0/7/3");
    std::vector<Bytes> waves;
    for (size_t i = 4368; i < cg.count(); i++) waves.push_back(cwar_first(cg.read(i)));
    Bytes cd = disc->read("common.fsys"), rel;
    for (auto& e : pbr::fsys_entries(cd)) if (e.name == "common_rel") rel = pbr::fsys_extract(cd, e);
    const uint8_t sig[12] = {0, 35, 0, 55, 0, 30, 0, 50, 0, 40, 0, 90};
    auto sit = std::search(rel.begin(), rel.end(), sig, sig + 12);
    if (sit == rel.end()) throw Fail("species table not found in common_rel", 2);
    const long long base = (long long)(sit - rel.begin()) - 0x8E - 25 * 292;
    auto voice_of = [&](int n) { size_t at = (size_t)(base + (long long)n * 292 + 0x0C); return (int)(rel.at(at) << 8 | rel.at(at + 1)); };
    if (n4 > CRY_RESERVED || w4 > CRY_RESERVED) throw Fail("the Gen 4 pack has more cries than the Gen 5-7 pack left room for");
    std::vector<pbr::FsMember> new_h, new_b;
    int j = 0;
    for (int nat : nats) {
      const Json* wj = cw.find(std::to_string(nat));
      if (!wj) continue;
      const int w = (int)wj->i;
      std::vector<uint8_t> hdr, body;
      std::vector<std::string> nt;
      auto cs = parse_cwav(waves.at((size_t)w));
      pbr::cry_build_xd(cs, hdr, body, nt);
      const int n = CRY_RESERVED + j;
      const uint32_t hid = 0x100 + 2 * (uint32_t)j, bid = 0x101 + 2 * (uint32_t)j;
      pbr::FsMember h, b;
      h.word0 = (hid << 16) | (20u << 9); h.type = 20; h.name = "(null)"; h.data = hdr; h.size = (uint32_t)hdr.size();
      b.word0 = (bid << 16) | (21u << 9); b.type = 21; b.name = "(null)"; b.data = body; b.size = (uint32_t)body.size();
      new_h.push_back(h);
      new_b.push_back(b);
      cry_rows.push_back("row snd_file " + std::to_string(SND_FILE_COUNT + n) + " 1 -1 0:4:" + std::to_string(CRY57_GROUP) + " 4:4:" + std::to_string(h.word0) + " 8:4:" + std::to_string(b.word0));
      cry_rows.push_back("row snd_data " + std::to_string(SND_DATA_COUNT + n) + " 1 " + std::to_string(voice_of(standin.at(nat))) + " 6:2:" + std::to_string(SND_FILE_COUNT + n));
      const auto wd = pbr::cry_wave_disp(hdr, body);
      static const char* hx = "0123456789abcdef";
      std::string hs;
      for (uint8_t x : wd) { hs += hx[x >> 4]; hs += hx[x & 15]; }
      cry_rows.push_back("wave " + std::to_string(nat) + " " + hs);
      cry_edit[nat] = {WAVE_OLD + CRY_RESERVED + j, SND_DATA_COUNT + n};
      Json cn = Json::object();
      cn.set("wave", Json(w));
      cn.set("notes", jstrs(nt));
      cry_notes[nat] = cn;
      j++;
    }
    std::vector<pbr::FsMember> all;
    for (auto& e : new_h) all.push_back(e);
    for (auto& e : new_b) all.push_back(e);
    if (j) pf.write_bytes(std::string("files/") + CRY57_ARCHIVE + ".fsys", pbr::pack_build_fsys((uint32_t)CRY57_GROUP, all));
  }

  std::set<int> has_icon;
  {
    std::map<int, int> num2nat;
    const Json mnfile = read_json(o.data_dir + "/gen57_model_numbers.json");
    const Json& mn = jget(mnfile, "model_number");
    for (int n = FIRST; n <= LAST; n++) {
      const Json* v = mn.find(std::to_string(n));
      num2nat[n < 650 ? n : (int)(v->kind == Json::Str ? atoi(v->s.c_str()) : v->i)] = n;
    }
    const Garc& g = u.garc("a/2/5/7");
    std::map<int, std::pair<int, Rgba>> best;
    for (size_t i = 0; i < g.count(); i++) {
      Bytes b = lz11_maybe(g.read(i));
      std::string head((const char*)b.data(), std::min<size_t>(0x80, b.size()));
      int num = -1;
      std::string variant;
      for (size_t p = head.find("poke_icon_"); p != std::string::npos && num < 0; p = head.find("poke_icon_", p + 1)) {
        if (p + 14 > head.size() || !isdigit((unsigned char)head[p + 10]) || !isdigit((unsigned char)head[p + 11]) || !isdigit((unsigned char)head[p + 12]) || head[p + 13] != '_') continue;
        const size_t q = p + 14;
        for (size_t k = 0; q + k < head.size(); k++) {
          if (head.compare(q + k, 4, "_001") == 0) { num = atoi(head.substr(p + 10, 3).c_str()); variant = head.substr(q, k); break; }
          const char ch = head[q + k];
          if (!((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '_')) break;
        }
      }
      if (num < 0) continue;
      auto it = num2nat.find(num);
      if (it == num2nat.end()) continue;
      const int rank = (variant == "m" || variant == "f") ? 0 : variant.compare(0, 6, "normal") == 0 ? 1 : 2;
      auto bt = best.find(it->second);
      if (bt != best.end() && bt->second.first <= rank) continue;
      if (b.size() < 0x80) continue;
      GfTexture t;
      t.w = b[0x68] | (b[0x69] << 8); t.h = b[0x6A] | (b[0x6B] << 8); t.fmt = b[0x6C] | (b[0x6D] << 8);
      t.data.assign(b.begin() + 0x80, b.end());
      Rgba px = decode_texture(t);
      Rgba flipped;
      flipped.w = px.w; flipped.h = px.h; flipped.px.resize(px.px.size());
      for (int y = 0; y < px.h; y++) memcpy(&flipped.px[(size_t)y * px.w * 4], &px.px[(size_t)(px.h - 1 - y) * px.w * 4], (size_t)px.w * 4);
      best[it->second] = {rank, std::move(flipped)};
    }
    for (auto& kv : best) {
      Image8 ic, face;
      ic.w = kv.second.second.w; ic.h = kv.second.second.h; ic.ch = 4; ic.px = kv.second.second.px;
      if (!box_icon_face(ic, &face)) continue;
      pf.write_bytes("textures/faces/" + std::to_string(kv.first) + ".png", png_encode(face));
      has_icon.insert(kv.first);
    }
  }

  Json mbattle;
  Json mreport_moves;
  std::vector<std::array<int, 3>> pending;
  Json report = Json::array();
  std::map<int, Json> rows_us;
  Json move_langs_dummy;
  std::vector<std::string> mlangs(kDumpLang, kDumpLang + 6);
  MovesBuilt mb = build_all_moves(u, tables, xd.moves, mlangs);
  mbattle = mb.battle;
  mreport_moves = mb.report;
  for (int li = 0; li < 6; li++) {
    const std::string lg = kLangs[li], dl = kDumpLang[li];
    const std::string folder = lg == "us" ? "data" : "data_" + lg;
    fs::create_directories(tmp + "/" + folder);
    Json moves = Json::object();
    for (auto& kv : mb.rows[(size_t)li]) {
      Json r = Json::object();
      for (auto& f : kv.second.o) if (f.first.compare(0, 1, "_") != 0) r.set(f.first, f.second);
      moves.set(kv.first, r);
    }
    if (!moves.o.empty()) {
      Json top = Json::object(), add = Json::object();
      top.set("_note", Json(kNote));
      add.set("moves", moves);
      top.set("add", add);
      pf.write(folder + "/01_moves.json", top.dump(1, false));
    }
    Json species = Json::object();
    for (int nat : nats) {
      std::vector<std::array<int, 3>> pend;
      RowOut r = build_row(c, nat, standin.at(nat), nat, nat + PKX_SHIFT, &pend);
      Json row = r.row;
      row.set("text", text_for(u, nat, dl));
      if (has_icon.count(nat)) row.set("face", Json("textures/faces/" + std::to_string(nat) + ".png"));
      row.set("icon", Json("textures/icons/" + std::to_string(nat) + ".png"));
      if (auto ce = cry_edit.find(nat); ce != cry_edit.end()) {
        row.set("nakigoeId", Json(ce->second.first));
        row.set("voice", Json(ce->second.second));
      }
      species.set(u.species_name(nat, "en"), row);
      if (lg == "us") {
        rows_us[nat] = row;
        for (auto& p : pend) pending.push_back(p);
        Json rep = r.rep;
        rep.set("notes", jstrs(r.notes));
        rep.set("nat", Json(nat));
        rep.set("name", Json(u.species_name(nat, "en")));
        rep.set("row", Json(nat + ROW_SHIFT));
        rep.set("standin", Json(standin.at(nat)));
        auto m = mrep.find(nat);
        rep.set("model", m != mrep.end() ? m->second : Json());
        const Json* cwv = cw.find(std::to_string(nat));
        rep.set("cry_wave", cwv ? *cwv : Json());
        auto cn = cry_notes.find(nat);
        rep.set("cry", cn != cry_notes.end() ? cn->second : Json());
        report.push(rep);
      }
    }
    Json top = Json::object(), add = Json::object();
    top.set("_note", Json(kNote));
    add.set("pokemon", species);
    top.set("add", add);
    pf.write(folder + "/02_species.json", top.dump(1, false));
  }

  if (nats.size() == (size_t)(LAST - FIRST + 1)) {
    const Json& s57 = jget(jget(tables, "all"), "spots57");
    Json data = Json::object();
    data.set("_note", Json("Poke Spot odds with the Gen 4 and Gen 5-7 sets: the originals 45 %, the five Gen 4 entries 5 % each, the five Gen 5-7 entries 6 % each"));
    std::vector<std::string> rows;
    static const int orig[3] = {24, 16, 5};
    for (const auto& st : pbr::kSpots) {
      Json d = Json::object();
      for (int i = 0; i < 3; i++) {
        Json pr = Json::object();
        pr.set("prob, rate", jints({orig[i], st.rates[i]}));
        d.set("#" + std::to_string(i), pr);
      }
      int k = 0;
      for (const auto& ad : st.adds) {
        Json pr = Json::object();
        pr.set("prob, rate", jints({5, st.rates[ad.rate_of]}));
        d.set("#" + std::to_string(3 + k), pr);
        k++;
      }
      data.set(st.table, d);
      const Json& lst = jget(s57, st.table.c_str());
      static const int rate_of[5] = {0, 1, 2, 2, 2};
      for (size_t kk = 0; kk < lst.a.size(); kk++) {
        const int ro = rate_of[kk];
        rows.push_back("row " + st.table + " " + std::to_string(8 + kk) + " 1 " + std::to_string(ro) + " 0:1:10 1:1:" + std::to_string(st.top) + " 2:2:" +
                       std::to_string(lst.a[kk].i + ROW_SHIFT) + " 4:4:6 8:4:" + std::to_string(st.rates[ro]));
      }
    }
    pf.write("optional/spots.json", data.dump(1, true));
    std::string t = "format 1";
    for (auto& r : rows) t += "\n" + r;
    pf.write("optional/spots_tables.txt", t + "\n");
  }

  std::vector<std::string> rz, names;
  {
    const std::set<int> legend = [&] { std::set<int> s; for (auto& x : jget(jget(tables, "all"), "legend57").a) s.insert((int)x.i); return s; }();
    for (int nat : nats) {
      const Json& row = rows_us.at(nat);
      std::vector<int> evo;
      const Json& tg = jget(row, "sinka.pokemonDataId");
      std::vector<std::string> targets;
      for (const Json& t : tg.a) if (t.kind == Json::Str && !t.s.empty()) targets.push_back(t.s);
      for (const EvoRec& e : u.evolutions(nat)) {
        const std::string tname = u.species_name(e.species);
        const Json* rule = evo_rule(tables, u.species_name(nat), tname);
        if (rule && rule->a.at(2).s != "trade") targets.push_back(tname);
      }
      targets.resize(std::max<size_t>(targets.size(), 5));
      targets.resize(5);
      for (int k = 0; k < 5; k++) {
        int tn = 0;
        if (!targets[(size_t)k].empty()) {
          for (int n = FIRST; n <= LAST; n++) if (u.species_name(n, "en") == targets[(size_t)k]) { tn = n; break; }
        }
        evo.push_back(tn ? tn + ROW_SHIFT : 0);
      }
      std::vector<int> bs = ints_of(jget(row, "basisStatus"));
      bs.resize(6);
      rz.push_back("sp " + std::to_string(nat + ROW_SHIFT) + " " + std::to_string(nat) + " " + (legend.count(nat) ? "1" : "0") + " " + join_ints(bs) + " | " +
                   join_ints(ints_of(jget(row, "zokuseiDataId"))) + " | " + join_ints(ints_of(jget(row, "tokuseiDataId"))) + " | " + join_ints(evo) + " | " +
                   join_ints(ints_of(jget(row, "waza.level"))) + " | " + join_ints(ints_of(jget(row, "waza.dataId"))));
      std::string nm = u.species_name(nat, "en");
      std::replace(nm.begin(), nm.end(), ' ', '_');
      names.push_back("species " + std::to_string(nat) + " " + std::to_string(nat + ROW_SHIFT) + " " + nm);
    }
  }
  {
    std::string t;
    for (auto& l : rz) t += l + "\n";
    pf.write("randomizer.txt", t);
  }
  {
    const Json& ev = jget(tables, "evolutions");
    std::string t = "format 1\n";
    for (int nat : nats) {
      const std::string sname = u.species_name(nat);
      std::string s = sname;
      std::replace(s.begin(), s.end(), ' ', '_');
      for (const EvoRec& e : u.evolutions(nat)) {
        const std::string tname = u.species_name(e.species);
        const Json* rule = evo_rule(tables, sname, tname);
        if (!rule || rule->a.at(2).s == "trade") continue;
        std::string tt = tname;
        std::replace(tt.begin(), tt.end(), ' ', '_');
        const std::string how = rule->a.at(2).s, item = rule->a.at(3).s;
        if (how == "item") t += "evo " + s + " 7 " + evo_item_ref(tables, item) + " " + tt + "\n";
        else if (how == "held") t += "evo " + s + " 6 " + evo_item_ref(tables, item) + " " + tt + "\nevo " + s + " 7 " + evo_item_ref(tables, item) + " " + tt + "\n";
        else if (how == "level_dark") t += "evo " + s + " 20 " + std::to_string(e.level) + " " + tt + "\n";
        else if (how == "level_female") t += "evo " + s + " 21 " + std::to_string(e.level) + " " + tt + "\n";
        else if (how == "knows_move") {
          int lvl = 1;
          for (auto& lm : u.levelup(nat)) if (lm.second == e.arg) { lvl = lm.first; break; }
          t += "evo " + s + " 22 #" + std::to_string(evo_move_row(e.arg)) + " " + tt + " " + std::to_string(std::max(lvl, 1)) + "\n";
        }
      }
    }
    for (const Json& n : jget(ev, "shop_order").a) t += "shop " + evo_item_ref(tables, n.s) + "\n";
    for (auto& kv : jget(ev, "items").o) t += "shop " + evo_item_ref(tables, kv.first) + "\n";
    pf.write("evolutions.txt", t);
    for (auto& kv : jget(ev, "folders").o) {
      if (!fs::is_directory(tmp + "/" + kv.first)) continue;
      const std::string lg = kv.second.s, dl = jget(jget(ev, "dump_lang"), lg.c_str()).s;
      Json items = Json::object();
      for (auto& it : jget(ev, "items").o) {
        Json x = Json::object(), text = Json::object();
        x.set("copy", Json("Thunder Stone"));
        text.set("name", Json(shown(u.item_name((int)it.second.i, dl))));
        text.set("doc", jget(jget(ev, "item_doc"), lg.c_str()));
        x.set("text", text);
        x.set("coupon", jget(ev, "coupon"));
        items.set(it.first, x);
      }
      Json add = Json::object(), top = Json::object();
      add.set("items", items);
      top.set("_note", Json("made by gen57_evolutions.py: the Sachet and the Whipped Dream (items that evolve Spritzee and Swirlix; 0.7.5; placeholder price)"));
      top.set("add", add);
      pf.write(kv.first + "/06_evolution_items.json", top.dump(1, false));
      Json citems = Json::object(), top2 = Json::object();
      for (auto& cl : jget(ev, "classic").o) {
        Json cp = Json::object();
        cp.set("coupon", jget(ev, "coupon"));
        citems.set("#" + std::to_string(cl.second.i), cp);
      }
      top2.set("_note", Json("made by gen57_evolutions.py: the six classic stones get a Poke Coupon price, so the prize desk can sell them (XD only: XG sells them itself; 0.7.5; placeholder price)"));
      top2.set("items", citems);
      pf.write(kv.first + "/07_stone_coupons.json", top2.dump(1, false));
    }
  }
  Json top = Json::object();
  top.set("_note", Json("made by gen57_all.py: what the new moves do (docs/modding/battle-effects.md)"));
  top.set("moves", mbattle);
  pf.write("battle/moves.json", top.dump(1, true));
  pf.write("moves_report.json", mreport_moves.dump(1, true));
  std::vector<std::string> fairy;
  for (auto& r : mreport_moves.a) if (jget(r, "type").i == 17) fairy.push_back(jget(r, "name").s);
  std::vector<int> abil_ids;
  for (int a = 124; a < u.ability_count(); a++) abil_ids.push_back(a);
  Json adefs, areport;
  build_all_abilities(u, tables, adefs, areport);
  std::set<int> built;
  for (auto& kv : adefs.o) built.insert((int)jget(kv.second, "id").i);
  pf.write("abilities.txt", ability_lines(u, tables, abil_ids, built));
  Json atop = Json::object();
  atop.set("_note", Json("made by gen57_all.py: what the new abilities do (docs/modding/battle-effects.md)"));
  atop.set("abilities", adefs);
  pf.write("battle/abilities.json", atop.dump(1, true));
  pf.write("abilities_report.json", areport.dump(1, true));
  {
    Json h = Json::object(), hh = Json::object();
    for (auto& kv : build_hidden(u, 807)) hh.set(std::to_string(kv.first), Json(kv.second));
    h.set("_note", Json("made by gen57_hidden.py from the player's own dump: the hidden ability of each species by National Dex number (PLACEHOLDER rule: docs/GEN57.md)"));
    h.set("hidden_abilities", hh);
    pf.write("battle/hidden_abilities.json", h.dump(0, true));
  }
  {
    std::string t = "format 1\npkxcount 419";
    for (auto& l : cry_rows) t += "\n" + l;
    for (auto& l : pkx_rows) t += "\n" + l;
    for (auto& l : names) t += "\n" + l;
    pf.write("tables.txt", t + "\n", false, true);
  }
  {
    Json pj = Json::array();
    for (auto& p : pending) {
      Json x = Json::object();
      x.set("species", Json(p[0]));
      x.set("level", Json(p[1]));
      x.set("usum_move", Json(p[2]));
      x.set("xd_row", Json(p[2] + 20));
      pj.push(x);
    }
    pf.write("pending_moves.json", pj.dump(-1));
  }
  {
    Json g = Json::object();
    g.set("id", Json("ultra-sun-moon-gen57"));
    g.set("version", Json(PACK_VERSION));
    g.set("species", Json((int)nats.size()));
    g.set("moves", Json((int)mreport_moves.a.size()));
    g.set("fairy_moves", jstrs(fairy));
    g.set("abilities", Json((int)abil_ids.size()));
    g.set("game", Json("GXXE01"));
    g.set("generated_from", Json(u.prod()));
    g.set("needs", Json("gen4-rows"));
    g.set("evolution_rule", jget(jget(tables, "all"), "evo_notes"));
    if (o.cries) {
      g.set("cry_archive", Json("own"));
      g.set("cry_rows_reserved", Json(CRY_RESERVED));
      g.set("gen4_toc", Json(fnv64_hex(g4toc)));
    }
    pf.write("gen57.json", g.dump(1, true));
  }
  {
    std::string why;
    if (write_pack_sounds(u.rom(), tmp, &why)) log("sound/mega_evolution.wav written");
    else log("no Mega Evolution jingle: " + why);
  }
  pf.write("report.json", report.dump(1, true));
  fs::remove_all(out, ec);
  fs::rename(tmp, out);
  log("pack " + out + " " + std::to_string(nats.size()) + " species " + std::to_string(ok.size()) + " models " + std::to_string(pending.size()) +
      " pending move entries " + std::to_string(abil_ids.size()) + " abilities");
}

}
