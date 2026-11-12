#include "controls.h"

#include "controls_tables.h"

namespace engine::controls {

namespace {

using detail::FindKey;

struct PadNames {
  const char* family;
  const char* buttons[static_cast<size_t>(PadButton::Count)];
  const char* lstick;
  const char* rstick;
  const char* ltrigger;
  const char* rtrigger;
};

#define PAD_TAIL "D-pad Up", "D-pad Down", "D-pad Left", "D-pad Right"
#define PAD_PADDLES "P1", "P3", "P2", "P4"

const PadNames kXbox{"xbox",
                     {"A", "B", "X", "Y", "View", "Xbox", "Menu", "LS", "RS", "LB", "RB", PAD_TAIL, "Share", PAD_PADDLES,
                      "Touchpad", "Misc 2", "Misc 3", "Misc 4", "Misc 5", "Misc 6"},
                     "Left stick", "Right stick", "LT", "RT"};
const PadNames kPlayStation{"ps",
                            {"Cross", "Circle", "Square", "Triangle", "Share", "PS", "Options", "L3", "R3", "L1",
                             "R1", PAD_TAIL, "Mic", "Right paddle", "Left paddle", "Right Fn", "Left Fn", "Touchpad",
                             "Misc 2", "Misc 3", "Misc 4", "Misc 5", "Misc 6"},
                            "Left stick", "Right stick", "L2", "R2"};
const PadNames kSwitch{"switch",
                       {"B", "A", "Y", "X", "Minus", "Home", "Plus", "LS", "RS", "L", "R", PAD_TAIL, "Capture",
                        "Right SR", "Left SL", "Right SL", "Left SR", "Touchpad", "Misc 2", "Misc 3", "Misc 4",
                        "Misc 5", "Misc 6"},
                       "Left stick", "Right stick", "ZL", "ZR"};
const PadNames kGameCube{"gc",
                         {"A", "X", "B", "Y", "Back", "Guide", "Start", "LS", "RS", "LB", "Z", PAD_TAIL, "Misc 1",
                          PAD_PADDLES, "Touchpad", "Misc 2", "L", "R", "Misc 5", "Misc 6"},
                         "Control stick", "C-stick", "L", "R"};
const PadNames kNsoGameCube{"gc",
                            {"A", "X", "B", "Y", "Z", "Home", "Start", "LS", "RS", "L", "R", PAD_TAIL, "Capture",
                             PAD_PADDLES, "Touchpad", "Misc 2", "Misc 3", "Misc 4", "Misc 5", "Misc 6"},
                            "Control stick", "C-stick", "L", "R"};
const PadNames kGeneric{"pad",
                        {"A", "B", "X", "Y", "Back", "Guide", "Start", "LS", "RS", "LB", "RB", PAD_TAIL, "Misc 1",
                         PAD_PADDLES, "Touchpad", "Misc 2", "Misc 3", "Misc 4", "Misc 5", "Misc 6"},
                        "Left stick", "Right stick", "LT", "RT"};

#undef PAD_TAIL
#undef PAD_PADDLES

const PadNames& NamesFor(Device device) {
  switch (device) {
  case Device::Xbox: return kXbox;
  case Device::PlayStation: return kPlayStation;
  case Device::Switch: return kSwitch;
  case Device::GameCube: return kGameCube;
  case Device::NsoGameCube: return kNsoGameCube;
  default: return kGeneric;
  }
}

std::string Slug(std::string_view text) {
  std::string s;
  for (char ch : text) {
    if (ch >= 'A' && ch <= 'Z') {
      s += static_cast<char>(ch - 'A' + 'a');
    } else if ((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9')) {
      s += ch;
    } else if (ch == '+') {
      s += "plus";
    } else if (ch == '-') {
      s += "minus";
    } else if (!s.empty() && s.back() != '_') {
      s += '_';
    }
  }
  while (!s.empty() && s.back() == '_') {
    s.pop_back();
  }
  return s;
}

ButtonLabel Named(const char* family, std::string text) {
  return ButtonLabel{text, std::string(family) + "_" + Slug(text)};
}

}

ButtonLabel InputLabel(const Input& input, Device device) {
  switch (input.kind) {
  case Input::Kind::None: return {};
  case Input::Kind::Key: {
    const detail::KeyInfo* k = FindKey(input.code);
    if (k == nullptr) {
      return ButtonLabel{"Key " + std::to_string(input.code), "key_" + std::to_string(input.code)};
    }
    return ButtonLabel{k->name, std::string("key_") + k->token};
  }
  case Input::Kind::Label:
    return InputLabel(Input::Button(LabelPosition(static_cast<FaceLabel>(input.code), device)), device);
  case Input::Kind::Button: {
    const PadNames& n = NamesFor(device);
    if (input.code >= static_cast<uint16_t>(PadButton::Count)) {
      return {};
    }
    return Named(n.family, n.buttons[input.code]);
  }
  case Input::Kind::Axis: {
    const PadNames& n = NamesFor(device);
    const PadAxis axis = static_cast<PadAxis>(input.code);
    if (axis == PadAxis::LeftTrigger || axis == PadAxis::RightTrigger) {
      return Named(n.family, axis == PadAxis::LeftTrigger ? n.ltrigger : n.rtrigger);
    }
    const bool left = axis == PadAxis::LeftX || axis == PadAxis::LeftY;
    const bool horizontal = axis == PadAxis::LeftX || axis == PadAxis::RightX;
    const char* dir = horizontal ? (input.sign < 0 ? "left" : "right") : (input.sign < 0 ? "up" : "down");
    return Named(n.family, std::string(left ? n.lstick : n.rstick) + " " + dir);
  }
  }
  return {};
}

ButtonLabel GcInputLabel(const PortMap& map, GcInput input, Device device) {
  if (static_cast<size_t>(input) >= kGcInputCount) {
    return {};
  }
  if (device == Device::Keyboard) {
    const std::vector<Input>& keys = map.keyboard.keys[static_cast<size_t>(input)];
    return keys.empty() ? ButtonLabel{} : InputLabel(keys.front(), device);
  }
  const std::vector<Input> inputs = PadInputs(map.gamepad, input, device);
  return inputs.empty() ? ButtonLabel{} : InputLabel(inputs.front(), device);
}

}
