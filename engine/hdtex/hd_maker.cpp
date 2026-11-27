#include "hd_maker.h"

#include "hd_textures.h"
#include "update.h"

#include <png.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <fstream>
#include <thread>
#include <unordered_set>

#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

namespace fs = std::filesystem;

namespace engine::hdtex {

ToolInfo OfficialTool() {
  ToolInfo t;
  const std::string base = "https://github.com/xinntao/Real-ESRGAN/releases/download/v0.2.5.0/realesrgan-ncnn-vulkan-20220424-";
#if defined(_WIN32)
  t.system = "windows"; t.url = base + "windows.zip";
  t.sha256 = "abc02804e17982a3be33675e4d471e91ea374e65b70167abc09e31acb412802d";
  t.exeName = "realesrgan-ncnn-vulkan.exe"; t.bytes = 45474481;
#elif defined(__APPLE__)
  t.system = "mac"; t.url = base + "macos.zip"; t.exeName = "realesrgan-ncnn-vulkan";
  t.sha256 = "e0ad05580abfeb25f8d8fb55aaf7bedf552c375b5b4d9bd3c8d59764d2cc333a"; t.bytes = 51817124;
#else
  t.system = "linux"; t.url = base + "ubuntu.zip"; t.exeName = "realesrgan-ncnn-vulkan";
  t.sha256 = "e5aa6eb131234b87c0c51f82b89390f5e3e642b7b70f2b9bbe95b6a285a40c96"; t.bytes = 46931474;
#endif
  return t;
}

std::string ToolLicenceText() {
  return
      "The HD texture maker uses Real-ESRGAN (ncnn-vulkan), a separate program by its authors. It is downloaded from "
      "their official release page on GitHub, checked against a fixed checksum, and kept in your data folder. Nothing of "
      "it is part of this program.\n\n"
      "Licences of what is downloaded:\n"
      " - Real-ESRGAN: BSD 3-Clause License.\n"
      " - realesrgan-ncnn-vulkan and the ncnn library: MIT License.\n"
      " - The model files (realesrgan-x4plus and the others in the download): they come with no licence text of their "
      "own. They belong to the Real-ESRGAN project; you use them at your own risk and only for yourself.\n\n"
      "The program runs on your graphics card (Vulkan) and writes pictures made from YOUR copy of the game. Those "
      "pictures are for your own use: do not share them.";
}

bool ToolInstalled(const fs::path& folder, fs::path* exe) {
  const ToolInfo t = OfficialTool();
  std::error_code ec;
  const fs::path e = folder / t.exeName;
  if (!fs::is_regular_file(e, ec)) return false;
  if (!fs::is_regular_file(folder / "models" / "realesrgan-x4plus.param", ec) ||
      !fs::is_regular_file(folder / "models" / "realesrgan-x4plus.bin", ec))
    return false;
  if (exe) *exe = e;
  return true;
}

bool InstallTool(const fs::path& folder, std::string& error, const std::function<bool(uint64_t, uint64_t)>& progress) {
  const ToolInfo t = OfficialTool();
  if (t.url.empty() || t.sha256.empty()) {
    error = "There is no checked download of the upscaler for this system yet.";
    return false;
  }
  std::error_code ec;
  fs::create_directories(folder, ec);
  const fs::path zip = folder / "download.zip.part";
  update::FetchOptions opts;
  opts.maxBytes = 200ull * 1024 * 1024;
  opts.timeoutMs = 60000;
  opts.httpsOnly = true;
  opts.userAgent = "PokemonXDPC-HDMaker/1";
  opts.progress = progress;
  if (!update::FetchToFile(t.url, zip, opts, error)) return false;
  std::string hex;
  if (!update::Sha256File(zip, hex, error)) { fs::remove(zip, ec); return false; }
  if (hex != t.sha256) {
    fs::remove(zip, ec);
    error = "The downloaded file is not the expected one (checksum differs); it was thrown away.";
    return false;
  }
  const fs::path tmp = folder / "unpack.tmp";
  fs::remove_all(tmp, ec);
  if (!update::ExtractZip(zip, tmp, error)) { fs::remove(zip, ec); fs::remove_all(tmp, ec); return false; }
  fs::remove(zip, ec);
  fs::path root = tmp;
  if (!fs::exists(root / t.exeName, ec)) {
    for (fs::directory_iterator it(tmp, ec), end; !ec && it != end; it.increment(ec))
      if (it->is_directory(ec) && fs::exists(it->path() / t.exeName, ec)) { root = it->path(); break; }
  }
  if (!fs::exists(root / t.exeName, ec)) { fs::remove_all(tmp, ec); error = "The download holds no upscaler program."; return false; }
  for (fs::directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
    const fs::path to = folder / it->path().filename();
    fs::remove_all(to, ec);
    fs::rename(it->path(), to, ec);
    if (ec) { error = "Could not move the unpacked files: " + ec.message(); return false; }
  }
  fs::remove_all(tmp, ec);
#ifndef _WIN32
  fs::permissions(folder / t.exeName, fs::perms::owner_exec | fs::perms::group_exec | fs::perms::owner_read | fs::perms::owner_write, fs::perm_options::add, ec);
#endif
  if (!ToolInstalled(folder)) { error = "The unpacked download is incomplete."; return false; }
  return true;
}

bool ReadPng(const fs::path& file, Image& out) {
#ifdef _WIN32
  FILE* f = _wfopen(file.c_str(), L"rb");
#else
  FILE* f = std::fopen(file.c_str(), "rb");
#endif
  if (!f) return false;
  png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
  png_infop info = png ? png_create_info_struct(png) : nullptr;
  if (!png || !info) { if (png) png_destroy_read_struct(&png, nullptr, nullptr); std::fclose(f); return false; }
  std::vector<png_bytep> rows;
  bool ok = false;
  if (!setjmp(png_jmpbuf(png))) {
    png_init_io(png, f);
    png_read_info(png, info);
    png_set_expand(png);
    png_set_strip_16(png);
    png_set_gray_to_rgb(png);
    png_set_add_alpha(png, 0xFF, PNG_FILLER_AFTER);
    png_read_update_info(png, info);
    out.w = png_get_image_width(png, info);
    out.h = png_get_image_height(png, info);
    out.rgba.assign((size_t)out.w * out.h * 4, 0);
    rows.resize(out.h);
    for (uint32_t y = 0; y < out.h; y++) rows[y] = out.rgba.data() + (size_t)y * out.w * 4;
    png_read_image(png, rows.data());
    ok = true;
  }
  png_destroy_read_struct(&png, &info, nullptr);
  std::fclose(f);
  return ok;
}

bool WritePng(const fs::path& file, uint32_t w, uint32_t h, const uint8_t* rgba, bool hasAlpha, int level) {
#ifdef _WIN32
  FILE* f = _wfopen(file.c_str(), L"wb");
#else
  FILE* f = std::fopen(file.c_str(), "wb");
#endif
  if (!f) return false;
  png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
  png_infop info = png ? png_create_info_struct(png) : nullptr;
  if (!png || !info) { if (png) png_destroy_write_struct(&png, nullptr); std::fclose(f); return false; }
  bool ok = false;
  std::vector<uint8_t> rgb;
  if (!setjmp(png_jmpbuf(png))) {
    png_init_io(png, f);
    png_set_compression_level(png, level);
    png_set_IHDR(png, info, w, h, 8, hasAlpha ? PNG_COLOR_TYPE_RGBA : PNG_COLOR_TYPE_RGB, PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png, info);
    if (!hasAlpha) {
      rgb.resize((size_t)w * h * 3);
      for (size_t i = 0; i < (size_t)w * h; i++) { rgb[i * 3] = rgba[i * 4]; rgb[i * 3 + 1] = rgba[i * 4 + 1]; rgb[i * 3 + 2] = rgba[i * 4 + 2]; }
    }
    for (uint32_t y = 0; y < h; y++) {
      const uint8_t* row = hasAlpha ? rgba + (size_t)y * w * 4 : rgb.data() + (size_t)y * w * 3;
      png_write_row(png, row);
    }
    png_write_end(png, info);
    ok = true;
  }
  png_destroy_write_struct(&png, &info);
  std::fclose(f);
  if (!ok) { std::error_code ec; fs::remove(file, ec); }
  return ok;
}

void BleedColour(std::vector<uint8_t>& px, uint32_t w, uint32_t h) {
  std::vector<uint8_t> done((size_t)w * h, 0);
  std::deque<uint32_t> q;
  for (size_t i = 0; i < (size_t)w * h; i++)
    if (px[i * 4 + 3] > 0) { done[i] = 1; q.push_back((uint32_t)i); }
  if (q.empty() || q.size() == (size_t)w * h) return;
  while (!q.empty()) {
    const uint32_t i = q.front(); q.pop_front();
    const int x = (int)(i % w), y = (int)(i / w);
    static const int dx[4] = {1, -1, 0, 0}, dy[4] = {0, 0, 1, -1};
    for (int k = 0; k < 4; k++) {
      const int nx = x + dx[k], ny = y + dy[k];
      if (nx < 0 || ny < 0 || nx >= (int)w || ny >= (int)h) continue;
      const size_t j = (size_t)ny * w + nx;
      if (done[j]) continue;
      done[j] = 1;
      std::memcpy(&px[j * 4], &px[(size_t)i * 4], 3);
      q.push_back((uint32_t)j);
    }
  }
}

bool LooksTiling(const std::vector<uint8_t>& px, uint32_t w, uint32_t h) {
  if (w < 2 || h < 2) return false;
  double inner = 0, seam = 0;
  size_t n1 = 0, n2 = 0;
  for (uint32_t y = 0; y < h; y++)
    for (uint32_t x = 0; x + 1 < w; x++)
      for (int c = 0; c < 3; c++) { inner += std::abs((int)px[((size_t)y * w + x) * 4 + c] - (int)px[((size_t)y * w + x + 1) * 4 + c]); n1++; }
  for (uint32_t y = 0; y + 1 < h; y++)
    for (uint32_t x = 0; x < w; x++)
      for (int c = 0; c < 3; c++) { inner += std::abs((int)px[((size_t)y * w + x) * 4 + c] - (int)px[((size_t)(y + 1) * w + x) * 4 + c]); n1++; }
  for (uint32_t y = 0; y < h; y++)
    for (int c = 0; c < 3; c++) { seam += std::abs((int)px[((size_t)y * w) * 4 + c] - (int)px[((size_t)y * w + w - 1) * 4 + c]); n2++; }
  for (uint32_t x = 0; x < w; x++)
    for (int c = 0; c < 3; c++) { seam += std::abs((int)px[(size_t)x * 4 + c] - (int)px[((size_t)(h - 1) * w + x) * 4 + c]); n2++; }
  inner = inner / (double)std::max<size_t>(n1, 1) + 0.5;
  seam = seam / (double)std::max<size_t>(n2, 1);
  return seam <= 1.6 * inner;
}

namespace {
using Planes = std::vector<float>;

void Blur1(const float* in, float* out, int w, int h, int r, bool wrap, bool horizontal) {
  const int len = horizontal ? w : h, lines = horizontal ? h : w;
  const int stride = horizontal ? 1 : w, lineStride = horizontal ? w : 1;
  const float norm = 1.0f / (float)(2 * r + 1);
  for (int l = 0; l < lines; l++) {
    const float* src = in + (size_t)l * lineStride;
    float* dst = out + (size_t)l * lineStride;
    auto at = [&](int i) {
      if (wrap) { i %= len; if (i < 0) i += len; } else i = std::clamp(i, 0, len - 1);
      return src[(size_t)i * stride];
    };
    float acc = 0;
    for (int k = -r; k <= r; k++) acc += at(k);
    for (int i = 0; i < len; i++) {
      dst[(size_t)i * stride] = acc * norm;
      acc += at(i + r + 1) - at(i - r);
    }
  }
}
void Blur(Planes& p, int w, int h, int r, bool wrap) {
  std::vector<float> tmp((size_t)w * h);
  for (int c = 0; c < 3; c++) {
    float* pl = p.data() + (size_t)c * w * h;
    for (int pass = 0; pass < 2; pass++) {
      Blur1(pl, tmp.data(), w, h, r, wrap, true);
      Blur1(tmp.data(), pl, w, h, r, wrap, false);
    }
  }
}
Planes ShrinkPlanes(const std::vector<uint8_t>& px, uint32_t w, uint32_t h, uint32_t s) {
  Planes out((size_t)3 * w * h);
  const uint32_t W = w * s;
  for (uint32_t y = 0; y < h; y++)
    for (uint32_t x = 0; x < w; x++)
      for (int c = 0; c < 3; c++) {
        uint32_t sum = 0;
        for (uint32_t j = 0; j < s; j++)
          for (uint32_t i = 0; i < s; i++) sum += px[(((size_t)y * s + j) * W + x * s + i) * 4 + c];
        out[(size_t)c * w * h + (size_t)y * w + x] = (float)sum / (float)(s * s);
      }
  return out;
}
Planes PlanesOf(const std::vector<uint8_t>& px, uint32_t w, uint32_t h) {
  Planes out((size_t)3 * w * h);
  for (size_t i = 0; i < (size_t)w * h; i++)
    for (int c = 0; c < 3; c++) out[(size_t)c * w * h + i] = px[i * 4 + c];
  return out;
}
}

double ColourMatch(std::vector<uint8_t>& result, const std::vector<uint8_t>& original, uint32_t w, uint32_t h, uint32_t s,
                   bool wrap, int radius) {
  Planes rd = ShrinkPlanes(result, w, h, s);
  Planes od = PlanesOf(original, w, h);
  const int r = std::max(1, std::min<int>(radius, (int)std::min(w, h) / 4));
  Blur(rd, (int)w, (int)h, r, wrap);
  Blur(od, (int)w, (int)h, r, wrap);
  double change = 0;
  for (size_t i = 0; i < od.size(); i++) { od[i] -= rd[i]; change += std::abs(od[i]); }
  change /= (double)od.size();
  const uint32_t W = w * s, H = h * s;
  for (uint32_t Y = 0; Y < H; Y++) {
    const float fy = ((float)Y + 0.5f) / (float)s - 0.5f;
    int y0 = (int)std::floor(fy);
    const float ty = fy - (float)y0;
    int y1 = y0 + 1;
    if (wrap) { y0 = (y0 % (int)h + (int)h) % (int)h; y1 = (y1 % (int)h + (int)h) % (int)h; }
    else { y0 = std::clamp(y0, 0, (int)h - 1); y1 = std::clamp(y1, 0, (int)h - 1); }
    for (uint32_t X = 0; X < W; X++) {
      const float fx = ((float)X + 0.5f) / (float)s - 0.5f;
      int x0 = (int)std::floor(fx);
      const float tx = fx - (float)x0;
      int x1 = x0 + 1;
      if (wrap) { x0 = (x0 % (int)w + (int)w) % (int)w; x1 = (x1 % (int)w + (int)w) % (int)w; }
      else { x0 = std::clamp(x0, 0, (int)w - 1); x1 = std::clamp(x1, 0, (int)w - 1); }
      uint8_t* p = &result[((size_t)Y * W + X) * 4];
      for (int c = 0; c < 3; c++) {
        const float* pl = od.data() + (size_t)c * w * h;
        const float d = (pl[(size_t)y0 * w + x0] * (1 - tx) + pl[(size_t)y0 * w + x1] * tx) * (1 - ty) +
                        (pl[(size_t)y1 * w + x0] * (1 - tx) + pl[(size_t)y1 * w + x1] * tx) * ty;
        p[c] = (uint8_t)std::clamp((int)std::lround((float)p[c] + d), 0, 255);
      }
    }
  }
  return change;
}

double MeanColourDifference(const std::vector<uint8_t>& result, const std::vector<uint8_t>& original, uint32_t w, uint32_t h,
                            uint32_t s) {
  const Planes rd = ShrinkPlanes(result, w, h, s);
  double sum = 0;
  for (size_t i = 0; i < (size_t)w * h; i++)
    for (int c = 0; c < 3; c++) sum += std::abs(rd[(size_t)c * w * h + i] - (float)original[i * 4 + c]);
  return sum / (double)((size_t)w * h * 3);
}

namespace {
int RunProgram(const fs::path& exe, const std::vector<std::string>& args, const std::function<bool()>& tick, std::string& log) {
#ifdef _WIN32
  auto quote = [](const std::wstring& a) { return L"\"" + a + L"\""; };
  auto wide = [](const std::string& s) { return fs::u8path(s).wstring(); };
  std::wstring cmd = quote(exe.wstring());
  for (const std::string& a : args) cmd += L" " + quote(wide(a));
  STARTUPINFOW si{};
  si.cb = sizeof si;
  PROCESS_INFORMATION pi{};
  std::vector<wchar_t> buf(cmd.begin(), cmd.end());
  buf.push_back(0);
  if (!CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW | BELOW_NORMAL_PRIORITY_CLASS, nullptr,
                      exe.parent_path().c_str(), &si, &pi)) {
    log = "could not start " + exe.string();
    return -1;
  }
  for (;;) {
    if (WaitForSingleObject(pi.hProcess, 500) == WAIT_OBJECT_0) break;
    if (tick && !tick()) { TerminateProcess(pi.hProcess, 1); WaitForSingleObject(pi.hProcess, 5000); CloseHandle(pi.hThread); CloseHandle(pi.hProcess); return -1; }
  }
  DWORD code = 1;
  GetExitCodeProcess(pi.hProcess, &code);
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  return (int)code;
#else
  std::vector<std::string> store;
  store.push_back(exe.string());
  for (const std::string& a : args) store.push_back(a);
  std::vector<char*> argv;
  for (std::string& s : store) argv.push_back(s.data());
  argv.push_back(nullptr);
  pid_t pid;
  if (posix_spawn(&pid, exe.c_str(), nullptr, nullptr, argv.data(), environ) != 0) { log = "could not start " + exe.string(); return -1; }
  int status = 0;
  for (;;) {
    const pid_t r = waitpid(pid, &status, WNOHANG);
    if (r == pid) break;
    if (r < 0) return -1;
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    if (tick && !tick()) { kill(pid, SIGTERM); waitpid(pid, &status, 0); return -1; }
  }
  return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
#endif
}

size_t CountFiles(const fs::path& dir) {
  std::error_code ec;
  size_t n = 0;
  for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) n++;
  return n;
}

constexpr uint32_t kPad = 8;
std::string U8(const fs::path& p) {
  const auto u = p.u8string();
  return std::string(u.begin(), u.end());
}

struct Item {
  std::string stem;
  uint32_t w, h;
  bool hasAlpha, wrap;
  std::vector<uint8_t> original;
};
}

MakerResult MakePack(const FileSource& source, const MakerOptions& options, const std::function<bool(const MakerProgress&)>& progress) {
  MakerOptions o = options;
  {
    std::error_code abs;
    o.toolExe = fs::absolute(o.toolExe, abs);
    o.models = fs::absolute(o.models, abs);
    o.work = fs::absolute(o.work, abs);
    o.pack = fs::absolute(o.pack, abs);
  }
  MakerResult res;
  const auto t0 = std::chrono::steady_clock::now();
  std::error_code ec;
  fs::create_directories(o.pack, ec);
  fs::remove_all(o.work, ec);
  fs::create_directories(o.work / "in_rgb", ec);
  fs::create_directories(o.work / "in_a", ec);
  fs::create_directories(o.work / "out_rgb", ec);
  fs::create_directories(o.work / "out_a", ec);
  const uint32_t S = o.scale == 4 ? 4 : 2;
  auto report = [&](const char* phase, uint64_t done, uint64_t total, const std::string& detail) {
    MakerProgress p;
    p.phase = phase; p.done = done; p.total = total; p.detail = detail;
    return !progress || progress(p);
  };
  const std::unordered_set<std::string> onlySet(o.only.begin(), o.only.end());
  auto onlyOk = [&](const std::string& stem) {
    if (onlySet.empty()) return true;
    if (onlySet.count(stem)) return true;
    const size_t m = stem.find("_m_");
    return m != std::string::npos && onlySet.count(stem.substr(0, m) + "_" + stem.substr(m + 3)) > 0;
  };
  auto wanted = [&](TexClass c) {
    switch (c) {
      case TexClass::Surface3D: return true;
      case TexClass::Character: case TexClass::AlphaDither: return o.includeCharacters;
      case TexClass::Effect: return o.includeEffects;
      case TexClass::Interface: return o.includeInterface;
      case TexClass::TextOrMask: return false;
    }
    return false;
  };

  {
    std::ofstream skip(o.pack / "maker_skiplist.tsv", std::ios::binary);
    skip << "name\tclass\tsource\tarchive\tentry\tsize\tformat\tupscaled\n";
    bool stopped = false;
    const bool walked = ForEachDiscTexture(
        source,
        [&](DiscTexture& t) {
          const bool small = std::min(t.width, t.height) < o.minSide;
          const bool want = wanted(t.cls) && !small && onlyOk(t.stem);
          skip << t.stem << '\t' << ClassName(t.cls) << '\t' << SourceName(t.source) << '\t' << t.fsys << '\t' << t.member << '\t'
               << t.width << 'x' << t.height << '\t' << t.format << '\t' << (want ? "yes" : (small ? "no(too small)" : "no")) << '\n';
          if (want) res.candidates++;
          else if (small && wanted(t.cls)) res.tooSmall++;
          else res.skippedByClass[(int)t.cls]++;
        },
        [&](uint32_t d, uint32_t n) {
          if (!report("counting", d, n, "")) { stopped = true; return false; }
          return true;
        },
        nullptr);
    if (stopped) { res.cancelled = true; res.error = "stopped"; return res; }
    if (!walked) { res.error = "The disc has no game archives I can read."; return res; }
  }
  const uint64_t total = o.limit ? std::min<uint64_t>(o.limit, res.candidates) : res.candidates;

  std::vector<Item> batch;
  uint64_t processed = 0;
  bool stop = false;
  int failedBatches = 0;
  std::string lastDetail;

  auto flush = [&]() -> bool {
    if (batch.empty()) return true;
    bool anyAlpha = false;
    for (const Item& it : batch) anyAlpha |= it.hasAlpha;
    fs::remove_all(o.work / "out_rgb", ec); fs::create_directories(o.work / "out_rgb", ec);
    fs::remove_all(o.work / "out_a", ec); fs::create_directories(o.work / "out_a", ec);
    const uint64_t base = processed;
    auto tick = [&]() {
      const size_t seen = CountFiles(o.work / "out_rgb");
      if (!report("making", base + std::min<size_t>(seen, batch.size()), total, lastDetail)) { stop = true; return false; }
      return true;
    };
    auto run = [&](const char* in, const char* out) {
      std::vector<std::string> args = {"-i", U8(o.work / in), "-o", U8(o.work / out), "-n", o.model, "-m", U8(o.models),
                                       "-s", "4", "-f", "png"};
      if (!o.gpu.empty()) { args.push_back("-g"); args.push_back(o.gpu); }
      std::string log;
      return RunProgram(o.toolExe, args, tick, log);
    };
    int code = run("in_rgb", "out_rgb");
    if (code == 0 && anyAlpha) code = run("in_a", "out_a");
    if (stop) return false;
    if (code != 0) {
      res.failed += batch.size();
      failedBatches++;
    } else {
      failedBatches = 0;
      for (Item& it : batch) {
        Image rgbOut, aOut;
        if (!ReadPng(o.work / "out_rgb" / (it.stem + ".png"), rgbOut) || rgbOut.w != (it.w + 2 * kPad) * 4 || rgbOut.h != (it.h + 2 * kPad) * 4) { res.failed++; continue; }
        if (it.hasAlpha && (!ReadPng(o.work / "out_a" / (it.stem + ".png"), aOut) || aOut.w != rgbOut.w)) { res.failed++; continue; }
        const uint32_t cw = it.w * S, ch = it.h * S;
        std::vector<uint8_t> out((size_t)cw * ch * 4, 255);
        for (uint32_t y = 0; y < ch; y++)
          for (uint32_t x = 0; x < cw; x++) {
            uint8_t* d = &out[((size_t)y * cw + x) * 4];
            if (S == 4) {
              const size_t si = ((size_t)(y + kPad * 4) * rgbOut.w + x + kPad * 4) * 4;
              d[0] = rgbOut.rgba[si]; d[1] = rgbOut.rgba[si + 1]; d[2] = rgbOut.rgba[si + 2];
              if (it.hasAlpha) d[3] = aOut.rgba[si];
            } else {
              uint32_t sum[4] = {0, 0, 0, 0};
              for (uint32_t j = 0; j < 2; j++)
                for (uint32_t i = 0; i < 2; i++) {
                  const size_t si = ((size_t)(y * 2 + j + kPad * 4) * rgbOut.w + x * 2 + i + kPad * 4) * 4;
                  sum[0] += rgbOut.rgba[si]; sum[1] += rgbOut.rgba[si + 1]; sum[2] += rgbOut.rgba[si + 2];
                  if (it.hasAlpha) sum[3] += aOut.rgba[si];
                }
              d[0] = (uint8_t)((sum[0] + 2) / 4); d[1] = (uint8_t)((sum[1] + 2) / 4); d[2] = (uint8_t)((sum[2] + 2) / 4);
              if (it.hasAlpha) d[3] = (uint8_t)((sum[3] + 2) / 4);
            }
          }
        if (o.colourMatch) res.colourChange += ColourMatch(out, it.original, it.w, it.h, S, it.wrap);
        if (WritePng(o.pack / (it.stem + ".png"), cw, ch, out.data(), it.hasAlpha, 6)) res.made++;
        else res.failed++;
      }
    }
    processed += batch.size();
    batch.clear();
    fs::remove_all(o.work / "in_rgb", ec); fs::create_directories(o.work / "in_rgb", ec);
    fs::remove_all(o.work / "in_a", ec); fs::create_directories(o.work / "in_a", ec);
    return report("making", processed, total, lastDetail) && failedBatches < 3;
  };

  uint64_t seenCandidates = 0;
  const bool walked = ForEachDiscTexture(
      source,
      [&](DiscTexture& t) {
        if (stop || failedBatches >= 3) return;
        if (!wanted(t.cls) || std::min(t.width, t.height) < o.minSide || !onlyOk(t.stem)) return;
        if (o.limit && seenCandidates >= o.limit) return;
        seenCandidates++;
        if (fs::exists(o.pack / (t.stem + ".png"), ec)) { res.resumed++; processed++; return; }
        std::vector<uint8_t> px;
        if (!DecodeRgba(t, px)) { res.failed++; processed++; return; }
        bool hasAlpha = false;
        for (size_t i = 3; i < px.size(); i += 4) if (px[i] != 255) { hasAlpha = true; break; }
        std::vector<uint8_t> alphaOf(px.size() / 4);
        for (size_t i = 0; i < alphaOf.size(); i++) alphaOf[i] = px[i * 4 + 3];
        BleedColour(px, t.width, t.height);
        const bool wrap = LooksTiling(px, t.width, t.height);
        const uint32_t pw = t.width + 2 * kPad, ph = t.height + 2 * kPad;
        auto padded = [&](bool alphaPlane) {
          std::vector<uint8_t> out((size_t)pw * ph * 3);
          for (uint32_t y = 0; y < ph; y++)
            for (uint32_t x = 0; x < pw; x++) {
              int sx = (int)x - (int)kPad, sy = (int)y - (int)kPad;
              if (wrap) { sx = ((sx % (int)t.width) + (int)t.width) % (int)t.width; sy = ((sy % (int)t.height) + (int)t.height) % (int)t.height; }
              else { sx = std::clamp(sx, 0, (int)t.width - 1); sy = std::clamp(sy, 0, (int)t.height - 1); }
              const uint8_t* s = &px[((size_t)sy * t.width + sx) * 4];
              uint8_t* d = &out[((size_t)y * pw + x) * 3];
              if (alphaPlane) d[0] = d[1] = d[2] = alphaOf[(size_t)sy * t.width + sx];
              else { d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; }
            }
          return out;
        };
        const std::vector<uint8_t> rgbIn = padded(false);
        auto expand = [&](const std::vector<uint8_t>& rgb) {
          std::vector<uint8_t> out((size_t)pw * ph * 4, 255);
          for (size_t i = 0; i < (size_t)pw * ph; i++) std::memcpy(&out[i * 4], &rgb[i * 3], 3);
          return out;
        };
        bool okw = WritePng(o.work / "in_rgb" / (t.stem + ".png"), pw, ph, expand(rgbIn).data(), false, 1);
        if (okw && hasAlpha) okw = WritePng(o.work / "in_a" / (t.stem + ".png"), pw, ph, expand(padded(true)).data(), false, 1);
        if (!okw) { res.failed++; processed++; return; }
        Item it;
        it.stem = t.stem; it.w = t.width; it.h = t.height; it.hasAlpha = hasAlpha; it.wrap = wrap;
        it.original = px;
        lastDetail = t.stem;
        batch.push_back(std::move(it));
        if (batch.size() >= o.batch && !flush()) stop = true;
      },
      [&](uint32_t, uint32_t) { return !stop; }, nullptr);
  if (!stop && failedBatches < 3) flush();
  (void)walked;
  fs::remove_all(o.work, ec);
  res.cancelled = stop;
  if (failedBatches >= 3) res.error = "The upscaler failed three times in a row (is Vulkan available on this PC?).";
  else if (stop) res.error = "stopped";
  res.ok = !stop && failedBatches < 3;
  if (res.made) res.colourChange /= (double)res.made;
  for (fs::recursive_directory_iterator it(o.pack, ec), end; !ec && it != end; it.increment(ec)) {
    std::error_code e2;
    if (it->is_regular_file(e2)) res.packBytes += it->file_size(e2);
  }
  res.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  std::ofstream st(o.pack / "maker_state.txt", std::ios::binary);
  st << "scale " << S << "\nmodel " << o.model << "\nmade " << res.made << "\nresumed " << res.resumed << "\nfailed " << res.failed
     << "\ncandidates " << res.candidates << "\ncomplete " << (res.ok && res.failed == 0 ? 1 : 0) << "\nseconds " << res.seconds << "\n";
  return res;
}

}
