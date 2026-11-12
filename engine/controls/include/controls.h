#ifndef ENGINE_CONTROLS_H
#define ENGINE_CONTROLS_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace engine::settings {
class Registry;
}

namespace engine::controls {

inline constexpr int kPorts = 4;

enum class GcInput : uint8_t {
  A, B, X, Y, Z, L, R, Start,
  DpadUp, DpadDown, DpadLeft, DpadRight,
  StickUp, StickDown, StickLeft, StickRight,
  CStickUp, CStickDown, CStickLeft, CStickRight,
  Count
};
inline constexpr size_t kGcInputCount = static_cast<size_t>(GcInput::Count);
const char* GcInputId(GcInput input);
const char* GcInputName(GcInput input);
std::optional<GcInput> GcInputFromId(std::string_view id);

enum class Target : uint8_t {
  A, B, X, Y, Z, Start, L, R, Up, Down, Left, Right,
  StickX, StickY, CStickX, CStickY, TriggerL, TriggerR,
  Count
};
inline constexpr size_t kTargetCount = static_cast<size_t>(Target::Count);
const char* TargetId(Target target);
bool IsAxisTarget(Target target);

enum class PadButton : uint8_t {
  South, East, West, North, Back, Guide, Start, LeftStick, RightStick, LeftShoulder, RightShoulder,
  DpadUp, DpadDown, DpadLeft, DpadRight, Misc1, RightPaddle1, LeftPaddle1, RightPaddle2, LeftPaddle2,
  Touchpad, Misc2, Misc3, Misc4, Misc5, Misc6,
  Count
};
enum class PadAxis : uint8_t { LeftX, LeftY, RightX, RightY, LeftTrigger, RightTrigger, Count };

enum class FaceLabel : uint8_t { A, B, X, Y };

struct Input {
  enum class Kind : uint8_t { None, Key, Button, Axis, Label };
  Kind kind = Kind::None;
  uint16_t code = 0;
  int8_t sign = 0;

  static Input Key(uint16_t scancode) { return {Kind::Key, scancode, 0}; }
  static Input Button(PadButton b) { return {Kind::Button, static_cast<uint16_t>(b), 0}; }
  static Input Axis(PadAxis a, int sign) { return {Kind::Axis, static_cast<uint16_t>(a), static_cast<int8_t>(sign)}; }
  static Input Label(FaceLabel l) { return {Kind::Label, static_cast<uint16_t>(l), 0}; }
  bool operator==(const Input&) const = default;
};

enum class Device : uint8_t {
  Keyboard,
  Xbox,
  PlayStation,
  Switch,
  GameCube,
  NsoGameCube,
  Generic,
  Count
};
const char* DeviceId(Device device);

std::string InputToken(const Input& input);
std::optional<Input> ParseKeyToken(std::string_view token);
std::optional<Input> ParsePadToken(std::string_view token);

inline constexpr float kDefaultStickDeadzone = 8000.f / 32767.f;
inline constexpr float kDefaultClickThreshold = 31151.f / 32767.f;
inline constexpr int kDefaultWalkPercent = 50;

struct KeyboardMap {
  std::array<std::vector<Input>, kGcInputCount> keys;
  Input walk;
  int walkPercent = kDefaultWalkPercent;
  bool operator==(const KeyboardMap&) const = default;
};

enum class Layout : uint8_t {
  Position,
  Label,
};

struct GamepadMap {
  Layout layout = Layout::Position;
  std::optional<Device> kind;
  bool rumble = true;
  std::array<std::optional<std::vector<Input>>, kGcInputCount> buttons;
  float leftStickDeadzone = kDefaultStickDeadzone;
  float rightStickDeadzone = kDefaultStickDeadzone;
  float lClickThreshold = kDefaultClickThreshold;
  float rClickThreshold = kDefaultClickThreshold;
  bool operator==(const GamepadMap&) const = default;
};

struct ControllerChoice {
  enum class Mode : uint8_t { Auto, None, Pinned };
  Mode mode = Mode::None;
  std::string guid;
  int nth = 0;
  std::string name;
  bool operator==(const ControllerChoice&) const = default;
  static ControllerChoice Auto() { return {Mode::Auto}; }
  static ControllerChoice Pin(std::string guid, int nth, std::string name) {
    return {Mode::Pinned, std::move(guid), nth, std::move(name)};
  }
};
std::string ChoiceText(const ControllerChoice& choice);
bool ParseChoice(std::string_view text, ControllerChoice& out);

struct ConnectedPad {
  std::string guid;
  std::string name;
};
std::array<int, 4> AssignControllers(const std::array<ControllerChoice, 4>& choices,
                                     const std::vector<ConnectedPad>& pads);
int NthOfModel(const std::vector<ConnectedPad>& pads, size_t index);

enum class Action : uint8_t { FastForward, Count };
inline constexpr size_t kActionCount = static_cast<size_t>(Action::Count);
const char* ActionId(Action action);
const char* ActionName(Action action);
std::optional<Action> ActionFromId(std::string_view id);

struct ActionMap {
  std::array<std::vector<Input>, kActionCount> keys;
  std::array<std::vector<Input>, kActionCount> pad;
  bool operator==(const ActionMap&) const = default;
};
ActionMap DefaultActions(int port);

struct PortMap {
  KeyboardMap keyboard;
  GamepadMap gamepad;
  ControllerChoice controller;
  ActionMap actions;
  bool operator==(const PortMap&) const = default;
};

KeyboardMap DefaultKeyboard(int port);
GamepadMap DefaultGamepad();
ControllerChoice DefaultChoice(int port);
PortMap DefaultPort(int port);

inline Device EffectiveDevice(const GamepadMap& map, Device detected) { return map.kind ? *map.kind : detected; }

enum class PadPreset : uint8_t { GameCubeAdapter, Xbox, PlayStation, SwitchPro, Count };
const char* PadPresetName(PadPreset preset);
GamepadMap ApplyPadPreset(const GamepadMap& map, PadPreset preset);
std::optional<PadPreset> PadPresetOf(const GamepadMap& map);

std::vector<Input> DefaultPadInputs(GcInput input, Device device, Layout layout);
std::vector<Input> PadInputs(const GamepadMap& map, GcInput input, Device device);

bool UsesDefaultButtons(const GamepadMap& map);

PadButton LabelPosition(FaceLabel label, Device device);

struct Binding {
  Input input;
  Target target = Target::A;
  float scale = 1.f;
  float threshold = 0.5f;
  float deadZone = 0.f;
  Input held;
  bool operator==(const Binding&) const = default;
};

std::vector<Binding> ResolveKeyboard(const KeyboardMap& map);
std::vector<Binding> ResolveGamepad(const GamepadMap& map, Device device, bool emulateTriggers);

struct RawInputs {
  std::vector<uint16_t> keys;
  std::array<bool, static_cast<size_t>(PadButton::Count)> buttons{};
  std::array<float, static_cast<size_t>(PadAxis::Count)> axes{};
};

struct PadReading {
  uint16_t buttons = 0;
  int8_t stickX = 0, stickY = 0, substickX = 0, substickY = 0;
  uint8_t triggerLeft = 0, triggerRight = 0;
  bool operator==(const PadReading&) const = default;
};
float TargetValue(const std::vector<Binding>& bindings, Target target, const RawInputs& raw);
PadReading Evaluate(const std::vector<Binding>& bindings, const RawInputs& raw);

struct ButtonLabel {
  std::string text;
  std::string glyph;
  bool operator==(const ButtonLabel&) const = default;
};

ButtonLabel InputLabel(const Input& input, Device device);
ButtonLabel GcInputLabel(const PortMap& map, GcInput input, Device device);

void RegisterSettings(engine::settings::Registry& registry);
PortMap ReadPort(const engine::settings::Registry& registry, int port);
bool WritePort(engine::settings::Registry& registry, int port, const PortMap& map);
std::vector<std::string> UnknownEntries(const engine::settings::Registry& registry);

std::string KeyListText(const std::vector<Input>& keys);
std::string PadListText(const std::optional<std::vector<Input>>& inputs);
bool ParseKeyList(std::string_view text, std::vector<Input>& out);
bool ParsePadList(std::string_view text, std::optional<std::vector<Input>>& out);

}

#endif
