#include "port_zip.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

#include <zlib.h>

namespace {

namespace fs = std::filesystem;

uint32_t Crc32(const std::string& data) {
  uLong crc = crc32(0L, Z_NULL, 0);
  for (size_t at = 0; at < data.size();) {
    const uInt n = static_cast<uInt>(std::min<size_t>(data.size() - at, 1u << 30));
    crc = crc32(crc, reinterpret_cast<const Bytef*>(data.data() + at), n);
    at += n;
  }
  return static_cast<uint32_t>(crc);
}

bool CompressedAlready(const std::string& name) {
  const size_t dot = name.find_last_of('.');
  if (dot == std::string::npos) {
    return false;
  }
  std::string ext = name.substr(dot + 1);
  for (char& c : ext) {
    c = static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
  }
  for (const char* known : {"png", "jpg", "jpeg", "webp", "zip", "7z", "gz", "xz", "zst", "mp4", "ogg"}) {
    if (ext == known) {
      return true;
    }
  }
  return false;
}

bool Deflate(const std::string& data, std::string& out) {
  if (data.size() < 64 || data.size() >= 0xFFFFFFFFull) {
    return false;
  }
  z_stream z{};
  if (deflateInit2(&z, Z_BEST_COMPRESSION, Z_DEFLATED, -MAX_WBITS, 9, Z_DEFAULT_STRATEGY) != Z_OK) {
    return false;
  }
  out.resize(deflateBound(&z, static_cast<uLong>(data.size())));
  z.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(data.data()));
  z.avail_in = static_cast<uInt>(data.size());
  z.next_out = reinterpret_cast<Bytef*>(out.data());
  z.avail_out = static_cast<uInt>(out.size());
  const int result = deflate(&z, Z_FINISH);
  out.resize(z.total_out);
  deflateEnd(&z);
  return result == Z_STREAM_END && out.size() < data.size();
}

void Put16(std::string& out, uint32_t v) {
  out.push_back(static_cast<char>(v & 0xFF));
  out.push_back(static_cast<char>((v >> 8) & 0xFF));
}
void Put32(std::string& out, uint32_t v) {
  Put16(out, v & 0xFFFF);
  Put16(out, v >> 16);
}

struct Entry {
  std::string name;
  uint32_t crc = 0, size = 0, packed = 0, offset = 0;
  uint16_t time = 0, date = 0, method = 0;
};

void Common(std::string& out, const Entry& e) {
  Put16(out, 20);
  Put16(out, 1 << 11);
  Put16(out, e.method);
  Put16(out, e.time);
  Put16(out, e.date);
  Put32(out, e.crc);
  Put32(out, e.packed);
  Put32(out, e.size);
  Put16(out, static_cast<uint32_t>(e.name.size()));
  Put16(out, 0);
}

}

namespace port_zip {

void Time(const fs::path& path, uint16_t* time, uint16_t* date) {
  std::error_code ec;
  const auto written = fs::last_write_time(path, ec);
  std::time_t t = std::time(nullptr);
  if (!ec) {
#ifdef _LIBCPP_VERSION
    t = std::chrono::system_clock::to_time_t(std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        std::chrono::file_clock::to_sys(written)));
#else
    t = std::chrono::system_clock::to_time_t(std::chrono::clock_cast<std::chrono::system_clock>(written));
#endif
  }
  std::tm local{};
#ifdef _WIN32
  localtime_s(&local, &t);
#else
  localtime_r(&t, &local);
#endif
  const int year = std::max(local.tm_year + 1900, 1980);
  *time = static_cast<uint16_t>((local.tm_hour << 11) | (local.tm_min << 5) | (local.tm_sec / 2));
  *date = static_cast<uint16_t>(((year - 1980) << 9) | ((local.tm_mon + 1) << 5) | local.tm_mday);
}

bool Store(const fs::path& zip, const std::vector<File>& files) {
  std::error_code ec;
  std::ofstream out(zip, std::ios::binary | std::ios::trunc);
  if (!out) {
    return false;
  }
  std::vector<Entry> entries;
  uint64_t offset = 0;
  bool ok = true;
  for (const File& f : files) {
    if (f.data.size() >= 0xFFFFFFFFull || offset >= 0xFFFFFFFFull) {
      ok = false;
      break;
    }
    Entry e;
    e.name = f.name;
    e.crc = Crc32(f.data);
    e.size = static_cast<uint32_t>(f.data.size());
    e.offset = static_cast<uint32_t>(offset);
    e.time = f.time;
    e.date = f.date;
    std::string deflated;
    const bool packed = !CompressedAlready(f.name) && Deflate(f.data, deflated);
    const std::string& body = packed ? deflated : f.data;
    e.method = packed ? 8 : 0;
    e.packed = static_cast<uint32_t>(body.size());
    std::string header;
    Put32(header, 0x04034b50);
    Common(header, e);
    header += e.name;
    out.write(header.data(), static_cast<std::streamsize>(header.size()));
    out.write(body.data(), static_cast<std::streamsize>(body.size()));
    offset += header.size() + body.size();
    entries.push_back(std::move(e));
  }
  if (ok && offset < 0xFFFFFFFFull) {
    std::string directory;
    for (const Entry& e : entries) {
      Put32(directory, 0x02014b50);
      Put16(directory, 20);
      Common(directory, e);
      Put16(directory, 0);
      Put16(directory, 0);
      Put16(directory, 0);
      Put32(directory, 0);
      Put32(directory, e.offset);
      directory += e.name;
    }
    std::string end;
    Put32(end, 0x06054b50);
    Put16(end, 0);
    Put16(end, 0);
    Put16(end, static_cast<uint32_t>(entries.size()));
    Put16(end, static_cast<uint32_t>(entries.size()));
    Put32(end, static_cast<uint32_t>(directory.size()));
    Put32(end, static_cast<uint32_t>(offset));
    Put16(end, 0);
    out.write(directory.data(), static_cast<std::streamsize>(directory.size()));
    out.write(end.data(), static_cast<std::streamsize>(end.size()));
  } else {
    ok = false;
  }
  out.close();
  if (!ok || out.fail()) {
    fs::remove(zip, ec);
    return false;
  }
  return true;
}

}

extern "C" bool PortZip_StoreFolder(const wchar_t* folder, const wchar_t* zipPath) {
  std::error_code ec;
  std::vector<fs::path> paths;
  for (const auto& entry : fs::directory_iterator(fs::path(folder), ec)) {
    if (entry.is_regular_file(ec)) {
      paths.push_back(entry.path());
    }
  }
  if (ec) {
    return false;
  }
  std::sort(paths.begin(), paths.end());
  std::vector<port_zip::File> files;
  for (const fs::path& path : paths) {
    std::ifstream in(path, std::ios::binary);
    port_zip::File f;
    f.data.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    if (!in.good() && !in.eof()) {
      return false;
    }
    const std::u8string u8 = path.filename().u8string();
    f.name.assign(reinterpret_cast<const char*>(u8.data()), u8.size());
    port_zip::Time(path, &f.time, &f.date);
    files.push_back(std::move(f));
  }
  return port_zip::Store(fs::path(zipPath), files);
}
