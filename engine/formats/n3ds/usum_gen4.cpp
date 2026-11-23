#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>

#include "usum_data_internal.h"

namespace fs = std::filesystem;

namespace n3ds {

namespace {

constexpr int LAST_MOVE = 467, SPECIES_TOTAL = 501;

int type7to4(int t) { return t < 9 ? t : (t <= 16 ? t + 1 : -1); }
int evo_method7to4(int m) {
  if (m >= 1 && m <= 6) return m;
  if (m >= 8 && m <= 27) return m - 1;
  return 0;
}
uint16_t u16le(const Bytes& b, size_t o) { return (uint16_t)(b.at(o) | b.at(o + 1) << 8); }
void be16(Bytes& o, size_t at, uint32_t v) { o.at(at) = (uint8_t)(v >> 8); o.at(at + 1) = (uint8_t)v; }
void be32(Bytes& o, size_t at, uint32_t v) { for (int i = 0; i < 4; i++) o.at(at + (size_t)i) = (uint8_t)(v >> (24 - 8 * i)); }
void append(Bytes& a, const Bytes& b) { a.insert(a.end(), b.begin(), b.end()); }

struct Tables {
  Json j;
  std::vector<int> tm;
  std::vector<std::string> tm7;
  explicit Tables(const std::string& path) : j(read_json(path)) {
    for (auto& x : jget(j, "tm").a) tm.push_back((int)x.i);
    for (auto& x : jget(j, "tm7").a) tm7.push_back(x.s);
  }
};

struct Names { std::vector<std::u32string> species, moves, abilities, items; };

Names find_names(Usum& u) {
  struct Want { const char* key; const char* a; const char* b; std::vector<std::u32string>* dst; bool got = false; };
  Names n;
  Want want[4] = {{"species", "Bulbasaur", "Ivysaur", &n.species}, {"moves", "Pound", "Karate Chop", &n.moves}, {"abilities", "Stench", "Drizzle", &n.abilities},
                  {"items", "Master Ball", "Ultra Ball", &n.items}};
  const Garc& g = u.garc(u.text_archive());
  for (size_t e = 0; e < g.count(); e++) {
    std::vector<std::u32string> t;
    try { t = decode_text_units(u.read_entry(u.text_archive(), e)); } catch (const std::exception&) { continue; }
    if (t.size() < 100) continue;
    for (auto& w : want) {
      if (w.got || t.size() <= 2) continue;
      if (to_utf8(t[1]) == w.a && to_utf8(t[2]) == w.b) { *w.dst = t; w.got = true; }
    }
  }
  std::string missing;
  for (auto& w : want) if (!w.got) missing += (missing.empty() ? "" : ", ") + std::string(w.key);
  if (!missing.empty()) throw Fail("3DS dump: the English text for " + missing + " was not found");
  return n;
}

std::u32string name_of(const std::vector<std::u32string>& v, int i) { return i >= 0 && (size_t)i < v.size() ? v[(size_t)i] : std::u32string(); }

struct Gen4 {
  Usum& u;
  Tables T;
  bool on;
  Names names;
  Gen4(Usum& u, const std::string& data_dir, bool on) : u(u), T(data_dir + "/gen4_tables.json"), on(on), names(find_names(u)) {}

  std::vector<std::pair<int, int>> learnset7(int dex) {
    Bytes d = u.read_entry(u.levelup_archive(), (size_t)dex);
    std::vector<std::pair<int, int>> out;
    for (size_t k = 0; k + 3 < d.size(); k += 4) {
      const int m = (int16_t)u16le(d, k), lv = (int16_t)u16le(d, k + 2);
      if (m < 0) break;
      out.push_back({lv, m});
    }
    return out;
  }
  struct Evo7 { int m, p, s, form, lv; };
  std::vector<Evo7> evolutions7(int dex) {
    Bytes d = u.read_entry("a/0/1/4", (size_t)dex);
    std::vector<Evo7> out;
    for (int k = 0; k < 8; k++) {
      const size_t o = (size_t)k * 8;
      Evo7 e{u16le(d, o), u16le(d, o + 2), u16le(d, o + 4), d.at(o + 6), d.at(o + 7)};
      if (e.m) out.push_back(e);
    }
    return out;
  }
  Bytes move_record(int m) {
    Bytes d = u.read_entry("a/0/1/1", 0);
    const size_t n = u16le(d, 2);
    std::vector<uint32_t> offs;
    for (size_t i = 0; i <= n; i++) offs.push_back((uint32_t)(u16le(d, 4 + 4 * i) | u16le(d, 6 + 4 * i) << 16));
    return Bytes(d.begin() + offs.at((size_t)m), d.begin() + offs.at((size_t)m + 1));
  }

  void tm_bits(int dex, const std::set<int>& learn, const std::set<int>* pub, uint64_t bits[2]) {
    bits[0] = bits[1] = 0;
    auto setbit = [&](size_t k, bool on_) { if (on_) bits[k / 64] |= (uint64_t)1 << (k % 64); };
    if (pub) {
      for (size_t k = 0; k < T.tm.size(); k++) setbit(k, pub->count(T.tm[k]) != 0);
      return;
    }
    Bytes d = u.personal_raw(dex);
    std::map<std::string, size_t> g7m;
    for (size_t j = 0; j < T.tm7.size(); j++) g7m.emplace(T.tm7[j], j);
    for (size_t k = 0; k < T.tm.size(); k++) {
      const std::string nm = to_utf8(name_of(names.moves, T.tm[k]));
      bool bit;
      auto it = g7m.find(nm);
      if (it != g7m.end()) bit = (d.at(0x28 + it->second / 8) >> (it->second % 8)) & 1;
      else bit = learn.count(T.tm[k]) != 0;
      setbit(k, bit);
    }
  }

  Bytes personal_record(int dex) {
    Bytes d = u.personal_raw(dex);
    std::set<int> learn;
    for (auto& lm : learnset7(dex)) if (lm.second > 0 && lm.second <= LAST_MOVE) learn.insert(lm.second);
    Bytes r(52, 0);
    std::set<int> pub;
    bool have_pub = false;
    if (on) {
      have_pub = true;
      if (const Json* m = jget(T.j, "machines").find(std::to_string(dex))) for (auto& x : m->a) pub.insert((int)x.i);
    }
    uint64_t bits[2];
    tm_bits(dex, learn, have_pub ? &pub : nullptr, bits);
    for (int w = 0; w < 4; w++) be32(r, (size_t)(4 * w), (uint32_t)((bits[w / 2] >> (32 * (w % 2))) & 0xFFFFFFFFu));
    int held[2] = {u16le(d, 0x0C), u16le(d, 0x0E)};
    if (on) {
      held[0] = held[1] = 0;
      if (const Json* h = jget(T.j, "held_items").find(std::to_string(dex))) { held[0] = (int)h->a.at(0).i; held[1] = (int)h->a.at(1).i; }
    }
    be16(r, 16, (uint32_t)held[0]); be16(r, 18, (uint32_t)held[1]);
    be16(r, 20, (uint32_t)(u16le(d, 0x24) / 10));
    be16(r, 22, u16le(d, 0x26));
    for (int i = 0; i < 6; i++) r[(size_t)(30 + i)] = d[(size_t)i];
    const int t1 = type7to4(d[6]), t2 = type7to4(d[7]);
    r[36] = (uint8_t)(t1 >= 0 ? t1 : 0); r[37] = (uint8_t)(t2 >= 0 ? t2 : 0);
    r[38] = d[8];
    r[39] = (uint8_t)std::min<int>(u16le(d, 0x22), 255);
    const int ev7 = u16le(d, 0x0A);
    int evv = 0;
    for (int k = 0; k < 6; k++) evv += ((ev7 >> (2 * k)) & 3) << (14 - 2 * k);
    be16(r, 40, (uint32_t)evv);
    r[42] = d[0x12]; r[43] = d[0x13]; r[44] = d[0x14]; r[45] = d[0x15];
    r[46] = d[0x16]; r[47] = d[0x17]; r[48] = d[0x18]; r[49] = d[0x19];
    if (on) {
      static const std::map<std::string, int> off = {{"hp", 30}, {"atk", 31}, {"def", 32}, {"spe", 33}, {"spa", 34}, {"spd", 35}, {"t1", 36}, {"t2", 37}, {"catch", 38}, {"exp", 39}};
      if (const Json* sp = jget(T.j, "species").find(std::to_string(dex)))
        for (auto& kv : sp->o) r[(size_t)off.at(kv.first)] = (uint8_t)std::min<int64_t>(kv.second.i, 255);
    }
    return r;
  }

  Bytes evolution_record(int dex) {
    Bytes out(42, 0);
    size_t k = 0;
    for (const Evo7& e : evolutions7(dex)) {
      if (e.s > 493 || (e.form != 0 && e.form != 255)) continue;
      const int m4 = evo_method7to4(e.m);
      if (!m4 || k >= 7) continue;
      int p = e.p;
      if (m4 == 4 || m4 == 8 || m4 == 9 || m4 == 10 || m4 == 22 || m4 == 23 || ((m4 == 11 || m4 == 12 || m4 == 13) && p == 0)) p = e.lv;
      be16(out, 6 * k, (uint32_t)m4); be16(out, 6 * k + 2, (uint32_t)p); be16(out, 6 * k + 4, (uint32_t)e.s);
      k++;
    }
    return out;
  }

  Bytes learnset_record(int dex) {
    std::vector<std::pair<int, int>> src;
    bool have4 = false;
    if (on) {
      if (const Json* l = jget(T.j, "levelup").find(std::to_string(dex))) { have4 = true; for (auto& x : l->a) src.push_back({(int)x.a.at(0).i, (int)x.a.at(1).i}); }
    }
    if (!have4) src = learnset7(dex);
    std::set<int> seen;
    std::vector<std::pair<int, int>> rows;
    for (auto& lm : src) {
      const int lv = lm.first, m = lm.second;
      if (!(m > 0 && m <= LAST_MOVE) || seen.count(m)) continue;
      seen.insert(m);
      rows.push_back({std::max(lv, 1), m});
    }
    std::stable_sort(rows.begin(), rows.end(), [](auto& a, auto& b) { return a.first < b.first; });
    if (rows.size() > 20) rows.erase(rows.begin(), rows.end() - 20);
    Bytes out(80, 0);
    for (size_t k = 0; k < 20; k++) {
      if (k < rows.size()) { be16(out, 4 * k, (uint32_t)rows[k].second); out[4 * k + 2] = (uint8_t)rows[k].first; out[4 * k + 3] = 0; }
      else { out[4 * k + 3] = 0xD0; break; }
    }
    return out;
  }

  static int effect_chance(const Bytes& u7) {
    const int inflict = u7[8] | (u7[9] << 8);
    if (inflict == 8) return 0;
    if (u7[10]) return u7[10];
    if (u7[15]) return u7[15];
    for (int k = 0; k < 3; k++) if (u7[(size_t)(21 + k)] && u7[(size_t)(27 + k)]) return u7[(size_t)(27 + k)];
    return 0;
  }

  Bytes move_row(int m) {
    Bytes u7 = move_record(m);
    Bytes r(24, 0);
    const std::string name = to_utf8(name_of(names.moves, m));
    int eff = m;
    if (m > 354) { const Json* d = jget(T.j, "donor_of").find(name); eff = d ? (int)d->i : 1000 + m; }
    be16(r, 0, (uint32_t)eff);
    r[14] = u7[2] == 1 ? 0 : u7[2] == 2 ? 1 : 2;
    r[15] = u7[3];
    const int t = type7to4(u7[0]);
    r[16] = (uint8_t)(t >= 0 ? t : 0);
    r[17] = u7[4] >= 101 ? 0 : u7[4];
    r[18] = u7[5];
    r[19] = (uint8_t)effect_chance(u7);
    r[20] = u7[6];
    const uint32_t flags = (uint32_t)(u7[36] | u7[37] << 8 | u7[38] << 16 | (uint32_t)u7[39] << 24);
    static const int fb[5][2] = {{0, 0}, {3, 1}, {4, 2}, {5, 3}, {6, 4}};
    int f = 0;
    for (auto& b : fb) f |= ((flags >> b[0]) & 1) << b[1];
    r[21] = (uint8_t)f;
    if (m > 354) {
      if (on) {
        if (const Json* mv = jget(T.j, "moves").find(name)) {
          static const std::map<std::string, int> off = {{"power", 15}, {"acc", 17}, {"pp", 18}, {"chance", 19}};
          for (auto& kv : mv->o) r[(size_t)off.at(kv.first)] = (uint8_t)kv.second.i;
        }
        if (const Json* fl = jget(T.j, "move_flags").find(name)) {
          for (int i = 0; i < 4; i++) {
            const int bit = i + 1, v = (int)fl->a.at((size_t)i).i;
            r[21] = (uint8_t)((r[21] & ~(1 << bit)) | (v << bit));
          }
        }
      }
      bool kr = false;
      for (auto& x : jget(T.j, "kings_rock").a) if (x.s == name) kr = true;
      if (kr) r[21] |= 32; else r[21] &= (uint8_t)~32;
    }
    return r;
  }
};

Bytes mesg(const std::vector<std::u32string>& strings) {
  const size_t n = strings.size(), head = 0x10 + 4 * n;
  Bytes body;
  std::vector<size_t> offs;
  for (auto& s : strings) {
    offs.push_back(head + body.size());
    for (char32_t c : s) { body.push_back((uint8_t)(c >> 8)); body.push_back((uint8_t)c); }
    body.push_back(0); body.push_back(0);
  }
  Bytes out(head, 0);
  memcpy(out.data(), "MESG", 4);
  be32(out, 4, (uint32_t)n);
  for (size_t k = 0; k < n; k++) be32(out, 0x10 + 4 * k, (uint32_t)offs[k]);
  append(out, body);
  return out;
}

}

void write_gen4_source(const Gen4SourceOptions& o, const std::function<void(const std::string&)>& log) {
  Usum u(o.rom);
  Gen4 g(u, o.data_dir, o.values_on);
  fs::create_directories(o.src);
  const Bytes blank52(52, 0);
  Bytes pers = Bytes(0x30, 0), evos = Bytes(0x30, 0), learn = Bytes(0x30, 0), tml, moves = Bytes(0x40, 0);
  for (int dex = 0; dex < SPECIES_TOTAL; dex++) {
    const bool in = dex >= 1 && dex <= 493;
    append(pers, dex == 0 || !in ? blank52 : g.personal_record(dex));
    append(evos, in ? g.evolution_record(dex) : Bytes(42, 0));
    if (in) append(learn, g.learnset_record(dex)); else { Bytes b(80, 0); b[3] = 0xD0; append(learn, b); }
  }
  for (int m : g.T.tm) { Bytes b(4, 0); be16(b, 0, (uint32_t)m); append(tml, b); }
  tml.insert(tml.begin(), 0x30, 0);
  append(moves, Bytes(24, 0));
  for (int m = 1; m <= LAST_MOVE; m++) append(moves, g.move_row(m));
  std::vector<pbr::FsMember> members;
  for (int i = 0; i < 33; i++) { pbr::FsMember f; f.name = "blank" + std::to_string(i); members.push_back(f); }
  auto put = [&](int i, const char* name, const Bytes& data) {
    pbr::FsMember f;
    f.word0 = (uint32_t)(i << 16 | 0x1200); f.type = 0; f.name = name; f.data = data; f.size = (uint32_t)data.size();
    members[(size_t)i] = f;
  };
  put(8, "personal", pers);
  put(9, "evolutions", evos);
  put(29, "tm_list", tml);
  put(30, "moves", moves);
  put(32, "learnsets", learn);
  PackFiles pf{o.src, o.eol_crlf};
  pf.write_bytes("common.fsys", pbr::pack_build_fsys(0x4C1, members));
  const size_t n = 2989 + 124;
  std::vector<std::u32string> t(n);
  for (int dex = 1; dex < 501; dex++) t[(size_t)(9 + dex)] = to_u32(upper_str(to_utf8(name_of(g.names.species, dex))));
  for (int m = 1; m <= LAST_MOVE; m++) t[(size_t)(505 + m)] = name_of(g.names.moves, m);
  for (int i = 1; i < 960; i++) if (1990 + i < 2989) t[(size_t)(1990 + i)] = name_of(g.names.items, i);
  for (int a = 1; a < 124; a++) t[(size_t)(2989 + a)] = name_of(g.names.abilities, a);
  std::vector<pbr::FsMember> mes(2);
  mes[0].name = "blank0";
  mes[1].word0 = (uint32_t)(1 << 16 | 0x1200); mes[1].name = "mes_common";
  mes[1].data = mesg(t); mes[1].size = (uint32_t)mes[1].data.size();
  pf.write_bytes("mes_common.fsys", pbr::pack_build_fsys(0x4C2, mes));
  std::string plan;
  for (auto& l : o.plan_lines) plan += l + "\n";
  pf.write("plan.tsv", plan);
  std::string name = "Pokemon Ultra Sun";
  const std::string& prod = u.prod();
  if (prod.compare(0, 9, "CTR-P-A2B") == 0) name = "Pokemon Ultra Moon";
  else if (prod.compare(0, 9, "CTR-P-BND") == 0) name = "Pokemon Sun";
  else if (prod.compare(0, 9, "CTR-P-BNE") == 0) name = "Pokemon Moon";
  pf.write("gen4src.txt", "usum_gen4 source: " + name + ", gen4 values " + (o.values_on ? "on" : "off") + "\n");
  if (log) log("gen4 source: 107 species, 467 moves");
}

}
