#include "save_thumbnail.h"

#include <zlib.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <system_error>

namespace engine::memcard::thumbnail {

namespace fs = std::filesystem;

Picture Shrink(const uint8_t* rgba, uint32_t w, uint32_t h) {
  Picture out;
  if (rgba == nullptr || w == 0 || h == 0) return out;
  if (w <= kMaxWidth) {
    out.width = w;
    out.height = h;
    out.rgba.assign(rgba, rgba + static_cast<size_t>(w) * h * 4);
    return out;
  }
  out.width = kMaxWidth;
  out.height = std::max<uint32_t>(1, static_cast<uint32_t>((static_cast<uint64_t>(h) * kMaxWidth + w / 2) / w));
  out.rgba.resize(static_cast<size_t>(out.width) * out.height * 4);
  for (uint32_t y = 0; y < out.height; y++) {
    const uint32_t y0 = static_cast<uint32_t>(static_cast<uint64_t>(y) * h / out.height);
    const uint32_t y1 = std::max(y0 + 1, static_cast<uint32_t>(static_cast<uint64_t>(y + 1) * h / out.height));
    for (uint32_t x = 0; x < out.width; x++) {
      const uint32_t x0 = static_cast<uint32_t>(static_cast<uint64_t>(x) * w / out.width);
      const uint32_t x1 = std::max(x0 + 1, static_cast<uint32_t>(static_cast<uint64_t>(x + 1) * w / out.width));
      uint64_t sum[3] = {0, 0, 0};
      uint64_t n = 0;
      for (uint32_t yy = y0; yy < y1 && yy < h; yy++) {
        for (uint32_t xx = x0; xx < x1 && xx < w; xx++) {
          const uint8_t* p = rgba + (static_cast<size_t>(yy) * w + xx) * 4;
          sum[0] += p[0], sum[1] += p[1], sum[2] += p[2];
          n++;
        }
      }
      if (n == 0) n = 1;
      uint8_t* q = out.rgba.data() + (static_cast<size_t>(y) * out.width + x) * 4;
      q[0] = static_cast<uint8_t>(sum[0] / n), q[1] = static_cast<uint8_t>(sum[1] / n), q[2] = static_cast<uint8_t>(sum[2] / n);
      q[3] = 255;
    }
  }
  return out;
}

namespace {

void Put32(std::vector<uint8_t>& v, uint32_t x) {
  v.push_back(static_cast<uint8_t>(x >> 24)), v.push_back(static_cast<uint8_t>(x >> 16));
  v.push_back(static_cast<uint8_t>(x >> 8)), v.push_back(static_cast<uint8_t>(x));
}

void Chunk(std::vector<uint8_t>& out, const char* type, const std::vector<uint8_t>& data) {
  Put32(out, static_cast<uint32_t>(data.size()));
  const size_t from = out.size();
  out.insert(out.end(), type, type + 4);
  out.insert(out.end(), data.begin(), data.end());
  const uLong crc = crc32(crc32(0L, nullptr, 0), out.data() + from, static_cast<uInt>(out.size() - from));
  Put32(out, static_cast<uint32_t>(crc));
}

uint32_t Get32(const uint8_t* p) { return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3]; }

}

std::vector<uint8_t> EncodePng(const Picture& pic) {
  std::vector<uint8_t> out;
  if (pic.Empty() || pic.rgba.size() < static_cast<size_t>(pic.width) * pic.height * 4) return out;
  static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
  out.insert(out.end(), sig, sig + 8);
  std::vector<uint8_t> ihdr;
  Put32(ihdr, pic.width), Put32(ihdr, pic.height);
  ihdr.push_back(8), ihdr.push_back(2), ihdr.push_back(0), ihdr.push_back(0), ihdr.push_back(0);
  Chunk(out, "IHDR", ihdr);
  std::vector<uint8_t> raw;
  raw.reserve((static_cast<size_t>(pic.width) * 3 + 1) * pic.height);
  for (uint32_t y = 0; y < pic.height; y++) {
    raw.push_back(0);
    for (uint32_t x = 0; x < pic.width; x++) {
      const uint8_t* p = pic.rgba.data() + (static_cast<size_t>(y) * pic.width + x) * 4;
      raw.push_back(p[0]), raw.push_back(p[1]), raw.push_back(p[2]);
    }
  }
  uLongf size = compressBound(static_cast<uLong>(raw.size()));
  std::vector<uint8_t> packed(size);
  if (compress2(packed.data(), &size, raw.data(), static_cast<uLong>(raw.size()), 6) != Z_OK) return {};
  packed.resize(size);
  Chunk(out, "IDAT", packed);
  Chunk(out, "IEND", {});
  return out;
}

bool DecodePng(const std::vector<uint8_t>& b, Picture& out, std::string& error) {
  out = Picture();
  static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
  if (b.size() < 33 || std::memcmp(b.data(), sig, 8) != 0) {
    error = "not a PNG picture";
    return false;
  }
  uint32_t w = 0, h = 0;
  int depth = 0, type = -1, interlace = 0;
  std::vector<uint8_t> idat;
  size_t at = 8;
  while (at + 12 <= b.size()) {
    const uint32_t len = Get32(&b[at]);
    if (len > b.size() || at + 12 + len > b.size()) break;
    const char* t = reinterpret_cast<const char*>(&b[at + 4]);
    const uint8_t* d = &b[at + 8];
    if (std::memcmp(t, "IHDR", 4) == 0 && len >= 13) {
      w = Get32(d), h = Get32(d + 4), depth = d[8], type = d[9], interlace = d[12];
    } else if (std::memcmp(t, "IDAT", 4) == 0) {
      idat.insert(idat.end(), d, d + len);
    } else if (std::memcmp(t, "IEND", 4) == 0) {
      break;
    }
    at += 12 + len;
  }
  if (w == 0 || h == 0 || w > 8192 || h > 8192 || depth != 8 || (type != 2 && type != 6) || interlace != 0) {
    error = "a PNG this reader doesn't take (8-bit RGB / RGBA, not interlaced)";
    return false;
  }
  const size_t bpp = type == 2 ? 3 : 4, stride = static_cast<size_t>(w) * bpp;
  std::vector<uint8_t> raw((stride + 1) * h);
  uLongf size = static_cast<uLongf>(raw.size());
  if (uncompress(raw.data(), &size, idat.data(), static_cast<uLong>(idat.size())) != Z_OK || size != raw.size()) {
    error = "the picture's data is damaged";
    return false;
  }
  std::vector<uint8_t> prev(stride, 0), cur(stride);
  out.width = w, out.height = h;
  out.rgba.assign(static_cast<size_t>(w) * h * 4, 255);
  for (uint32_t y = 0; y < h; y++) {
    const uint8_t filter = raw[y * (stride + 1)];
    const uint8_t* src = &raw[y * (stride + 1) + 1];
    for (size_t i = 0; i < stride; i++) {
      const int a = i >= bpp ? cur[i - bpp] : 0, up = prev[i], c = i >= bpp ? prev[i - bpp] : 0;
      int v = src[i];
      switch (filter) {
      case 1: v += a; break;
      case 2: v += up; break;
      case 3: v += (a + up) / 2; break;
      case 4: {
        const int p = a + up - c, pa = std::abs(p - a), pb = std::abs(p - up), pc = std::abs(p - c);
        v += (pa <= pb && pa <= pc) ? a : (pb <= pc ? up : c);
        break;
      }
      default: break;
      }
      cur[i] = static_cast<uint8_t>(v);
    }
    for (uint32_t x = 0; x < w; x++) {
      uint8_t* q = out.rgba.data() + (static_cast<size_t>(y) * w + x) * 4;
      q[0] = cur[x * bpp], q[1] = cur[x * bpp + 1], q[2] = cur[x * bpp + 2];
      q[3] = 255;
    }
    prev = cur;
  }
  return true;
}

bool WriteFile(const fs::path& file, const Picture& picture, std::string& error) {
  const std::vector<uint8_t> png = EncodePng(picture);
  if (png.empty()) {
    error = "no picture to keep";
    return false;
  }
  std::error_code ec;
  fs::create_directories(file.parent_path(), ec);
  fs::path part = file;
  part += ".part";
  {
    std::ofstream out(part, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
    if (!out) {
      error = "could not write the picture file";
      return false;
    }
  }
  fs::rename(part, file, ec);
  if (ec) {
    fs::remove(file, ec);
    fs::rename(part, file, ec);
  }
  if (ec) {
    error = "could not move the picture file into place: " + ec.message();
    return false;
  }
  return true;
}

bool ReadFile(const fs::path& file, Picture& out, std::string& error) {
  std::ifstream in(file, std::ios::binary);
  if (!in) {
    error = "no picture";
    return false;
  }
  std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  return DecodePng(bytes, out, error);
}

}
