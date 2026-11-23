#include "n3ds_plan.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <sstream>
#include <thread>

#include "n3ds_gf.h"

namespace n3ds {

namespace {
struct JP {
  const std::string& s;
  size_t p = 0;
  explicit JP(const std::string& t) : s(t) {}
  [[noreturn]] void bad(const char* m) { throw Fail(std::string("a data file is not valid JSON (") + m + " at " + std::to_string(p) + ")"); }
  void ws() { while (p < s.size() && (s[p] == ' ' || s[p] == '\n' || s[p] == '\r' || s[p] == '\t')) p++; }
  std::string str() {
    std::string o;
    p++;
    while (p < s.size() && s[p] != '"') {
      char c = s[p++];
      if (c == '\\' && p < s.size()) {
        char e = s[p++];
        if (e == 'n') o += '\n';
        else if (e == 't') o += '\t';
        else if (e == 'r') o += '\r';
        else if (e == 'u' && p + 4 <= s.size()) {
          unsigned v = (unsigned)strtoul(s.substr(p, 4).c_str(), nullptr, 16);
          p += 4;
          if (v < 0x80) o += (char)v;
          else if (v < 0x800) { o += (char)(0xC0 | (v >> 6)); o += (char)(0x80 | (v & 0x3F)); }
          else { o += (char)(0xE0 | (v >> 12)); o += (char)(0x80 | ((v >> 6) & 0x3F)); o += (char)(0x80 | (v & 0x3F)); }
        } else o += e;
      } else o += c;
    }
    if (p >= s.size()) bad("string");
    p++;
    return o;
  }
  JV val() {
    ws();
    if (p >= s.size()) bad("end");
    JV v;
    char c = s[p];
    if (c == '{') {
      v.type = JV::OBJ;
      p++;
      ws();
      if (s[p] == '}') { p++; return v; }
      while (true) {
        ws();
        if (s[p] != '"') bad("key");
        std::string k = str();
        ws();
        if (s[p] != ':') bad(":");
        p++;
        v.obj.push_back({k, val()});
        ws();
        if (s[p] == ',') { p++; continue; }
        if (s[p] == '}') { p++; break; }
        bad("object");
      }
    } else if (c == '[') {
      v.type = JV::ARR;
      p++;
      ws();
      if (s[p] == ']') { p++; return v; }
      while (true) {
        v.arr.push_back(val());
        ws();
        if (s[p] == ',') { p++; continue; }
        if (s[p] == ']') { p++; break; }
        bad("array");
      }
    } else if (c == '"') {
      v.type = JV::STR;
      v.str = str();
    } else if (s.compare(p, 4, "true") == 0) { v.type = JV::BOOL; v.b = true; p += 4; }
    else if (s.compare(p, 5, "false") == 0) { v.type = JV::BOOL; p += 5; }
    else if (s.compare(p, 4, "null") == 0) { p += 4; }
    else {
      char* end = nullptr;
      v.num = strtod(s.c_str() + p, &end);
      if (end == s.c_str() + p) bad("value");
      v.type = JV::NUM;
      p = (size_t)(end - s.c_str());
    }
    return v;
  }
};
}

const JV* JV::get(const std::string& k) const {
  for (auto& kv : obj) if (kv.first == k) return &kv.second;
  return nullptr;
}
const JV& JV::at(const std::string& k) const {
  const JV* v = get(k);
  if (!v) throw Fail("a data file lacks \"" + k + "\"");
  return *v;
}
JV json_parse(const std::string& text) {
  JP p(text);
  return p.val();
}
JV json_read_file(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) throw Fail("cannot read the data file " + path);
  std::string t((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  if (t.size() >= 3 && (uint8_t)t[0] == 0xEF) t = t.substr(3);
  return json_parse(t);
}

namespace {
struct Sha1 {
  uint32_t h[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
  uint8_t buf[64];
  size_t nb = 0;
  uint64_t total = 0;
  static uint32_t rol(uint32_t x, int n) { return (x << n) | (x >> (32 - n)); }
  void block(const uint8_t* b) {
    uint32_t w[80];
    for (int i = 0; i < 16; i++) w[i] = (uint32_t)b[4 * i] << 24 | (uint32_t)b[4 * i + 1] << 16 | (uint32_t)b[4 * i + 2] << 8 | b[4 * i + 3];
    for (int i = 16; i < 80; i++) w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    uint32_t a = h[0], bb = h[1], c = h[2], d = h[3], e = h[4];
    for (int i = 0; i < 80; i++) {
      uint32_t f, k;
      if (i < 20) { f = (bb & c) | (~bb & d); k = 0x5A827999; }
      else if (i < 40) { f = bb ^ c ^ d; k = 0x6ED9EBA1; }
      else if (i < 60) { f = (bb & c) | (bb & d) | (c & d); k = 0x8F1BBCDC; }
      else { f = bb ^ c ^ d; k = 0xCA62C1D6; }
      uint32_t t = rol(a, 5) + f + e + k + w[i];
      e = d; d = c; c = rol(bb, 30); bb = a; a = t;
    }
    h[0] += a; h[1] += bb; h[2] += c; h[3] += d; h[4] += e;
  }
  void update(const uint8_t* d, size_t n) {
    total += n;
    while (n) {
      size_t k = std::min(n, 64 - nb);
      memcpy(buf + nb, d, k);
      nb += k; d += k; n -= k;
      if (nb == 64) { block(buf); nb = 0; }
    }
  }
  std::string hex() {
    uint64_t bits = total * 8;
    uint8_t pad = 0x80;
    update(&pad, 1);
    uint8_t z = 0;
    while (nb != 56) update(&z, 1);
    uint8_t len[8];
    for (int i = 0; i < 8; i++) len[i] = (uint8_t)(bits >> (56 - 8 * i));
    update(len, 8);
    char out[41];
    for (int i = 0; i < 5; i++) snprintf(out + 8 * i, 9, "%08x", h[i]);
    return out;
  }
};
}

std::string sha1_hex(const uint8_t* d, size_t n) {
  Sha1 h;
  h.update(d, n);
  return h.hex();
}

std::string dump_fingerprint(const std::string& path) {
  ByteFile f(path);
  uint64_t size = f.size();
  Sha1 h;
  std::string sz = std::to_string(size);
  h.update((const uint8_t*)sz.data(), sz.size());
  auto rd = [&](uint64_t at) {
    uint64_t n = std::min<uint64_t>(1 << 20, size > at ? size - at : 0);
    Bytes b = f.read(at, n);
    h.update(b.data(), b.size());
  };
  rd(0);
  rd(size > (1u << 20) ? size - (1u << 20) : 0);
  return sz + "-" + h.hex().substr(0, 16);
}

namespace {
uint32_t be32(const uint8_t* p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
uint32_t be32(const Bytes& b, size_t o) {
  if (o + 4 > b.size()) throw Fail("a file of the XD image is too short");
  return be32(&b[o]);
}
uint16_t be16(const Bytes& b, size_t o) {
  if (o + 2 > b.size()) throw Fail("a file of the XD image is too short");
  return (uint16_t)(b[o] << 8 | b[o + 1]);
}

class IsoDisc : public pbr::DiscSource {
 public:
  explicit IsoDisc(const std::string& path) : path_(path) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) throw pbr::Fail(path + " cannot be read as a disc image.", 2);
    uint8_t h[0x440];
    if (fread(h, 1, sizeof h, f) != sizeof h) { fclose(f); throw pbr::Fail(path + " is not a GameCube disc image.", 2); }
    if (be32(h + 0x1C) != 0xC2339F3D) { fclose(f); throw pbr::Fail(path + " is not a GameCube disc image (an ISO / GCM file).", 2); }
    id_.assign((const char*)h, 6);
    dol_off_ = be32(h + 0x420);
    uint32_t fst_off = be32(h + 0x424), fst_size = be32(h + 0x428);
    Bytes fst(fst_size);
    fseek(f, fst_off, SEEK_SET);
    if (fread(fst.data(), 1, fst_size, f) != fst_size) { fclose(f); throw pbr::Fail("the disc image's file table is cut short.", 2); }
    fclose(f);
    if (fst.size() < 12) throw pbr::Fail("the disc image's file table is empty.", 2);
    uint32_t n = be32(&fst[8]);
    if ((size_t)n * 12 > fst.size()) throw pbr::Fail("the disc image's file table is damaged.", 2);
    size_t names = (size_t)n * 12;
    std::vector<std::pair<std::string, uint32_t>> dirs;
    std::string cur;
    std::vector<std::pair<uint32_t, std::string>> stack;
    for (uint32_t i = 1; i < n; i++) {
      while (!stack.empty() && i >= stack.back().first) stack.pop_back();
      std::string base = stack.empty() ? "" : stack.back().second;
      uint32_t w0 = be32(&fst[i * 12]), w1 = be32(&fst[i * 12 + 4]), w2 = be32(&fst[i * 12 + 8]);
      std::string name((const char*)&fst[names + (w0 & 0xFFFFFF)]);
      std::string full = base.empty() ? name : base + "/" + name;
      if (w0 >> 24) stack.push_back({w2, full});
      else { files_.push_back({full, w2}); where_[full] = {w1, w2}; }
    }
  }
  std::string game_id() const override { return id_; }
  std::vector<std::pair<std::string, uint32_t>> files() const override { return files_; }
  std::vector<uint8_t> read(const std::string& p) const override {
    auto it = where_.find(p);
    if (it == where_.end()) {
      for (auto& kv : where_) {
        if (kv.first.size() == p.size() && std::equal(kv.first.begin(), kv.first.end(), p.begin(), [](char a, char b) { return tolower((unsigned char)a) == tolower((unsigned char)b); })) { it = where_.find(kv.first); break; }
      }
      if (it == where_.end()) throw std::runtime_error("the XD image has no file " + p);
    }
    return slice(it->second.first, it->second.second);
  }
  std::vector<uint8_t> dol() const {
    Bytes h = slice(dol_off_, 0x100);
    uint32_t end = 0x100;
    for (int i = 0; i < 18; i++) end = std::max(end, be32(&h[i * 4]) + be32(&h[0x90 + i * 4]));
    return slice(dol_off_, end);
  }

 private:
  Bytes slice(uint64_t off, uint64_t n) const {
    FILE* f = fopen(path_.c_str(), "rb");
    if (!f) throw std::runtime_error("cannot open the XD image again");
#ifdef _WIN32
    _fseeki64(f, (long long)off, SEEK_SET);
#else
    fseeko(f, (off_t)off, SEEK_SET);
#endif
    Bytes b((size_t)n);
    size_t got = fread(b.data(), 1, (size_t)n, f);
    fclose(f);
    if (got != n) throw std::runtime_error("the XD image is cut short");
    return b;
  }
  std::string path_, id_;
  uint32_t dol_off_ = 0;
  std::vector<std::pair<std::string, uint32_t>> files_;
  std::map<std::string, std::pair<uint32_t, uint32_t>> where_;
};
}

std::unique_ptr<pbr::DiscSource> open_iso(const std::string& path) { return std::unique_ptr<pbr::DiscSource>(new IsoDisc(path)); }
std::vector<uint8_t> disc_dol(const pbr::DiscSource& d) {
  const IsoDisc* iso = dynamic_cast<const IsoDisc*>(&d);
  if (!iso) throw Fail("this disc source cannot give main.dol");
  return iso->dol();
}

namespace {
constexpr int REC = 292, PKX_ROWS = 419, GEN3_LAST = 386, BONSLY = 438;
constexpr uint32_t PKX_TABLE = 0x8040D0A8;

Bytes dol_read(const Bytes& dol, uint32_t addr, uint32_t n) {
  for (int i = 0; i < 18; i++) {
    uint32_t off = be32(dol, i * 4), a = be32(dol, 0x48 + i * 4), sz = be32(dol, 0x90 + i * 4);
    if (sz && a <= addr && addr + n <= a + sz) return Bytes(dol.begin() + off + (addr - a), dol.begin() + off + (addr - a) + n);
  }
  throw Fail("main.dol of the XD image does not hold the model table");
}
bool starts_lower(const std::string& p, const char* pre) {
  size_t n = strlen(pre);
  if (p.size() < n) return false;
  for (size_t i = 0; i < n; i++) if (tolower((unsigned char)p[i]) != pre[i]) return false;
  return true;
}
}

XdSide::XdSide(const pbr::DiscSource& disc) : disc_(disc) {
  Bytes d = disc.read("common.fsys");
  Bytes rel;
  bool found = false;
  for (auto& e : pbr::fsys_entries(d))
    if (e.name == "common_rel") { rel = pbr::fsys_extract(d, e); found = true; break; }
  if (!found) throw Fail("this does not look like the XD disc (common_rel is missing)", 2);
  const uint8_t sig[12] = {0, 35, 0, 55, 0, 30, 0, 50, 0, 40, 0, 90};
  auto it = std::search(rel.begin(), rel.end(), sig, sig + 12);
  if (it == rel.end()) throw Fail("species table not found in common_rel", 2);
  long long i = it - rel.begin();
  long long base = i - 0x8E - 25 * REC;
  if (base < 0) throw Fail("species table not found in common_rel", 2);
  Bytes dol = disc_dol(disc);
  Bytes table = dol_read(dol, PKX_TABLE, PKX_ROWS * 8);
  std::map<uint32_t, XdModel> by_group;
  auto fl = disc.files();
  std::vector<std::string> names;
  for (auto& f : fl) names.push_back(f.first);
  std::sort(names.begin(), names.end());
  for (auto& p : names) {
    if (!(starts_lower(p, "pkx_") && p.size() >= 5)) continue;
    std::string low = p;
    for (auto& c : low) c = (char)tolower((unsigned char)c);
    if (low.size() < 5 || low.compare(low.size() - 5, 5, ".fsys") != 0) continue;
    Bytes data = disc.read(p);
    if (data.size() < 4 || memcmp(data.data(), "FSYS", 4) != 0) continue;
    for (auto& e : pbr::fsys_entries(data))
      if (e.type == 15) { by_group[be32(data, 8)] = {p, e.index, e.name}; break; }
  }
  for (int s = 1; s < 415; s++) {
    size_t o = (size_t)(base + (long long)s * REC);
    if (o + REC > rel.size()) break;
    Bytes r(rel.begin() + o, rel.begin() + o + REC);
    int nat = be16(r, 0x12);
    uint32_t row = be32(r, 0x2C);
    if (!((1 <= nat && nat <= GEN3_LAST) || nat == BONSLY) || row >= (uint32_t)PKX_ROWS) continue;
    uint32_t grp = be32(table, row * 8);
    auto g = by_group.find(grp);
    if (g != by_group.end() && !species.count(nat)) species[nat] = g->second;
  }
}

std::shared_ptr<const Bytes> XdSide::pkx_of(int nat) const {
  auto it = species.find(nat);
  if (it == species.end()) throw Fail("XD has no model of species " + std::to_string(nat));
  std::lock_guard<std::mutex> g(mu_);
  Bytes data = disc_.read(it->second.arch);
  for (auto& e : pbr::fsys_entries(data))
    if (e.index == it->second.idx) return std::make_shared<const Bytes>(pbr::fsys_extract(data, e));
  throw Fail("XD's archive " + it->second.arch + " lost its model");
}

PersonalRec read_personal(const Garc& personal, int record) {
  Bytes b = lz11_maybe(personal.read((size_t)record));
  if (b.size() < 0x26) throw Fail("a personal record is too short");
  PersonalRec r;
  r.type1 = b[6];
  r.type2 = b[7];
  r.form_stats_index = b[0x1C] | b[0x1D] << 8;
  r.height_cm = b[0x24] | b[0x25] << 8;
  return r;
}

std::optional<std::string> model_texture_name(const Garc& g, size_t k) {
  Bytes raw = g.read(1 + 9 * k);
  if (!raw.empty() && raw[0] == 0x11 && raw.size() >= 8) {
    uint32_t size = raw[1] | raw[2] << 8 | raw[3] << 16;
    if (size == 0) size = raw[4] | raw[5] << 8 | raw[6] << 16 | (uint32_t)raw[7] << 24;
    if (size < 1000) return std::nullopt;
    for (uint64_t lim : {4096, 32768}) {
      if (lim >= size) break;
      try {
        return model_first_texture(lz11(raw.data(), raw.size(), lim));
      } catch (const std::exception&) {
      }
    }
  }
  Bytes bb = lz11_maybe(raw);
  if (bb.size() < 1000) return std::nullopt;
  return model_first_texture(bb);
}

namespace {
std::vector<std::optional<std::string>> all_names(const Garc& g, int threads) {
  size_t n = (g.count() - 1) / 9;
  std::vector<std::optional<std::string>> out(n);
  std::atomic<size_t> next{0};
  auto work = [&]() {
    while (true) {
      size_t k = next.fetch_add(1);
      if (k >= n) return;
      try { out[k] = model_texture_name(g, k); } catch (const std::exception&) { out[k] = std::nullopt; }
    }
  };
  std::vector<std::thread> th;
  for (int i = 0; i < std::max(1, threads); i++) th.emplace_back(work);
  for (auto& t : th) t.join();
  return out;
}
bool all_digits(const std::string& s) {
  if (s.empty()) return false;
  for (char c : s) if (!isdigit((unsigned char)c)) return false;
  return true;
}
}

std::map<int, std::pair<int, std::string>> scan_groups(const Garc& g, int threads) {
  auto names = all_names(g, threads);
  std::map<int, std::pair<int, std::string>> out;
  for (size_t k = 0; k < names.size(); k++) {
    const auto& tex = names[k];
    if (tex && tex->compare(0, 2, "pm") == 0 && tex->size() >= 6 && all_digits(tex->substr(2, 4)))
      out[(int)k] = {atoi(tex->substr(2, 4).c_str()), (tex->size() >= 9 && (*tex)[6] == '_') ? tex->substr(7, 2) : std::string("00")};
  }
  return out;
}

namespace {
const int TYPE_XD_TABLE[18] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 10, 11, 12, 13, 14, 15, 16, 17, -1};
int type_xd(int t) { return t >= 0 && t < 18 ? TYPE_XD_TABLE[t] : -1; }
const std::set<int> FAIRY_OLD = {35, 36, 39, 40, 173, 174, 175, 176, 183, 184, 209, 210, 122, 468};

std::map<int, std::vector<std::pair<int, int>>> standin_pool(const Garc& pers, const XdSide& xd) {
  std::map<int, std::vector<std::pair<int, int>>> pool;
  for (int n = 1; n < 252; n++) {
    if (!xd.species.count(n) || FAIRY_OLD.count(n)) continue;
    PersonalRec p = read_personal(pers, n);
    int t = type_xd(p.type1);
    if (t < 0) continue;
    pool[t].push_back({p.height_cm, n});
  }
  return pool;
}
int pick_from(const std::map<int, std::vector<std::pair<int, int>>>& pool, const PersonalRec& p) {
  int t = type_xd(p.type1);
  if (t < 0) t = type_xd(p.type2);
  if (t < 0) t = 0;
  auto it = pool.find(t);
  const auto* cands = (it != pool.end() && !it->second.empty()) ? &it->second : &pool.at(0);
  std::pair<int, int> best = {1 << 30, 0};
  bool have = false;
  for (auto& c : *cands) {
    std::pair<int, int> key = {std::abs(c.first - p.height_cm), c.second};
    if (!have || key < best) { best = key; have = true; }
  }
  return best.second;
}
}

Plan make_plan(const RomFs& rom, const Detected& det, const Garc& models, const pbr::DiscSource& xd_disc, const PlanOptions& o) {
  (void)rom;
  auto say = [&](const std::string& s) { if (o.log) o.log(s); };
  auto path = [&](const char* f) { return o.data_dir + "/" + f; };
  Plan plan;
  XdSide xd(xd_disc);
  plan.species_in_xd = xd.species.size();
  Garc pers = garc_at(rom, det.personal);
  size_t ngroups = (models.count() - 1) / 9;
  auto names = all_names(models, 8);
  std::map<int, std::pair<int, std::string>> groups;
  for (size_t k = 0; k < names.size(); k++) {
    const auto& tex = names[k];
    if (tex && tex->compare(0, 2, "pm") == 0 && tex->size() >= 6 && all_digits(tex->substr(2, 4)))
      groups[(int)k] = {atoi(tex->substr(2, 4).c_str()), (tex->size() >= 9 && (*tex)[6] == '_') ? tex->substr(7, 2) : std::string("00")};
  }
  plan.groups_scanned = groups.size();
  JV dexj = json_read_file(path("usum_dex_map.json"));
  std::map<int, int> dexmap;
  for (auto& kv : dexj.at("kalos_alola").obj) dexmap[atoi(kv.first.c_str())] = kv.second.as_int();
  std::map<int, int> table;
  for (auto& kv : groups) {
    int pm = kv.second.first;
    const std::string& form = kv.second.second;
    int nat = (pm >= 1 && pm <= 649) ? pm : (dexmap.count(pm) ? dexmap[pm] : 0);
    if (nat && (!table.count(nat) || (form == "00" && groups[table[nat]].second != "00"))) table[nat] = kv.first;
  }
  plan.direct = table.size();
  for (auto& kv : table) plan.lookup[kv.first] = kv.second;
  say("lookup: " + std::to_string(xd.species.size()) + " XD species with a model, " + std::to_string(groups.size()) + " model groups scanned, " + std::to_string(table.size()) +
      " species with a direct model");
  auto want = [&](int nat) { return o.only.empty() || o.only.count(nat); };
  if (o.packs.count("usum")) {
    for (auto& kv : xd.species) {
      int nat = kv.first;
      if (!want(nat)) continue;
      if (!table.count(nat)) { plan.no_model_in_dump.push_back(nat); continue; }
      PlanItem it;
      char nm[32];
      snprintf(nm, sizeof nm, "xd_%03d", nat);
      it.pack = "usum"; it.name = nm; it.nat = nat; it.group = table[nat]; it.arch = kv.second.arch; it.idx = kv.second.idx; it.model = kv.second.name; it.standin_nat = nat;
      plan.items.push_back(std::move(it));
    }
  }
  if (o.packs.count("gen4")) {
    JV g4 = json_read_file(path("usum_gen4_plan.json"));
    std::vector<std::pair<int, const JV*>> rows;
    for (auto& kv : g4.at("plan").obj) rows.push_back({atoi(kv.first.c_str()), &kv.second});
    std::sort(rows.begin(), rows.end(), [](auto& a, auto& b) { return a.first < b.first; });
    for (auto& r : rows) {
      int dex = r.first, standin = r.second->arr.at(1).as_int();
      if (!want(dex) || !table.count(dex) || !xd.species.count(standin)) continue;
      PlanItem it;
      char nm[32];
      snprintf(nm, sizeof nm, "g4_%03d", dex);
      it.pack = "gen4"; it.name = nm; it.nat = dex; it.group = table[dex]; it.model = r.second->arr.at(0).str; it.standin_nat = standin;
      it.height = read_personal(pers, dex).height_cm;
      plan.items.push_back(std::move(it));
    }
  }
  JV mnj = json_read_file(path("gen57_model_numbers.json"));
  const JV& mn = mnj.at("model_number");
  const JV* ovr = mnj.get("group_override");
  JV spj;
  std::string fp = dump_fingerprint(o.rom);
  const JV* speed = nullptr;
  {
    const char* off = getenv("GEN57_NO_SPEED_TABLE");
    if (!(off && std::string(off) == "1")) {
      try {
        spj = json_read_file(path("gen57_speed_numbers.json"));
        const JV* ver = spj.get("version");
        if (ver && ver->as_int() == 1) { const JV* dd = spj.at("dumps").get(fp); if (dd) speed = dd; }
      } catch (const Fail&) {
      }
    }
  }
  auto ladder_of = [&](const char* kind, const std::string& key) {
    if (!speed) return 0;
    const JV* l = speed->get(kind);
    const JV* v = l ? l->get(key) : nullptr;
    return v ? v->as_int() : 0;
  };
  auto model_number = [&](int nat) -> int {
    if (nat < 650) return nat;
    const JV* v = mn.get(std::to_string(nat));
    if (!v) throw Fail("no model number for species " + std::to_string(nat));
    return v->str.empty() ? v->as_int() : atoi(v->str.c_str());
  };
  auto find_group = [&](int pm, const std::string& form) -> int {
    char want_s[16];
    snprintf(want_s, sizeof want_s, "pm%04d_%s", pm, form.c_str());
    int n = (int)ngroups, k0 = std::min(pm, n - 1);
    for (int d = 0; d < n; d++)
      for (int k : {k0 + d, k0 - d})
        if (k >= 0 && k < n && names[(size_t)k] && names[(size_t)k]->compare(0, strlen(want_s), want_s) == 0) return k;
    return -1;
  };
  std::map<std::pair<int, std::string>, std::vector<int>> by;
  for (auto& kv : groups) by[{kv.second.first, kv.second.second}].push_back(kv.first);
  auto pool = standin_pool(pers, xd);
  std::map<int, int> standin_g57;
  for (int nat = 494; nat <= 807; nat++) standin_g57[nat] = pick_from(pool, read_personal(pers, nat));
  if (o.packs.count("g57")) {
    for (int nat = 494; nat <= 807; nat++) {
      if (!want(nat)) continue;
      PlanItem it;
      it.pack = "g57"; it.name = "n3_" + std::to_string(nat); it.nat = nat; it.standin_nat = standin_g57[nat];
      it.gid = PKX_GROUP_FIRST_G57 + (uint32_t)(nat - 494); it.res = PKX_RES;
      it.height = read_personal(pers, nat).height_cm;
      it.ladder = ladder_of("ladder", std::to_string(nat));
      try {
        int pm = model_number(nat);
        const JV* g = ovr ? ovr->get(std::to_string(nat)) : nullptr;
        it.group = g ? g->as_int() : find_group(pm, "00");
        if (it.group < 0) it.skip = "model pm" + std::to_string(pm) + "_00 not found";
      } catch (const Fail& e) {
        it.skip = e.what();
      }
      plan.items.push_back(std::move(it));
    }
  }
  if (o.packs.count("form")) {
    JV fj = json_read_file(path("gen57_forms.json"));
    const auto& forms = fj.at("forms").arr;
    for (size_t i = 0; i < forms.size(); i++) {
      const JV& f = forms[i];
      int nat = f.at("nat").as_int(), form = f.at("form").as_int();
      std::string kind = f.at("kind").str;
      if (!want(nat)) continue;
      PlanItem it;
      it.pack = "form"; it.name = "n3f_" + std::to_string(nat) + "_" + std::to_string(form); it.nat = nat;
      it.gid = PKX_GROUP_FIRST_FORM + (uint32_t)i; it.res = PKX_RES;
      PersonalRec base = read_personal(pers, nat);
      int record = base.form_stats_index + form - 1;
      it.height = read_personal(pers, record).height_cm;
      it.ladder = ladder_of("forms", it.name);
      int sh = standin_g57.count(nat) ? standin_g57[nat] : nat;
      if (!xd.species.count(sh) && !xd.species.count(nat)) {
        sh = pick_from(pool, base);
      }
      std::string code;
      const JV* mj = f.get("model");
      if (mj && !mj->str.empty()) code = mj->str;
      else if (kind == "alola") code = "61";
      else if (kind == "mega" || kind == "primal") { char b[8]; snprintf(b, sizeof b, "5%d", form); code = b; }
      else code = "00";
      int pm = 0;
      try { pm = model_number(nat); } catch (const Fail& e) { it.skip = e.what(); }
      auto g = by.find({pm, code});
      if (it.skip.empty() && (g == by.end() || g->second.empty())) it.skip = "nogroup";
      else if (it.skip.empty()) it.group = g->second[0];
      if (xd.species.count(sh)) it.standin_nat = sh;
      else if (xd.species.count(nat)) it.standin_nat = nat;
      else if (it.skip.empty()) it.skip = "noshell";
      plan.items.push_back(std::move(it));
    }
  }
  return plan;
}

}
