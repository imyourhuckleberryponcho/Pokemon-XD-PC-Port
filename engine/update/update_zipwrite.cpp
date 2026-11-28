#include "update_internal.h"

#include <zlib.h>

#include <algorithm>
#include <cstdint>
#include <fstream>

namespace engine::update {

namespace fs = std::filesystem;
using detail::PathOf;
using detail::Utf8;

namespace {

void Put16(std::vector<uint8_t>& v, uint32_t x) {
  v.push_back(static_cast<uint8_t>(x));
  v.push_back(static_cast<uint8_t>(x >> 8));
}
void Put32(std::vector<uint8_t>& v, uint32_t x) {
  Put16(v, x & 0xFFFF);
  Put16(v, x >> 16);
}

constexpr uint64_t kMaxOneFile = 1024ull * 1024 * 1024;
constexpr uint32_t kDosDate = (0 << 9) | (1 << 5) | 1;

struct Item {
  std::string name;
  fs::path path;
  uint64_t size = 0;
};

}

bool ZipFolderEntries(const fs::path& root, const std::vector<std::string>& names, const fs::path& zip, std::string& error,
                      const std::function<void(uint64_t, uint64_t)>& progress, const std::function<bool()>& stop) {
  std::error_code ec;
  std::vector<Item> items;
  uint64_t total = 0;
  for (const std::string& top : names) {
    const fs::path start = root / PathOf(top);
    if (fs::is_regular_file(start, ec)) {
      items.push_back({top, start, static_cast<uint64_t>(fs::file_size(start, ec))});
    } else if (fs::is_directory(start, ec)) {
      for (fs::recursive_directory_iterator it(start, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end;
           it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        std::string rel = Utf8(it->path().lexically_relative(root));
        std::replace(rel.begin(), rel.end(), '\\', '/');
        items.push_back({rel, it->path(), static_cast<uint64_t>(it->file_size(ec))});
      }
    } else {
      error = Fmt(Tr("%s is not there"), top.c_str());
      return false;
    }
  }
  if (items.empty()) {
    error = Tr("nothing to put in the zip");
    return false;
  }
  for (const Item& i : items) {
    total += i.size;
    if (i.size > kMaxOneFile) {
      error = Fmt(Tr("a file is too large for the zip: %s"), i.name.c_str());
      return false;
    }
  }
  if (items.size() >= 0xFFFF || total >= 0xF0000000ull) {
    error = Tr("these packs are too large for one zip (the phone takes zips under 4 GB): export fewer packs at a time");
    return false;
  }

  const fs::path tmp = zip.parent_path() / PathOf(Utf8(zip.filename()) + ".part");
  std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
  if (!out) {
    error = Fmt(Tr("the zip couldn't be written: %s"), Utf8(zip).c_str());
    return false;
  }
  struct Central {
    std::string name;
    uint32_t crc, packed, size, offset;
    uint16_t method;
  };
  std::vector<Central> central;
  uint64_t pos = 0, done = 0;
  auto write = [&](const std::vector<uint8_t>& v) {
    out.write(reinterpret_cast<const char*>(v.data()), static_cast<std::streamsize>(v.size()));
    pos += v.size();
  };
  auto fail = [&](const std::string& why) {
    error = why;
    out.close();
    fs::remove(tmp, ec);
    return false;
  };
  for (const Item& it : items) {
    if (stop && stop()) return fail("stopped");
    std::vector<uint8_t> data(static_cast<size_t>(it.size));
    {
      std::ifstream in(it.path, std::ios::binary);
      if (!in || (!data.empty() && !in.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size())))) {
        return fail(Fmt(Tr("a file couldn't be read: %s"), it.name.c_str()));
      }
    }
    Central c;
    c.name = it.name;
    c.size = static_cast<uint32_t>(data.size());
    c.crc = static_cast<uint32_t>(crc32(0L, data.empty() ? Z_NULL : data.data(), static_cast<uInt>(data.size())));
    c.offset = static_cast<uint32_t>(pos);
    c.method = 0;
    std::vector<uint8_t> packed;
    if (!data.empty()) {
      z_stream z{};
      deflateInit2(&z, 6, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY);
      packed.resize(deflateBound(&z, static_cast<uLong>(data.size())));
      z.next_in = data.data();
      z.avail_in = static_cast<uInt>(data.size());
      z.next_out = packed.data();
      z.avail_out = static_cast<uInt>(packed.size());
      deflate(&z, Z_FINISH);
      packed.resize(z.total_out);
      deflateEnd(&z);
      if (packed.size() < data.size()) c.method = 8;
    }
    const std::vector<uint8_t>& body = c.method == 8 ? packed : data;
    c.packed = static_cast<uint32_t>(body.size());
    std::vector<uint8_t> head;
    Put32(head, 0x04034B50);
    Put16(head, 20);
    Put16(head, 0x0800);
    Put16(head, c.method);
    Put16(head, 0);
    Put16(head, kDosDate);
    Put32(head, c.crc);
    Put32(head, c.packed);
    Put32(head, c.size);
    Put16(head, static_cast<uint32_t>(it.name.size()));
    Put16(head, 0);
    head.insert(head.end(), it.name.begin(), it.name.end());
    write(head);
    write(body);
    if (!out) return fail(Fmt(Tr("the zip couldn't be written: %s"), Utf8(zip).c_str()));
    central.push_back(std::move(c));
    done += it.size;
    if (progress) progress(done, total);
  }
  const uint32_t dirStart = static_cast<uint32_t>(pos);
  std::vector<uint8_t> dir;
  for (const Central& c : central) {
    Put32(dir, 0x02014B50);
    Put16(dir, 20);
    Put16(dir, 20);
    Put16(dir, 0x0800);
    Put16(dir, c.method);
    Put16(dir, 0);
    Put16(dir, kDosDate);
    Put32(dir, c.crc);
    Put32(dir, c.packed);
    Put32(dir, c.size);
    Put16(dir, static_cast<uint32_t>(c.name.size()));
    Put16(dir, 0);
    Put16(dir, 0);
    Put16(dir, 0);
    Put16(dir, 0);
    Put32(dir, 0);
    Put32(dir, c.offset);
    dir.insert(dir.end(), c.name.begin(), c.name.end());
  }
  write(dir);
  std::vector<uint8_t> end;
  Put32(end, 0x06054B50);
  Put16(end, 0);
  Put16(end, 0);
  Put16(end, static_cast<uint32_t>(central.size()));
  Put16(end, static_cast<uint32_t>(central.size()));
  Put32(end, static_cast<uint32_t>(dir.size()));
  Put32(end, dirStart);
  Put16(end, 0);
  write(end);
  out.close();
  if (!out) return fail(Fmt(Tr("the zip couldn't be written: %s"), Utf8(zip).c_str()));
  fs::remove(zip, ec);
  fs::rename(tmp, zip, ec);
  if (ec) {
    fs::remove(tmp, ec);
    error = Fmt(Tr("the zip couldn't be written: %s"), Utf8(zip).c_str());
    return false;
  }
  return true;
}

}
