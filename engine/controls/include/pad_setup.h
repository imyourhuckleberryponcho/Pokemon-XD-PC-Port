#ifndef ENGINE_CONTROLS_PAD_SETUP_H
#define ENGINE_CONTROLS_PAD_SETUP_H

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace engine::controls::padsetup {

enum class Role {
  A, B, X, Y, Start, Z, L, R,
  DpadUp, DpadDown, DpadLeft, DpadRight,
  StickLeft, StickUp,
  CStickLeft, CStickUp,
  Count
};
inline constexpr int kRoleCount = static_cast<int>(Role::Count);
const char* RoleId(Role role);
std::optional<Role> RoleFromId(const std::string& id);
bool IsStickRole(Role role);
bool IsTriggerRole(Role role);

struct Source {
  enum class Kind { None, Button, Axis, Hat };
  Kind kind = Kind::None;
  int index = 0;
  int hatMask = 0;
  int direction = 0;
  float rest = 0.0f;
  bool operator==(const Source& o) const;
  bool Valid() const { return kind != Kind::None; }
};
std::string DescribeSource(const Source& s);

struct Snapshot {
  std::vector<float> axes;
  std::vector<bool> buttons;
  std::vector<uint8_t> hats;
};

class Detector {
 public:
  void Begin(const Snapshot& rest);
  std::optional<Source> Feed(const Snapshot& now);
  bool Released(const Snapshot& now) const;
  static constexpr float kAxisMove = 0.6f;

 private:
  Snapshot rest_;
  bool begun_ = false;
};

std::string CheckSource(Role role, const Source& source);

struct Answers {
  Source source[kRoleCount];
  bool Has(Role r) const { return source[static_cast<int>(r)].Valid(); }
};

std::string TakenBy(const Answers& answers, Role role, const Source& source);

std::string BuildMapping(const std::string& guid, const std::string& name, const std::string& platform,
                         const Answers& answers);

class Session {
 public:
  enum class Event { None, Answered, Taken, WrongKind };
  void Start(const std::string& guid, const std::string& name, const std::string& platform);
  const std::string& Guid() const { return guid_; }
  const std::string& Name() const { return name_; }
  int Step() const { return step_; }
  bool Finished() const { return step_ >= kRoleCount; }
  Role Current() const { return static_cast<Role>(step_ < kRoleCount ? step_ : kRoleCount - 1); }
  const Answers& GetAnswers() const { return answers_; }
  Event LastEvent() const { return event_; }
  const Source& LastSource() const { return last_; }
  const std::string& TakenByRole() const { return takenBy_; }
  void Feed(const Snapshot& now);
  void Skip();
  void Back();
  void Restart();
  void Resume() { begun_ = false; }
  int Answered() const;
  std::string Mapping() const { return BuildMapping(guid_, name_, platform_, answers_); }

 private:
  std::string guid_, name_, platform_;
  Answers answers_;
  int step_ = 0;
  Detector det_;
  bool begun_ = false;
  bool releasing_ = false;
  Event event_ = Event::None;
  Source last_;
  std::string takenBy_;
};

std::string GuidOfLine(const std::string& line);

inline constexpr const char* kMappingFile = "pad_mappings.txt";
inline constexpr const char* kDatabaseFile = "gamecontrollerdb.txt";
std::filesystem::path Folder(const std::filesystem::path& dataFolder);
std::vector<std::string> LoadMappings(const std::filesystem::path& dataFolder);
bool SaveMappings(const std::filesystem::path& dataFolder, const std::vector<std::string>& lines, std::string* error = nullptr);
bool StoreMapping(const std::filesystem::path& dataFolder, const std::string& line, std::string* error = nullptr);
bool ForgetMapping(const std::filesystem::path& dataFolder, const std::string& guid, std::string* error = nullptr);
bool HasDatabase(const std::filesystem::path& dataFolder);
std::filesystem::path MappingPath(const std::filesystem::path& dataFolder);
std::filesystem::path DatabasePath(const std::filesystem::path& dataFolder);

}

#endif
