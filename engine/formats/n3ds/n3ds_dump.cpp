#include "n3ds_dump.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace n3ds {

static uint32_t u32(const uint8_t* p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint64_t u64(const uint8_t* p) { return (uint64_t)u32(p) | (uint64_t)u32(p + 4) << 32; }
static uint16_t u16(const uint8_t* p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t be32(const uint8_t* p) { return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }
static uint16_t be16(const uint8_t* p) { return (uint16_t)(p[0] << 8 | p[1]); }
static uint64_t be64(const uint8_t* p) { return (uint64_t)be32(p) << 32 | be32(p + 4); }

#ifdef _WIN32
#define SEEK64 _fseeki64
#else
#define SEEK64 fseeko
#endif

namespace {
struct Handles {
  std::map<std::string, FILE*> h;
  ~Handles() {
    for (auto& kv : h) fclose(kv.second);
  }
};
thread_local Handles g_handles;
}

ByteFile::ByteFile(const std::string& path) : path_(path) {
  FILE* f = fopen(path.c_str(), "rb");
  if (!f) throw Fail("cannot open " + path, 2);
  SEEK64(f, 0, SEEK_END);
#ifdef _WIN32
  size_ = (uint64_t)_ftelli64(f);
#else
  size_ = (uint64_t)ftello(f);
#endif
  g_handles.h[path] = f;
}

Bytes ByteFile::read(uint64_t off, uint64_t len) const {
  FILE*& f = g_handles.h[path_];
  if (!f) {
    f = fopen(path_.c_str(), "rb");
    if (!f) throw Fail("cannot open " + path_, 2);
  }
  if (off >= size_) return Bytes();
  if (off + len > size_) len = size_ - off;
  Bytes b(len);
  SEEK64(f, (long long)off, SEEK_SET);
  size_t got = fread(b.data(), 1, len, f);
  b.resize(got);
  return b;
}

static uint64_t align_up(uint64_t n, uint64_t a = 0x40) { return (n + a - 1) & ~(a - 1); }

static const char* ENCRYPTED_CIA =
    "this .cia is encrypted. The converter only reads decrypted copies and does not decrypt anything itself: "
    "re-export the game decrypted with your own dumping tool.";
static const char* SHORT_CIA = "this .cia looks cut short or damaged: the game data is not where its header says.";

static uint64_t cia_content0(const ByteFile& f) {
  Bytes h = f.read(0, 0x2020);
  if (h.size() < 0x2020) throw Fail(SHORT_CIA, 2);
  uint32_t hdr = u32(&h[0]);
  uint16_t typ = u16(&h[4]);
  uint32_t cert = u32(&h[8]), tik = u32(&h[12]), tmd = u32(&h[16]), meta = u32(&h[20]);
  if (hdr != 0x2020 || typ != 0) throw Fail("this is not a 3DS game file the converter can read (a .3ds, .cci or decrypted .cia).", 2);
  uint64_t tmd_off = align_up(hdr) + align_up(cert) + align_up(tik);
  uint64_t cont_off = tmd_off + align_up(tmd) + align_up(meta);
  Bytes t = f.read(tmd_off, std::min<uint32_t>(tmd, 0x10000));
  if (t.size() < 4 || tmd < 0x100) throw Fail(SHORT_CIA, 2);
  uint32_t sigtype = be32(&t[0]);
  uint32_t sig = 0;
  switch (sigtype) {
    case 0x10000: case 0x10003: sig = 0x240; break;
    case 0x10001: case 0x10004: sig = 0x140; break;
    case 0x10002: case 0x10005: sig = 0x80; break;
    default: throw Fail(SHORT_CIA, 2);
  }
  if (t.size() < sig + 0xC4 + 0x900) throw Fail(SHORT_CIA, 2);
  uint64_t tid = be64(&t[sig + 0x4C]);
  uint32_t hi = (uint32_t)(tid >> 32);
  if (hi == 0x0004000E) throw Fail("this .cia is the game's update, not the game itself. Choose the game's own .cia.", 2);
  if (hi == 0x0004008C) throw Fail("this .cia is the game's downloadable content (DLC), not the game itself. Choose the game's own .cia.", 2);
  if (hi != 0x00040000) {
    char tb[96];
    snprintf(tb, sizeof tb, "this .cia is not a game (its title id is %016llX).", (unsigned long long)tid);
    throw Fail(tb, 2);
  }
  uint32_t count = be16(&t[sig + 0x9E]);
  uint32_t rec = sig + 0xC4 + 0x900;
  if (count == 0 || t.size() < rec + (size_t)count * 0x30) throw Fail(SHORT_CIA, 2);
  uint64_t off = cont_off;
  for (uint32_t i = 0; i < count; i++) {
    const uint8_t* e = &t[rec + i * 0x30];
    uint16_t cidx = be16(e + 4), ctype = be16(e + 6);
    uint64_t csz = be64(e + 8);
    if (!(h[0x20 + (cidx >> 3)] & (0x80 >> (cidx & 7)))) continue;
    if (cidx == 0) {
      if (ctype & 1) throw Fail(ENCRYPTED_CIA, 2);
      if (off + 0x200 > f.size()) throw Fail(SHORT_CIA, 2);
      return off;
    }
    off += csz;
  }
  throw Fail(SHORT_CIA, 2);
}

uint64_t find_ncch(const ByteFile& f, int part) {
  Bytes h = f.read(0, 0x200);
  if (h.size() >= 0x200 && !memcmp(&h[0x100], "NCSD", 4)) return (uint64_t)u32(&h[0x120 + part * 8]) * 0x200;
  if (h.size() >= 8 && u32(&h[0]) == 0x2020) return cia_content0(f);
  throw Fail("The file is not a decrypted 3DS cartridge dump the converter can read (not NCSD).", 2);
}

RomFs::RomFs(const ByteFile& f, uint64_t base, uint64_t) : file(f) {
  Bytes n = f.read(base, 0x200);
  if (n.size() < 0x200 || memcmp(&n[0x100], "NCCH", 4)) throw Fail("The file is not a decrypted 3DS cartridge dump the converter can read (not NCCH).", 2);
  if (!(n[0x188 + 7] & 4)) throw Fail("this dump is encrypted; only decrypted dumps are supported (the tool does not decrypt)", 2);
  uint32_t ro = u32(&n[0x1B0]);
  size_t pl = 0;
  while (pl < 16 && n[0x150 + pl]) pl++;
  product.assign((const char*)&n[0x150], pl);
  uint64_t rbase = base + (uint64_t)ro * 0x200;
  Bytes h = f.read(rbase, 0x60);
  if (h.size() < 0x60 || memcmp(h.data(), "IVFC", 4)) throw Fail("not IVFC", 2);
  uint32_t mh = u32(&h[8]);
  uint64_t start = (0x60 + (uint64_t)mh + 0xFFF) & ~(uint64_t)0xFFF;
  l3_ = rbase + start;
  Bytes hd = f.read(l3_, 0x28);
  uint32_t v[10];
  for (int i = 0; i < 10; i++) v[i] = u32(&hd[i * 4]);
  uint32_t dmo = v[3], dms = v[4], fmo = v[7], fms = v[8];
  fdo_ = v[9];
  Bytes dmeta = f.read(l3_ + dmo, dms), fmeta = f.read(l3_ + fmo, fms);
  walk(dmeta, fmeta, 0, "");
}

static std::string utf16_ascii(const Bytes& b, size_t off, size_t nchars) {
  std::string s;
  for (size_t i = 0; i < nchars && off + i * 2 + 1 < b.size(); i++) {
    uint32_t c = u16(&b[off + i * 2]);
    if (c < 0x80) s.push_back((char)c);
    else if (c < 0x800) { s.push_back((char)(0xC0 | c >> 6)); s.push_back((char)(0x80 | (c & 63))); }
    else { s.push_back((char)(0xE0 | c >> 12)); s.push_back((char)(0x80 | ((c >> 6) & 63))); s.push_back((char)(0x80 | (c & 63))); }
  }
  return s;
}

void RomFs::walk(const Bytes& d, const Bytes& fm, uint32_t doff, const std::string& path) {
  uint32_t child = u32(&d[doff + 8]), fchild = u32(&d[doff + 12]), nl = u32(&d[doff + 20]);
  std::string name = utf16_ascii(d, doff + 0x18, nl / 2);
  std::string p = path + (name.empty() ? "" : name + "/");
  uint32_t fo = fchild;
  while (fo != 0xFFFFFFFFu) {
    uint32_t sb = u32(&fm[fo + 4]);
    uint64_t fdo = u64(&fm[fo + 8]), fds = u64(&fm[fo + 16]);
    uint32_t nl2 = u32(&fm[fo + 28]);
    files[p + utf16_ascii(fm, fo + 0x20, nl2 / 2)] = {fdo, fds};
    fo = sb;
  }
  uint32_t co = child;
  while (co != 0xFFFFFFFFu) {
    walk(d, fm, co, p);
    co = u32(&d[co + 4]);
  }
}

std::pair<uint64_t, uint64_t> RomFs::where(const std::string& path) const {
  auto it = files.find(path);
  if (it == files.end()) throw Fail("no such file in the RomFS: " + path);
  return {l3_ + fdo_ + it->second.first, it->second.second};
}

Bytes RomFs::read(const std::string& path) const {
  auto w = where(path);
  return file.read(w.first, w.second);
}

RomFs open_dump(const std::string& path, int part) {
  ByteFile f(path);
  return RomFs(f, find_ncch(f, part), 0);
}

Bytes lz11(const uint8_t* b, size_t n, uint64_t limit) {
  if (n < 4 || b[0] != 0x11) throw Fail("not LZ11");
  uint64_t size = b[1] | b[2] << 8 | b[3] << 16;
  size_t p = 4;
  if (size == 0) {
    if (n < 8) throw Fail("LZ11 short");
    size = u32(b + 4);
    p = 8;
  }
  uint64_t want = (limit == 0 || limit > size) ? size : limit;
  Bytes out;
  out.reserve(want + 0x1000);
  auto need = [&](size_t k) { if (p + k > n) throw Fail("LZ11 ran past its input"); };
  while (out.size() < want) {
    need(1);
    uint8_t flags = b[p++];
    for (int bit = 7; bit >= 0; bit--) {
      if (out.size() >= want) break;
      if (!(flags >> bit & 1)) {
        need(1);
        out.push_back(b[p++]);
      } else {
        need(1);
        uint8_t b0 = b[p];
        int ind = b0 >> 4;
        size_t ln, disp;
        if (ind == 0) {
          need(3);
          uint8_t b1 = b[p + 1], b2 = b[p + 2];
          p += 3;
          ln = (((b0 & 15) << 4) | (b1 >> 4)) + 0x11;
          disp = (((b1 & 15) << 8) | b2) + 1;
        } else if (ind == 1) {
          need(4);
          uint8_t b1 = b[p + 1], b2 = b[p + 2], b3 = b[p + 3];
          p += 4;
          ln = (((b0 & 15) << 12) | (b1 << 4) | (b2 >> 4)) + 0x111;
          disp = (((b2 & 15) << 8) | b3) + 1;
        } else {
          need(2);
          uint8_t b1 = b[p + 1];
          p += 2;
          ln = ind + 1;
          disp = (((b0 & 15) << 8) | b1) + 1;
        }
        size_t cur = out.size();
        if (disp > cur) throw Fail("LZ11 back reference before the start");
        out.resize(cur + ln);
        for (size_t i = 0; i < ln; i++) out[cur + i] = out[cur + i - disp];
      }
    }
  }
  out.resize(want);
  return out;
}

Bytes lz11_maybe(const Bytes& b) {
  if (!b.empty() && b[0] == 0x11) {
    try {
      return lz11(b.data(), b.size());
    } catch (const Fail&) {
      return b;
    }
  }
  return b;
}

Garc::Garc(const ByteFile& f, uint64_t off, uint64_t) : f_(f), off_(off) {
  Bytes h = f.read(off, 0x1C);
  if (h.size() < 0x1C || memcmp(h.data(), "CRAG", 4)) throw Fail("not GARC");
  uint32_t hl = u32(&h[4]);
  data_off_ = u32(&h[0x10]);
  Bytes o = f.read(off + hl, 0x10);
  if (o.size() < 0x10 || memcmp(o.data(), "OTAF", 4)) throw Fail("no OTAF");
  uint32_t osz = u32(&o[4]);
  Bytes b = f.read(off + hl + osz, 12);
  if (b.size() < 12 || memcmp(b.data(), "BTAF", 4)) throw Fail("no BTAF");
  uint32_t bsz = u32(&b[4]), n2 = u32(&b[8]);
  Bytes body = f.read(off + hl + osz + 12, bsz - 12);
  size_t p = 0;
  entries.reserve(n2);
  for (uint32_t i = 0; i < n2; i++) {
    uint32_t mask = u32(&body[p]);
    p += 4;
    std::vector<std::pair<int64_t, int64_t>> subs(32, {0, -1});
    for (int bit = 0; bit < 32; bit++) {
      if (mask >> bit & 1) {
        subs[bit] = {u32(&body[p]), u32(&body[p + 8])};
        p += 12;
      }
    }
    entries.push_back(std::move(subs));
  }
}

Bytes Garc::read(size_t i, size_t sub) const {
  auto s = entries.at(i).at(sub);
  if (s.second < 0) throw Fail("empty GARC slot");
  return f_.read(off_ + data_off_ + (uint64_t)s.first, (uint64_t)s.second);
}

size_t Garc::nsub(size_t i) const {
  size_t n = 0;
  for (auto& s : entries.at(i)) n += s.second >= 0;
  return n;
}

Garc garc_at(const RomFs& r, const std::string& path) {
  auto w = r.where(path);
  return Garc(r.file, w.first, w.second);
}

std::vector<Bytes> pc_split(const Bytes& b, bool* ok) {
  std::vector<Bytes> out;
  if (b.size() < 4 || b[0] != 'P' || b[1] != 'C') {
    if (ok) *ok = false;
    return out;
  }
  size_t n = u16(&b[2]);
  if (b.size() < 4 + (n + 1) * 4) {
    if (ok) *ok = false;
    return out;
  }
  for (size_t i = 0; i < n; i++) {
    uint32_t a = u32(&b[4 + i * 4]), e = u32(&b[8 + i * 4]);
    if (a > e || e > b.size()) {
      if (ok) *ok = false;
      return std::vector<Bytes>();
    }
    out.emplace_back(b.begin() + a, b.begin() + e);
  }
  if (ok) *ok = true;
  return out;
}

static bool decode_species_text(const Bytes& d, std::vector<std::string>& out) {
  if (d.size() < 16) return false;
  uint16_t nlines = u16(&d[2]);
  uint32_t off = u32(&d[12]);
  for (uint32_t i = 0; i < nlines; i++) {
    if (off + 4 + i * 8 + 6 > d.size()) return false;
    int32_t o = (int32_t)u32(&d[off + 4 + i * 8]);
    uint16_t ln = u16(&d[off + 4 + i * 8 + 4]);
    uint16_t key = (0x7C89 + 0x2983 * i) & 0xffff;
    std::string s;
    for (uint32_t k = 0; k < ln; k++) {
      size_t at = (size_t)off + o + k * 2;
      if (at + 2 > d.size()) return false;
      uint16_t c = u16(&d[at]) ^ key;
      key = (uint16_t)(((key << 3) | (key >> 13)) & 0xffff);
      if (c && c < 0x80) s.push_back((char)c);
    }
    out.push_back(s);
  }
  return true;
}

Detected detect(const RomFs& r) {
  static const uint8_t PIKACHU[6] = {35, 55, 40, 90, 50, 50};
  Detected det;
  std::vector<std::string> cands;
  for (auto& kv : r.files) {
    const std::string& p = kv.first;
    if (p.size() > 2 && p[0] == 'a' && p[1] == '/' && std::count(p.begin(), p.end(), '/') == 3) cands.push_back(p);
  }
  for (auto& p : cands) {
    uint64_t size = r.files.at(p).second;
    try {
      if (size > 400000000ull && det.models.empty()) {
        Garc g = garc_at(r, p);
        if (g.count() > 5000 && (g.count() - 1) % 9 == 0) det.models = p;
      } else if (size < 2000000 && det.personal.empty()) {
        Garc g = garc_at(r, p);
        if (g.count() >= 700 && g.count() <= 1300) {
          Bytes d = lz11_maybe(g.read(25));
          if (d.size() == 84 && !memcmp(d.data(), PIKACHU, 6)) det.personal = p;
        }
      }
    } catch (const std::exception&) {
    }
  }
  if (!det.personal.empty()) {
    size_t n = garc_at(r, det.personal).count();
    for (auto& p : cands) {
      if (p == det.personal || r.files.at(p).second > 8000000) continue;
      try {
        Garc g = garc_at(r, p);
        size_t m = g.count();
        if ((m > n ? m - n : n - m) <= 8) {
          Bytes d = lz11_maybe(g.read(25));
          bool found = false;
          for (size_t i = 0; i + 4 <= d.size(); i += 4)
            if (u16(&d[i]) == 84 && u16(&d[i + 2]) == 1) found = true;
          if (found && d.size() < 400) {
            det.levelup = p;
            break;
          }
        }
      } catch (const std::exception&) {
      }
    }
  }
  for (auto& p : cands) {
    if (p.compare(0, 6, "a/0/3/") != 0) continue;
    try {
      Garc g = garc_at(r, p);
      for (size_t e : {60, 61, 59, 62}) {
        if (e < g.count()) {
          std::vector<std::string> t;
          if (decode_species_text(lz11_maybe(g.read(e)), t) && t.size() > 800 && t[25] == "Pikachu") {
            det.text = p;
            break;
          }
        }
      }
      if (!det.text.empty()) break;
    } catch (const std::exception&) {
    }
  }
  std::string missing;
  for (auto& kv : {std::make_pair("models", &det.models), std::make_pair("personal", &det.personal), std::make_pair("levelup", &det.levelup),
                   std::make_pair("text", &det.text)})
    if (kv.second->empty()) missing += (missing.empty() ? "" : ", ") + std::string(kv.first);
  if (!missing.empty())
    throw Fail("3DS dump: could not find " + missing + " (is it a decrypted Pokemon Sun / Moon / Ultra Sun / Ultra Moon image?)", 2);
  return det;
}

}
