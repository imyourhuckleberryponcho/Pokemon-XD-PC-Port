#include "launcher_extra.h"

#include "launcher_sys.h"

#include "i18n.h"
#include <mods.h>

#include <algorithm>
#include <cctype>
#include <cstdarg>
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

using engine::i18n::Tr;

std::string Fmt(const char* format, ...) {
  char buf[2048];
  va_list args;
  va_start(args, format);
  std::vsnprintf(buf, sizeof(buf), format, args);
  va_end(args);
  return buf;
}

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
  if (verb == "stage") {
    Line l;
    std::string id;
    if (!Word(rest, id) || !Number(rest, l.done) || !Number(rest, l.total)) return line;
    l.kind = Line::Kind::Stage;
    l.name = id;
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
  std::string stem = source.converterStem;
  bool preferTest = false;
  if (!source.converterTestEnv.empty() && !source.converterTestStem.empty()) {
    const std::string v = sys::EnvPath(source.converterTestEnv.c_str()).string();
    preferTest = v != "0";
    if (preferTest) stem = source.converterTestStem;
  }
  fs::path folder = programFolder;
  if (!source.converterFolder.empty()) folder /= source.converterFolder;
  if (preferTest) {
    std::error_code ec;
    if (!fs::exists(folder / sys::ProgramName(stem), ec) && fs::exists(folder / sys::ProgramName(source.converterStem), ec)) stem = source.converterStem;
  }
#if defined(__APPLE__)
  {
    std::error_code ec;
    const fs::path real = fs::canonical(folder, ec);
    if (!ec) folder = real;
  }
#endif
  return folder / sys::ProgramName(stem);
}

std::vector<std::string> ConverterArguments(const Source& source, const std::string& disc, const std::string& gameDisc,
                                            const fs::path& out, const fs::path& out2, const fs::path& out3,
                                            const std::string& gameProgram, const fs::path& dataFolder) {
  std::vector<std::string> args;
  for (std::string a : source.converterArguments) {
    const std::pair<const char*, std::string> fills[] = {
        {"{disc}", disc}, {"{game_disc}", gameDisc}, {"{out}", Utf8(out)}, {"{out2}", Utf8(out2)},
        {"{out3}", Utf8(out3)}, {"{game_program}", gameProgram}, {"{data}", Utf8(dataFolder)}};
    for (const auto& [key, value] : fills) {
      for (size_t at = a.find(key); at != std::string::npos; at = a.find(key, at + value.size())) {
        a.replace(at, std::strlen(key), value);
      }
    }
    args.push_back(a);
  }
  return args;
}

GameDiscChoice ChooseGameDisc(const Source& source, const std::string& chosenDisc, const std::string& chosenEdition,
                              const std::string& rememberedPlain, const std::function<bool(const std::string&)>& exists) {
  GameDiscChoice r;
  if (!source.needsPlainGameDisc || chosenEdition.empty()) {
    r.disc = chosenDisc;
    return r;
  }
  if (!rememberedPlain.empty() && rememberedPlain != chosenDisc && (!exists || exists(rememberedPlain))) {
    r.disc = rememberedPlain;
    return r;
  }
  r.message = Tr("Making the Gen 4 pack needs the plain Pokemon XD disc once. Choose it on the Disc page, press Make, "
                 "then choose your Pokemon XG disc again: you can play XG afterwards");
  return r;
}

namespace {
long long JsonNumber(const std::string& json, const std::string& key);
std::string JsonString(const std::string& json, const std::string& key);
}

Status ComputeStatus(const fs::path& modFolder, const std::string& expectedModId, bool converterFound,
                     const std::string& converterVersion, const std::string& modelsFolder) {
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
      why = Fmt(Tr("The folder %s is there but its mod.json can't be read (%s)."), Utf8(modFolder).c_str(), error.c_str());
    } else if (manifest.id != expectedModId) {
      why = Fmt(Tr("The folder %s holds another mod (%s)."), Utf8(modFolder).c_str(), manifest.id.c_str());
    } else {
      usable = true;
      st.version = manifest.version;
    }
  }
  if (!usable) {
    st.kind = converterFound ? Status::Kind::NotSetUp : Status::Kind::ConverterMissing;
    st.text = converterFound ? Tr("Not set up") : Tr("Converter missing");
    st.detail = !why.empty() ? why
                : converterFound ? std::string(Tr("Choose the disc below and press Make."))
                                 : std::string(Tr("The program that makes the models isn't next to the game, so they can't be made. "
                                                  "Reinstall the game, or get the newest version."));
    return st;
  }
  std::string unitOne = "model", unitMany = "models", listed;
  bool customUnits = false;
  {
    std::ifstream in(modFolder / "mod.json", std::ios::binary);
    const std::string json((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const long long count = JsonNumber(json, "count");
    if (count >= 0) {
      st.models = static_cast<int>(count);
      if (!JsonString(json, "unit").empty()) unitOne = JsonString(json, "unit"), customUnits = true;
      if (!JsonString(json, "units").empty()) unitMany = JsonString(json, "units"), customUnits = true;
      listed = JsonString(json, "detail");
    } else {
      for (auto it = fs::directory_iterator(modFolder / modelsFolder, ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
        if (modelsFolder == "fsys" ? it->is_directory(ec) : it->is_regular_file(ec)) st.models++;
      }
    }
  }
  const bool outOfDate = converterFound && !converterVersion.empty() && !st.version.empty() &&
                         engine::mods::CompareVersions(st.version, converterVersion) < 0;
  if (outOfDate) {
    st.kind = Status::Kind::OutOfDate;
    st.text = Tr("Out of date: make again");
    st.detail = Fmt(Tr("The models were made by an older version of the converter (%s; now %s). They still work; make them "
                       "again to get the improvements."),
                    st.version.c_str(), converterVersion.c_str());
  } else {
    st.kind = Status::Kind::Ready;
    st.text = customUnits
                  ? Fmt(Tr("Ready: %d %s"), st.models, (st.models == 1 ? unitOne : unitMany).c_str())
              : st.models == 1 ? Fmt(Tr("Ready: %d model"), st.models)
                               : Fmt(Tr("Ready: %d models"), st.models);
    if (!listed.empty()) st.text += " (" + listed + ")";
    if (!converterFound) st.detail = Tr("The converter isn't next to the game any more, so they can't be made again.");
  }
  return st;
}

namespace {
long long JsonNumber(const std::string& json, const std::string& key) {
  const size_t k = json.find("\"" + key + "\"");
  if (k == std::string::npos) return -1;
  size_t i = json.find(':', k);
  if (i == std::string::npos) return -1;
  i++;
  while (i < json.size() && std::isspace(static_cast<unsigned char>(json[i]))) i++;
  if (i >= json.size() || !std::isdigit(static_cast<unsigned char>(json[i]))) return -1;
  return std::strtoll(json.c_str() + i, nullptr, 10);
}
std::string JsonString(const std::string& json, const std::string& key) {
  const size_t k = json.find("\"" + key + "\"");
  if (k == std::string::npos) return {};
  size_t i = json.find(':', k);
  if (i == std::string::npos) return {};
  i = json.find('"', i);
  if (i == std::string::npos) return {};
  const size_t e = json.find('"', i + 1);
  return e == std::string::npos ? std::string() : json.substr(i + 1, e - i - 1);
}
}

std::vector<std::string> ListNames(const fs::path& jsonFile) {
  std::vector<std::string> names;
  std::ifstream in(jsonFile, std::ios::binary);
  const std::string json((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  for (size_t at = json.find("\"name\""); at != std::string::npos; at = json.find("\"name\"", at + 6)) {
    size_t colon = json.find(':', at);
    size_t open = colon == std::string::npos ? colon : json.find('"', colon);
    size_t close = open == std::string::npos ? open : json.find('"', open + 1);
    if (close == std::string::npos) break;
    names.push_back(json.substr(open + 1, close - open - 1));
  }
  return names;
}

SecondStatus ComputeSecondStatus(const fs::path& folder, const std::string& manifestFile, const std::string& expectedId,
                                 int wantedVersion, const std::string& provides, bool firstOutOfDate) {
  SecondStatus st;
  std::ifstream in(folder / manifestFile, std::ios::binary);
  const std::string json((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  if (!in || JsonString(json, "id") != expectedId) {
    st.kind = SecondStatus::Kind::NotMade;
    st.text = Fmt(Tr("%s: not made yet"), Tr(provides).c_str());
    st.detail = Tr("It is made together with the models: choose the disc below and press Make.");
    return st;
  }
  const long long version = JsonNumber(json, "version");
  st.species = static_cast<int>(std::max<long long>(0, JsonNumber(json, "species")));
  if (version != wantedVersion || firstOutOfDate) {
    st.kind = SecondStatus::Kind::OutOfDate;
    st.text = Fmt(Tr("%s: out of date, make again"), Tr(provides).c_str());
    st.detail = version != wantedVersion
                    ? Fmt(Tr("It was made for another version of the game (pack %lld; now %d), so the game won't use it until "
                             "it is made again."),
                          version, wantedVersion)
                    : std::string(Tr("It was made by an older version of the converter. It still works; make it again to get "
                                     "the improvements."));
    return st;
  }
  st.kind = SecondStatus::Kind::Ready;
  st.text = st.species > 0 ? Fmt(Tr("%s: ready (%d species)"), Tr(provides).c_str(), st.species)
                           : Fmt(Tr("%s: ready"), Tr(provides).c_str());
  return st;
}

std::string QueryVersion(const fs::path& converter, const fs::path& logFile, int waitMs, const std::string& versionArgument) {
  std::error_code ec;
  if (!fs::is_regular_file(converter, ec)) return {};
  sys::StartRequest request;
  request.program = converter;
  request.arguments = {"--version"};
  if (!versionArgument.empty()) request.arguments.push_back(versionArgument);
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
  thread_ = std::thread([this, source, path, identify = source.identify ? source.identify : assets.identifyDisc] {
    std::string gameId, title, error;
    State result;
    std::string message;
    if (!identify || !identify(path, gameId, title, error)) {
      result = State::Unreadable;
      if (source.dump) {
        message = error.empty() ? std::string(Tr("That file can't be read as a 3DS game dump.")) : error;
      } else {
        message = error.empty() ? std::string(Tr("That file can't be read as a disc image."))
                                : Fmt(Tr("That file can't be read as a disc image (%s)."), error.c_str());
      }
    } else if (AcceptsGameId(source, gameId)) {
      result = State::Good;
      message = source.dump ? Fmt(Tr("This is %s (%s): a dump the converter can read."), source.name.c_str(), gameId.c_str())
                            : Fmt(Tr("This is %s (%s): the right disc."), source.name.c_str(), gameId.c_str());
    } else if (source.dump) {
      result = State::WrongGame;
      message = title.empty() ? Fmt(Tr("This is another 3DS game (%s), not %s. Choose your Ultra Sun or Ultra Moon dump."),
                                    gameId.c_str(), source.name.c_str())
                              : Fmt(Tr("This is %s (%s), not %s. Choose your Ultra Sun or Ultra Moon dump."), title.c_str(),
                                    gameId.c_str(), source.name.c_str());
    } else {
      std::string wanted;
      for (const std::string& g : source.gameIds) wanted += (wanted.empty() ? "" : ", ") + g;
      result = State::WrongGame;
      message = title.empty() ? Fmt(Tr("This is another game (%s), not %s (%s). Choose the %s disc image."), gameId.c_str(),
                                    source.name.c_str(), wanted.c_str(), source.name.c_str())
                              : Fmt(Tr("This is %s (%s), not %s (%s). Choose the %s disc image."), title.c_str(), gameId.c_str(),
                                    source.name.c_str(), wanted.c_str(), source.name.c_str());
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

bool HasUnfinishedWork(const fs::path& outFolder) {
  std::error_code ec;
  return fs::exists(fs::path(outFolder.native() + fs::path(".work").native()) / "state.json", ec);
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
    error = Tr("It is already running.");
    return false;
  }
  if (thread_.joinable()) thread_.join();
  std::error_code ec;
  fs::create_directories(request.logFile.parent_path(), ec);
  fs::create_directories(request.outFolder.parent_path(), ec);
  RemoveLeftovers(request.outFolder);
  if (!request.outFolder2.empty()) RemoveLeftovers(request.outFolder2);
  sys::StartRequest start;
  start.program = request.converter;
  start.arguments = request.arguments;
  start.folder = request.converter.parent_path();
  start.hidden = true;
  start.outputFile = request.logFile;
  start.killTree = true;
  sys::Process* process = sys::StartProgram(start, error);
  if (process == nullptr) {
    error = Fmt(Tr("The converter couldn't be started (%s, %s)."), Utf8(request.converter).c_str(), error.c_str());
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
      if (line.kind == Line::Kind::Stage) {
        snap_.stage = line.name;
        snap_.stageFrom = line.done;
        snap_.stageTo = line.total;
        snap_.done = 0;
        snap_.total = 0;
        snap_.current.clear();
      } else if (line.kind == Line::Kind::Progress) {
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
    if (!request.outFolder2.empty()) RemoveLeftovers(request.outFolder2);
    result.phase = Phase::Cancelled;
    result.message = !request.outFolder3.empty() && HasUnfinishedWork(request.outFolder3)
                         ? std::string(Tr("Stopped. What is made so far is kept: press Make and it continues where it stopped."))
                         : std::string(Tr("Stopped. Nothing was changed."));
  } else if (code == 0 && sawDone) {
    result.phase = Phase::Done;
  } else {
    RemoveLeftovers(request.outFolder);
    if (!request.outFolder2.empty()) RemoveLeftovers(request.outFolder2);
    if (code == 2) {
      result.phase = Phase::WrongDisc;
      result.message = errorText.empty() ? std::string(Tr("One of the discs is not the one the converter needs.")) : errorText;
    } else {
      result.phase = Phase::Failed;
      result.message = !errorText.empty() ? errorText
                      : code == 0         ? std::string(Tr("The converter stopped without saying it was done."))
                                          : Fmt(Tr("The converter stopped without saying why (exit code %d)."), code);
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
