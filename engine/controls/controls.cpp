#include "controls.h"

#include "controls_tables.h"

#include <algorithm>
#include <cmath>

namespace engine::controls {

namespace detail {

const KeyInfo kKeys[] = {
    {4, "a", "A"}, {5, "b", "B"}, {6, "c", "C"}, {7, "d", "D"}, {8, "e", "E"}, {9, "f", "F"}, {10, "g", "G"},
    {11, "h", "H"}, {12, "i", "I"}, {13, "j", "J"}, {14, "k", "K"}, {15, "l", "L"}, {16, "m", "M"},
    {17, "n", "N"}, {18, "o", "O"}, {19, "p", "P"}, {20, "q", "Q"}, {21, "r", "R"}, {22, "s", "S"},
    {23, "t", "T"}, {24, "u", "U"}, {25, "v", "V"}, {26, "w", "W"}, {27, "x", "X"}, {28, "y", "Y"},
    {29, "z", "Z"},
    {30, "1", "1"}, {31, "2", "2"}, {32, "3", "3"}, {33, "4", "4"}, {34, "5", "5"}, {35, "6", "6"},
    {36, "7", "7"}, {37, "8", "8"}, {38, "9", "9"}, {39, "0", "0"},
    {40, "enter", "Enter"}, {41, "escape", "Esc"}, {42, "backspace", "Backspace"}, {43, "tab", "Tab"},
    {44, "space", "Space"}, {45, "minus", "-"}, {46, "equals", "="}, {47, "lbracket", "["},
    {48, "rbracket", "]"}, {49, "backslash", "\\"}, {51, "semicolon", ";"}, {52, "apostrophe", "'"},
    {53, "grave", "`"}, {54, "comma", ","}, {55, "period", "."}, {56, "slash", "/"},
    {57, "capslock", "Caps Lock"},
    {58, "f1", "F1"}, {59, "f2", "F2"}, {60, "f3", "F3"}, {61, "f4", "F4"}, {62, "f5", "F5"}, {63, "f6", "F6"},
    {64, "f7", "F7"}, {65, "f8", "F8"}, {66, "f9", "F9"}, {67, "f10", "F10"}, {68, "f11", "F11"},
    {69, "f12", "F12"},
    {73, "insert", "Insert"}, {74, "home", "Home"}, {75, "pageup", "Page Up"}, {76, "delete", "Delete"},
    {77, "end", "End"}, {78, "pagedown", "Page Down"},
    {79, "right", "Right Arrow"}, {80, "left", "Left Arrow"}, {81, "down", "Down Arrow"}, {82, "up", "Up Arrow"},
    {84, "kp_divide", "Num /"}, {85, "kp_multiply", "Num *"}, {86, "kp_minus", "Num -"}, {87, "kp_plus", "Num +"},
    {88, "kp_enter", "Num Enter"}, {89, "kp_1", "Num 1"}, {90, "kp_2", "Num 2"}, {91, "kp_3", "Num 3"},
    {92, "kp_4", "Num 4"}, {93, "kp_5", "Num 5"}, {94, "kp_6", "Num 6"}, {95, "kp_7", "Num 7"},
    {96, "kp_8", "Num 8"}, {97, "kp_9", "Num 9"}, {98, "kp_0", "Num 0"}, {99, "kp_period", "Num ."},
    {224, "lctrl", "Left Ctrl"}, {225, "lshift", "Left Shift"}, {226, "lalt", "Left Alt"},
    {228, "rctrl", "Right Ctrl"}, {229, "rshift", "Right Shift"}, {230, "ralt", "Right Alt"},
};

const KeyInfo* FindKey(uint16_t scancode) {
  for (const KeyInfo& k : kKeys) {
    if (k.scancode == scancode) {
      return &k;
    }
  }
  return nullptr;
}

const char* const kPadButtonTokens[] = {
    "south", "east", "west", "north", "back", "guide", "start", "lstick_click", "rstick_click", "lshoulder",
    "rshoulder", "dpad_up", "dpad_down", "dpad_left", "dpad_right", "misc1", "rpaddle1", "lpaddle1", "rpaddle2",
    "lpaddle2", "touchpad", "misc2", "misc3", "misc4", "misc5", "misc6",
};
static_assert(std::size(kPadButtonTokens) == static_cast<size_t>(PadButton::Count));

const AxisToken kAxisTokens[] = {
    {0, -1, "lstick_left"}, {0, 1, "lstick_right"}, {1, -1, "lstick_up"}, {1, 1, "lstick_down"},
    {2, -1, "rstick_left"}, {2, 1, "rstick_right"}, {3, -1, "rstick_up"}, {3, 1, "rstick_down"},
    {4, 1, "ltrigger"},     {5, 1, "rtrigger"},
};
const int kAxisTokenCount = static_cast<int>(std::size(kAxisTokens));

}

using namespace detail;

namespace {

struct GcInputInfo {
  const char* id;
  const char* name;
};
constexpr GcInputInfo kGcInputs[] = {
    {"a", "A"},
    {"b", "B"},
    {"x", "X"},
    {"y", "Y"},
    {"z", "Z"},
    {"l", "L"},
    {"r", "R"},
    {"start", "Start"},
    {"dpad_up", "D-pad up"},
    {"dpad_down", "D-pad down"},
    {"dpad_left", "D-pad left"},
    {"dpad_right", "D-pad right"},
    {"stick_up", "Main stick up"},
    {"stick_down", "Main stick down"},
    {"stick_left", "Main stick left"},
    {"stick_right", "Main stick right"},
    {"c_up", "C-stick up"},
    {"c_down", "C-stick down"},
    {"c_left", "C-stick left"},
    {"c_right", "C-stick right"},
};
static_assert(std::size(kGcInputs) == kGcInputCount);

constexpr GcInputInfo kActions[] = {
    {"fast_forward", "Fast-forward"},
};
static_assert(std::size(kActions) == kActionCount);

constexpr const char* kTargetIds[] = {"a",      "b",      "x",       "y",       "z",         "start",
                                      "l",      "r",      "up",      "down",    "left",      "right",
                                      "left_x", "left_y", "right_x", "right_y", "trigger_l", "trigger_r"};
static_assert(std::size(kTargetIds) == kTargetCount);

constexpr const char* kDeviceIds[] = {"keyboard", "xbox", "playstation", "switch", "gamecube", "nso_gamecube",
                                      "generic"};
static_assert(std::size(kDeviceIds) == static_cast<size_t>(Device::Count));

constexpr const char* kLabelTokens[] = {"a", "b", "x", "y"};

size_t Index(GcInput input) { return static_cast<size_t>(input); }

std::string_view Trim(std::string_view s) {
  while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) {
    s.remove_prefix(1);
  }
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) {
    s.remove_suffix(1);
  }
  return s;
}

bool SplitList(std::string_view text, std::vector<std::string_view>& out) {
  out.clear();
  text = Trim(text);
  if (text.empty()) {
    return false;
  }
  while (true) {
    const size_t comma = text.find(',');
    const std::string_view token = Trim(text.substr(0, comma));
    if (token.empty()) {
      return false;
    }
    out.push_back(token);
    if (comma == std::string_view::npos) {
      return true;
    }
    text = text.substr(comma + 1);
  }
}

std::optional<Target> DigitalTarget(GcInput input) {
  switch (input) {
  case GcInput::A: return Target::A;
  case GcInput::B: return Target::B;
  case GcInput::X: return Target::X;
  case GcInput::Y: return Target::Y;
  case GcInput::Z: return Target::Z;
  case GcInput::Start: return Target::Start;
  case GcInput::DpadUp: return Target::Up;
  case GcInput::DpadDown: return Target::Down;
  case GcInput::DpadLeft: return Target::Left;
  case GcInput::DpadRight: return Target::Right;
  default: return std::nullopt;
  }
}

bool StickTarget(GcInput input, Target& target, float& sign) {
  switch (input) {
  case GcInput::StickUp: target = Target::StickY, sign = 1.f; return true;
  case GcInput::StickDown: target = Target::StickY, sign = -1.f; return true;
  case GcInput::StickLeft: target = Target::StickX, sign = -1.f; return true;
  case GcInput::StickRight: target = Target::StickX, sign = 1.f; return true;
  case GcInput::CStickUp: target = Target::CStickY, sign = 1.f; return true;
  case GcInput::CStickDown: target = Target::CStickY, sign = -1.f; return true;
  case GcInput::CStickLeft: target = Target::CStickX, sign = -1.f; return true;
  case GcInput::CStickRight: target = Target::CStickX, sign = 1.f; return true;
  default: return false;
  }
}

struct FaceLayout {
  PadButton a, b, x, y;
};
constexpr FaceLayout kStandardFaces{PadButton::South, PadButton::East, PadButton::West, PadButton::North};
constexpr FaceLayout kGameCubeFaces{PadButton::South, PadButton::West, PadButton::East, PadButton::North};
constexpr FaceLayout kNintendoLetters{PadButton::East, PadButton::South, PadButton::North, PadButton::West};

FaceLayout PositionFaces(Device device) {
  switch (device) {
  case Device::Xbox:
  case Device::GameCube:
  case Device::NsoGameCube: return kGameCubeFaces;
  default: return kStandardFaces;
  }
}

}

const char* GcInputId(GcInput input) { return Index(input) < kGcInputCount ? kGcInputs[Index(input)].id : ""; }
const char* GcInputName(GcInput input) { return Index(input) < kGcInputCount ? kGcInputs[Index(input)].name : ""; }

std::optional<GcInput> GcInputFromId(std::string_view id) {
  for (size_t i = 0; i < kGcInputCount; i++) {
    if (id == kGcInputs[i].id) {
      return static_cast<GcInput>(i);
    }
  }
  return std::nullopt;
}

const char* ActionId(Action action) {
  const size_t i = static_cast<size_t>(action);
  return i < kActionCount ? kActions[i].id : "";
}
const char* ActionName(Action action) {
  const size_t i = static_cast<size_t>(action);
  return i < kActionCount ? kActions[i].name : "";
}
std::optional<Action> ActionFromId(std::string_view id) {
  for (size_t i = 0; i < kActionCount; i++) {
    if (id == kActions[i].id) {
      return static_cast<Action>(i);
    }
  }
  return std::nullopt;
}

ActionMap DefaultActions(int port) {
  ActionMap m;
  const size_t ff = static_cast<size_t>(Action::FastForward);
  if (port == 0) {
    m.keys[ff] = {*ParseKeyToken("tab")};
  }
  m.pad[ff] = {Input::Button(PadButton::RightStick)};
  return m;
}

const char* TargetId(Target target) {
  const size_t i = static_cast<size_t>(target);
  return i < kTargetCount ? kTargetIds[i] : "";
}

bool IsAxisTarget(Target target) { return target >= Target::StickX && target <= Target::TriggerR; }

const char* DeviceId(Device device) {
  const size_t i = static_cast<size_t>(device);
  return i < std::size(kDeviceIds) ? kDeviceIds[i] : "generic";
}

std::string InputToken(const Input& input) {
  switch (input.kind) {
  case Input::Kind::None: return "none";
  case Input::Kind::Key: {
    const KeyInfo* k = FindKey(input.code);
    return k != nullptr ? k->token : "none";
  }
  case Input::Kind::Button:
    return input.code < static_cast<uint16_t>(PadButton::Count) ? kPadButtonTokens[input.code] : "none";
  case Input::Kind::Axis:
    for (const AxisToken& a : kAxisTokens) {
      if (a.axis == input.code && a.sign == input.sign) {
        return a.token;
      }
    }
    return "none";
  case Input::Kind::Label: return input.code < std::size(kLabelTokens) ? kLabelTokens[input.code] : "none";
  }
  return "none";
}

std::optional<Input> ParseKeyToken(std::string_view token) {
  for (const KeyInfo& k : kKeys) {
    if (token == k.token) {
      return Input::Key(k.scancode);
    }
  }
  return std::nullopt;
}

std::optional<Input> ParsePadToken(std::string_view token) {
  for (size_t b = 0; b < static_cast<size_t>(PadButton::Count); b++) {
    if (token == kPadButtonTokens[b]) {
      return Input::Button(static_cast<PadButton>(b));
    }
  }
  for (const AxisToken& a : kAxisTokens) {
    if (token == a.token) {
      return Input::Axis(static_cast<PadAxis>(a.axis), a.sign);
    }
  }
  for (size_t l = 0; l < std::size(kLabelTokens); l++) {
    if (token == kLabelTokens[l]) {
      return Input::Label(static_cast<FaceLabel>(l));
    }
  }
  return std::nullopt;
}

std::string KeyListText(const std::vector<Input>& keys) {
  if (keys.empty()) {
    return "none";
  }
  std::string s;
  for (const Input& k : keys) {
    s += (s.empty() ? "" : ", ") + InputToken(k);
  }
  return s;
}

std::string PadListText(const std::optional<std::vector<Input>>& inputs) {
  return inputs ? KeyListText(*inputs) : "default";
}

bool ParseKeyList(std::string_view text, std::vector<Input>& out) {
  out.clear();
  std::vector<std::string_view> tokens;
  if (!SplitList(text, tokens)) {
    return false;
  }
  if (tokens.size() == 1 && tokens[0] == "none") {
    return true;
  }
  for (std::string_view t : tokens) {
    const std::optional<Input> key = ParseKeyToken(t);
    if (!key) {
      out.clear();
      return false;
    }
    if (std::find(out.begin(), out.end(), *key) == out.end()) {
      out.push_back(*key);
    }
  }
  return true;
}

bool ParsePadList(std::string_view text, std::optional<std::vector<Input>>& out) {
  out.reset();
  std::vector<std::string_view> tokens;
  if (!SplitList(text, tokens)) {
    return false;
  }
  if (tokens.size() == 1 && tokens[0] == "default") {
    return true;
  }
  std::vector<Input> inputs;
  if (!(tokens.size() == 1 && tokens[0] == "none")) {
    for (std::string_view t : tokens) {
      const std::optional<Input> input = ParsePadToken(t);
      if (!input) {
        return false;
      }
      if (std::find(inputs.begin(), inputs.end(), *input) == inputs.end()) {
        inputs.push_back(*input);
      }
    }
  }
  out = std::move(inputs);
  return true;
}

KeyboardMap DefaultKeyboard(int port) {
  KeyboardMap m;
  if (port != 0) {
    return m;
  }
  const auto set = [&](GcInput input, const char* token) { m.keys[Index(input)] = {*ParseKeyToken(token)}; };
  set(GcInput::A, "x");
  set(GcInput::B, "z");
  set(GcInput::X, "c");
  set(GcInput::Y, "s");
  set(GcInput::Z, "d");
  set(GcInput::Start, "enter");
  set(GcInput::L, "q");
  set(GcInput::R, "w");
  set(GcInput::DpadUp, "t");
  set(GcInput::DpadDown, "g");
  set(GcInput::DpadLeft, "f");
  set(GcInput::DpadRight, "h");
  set(GcInput::StickUp, "up");
  set(GcInput::StickDown, "down");
  set(GcInput::StickLeft, "left");
  set(GcInput::StickRight, "right");
  set(GcInput::CStickUp, "i");
  set(GcInput::CStickDown, "k");
  set(GcInput::CStickLeft, "j");
  set(GcInput::CStickRight, "l");
  return m;
}

GamepadMap DefaultGamepad() { return GamepadMap{}; }

ControllerChoice DefaultChoice(int port) { return port == 0 ? ControllerChoice::Auto() : ControllerChoice{}; }

PortMap DefaultPort(int port) {
  return PortMap{DefaultKeyboard(port), DefaultGamepad(), DefaultChoice(port), DefaultActions(port)};
}

std::string ChoiceText(const ControllerChoice& choice) {
  switch (choice.mode) {
  case ControllerChoice::Mode::Auto: return "auto";
  case ControllerChoice::Mode::None: return "none";
  case ControllerChoice::Mode::Pinned: break;
  }
  std::string s = choice.guid;
  if (choice.nth > 0) {
    s += "#" + std::to_string(choice.nth);
  }
  if (!choice.name.empty()) {
    s += "|" + choice.name;
  }
  return s;
}

bool ParseChoice(std::string_view text, ControllerChoice& out) {
  text = Trim(text);
  if (text == "auto") {
    out = ControllerChoice::Auto();
    return true;
  }
  if (text == "none") {
    out = ControllerChoice{};
    return true;
  }
  const size_t bar = text.find('|');
  std::string_view id = Trim(text.substr(0, bar));
  const std::string name(bar == std::string_view::npos ? std::string_view() : Trim(text.substr(bar + 1)));
  int nth = 0;
  if (const size_t hash = id.find('#'); hash != std::string_view::npos) {
    const std::string_view digits = id.substr(hash + 1);
    if (digits.empty() || digits.size() > 2) {
      return false;
    }
    for (char c : digits) {
      if (c < '0' || c > '9') {
        return false;
      }
      nth = nth * 10 + (c - '0');
    }
    id = id.substr(0, hash);
  }
  if (id.size() != 32 || name.find_first_of("\r\n") != std::string::npos) {
    return false;
  }
  std::string guid;
  for (char c : id) {
    if (c >= 'A' && c <= 'F') {
      c = static_cast<char>(c - 'A' + 'a');
    }
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) {
      return false;
    }
    guid += c;
  }
  out = ControllerChoice::Pin(std::move(guid), nth, name);
  return true;
}

int NthOfModel(const std::vector<ConnectedPad>& pads, size_t index) {
  int nth = 0;
  for (size_t i = 0; i < index && i < pads.size(); i++) {
    nth += pads[i].guid == pads[index].guid ? 1 : 0;
  }
  return nth;
}

bool IsBuiltInPad(const ConnectedPad& pad) {
  if (pad.vendor == 0x28de && pad.product == 0x1205) {
    return true;
  }
  return pad.name.rfind("Steam Deck", 0) == 0;
}

bool AutoFollowsPresses(const std::array<ControllerChoice, 4>& choices) {
  for (size_t port = 1; port < choices.size(); port++) {
    if (choices[port].mode != ControllerChoice::Mode::None) {
      return false;
    }
  }
  return true;
}

int PreferredAutoPad(const std::vector<ConnectedPad>& pads, int lastPressed) {
  if (pads.size() < 2) {
    return -1;
  }
  if (lastPressed >= 0 && static_cast<size_t>(lastPressed) < pads.size()) {
    return lastPressed;
  }
  bool anyBuiltIn = false;
  for (const ConnectedPad& pad : pads) {
    anyBuiltIn = anyBuiltIn || IsBuiltInPad(pad);
  }
  if (!anyBuiltIn) {
    return -1;
  }
  for (size_t i = 0; i < pads.size(); i++) {
    if (!IsBuiltInPad(pads[i])) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

std::array<int, 4> AssignControllers(const std::array<ControllerChoice, 4>& choices,
                                     const std::vector<ConnectedPad>& pads) {
  std::array<int, 4> out{-1, -1, -1, -1};
  std::vector<bool> taken(pads.size(), false);
  for (size_t port = 0; port < choices.size(); port++) {
    const ControllerChoice& c = choices[port];
    if (c.mode != ControllerChoice::Mode::Pinned) {
      continue;
    }
    for (size_t i = 0; i < pads.size(); i++) {
      if (!taken[i] && pads[i].guid == c.guid && NthOfModel(pads, i) == c.nth) {
        out[port] = static_cast<int>(i);
        taken[i] = true;
        break;
      }
    }
  }
  for (size_t port = 0; port < choices.size(); port++) {
    if (choices[port].mode != ControllerChoice::Mode::Auto) {
      continue;
    }
    for (size_t i = 0; i < pads.size(); i++) {
      if (!taken[i]) {
        out[port] = static_cast<int>(i);
        taken[i] = true;
        break;
      }
    }
  }
  return out;
}

const char* PadPresetName(PadPreset preset) {
  switch (preset) {
  case PadPreset::GameCubeAdapter: return "GameCube controller (adapter)";
  case PadPreset::Xbox: return "Xbox";
  case PadPreset::PlayStation: return "PlayStation";
  case PadPreset::SwitchPro: return "Switch Pro";
  case PadPreset::Count: break;
  }
  return "";
}

namespace {
void PresetShape(PadPreset preset, Device& device, Layout& layout) {
  layout = Layout::Position;
  switch (preset) {
  case PadPreset::GameCubeAdapter: device = Device::GameCube; break;
  case PadPreset::Xbox: device = Device::Xbox; break;
  case PadPreset::PlayStation: device = Device::PlayStation; break;
  case PadPreset::SwitchPro:
  default:
    device = Device::Switch;
    layout = Layout::Label;
    break;
  }
}
}

GamepadMap ApplyPadPreset(const GamepadMap& map, PadPreset preset) {
  GamepadMap out = map;
  Device device;
  PresetShape(preset, device, out.layout);
  out.kind = device;
  for (auto& b : out.buttons) {
    b.reset();
  }
  return out;
}

std::optional<PadPreset> PadPresetOf(const GamepadMap& map) {
  for (int p = 0; p < static_cast<int>(PadPreset::Count); p++) {
    const PadPreset preset = static_cast<PadPreset>(p);
    if (ApplyPadPreset(map, preset) == map) {
      return preset;
    }
  }
  return std::nullopt;
}

std::vector<Input> DefaultPadInputs(GcInput input, Device device, Layout layout) {
  using B = PadButton;
  const FaceLayout faces = PositionFaces(device);
  const bool labels = EffectiveLayout(layout, device) == Layout::Label;
  switch (input) {
  case GcInput::A: return {labels ? Input::Label(FaceLabel::A) : Input::Button(faces.a)};
  case GcInput::B: return {labels ? Input::Label(FaceLabel::B) : Input::Button(faces.b)};
  case GcInput::X: return {labels ? Input::Label(FaceLabel::X) : Input::Button(faces.x)};
  case GcInput::Y: return {labels ? Input::Label(FaceLabel::Y) : Input::Button(faces.y)};
  case GcInput::Z: return {Input::Button(device == Device::NsoGameCube ? B::Back : B::RightShoulder)};
  case GcInput::Start: return {Input::Button(B::Start)};
  case GcInput::L:
    if (device == Device::GameCube) {
      return {Input::Button(B::Misc3), Input::Axis(PadAxis::LeftTrigger, 1)};
    }
    if (device == Device::NsoGameCube) {
      return {Input::Button(B::LeftShoulder), Input::Axis(PadAxis::LeftTrigger, 1)};
    }
    return {Input::Axis(PadAxis::LeftTrigger, 1)};
  case GcInput::R:
    if (device == Device::GameCube) {
      return {Input::Button(B::Misc4), Input::Axis(PadAxis::RightTrigger, 1)};
    }
    if (device == Device::NsoGameCube) {
      return {Input::Button(B::RightShoulder), Input::Axis(PadAxis::RightTrigger, 1)};
    }
    return {Input::Axis(PadAxis::RightTrigger, 1)};
  case GcInput::DpadUp: return {Input::Button(B::DpadUp)};
  case GcInput::DpadDown: return {Input::Button(B::DpadDown)};
  case GcInput::DpadLeft: return {Input::Button(B::DpadLeft)};
  case GcInput::DpadRight: return {Input::Button(B::DpadRight)};
  case GcInput::StickUp: return {Input::Axis(PadAxis::LeftY, -1)};
  case GcInput::StickDown: return {Input::Axis(PadAxis::LeftY, 1)};
  case GcInput::StickLeft: return {Input::Axis(PadAxis::LeftX, -1)};
  case GcInput::StickRight: return {Input::Axis(PadAxis::LeftX, 1)};
  case GcInput::CStickUp: return {Input::Axis(PadAxis::RightY, -1)};
  case GcInput::CStickDown: return {Input::Axis(PadAxis::RightY, 1)};
  case GcInput::CStickLeft: return {Input::Axis(PadAxis::RightX, -1)};
  case GcInput::CStickRight: return {Input::Axis(PadAxis::RightX, 1)};
  case GcInput::Count: break;
  }
  return {};
}

std::vector<Input> PadInputs(const GamepadMap& map, GcInput input, Device device) {
  const auto& own = map.buttons[Index(input)];
  return own ? *own : DefaultPadInputs(input, device, map.layout);
}

bool UsesDefaultButtons(const GamepadMap& map, Device device) {
  return EffectiveLayout(map.layout, device) == Layout::Position && !map.kind &&
         std::all_of(map.buttons.begin(), map.buttons.end(), [](const auto& b) { return !b.has_value(); });
}

PadButton LabelPosition(FaceLabel label, Device device) {
  FaceLayout letters = kStandardFaces;
  if (device == Device::Switch) {
    letters = kNintendoLetters;
  } else if (device == Device::GameCube || device == Device::NsoGameCube) {
    letters = kGameCubeFaces;
  }
  switch (label) {
  case FaceLabel::A: return letters.a;
  case FaceLabel::B: return letters.b;
  case FaceLabel::X: return letters.x;
  case FaceLabel::Y: return letters.y;
  }
  return letters.a;
}

std::vector<Binding> ResolveKeyboard(const KeyboardMap& map) {
  std::vector<Binding> out;
  const float walkBack = 1.f - static_cast<float>(std::clamp(map.walkPercent, 0, 100)) / 100.f;
  for (size_t i = 0; i < kGcInputCount; i++) {
    const GcInput gc = static_cast<GcInput>(i);
    for (const Input& key : map.keys[i]) {
      if (key.kind != Input::Kind::Key) {
        continue;
      }
      Target target;
      float sign;
      if (const auto digital = DigitalTarget(gc)) {
        out.push_back({key, *digital});
      } else if (gc == GcInput::L || gc == GcInput::R) {
        out.push_back({key, gc == GcInput::L ? Target::L : Target::R});
        out.push_back({key, gc == GcInput::L ? Target::TriggerL : Target::TriggerR});
      } else if (StickTarget(gc, target, sign)) {
        out.push_back({key, target, sign});
        const bool mainStick = target == Target::StickX || target == Target::StickY;
        if (mainStick && map.walk.kind == Input::Kind::Key && walkBack > 0.f) {
          Binding back{key, target, -sign * walkBack};
          back.held = map.walk;
          out.push_back(back);
        }
      }
    }
  }
  return out;
}

std::vector<Binding> ResolveGamepad(const GamepadMap& map, Device device, bool emulateTriggers) {
  std::vector<Binding> out;
  const auto withDeadzone = [&](Binding b) {
    if (b.input.kind == Input::Kind::Axis) {
      const PadAxis axis = static_cast<PadAxis>(b.input.code);
      if (axis == PadAxis::LeftX || axis == PadAxis::LeftY) {
        b.deadZone = map.leftStickDeadzone;
      } else if (axis == PadAxis::RightX || axis == PadAxis::RightY) {
        b.deadZone = map.rightStickDeadzone;
      }
    }
    return b;
  };
  for (size_t i = 0; i < kGcInputCount; i++) {
    const GcInput gc = static_cast<GcInput>(i);
    std::vector<Input> inputs = PadInputs(map, gc, device);
    for (Input& in : inputs) {
      if (in.kind == Input::Kind::Label) {
        in = Input::Button(LabelPosition(static_cast<FaceLabel>(in.code), device));
      }
    }
    Target target;
    float sign;
    if (const auto digital = DigitalTarget(gc)) {
      for (const Input& in : inputs) {
        out.push_back(withDeadzone({in, *digital}));
      }
    } else if (gc == GcInput::L || gc == GcInput::R) {
      const bool left = gc == GcInput::L;
      const Target click = left ? Target::L : Target::R;
      const Target analog = left ? Target::TriggerL : Target::TriggerR;
      bool hasButton = false;
      for (const Input& in : inputs) {
        if (in.kind == Input::Kind::Axis) {
          out.push_back(withDeadzone({in, analog}));
        } else {
          out.push_back({in, click});
          hasButton = true;
        }
      }
      if (emulateTriggers && !hasButton) {
        for (const Input& in : inputs) {
          if (in.kind == Input::Kind::Axis) {
            Binding b = withDeadzone({in, click});
            b.threshold = left ? map.lClickThreshold : map.rClickThreshold;
            out.push_back(b);
          }
        }
      }
    } else if (StickTarget(gc, target, sign)) {
      for (const Input& in : inputs) {
        out.push_back(withDeadzone({in, target, sign}));
      }
    }
  }
  return out;
}

namespace {

float InputValue(const Input& in, const RawInputs& raw) {
  switch (in.kind) {
  case Input::Kind::Key:
    return std::find(raw.keys.begin(), raw.keys.end(), in.code) != raw.keys.end() ? 1.f : 0.f;
  case Input::Kind::Button:
    return in.code < raw.buttons.size() && raw.buttons[in.code] ? 1.f : 0.f;
  case Input::Kind::Axis: {
    if (in.code >= raw.axes.size()) {
      return 0.f;
    }
    const float v = raw.axes[in.code];
    if (in.sign > 0) {
      return v > 0.f ? v : 0.f;
    }
    if (in.sign < 0) {
      return v < 0.f ? -v : 0.f;
    }
    return v;
  }
  default: return 0.f;
  }
}

float Contribution(const Binding& b, const RawInputs& raw) {
  if (b.held.kind != Input::Kind::None && InputValue(b.held, raw) < 0.5f) {
    return 0.f;
  }
  float value = InputValue(b.input, raw);
  if (std::abs(value) <= b.deadZone) {
    value = 0.f;
  }
  if (!IsAxisTarget(b.target)) {
    return value >= b.threshold && value != 0.f ? 1.f : 0.f;
  }
  return std::clamp(value * b.scale, -1.f, 1.f);
}

int8_t ToStick(float v) { return static_cast<int8_t>(std::clamp(std::lround(v * 127.f), -127l, 127l)); }
uint8_t ToTrigger(float v) { return static_cast<uint8_t>(std::clamp(std::lround(v * 255.f), 0l, 255l)); }

}

float TargetValue(const std::vector<Binding>& bindings, Target target, const RawInputs& raw) {
  float sum = 0.f;
  for (const Binding& b : bindings) {
    if (b.target != target) {
      continue;
    }
    const float c = Contribution(b, raw);
    if (!IsAxisTarget(target) && c != 0.f) {
      return 1.f;
    }
    sum += c;
  }
  return std::clamp(sum, -1.f, 1.f);
}

PadReading Evaluate(const std::vector<Binding>& bindings, const RawInputs& raw) {
  struct Bit {
    Target target;
    uint16_t bit;
  };
  constexpr Bit kBits[] = {
      {Target::Left, 0x0001}, {Target::Right, 0x0002}, {Target::Down, 0x0004}, {Target::Up, 0x0008},
      {Target::Z, 0x0010},    {Target::R, 0x0020},     {Target::L, 0x0040},    {Target::A, 0x0100},
      {Target::B, 0x0200},    {Target::X, 0x0400},     {Target::Y, 0x0800},    {Target::Start, 0x1000},
  };
  PadReading r;
  for (const auto& [target, bit] : kBits) {
    if (TargetValue(bindings, target, raw) >= 0.5f) {
      r.buttons |= bit;
    }
  }
  r.stickX = ToStick(TargetValue(bindings, Target::StickX, raw));
  r.stickY = ToStick(TargetValue(bindings, Target::StickY, raw));
  r.substickX = ToStick(TargetValue(bindings, Target::CStickX, raw));
  r.substickY = ToStick(TargetValue(bindings, Target::CStickY, raw));
  r.triggerLeft = ToTrigger(TargetValue(bindings, Target::TriggerL, raw));
  r.triggerRight = ToTrigger(TargetValue(bindings, Target::TriggerR, raw));
  if (r.buttons & 0x0040) {
    r.triggerLeft = 180;
  }
  if (r.buttons & 0x0020) {
    r.triggerRight = 180;
  }
  return r;
}

}
