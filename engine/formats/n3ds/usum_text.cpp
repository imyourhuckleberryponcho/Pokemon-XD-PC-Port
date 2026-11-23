#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>

#include "n3ds_data.h"

namespace n3ds {

namespace {
struct UpRow { uint32_t c; uint32_t u[3]; };
const UpRow kUpper[] = {
#include "unicode_upper.inc"
};
bool is_space(char32_t c) {
  return (c >= 9 && c <= 13) || (c >= 0x1c && c <= 0x20) || c == 0x85 || c == 0xA0 || c == 0x1680 || (c >= 0x2000 && c <= 0x200A) || c == 0x2028 ||
         c == 0x2029 || c == 0x202F || c == 0x205F || c == 0x3000;
}
}

std::u32string to_u32(const std::string& s) {
  std::u32string o;
  size_t i = 0;
  while (i < s.size()) {
    unsigned char c = (unsigned char)s[i];
    if (c < 0x80) { o.push_back(c); i++; }
    else if ((c >> 5) == 6 && i + 1 < s.size()) { o.push_back(((c & 0x1F) << 6) | (s[i + 1] & 0x3F)); i += 2; }
    else if ((c >> 4) == 14 && i + 2 < s.size()) { o.push_back(((c & 0x0F) << 12) | ((s[i + 1] & 0x3F) << 6) | (s[i + 2] & 0x3F)); i += 3; }
    else if ((c >> 3) == 30 && i + 3 < s.size()) {
      o.push_back(((c & 0x07) << 18) | ((s[i + 1] & 0x3F) << 12) | ((s[i + 2] & 0x3F) << 6) | (s[i + 3] & 0x3F));
      i += 4;
    } else { o.push_back(c); i++; }
  }
  return o;
}
std::string to_utf8(const std::u32string& s) {
  std::string o;
  for (char32_t cp : s) {
    if (cp < 0x80) o += (char)cp;
    else if (cp < 0x800) { o += (char)(0xC0 | (cp >> 6)); o += (char)(0x80 | (cp & 0x3F)); }
    else if (cp < 0x10000) { o += (char)(0xE0 | (cp >> 12)); o += (char)(0x80 | ((cp >> 6) & 0x3F)); o += (char)(0x80 | (cp & 0x3F)); }
    else { o += (char)(0xF0 | (cp >> 18)); o += (char)(0x80 | ((cp >> 12) & 0x3F)); o += (char)(0x80 | ((cp >> 6) & 0x3F)); o += (char)(0x80 | (cp & 0x3F)); }
  }
  return o;
}
std::string upper_str(const std::string& s) {
  std::u32string in = to_u32(s), out;
  for (char32_t c : in) {
    if (c < 0x80) { out.push_back(c >= 'a' && c <= 'z' ? c - 32 : c); continue; }
    const UpRow* e = std::lower_bound(std::begin(kUpper), std::end(kUpper), (uint32_t)c, [](const UpRow& r, uint32_t v) { return r.c < v; });
    if (e != std::end(kUpper) && e->c == c) { for (uint32_t u : e->u) if (u) out.push_back(u); }
    else out.push_back(c);
  }
  return to_utf8(out);
}
std::vector<std::string> py_split(const std::string& s) {
  std::u32string in = to_u32(s), cur;
  std::vector<std::string> out;
  for (char32_t c : in) {
    if (is_space(c)) { if (!cur.empty()) { out.push_back(to_utf8(cur)); cur.clear(); } }
    else cur.push_back(c);
  }
  if (!cur.empty()) out.push_back(to_utf8(cur));
  return out;
}
std::string py_strip(const std::string& s) {
  std::u32string in = to_u32(s);
  size_t a = 0, b = in.size();
  while (a < b && is_space(in[a])) a++;
  while (b > a && is_space(in[b - 1])) b--;
  return to_utf8(in.substr(a, b - a));
}

static std::string replace_all(std::string s, const std::string& a, const std::string& b) {
  size_t p = 0;
  while ((p = s.find(a, p)) != std::string::npos) { s.replace(p, a.size(), b); p += b.size(); }
  return s;
}
static std::string join_words(const std::string& s) {
  std::string o;
  for (auto& w : py_split(s)) { if (!o.empty()) o += ' '; o += w; }
  return o;
}
static std::string upper_if_latin(const std::string& s) {
  std::u32string f = to_u32(s.substr(0, std::min<size_t>(4, s.size())));
  if (!s.empty() && !f.empty() && f[0] < 0x2E80) return upper_str(join_words(s));
  return s;
}
static std::string tick(std::string s) { return replace_all(s, "\xE2\x80\x99", "'"); }

std::string shown(const std::string& s0) { return upper_if_latin(tick(replace_all(s0, "{var}", ""))); }
std::string desc_text(const std::string& s0) { return replace_all(tick(replace_all(s0, "{var}", "")), "\n", "{00}"); }
std::string move_upper(const std::string& s0) { return upper_if_latin(replace_all(tick(replace_all(s0, "{var}", "")), "\xEF\xBF\xBD", "'")); }
std::string move_desc(const std::string& s0) {
  return replace_all(replace_all(tick(replace_all(s0, "{var}", "")), "\xEF\xBF\xBD", "'"), "\n", "{00}");
}

std::vector<int> MoveRec::signature() const {
  std::vector<int> v = {quality, category == 0, hits, inflict, turn_min, turn_max, crit > 0, flinch > 0, effect_seq, recoil, heal, target, priority};
  for (int k = 0; k < 3; k++) for (int j = 0; j < 3; j++) v.push_back(stats[k][j]);
  return v;
}

static int u16(const Bytes& b, size_t o) { return b.at(o) | b.at(o + 1) << 8; }
static uint32_t u32(const Bytes& b, size_t o) { return (uint32_t)u16(b, o) | (uint32_t)u16(b, o + 2) << 16; }
static int s8(int v) { return v > 127 ? v - 256 : v; }

Usum::Usum(const std::string& rom_path) : rom_(open_dump(rom_path)) { det_ = detect(rom_); }

const Garc& Usum::garc(const std::string& path) {
  auto it = garcs_.find(path);
  if (it == garcs_.end()) it = garcs_.emplace(path, std::make_unique<Garc>(garc_at(rom_, path))).first;
  return *it->second;
}
Bytes Usum::read_entry(const std::string& path, size_t i) { return lz11_maybe(garc(path).read(i)); }

static const char* kLangIndex[] = {"ja", "jak", "en", "fr", "it", "de", "es", "ko"};

const std::vector<std::string>& Usum::lines(const std::string& lang, int entry) {
  auto key = std::make_pair(lang, entry);
  auto it = text_.find(key);
  if (it != text_.end()) return it->second;
  int li = -1;
  for (int i = 0; i < 8; i++) if (lang == kLangIndex[i]) li = i;
  if (li < 0) throw Fail("unknown text language " + lang);
  Bytes d = read_entry("a/0/3/" + std::to_string(li), (size_t)entry);
  const uint32_t nlines = (uint32_t)u16(d, 2), off = u32(d, 12);
  std::vector<std::string> out;
  for (uint32_t i = 0; i < nlines; i++) {
    const int32_t o = (int32_t)u32(d, off + 4 + i * 8);
    const uint32_t ln = (uint32_t)u16(d, off + 4 + i * 8 + 4);
    uint32_t k16 = (0x7C89 + 0x2983 * i) & 0xffff;
    std::vector<uint32_t> ch;
    for (uint32_t k = 0; k < ln; k++) {
      const size_t at = (size_t)off + (size_t)o + k * 2;
      uint32_t c = (uint32_t)u16(d, at) ^ k16;
      k16 = ((k16 << 3) | (k16 >> 13)) & 0xffff;
      ch.push_back(c);
    }
    std::u32string s;
    size_t i2 = 0;
    while (i2 < ch.size()) {
      const uint32_t c = ch[i2];
      if (c == 0) break;
      if (c == 0x10 && i2 + 3 < ch.size()) {
        for (char x : std::string("{var}")) s.push_back((char32_t)x);
        i2 += 4 + ch[i2 + 3];
        continue;
      }
      s.push_back(c);
      i2++;
    }
    out.push_back(to_utf8(s));
  }
  return text_.emplace(key, std::move(out)).first->second;
}

Personal Usum::personal(int record) {
  Bytes b = read_entry(det_.personal, (size_t)record);
  Personal p;
  p.hp = b.at(0); p.atk = b.at(1); p.defn = b.at(2); p.spe = b.at(3); p.spa = b.at(4); p.spd = b.at(5);
  p.type1 = b[6]; p.type2 = b[7]; p.catch_rate = b[8];
  const int ev = u16(b, 0x0A);
  p.ev_hp = ev & 3; p.ev_atk = (ev >> 2) & 3; p.ev_def = (ev >> 4) & 3; p.ev_spe = (ev >> 6) & 3; p.ev_spa = (ev >> 8) & 3; p.ev_spd = (ev >> 10) & 3;
  p.gender = b[0x12]; p.hatch = b[0x13]; p.friendship = b[0x14]; p.growth = b[0x15];
  p.abilities[0] = b[0x18]; p.abilities[1] = b[0x19]; p.abilities[2] = b[0x1A];
  p.form_stats_index = u16(b, 0x1C); p.form_count = b[0x20]; p.base_exp = u16(b, 0x22); p.height_cm = u16(b, 0x24); p.weight_hg = u16(b, 0x26);
  return p;
}

std::vector<std::pair<int, int>> Usum::levelup(int record) {
  Bytes d = read_entry(det_.levelup, (size_t)record);
  std::vector<std::pair<int, int>> out;
  for (size_t i = 0; i + 3 < d.size(); i += 4) {
    const int mv = (int16_t)u16(d, i), lv = (int16_t)u16(d, i + 2);
    if (mv == -1) break;
    out.push_back({lv, mv});
  }
  return out;
}

std::vector<EvoRec> Usum::evolutions(int record) {
  Bytes d = read_entry("a/0/1/4", (size_t)record);
  std::vector<EvoRec> out;
  for (size_t k = 0; k + 8 <= d.size(); k += 8) {
    EvoRec e;
    e.method = u16(d, k); e.arg = u16(d, k + 2); e.species = u16(d, k + 4); e.form = d[k + 6]; e.level = d[k + 7];
    if (e.method) out.push_back(e);
  }
  return out;
}

const std::vector<MoveRec>& Usum::moves() {
  if (moves_loaded_) return moves_;
  Bytes d = read_entry("a/0/1/1", 0);
  const size_t n = (size_t)u16(d, 2);
  std::vector<uint32_t> offs;
  for (size_t i = 0; i <= n; i++) offs.push_back(u32(d, 4 + 4 * i));
  for (size_t i = 0; i < n; i++) {
    Bytes b(d.begin() + offs[i], d.begin() + offs[i + 1]);
    MoveRec m;
    m.id = (int)i;
    m.type = b.at(0); m.quality = b.at(1); m.category = b.at(2); m.power = b.at(3); m.accuracy = b.at(4); m.pp = b.at(5);
    m.priority = s8(b.at(6)); m.hits = b.at(7); m.inflict = u16(b, 8); m.inflict_percent = b.at(0x0A);
    m.turn_min = b.at(0x0C); m.turn_max = b.at(0x0D); m.crit = b.at(0x0E); m.flinch = b.at(0x0F); m.effect_seq = u16(b, 0x10);
    m.recoil = s8(b.at(0x12)); m.heal = s8(b.at(0x13)); m.target = b.at(0x14);
    for (int k = 0; k < 3; k++) { m.stats[k][0] = b.at(0x15 + k); m.stats[k][1] = s8(b.at(0x18 + k)); m.stats[k][2] = b.at(0x1B + k); }
    m.flags = b.size() >= 0x26 ? u16(b, 0x24) : 0;
    moves_.push_back(m);
  }
  moves_loaded_ = true;
  return moves_;
}

std::vector<std::u32string> decode_text_units(const Bytes& d) {
  const uint32_t nlines = (uint32_t)u16(d, 2), off = u32(d, 12);
  std::vector<std::u32string> out;
  for (uint32_t i = 0; i < nlines; i++) {
    const int32_t o = (int32_t)u32(d, off + 4 + i * 8);
    const uint32_t ln = (uint32_t)u16(d, off + 4 + i * 8 + 4);
    uint32_t key = (0x7C89 + 0x2983 * i) & 0xffff;
    std::u32string s;
    for (uint32_t k = 0; k < ln; k++) {
      const uint32_t c = (uint32_t)u16(d, (size_t)off + (size_t)o + k * 2) ^ key;
      key = ((key << 3) | (key >> 13)) & 0xffff;
      if (c) s.push_back(c);
    }
    out.push_back(s);
  }
  return out;
}
namespace {
std::string dex_clean(const std::u32string& text) {
  std::u32string t;
  for (size_t i = 0; i < text.size(); i++) {
    if (text[i] == U'\\' && i + 1 < text.size() && text[i + 1] == U'n') { t.push_back(U' '); i++; } else t.push_back(text[i]);
  }
  std::u32string out;
  for (char32_t c : t) {
    if (c == U'\n' || c == U'\r') { out.push_back(U' '); continue; }
    if (c < 0x20 || (c >= 0xD800 && c < 0xF900)) continue;
    switch (c) {
      case 0x2018: case 0x2019: out.push_back(U'\''); break;
      case 0x201C: case 0x201D: out.push_back(U'"'); break;
      case 0x2026: out += U"..."; break;
      case 0x2013: case 0x2014: out.push_back(U'-'); break;
      case 0x00A0: case 0x3000: out.push_back(U' '); break;
      case 0x2640: case 0x2642: break;
      default: out.push_back(c);
    }
  }
  std::u32string o2;
  for (char32_t c : out) { if (c == U' ' && !o2.empty() && o2.back() == U' ') continue; o2.push_back(c); }
  return py_strip(to_utf8(o2));
}
}

int make_dex_entries(const std::string& rom, const std::string& out_dir, const std::function<void(const std::string&)>& log) {
  Usum u(rom);
  static const std::pair<const char*, int> langs[] = {{"ja", 0}, {"en", 2}, {"fr", 3}, {"it", 4}, {"de", 5}, {"es", 6}};
  static const std::pair<const char*, int> versions[] = {{"sun", 124}, {"moon", 125}};
  int made = 0;
  for (auto& lg : langs) {
    for (auto& vs : versions) {
      std::vector<std::u32string> lines;
      try {
        lines = decode_text_units(u.read_entry(std::string("a/0/3/") + std::to_string(lg.second), (size_t)vs.second));
      } catch (const std::exception& e) {
        if (log) log(std::string(lg.first) + " " + vs.first + ": not found (" + e.what() + ")");
        continue;
      }
      if (lines.size() < 807 || lines[25].size() < 8) {
        if (log) log(std::string(lg.first) + " " + vs.first + ": entry " + std::to_string(vs.second) + " is not the Pokedex text");
        continue;
      }
      std::string text;
      int n = 0;
      for (int no = 1; no <= 807; no++) {
        const std::string t = dex_clean(lines[(size_t)no]);
        if (!t.empty()) { text += std::to_string(no) + "\t" + t + "\n"; n++; }
      }
      std::filesystem::create_directories(out_dir);
      std::ofstream f(out_dir + "/entries_" + lg.first + "_" + vs.first + ".txt", std::ios::binary);
      f.write(text.data(), (std::streamsize)text.size());
      if (log) log(std::string(lg.first) + " " + vs.first + ": " + std::to_string(n) + " texts");
      made++;
    }
  }
  return made;
}

}
