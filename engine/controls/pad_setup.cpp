#include "pad_setup.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <system_error>

namespace engine::controls::padsetup {
namespace {

struct RoleInfo {
  const char* id;
  const char* sdl;
};
constexpr RoleInfo kRoles[kRoleCount] = {
    {"a", "a"},          {"b", "b"},          {"x", "x"},           {"y", "y"},
    {"start", "start"},  {"z", "rightshoulder"}, {"l", "lefttrigger"}, {"r", "righttrigger"},
    {"dpad_up", "dpup"}, {"dpad_down", "dpdown"}, {"dpad_left", "dpleft"}, {"dpad_right", "dpright"},
    {"stick_left", "leftx"}, {"stick_up", "lefty"}, {"cstick_left", "rightx"}, {"cstick_up", "righty"},
};

std::string Lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return s;
}

std::string Trim(const std::string& s) {
  size_t a = 0, b = s.size();
  while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) {
    a++;
  }
  while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) {
    b--;
  }
  return s.substr(a, b - a);
}

std::string Input(Role role, const Source& s) {
  const std::string n = std::to_string(s.index);
  switch (s.kind) {
  case Source::Kind::Button: return "b" + n;
  case Source::Kind::Hat: return "h" + n + "." + std::to_string(s.hatMask);
  case Source::Kind::Axis:
    if (IsStickRole(role)) {
      return "a" + n + (s.direction < 0 ? "" : "~");
    }
    if (IsTriggerRole(role)) {
      if (s.rest <= -0.8f && s.direction > 0) {
        return "a" + n;
      }
      if (s.rest >= 0.8f && s.direction < 0) {
        return "a" + n + "~";
      }
    }
    return std::string(s.direction < 0 ? "-" : "+") + "a" + n;
  case Source::Kind::None: break;
  }
  return {};
}

}

const char* RoleId(Role role) { return kRoles[static_cast<int>(role)].id; }

std::optional<Role> RoleFromId(const std::string& id) {
  const std::string l = Lower(id);
  for (int i = 0; i < kRoleCount; i++) {
    if (l == kRoles[i].id) {
      return static_cast<Role>(i);
    }
  }
  return std::nullopt;
}

bool IsStickRole(Role role) { return role >= Role::StickLeft && role <= Role::CStickUp; }
bool IsTriggerRole(Role role) { return role == Role::L || role == Role::R; }

bool Source::operator==(const Source& o) const {
  if (kind != o.kind || index != o.index) {
    return false;
  }
  if (kind == Kind::Hat) {
    return hatMask == o.hatMask;
  }
  if (kind == Kind::Axis) {
    return direction == o.direction;
  }
  return true;
}

std::string DescribeSource(const Source& s) {
  switch (s.kind) {
  case Source::Kind::Button: return "button " + std::to_string(s.index);
  case Source::Kind::Axis: return "axis " + std::to_string(s.index) + (s.direction < 0 ? " (-)" : " (+)");
  case Source::Kind::Hat: {
    const char* dir = s.hatMask == 1 ? "up" : s.hatMask == 2 ? "right" : s.hatMask == 4 ? "down" : s.hatMask == 8 ? "left" : "?";
    return "hat " + std::to_string(s.index) + " " + dir;
  }
  case Source::Kind::None: break;
  }
  return "skipped";
}

void Detector::Begin(const Snapshot& rest) {
  rest_ = rest;
  begun_ = true;
}

std::optional<Source> Detector::Feed(const Snapshot& now) {
  if (!begun_) {
    Begin(now);
    return std::nullopt;
  }
  for (size_t i = 0; i < now.buttons.size(); i++) {
    const bool was = i < rest_.buttons.size() && rest_.buttons[i];
    if (now.buttons[i] && !was) {
      Source s;
      s.kind = Source::Kind::Button;
      s.index = static_cast<int>(i);
      return s;
    }
  }
  for (size_t i = 0; i < now.hats.size(); i++) {
    const int was = i < rest_.hats.size() ? rest_.hats[i] : 0;
    const int v = now.hats[i];
    if (v != 0 && v != was && (v & (v - 1)) == 0) {
      Source s;
      s.kind = Source::Kind::Hat;
      s.index = static_cast<int>(i);
      s.hatMask = v;
      return s;
    }
  }
  for (size_t i = 0; i < now.axes.size(); i++) {
    const float was = i < rest_.axes.size() ? rest_.axes[i] : 0.0f;
    const float d = now.axes[i] - was;
    if (std::fabs(d) >= kAxisMove) {
      Source s;
      s.kind = Source::Kind::Axis;
      s.index = static_cast<int>(i);
      s.direction = d < 0 ? -1 : 1;
      s.rest = was;
      return s;
    }
  }
  return std::nullopt;
}

bool Detector::Released(const Snapshot& now) const {
  for (size_t i = 0; i < now.buttons.size(); i++) {
    const bool was = i < rest_.buttons.size() && rest_.buttons[i];
    if (now.buttons[i] && !was) {
      return false;
    }
  }
  for (size_t i = 0; i < now.hats.size(); i++) {
    if (now.hats[i] != 0 && (i >= rest_.hats.size() || now.hats[i] != rest_.hats[i])) {
      return false;
    }
  }
  for (size_t i = 0; i < now.axes.size(); i++) {
    const float was = i < rest_.axes.size() ? rest_.axes[i] : 0.0f;
    if (std::fabs(now.axes[i] - was) > 0.3f) {
      return false;
    }
  }
  return true;
}

std::string CheckSource(Role role, const Source& source) {
  if (IsStickRole(role) && source.kind != Source::Kind::Axis) {
    return "A stick moves an axis, and that was a button or a D-pad. Push the stick instead (or skip).";
  }
  return {};
}

std::string TakenBy(const Answers& answers, Role role, const Source& source) {
  for (int i = 0; i < kRoleCount; i++) {
    if (i != static_cast<int>(role) && answers.source[i].Valid() && answers.source[i] == source) {
      return RoleId(static_cast<Role>(i));
    }
  }
  return {};
}

std::string BuildMapping(const std::string& guid, const std::string& name, const std::string& platform,
                         const Answers& answers) {
  std::string clean;
  for (char c : name) {
    clean += (c == ',' || c == '\n' || c == '\r') ? ' ' : c;
  }
  clean = Trim(clean);
  if (clean.empty()) {
    clean = "Controller";
  }
  std::string out = guid + "," + clean;
  bool any = false;
  for (int i = 0; i < kRoleCount; i++) {
    const Role role = static_cast<Role>(i);
    const Source& s = answers.source[i];
    if (!s.Valid()) {
      continue;
    }
    if (!CheckSource(role, s).empty()) {
      continue;
    }
    any = true;
    out += std::string(",") + kRoles[i].sdl + ":" + Input(role, s);
  }
  if (!any) {
    return {};
  }
  if (!platform.empty()) {
    out += ",platform:" + platform;
  }
  return out;
}

void Session::Start(const std::string& guid, const std::string& name, const std::string& platform) {
  guid_ = guid;
  name_ = name;
  platform_ = platform;
  Restart();
}

void Session::Restart() {
  answers_ = Answers{};
  step_ = 0;
  begun_ = false;
  releasing_ = false;
  event_ = Event::None;
  last_ = Source{};
  takenBy_.clear();
}

int Session::Answered() const {
  int n = 0;
  for (const Source& s : answers_.source) {
    n += s.Valid() ? 1 : 0;
  }
  return n;
}

void Session::Skip() {
  if (step_ < kRoleCount) {
    answers_.source[step_] = Source{};
    step_++;
  }
  begun_ = false;
  releasing_ = false;
  event_ = Event::None;
}

void Session::Back() {
  if (step_ > 0) {
    step_--;
    answers_.source[step_] = Source{};
  }
  begun_ = false;
  releasing_ = false;
  event_ = Event::None;
}

void Session::Feed(const Snapshot& now) {
  if (Finished()) {
    return;
  }
  if (!begun_) {
    det_.Begin(now);
    begun_ = true;
    releasing_ = false;
    return;
  }
  if (releasing_) {
    if (det_.Released(now)) {
      det_.Begin(now);
      releasing_ = false;
    }
    return;
  }
  const std::optional<Source> got = det_.Feed(now);
  if (!got) {
    return;
  }
  last_ = *got;
  releasing_ = true;
  if (!CheckSource(Current(), *got).empty()) {
    event_ = Event::WrongKind;
    return;
  }
  const std::string other = TakenBy(answers_, Current(), *got);
  if (!other.empty()) {
    event_ = Event::Taken;
    takenBy_ = other;
    return;
  }
  answers_.source[step_] = *got;
  step_++;
  event_ = Event::Answered;
}

std::string GuidOfLine(const std::string& line) {
  const std::string t = Trim(line);
  if (t.empty() || t[0] == '#') {
    return {};
  }
  const size_t comma = t.find(',');
  return comma == std::string::npos ? std::string() : Lower(Trim(t.substr(0, comma)));
}

std::filesystem::path Folder(const std::filesystem::path& dataFolder) {
  if (const char* env = std::getenv("XD_PAD_MAPPING_DIR"); env != nullptr && *env != 0) {
    return std::filesystem::path(env);
  }
  return dataFolder;
}

std::filesystem::path MappingPath(const std::filesystem::path& dataFolder) { return Folder(dataFolder) / kMappingFile; }
std::filesystem::path DatabasePath(const std::filesystem::path& dataFolder) { return Folder(dataFolder) / kDatabaseFile; }

bool HasDatabase(const std::filesystem::path& dataFolder) {
  std::error_code ec;
  return std::filesystem::is_regular_file(DatabasePath(dataFolder), ec);
}

std::vector<std::string> LoadMappings(const std::filesystem::path& dataFolder) {
  std::vector<std::string> out;
  std::ifstream in(MappingPath(dataFolder), std::ios::binary);
  std::string line;
  while (std::getline(in, line)) {
    line = Trim(line);
    if (!line.empty() && line[0] != '#' && line.find(',') != std::string::npos) {
      out.push_back(line);
    }
  }
  return out;
}

bool SaveMappings(const std::filesystem::path& dataFolder, const std::vector<std::string>& lines, std::string* error) {
  namespace fs = std::filesystem;
  const fs::path file = MappingPath(dataFolder);
  std::error_code ec;
  fs::create_directories(file.parent_path(), ec);
  const fs::path tmp = fs::path(file).concat(".tmp");
  {
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    if (!out) {
      if (error != nullptr) {
        *error = "cannot write " + tmp.string();
      }
      return false;
    }
    out << "# Controllers set up by hand (the launcher's Controls page, \"Set it up...\"): one SDL mapping per line.\n"
           "# Read by the launcher and the game at start. Delete a line (or use \"Forget this set-up\") to undo it.\n";
    for (const std::string& l : lines) {
      out << l << "\n";
    }
    if (!out) {
      if (error != nullptr) {
        *error = "cannot write " + tmp.string();
      }
      return false;
    }
  }
  fs::rename(tmp, file, ec);
  if (ec) {
    fs::remove(file, ec);
    ec.clear();
    fs::rename(tmp, file, ec);
    if (ec) {
      if (error != nullptr) {
        *error = "cannot replace " + file.string() + ": " + ec.message();
      }
      return false;
    }
  }
  return true;
}

bool StoreMapping(const std::filesystem::path& dataFolder, const std::string& line, std::string* error) {
  const std::string guid = GuidOfLine(line);
  if (guid.empty()) {
    if (error != nullptr) {
      *error = "not a mapping line";
    }
    return false;
  }
  std::vector<std::string> lines = LoadMappings(dataFolder);
  lines.erase(std::remove_if(lines.begin(), lines.end(), [&](const std::string& l) { return GuidOfLine(l) == guid; }),
              lines.end());
  lines.push_back(Trim(line));
  return SaveMappings(dataFolder, lines, error);
}

bool ForgetMapping(const std::filesystem::path& dataFolder, const std::string& guid, std::string* error) {
  std::vector<std::string> lines = LoadMappings(dataFolder);
  const std::string g = Lower(guid);
  const size_t before = lines.size();
  lines.erase(std::remove_if(lines.begin(), lines.end(), [&](const std::string& l) { return GuidOfLine(l) == g; }),
              lines.end());
  if (lines.size() == before) {
    return false;
  }
  return SaveMappings(dataFolder, lines, error);
}

}
