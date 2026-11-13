#include "launcher_extra.h"

#include "launcher_sys.h"

#include <mods.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <sstream>

namespace launcher::extra {

namespace fs = std::filesystem;

namespace {

std::string Utf8(const fs::path& p) {
  const std::u8string u8 = p.u8string();
  return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
}

std::string Lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return s;
}

std::string Trim(std::string_view s) {
  size_t a = 0, b = s.size();
  while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) a++;
  while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) b--;
  return std::string(s.substr(a, b - a));
}

bool Word(std::string_view& rest, std::string& out) {
  size_t a = 0;
  while (a < rest.size() && std::isspace(static_cast<unsigned char>(rest[a]))) a++;
  size_t b = a;
  while (b < rest.size() && !std::isspace(static_cast<unsigned char>(rest[b]))) b++;
  if (a == b) return false;
  out.assign(rest.substr(a, b - a));
  rest.remove_prefix(b);
  return true;
}

bool Number(std::string_view& rest, uint64_t& out) {
  std::string w;
  if (!Word(rest, w) || w.empty() || !std::all_of(w.begin(), w.end(), [](unsigned char c) { return std::isdigit(c) != 0; }) ||
      w.size() > 19) {
    return false;
  }
  out = std::strtoull(w.c_str(), nullptr, 10);
  return true;
}

}

Line ParseLine(std::string_view text) {
  Line line;
  std::string_view rest = text;
  std::string verb;
  if (!Word(rest, verb)) return line;
  if (verb == "progress") {
    Line l;
    if (!Number(rest, l.done) || !Number(rest, l.total)) return line;
    l.kind = Line::Kind::Progress;
    l.name = Trim(rest);
    return l;
  }
  if (verb == "done") {
    Line l;
    if (!Number(rest, l.converted) || !Number(rest, l.keptOriginal) || !Number(rest, l.totalBytes)) return line;
    l.kind = Line::Kind::Done;
    return l;
  }
  if (verb == "error") {
    line.kind = Line::Kind::Error;
    line.message = Trim(rest);
    return line;
  }
  return line;
}

std::string ParseVersionOutput(std::string_view output) {
  std::string_view rest = output;
  std::string w;
  while (Word(rest, w)) {
    size_t start = (w.size() > 1 && (w[0] == 'v' || w[0] == 'V')) ? 1 : 0;
    if (start < w.size() && std::isdigit(static_cast<unsigned char>(w[start]))) {
      std::string v = w.substr(start);
      while (!v.empty() && (v.back() == ',' || v.back() == ')' || v.back() == ';')) v.pop_back();
      return v;
    }
  }
  return {};
}

const Source* FindSource(const std::vector<Source>& sources, std::string_view id) {
  for (const Source& s : sources) {
    if (Lower(s.id) == Lower(std::string(id))) return &s;
  }
  return nullptr;
}

bool AcceptsGameId(const Source& source, std::string_view gameId) {
  return std::any_of(source.gameIds.begin(), source.gameIds.end(),
                     [&](const std::string& g) { return Lower(g) == Lower(std::string(gameId)); });
}

fs::path ConverterPath(const Source& source, const fs::path& programFolder) {
  if (!source.converterOverrideEnv.empty()) {
    const fs::path over = sys::EnvPath(source.converterOverrideEnv.c_str());
    if (!over.empty()) return over;
  }
  fs::path folder = programFolder;
  if (!source.converterFolder.empty()) folder /= source.converterFolder;
#if defined(__APPLE__)
  {
    std::error_code ec;
    const fs::path real = fs::canonical(folder, ec);
    if (!ec) folder = real;
  }
#endif
  return folder / sys::ProgramName(source.converterStem);
}

std::vector<std::string> ConverterArguments(const Source& source, const std::string& disc, const std::string& gameDisc,
                                            const fs::path& out) {
  std::vector<std::string> args;
  for (std::string a : source.converterArguments) {
    const std::pair<const char*, std::string> fills[] = {{"{disc}", disc}, {"{game_disc}", gameDisc}, {"{out}", Utf8(out)}};
    for (const auto& [key, value] : fills) {
      for (size_t at = a.find(key); at != std::string::npos; at = a.find(key, at + value.size())) {
        a.replace(at, std::strlen(key), value);
      }
    }
    args.push_back(a);
  }
  return args;
}

Status ComputeStatus(const fs::path& modFolder, const std::string& expectedModId, bool converterFound,
                     const std::string& converterVersion) {
  Status st;
  std::error_code ec;
  bool usable = false;
  std::string why;
  if (fs::is_regular_file(modFolder / "mod.json", ec)) {
    std::ifstream in(modFolder / "mod.json", std::ios::binary);
    const std::string json((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    engine::mods::Manifest manifest;
    std::string error;
    if (!engine::mods::ParseManifest(json, manifest, error)) {
      why = "The folder " + Utf8(modFolder) + " is there but its mod.json can't be read (" + error + ").";
    } else if (manifest.id != expectedModId) {
      why = "The folder " + Utf8(modFolder) + " holds another mod (" + manifest.id + ").";
    } else {
      usable = true;
      st.version = manifest.version;
    }
  }
  if (!usable) {
    st.kind = converterFound ? Status::Kind::NotSetUp : Status::Kind::ConverterMissing;
    st.text = converterFound ? "Not set up" : "Converter missing";
    st.detail = !why.empty() ? why
                : converterFound ? "Choose the disc below and press Make."
                                 : "The program that makes the models isn't next to the game, so they can't be made. "
                                   "Reinstall the game, or get the newest version.";
    return st;
  }
  for (auto it = fs::directory_iterator(modFolder / "fsys", ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
    if (it->is_directory(ec)) st.models++;
  }
  const bool outOfDate = converterFound && !converterVersion.empty() && !st.version.empty() &&
                         engine::mods::CompareVersions(st.version, converterVersion) < 0;
  if (outOfDate) {
    st.kind = Status::Kind::OutOfDate;
    st.text = "Out of date: make again";
    st.detail = "The models were made by an older version of the converter (" + st.version + "; now " + converterVersion +
                "). They still work; make them again to get the improvements.";
  } else {
    st.kind = Status::Kind::Ready;
    st.text = "Ready: " + std::to_string(st.models) + (st.models == 1 ? " model" : " models");
    if (!converterFound) st.detail = "The converter isn't next to the game any more, so they can't be made again.";
  }
  return st;
}

std::string QueryVersion(const fs::path& converter, const fs::path& logFile, int waitMs) {
  std::error_code ec;
  if (!fs::is_regular_file(converter, ec)) return {};
  sys::StartRequest request;
  request.program = converter;
  request.arguments = {"--version"};
  request.folder = converter.parent_path();
  request.hidden = true;
  request.outputFile = logFile;
  request.killTree = true;
  std::string error;
  sys::Process* p = sys::StartProgram(request, error);
  if (p == nullptr) return {};
  std::string out;
  if (sys::Ended(p, waitMs)) {
    if (sys::ExitCode(p) == 0) {
      std::ifstream in(logFile, std::ios::binary);
      out.assign((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    }
  } else {
    sys::Stop(p);
  }
  sys::Close(p);
  fs::remove(logFile, ec);
  return ParseVersionOutput(out);
}

DiscCheck::~DiscCheck() { Join(); }

void DiscCheck::Join() {
  if (thread_.joinable()) thread_.join();
}

void DiscCheck::Clear() {
  Join();
  std::lock_guard<std::mutex> lock(mutex_);
  state_ = State::None;
  message_.clear();
  path_.clear();
}

void DiscCheck::Start(const Source& source, const std::string& path, const GameInfo::ExtraAssets& assets) {
  Join();
  {
    std::lock_guard<std::mutex> lock(mutex_);
    path_ = path;
    message_.clear();
  }
  if (path.empty()) {
    state_ = State::None;
    return;
  }
  state_ = State::Checking;
  thread_ = std::thread([this, source, path, identify = assets.identifyDisc] {
    std::string gameId, title, error;
    State result;
    std::string message;
    if (!identify || !identify(path, gameId, title, error)) {
      result = State::Unreadable;
      message = "That file can't be read as a disc image" + (error.empty() ? std::string(".") : " (" + error + ").");
    } else if (AcceptsGameId(source, gameId)) {
      result = State::Good;
      message = "This is " + source.name + " (" + gameId + "): the right disc.";
    } else {
      std::string wanted;
      for (const std::string& g : source.gameIds) wanted += (wanted.empty() ? "" : ", ") + g;
      result = State::WrongGame;
      message = "This is " + (title.empty() ? std::string("another game") : title) + " (" + gameId + "), not " +
                source.name + " (" + wanted + "). Choose the " + source.name + " disc image.";
    }
    std::lock_guard<std::mutex> lock(mutex_);
    message_ = message;
    state_ = result;
  });
}

std::string DiscCheck::message() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return message_;
}

std::string DiscCheck::forPath() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return path_;
}

void RemoveLeftovers(const fs::path& outFolder) {
  const fs::path tmp = fs::path(outFolder.native() + fs::path(".tmp").native());
  std::error_code ec;
  if (tmp.filename().string().size() > 4 && tmp.filename().extension() == ".tmp" && tmp.parent_path() == outFolder.parent_path() &&
      fs::exists(tmp, ec)) {
    fs::remove_all(tmp, ec);
  }
}

MakeJob::~MakeJob() {
  cancel_ = true;
  if (thread_.joinable()) thread_.join();
}

MakeJob::Snapshot MakeJob::Get() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return snap_;
}

void MakeJob::Cancel() { cancel_ = true; }

bool MakeJob::Start(const MakeRequest& request, std::string& error) {
  if (running_) {
    error = "It is already running.";
    return false;
  }
  if (thread_.joinable()) thread_.join();
  std::error_code ec;
  fs::create_directories(request.logFile.parent_path(), ec);
  fs::create_directories(request.outFolder.parent_path(), ec);
  RemoveLeftovers(request.outFolder);
  sys::StartRequest start;
  start.program = request.converter;
  start.arguments = request.arguments;
  start.folder = request.converter.parent_path();
  start.hidden = true;
  start.outputFile = request.logFile;
  start.killTree = true;
  sys::Process* process = sys::StartProgram(start, error);
  if (process == nullptr) {
    error = "The converter couldn't be started (" + Utf8(request.converter) + ", " + error + ").";
    return false;
  }
  cancel_ = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    snap_ = Snapshot();
    snap_.phase = Phase::Running;
  }
  running_ = true;
  thread_ = std::thread([this, request, process] { Run(request, process); });
  return true;
}

void MakeJob::Run(MakeRequest request, void* processPointer) {
  sys::Process* process = static_cast<sys::Process*>(processPointer);
  std::string pending, errorText;
  bool sawDone = false;
  uint64_t offset = 0;
  const auto read = [&] {
    std::ifstream in(request.logFile, std::ios::binary);
    if (!in) return;
    in.seekg(static_cast<std::streamoff>(offset));
    std::string chunk((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    offset += chunk.size();
    pending += chunk;
    for (size_t nl; (nl = pending.find('\n')) != std::string::npos; pending.erase(0, nl + 1)) {
      const Line line = ParseLine(pending.substr(0, nl));
      std::lock_guard<std::mutex> lock(mutex_);
      if (line.kind == Line::Kind::Progress) {
        snap_.done = line.done;
        snap_.total = line.total;
        snap_.current = line.name;
      } else if (line.kind == Line::Kind::Done) {
        sawDone = true;
        snap_.converted = line.converted;
        snap_.keptOriginal = line.keptOriginal;
        snap_.totalBytes = line.totalBytes;
      } else if (line.kind == Line::Kind::Error) {
        errorText = line.message;
      }
    }
  };
  bool cancelled = false;
  for (;;) {
    if (cancel_) {
      cancelled = true;
      sys::Stop(process);
      break;
    }
    if (sys::Ended(process, 80)) break;
    read();
  }
  read();
  const int code = cancelled ? 1 : sys::ExitCode(process);
  sys::Close(process);
  MakeJob::Snapshot result;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    result = snap_;
  }
  if (cancelled) {
    RemoveLeftovers(request.outFolder);
    result.phase = Phase::Cancelled;
    result.message = "Stopped. Nothing was changed.";
  } else if (code == 0 && sawDone) {
    result.phase = Phase::Done;
  } else {
    RemoveLeftovers(request.outFolder);
    if (code == 2) {
      result.phase = Phase::WrongDisc;
      result.message = errorText.empty() ? "One of the discs is not the one the converter needs." : errorText;
    } else {
      result.phase = Phase::Failed;
      result.message = !errorText.empty() ? errorText
                      : code == 0         ? "The converter stopped without saying it was done."
                                          : "The converter stopped without saying why (exit code " + std::to_string(code) + ").";
    }
  }
  std::error_code ec;
  fs::remove(request.logFile, ec);
  {
    std::lock_guard<std::mutex> lock(mutex_);
    snap_ = result;
  }
  running_ = false;
}

}
