#include "room_mesh.h"

#include "json.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <map>
#include <set>

namespace engine::rooms {

namespace json = engine::mods::json;

namespace {

struct BitReader {
  const uint8_t* p;
  size_t n, pos = 0;
  uint32_t bits = 0;
  int count = 0;
  bool bad = false;
  uint32_t Get(int k) {
    while (count < k) {
      if (pos >= n) { bad = true; return 0; }
      bits |= uint32_t(p[pos++]) << count;
      count += 8;
    }
    const uint32_t v = k == 0 ? 0 : (bits & ((1u << k) - 1));
    bits >>= k, count -= k;
    return v;
  }
};

struct Huff {
  uint16_t counts[16] = {};
  uint16_t symbols[320] = {};
  void Build(const uint8_t* lengths, int n) {
    std::memset(counts, 0, sizeof counts);
    for (int i = 0; i < n; i++) counts[lengths[i]]++;
    counts[0] = 0;
    uint16_t offs[16] = {};
    for (int i = 1; i < 16; i++) offs[i] = uint16_t(offs[i - 1] + counts[i - 1]);
    for (int i = 0; i < n; i++) {
      if (lengths[i] != 0) symbols[offs[lengths[i]]++] = uint16_t(i);
    }
  }
  int Decode(BitReader& br) const {
    int code = 0, first = 0, index = 0;
    for (int len = 1; len < 16; len++) {
      code |= int(br.Get(1));
      const int c = counts[len];
      if (code - c < first) return symbols[index + (code - first)];
      index += c, first += c, first <<= 1, code <<= 1;
      if (br.bad) return -1;
    }
    return -1;
  }
};

bool Inflate(const uint8_t* src, size_t n, std::vector<uint8_t>& out, std::string& why) {
  static const uint16_t lbase[] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
  static const uint16_t lext[] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
  static const uint16_t dbase[] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
  static const uint16_t dext[] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
  BitReader br{src, n};
  bool last = false;
  while (!last) {
    last = br.Get(1) != 0;
    const uint32_t type = br.Get(2);
    if (br.bad) { why = "the picture's data ends too early"; return false; }
    if (type == 0) {
      br.bits = 0, br.count = 0;
      if (br.pos + 4 > n) { why = "the picture's data ends too early"; return false; }
      const uint32_t len = src[br.pos] | (uint32_t(src[br.pos + 1]) << 8);
      br.pos += 4;
      if (br.pos + len > n) { why = "the picture's data ends too early"; return false; }
      out.insert(out.end(), src + br.pos, src + br.pos + len);
      br.pos += len;
      continue;
    }
    if (type == 3) { why = "the picture's data is damaged"; return false; }
    Huff lit, dist;
    if (type == 1) {
      uint8_t l[288];
      for (int i = 0; i < 144; i++) l[i] = 8;
      for (int i = 144; i < 256; i++) l[i] = 9;
      for (int i = 256; i < 280; i++) l[i] = 7;
      for (int i = 280; i < 288; i++) l[i] = 8;
      lit.Build(l, 288);
      uint8_t d[30];
      for (int i = 0; i < 30; i++) d[i] = 5;
      dist.Build(d, 30);
    } else {
      const int nlen = int(br.Get(5)) + 257, ndist = int(br.Get(5)) + 1, ncode = int(br.Get(4)) + 4;
      static const uint8_t order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
      uint8_t cl[19] = {};
      for (int i = 0; i < ncode; i++) cl[order[i]] = uint8_t(br.Get(3));
      Huff ch;
      ch.Build(cl, 19);
      uint8_t lengths[320] = {};
      int i = 0;
      while (i < nlen + ndist) {
        const int sym = ch.Decode(br);
        if (sym < 0 || br.bad) { why = "the picture's data is damaged"; return false; }
        if (sym < 16) lengths[i++] = uint8_t(sym);
        else {
          int rep, val = 0;
          if (sym == 16) {
            if (i == 0) { why = "the picture's data is damaged"; return false; }
            val = lengths[i - 1], rep = 3 + int(br.Get(2));
          } else if (sym == 17) rep = 3 + int(br.Get(3));
          else rep = 11 + int(br.Get(7));
          if (i + rep > nlen + ndist) { why = "the picture's data is damaged"; return false; }
          while (rep-- > 0) lengths[i++] = uint8_t(val);
        }
      }
      lit.Build(lengths, nlen);
      dist.Build(lengths + nlen, ndist);
    }
    for (;;) {
      const int sym = lit.Decode(br);
      if (sym < 0 || br.bad) { why = "the picture's data is damaged"; return false; }
      if (sym < 256) out.push_back(uint8_t(sym));
      else if (sym == 256) break;
      else {
        const int s = sym - 257;
        if (s >= 29) { why = "the picture's data is damaged"; return false; }
        const int len = lbase[s] + int(br.Get(lext[s]));
        const int ds = dist.Decode(br);
        if (ds < 0 || ds >= 30) { why = "the picture's data is damaged"; return false; }
        const size_t d = dbase[ds] + br.Get(dext[ds]);
        if (d > out.size()) { why = "the picture's data is damaged"; return false; }
        for (int k = 0; k < len; k++) out.push_back(out[out.size() - d]);
      }
      if (out.size() > (size_t(1) << 28)) { why = "the picture is too big"; return false; }
    }
  }
  return true;
}

uint32_t Be32(const uint8_t* p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; }

}

bool PngDecode(const uint8_t* b, size_t n, int& width, int& height, std::vector<uint8_t>& rgba, std::string& problem) {
  static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
  if (n < 33 || std::memcmp(b, sig, 8) != 0) { problem = "the picture is not a PNG (only PNG pictures are read)"; return false; }
  size_t pos = 8;
  int w = 0, h = 0, depth = 0, ctype = 0;
  std::vector<uint8_t> idat, palette, trns;
  while (pos + 12 <= n) {
    const uint32_t len = Be32(b + pos);
    const char* type = reinterpret_cast<const char*>(b + pos + 4);
    if (pos + 12 + size_t(len) > n) { problem = "the PNG is cut short"; return false; }
    const uint8_t* d = b + pos + 8;
    if (std::memcmp(type, "IHDR", 4) == 0 && len >= 13) {
      w = int(Be32(d)), h = int(Be32(d + 4)), depth = d[8], ctype = d[9];
      if (d[12] != 0) { problem = "interlaced PNGs are not read; save the picture without interlacing"; return false; }
    } else if (std::memcmp(type, "PLTE", 4) == 0) palette.assign(d, d + len);
    else if (std::memcmp(type, "tRNS", 4) == 0) trns.assign(d, d + len);
    else if (std::memcmp(type, "IDAT", 4) == 0) idat.insert(idat.end(), d, d + len);
    else if (std::memcmp(type, "IEND", 4) == 0) break;
    pos += 12 + len;
  }
  if (w <= 0 || h <= 0 || w > 8192 || h > 8192) { problem = "the PNG's size is not usable"; return false; }
  if (depth != 8 || (ctype != 0 && ctype != 2 && ctype != 3 && ctype != 4 && ctype != 6)) {
    problem = "the PNG must have 8 bits per colour (grey, RGB, palette, with or without alpha)";
    return false;
  }
  if (idat.size() < 6) { problem = "the PNG has no picture data"; return false; }
  std::vector<uint8_t> raw;
  if (!Inflate(idat.data() + 2, idat.size() - 2, raw, problem)) return false;
  const int bpp = ctype == 0 ? 1 : ctype == 2 ? 3 : ctype == 3 ? 1 : ctype == 4 ? 2 : 4;
  const size_t stride = size_t(w) * bpp;
  if (raw.size() < (stride + 1) * size_t(h)) { problem = "the PNG's data is shorter than its size says"; return false; }
  std::vector<uint8_t> img(stride * size_t(h));
  for (int y = 0; y < h; y++) {
    const uint8_t f = raw[(stride + 1) * y];
    const uint8_t* in = &raw[(stride + 1) * y + 1];
    uint8_t* out = &img[stride * y];
    const uint8_t* up = y > 0 ? &img[stride * (y - 1)] : nullptr;
    for (size_t x = 0; x < stride; x++) {
      const int a = x >= size_t(bpp) ? out[x - bpp] : 0, c = (x >= size_t(bpp) && up) ? up[x - bpp] : 0, u = up ? up[x] : 0;
      int v = in[x];
      switch (f) {
        case 1: v += a; break;
        case 2: v += u; break;
        case 3: v += (a + u) / 2; break;
        case 4: {
          const int p = a + u - c, pa = std::abs(p - a), pb = std::abs(p - u), pc = std::abs(p - c);
          v += (pa <= pb && pa <= pc) ? a : (pb <= pc ? u : c);
          break;
        }
        default: break;
      }
      out[x] = uint8_t(v);
    }
  }
  width = w, height = h;
  rgba.assign(size_t(w) * h * 4, 255);
  for (size_t i = 0; i < size_t(w) * h; i++) {
    const uint8_t* s = &img[i * bpp];
    uint8_t* o = &rgba[i * 4];
    switch (ctype) {
      case 0: o[0] = o[1] = o[2] = s[0]; break;
      case 2: o[0] = s[0], o[1] = s[1], o[2] = s[2]; break;
      case 3:
        if (size_t(s[0]) * 3 + 2 < palette.size()) o[0] = palette[s[0] * 3], o[1] = palette[s[0] * 3 + 1], o[2] = palette[s[0] * 3 + 2];
        if (size_t(s[0]) < trns.size()) o[3] = trns[s[0]];
        break;
      case 4: o[0] = o[1] = o[2] = s[0], o[3] = s[1]; break;
      default: o[0] = s[0], o[1] = s[1], o[2] = s[2], o[3] = s[3]; break;
    }
  }
  return true;
}

namespace {

using Mat4 = std::array<double, 16>;

Mat4 Ident() { return {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}; }
Mat4 Mul4(const Mat4& a, const Mat4& b) {
  Mat4 r{};
  for (int c = 0; c < 4; c++) {
    for (int rr = 0; rr < 4; rr++) {
      double s = 0;
      for (int k = 0; k < 4; k++) s += a[k * 4 + rr] * b[c * 4 + k];
      r[c * 4 + rr] = s;
    }
  }
  return r;
}

const json::Value* Get(const json::Value& v, const char* key) {
  if (v.kind != json::Value::Kind::Object) return nullptr;
  const auto it = v.fields.find(key);
  return it == v.fields.end() ? nullptr : &it->second;
}
bool IsNum(const json::Value* v) { return v != nullptr && v->kind == json::Value::Kind::Number; }
int IntOr(const json::Value* v, int d) { return IsNum(v) ? int(v->number) : d; }

bool Base64(const std::string& s, size_t from, std::vector<uint8_t>& out) {
  auto val = [](char c) -> int {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+' || c == '-') return 62;
    if (c == '/' || c == '_') return 63;
    return -1;
  };
  uint32_t acc = 0;
  int bits = 0;
  for (size_t i = from; i < s.size(); i++) {
    if (s[i] == '=') break;
    const int v = val(s[i]);
    if (v < 0) return false;
    acc = (acc << 6) | uint32_t(v), bits += 6;
    if (bits >= 8) bits -= 8, out.push_back(uint8_t(acc >> bits)), acc &= (1u << bits) - 1;
  }
  return true;
}

bool DataUri(const std::string& uri, std::vector<uint8_t>& out) {
  if (uri.compare(0, 5, "data:") != 0) return false;
  const size_t comma = uri.find(',');
  if (comma == std::string::npos || uri.find(";base64") == std::string::npos || uri.find(";base64") > comma) return false;
  return Base64(uri, comma + 1, out);
}

struct Source {
  json::Value doc;
  std::vector<std::vector<uint8_t>> buffers;
};

const uint8_t* ViewBytes(const Source& g, int view, size_t& size, int& stride, std::string& problem) {
  const json::Value* views = Get(g.doc, "bufferViews");
  if (views == nullptr || view < 0 || size_t(view) >= views->items.size()) { problem = "a bufferView is missing"; return nullptr; }
  const json::Value& v = views->items[size_t(view)];
  const int buf = IntOr(Get(v, "buffer"), 0);
  const size_t off = size_t(IntOr(Get(v, "byteOffset"), 0)), len = size_t(IntOr(Get(v, "byteLength"), 0));
  if (buf < 0 || size_t(buf) >= g.buffers.size() || off + len > g.buffers[size_t(buf)].size()) { problem = "a bufferView leads outside its buffer"; return nullptr; }
  size = len, stride = IntOr(Get(v, "byteStride"), 0);
  return g.buffers[size_t(buf)].data() + off;
}

bool ReadAccessor(const Source& g, int index, int comps, std::vector<double>& out, std::string& problem) {
  const json::Value* acc = Get(g.doc, "accessors");
  if (acc == nullptr || index < 0 || size_t(index) >= acc->items.size()) { problem = "an accessor is missing"; return false; }
  const json::Value& a = acc->items[size_t(index)];
  if (Get(a, "sparse") != nullptr) { problem = "sparse accessors are not read"; return false; }
  const json::Value* type = Get(a, "type");
  int have = 0;
  if (type != nullptr) {
    const std::string& t = type->text;
    have = t == "SCALAR" ? 1 : t == "VEC2" ? 2 : t == "VEC3" ? 3 : t == "VEC4" ? 4 : 0;
  }
  if (have != comps) { problem = "an accessor has " + std::to_string(have) + " components, " + std::to_string(comps) + " were expected"; return false; }
  const int ct = IntOr(Get(a, "componentType"), 0), count = IntOr(Get(a, "count"), 0);
  const json::Value* normalized = Get(a, "normalized");
  const bool norm = normalized != nullptr && normalized->boolean;
  int csize = 0;
  switch (ct) {
    case 5120: case 5121: csize = 1; break;
    case 5122: case 5123: csize = 2; break;
    case 5125: case 5126: csize = 4; break;
    default: problem = "an accessor has an unknown component type"; return false;
  }
  if (Get(a, "bufferView") == nullptr) { out.assign(size_t(count) * comps, 0.0); return true; }
  size_t vsize = 0;
  int stride = 0;
  const uint8_t* base = ViewBytes(g, IntOr(Get(a, "bufferView"), -1), vsize, stride, problem);
  if (base == nullptr) return false;
  const size_t aoff = size_t(IntOr(Get(a, "byteOffset"), 0));
  const size_t elem = size_t(csize) * comps, step = stride > 0 ? size_t(stride) : elem;
  if (count < 0 || count > 20000000 || (count > 0 && aoff + step * size_t(count - 1) + elem > vsize)) { problem = "an accessor leads outside its bufferView"; return false; }
  out.resize(size_t(count) * comps);
  for (int i = 0; i < count; i++) {
    for (int c = 0; c < comps; c++) {
      const uint8_t* p = base + aoff + step * size_t(i) + size_t(c) * csize;
      double v = 0;
      switch (ct) {
        case 5120: v = int8_t(p[0]), v = norm ? std::max(v / 127.0, -1.0) : v; break;
        case 5121: v = p[0], v = norm ? v / 255.0 : v; break;
        case 5122: v = int16_t(p[0] | (p[1] << 8)), v = norm ? std::max(v / 32767.0, -1.0) : v; break;
        case 5123: v = uint16_t(p[0] | (p[1] << 8)), v = norm ? v / 65535.0 : v; break;
        case 5125: v = double(uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24); break;
        default: {
          const uint32_t u = uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
          float f;
          std::memcpy(&f, &u, 4);
          v = f;
        }
      }
      out[size_t(i) * comps + size_t(c)] = v;
    }
  }
  return true;
}

}

bool GltfRead(const uint8_t* bytes, size_t size, MeshModel& out, std::string& problem) {
  out = MeshModel{};
  Source g;
  std::string jsonText;
  std::vector<uint8_t> glbBin;
  bool haveBin = false;
  if (size >= 12 && std::memcmp(bytes, "glTF", 4) == 0) {
    const auto le32 = [&](size_t o) { return uint32_t(bytes[o]) | uint32_t(bytes[o + 1]) << 8 | uint32_t(bytes[o + 2]) << 16 | uint32_t(bytes[o + 3]) << 24; };
    if (le32(4) != 2) { problem = "only glTF version 2 is read"; return false; }
    size_t pos = 12;
    const size_t total = std::min<size_t>(size, le32(8));
    while (pos + 8 <= total) {
      const uint32_t len = le32(pos), type = le32(pos + 4);
      if (pos + 8 + len > total) { problem = "the .glb file is cut short"; return false; }
      if (type == 0x4E4F534A) jsonText.assign(reinterpret_cast<const char*>(bytes + pos + 8), len);
      else if (type == 0x004E4942 && !haveBin) glbBin.assign(bytes + pos + 8, bytes + pos + 8 + len), haveBin = true;
      pos += 8 + size_t((len + 3) & ~3u);
    }
  } else {
    jsonText.assign(reinterpret_cast<const char*>(bytes), size);
  }
  if (jsonText.empty()) { problem = "the file holds no glTF data"; return false; }
  json::Reader reader(jsonText);
  if (!reader.Document(g.doc) || g.doc.kind != json::Value::Kind::Object) { problem = "the glTF's JSON is not readable: " + reader.Error(); return false; }
  if (const json::Value* asset = Get(g.doc, "asset")) {
    if (const json::Value* ver = Get(*asset, "version"); ver != nullptr && !ver->text.empty() && ver->text[0] != '2') { problem = "only glTF version 2 is read"; return false; }
  }
  if (const json::Value* ext = Get(g.doc, "extensionsRequired"); ext != nullptr && !ext->items.empty()) {
    problem = "the glTF needs the extension \"" + ext->items[0].text + "\", which is not read (compressed meshes need to be exported plainly)";
    return false;
  }
  if (const json::Value* bufs = Get(g.doc, "buffers")) {
    for (size_t i = 0; i < bufs->items.size(); i++) {
      std::vector<uint8_t> data;
      const json::Value* uri = Get(bufs->items[i], "uri");
      if (uri == nullptr) {
        if (i != 0 || !haveBin) { problem = "a buffer has no data"; return false; }
        data = glbBin;
      } else if (!DataUri(uri->text, data)) {
        problem = "a buffer is in a separate file; put everything into one .glb (or into the .gltf as data)";
        return false;
      }
      g.buffers.push_back(std::move(data));
    }
  }
  const json::Value* images = Get(g.doc, "images");
  const json::Value* textures = Get(g.doc, "textures");
  const json::Value* samplers = Get(g.doc, "samplers");
  std::map<int, int> imageOfTexture;
  const auto pictureOf = [&](int tex, std::string& why) -> int {
    const auto found = imageOfTexture.find(tex);
    if (found != imageOfTexture.end()) return found->second;
    if (textures == nullptr || tex < 0 || size_t(tex) >= textures->items.size()) { why = "a material names a texture that is not there"; return -1; }
    const json::Value& t = textures->items[size_t(tex)];
    const int src = IntOr(Get(t, "source"), -1);
    if (images == nullptr || src < 0 || size_t(src) >= images->items.size()) { why = "a texture has no picture (only PNG pictures are read)"; return -1; }
    const json::Value& im = images->items[size_t(src)];
    std::vector<uint8_t> data;
    if (const json::Value* uri = Get(im, "uri")) {
      if (!DataUri(uri->text, data)) { why = "a picture is in a separate file; put everything into one .glb"; return -1; }
    } else if (Get(im, "bufferView") != nullptr) {
      size_t vs = 0;
      int stride = 0;
      const uint8_t* p = ViewBytes(g, IntOr(Get(im, "bufferView"), -1), vs, stride, why);
      if (p == nullptr) return -1;
      data.assign(p, p + vs);
    } else {
      why = "a picture has no data";
      return -1;
    }
    MeshImage mi;
    if (!PngDecode(data.data(), data.size(), mi.width, mi.height, mi.rgba, why)) return -1;
    if (samplers != nullptr) {
      const int s = IntOr(Get(t, "sampler"), -1);
      if (s >= 0 && size_t(s) < samplers->items.size()) {
        const int ws = IntOr(Get(samplers->items[size_t(s)], "wrapS"), 10497), wt = IntOr(Get(samplers->items[size_t(s)], "wrapT"), 10497);
        mi.repeat = ws != 33071 && wt != 33071;
      } else mi.repeat = true;
    } else mi.repeat = true;
    out.images.push_back(std::move(mi));
    imageOfTexture[tex] = int(out.images.size()) - 1;
    return imageOfTexture[tex];
  };
  if (const json::Value* mats = Get(g.doc, "materials")) {
    for (const json::Value& m : mats->items) {
      MeshMaterial mm;
      if (const json::Value* ds = Get(m, "doubleSided")) mm.doubleSided = ds->boolean;
      if (const json::Value* pbr = Get(m, "pbrMetallicRoughness")) {
        if (const json::Value* f = Get(*pbr, "baseColorFactor"); f != nullptr && f->items.size() == 4) {
          for (int i = 0; i < 4; i++) mm.color[i] = float(f->items[size_t(i)].number);
        }
        if (const json::Value* bt = Get(*pbr, "baseColorTexture")) {
          std::string why;
          mm.image = pictureOf(IntOr(Get(*bt, "index"), -1), why);
          if (mm.image < 0) { problem = why; return false; }
        }
      }
      out.materials.push_back(mm);
    }
  }
  const json::Value* nodes = Get(g.doc, "nodes");
  const json::Value* meshes = Get(g.doc, "meshes");
  if (meshes == nullptr || meshes->items.empty()) { problem = "the glTF has no mesh"; return false; }
  std::vector<int> roots;
  if (nodes != nullptr && !nodes->items.empty()) {
    const json::Value* scenes = Get(g.doc, "scenes");
    if (scenes != nullptr && !scenes->items.empty()) {
      const int s = std::min<int>(IntOr(Get(g.doc, "scene"), 0), int(scenes->items.size()) - 1);
      if (const json::Value* rn = Get(scenes->items[size_t(std::max(0, s))], "nodes")) {
        for (const json::Value& n : rn->items) roots.push_back(int(n.number));
      }
    } else {
      std::set<int> child;
      for (const json::Value& n : nodes->items) {
        if (const json::Value* c = Get(n, "children")) {
          for (const json::Value& k : c->items) child.insert(int(k.number));
        }
      }
      for (size_t i = 0; i < nodes->items.size(); i++) {
        if (!child.count(int(i))) roots.push_back(int(i));
      }
    }
  }
  bool bad = false;
  size_t total = 0;
  const std::function<void(int, const Mat4&, int)> visit = [&](int ni, const Mat4& parent, int depth) {
    if (bad) return;
    if (nodes == nullptr || ni < 0 || size_t(ni) >= nodes->items.size() || depth > 64) { problem = "the glTF's node tree is not usable"; bad = true; return; }
    const json::Value& n = nodes->items[size_t(ni)];
    Mat4 local = Ident();
    if (const json::Value* m = Get(n, "matrix"); m != nullptr && m->items.size() == 16) {
      for (int i = 0; i < 16; i++) local[size_t(i)] = m->items[size_t(i)].number;
    } else {
      double t[3] = {0, 0, 0}, s[3] = {1, 1, 1}, q[4] = {0, 0, 0, 1};
      if (const json::Value* v = Get(n, "translation"); v != nullptr && v->items.size() == 3) for (int i = 0; i < 3; i++) t[i] = v->items[size_t(i)].number;
      if (const json::Value* v = Get(n, "scale"); v != nullptr && v->items.size() == 3) for (int i = 0; i < 3; i++) s[i] = v->items[size_t(i)].number;
      if (const json::Value* v = Get(n, "rotation"); v != nullptr && v->items.size() == 4) for (int i = 0; i < 4; i++) q[i] = v->items[size_t(i)].number;
      const double x = q[0], y = q[1], z = q[2], w = q[3];
      const double r[9] = {1 - 2 * (y * y + z * z), 2 * (x * y + z * w), 2 * (x * z - y * w), 2 * (x * y - z * w), 1 - 2 * (x * x + z * z), 2 * (y * z + x * w),
                           2 * (x * z + y * w), 2 * (y * z - x * w), 1 - 2 * (x * x + y * y)};
      for (int c = 0; c < 3; c++) {
        for (int rr = 0; rr < 3; rr++) local[size_t(c * 4 + rr)] = r[c * 3 + rr] * s[c];
      }
      local[12] = t[0], local[13] = t[1], local[14] = t[2];
    }
    const Mat4 world = Mul4(parent, local);
    if (const json::Value* mi = Get(n, "mesh"); IsNum(mi)) {
      const int mesh = int(mi->number);
      if (mesh < 0 || size_t(mesh) >= meshes->items.size()) { problem = "a node names a mesh that is not there"; bad = true; return; }
      const json::Value* prims = Get(meshes->items[size_t(mesh)], "primitives");
      if (prims == nullptr) return;
      const double det = world[0] * (world[5] * world[10] - world[9] * world[6]) - world[4] * (world[1] * world[10] - world[9] * world[2]) +
                         world[8] * (world[1] * world[6] - world[5] * world[2]);
      for (const json::Value& pr : prims->items) {
        if (IntOr(Get(pr, "mode"), 4) != 4) { problem = "a mesh is not made of triangle lists (strips and fans are not read; export triangles)"; bad = true; return; }
        const json::Value* attrs = Get(pr, "attributes");
        if (attrs == nullptr || Get(*attrs, "POSITION") == nullptr) { problem = "a mesh has no positions"; bad = true; return; }
        MeshPrim p;
        std::vector<double> pos, uv, col;
        if (!ReadAccessor(g, IntOr(Get(*attrs, "POSITION"), -1), 3, pos, problem)) { bad = true; return; }
        if (const json::Value* t = Get(*attrs, "TEXCOORD_0")) {
          if (!ReadAccessor(g, IntOr(t, -1), 2, uv, problem)) { bad = true; return; }
        }
        if (const json::Value* c = Get(*attrs, "COLOR_0")) {
          std::string ignore;
          if (!ReadAccessor(g, IntOr(c, -1), 4, col, ignore)) {
            col.clear();
            std::vector<double> c3;
            if (ReadAccessor(g, IntOr(c, -1), 3, c3, ignore)) {
              for (size_t i = 0; i + 2 < c3.size(); i += 3) col.insert(col.end(), {c3[i], c3[i + 1], c3[i + 2], 1.0});
            }
          }
        }
        const size_t nv = pos.size() / 3;
        std::vector<double> idx;
        if (const json::Value* ix = Get(pr, "indices"); IsNum(ix)) {
          if (!ReadAccessor(g, int(ix->number), 1, idx, problem)) { bad = true; return; }
        } else {
          idx.resize(nv);
          for (size_t i = 0; i < nv; i++) idx[i] = double(i);
        }
        if (idx.size() % 3 != 0) { problem = "a mesh's index count is not a multiple of 3"; bad = true; return; }
        for (double d : idx) {
          if (d < 0 || size_t(d) >= nv) { problem = "a mesh's index points past its vertices"; bad = true; return; }
        }
        p.pos.resize(nv * 3);
        for (size_t i = 0; i < nv; i++) {
          for (int r = 0; r < 3; r++) {
            p.pos[i * 3 + size_t(r)] = float(world[size_t(r)] * pos[i * 3] + world[size_t(4 + r)] * pos[i * 3 + 1] + world[size_t(8 + r)] * pos[i * 3 + 2] + world[size_t(12 + r)]);
          }
        }
        if (uv.size() == nv * 2) p.uv.assign(uv.begin(), uv.end());
        if (col.size() == nv * 4) p.color.assign(col.begin(), col.end());
        p.idx.resize(idx.size());
        for (size_t i = 0; i < idx.size(); i += 3) {
          p.idx[i] = uint32_t(idx[i]);
          p.idx[i + 1] = uint32_t(idx[det < 0 ? i + 2 : i + 1]);
          p.idx[i + 2] = uint32_t(idx[det < 0 ? i + 1 : i + 2]);
        }
        p.material = IntOr(Get(pr, "material"), -1);
        if (p.material >= int(out.materials.size())) { problem = "a mesh names a material that is not there"; bad = true; return; }
        total += p.idx.size() / 3;
        out.prims.push_back(std::move(p));
      }
    }
    if (const json::Value* kids = Get(n, "children")) {
      for (const json::Value& k : kids->items) visit(int(k.number), world, depth + 1);
    }
  };
  for (int r : roots) visit(r, Ident(), 0);
  if (bad) return false;
  if (total == 0) { problem = "the glTF holds no triangles"; return false; }
  int plain = -1;
  for (MeshPrim& p : out.prims) {
    if (p.material < 0) {
      if (plain < 0) out.materials.push_back(MeshMaterial{}), plain = int(out.materials.size()) - 1;
      p.material = plain;
    }
  }
  return true;
}

std::vector<std::array<float, 9>> MeshWorldTriangles(const MeshModel& m, float scale, float turnDegrees, float x, float y, float z) {
  const float a = turnDegrees * 3.14159265358979f / 180.f, c = std::cos(a), s = std::sin(a);
  std::vector<std::array<float, 9>> out;
  for (const MeshPrim& p : m.prims) {
    for (size_t t = 0; t + 2 < p.idx.size(); t += 3) {
      std::array<float, 9> tri;
      for (int k = 0; k < 3; k++) {
        const float px = p.pos[p.idx[t + size_t(k)] * 3] * scale, py = p.pos[p.idx[t + size_t(k)] * 3 + 1] * scale, pz = p.pos[p.idx[t + size_t(k)] * 3 + 2] * scale;
        tri[size_t(k * 3)] = c * px + s * pz + x;
        tri[size_t(k * 3 + 1)] = py + y;
        tri[size_t(k * 3 + 2)] = -s * px + c * pz + z;
      }
      out.push_back(tri);
    }
  }
  return out;
}

namespace {

struct Writer {
  HsdArchive& a;
  uint32_t Alloc(size_t bytes, uint32_t align = 4) {
    uint32_t at = uint32_t((a.data.size() + align - 1) / align * align);
    a.data.resize(size_t(at) + bytes, 0);
    return at;
  }
  void Ptr(uint32_t at, uint32_t to) { a.SetU32(at, to), a.AddSlot(at); }
  void U16(uint32_t at, uint16_t v) { a.data[at] = uint8_t(v >> 8), a.data[at + 1] = uint8_t(v); }
  void U8(uint32_t at, uint8_t v) { a.data[at] = v; }
};

uint16_t Rgb565(const uint8_t* p) { return uint16_t(((p[0] >> 3) << 11) | ((p[1] >> 2) << 5) | (p[2] >> 3)); }

}

bool HsdAddMeshModel(HsdArchive& into, const MeshModel& m, float scale, float turn, float x, float y, float z, std::string& problem, uint32_t* newRoot, uint32_t* bytesAdded) {
  if (into.FindPublic("scene_data") < 0) { problem = "the room's map has no scene_data"; return false; }
  if (!(scale > 0) || scale > 1e4f) { problem = "\"scale\" must be a number above 0"; return false; }
  const uint32_t startSize = uint32_t(into.data.size());
  const auto fail = [&](const std::string& why) {
    problem = why;
    into.data.resize(startSize);
    into.slots.erase(std::lower_bound(into.slots.begin(), into.slots.end(), startSize), into.slots.end());
    return false;
  };
  Writer w{into};
  std::vector<std::vector<size_t>> byMaterial(m.materials.size());
  for (size_t i = 0; i < m.prims.size(); i++) {
    if (m.prims[i].material < 0 || size_t(m.prims[i].material) >= m.materials.size()) return fail("a mesh has no material");
    byMaterial[size_t(m.prims[i].material)].push_back(i);
  }
  const uint32_t root = w.Alloc(0x40, 32);
  w.a.SetU32(root + 4, 0x10040000);
  w.a.SetF32(root + 0x14, 0), w.a.SetF32(root + 0x18, turn * 3.14159265358979f / 180.f), w.a.SetF32(root + 0x1C, 0);
  w.a.SetF32(root + 0x20, 1), w.a.SetF32(root + 0x24, 1), w.a.SetF32(root + 0x28, 1);
  w.a.SetF32(root + 0x2C, x), w.a.SetF32(root + 0x30, y), w.a.SetF32(root + 0x34, z);
  uint32_t lastDobj = 0;
  std::map<int, uint32_t> imageDesc;
  for (size_t mi = 0; mi < m.materials.size(); mi++) {
    if (byMaterial[mi].empty()) continue;
    const MeshMaterial& mat = m.materials[mi];
    const bool textured = mat.image >= 0;
    std::vector<float> pos, uv;
    std::vector<uint8_t> col;
    std::vector<uint32_t> list;
    for (size_t pi : byMaterial[mi]) {
      const MeshPrim& p = m.prims[pi];
      const uint32_t first = uint32_t(pos.size() / 3);
      const size_t nv = p.pos.size() / 3;
      for (size_t v = 0; v < nv; v++) {
        for (int k = 0; k < 3; k++) pos.push_back(p.pos[v * 3 + size_t(k)] * scale);
        uv.push_back(p.uv.size() == nv * 2 ? p.uv[v * 2] : 0.f), uv.push_back(p.uv.size() == nv * 2 ? p.uv[v * 2 + 1] : 0.f);
        for (int k = 0; k < 4; k++) {
          const float vc = p.color.size() == nv * 4 ? p.color[v * 4 + size_t(k)] : 1.f;
          const float f = std::min(1.f, std::max(0.f, vc * mat.color[k]));
          col.push_back(uint8_t(f * 255.f + 0.5f));
        }
      }
      for (size_t i = 0; i + 2 < p.idx.size(); i += 3) list.push_back(first + p.idx[i]), list.push_back(first + p.idx[i + 2]), list.push_back(first + p.idx[i + 1]);
    }
    if (pos.size() / 3 > 65535) return fail("a material of the model has more than 65535 vertices; split the model by material or simplify it");
    if (list.size() > 3000000) return fail("the model has too many triangles");
    const uint32_t nvert = uint32_t(pos.size() / 3);
    const uint32_t posAt = w.Alloc(size_t(nvert) * 12, 32);
    for (uint32_t i = 0; i < nvert * 3; i++) w.a.SetF32(posAt + 4 * i, pos[i]);
    const uint32_t colAt = w.Alloc(size_t(nvert) * 4, 32);
    std::memcpy(&w.a.data[colAt], col.data(), col.size());
    uint32_t uvAt = 0;
    if (textured) {
      uvAt = w.Alloc(size_t(nvert) * 8, 32);
      for (uint32_t i = 0; i < nvert * 2; i++) w.a.SetF32(uvAt + 4 * i, uv[i]);
    }
    std::vector<uint8_t> dl;
    const size_t per = textured ? 6 : 4;
    for (size_t at = 0; at < list.size();) {
      const size_t n = std::min<size_t>(list.size() - at, 65535 / 3 * 3);
      dl.push_back(0x90), dl.push_back(uint8_t(n >> 8)), dl.push_back(uint8_t(n));
      for (size_t k = 0; k < n; k++) {
        const uint32_t v = list[at + k];
        dl.push_back(uint8_t(v >> 8)), dl.push_back(uint8_t(v));
        dl.push_back(uint8_t(v >> 8)), dl.push_back(uint8_t(v));
        if (textured) dl.push_back(uint8_t(v >> 8)), dl.push_back(uint8_t(v));
      }
      at += n;
    }
    (void)per;
    while (dl.size() % 32 != 0) dl.push_back(0);
    if (dl.size() / 32 > 65535) return fail("the model has too many triangles for one mesh; split it by material");
    const uint32_t dlAt = w.Alloc(dl.size(), 32);
    std::memcpy(&w.a.data[dlAt], dl.data(), dl.size());
    const int nattr = textured ? 3 : 2;
    const uint32_t descAt = w.Alloc(size_t(nattr + 1) * 0x18, 4);
    const auto attr = [&](int i, uint32_t at, uint32_t type, uint32_t cnt, uint32_t comp, uint16_t stride, uint32_t base) {
      const uint32_t o = descAt + uint32_t(i) * 0x18;
      w.a.SetU32(o, at), w.a.SetU32(o + 4, type), w.a.SetU32(o + 8, cnt), w.a.SetU32(o + 0xC, comp);
      w.U8(o + 0x10, 0), w.U16(o + 0x12, stride);
      w.Ptr(o + 0x14, base);
    };
    attr(0, 9, 3, 1, 4, 12, posAt);
    attr(1, 11, 3, 1, 5, 4, colAt);
    if (textured) attr(2, 13, 3, 1, 4, 8, uvAt);
    {
      const uint32_t o = descAt + uint32_t(nattr) * 0x18;
      w.a.SetU32(o, 0xFF), w.a.SetU32(o + 4, 2), w.a.SetU32(o + 8, 0), w.a.SetU32(o + 0xC, 4);
    }
    const uint32_t pobj = w.Alloc(0x18, 4);
    w.Ptr(pobj + 8, descAt);
    w.U16(pobj + 0xC, mat.doubleSided ? 0x0000 : 0x8000);
    w.U16(pobj + 0xE, uint16_t(dl.size() / 32));
    w.Ptr(pobj + 0x10, dlAt);
    const uint32_t matAt = w.Alloc(0x14, 4);
    const uint8_t amb[4] = {0x80, 0x80, 0x80, 0xFF}, dif[4] = {0xFF, 0xFF, 0xFF, 0xFF}, spc[4] = {0xFF, 0xFF, 0xFF, 0xFF};
    std::memcpy(&w.a.data[matAt], amb, 4), std::memcpy(&w.a.data[matAt + 4], dif, 4), std::memcpy(&w.a.data[matAt + 8], spc, 4);
    w.a.SetF32(matAt + 0xC, 1.0f), w.a.SetF32(matAt + 0x10, 50.0f);
    uint32_t tobj = 0;
    if (textured) {
      const MeshImage& im = m.images[size_t(mat.image)];
      auto it = imageDesc.find(mat.image);
      if (it == imageDesc.end()) {
        if (im.width < 1 || im.height < 1 || im.width > 1024 || im.height > 1024) return fail("a picture is " + std::to_string(im.width) + " x " + std::to_string(im.height) + "; the largest is 1024 x 1024 (the game's own use 512 x 512 at most)");
        const int tw = (im.width + 3) / 4 * 4, th = (im.height + 3) / 4 * 4;
        bool alpha = false;
        for (size_t i = 3; i < im.rgba.size(); i += 4) alpha = alpha || im.rgba[i] < 250;
        const uint32_t format = alpha ? 5 : 4;
        const uint32_t imgAt = w.Alloc(size_t(tw) * th * 2, 32);
        uint32_t o = imgAt;
        for (int ty = 0; ty < th; ty += 4) {
          for (int tx = 0; tx < tw; tx += 4) {
            for (int yy = 0; yy < 4; yy++) {
              for (int xx = 0; xx < 4; xx++) {
                const int sx = std::min(tx + xx, im.width - 1), sy = std::min(ty + yy, im.height - 1);
                const uint8_t* p = &im.rgba[(size_t(sy) * im.width + size_t(sx)) * 4];
                uint16_t v;
                if (!alpha) v = Rgb565(p);
                else if (p[3] >= 0xE0) v = uint16_t(0x8000 | ((p[0] >> 3) << 10) | ((p[1] >> 3) << 5) | (p[2] >> 3));
                else v = uint16_t(((p[3] >> 5) << 12) | ((p[0] >> 4) << 8) | ((p[1] >> 4) << 4) | (p[2] >> 4));
                w.U16(o, v);
                o += 2;
              }
            }
          }
        }
        const uint32_t desc = w.Alloc(0x18, 4);
        w.Ptr(desc, imgAt);
        w.U16(desc + 4, uint16_t(tw)), w.U16(desc + 6, uint16_t(th));
        w.a.SetU32(desc + 8, format), w.a.SetU32(desc + 0xC, 0);
        it = imageDesc.emplace(mat.image, desc).first;
      }
      tobj = w.Alloc(0x5C, 4);
      w.a.SetU32(tobj + 0xC, 4);
      w.a.SetF32(tobj + 0x1C, 1), w.a.SetF32(tobj + 0x20, 1), w.a.SetF32(tobj + 0x24, 1);
      const bool pow2 = (im.width & (im.width - 1)) == 0 && (im.height & (im.height - 1)) == 0 && im.width % 4 == 0 && im.height % 4 == 0;
      const uint32_t wrap = (im.repeat && pow2) ? 1 : 0;
      w.a.SetU32(tobj + 0x34, wrap), w.a.SetU32(tobj + 0x38, wrap);
      w.U8(tobj + 0x3C, 1), w.U8(tobj + 0x3D, 1);
      w.a.SetU32(tobj + 0x40, 0x00040010);
      w.a.SetF32(tobj + 0x44, 1.0f);
      w.a.SetU32(tobj + 0x48, 1);
      w.Ptr(tobj + 0x4C, it->second);
    }
    const uint32_t mobj = w.Alloc(0x18, 4);
    w.a.SetU32(mobj + 4, textured ? 0x2012u : 0x2002u);
    if (textured) w.Ptr(mobj + 8, tobj);
    w.Ptr(mobj + 0xC, matAt);
    const uint32_t dobj = w.Alloc(0x10, 4);
    w.Ptr(dobj + 8, mobj);
    w.Ptr(dobj + 0xC, pobj);
    if (lastDobj == 0) w.Ptr(root + 0x10, dobj);
    else w.Ptr(lastDobj + 4, dobj);
    lastDobj = dobj;
  }
  if (lastDobj == 0) return fail("the model has no triangles");
  w.a.data.resize((w.a.data.size() + 31) / 32 * 32, 0);
  if (!HsdAddModelSet(into, root, problem)) {
    into.data.resize(startSize);
    into.slots.erase(std::lower_bound(into.slots.begin(), into.slots.end(), startSize), into.slots.end());
    return false;
  }
  if (newRoot != nullptr) *newRoot = root;
  if (bytesAdded != nullptr) *bytesAdded = uint32_t(into.data.size()) - startSize;
  return true;
}

MeshCollisionCount CcdAddMeshTriangles(CcdRecord& rec, const std::vector<std::array<float, 9>>& tris, float floorLimit) {
  MeshCollisionCount count;
  struct T { std::array<float, 9> v; float n[3]; bool ok; };
  std::vector<T> all;
  for (const auto& t : tris) {
    T x;
    x.v = t;
    const float ax = t[3] - t[0], ay = t[4] - t[1], az = t[5] - t[2], bx = t[6] - t[0], by = t[7] - t[1], bz = t[8] - t[2];
    float nx = ay * bz - az * by, ny = az * bx - ax * bz, nz = ax * by - ay * bx;
    const float l = std::sqrt(nx * nx + ny * ny + nz * nz);
    x.ok = l > 1e-9f && std::isfinite(l);
    if (x.ok) nx /= l, ny /= l, nz /= l;
    x.n[0] = nx, x.n[1] = ny, x.n[2] = nz;
    all.push_back(x);
  }
  std::vector<size_t> walls;
  for (size_t i = 0; i < all.size(); i++) {
    if (!all[i].ok) { count.skipped++; continue; }
    if (all[i].n[1] > floorLimit) {
      CcdTri ct;
      std::memcpy(ct.v, all[i].v.data(), sizeof ct.v);
      std::memcpy(ct.n, all[i].n, sizeof ct.n);
      ct.tail[0] = 0xFF;
      rec.block[kCcdWalk].present = true;
      rec.block[kCcdWalk].tris.push_back(ct);
      count.floor++;
    } else walls.push_back(i);
  }
  const auto key = [](const float* p) {
    const auto q = [](float f) { return int64_t(std::llround(f * 1000.f)); };
    return std::array<int64_t, 3>{q(p[0]), q(p[1]), q(p[2])};
  };
  std::map<std::pair<std::array<int64_t, 3>, std::array<int64_t, 3>>, std::vector<std::pair<size_t, int>>> edges;
  for (size_t wi = 0; wi < walls.size(); wi++) {
    const T& t = all[walls[wi]];
    for (int e = 0; e < 3; e++) {
      auto a = key(&t.v[size_t(e * 3)]), b = key(&t.v[size_t(((e + 1) % 3) * 3)]);
      if (b < a) std::swap(a, b);
      edges[{a, b}].emplace_back(wi, e);
    }
  }
  std::vector<uint16_t> flags(walls.size(), 0);
  for (const auto& kv : edges) {
    for (const auto& [wi, e] : kv.second) {
      bool test = kv.second.size() == 1;
      if (!test) {
        const T& me = all[walls[wi]];
        for (const auto& [wj, ej] : kv.second) {
          if (wj == wi) continue;
          const T& other = all[walls[wj]];
          const float* opp = &other.v[size_t(((ej + 2) % 3) * 3)];
          const float* o0 = &me.v[0];
          const float d = me.n[0] * (opp[0] - o0[0]) + me.n[1] * (opp[1] - o0[1]) + me.n[2] * (opp[2] - o0[2]);
          if (d < -1e-4f) test = true;
        }
      }
      if (test) flags[wi] = uint16_t(flags[wi] | (1u << e));
    }
  }
  for (size_t wi = 0; wi < walls.size(); wi++) {
    CcdTri ct;
    std::memcpy(ct.v, all[walls[wi]].v.data(), sizeof ct.v);
    std::memcpy(ct.n, all[walls[wi]].n, sizeof ct.n);
    ct.tail[0] = uint8_t(flags[wi] >> 8), ct.tail[1] = uint8_t(flags[wi]), ct.tail[2] = ct.tail[3] = 0;
    for (int kind : {int(kCcdHit), int(kCcdHit34)}) {
      rec.block[kind].present = true;
      rec.block[kind].tris.push_back(ct);
    }
    count.walls++;
  }
  return count;
}

}
