#include "pbr_pack.h"

namespace pbr {

namespace {
uint16_t be16(const uint8_t* p) { return (uint16_t)((p[0] << 8) | p[1]); }
uint32_t be32(const uint8_t* p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }

void put_utf8(std::string& s, unsigned c) {
  if (c < 0x80) {
    s += (char)c;
  } else if (c < 0x800) {
    s += (char)(0xC0 | (c >> 6));
    s += (char)(0x80 | (c & 0x3F));
  } else {
    s += (char)(0xE0 | (c >> 12));
    s += (char)(0x80 | ((c >> 6) & 0x3F));
    s += (char)(0x80 | (c & 0x3F));
  }
}

std::vector<std::string> parse_mesg(const std::vector<uint8_t>& d) {
  if (d.size() < 0x10 || d[0] != 'M' || d[1] != 'E' || d[2] != 'S' || d[3] != 'G') throw Fail("mes_common: not a MESG file");
  const uint32_t n = be32(&d[4]);
  std::vector<uint32_t> offs(n);
  for (uint32_t i = 0; i < n; i++) offs[i] = be32(&d[0x10 + 4 * i]);
  std::vector<std::string> out;
  for (uint32_t i = 0; i < n; i++) {
    const uint32_t o = offs[i], e = i + 1 < n ? offs[i + 1] : (uint32_t)d.size();
    std::string s;
    for (uint32_t k = o; k + 1 < e; k += 2) {
      const unsigned c = be16(&d[k]);
      if (c == 0) break;
      if (c >= 0x20 && c < 0xD800) put_utf8(s, c);
    }
    out.push_back(s);
  }
  return out;
}
}

PbrData::PbrData(const DiscSource& pbr) {
  const std::vector<uint8_t> common = pbr.read("common.fsys");
  for (const FsysEntry& e : fsys_entries(common)) {
    switch (e.index) {
      case 8: common8 = fsys_extract(common, e); break;
      case 9: common9 = fsys_extract(common, e); break;
      case 29: common29 = fsys_extract(common, e); break;
      case 30: common30 = fsys_extract(common, e); break;
      case 32: common32 = fsys_extract(common, e); break;
      default: break;
    }
  }
  const std::vector<uint8_t> mes = pbr.read("mes_common.fsys");
  bool found = false;
  for (const FsysEntry& e : fsys_entries(mes)) {
    if (e.index == 1) {
      text = parse_mesg(fsys_extract(mes, e));
      found = true;
      break;
    }
  }
  if (!found || common8.empty() || common9.empty() || common29.empty() || common30.empty() || common32.empty())
    throw Fail("The Battle Revolution disc's tables (common.fsys, mes_common.fsys) are not as expected.", 2);
  for (int i = 0; i < 100; i++) tmlist.push_back(be16(&common29[0x30 + 4 * i]));
}

PbrPersonal PbrData::personal(int dex) const {
  const uint8_t* r = &common8[0x30 + (size_t)dex * 52];
  PbrPersonal p;
  uint32_t w[4];
  for (int k = 0; k < 4; k++) w[k] = be32(r + 4 * k);
  const int ev = be16(r + 40);
  const int evs[6] = {(ev >> 14) & 3, (ev >> 12) & 3, (ev >> 10) & 3, (ev >> 8) & 3, (ev >> 6) & 3, (ev >> 4) & 3};
  p.hp = r[30]; p.atk = r[31]; p.de = r[32]; p.spe = r[33]; p.spa = r[34]; p.spd = r[35];
  p.t1 = r[36]; p.t2 = r[37]; p.catch_rate = r[38]; p.exp = r[39];
  p.ev_hp = evs[0]; p.ev_atk = evs[1]; p.ev_def = evs[2]; p.ev_spe = evs[3]; p.ev_spa = evs[4]; p.ev_spd = evs[5];
  p.gender = r[42]; p.hatch = r[43]; p.friendship = r[44]; p.growth = r[45];
  p.egg[0] = r[46]; p.egg[1] = r[47]; p.ab[0] = r[48]; p.ab[1] = r[49];
  p.items[0] = be16(r + 16); p.items[1] = be16(r + 18);
  p.height = be16(r + 20); p.weight = be16(r + 22);
  for (int k = 0; k < 100; k++)
    if ((w[k / 32] >> (k % 32)) & 1) p.tm.push_back(k);
  return p;
}

std::vector<std::pair<int, int>> PbrData::learnset(int dex) const {
  const uint8_t* r = &common32[0x30 + (size_t)dex * 80];
  std::vector<std::pair<int, int>> out;
  for (int k = 0; k < 20; k++) {
    const int mv = be16(r + 4 * k), lv = r[4 * k + 2], pad = r[4 * k + 3];
    if (mv == 0 && lv == 0 && pad == 0xd0) break;
    out.emplace_back(lv, mv);
  }
  return out;
}

std::vector<PbrEvo> PbrData::evolutions(int dex) const {
  const uint8_t* r = &common9[0x30 + (size_t)dex * 42];
  std::vector<PbrEvo> out;
  for (int k = 0; k < 7; k++) {
    const int m = be16(r + 6 * k), p = be16(r + 6 * k + 2), s = be16(r + 6 * k + 4);
    if (m) out.push_back({m, p, s});
  }
  return out;
}

}
