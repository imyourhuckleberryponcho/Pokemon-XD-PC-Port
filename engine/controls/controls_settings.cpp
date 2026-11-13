#include "controls.h"

#include <settings.h>

#include <iterator>
#include <string>

namespace engine::controls {

namespace {

using engine::settings::Category;
using engine::settings::Definition;
using engine::settings::Registry;

std::string Section(const char* device, int port) { return std::string(device) + std::to_string(port + 1); }
std::string Id(const char* device, int port, std::string_view key) {
  return Section(device, port) + "." + std::string(key);
}

bool ValidKeyList(std::string_view text) {
  std::vector<Input> keys;
  return ParseKeyList(text, keys);
}

bool ValidWalkKey(std::string_view text) {
  std::vector<Input> keys;
  return ParseKeyList(text, keys) && keys.size() <= 1;
}

bool ValidChoice(std::string_view text) {
  ControllerChoice c;
  return ParseChoice(text, c);
}

constexpr Device kKinds[] = {Device::GameCube, Device::Xbox, Device::PlayStation, Device::Switch};
constexpr const char* kKindIds[] = {"gamecube", "xbox", "playstation", "switch"};

bool ValidPadList(std::string_view text) {
  std::optional<std::vector<Input>> inputs;
  return ParsePadList(text, inputs);
}

std::string What(GcInput input) {
  switch (input) {
  case GcInput::A:
  case GcInput::B:
  case GcInput::X:
  case GcInput::Y:
  case GcInput::Z:
  case GcInput::Start: return std::string("the GameCube ") + GcInputName(input) + " button";
  case GcInput::L:
  case GcInput::R: return std::string("the GameCube ") + GcInputName(input) + " trigger";
  default: {
    std::string name = GcInputName(input);
    if (name[0] == 'M') {
      name[0] = 'm';
    }
    return "the GameCube " + name;
  }
  }
}

constexpr const char* kKeyboard = "keyboard";
constexpr const char* kGamepad = "gamepad";

}

void RegisterSettings(Registry& registry) {
  for (int port = 0; port < kPorts; port++) {
    const std::string controller = "controller " + std::to_string(port + 1);
    const KeyboardMap keys = DefaultKeyboard(port);
    for (size_t i = 0; i < kGcInputCount; i++) {
      const GcInput gc = static_cast<GcInput>(i);
      const std::string def = KeyListText(keys.keys[i]);
      registry.Add(Definition::Text(Id(kKeyboard, port, GcInputId(gc)), Category::Controls, GcInputName(gc),
                                    "Keyboard keys for " + What(gc) + " on " + controller +
                                        ", separated by commas (none: no key).",
                                    def, def, ValidKeyList)
                       .Preference().Implemented());
    }
    registry.Add(Definition::Text(Id(kKeyboard, port, "walk"), Category::Controls, "Walk key",
                                  "While this key is held, the keys for the main stick tilt it only part way "
                                  "(walk_tilt), as a gentle push on a real stick does (none: no walk key).",
                                  "none", "none", ValidWalkKey)
                     .Preference().Implemented());
    registry.Add(Definition::Int(Id(kKeyboard, port, "walk_tilt"), Category::Controls, "Walk tilt",
                                 "How far the main stick tilts while the walk key is held, in percent.", 10, 90,
                                 kDefaultWalkPercent, kDefaultWalkPercent)
                     .Preference().Implemented());
    const ActionMap actions = DefaultActions(port);
    for (size_t a = 0; a < kActionCount; a++) {
      const std::string def = KeyListText(actions.keys[a]);
      registry.Add(Definition::Text(Id(kKeyboard, port, ActionId(static_cast<Action>(a))), Category::Controls,
                                    ActionName(static_cast<Action>(a)),
                                    std::string("Keyboard keys for the port's ") + ActionName(static_cast<Action>(a)) +
                                        " (not a GameCube button; does something only while that setting is on), "
                                        "separated by commas (none: no key).",
                                    def, def, ValidKeyList)
                       .Preference().Implemented());
    }

    const std::string choice = ChoiceText(DefaultChoice(port));
    registry.Add(Definition::Text(Id(kGamepad, port, "controller"), Category::Controls, "Controller",
                                  "Which PC controller plays as " + controller +
                                      ". auto: the first one plugged in that no other controller number has; none: "
                                      "no controller (the keyboard still works); or one particular controller (as "
                                      "the launcher's Controls page writes it: its model number, #which one when "
                                      "several of the same are plugged in, |its name).",
                                  choice, choice, ValidChoice)
                     .Preference().Implemented());
    registry.Add(Definition::Enum(Id(kGamepad, port, "kind"), Category::Controls, "Controller type",
                                  "What kind of controller it is, for its standard buttons and the names shown. "
                                  "auto: as the controller reports itself. The presets on the Controls page set it.",
                                  {{"auto", "Auto"},
                                   {"gamecube", "GameCube controller"},
                                   {"xbox", "Xbox"},
                                   {"playstation", "PlayStation"},
                                   {"switch", "Switch"}},
                                  "auto", "auto")
                     .Preference().Implemented());
    registry.Add(Definition::Bool(Id(kGamepad, port, "rumble"), Category::Controls, "Rumble",
                                  "The game's rumble reaches the controller (on, as on the GameCube; the game's own "
                                  "Rumble option still decides when it rumbles).",
                                  true, true)
                     .Preference().Implemented());
    registry.Add(Definition::Enum(Id(kGamepad, port, "layout"), Category::Controls, "Button layout",
                                  "Where the GameCube's A, B, X and Y buttons go on a PC controller by default. "
                                  "auto: by the letter printed on Nintendo controllers (Switch Pro, Joy-Cons), by "
                                  "place on the others; position: by place (A is the bottom button, as on the "
                                  "GameCube); label: by the letter printed on the button.",
                                  {{"auto", "Auto"}, {"position", "By position"}, {"label", "By label"}}, "auto", "auto")
                     .Preference().Implemented());
    for (size_t i = 0; i < kGcInputCount; i++) {
      const GcInput gc = static_cast<GcInput>(i);
      registry.Add(Definition::Text(Id(kGamepad, port, GcInputId(gc)), Category::Controls, GcInputName(gc),
                                    "PC controller buttons for " + What(gc) + " on " + controller +
                                        ", separated by commas: south/east/west/north (by place), a/b/x/y (by "
                                        "letter), lshoulder, ltrigger, lstick_up, dpad_up... default: the usual "
                                        "button for the controller plugged in; none: no button.",
                                    "default", "default", ValidPadList)
                       .Preference().Implemented());
    }
    for (size_t a = 0; a < kActionCount; a++) {
      const std::string def = PadListText(actions.pad[a]);
      registry.Add(Definition::Text(Id(kGamepad, port, ActionId(static_cast<Action>(a))), Category::Controls,
                                    ActionName(static_cast<Action>(a)),
                                    std::string("PC controller buttons for the port's ") +
                                        ActionName(static_cast<Action>(a)) + " on " + controller +
                                        " (not a GameCube button), separated by commas as above (rstick_click: the "
                                        "right stick pressed in; none: no button).",
                                    def, def, ValidPadList)
                       .Preference().Implemented());
    }
    registry.Add(Definition::Float(Id(kGamepad, port, "left_stick_deadzone"), Category::Controls,
                                   "Left stick deadzone",
                                   "How far the controller's left stick can move before it counts (0 to 0.9 of the "
                                   "way), so a worn stick doesn't drift.",
                                   0.0f, 0.9f, kDefaultStickDeadzone, kDefaultStickDeadzone)
                     .Preference().Implemented());
    registry.Add(Definition::Float(Id(kGamepad, port, "right_stick_deadzone"), Category::Controls,
                                   "Right stick deadzone", "The same for the controller's right stick.", 0.0f, 0.9f,
                                   kDefaultStickDeadzone, kDefaultStickDeadzone)
                     .Preference().Implemented());
    registry.Add(Definition::Float(Id(kGamepad, port, "l_click"), Category::Controls, "L click point",
                                   "How far a trigger must be pulled to click the GameCube L button, for "
                                   "controllers whose triggers don't click by themselves.",
                                   0.05f, 1.0f, kDefaultClickThreshold, kDefaultClickThreshold)
                     .Preference().Implemented());
    registry.Add(Definition::Float(Id(kGamepad, port, "r_click"), Category::Controls, "R click point",
                                   "The same for the GameCube R button.", 0.05f, 1.0f, kDefaultClickThreshold,
                                   kDefaultClickThreshold)
                     .Preference().Implemented());
  }
}

PortMap ReadPort(const Registry& registry, int port) {
  PortMap map = DefaultPort(port);
  if (port < 0 || port >= kPorts) {
    return map;
  }
  for (size_t i = 0; i < kGcInputCount; i++) {
    const char* key = GcInputId(static_cast<GcInput>(i));
    std::vector<Input> keys;
    if (ParseKeyList(registry.Text(Id(kKeyboard, port, key)), keys)) {
      map.keyboard.keys[i] = std::move(keys);
    }
    std::optional<std::vector<Input>> pad;
    if (ParsePadList(registry.Text(Id(kGamepad, port, key)), pad)) {
      map.gamepad.buttons[i] = std::move(pad);
    }
  }
  std::vector<Input> walk;
  if (ParseKeyList(registry.Text(Id(kKeyboard, port, "walk")), walk) && walk.size() == 1) {
    map.keyboard.walk = walk.front();
  }
  if (const auto tilt = registry.Int(Id(kKeyboard, port, "walk_tilt")); tilt.Valid()) {
    map.keyboard.walkPercent = tilt.Get();
  }
  if (const auto layout = registry.Enum(Id(kGamepad, port, "layout")); layout.Valid()) {
    const std::string id = Id(kGamepad, port, "layout");
    map.gamepad.layout = layout.Get() == registry.ChoiceIndex(id, "label")      ? Layout::Label
                         : layout.Get() == registry.ChoiceIndex(id, "position") ? Layout::Position
                                                                                 : Layout::Auto;
  }
  if (ControllerChoice c; ParseChoice(registry.Text(Id(kGamepad, port, "controller")), c)) {
    map.controller = std::move(c);
  }
  if (const auto kind = registry.Enum(Id(kGamepad, port, "kind")); kind.Valid()) {
    for (size_t k = 0; k < std::size(kKinds); k++) {
      if (kind.Get() == registry.ChoiceIndex(Id(kGamepad, port, "kind"), kKindIds[k])) {
        map.gamepad.kind = kKinds[k];
      }
    }
  }
  if (const auto rumble = registry.Bool(Id(kGamepad, port, "rumble")); rumble.Valid()) {
    map.gamepad.rumble = rumble.Get();
  }
  const auto readFloat = [&](const char* key, float& out) {
    if (const auto f = registry.Float(Id(kGamepad, port, key)); f.Valid()) {
      out = f.Get();
    }
  };
  readFloat("left_stick_deadzone", map.gamepad.leftStickDeadzone);
  readFloat("right_stick_deadzone", map.gamepad.rightStickDeadzone);
  readFloat("l_click", map.gamepad.lClickThreshold);
  readFloat("r_click", map.gamepad.rClickThreshold);
  for (size_t a = 0; a < kActionCount; a++) {
    const char* key = ActionId(static_cast<Action>(a));
    std::vector<Input> keys;
    if (ParseKeyList(registry.Text(Id(kKeyboard, port, key)), keys)) {
      map.actions.keys[a] = std::move(keys);
    }
    std::optional<std::vector<Input>> pad;
    if (ParsePadList(registry.Text(Id(kGamepad, port, key)), pad)) {
      map.actions.pad[a] = pad ? std::move(*pad) : DefaultActions(port).pad[a];
    }
  }
  return map;
}

bool WritePort(Registry& registry, int port, const PortMap& map) {
  if (port < 0 || port >= kPorts) {
    return false;
  }
  bool ok = true;
  for (size_t i = 0; i < kGcInputCount; i++) {
    const char* key = GcInputId(static_cast<GcInput>(i));
    ok &= registry.SetText(Id(kKeyboard, port, key), KeyListText(map.keyboard.keys[i]));
    ok &= registry.SetText(Id(kGamepad, port, key), PadListText(map.gamepad.buttons[i]));
  }
  ok &= registry.SetText(Id(kKeyboard, port, "walk"),
                         map.keyboard.walk.kind == Input::Kind::Key ? InputToken(map.keyboard.walk) : "none");
  ok &= registry.SetInt(Id(kKeyboard, port, "walk_tilt"), map.keyboard.walkPercent);
  ok &= registry.SetText(Id(kGamepad, port, "controller"), ChoiceText(map.controller));
  const char* kind = "auto";
  for (size_t k = 0; k < std::size(kKinds); k++) {
    if (map.gamepad.kind == kKinds[k]) {
      kind = kKindIds[k];
    }
  }
  ok &= registry.SetEnum(Id(kGamepad, port, "kind"), kind);
  ok &= registry.SetBool(Id(kGamepad, port, "rumble"), map.gamepad.rumble);
  ok &= registry.SetEnum(Id(kGamepad, port, "layout"), map.gamepad.layout == Layout::Label      ? "label"
                                                       : map.gamepad.layout == Layout::Position ? "position"
                                                                                                : "auto");
  ok &= registry.SetFloat(Id(kGamepad, port, "left_stick_deadzone"), map.gamepad.leftStickDeadzone);
  ok &= registry.SetFloat(Id(kGamepad, port, "right_stick_deadzone"), map.gamepad.rightStickDeadzone);
  ok &= registry.SetFloat(Id(kGamepad, port, "l_click"), map.gamepad.lClickThreshold);
  ok &= registry.SetFloat(Id(kGamepad, port, "r_click"), map.gamepad.rClickThreshold);
  for (size_t a = 0; a < kActionCount; a++) {
    const char* key = ActionId(static_cast<Action>(a));
    ok &= registry.SetText(Id(kKeyboard, port, key), KeyListText(map.actions.keys[a]));
    ok &= registry.SetText(Id(kGamepad, port, key), PadListText(map.actions.pad[a]));
  }
  return ok;
}

std::vector<std::string> UnknownEntries(const Registry& registry) {
  std::vector<std::string> messages;
  for (const std::string& id : registry.UnknownIds()) {
    for (int port = 0; port < kPorts; port++) {
      for (const char* device : {kKeyboard, kGamepad}) {
        const std::string prefix = Section(device, port) + ".";
        if (id.rfind(prefix, 0) == 0) {
          messages.push_back("settings.ini: [" + Section(device, port) + "] has no entry called \"" +
                             id.substr(prefix.size()) + "\"; ignored (it is kept in the file)");
        }
      }
    }
  }
  return messages;
}

}
