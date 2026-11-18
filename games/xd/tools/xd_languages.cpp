#include <gs_formats.h>
#include <port_disc.h>

#include "xd_languages.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

constexpr const char* kVersion = "1.1.1";
constexpr const char* kJpVersion = "1.1.0";

uint32_t Be32(const uint8_t* p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
void PutBe32(std::vector<uint8_t>& v, uint32_t x) {
  v.push_back((uint8_t)(x >> 24));
  v.push_back((uint8_t)(x >> 16));
  v.push_back((uint8_t)(x >> 8));
  v.push_back((uint8_t)x);
}

[[noreturn]] void Fail(int code, const std::string& message) {
  std::printf("error %s\n", message.c_str());
  std::fflush(stdout);
  std::exit(code);
}

std::string Lower(std::string s) {
  for (char& c : s) c = (char)std::tolower((unsigned char)c);
  return s;
}

bool EndsWith(const std::string& s, const char* suffix) {
  const size_t n = std::strlen(suffix);
  return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
}

std::string BaseName(const std::string& name) {
  for (const char* suf : {"_fr.fsys", "_ge.fsys", "_it.fsys", "_sp.fsys"}) {
    if (EndsWith(name, suf)) return name.substr(0, name.size() - 8) + ".fsys";
  }
  return {};
}

struct DiscFiles {
  std::vector<std::string> fsys;
  std::set<std::string> all;
};
void Collect(const char* path, uint32_t, void* user) {
  auto* d = static_cast<DiscFiles*>(user);
  d->all.insert(path);
  if (EndsWith(path, ".fsys")) d->fsys.emplace_back(path);
}

struct UsInfo {
  std::map<std::string, std::map<uint16_t, std::vector<uint8_t>>> rels;
  std::map<std::string, int> messages;
  std::vector<uint8_t> toc;
  std::set<std::string> files;
  std::map<std::string, uint64_t> suffixHash;
};

uint64_t HashBytes(const uint8_t* data, size_t size) {
  uint64_t h = 1469598103934665603ull;
  for (size_t i = 0; i < size; i++) h = (h ^ data[i]) * 1099511628211ull;
  return h ^ size;
}

bool WriteFile(const fs::path& path, const void* data, size_t size) {
  std::error_code ec;
  fs::create_directories(path.parent_path(), ec);
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) return false;
  out.write(static_cast<const char*>(data), (std::streamsize)size);
  return (bool)out;
}

struct Group {
  uint32_t id;
  std::string name;
};
std::vector<Group> ReadToc(const uint8_t* d, size_t size, std::vector<uint8_t>* header) {
  std::vector<Group> out;
  if (size < 0x20) return out;
  const uint32_t count = Be32(d + 8), groups = Be32(d + 0x10);
  if (header != nullptr) header->assign(d, d + 0x20);
  for (uint32_t i = 0; i < count; i++) {
    const size_t at = groups + (size_t)i * 16;
    if (at + 16 > size) break;
    const uint32_t id = Be32(d + at), nameAt = Be32(d + at + 4);
    if (nameAt >= size) continue;
    out.push_back({id, std::string(reinterpret_cast<const char*>(d + nameAt))});
  }
  return out;
}
std::vector<uint8_t> WriteToc(const std::vector<uint8_t>& header, const std::vector<Group>& groups) {
  std::vector<uint8_t> names, out = header;
  std::vector<uint32_t> nameAt;
  for (const Group& g : groups) {
    nameAt.push_back((uint32_t)(0x20 + names.size()));
    names.insert(names.end(), g.name.begin(), g.name.end());
    names.push_back(0);
  }
  while ((0x20 + names.size()) % 16 != 0) names.push_back(0);
  const uint32_t groupOffset = (uint32_t)(0x20 + names.size());
  out.resize(0x20);
  out[8] = (uint8_t)(groups.size() >> 24);
  out[9] = (uint8_t)(groups.size() >> 16);
  out[10] = (uint8_t)(groups.size() >> 8);
  out[11] = (uint8_t)groups.size();
  out[0x10] = (uint8_t)(groupOffset >> 24);
  out[0x11] = (uint8_t)(groupOffset >> 16);
  out[0x12] = (uint8_t)(groupOffset >> 8);
  out[0x13] = (uint8_t)groupOffset;
  out.insert(out.end(), names.begin(), names.end());
  for (size_t i = 0; i < groups.size(); i++) {
    PutBe32(out, groups[i].id);
    PutBe32(out, nameAt[i]);
    PutBe32(out, 0);
    PutBe32(out, 0);
  }
  return out;
}

bool DolRead(const uint8_t* dol, size_t size, uint32_t address, uint32_t n, std::vector<uint8_t>& out) {
  out.assign(n, 0);
  if (PortDol_Read(dol, size, address, out.data(), n)) return true;
  for (uint32_t take = n; take >= 0x40; take -= std::min<uint32_t>(take, 0x100)) {
    out.assign(take, 0);
    if (PortDol_Read(dol, size, address, out.data(), take)) return true;
  }
  out.clear();
  return false;
}

struct BankKind {
  const char* name;
  std::vector<uint8_t> head;
  std::vector<uint8_t> tail;
};
struct FoundBank {
  uint32_t address;
  std::string tag;
};
std::vector<FoundBank> FindBanks(const uint8_t* dol, size_t size, const BankKind& kind) {
  std::vector<FoundBank> out;
  for (uint32_t i = 0; i < 18; i++) {
    const uint32_t address = Be32(dol + 0x48 + 4 * i), length = Be32(dol + 0x90 + 4 * i), fileOff = Be32(dol + 4 * i);
    if (length == 0 || (uint64_t)fileOff + length > size) continue;
    const uint8_t* s = dol + fileOff;
    for (uint32_t k = 0; k + 24 <= length; k += 4) {
      if (std::memcmp(s + k, kind.head.data(), kind.head.size()) != 0) continue;
      if (std::memcmp(s + k + 8, "\0\0\0\0\0\0\0\0", 8) != 0) continue;
      if (std::memcmp(s + k + 16, kind.tail.data(), kind.tail.size()) != 0) continue;
      out.push_back({address + k, std::string(reinterpret_cast<const char*>(s + k + 6), 2)});
    }
  }
  std::sort(out.begin(), out.end(), [](const FoundBank& a, const FoundBank& b) { return a.address < b.address; });
  return out;
}

uint32_t BankTextEnd(const std::vector<uint8_t>& b) {
  const uint32_t count = (uint32_t)b[4] << 8 | b[5];
  uint32_t most = 0;
  for (uint32_t i = 0; i < count && 0x10 + 8 * i + 8 <= b.size(); i++) most = std::max(most, Be32(b.data() + 0x10 + 8 * i + 4));
  uint32_t at = most;
  while (at + 2 <= b.size() && (b[at] | b[at + 1]) != 0) at += 2;
  return at + 2;
}

uint64_t Fnv(const uint8_t* d, size_t n) {
  uint64_t h = 1469598103934665603ull;
  for (size_t i = 0; i < n; i++) h = (h ^ d[i]) * 1099511628211ull;
  return h;
}

bool JpMovieArchive(const std::string& archive) { return archive == "movie_auto_demo.fsys" || archive == "movie_staffroll.fsys"; }

bool JpKind(uint32_t type, const std::string& archive) {
  if (archive == "name_entry_menu.fsys" || archive == "pcbox_name_entry_menu.fsys" || archive == "colosseumbattle_menu.fsys") return type == 5 || type == 6;
  if (type == 5 || type == 6 || type == 9 || type == 24) return true;
  if (type == 0) return archive == "carde_menu.fsys" || archive == "common.fsys" || archive == "ending_event.fsys";
  if (type == 2) return archive == "title.fsys";
  return false;
}

struct JpWord {
  const char* english;
  const char16_t* japanese;
};
const char16_t kOwaru[] = {0x304A, 0x308F, 0x308B, 0};
const char16_t kGengo[] = {0x3052, 0x3093, 0x3054, 0};
const char16_t kGroupBattle[] = {0x30B0, 0x30EB, 0x30FC, 0x30D7, 0x30D0, 0x30C8, 0x30EB, 0};
const char16_t kQuickBattle[] = {0x30AF, 0x30A4, 0x30C3, 0x30AF, 0x30D0, 0x30C8, 0x30EB, 0};
const char16_t kDougu[] = {0x3069, 0x3046, 0x3050, 0};
const char16_t kYameru[] = {0x3084, 0x3081, 0x308B, 0};
const JpWord kJpWords[] = {
    {"EXIT", kOwaru},
    {"Exit", kOwaru},
    {"Language", kGengo},
    {"GROUP BATTLE", kGroupBattle},
    {"QUICK BATTLE", kQuickBattle},
    {"ITEM", kDougu},
    {"CANCEL", kYameru},
    {"Cancel", kYameru},
};

#include "xd_languages_label_glyphs.inc"
void CheckJpWords() {
  static bool done = false;
  if (done) return;
  done = true;
  for (const JpWord& w : kJpWords) {
    for (const char16_t* c = w.japanese; *c != 0; c++) {
      bool found = false;
      for (uint16_t g : kLabelGlyphs) found = found || g == *c;
      if (!found) Fail(1, std::string("the added word for \"") + w.english + "\" uses a character the label fonts do not have (it would show as a block): U+" + [&] { char b[16]; std::snprintf(b, sizeof b, "%04X", (unsigned)*c); return std::string(b); }());
    }
  }
}

using Text = std::vector<uint8_t>;

uint32_t CtrlArgBytes(uint8_t code) {
  switch (code) {
    case 7: case 9: case 56: case 82: case 83: case 84: case 91: case 92: return 1;
    case 8: return 4;
  }
  return 0;
}

Text JpText(const std::vector<uint8_t>& bank, uint32_t at) {
  Text t;
  while (at + 2 <= bank.size()) {
    if (bank[at] == 0 && bank[at + 1] == 0) break;
    t.push_back(bank[at]);
    t.push_back(bank[at + 1]);
    const bool ctrl = bank[at] == 0xFF && bank[at + 1] == 0xFF;
    at += 2;
    if (ctrl && at < bank.size()) {
      const uint8_t code = bank[at];
      for (uint32_t k = 0; k < 1 + CtrlArgBytes(code) && at < bank.size(); k++) t.push_back(bank[at++]);
    }
  }
  return t;
}

struct Piece {
  bool ctrl;
  uint16_t ch;
  uint8_t code;
  std::string args;
};

std::vector<Piece> Pieces(const Text& t) {
  std::vector<Piece> p;
  for (size_t i = 0; i + 2 <= t.size();) {
    const uint16_t c = (uint16_t)(t[i] << 8 | t[i + 1]);
    i += 2;
    if (c == 0xFFFF && i < t.size()) {
      Piece x{true, 0, t[i++], {}};
      for (uint32_t k = 0; k < CtrlArgBytes(x.code) && i < t.size(); k++) x.args.push_back((char)t[i++]);
      p.push_back(x);
    } else {
      p.push_back({false, c, 0, {}});
    }
  }
  return p;
}

void PutPiece(Text& t, const Piece& p) {
  if (p.ctrl) {
    t.push_back(0xFF);
    t.push_back(0xFF);
    t.push_back(p.code);
    t.insert(t.end(), p.args.begin(), p.args.end());
  } else {
    t.push_back((uint8_t)(p.ch >> 8));
    t.push_back((uint8_t)p.ch);
  }
}

Text ParseMarkup(const char16_t* m, const std::string& what) {
  Text t;
  auto hex = [&](char16_t c) -> int { return c >= '0' && c <= '9' ? c - '0' : c >= 'A' && c <= 'F' ? c - 'A' + 10 : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1; };
  for (; *m != 0; m++) {
    if (*m != u'{') {
      t.push_back((uint8_t)(*m >> 8));
      t.push_back((uint8_t)*m);
      continue;
    }
    std::vector<uint8_t> b;
    m++;
    while (*m != 0 && *m != u'}' && *m != u':') {
      if (hex(m[0]) < 0 || hex(m[1]) < 0) Fail(1, "translation " + what + ": bad control code");
      b.push_back((uint8_t)(hex(m[0]) << 4 | hex(m[1])));
      m += 2;
    }
    if (*m == u':') m++;
    while (*m != 0 && *m != u'}') {
      if (hex(m[0]) < 0 || hex(m[1]) < 0) Fail(1, "translation " + what + ": bad control argument");
      b.push_back((uint8_t)(hex(m[0]) << 4 | hex(m[1])));
      m += 2;
    }
    if (*m != u'}' || b.empty()) Fail(1, "translation " + what + ": unterminated {");
    t.push_back(0xFF);
    t.push_back(0xFF);
    t.insert(t.end(), b.begin(), b.end());
  }
  return t;
}

#include "xd_languages_jp_fonts.inc"
#include "xd_languages_jp_map.inc"
#include "xd_languages_jp_texts.inc"

bool InSet(const uint16_t* a, size_t n, uint16_t c) { return std::binary_search(a, a + n, c); }

std::vector<std::string> CtrlKey(const Text& t) {
  std::vector<std::string> k;
  for (const Piece& p : Pieces(t))
    if (p.ctrl && p.code > 6 && p.code != 0x6D && p.code != 0x6E) k.push_back(std::string(1, (char)p.code) + p.args);
  std::sort(k.begin(), k.end());
  return k;
}

std::string HexId(uint32_t id) {
  char b[16];
  std::snprintf(b, sizeof b, "0x%X", (unsigned)id);
  return b;
}

Text TranslationFor(const JpTranslation& tr, const Text& usText, const std::string& archive) {
  const std::string what = HexId(tr.id) + " (" + archive + ")";
  const Text t = ParseMarkup(tr.text, what);
  if (CtrlKey(t) != CtrlKey(usText)) Fail(1, "translation " + what + " does not hold the control codes of the USA text");
  for (const Piece& p : Pieces(t)) {
    if (p.ctrl) continue;
    bool ok = InSet(kFont0Glyphs, sizeof kFont0Glyphs / sizeof *kFont0Glyphs, p.ch) && InSet(kFont1Glyphs, sizeof kFont1Glyphs / sizeof *kFont1Glyphs, p.ch);
    if (tr.kind == 'L') {
      bool lab = false;
      for (uint16_t g : kLabelGlyphs) lab = lab || g == p.ch;
      ok = ok && lab;
    }
    if (!ok) {
      char b[96];
      std::snprintf(b, sizeof b, ": U+%04X is not in the font it is drawn with (it would show as a block)", (unsigned)p.ch);
      Fail(1, "translation " + what + b);
    }
  }
  return t;
}

struct MergeStats {
  int sameArchive = 0, fromOther = 0, translated = 0, words = 0, kept = 0, left = 0, rubyDropped = 0, noGlyph = 0;
  std::vector<std::string> keptList, leftList, noGlyphList;
};
MergeStats g_stats;
std::map<std::string, std::map<uint32_t, Text>> g_srcTexts;

std::map<std::string, std::set<uint16_t>> g_archGlyphs;

void ReadFontGlyphs(const std::string& archive, const std::vector<uint8_t>& font) {
  size_t o = 0;
  while (o + 24 <= font.size()) {
    const uint32_t id = (uint32_t)font[o] << 8 | font[o + 1], next = Be32(font.data() + o + 4), count = (uint32_t)font[o + 8] << 8 | font[o + 9];
    if (id == 0xFFFF) break;
    for (uint32_t k = 0; k < count && o + 24 + 8 * (size_t)k + 2 <= font.size(); k++) g_archGlyphs[archive].insert((uint16_t)(font[o + 24 + 8 * k] << 8 | font[o + 25 + 8 * k]));
    if (next == 0) break;
    o += next;
  }
}

bool HasGlyph(const std::string& archive, uint16_t c) {
  if (c <= 0x20 || c == 0x3000) return true;
  if (InSet(kFont0Glyphs, sizeof kFont0Glyphs / sizeof *kFont0Glyphs, c) || InSet(kFont1Glyphs, sizeof kFont1Glyphs / sizeof *kFont1Glyphs, c)) return true;
  const auto it = g_archGlyphs.find(archive);
  return it != g_archGlyphs.end() && it->second.count(c) != 0;
}

Text FixGlyphs(const std::string& archive, const Text& t, const std::string& label) {
  const std::vector<Piece> ps = Pieces(t);
  Text out;
  bool lost = false;
  for (size_t i = 0; i < ps.size(); i++) {
    if (ps[i].ctrl && ps[i].code == 4) {
      size_t a = i + 1, b = a;
      while (b < ps.size() && !(ps[b].ctrl && ps[b].code == 5)) b++;
      size_t c = b + 1, d = c;
      while (d < ps.size() && !(ps[d].ctrl && ps[d].code == 6)) d++;
      if (b < ps.size() && d < ps.size()) {
        bool all = true;
        for (size_t k = a; k < b; k++) all = all && (ps[k].ctrl || HasGlyph(archive, ps[k].ch));
        if (all) {
          for (size_t k = i; k <= d; k++) PutPiece(out, ps[k]);
        } else {
          for (size_t k = c; k < d; k++) {
            if (!ps[k].ctrl && !HasGlyph(archive, ps[k].ch)) lost = true;
            PutPiece(out, ps[k]);
          }
          g_stats.rubyDropped++;
        }
        i = d;
        continue;
      }
    }
    if (!ps[i].ctrl && !HasGlyph(archive, ps[i].ch)) {
      const char16_t* kana = ps[i].ch == 0x8A9E ? u"\u3054" : ps[i].ch == 0x82F1 ? u"\u3048\u3044" : nullptr;
      if (kana != nullptr) {
        for (; *kana != 0; kana++) PutPiece(out, Piece{false, (uint16_t)*kana, 0, {}});
        continue;
      }
      lost = true;
    }
    PutPiece(out, ps[i]);
  }
  if (lost) {
    g_stats.noGlyph++;
    g_stats.noGlyphList.push_back(archive + " " + label);
  }
  return out;
}

std::string Ascii(const Text& t) {
  std::string s;
  for (const Piece& p : Pieces(t)) s += p.ctrl ? '.' : (p.ch >= 0x20 && p.ch < 0x7F) ? (char)p.ch : '?';
  return s;
}

std::vector<uint8_t> MergeBank(const std::string& archive, const std::vector<uint8_t>& jp, const std::vector<uint8_t>& us, int* added, bool words = true) {
  CheckJpWords();
  *added = 0;
  if (jp.size() < 0x10 || us.size() < 0x10) return jp;
  std::map<uint32_t, Text> texts;
  const uint32_t jc = (uint32_t)jp[4] << 8 | jp[5], uc = (uint32_t)us[4] << 8 | us[5];
  for (uint32_t i = 0; i < jc && 0x18 + 8 * i <= jp.size(); i++) texts[Be32(jp.data() + 0x10 + 8 * i)] = JpText(jp, Be32(jp.data() + 0x14 + 8 * i));
  g_stats.sameArchive += (int)jc;
  const std::map<uint32_t, Text> own = texts;
  for (uint32_t i = 0; i < uc && 0x18 + 8 * i <= us.size(); i++) {
    const uint32_t id = Be32(us.data() + 0x10 + 8 * i);
    if (texts.count(id) != 0) continue;
    const Text usText = JpText(us, Be32(us.data() + 0x14 + 8 * i));
    Text t;
    bool done = false;
    for (const JpMapRow& r : kJpMap) {
      if (id != r.id || archive != r.archive) continue;
      const auto src = g_srcTexts.find(r.from);
      if (src == g_srcTexts.end()) break;
      const auto it = src->second.find(id);
      if (it == src->second.end()) break;
      t = FixGlyphs(archive, it->second, HexId(id));
      done = true;
      g_stats.fromOther++;
      break;
    }
    for (const JpTranslation& tr : kJpTranslations) {
      if (done || tr.id != id) continue;
      done = true;
      if (tr.kind == 'K') {
        t = usText;
        g_stats.kept++;
        g_stats.keptList.push_back(archive + " " + HexId(id) + " " + Ascii(usText));
      } else if (tr.kind == 'C') {
        std::string s;
        for (const char16_t* c = tr.text + 1; *c != 0; c++) s += (char)*c;
        const auto it = own.find((uint32_t)std::strtoul(s.c_str(), nullptr, 16));
        if (it == own.end()) Fail(1, "translation " + HexId(id) + ": the Japanese bank has no text " + s + " to copy");
        t = it->second;
        g_stats.translated++;
      } else {
        t = TranslationFor(tr, usText, archive);
        g_stats.translated++;
      }
    }
    if (!done) {
      t = usText;
      const std::vector<Piece> ps = Pieces(usText);
      size_t k = ps.size();
      while (k > 0 && !ps[k - 1].ctrl && ps[k - 1].ch >= 0x20 && ps[k - 1].ch < 0x7F) k--;
      std::string word;
      for (size_t q = k; q < ps.size(); q++) word += (char)ps[q].ch;
      bool replaced = false;
      for (const JpWord& w : kJpWords) {
        if (std::getenv("XD_JP_NOWORDS") != nullptr) break;
        if (!words || word != w.english) continue;
        t.clear();
        for (size_t q = 0; q < k; q++) PutPiece(t, ps[q]);
        for (const char16_t* c = w.japanese; *c != 0; c++) PutPiece(t, Piece{false, (uint16_t)*c, 0, {}});
        replaced = true;
        break;
      }
      if (replaced) {
        g_stats.words++;
      } else {
        g_stats.left++;
        g_stats.leftList.push_back(archive + " " + HexId(id) + " " + Ascii(usText));
      }
    }
    if (std::getenv("XD_JP_LOG_ADDED") != nullptr) std::fprintf(stderr, "added id %u in %s: %s\n", id, archive.c_str(), Ascii(t).c_str());
    texts[id] = t;
    (*added)++;
  }
  if (*added == 0) return jp;
  std::vector<uint8_t> out(jp.begin(), jp.begin() + 0x10);
  out[4] = (uint8_t)(texts.size() >> 8);
  out[5] = (uint8_t)texts.size();
  const uint32_t at = 0x10 + 8 * (uint32_t)texts.size();
  std::vector<uint8_t> body;
  for (const auto& [id, t] : texts) {
    PutBe32(out, id);
    PutBe32(out, at + (uint32_t)body.size());
    body.insert(body.end(), t.begin(), t.end());
    body.push_back(0);
    body.push_back(0);
    if (body.size() % 2 != 0) body.push_back(0);
  }
  out.insert(out.end(), body.begin(), body.end());
  while (out.size() % 4 != 0) out.push_back(0);
  return out;
}

struct JpEntry {
  uint32_t index;
  uint16_t id;
  uint32_t type;
  std::string name;
  uint64_t hash;
  uint32_t dims;
};

bool IsJpSheet(const std::string& name) {
  static const char* const sheets[] = {"uv_pkm_status_00", "uv_pkm_status_01", "uv_rlv_bg_01", "uv_pda_all_00", "uv_psc_top_02", "uv_psc_top_03",
                                       "uv_btl_tool_01", "uv_tool_sanka_00", "uv_str_bingo_02", "uv_bingo_aisho_00", "uv_tool_vs_01", "uv_ttl_battle_00"};
  for (const char* n : sheets)
    if (name == n) return true;
  return false;
}

bool JpSizeOk(const JpEntry& us, const std::vector<uint8_t>& jp) {
  if (const char* skip = std::getenv("XD_JP_SKIP"); skip != nullptr && (std::string(",") + skip + ",").find("," + us.name + ",") != std::string::npos) return false;
  if (jp.size() < 4 || Be32(jp.data()) == us.dims) return true;
  if (IsJpSheet(us.name)) return true;
  return us.name == "uv_ttl_select_00";
}

size_t MenuSpriteJp(std::vector<uint8_t>& us, const std::vector<uint8_t>& jp, const std::set<uint32_t>& sheetIds) {
  size_t changed = 0;
  for (uint32_t id = 4; id <= 1015; id++) {
    const size_t uo = (size_t)id * 0x48, jo = (size_t)(id + 3) * 0x18;
    if (uo + 0x48 > us.size() || jo + 0x18 > jp.size()) break;
    const uint32_t tex = Be32(us.data() + uo + 8 + 8);
    if (sheetIds.count(tex) == 0 || Be32(jp.data() + jo + 8 + 8) != tex) continue;
    if (std::memcmp(us.data() + uo + 8, jp.data() + jo + 8, 8) == 0) continue;
    for (int l = 0; l < 5; l++) std::memcpy(us.data() + uo + 8 + 0xC * l, jp.data() + jo + 8, 8);
    changed++;
  }
  return changed;
}

int MakeJapanese(const std::string& jpDisc, const std::string& xdDisc, const std::string& out) {
  const fs::path outFinal = fs::path(out), outTmp = fs::path(out + ".tmp");
  std::error_code ec;
  fs::remove_all(outTmp, ec);
  char error[512];
  std::map<std::string, std::vector<JpEntry>> usEntries;
  std::map<std::pair<std::string, uint32_t>, std::vector<uint8_t>> usBanks;
  std::vector<uint8_t> usCommonTbl, usSystemTbl, usTableRes;
  std::vector<uint8_t> jpSpriteTbl;
  std::set<uint32_t> sheetTex;
  std::set<std::string> usFiles;
  std::vector<std::string> usFsys;
  auto readUs = [&](bool entries) {
    if (!PortDisc_Open(xdDisc.c_str(), error, sizeof error)) Fail(1, std::string("can't read the USA disc: ") + error);
    if (std::strcmp(PortDisc_GameId(), "GXXE01") != 0) {
      Fail(3, std::string("the main disc is ") + PortDisc_GameId() + ", not the USA copy (GXXE01)");
    }
    DiscFiles list;
    PortDisc_ListFiles(Collect, &list);
    usFiles = list.all;
    usFsys = list.fsys;
    if (!entries) return;
    int n = 0;
    for (const std::string& name : list.fsys) {
      std::printf("progress %d %zu reading the USA disc\n", n++, list.fsys.size() * 3);
      std::fflush(stdout);
      size_t size = 0;
      uint8_t* data = PortDisc_ReadOriginalFile(name.c_str(), &size);
      FsysArchive fsys{};
      if (data == nullptr || !Fsys_Open(data, size, &fsys)) {
        PortDisc_Free(data);
        continue;
      }
      for (uint32_t i = 0; i < fsys.count; i++) {
        FsysEntry e{};
        if (!Fsys_Entry(&fsys, i, &e) || !JpKind(e.type, name)) continue;
        uint32_t n2 = 0;
        uint8_t* d = Fsys_Extract(&fsys, i, &n2);
        if (d == nullptr) continue;
        usEntries[name].push_back({i, e.id, e.type, e.name, Fnv(d, n2), n2 >= 4 ? Be32(d) : 0u});
        if (e.type == 5) usBanks[{name, i}].assign(d, d + n2);
        std::free(d);
      }
      PortDisc_Free(data);
    }
    size_t dolSize = 0;
    const uint8_t* dol = PortDisc_Dol(&dolSize);
    const BankKind common{"common_tbl", {0, 0, 0, 1, 0x03, 0x5B}, {0, 0, 0, 1}};
    const BankKind system{"system_error_tbl", {0, 0, 0, 0, 0x00, 0x05}, {0, 0, 0, 0x65, 0, 0, 0, 0x38}};
    for (const BankKind* kind : {&common, &system}) {
      for (const FoundBank& b : FindBanks(dol, dolSize, *kind)) {
        if (b.tag != "US") continue;
        std::vector<uint8_t> bank;
        if (!DolRead(dol, dolSize, b.address, 0x20000, bank)) continue;
        bank.resize(std::min<size_t>(bank.size(), BankTextEnd(bank) + 0x20));
        (kind == &common ? usCommonTbl : usSystemTbl) = bank;
      }
    }
    size_t cs = 0;
    uint8_t* cf = PortDisc_ReadOriginalFile("common.fsys", &cs);
    FsysArchive cfs{};
    if (cf != nullptr && Fsys_Open(cf, cs, &cfs)) {
      for (uint32_t i = 0; i < cfs.count; i++) {
        FsysEntry e{};
        if (!Fsys_Entry(&cfs, i, &e) || e.type != 14 || std::strcmp(e.name, "common_rel") != 0) continue;
        uint32_t rs = 0;
        uint8_t* rel = Fsys_Extract(&cfs, i, &rs);
        for (uint32_t at = 0; rel != nullptr && at + 24 <= rs && usTableRes.empty(); at += 4) {
          if (rel[at] == 0 && rel[at + 1] == 0 && rel[at + 2] == 0 && rel[at + 3] == 1 && rel[at + 4] == 0x07 && rel[at + 5] == 0x8B &&
              std::memcmp(rel + at + 6, "US", 2) == 0) {
            usTableRes.assign(rel + at, rel + rs);
            usTableRes.resize(std::min<size_t>(usTableRes.size(), BankTextEnd(usTableRes) + 0x20));
          }
        }
        std::free(rel);
      }
    }
    PortDisc_Free(cf);
    PortDisc_Close();
  };
  readUs(true);
  if (usCommonTbl.empty() || usTableRes.empty()) Fail(1, "the USA disc's text banks were not found");

  if (!PortDisc_Open(jpDisc.c_str(), error, sizeof error)) Fail(1, std::string("can't read the Japanese disc: ") + error);
  if (std::strcmp(PortDisc_GameId(), xd::languages::kJpGameId) != 0) {
    Fail(2, std::string("this disc is ") + PortDisc_GameId() + ", not the Japanese copy (GXXJ01)");
  }
  struct Repl {
    uint32_t usIndex;
    std::vector<uint8_t> data;
  };
  std::map<std::string, std::vector<Repl>> repl;
  DiscFiles jp;
  PortDisc_ListFiles(Collect, &jp);
  {
    std::set<std::string> sources;
    for (const JpMapRow& r : kJpMap) sources.insert(r.from);
    for (const std::string& src : sources) {
      size_t size = 0;
      uint8_t* data = PortDisc_ReadOriginalFile(src.c_str(), &size);
      FsysArchive fsys{};
      if (data == nullptr || !Fsys_Open(data, size, &fsys)) {
        PortDisc_Free(data);
        continue;
      }
      for (uint32_t i = 0; i < fsys.count; i++) {
        FsysEntry e{};
        uint32_t n2 = 0;
        if (!Fsys_Entry(&fsys, i, &e) || e.type != 5) continue;
        uint8_t* d = Fsys_Extract(&fsys, i, &n2);
        if (d == nullptr) continue;
        const std::vector<uint8_t> bank(d, d + n2);
        std::free(d);
        const uint32_t count = bank.size() >= 8 ? ((uint32_t)bank[4] << 8 | bank[5]) : 0;
        for (uint32_t k = 0; k < count && 0x18 + 8 * k <= bank.size(); k++) g_srcTexts[src].emplace(Be32(bank.data() + 0x10 + 8 * k), JpText(bank, Be32(bank.data() + 0x14 + 8 * k)));
      }
      PortDisc_Free(data);
    }
  }
  int n = 0, unmatched = 0, taken = 0, mergedTexts = 0, keptSize = 0;
  std::map<std::pair<uint16_t, std::string>, std::vector<uint8_t>> pool;
  std::map<std::string, std::vector<uint8_t>> rawMovies;
  for (const std::string& name : jp.fsys) {
    std::printf("progress %zu %zu reading the Japanese disc\n", usFsys.size() + (size_t)n++, usFsys.size() * 3);
    std::fflush(stdout);
    const auto usIt = usEntries.find(name);
    if (usFiles.count(name) == 0 || (usIt == usEntries.end() && !JpMovieArchive(name))) continue;
    size_t size = 0;
    uint8_t* data = PortDisc_ReadOriginalFile(name.c_str(), &size);
    FsysArchive fsys{};
    if (data == nullptr || !Fsys_Open(data, size, &fsys)) {
      PortDisc_Free(data);
      continue;
    }
    if (JpMovieArchive(name)) {
      rawMovies[name].assign(data, data + size);
      PortDisc_Free(data);
      continue;
    }
    for (uint32_t i = 0; i < fsys.count; i++) {
      FsysEntry e{};
      uint32_t n2 = 0;
      if (!Fsys_Entry(&fsys, i, &e) || e.type != 6) continue;
      if (uint8_t* d = Fsys_Extract(&fsys, i, &n2)) {
        ReadFontGlyphs(name, std::vector<uint8_t>(d, d + n2));
        std::free(d);
      }
    }
    std::set<uint32_t> used;
    for (uint32_t i = 0; i < fsys.count; i++) {
      FsysEntry e{};
      if (!Fsys_Entry(&fsys, i, &e) || !JpKind(e.type, name)) continue;
      const JpEntry* match = nullptr;
      int sameName = 0;
      for (const JpEntry& u : usIt->second) {
        if (u.type != e.type || used.count(u.index) != 0) continue;
        if (u.id == e.id && u.name == e.name) {
          match = &u;
          sameName = 1;
          break;
        }
        if (u.name == e.name) {
          sameName++;
          match = &u;
        }
      }
      if (match == nullptr || sameName > 1) {
        if (e.type == 5 || e.type == 6) unmatched++;
        if (e.type == 9 && match == nullptr) {
          uint32_t n3 = 0;
          if (uint8_t* d3 = Fsys_Extract(&fsys, i, &n3)) {
            pool[{e.id, e.name}].assign(d3, d3 + n3);
            std::free(d3);
          }
        }
        continue;
      }
      uint32_t n2 = 0;
      uint8_t* d = Fsys_Extract(&fsys, i, &n2);
      if (d == nullptr) continue;
      used.insert(match->index);
      std::vector<uint8_t> take(d, d + n2);
      if (e.type == 5) {
        int added = 0;
        take = MergeBank(name, take, usBanks[{name, match->index}], &added);
        mergedTexts += added;
      }
      if (e.type == 9 && IsJpSheet(match->name)) sheetTex.insert((uint32_t)match->id << 16 | 0x1200);
      if (e.type == 9 && !JpSizeOk(*match, take)) {
        keptSize++;
      } else if (Fnv(take.data(), take.size()) != match->hash) {
        repl[name].push_back({match->index, std::move(take)});
        taken++;
      }
      std::free(d);
    }
    PortDisc_Free(data);
  }
  for (const auto& [name, list] : usEntries) {
    for (const JpEntry& u : list) {
      if (u.type != 9) continue;
      const auto it = pool.find({u.id, u.name});
      if (it == pool.end() || Fnv(it->second.data(), it->second.size()) == u.hash || !JpSizeOk(u, it->second)) continue;
      bool already = false;
      for (const Repl& r : repl[name]) already = already || r.usIndex == u.index;
      if (already) continue;
      repl[name].push_back({u.index, it->second});
      if (IsJpSheet(u.name)) sheetTex.insert((uint32_t)u.id << 16 | 0x1200);
      taken++;
    }
    if (repl[name].empty()) repl.erase(name);
  }
  std::fprintf(stderr, "japanese: %d entries in %zu archives replace the USA ones; %d text / font entries without a USA counterpart; %d USA texts added to the Japanese banks; %d pictures of another size left as they are\n",
               taken, repl.size(), unmatched, mergedTexts, keptSize);

  fs::create_directories(outTmp / "files", ec);
  fs::create_directories(outTmp / "tables", ec);
  uint64_t bytes = 0;
  {
    size_t dolSize = 0;
    const uint8_t* dol = PortDisc_Dol(&dolSize);
    if (dol == nullptr) Fail(1, "the Japanese disc's main program could not be read");
    struct Item {
      const char* name;
      uint32_t address, size;
      int kind;
    };
    const Item items[] = {{"resident_fnt_jp", 0x8031D130, 0x476A0, 0},
                          {"common_fnt_jp", 0x803647D0, 0x15D00, 0},
                          {"common_fnt2_jp", 0x8037A4D0, 0x13BE0, 0},
                          {"common_tbl_jp", 0x8038E0B0, 0x11D10, 1},
                          {"system_error_tbl_jp", 0x8039FDC0, 0x2A0, 1}};
    if (!DolRead(dol, dolSize, 0x802FA460, 1019 * 0x18, jpSpriteTbl) || jpSpriteTbl.size() != 1019 * 0x18) {
      Fail(1, "the Japanese menu picture table is not where it was expected in the Japanese disc's main program");
    }
    for (const Item& it : items) {
      std::vector<uint8_t> d;
      if (!DolRead(dol, dolSize, it.address, it.size, d) || d.size() != it.size) {
        Fail(1, std::string(it.name) + " is not where it was expected in the Japanese disc's main program");
      }
      const bool bad = it.kind == 0 ? (d[2] != d[3] || (d[2] != 0x16 && d[2] != 0x12)) : std::memcmp(d.data() + 6, "JP", 2) != 0;
      if (bad) Fail(1, std::string(it.name) + " does not look like it should: is this the Japanese retail disc?");
      if (it.kind == 1) {
        int added = 0;
        d = MergeBank("main.dol", d, std::strcmp(it.name, "common_tbl_jp") == 0 ? usCommonTbl : usSystemTbl, &added);
        std::fprintf(stderr, "japanese: %s: %d USA texts added\n", it.name, added);
      }
      if (!WriteFile(outTmp / "tables" / (std::string(it.name) + ".bin"), d.data(), d.size())) Fail(1, "could not write a table");
      bytes += d.size();
    }
  }
  {
    size_t size = 0;
    uint8_t* fsysData = PortDisc_ReadOriginalFile("common.fsys", &size);
    FsysArchive fsys{};
    if (fsysData == nullptr || !Fsys_Open(fsysData, size, &fsys)) Fail(1, "the Japanese disc's common.fsys could not be read");
    uint32_t relSize = 0;
    uint8_t* rel = nullptr;
    for (uint32_t i = 0; i < fsys.count && rel == nullptr; i++) {
      FsysEntry e{};
      if (Fsys_Entry(&fsys, i, &e) && e.type == 14 && std::strcmp(e.name, "common_rel") == 0) rel = Fsys_Extract(&fsys, i, &relSize);
    }
    if (rel == nullptr) Fail(1, "the Japanese common_rel could not be read");
    uint32_t start = 0;
    for (uint32_t at = 0; at + 24 <= relSize && start == 0; at += 4) {
      if (rel[at] == 0 && rel[at + 1] == 0 && rel[at + 2] == 0 && rel[at + 3] == 1 && std::memcmp(rel + at + 6, "JP", 2) == 0 &&
          std::memcmp(rel + at + 8, "\0\0\0\0\0\0\0\0", 8) == 0) {
        start = at;
      }
    }
    if (start == 0) Fail(1, "the Japanese common_rel's text bank was not found");
    std::vector<uint8_t> bank(rel + start, rel + relSize);
    bank.resize(std::min<size_t>(bank.size(), BankTextEnd(bank) + 0x20));
    int added = 0;
    bank = MergeBank("common.fsys", bank, usTableRes, &added);
    std::fprintf(stderr, "japanese: table_res_jp: %d USA texts added\n", added);
    if (!WriteFile(outTmp / "tables" / "table_res_jp.bin", bank.data(), bank.size())) Fail(1, "could not write a text bank");
    bytes += bank.size();
    std::free(rel);
    PortDisc_Free(fsysData);
  }
  PortDisc_Close();
  std::fprintf(stderr, "japanese texts: %d in the Japanese banks themselves; USA-only ids filled: %d from the Japanese archive the decomp's list names, %d translated by the port (xd_languages_jp_translations.tsv), %d added words in kana, %d kept as the USA text on purpose\n",
               g_stats.sameArchive, g_stats.fromOther, g_stats.translated, g_stats.words, g_stats.kept);
  std::fprintf(stderr, "ruby kanji given up for their reading (the archive's font lacks them): %d; texts that still use a character their archive's fonts lack: %d\n", g_stats.rubyDropped, g_stats.noGlyph);
  for (const std::string& l : g_stats.noGlyphList) std::fprintf(stderr, "  no glyph: %s\n", l.c_str());
  std::fprintf(stderr, "US-only texts left in English: %d\n", g_stats.left);
  for (const std::string& l : g_stats.leftList) std::fprintf(stderr, "  left: %s\n", l.c_str());
  for (const std::string& l : g_stats.keptList) std::fprintf(stderr, "  kept on purpose: %s\n", l.c_str());

  readUs(false);
  {
    size_t dolSize = 0;
    const uint8_t* dol = PortDisc_Dol(&dolSize);
    std::vector<uint8_t> tbl;
    if (dol == nullptr || !DolRead(dol, dolSize, 0x803001A8, 0x11E98, tbl) || tbl.size() != 0x11E98) Fail(1, "the USA menu picture table could not be read");
    const size_t rows = MenuSpriteJp(tbl, jpSpriteTbl, sheetTex);
    std::fprintf(stderr, "japanese: _menu_sprite: %zu rows of %zu sheets got the Japanese rectangles\n", rows, sheetTex.size());
    if (!WriteFile(outTmp / "tables" / "_menu_sprite_jp.bin", tbl.data(), tbl.size())) Fail(1, "could not write the menu picture table");
    bytes += tbl.size();
  }
  size_t step = 0;
  int made = 0;
  for (auto& [name, list] : repl) {
    std::printf("progress %zu %zu %s\n", usFsys.size() * 2 + step++, usFsys.size() * 3, name.c_str());
    std::fflush(stdout);
    size_t size = 0;
    uint8_t* data = PortDisc_ReadOriginalFile(name.c_str(), &size);
    if (data == nullptr) Fail(1, "could not read " + name + " from the USA disc");
    std::vector<FsysReplacement> r;
    for (const Repl& x : list) r.push_back({x.usIndex, x.data.data(), (uint32_t)x.data.size()});
    size_t newSize = 0;
    uint8_t* rebuilt = Fsys_Rebuild(data, size, r.data(), r.size(), &newSize);
    if (rebuilt == nullptr) Fail(1, "could not rebuild " + name);
    if (!WriteFile(outTmp / "files" / name, rebuilt, newSize)) Fail(1, "could not write " + name + " (is there room in the data folder?)");
    bytes += newSize;
    made++;
    std::free(rebuilt);
    PortDisc_Free(data);
  }
  for (const auto& [name, bytesOfFile] : rawMovies) {
    if (!WriteFile(outTmp / "files" / name, bytesOfFile.data(), bytesOfFile.size())) Fail(1, "could not write " + name + " (is there room in the data folder?)");
    bytes += bytesOfFile.size();
    made++;
  }
  PortDisc_Close();
  const std::string json = xd::languages::PackJson({"jp"}, made, xd::languages::kJpGameId);
  WriteFile(outTmp / xd::languages::kPackFile, json.data(), json.size());
  const std::string mod = std::string("{\n  \"id\": \"") + xd::languages::kJpManifestId +
                          "\",\n  \"name\": \"Japanese\",\n  \"version\": \"" + kJpVersion +
                          "\",\n  \"description\": \"The Japanese text of the Japanese disc.\",\n  \"game\": [\"GXXE01\"],\n"
                          "  \"count\": 1,\n  \"unit\": \"language\",\n  \"units\": \"languages\",\n  \"detail\": \"Japanese\"\n}\n";
  WriteFile(outTmp / "mod.json", mod.data(), mod.size());
  fs::remove_all(outFinal, ec);
  fs::rename(outTmp, outFinal, ec);
  if (ec) Fail(1, "could not put the finished pack in place: " + ec.message());
  std::printf("done %d %d %llu\n", made, 0, (unsigned long long)bytes);
  std::fflush(stdout);
  return 0;
}

}

int main(int argc, char** argv) {
  std::string palDisc, xdDisc, out, jpDisc;
  for (int i = 1; i < argc; i++) {
    const std::string a = argv[i];
    if (a == "--version") {
      const bool jp = i + 1 < argc && std::string(argv[i + 1]) == "jp";
      std::printf("xd_languages %s\n", jp ? kJpVersion : kVersion);
      return 0;
    }
    const auto value = [&](std::string& dst) {
      if (i + 1 < argc) dst = argv[++i];
    };
    if (a == "--pal-disc") value(palDisc);
    else if (a == "--jp-disc") value(jpDisc);
    else if (a == "--xd-disc") value(xdDisc);
    else if (a == "--out") value(out);
  }
  if (!jpDisc.empty() && !xdDisc.empty() && !out.empty()) return MakeJapanese(jpDisc, xdDisc, out);
  if (palDisc.empty() || xdDisc.empty() || out.empty()) {
    std::fprintf(stderr, "Usage: xd_languages --pal-disc <GXXP01 image> --xd-disc <GXXE01 image> --out <folder>\n"
                         "       xd_languages --jp-disc <GXXJ01 image> --xd-disc <GXXE01 image> --out <folder>\n");
    return 1;
  }
  const fs::path outFinal = fs::path(out), outTmp = fs::path(out + ".tmp");
  std::error_code ec;
  fs::remove_all(outTmp, ec);

  UsInfo us;
  char error[512];
  if (!PortDisc_Open(xdDisc.c_str(), error, sizeof error)) Fail(1, std::string("can't read the USA disc: ") + error);
  if (std::strcmp(PortDisc_GameId(), "GXXE01") != 0) {
    Fail(3, std::string("the main disc is ") + PortDisc_GameId() + ", not the USA copy (GXXE01)");
  }
  {
    DiscFiles list;
    PortDisc_ListFiles(Collect, &list);
    us.files = list.all;
    size_t tocSize = 0;
    if (uint8_t* toc = PortDisc_ReadOriginalFile("GSfsys.toc", &tocSize)) {
      us.toc.assign(toc, toc + tocSize);
      PortDisc_Free(toc);
    }
    int n = 0;
    for (const std::string& name : list.fsys) {
      std::printf("progress %d %zu reading the USA disc\n", n++, list.fsys.size() * 2 + 8);
      std::fflush(stdout);
      size_t size = 0;
      uint8_t* data = PortDisc_ReadOriginalFile(name.c_str(), &size);
      FsysArchive fsys{};
      if (data == nullptr || !Fsys_Open(data, size, &fsys)) {
        PortDisc_Free(data);
        continue;
      }
      if (!BaseName(name).empty()) us.suffixHash[name] = HashBytes(data, size);
      for (uint32_t i = 0; i < fsys.count; i++) {
        FsysEntry e{};
        if (!Fsys_Entry(&fsys, i, &e)) continue;
        if (e.type == 5) us.messages[Lower(name)]++;
        if (e.type == 14 || e.type == 6) {
          uint32_t relSize = 0;
          if (uint8_t* rel = Fsys_Extract(&fsys, i, &relSize)) {
            us.rels[Lower(name)][e.id].assign(rel, rel + relSize);
            std::free(rel);
          }
        }
      }
      PortDisc_Free(data);
    }
  }
  if (us.toc.empty()) Fail(1, "the USA disc has no archive list (GSfsys.toc)");
  PortDisc_Close();

  if (!PortDisc_Open(palDisc.c_str(), error, sizeof error)) Fail(1, std::string("can't read the European disc: ") + error);
  if (std::strcmp(PortDisc_GameId(), xd::languages::kPalGameId) != 0) {
    Fail(2, std::string("this disc is ") + PortDisc_GameId() + ", not the European copy (GXXP01)");
  }
  DiscFiles pal;
  PortDisc_ListFiles(Collect, &pal);
  size_t palTocSize = 0;
  uint8_t* palToc = PortDisc_ReadOriginalFile("GSfsys.toc", &palTocSize);
  if (palToc == nullptr) Fail(1, "the European disc has no archive list (GSfsys.toc)");
  std::vector<uint8_t> usHeader;
  std::vector<Group> usGroups = ReadToc(us.toc.data(), us.toc.size(), &usHeader);
  const std::vector<Group> palGroups = ReadToc(palToc, palTocSize, nullptr);
  PortDisc_Free(palToc);
  std::set<uint32_t> usIds;
  for (const Group& g : usGroups) usIds.insert(g.id);

  std::vector<std::string> wanted;
  for (const std::string& name : pal.fsys) {
    const std::string lower = Lower(name);
    const bool languageNamed = !BaseName(name).empty();
    const bool palOnly = us.files.count(name) == 0;
    bool moreText = false;
    if (!languageNamed && !palOnly) {
      moreText = true;
    }
    (void)lower;
    if (languageNamed || palOnly || moreText) wanted.push_back(name);
  }

  fs::create_directories(outTmp / "files", ec);
  fs::create_directories(outTmp / "tables", ec);
  uint64_t bytes = 0;
  int made = 0, kept = 0;
  std::vector<std::string> madeNames;
  std::map<std::string, uint32_t> madeEntries;
  const size_t total = wanted.size() + 8;
  size_t done = pal.fsys.size() + 0;
  (void)done;
  size_t step = 0;
  for (const std::string& name : wanted) {
    step++;
    std::printf("progress %zu %zu %s\n", step, total, name.c_str());
    std::fflush(stdout);
    size_t size = 0;
    uint8_t* data = PortDisc_ReadOriginalFile(name.c_str(), &size);
    FsysArchive fsys{};
    if (data == nullptr || !Fsys_Open(data, size, &fsys)) {
      PortDisc_Free(data);
      continue;
    }
    if (!BaseName(name).empty()) {
      const auto same = us.suffixHash.find(name);
      if (same != us.suffixHash.end() && same->second == HashBytes(data, size)) {
        std::fprintf(stderr, "left out %s: the USA disc has the same file (not a language archive)\n", name.c_str());
        PortDisc_Free(data);
        kept++;
        continue;
      }
    }
    int messages = 0;
    std::vector<FsysReplacement> replacements;
    std::vector<std::vector<uint8_t>> keep;
    bool missingRel = false;
    keep.reserve(fsys.count);
    for (uint32_t i = 0; i < fsys.count; i++) {
      FsysEntry e{};
      if (!Fsys_Entry(&fsys, i, &e)) continue;
      if (e.type == 5) messages++;
      if (e.type != 14 && e.type != 6) continue;
      const std::vector<uint8_t>* rel = nullptr;
      for (const std::string& usName : {BaseName(name), name}) {
        if (rel != nullptr || usName.empty() || us.files.count(usName) == 0) continue;
        auto archive = us.rels.find(Lower(usName));
        if (archive == us.rels.end()) continue;
        auto it = archive->second.find(e.id);
        if (it != archive->second.end()) rel = &it->second;
      }
      if (rel == nullptr) {
        if (e.type == 6) continue;
        missingRel = true;
        break;
      }
      keep.push_back(*rel);
      replacements.push_back({i, keep.back().data(), (uint32_t)keep.back().size()});
    }
    const bool languageNamed = !BaseName(name).empty(), palOnly = us.files.count(name) == 0;
    const bool mapWithText = !languageNamed && !palOnly && messages > us.messages[Lower(name)];
    if (!languageNamed && !palOnly && !mapWithText) {
      PortDisc_Free(data);
      kept++;
      continue;
    }
    if (missingRel) {
      std::fprintf(stderr, "skipped %s: it holds code and the USA disc has no counterpart\n", name.c_str());
      PortDisc_Free(data);
      continue;
    }
    size_t newSize = 0;
    uint8_t* rebuilt = replacements.empty() ? nullptr : Fsys_Rebuild(data, size, replacements.data(), replacements.size(), &newSize);
    const uint8_t* write = rebuilt != nullptr ? rebuilt : data;
    const size_t writeSize = rebuilt != nullptr ? newSize : size;
    if (!replacements.empty() && rebuilt == nullptr) {
      PortDisc_Free(data);
      Fail(1, "could not rebuild " + name);
    }
    if (!WriteFile(outTmp / "files" / name, write, writeSize)) {
      std::free(rebuilt);
      PortDisc_Free(data);
      Fail(1, "could not write " + name + " (is there room in the data folder?)");
    }
    bytes += writeSize;
    made++;
    madeNames.push_back(name);
    madeEntries[name] = fsys.count;
    std::free(rebuilt);
    PortDisc_Free(data);
  }

  std::vector<std::string> languages;
  {
    std::printf("progress %zu %zu text and fonts\n", total - 1, total);
    std::fflush(stdout);
    size_t dolSize = 0;
    const uint8_t* dol = PortDisc_Dol(&dolSize);
    if (dol == nullptr) Fail(1, "the European disc's main program could not be read");
    const BankKind common{"common_tbl", {0, 0, 0, 1, 0x03, 0x5B}, {0, 0, 0, 1}};
    const BankKind system{"system_error_tbl", {0, 0, 0, 0, 0x00, 0x05}, {0, 0, 0, 0x65, 0, 0, 0, 0x38}};
    for (const BankKind* kind : {&common, &system}) {
      const std::vector<FoundBank> found = FindBanks(dol, dolSize, *kind);
      for (size_t i = 0; i < found.size(); i++) {
        const xd::languages::Language* lang = nullptr;
        for (const auto& l : xd::languages::All()) {
          if (found[i].tag == l.tag) lang = &l;
        }
        const bool uk = found[i].tag == "UK";
        if (lang == nullptr && !uk) continue;
        if (uk) continue;
        std::vector<uint8_t> bank;
        const uint32_t room = i + 1 < found.size() ? found[i + 1].address - found[i].address : 0x20000;
        if (!DolRead(dol, dolSize, found[i].address, std::min<uint32_t>(room, 0x20000), bank)) Fail(1, "a text table could not be read");
        const uint32_t end = std::min<uint32_t>(BankTextEnd(bank) + 0x20, (uint32_t)bank.size());
        bank.resize(end);
        const std::string file = std::string(kind->name) + "_" + lang->id;
        if (!WriteFile(outTmp / "tables" / (file + ".bin"), bank.data(), bank.size())) Fail(1, "could not write " + file);
        bytes += bank.size();
        if (kind == &common) languages.push_back(lang->id);
      }
    }
    struct Font {
      const char* name;
      uint32_t address, size;
    };
    for (const Font& f : {Font{"resident_fnt", 0x80330768, 0x45060}, Font{"common_fnt", 0x803757C8, 0x310},
                          Font{"_menu_sprite", 0x803018D8, 0x11E98}}) {
      std::vector<uint8_t> data;
      const bool font = f.name[0] != '_';
      if (!DolRead(dol, dolSize, f.address, f.size, data) || (font && (data[2] != 0x16 || data[3] != 0x16))) {
        Fail(1, std::string("the font ") + f.name + " is not where it was expected in the European disc's main program");
      }
      if (!WriteFile(outTmp / "tables" / (std::string(f.name) + ".bin"), data.data(), data.size())) Fail(1, "could not write a font");
      bytes += data.size();
    }
  }
  {
    struct LayoutRow {
      int table, row;
    };
    static const LayoutRow kRows[] = {
#include "xd_languages_layout_rows.inc"
    };
    static const uint32_t kPalTable[4] = {0x804277B8, 0x80313D20, 0x804212D0, 0x8042CB28};
    static const uint32_t kPalSize[4] = {0x4F50, 0xFA28, 0x32C0, 0x1228};
    size_t dolSize = 0;
    const uint8_t* dol = PortDisc_Dol(&dolSize);
    std::vector<uint8_t> out;
    for (const LayoutRow& r : kRows) {
      std::vector<uint8_t> row;
      if (!DolRead(dol, dolSize, kPalTable[r.table] + (uint32_t)r.row * 28, 28, row) || (uint32_t)r.row * 28 + 28 > kPalSize[r.table]) {
        Fail(1, "a menu layout row is not where it was expected in the European disc's main program");
      }
      out.push_back((uint8_t)r.table);
      out.push_back(0);
      out.push_back((uint8_t)(r.row >> 8));
      out.push_back((uint8_t)r.row);
      out.push_back(row[1]);
      out.push_back(0);
      out.insert(out.end(), row.begin() + 6, row.begin() + 0x0E);
      out.insert(out.end(), row.begin() + 0x10, row.begin() + 0x14);
      out.insert(out.end(), row.begin() + 0x18, row.begin() + 0x1A);
    }
    if (!WriteFile(outTmp / "tables" / "menu_layout.bin", out.data(), out.size())) Fail(1, "could not write the menu layout");
    bytes += out.size();
    std::vector<uint8_t> images;
    for (int t = 0; t < 4; t++) {
      for (uint32_t row = 0; row * 28 + 28 <= kPalSize[t]; row++) {
        std::vector<uint8_t> r;
        if (!DolRead(dol, dolSize, kPalTable[t] + row * 28, 28, r)) Fail(1, "a menu item row is not in the European main program");
        images.insert(images.end(), r.begin() + 4, r.begin() + 6);
        images.insert(images.end(), r.begin() + 0x0E, r.begin() + 0x10);
      }
    }
    if (!WriteFile(outTmp / "tables" / "menu_images.bin", images.data(), images.size())) Fail(1, "could not write the menu pictures");
    bytes += images.size();
  }
  {
    size_t size = 0;
    uint8_t* fsysData = PortDisc_ReadOriginalFile("common.fsys", &size);
    FsysArchive fsys{};
    if (fsysData == nullptr || !Fsys_Open(fsysData, size, &fsys)) Fail(1, "the European disc's common.fsys could not be read");
    int relIndex = -1;
    for (uint32_t i = 0; i < fsys.count; i++) {
      FsysEntry e{};
      if (Fsys_Entry(&fsys, i, &e) && e.type == 14 && std::strcmp(e.name, "common_rel") == 0) relIndex = (int)i;
    }
    uint32_t relSize = 0;
    uint8_t* rel = relIndex >= 0 ? Fsys_Extract(&fsys, (uint32_t)relIndex, &relSize) : nullptr;
    if (rel == nullptr) Fail(1, "the European common_rel could not be read");
    std::vector<std::pair<uint32_t, std::string>> found;
    for (uint32_t at = 0; at + 24 <= relSize; at += 4) {
      if (rel[at] == 0 && rel[at + 1] == 0 && rel[at + 2] == 0 && rel[at + 3] == 1 && rel[at + 4] == 0x07 && rel[at + 5] == 0x8B &&
          std::memcmp(rel + at + 8, "\0\0\0\0\0\0\0\0", 8) == 0) {
        found.push_back({at, std::string(reinterpret_cast<const char*>(rel + at + 6), 2)});
      }
    }
    int taken = 0;
    for (size_t i = 0; i < found.size(); i++) {
      const xd::languages::Language* lang = nullptr;
      for (const auto& l : xd::languages::All()) {
        if (found[i].second == l.tag && std::strcmp(l.id, "us") != 0) lang = &l;
      }
      if (lang == nullptr) continue;
      const uint32_t end = i + 1 < found.size() ? found[i + 1].first : relSize;
      std::vector<uint8_t> bank(rel + found[i].first, rel + end);
      if (i + 1 == found.size()) bank.resize(std::min<size_t>(bank.size(), BankTextEnd(bank) + 0x20));
      if (!WriteFile(outTmp / "tables" / (std::string("table_res_") + lang->id + ".bin"), bank.data(), bank.size())) Fail(1, "could not write a text bank");
      bytes += bank.size();
      taken++;
    }
    std::free(rel);
    PortDisc_Free(fsysData);
    if (taken < 4) Fail(1, "the European common_rel's text banks were not found");
  }
  if (languages.empty()) Fail(1, "no text tables of the other languages were found in the European disc's main program");
  std::sort(languages.begin(), languages.end());
  languages.erase(std::unique(languages.begin(), languages.end()), languages.end());
  std::vector<std::string> ordered;
  for (const auto& l : xd::languages::All()) {
    if (std::find(languages.begin(), languages.end(), l.id) != languages.end()) ordered.push_back(l.id);
  }
  {
    std::set<std::string> madeSet(madeNames.begin(), madeNames.end());
    std::vector<Group> merged = usGroups;
    for (const Group& g : palGroups) {
      if (usIds.count(g.id) == 0 && madeSet.count(g.name + ".fsys")) merged.push_back(g);
    }
    for (const std::string& id : ordered) {
      std::vector<Group> list = merged;
      int renamed = 0;
      for (Group& g : list) {
        const std::string target = g.name + "_" + id;
        const auto it = madeEntries.find(target + ".fsys");
        if (BaseName(g.name + ".fsys").empty() && it != madeEntries.end() && it->second > 0) {
          g.name = target;
          renamed++;
        }
      }
      const std::vector<uint8_t> toc = WriteToc(usHeader, list);
      if (!WriteFile(outTmp / ("lang_" + id) / "files" / "GSfsys.toc", toc.data(), toc.size())) Fail(1, "could not write the archive list");
      bytes += toc.size();
      std::fprintf(stderr, "language %s: %d archive names changed\n", id.c_str(), renamed);
    }
  }
  std::string names;
  for (const std::string& id : ordered) names += (names.empty() ? "" : ", ") + std::string(xd::languages::Find(id)->name);
  const std::string json = xd::languages::PackJson(ordered, made, xd::languages::kPalGameId);
  WriteFile(outTmp / xd::languages::kPackFile, json.data(), json.size());
  const std::string mod = std::string("{\n  \"id\": \"") + xd::languages::kManifestId +
                          "\",\n  \"name\": \"Languages\",\n  \"version\": \"" + kVersion +
                          "\",\n  \"description\": \"The other languages of the European disc.\",\n  \"game\": [\"GXXE01\"],\n"
                          "  \"count\": " + std::to_string(ordered.size()) + ",\n  \"unit\": \"language\",\n  \"units\": \"languages\",\n"
                          "  \"detail\": \"" + names + "\"\n}\n";
  WriteFile(outTmp / "mod.json", mod.data(), mod.size());

  fs::remove_all(outFinal, ec);
  fs::rename(outTmp, outFinal, ec);
  if (ec) Fail(1, "could not put the finished pack in place: " + ec.message());
  std::printf("done %d %d %llu\n", made, kept, (unsigned long long)bytes);
  std::fflush(stdout);
  return 0;
}
