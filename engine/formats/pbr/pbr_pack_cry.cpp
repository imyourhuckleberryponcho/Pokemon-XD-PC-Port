#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <map>

#include "pbr_pack.h"

namespace pbr {
namespace {

constexpr uint32_t kBlock = 0x8000;
constexpr uint32_t kMaxBlocks = 2;

uint32_t be32(const std::vector<uint8_t>& d, size_t o) {
  if (o + 4 > d.size()) throw Fail("cry: file too short");
  return ((uint32_t)d[o] << 24) | ((uint32_t)d[o + 1] << 16) | ((uint32_t)d[o + 2] << 8) | d[o + 3];
}
uint32_t be16(const std::vector<uint8_t>& d, size_t o) {
  if (o + 2 > d.size()) throw Fail("cry: file too short");
  return ((uint32_t)d[o] << 8) | d[o + 1];
}
void put32(std::vector<uint8_t>& d, size_t o, uint32_t v) {
  d[o] = (uint8_t)(v >> 24); d[o + 1] = (uint8_t)(v >> 16); d[o + 2] = (uint8_t)(v >> 8); d[o + 3] = (uint8_t)v;
}
void put16(std::vector<uint8_t>& d, size_t o, uint32_t v) {
  d[o] = (uint8_t)(v >> 8); d[o + 1] = (uint8_t)v;
}

struct Chan {
  uint16_t coefs[16];
};
struct Info {
  uint32_t rate = 0, nsamp = 0;
  std::vector<Chan> chans;
  std::vector<std::vector<uint8_t>> data;
};

Info parse_brstm(const std::vector<uint8_t>& d) {
  if (d.size() < 4 || std::memcmp(d.data(), "RSTM", 4) != 0) throw Fail("cry: not an RSTM file");
  const uint32_t head = be32(d, 0x10), data = be32(d, 0x20);
  const size_t base = head + 8;
  if (std::memcmp(&d.at(head), "HEAD", 4) != 0 || std::memcmp(&d.at(data), "DATA", 4) != 0) throw Fail("cry: bad RSTM");
  const uint32_t ref0 = be32(d, base + 4), ref2 = be32(d, base + 16 + 4);
  const size_t si = base + ref0;
  const int codec = d.at(si);
  Info in;
  in.rate = be16(d, si + 4);
  in.nsamp = be32(d, si + 12);
  const uint32_t dataoff = be32(d, si + 16), nblk = be32(d, si + 20), blksz = be32(d, si + 24), lastsz = be32(d, si + 32);
  if (codec != 2) throw Fail("cry: not DSP-ADPCM");
  const size_t ci = base + ref2;
  const int nc = d.at(ci);
  for (int i = 0; i < nc; i++) {
    const uint32_t cref = be32(d, ci + 4 + 8 * i + 4);
    const uint32_t ap = be32(d, base + cref + 4);
    const size_t q = base + ap;
    Chan c;
    for (int k = 0; k < 16; k++) c.coefs[k] = (uint16_t)be16(d, q + 2 * k);
    in.chans.push_back(c);
  }
  size_t pos = dataoff;
  std::vector<std::vector<uint8_t>> streams(nc);
  for (uint32_t b = 0; b < nblk; b++) {
    const bool last = b == nblk - 1;
    const uint32_t sz = last ? lastsz : blksz;
    const uint32_t pad = last ? ((sz + 0x1F) & ~0x1Fu) : sz;
    for (int c = 0; c < nc; c++) {
      const size_t a = std::min(pos, d.size()), e = std::min(pos + sz, d.size());
      streams[c].insert(streams[c].end(), d.begin() + a, d.begin() + e);
      pos += pad;
    }
  }
  const size_t need = (size_t)((in.nsamp + 13) / 14) * 8;
  for (auto& s : streams) {
    if (s.size() > need) s.resize(need);
    in.data.push_back(std::move(s));
  }
  return in;
}

void build_xd(const Info& in, std::vector<uint8_t>& hdr, std::vector<uint8_t>& body) {
  const size_t nch = in.data.size();
  uint32_t nsamp = in.nsamp;
  std::vector<std::vector<uint8_t>> data = in.data;
  const size_t cap = (size_t)kMaxBlocks * kBlock;
  if (data.at(0).size() > cap) {
    for (auto& x : data) x.resize(cap);
    nsamp = (uint32_t)(cap / 8 * 14);
  }
  const uint32_t last = nsamp - 1;
  const uint32_t end = last / 14 * 16 + 2 + last % 14;
  hdr.assign(192, 0);
  std::memcpy(hdr.data(), "I_SF", 4);
  const uint32_t w[9] = {0x20040109, in.rate, nsamp, end + 1, (uint32_t)nch, kBlock, 0, 2, end};
  for (int k = 0; k < 9; k++) put32(hdr, 4 + 4 * k, w[k]);
  put32(hdr, 0x28, end);
  for (size_t c = 0; c < in.chans.size(); c++) {
    const size_t o = 0x40 + 0x40 * c;
    for (int k = 0; k < 16; k++) put16(hdr, o + 2 * k, in.chans[c].coefs[k]);
    const uint32_t ps = data.at(c).at(0);
    const uint32_t v[7] = {0, ps, 0, 0, ps, 0, 0};
    for (int k = 0; k < 7; k++) put16(hdr, o + 32 + 2 * k, v[k]);
  }
  const size_t nb = (data[0].size() + kBlock - 1) / kBlock;
  body.clear();
  for (size_t j = 0; j < nb; j++) {
    for (size_t c = 0; c < nch; c++) {
      const size_t a = std::min(j * kBlock, data[c].size()), e = std::min((j + 1) * kBlock, data[c].size());
      body.insert(body.end(), data[c].begin() + a, data[c].begin() + e);
      body.insert(body.end(), kBlock - (e - a), 0);
    }
  }
}

struct XdCry {
  uint32_t rate = 0, nsamp = 0;
  int16_t coefs[16] = {};
  std::vector<uint8_t> data;
};
XdCry parse_xd(const std::vector<uint8_t>& hdr, const std::vector<uint8_t>& body) {
  if (hdr.size() != 192 || std::memcmp(hdr.data(), "I_SF", 4) != 0) throw Fail("cry: not a converted header");
  XdCry x;
  x.rate = be32(hdr, 8);
  x.nsamp = be32(hdr, 12);
  const uint32_t nch = be32(hdr, 20);
  for (int k = 0; k < 16; k++) x.coefs[k] = (int16_t)be16(hdr, 0x40 + 2 * k);
  const size_t need = (size_t)((x.nsamp + 13) / 14) * 8;
  const size_t nb = (need + kBlock - 1) / kBlock;
  for (size_t j = 0; j < nb; j++) {
    const size_t o = (j * nch + 0) * kBlock;
    for (size_t k = 0; k < kBlock && o + k < body.size(); k++) x.data.push_back(body[o + k]);
  }
  if (x.data.size() > need) x.data.resize(need);
  return x;
}

}

std::vector<uint8_t> gen4_wave(const CryOut& cry) {
  constexpr int64_t kPerSecond = 30, kMin = 16, kMax = 60;
  const XdCry x = parse_xd(cry.hdr, cry.body);
  std::vector<int> pcm;
  int64_t h1 = 0, h2 = 0;
  for (size_t f = 0; f + 7 < x.data.size() + 0 && (f + 8) <= x.data.size(); f += 8) {
    const int hdr = x.data[f];
    const int64_t sc = (int64_t)1 << (hdr & 15);
    const int64_t c1 = x.coefs[(hdr >> 4) * 2], c2 = x.coefs[(hdr >> 4) * 2 + 1];
    for (int k = 0; k < 14; k++) {
      if (pcm.size() >= x.nsamp) goto decoded;
      const int b = x.data[f + 1 + k / 2];
      int nib = (k % 2 == 0) ? (b >> 4) : (b & 15);
      if (nib >= 8) nib -= 16;
      int64_t v = (((nib * sc) << 11) + 1024 + c1 * h1 + c2 * h2) >> 11;
      v = std::max<int64_t>(-32768, std::min<int64_t>(32767, v));
      pcm.push_back((int)v);
      h2 = h1;
      h1 = v;
    }
  }
decoded:
  const int64_t nsamp = (int64_t)pcm.size(), rate = x.rate;
  const int64_t n = std::max(kMin, std::min(kMax, (nsamp * kPerSecond * 2 + rate) / (2 * rate)));
  std::vector<int> picks;
  int peak = 0;
  for (int64_t k = 0; k < n; k++) {
    const int64_t a = nsamp * k / n, b = nsamp * (k + 1) / n;
    int best = 0;
    for (int64_t i = a; i < b; i++)
      if (std::abs(pcm[(size_t)i]) > std::abs(best)) best = pcm[(size_t)i];
    picks.push_back(best);
    peak = std::max(peak, std::abs(best));
  }
  if (peak == 0) peak = 1;
  std::vector<uint8_t> out;
  out.push_back((uint8_t)n);
  for (int v : picks) {
    const int m = std::abs(v) * 127 / peak;
    out.push_back((uint8_t)((v < 0 ? -m : m) & 255));
  }
  return out;
}

std::vector<CryOut> gen4_cries(const DiscSource& pbr) {
  std::map<int, std::string> files;
  for (const auto& f : pbr.files()) {
    const std::string& k = f.first;
    if (k.rfind("sound/sound/s", 0) == 0 && k.size() > 6 && k.compare(k.size() - 6, 6, ".brstm") == 0) {
      const std::string num = k.substr(k.rfind('/') + 2, 3);
      files[std::atoi(num.c_str())] = k;
    }
  }
  std::vector<CryOut> out;
  for (int dex = 387; dex <= 493; dex++) {
    auto it = files.find(dex);
    if (it == files.end()) throw Fail("The Battle Revolution disc has no cry for Pokemon " + std::to_string(dex) + ".", 2);
    CryOut c;
    c.dex = dex;
    build_xd(parse_brstm(pbr.read(it->second)), c.hdr, c.body);
    out.push_back(std::move(c));
  }
  return out;
}

}
