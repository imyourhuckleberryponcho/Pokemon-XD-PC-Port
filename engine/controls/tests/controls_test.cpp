#include <controls.h>
#include <settings.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace engine::controls;
using engine::settings::Preset;
using engine::settings::Registry;
namespace fs = std::filesystem;

namespace {

bool sOk = true;
std::vector<std::string> sWarnings;

void check(bool cond, const char* what) {
  std::printf("%s %s\n", cond ? "ok  " : "FAIL", what);
  sOk &= cond;
}

void setup(Registry& r) {
  r.SetWarningSink([](const std::string& m) { sWarnings.push_back(m); });
  engine::settings::RegisterStandardSettings(r);
  RegisterSettings(r);
}

bool SameSet(std::vector<Binding> a, std::vector<Binding> b) {
  if (a.size() != b.size()) {
    return false;
  }
  for (const Binding& x : a) {
    const auto it = std::find(b.begin(), b.end(), x);
    if (it == b.end()) {
      std::printf("     missing: %s -> %s (scale %g, threshold %g)\n", InputToken(x.input).c_str(), TargetId(x.target),
                  x.scale, x.threshold);
      return false;
    }
    b.erase(it);
  }
  return true;
}

Binding B(Input in, Target t, float scale = 1.f, float threshold = 0.5f, float deadZone = 0.f) {
  Binding b{in, t, scale, threshold, deadZone};
  return b;
}

Input Key(int scancode) { return Input::Key(static_cast<uint16_t>(scancode)); }

void test_keyboard_defaults() {
  const std::vector<Binding> today = {
      B(Key(27), Target::A),         B(Key(29), Target::B),        B(Key(6), Target::X),
      B(Key(22), Target::Y),         B(Key(7), Target::Z),         B(Key(40), Target::Start),
      B(Key(20), Target::L),         B(Key(26), Target::R),        B(Key(20), Target::TriggerL),
      B(Key(26), Target::TriggerR),  B(Key(23), Target::Up),       B(Key(10), Target::Down),
      B(Key(9), Target::Left),       B(Key(11), Target::Right),    B(Key(79), Target::StickX, 1.f),
      B(Key(80), Target::StickX, -1.f), B(Key(82), Target::StickY, 1.f), B(Key(81), Target::StickY, -1.f),
      B(Key(15), Target::CStickX, 1.f), B(Key(13), Target::CStickX, -1.f), B(Key(12), Target::CStickY, 1.f),
      B(Key(14), Target::CStickY, -1.f),
  };
  check(SameSet(ResolveKeyboard(DefaultKeyboard(0)), today), "controller 1's default keys = keyboard_defaults.cpp");
  bool othersEmpty = true;
  for (int port = 1; port < kPorts; port++) {
    othersEmpty &= ResolveKeyboard(DefaultKeyboard(port)).empty();
  }
  check(othersEmpty, "controllers 2-4 have no keys by default (as before)");
  check(DefaultKeyboard(0).walk.kind == Input::Kind::None, "no walk key by default (nothing new happens)");
}

std::vector<Binding> AuroraDefaults(PadButton a, PadButton b, PadButton x, PadButton y, PadButton z,
                                    std::optional<PadButton> l, std::optional<PadButton> r, bool emulate) {
  using P = PadButton;
  const float dz = kDefaultStickDeadzone;
  std::vector<Binding> v = {
      B(Input::Button(a), Target::A),          B(Input::Button(b), Target::B),
      B(Input::Button(x), Target::X),          B(Input::Button(y), Target::Y),
      B(Input::Button(z), Target::Z),          B(Input::Button(P::Start), Target::Start),
      B(Input::Button(P::DpadUp), Target::Up), B(Input::Button(P::DpadDown), Target::Down),
      B(Input::Button(P::DpadLeft), Target::Left), B(Input::Button(P::DpadRight), Target::Right),
      B(Input::Axis(PadAxis::LeftX, 1), Target::StickX, 1.f, 0.5f, dz),
      B(Input::Axis(PadAxis::LeftX, -1), Target::StickX, -1.f, 0.5f, dz),
      B(Input::Axis(PadAxis::LeftY, -1), Target::StickY, 1.f, 0.5f, dz),
      B(Input::Axis(PadAxis::LeftY, 1), Target::StickY, -1.f, 0.5f, dz),
      B(Input::Axis(PadAxis::RightX, 1), Target::CStickX, 1.f, 0.5f, dz),
      B(Input::Axis(PadAxis::RightX, -1), Target::CStickX, -1.f, 0.5f, dz),
      B(Input::Axis(PadAxis::RightY, -1), Target::CStickY, 1.f, 0.5f, dz),
      B(Input::Axis(PadAxis::RightY, 1), Target::CStickY, -1.f, 0.5f, dz),
      B(Input::Axis(PadAxis::LeftTrigger, 1), Target::TriggerL),
      B(Input::Axis(PadAxis::RightTrigger, 1), Target::TriggerR),
  };
  if (l) {
    v.push_back(B(Input::Button(*l), Target::L));
  } else if (emulate) {
    v.push_back(B(Input::Axis(PadAxis::LeftTrigger, 1), Target::L, 1.f, kDefaultClickThreshold));
  }
  if (r) {
    v.push_back(B(Input::Button(*r), Target::R));
  } else if (emulate) {
    v.push_back(B(Input::Axis(PadAxis::RightTrigger, 1), Target::R, 1.f, kDefaultClickThreshold));
  }
  return v;
}

void test_gamepad_defaults() {
  using P = PadButton;
  const GamepadMap def = DefaultGamepad();
  check(UsesDefaultButtons(def), "the default controller mapping leaves Aurora's defaults alone");
  check(SameSet(ResolveGamepad(def, Device::Xbox, true),
                AuroraDefaults(P::South, P::West, P::East, P::North, P::RightShoulder, {}, {}, true)),
        "Xbox: Aurora's Xbox table");
  for (Device d : {Device::PlayStation, Device::Switch, Device::Generic}) {
    check(SameSet(ResolveGamepad(def, d, true),
                  AuroraDefaults(P::South, P::East, P::West, P::North, P::RightShoulder, {}, {}, true)),
          (std::string(DeviceId(d)) + ": Aurora's standard table").c_str());
  }
  check(SameSet(ResolveGamepad(def, Device::GameCube, false),
                AuroraDefaults(P::South, P::West, P::East, P::North, P::RightShoulder, P::Misc3, P::Misc4, false)),
        "GameCube adapter: Aurora's GameCube table");
  check(SameSet(ResolveGamepad(def, Device::NsoGameCube, false),
                AuroraDefaults(P::South, P::West, P::East, P::North, P::Back, P::LeftShoulder, P::RightShoulder,
                               false)),
        "NSO GameCube controller: Aurora's NSO table");
}

void test_position_and_label() {
  GamepadMap label = DefaultGamepad();
  label.layout = Layout::Label;
  check(!UsesDefaultButtons(label), "label layout is a change");
  const auto padFor = [](const GamepadMap& m, Device d, Target t) {
    for (const Binding& b : ResolveGamepad(m, d, true)) {
      if (b.target == t && b.input.kind == Input::Kind::Button) {
        return static_cast<PadButton>(b.input.code);
      }
    }
    return PadButton::Count;
  };
  const GamepadMap pos = DefaultGamepad();
  check(padFor(pos, Device::Switch, Target::A) == PadButton::South, "Switch by position: A is the bottom button");
  check(padFor(label, Device::Switch, Target::A) == PadButton::East, "Switch by label: A is the right button (A)");
  check(padFor(label, Device::Switch, Target::B) == PadButton::South, "Switch by label: B is the bottom button (B)");
  check(padFor(pos, Device::Xbox, Target::B) == PadButton::West, "Xbox by position: B is the left button");
  check(padFor(label, Device::Xbox, Target::B) == PadButton::East, "Xbox by label: B is the button marked B");
  check(padFor(label, Device::PlayStation, Target::A) == PadButton::South &&
            padFor(label, Device::PlayStation, Target::B) == PadButton::East,
        "PlayStation by label: Xbox places (no letters)");
  check(padFor(label, Device::GameCube, Target::B) == PadButton::West, "GameCube controller: labels are positions");

  GamepadMap own = DefaultGamepad();
  own.buttons[static_cast<size_t>(GcInput::A)] = std::vector<Input>{Input::Label(FaceLabel::A)};
  check(padFor(own, Device::Switch, Target::A) == PadButton::East && padFor(own, Device::Xbox, Target::A) == PadButton::South,
        "\"a\" means the button marked A on each controller");
  check(padFor(own, Device::Switch, Target::B) == PadButton::East, "the other buttons keep their defaults");
}

void test_reading() {
  const std::vector<Binding> pad = ResolveGamepad(DefaultGamepad(), Device::Xbox, true);
  RawInputs raw;
  raw.axes[static_cast<size_t>(PadAxis::LeftX)] = 0.2f;
  check(Evaluate(pad, raw).stickX == 0, "stick inside the deadzone reads 0");
  raw.axes[static_cast<size_t>(PadAxis::LeftX)] = 0.3f;
  check(Evaluate(pad, raw).stickX == 38, "stick past the deadzone reads its value (0.3 -> 38)");
  raw.axes[static_cast<size_t>(PadAxis::LeftY)] = -1.f;
  check(Evaluate(pad, raw).stickY == 127, "stick up reads +127 (GameCube's up)");

  GamepadMap smaller = DefaultGamepad();
  smaller.leftStickDeadzone = 0.1f;
  raw = {};
  raw.axes[static_cast<size_t>(PadAxis::LeftX)] = 0.2f;
  check(Evaluate(ResolveGamepad(smaller, Device::Xbox, true), raw).stickX == 25, "a smaller deadzone lets 0.2 through");
  raw.axes[static_cast<size_t>(PadAxis::RightX)] = 0.2f;
  check(Evaluate(ResolveGamepad(smaller, Device::Xbox, true), raw).substickX == 0, "the right stick has its own");

  raw = {};
  raw.axes[static_cast<size_t>(PadAxis::LeftTrigger)] = 0.9f;
  PadReading r = Evaluate(pad, raw);
  check(r.triggerLeft == 230 && !(r.buttons & 0x40), "trigger at 0.9: analog only, no click");
  raw.axes[static_cast<size_t>(PadAxis::LeftTrigger)] = 0.96f;
  r = Evaluate(pad, raw);
  check(r.triggerLeft == 180 && (r.buttons & 0x40), "trigger at 0.96: L clicks (and reads 180)");
  GamepadMap early = DefaultGamepad();
  early.lClickThreshold = 0.5f;
  raw.axes[static_cast<size_t>(PadAxis::LeftTrigger)] = 0.6f;
  check((Evaluate(ResolveGamepad(early, Device::Xbox, true), raw).buttons & 0x40) != 0, "a lower click point");
  check((Evaluate(ResolveGamepad(early, Device::GameCube, false), raw).buttons & 0x40) == 0,
        "GameCube controller: its own click, not the click point");

  KeyboardMap keys = DefaultKeyboard(0);
  check(Evaluate(ResolveKeyboard(keys), RawInputs{{79}}).stickX == 127, "Right arrow: stick fully right");
  keys.walk = *ParseKeyToken("lshift");
  const std::vector<Binding> walking = ResolveKeyboard(keys);
  check(Evaluate(walking, RawInputs{{79}}).stickX == 127, "walk key set but not held: full tilt");
  check(Evaluate(walking, RawInputs{{79, 225}}).stickX == 64, "Right + Left Shift: half tilt (64)");
  check(Evaluate(walking, RawInputs{{80, 82, 225}}).stickX == -64 && Evaluate(walking, RawInputs{{80, 82, 225}}).stickY == 64,
        "walking diagonally: half on both axes");
  check(Evaluate(walking, RawInputs{{15, 225}}).substickX == 127, "walk leaves the C-stick alone");
  keys.walkPercent = 30;
  check(Evaluate(ResolveKeyboard(keys), RawInputs{{79, 225}}).stickX == 38, "walk tilt 30%");
  check(Evaluate(walking, RawInputs{{20}}).triggerLeft == 180 && (Evaluate(walking, RawInputs{{20}}).buttons & 0x40),
        "Q: L click, analog 180 (as before)");
}

void test_names() {
  const PortMap map = DefaultPort(0);
  check(GcInputLabel(map, GcInput::A, Device::Xbox) == ButtonLabel{"A", "xbox_a"}, "A on Xbox: A");
  check(GcInputLabel(map, GcInput::A, Device::PlayStation) == ButtonLabel{"Cross", "ps_cross"}, "A on PlayStation: Cross");
  check(GcInputLabel(map, GcInput::A, Device::Switch) == ButtonLabel{"B", "switch_b"},
        "A on Switch (by position): the button marked B");
  check(GcInputLabel(map, GcInput::B, Device::Xbox) == ButtonLabel{"X", "xbox_x"}, "B on Xbox: X (GameCube place)");
  check(GcInputLabel(map, GcInput::Start, Device::Keyboard) == ButtonLabel{"Enter", "key_enter"}, "Start on keyboard: Enter");
  check(GcInputLabel(map, GcInput::L, Device::Xbox).text == "LT" && GcInputLabel(map, GcInput::L, Device::PlayStation).text == "L2",
        "L: LT / L2");
  check(GcInputLabel(map, GcInput::Z, Device::GameCube).text == "Z", "Z on a GameCube controller: Z");
  check(GcInputLabel(map, GcInput::StickUp, Device::Xbox) == ButtonLabel{"Left stick up", "xbox_left_stick_up"},
        "main stick up on Xbox");
  check(GcInputLabel(DefaultPort(1), GcInput::A, Device::Keyboard).text.empty(), "unbound: empty");
  PortMap label = map;
  label.gamepad.layout = Layout::Label;
  check(GcInputLabel(label, GcInput::A, Device::Switch) == ButtonLabel{"A", "switch_a"}, "A on Switch by label: A");

  bool allNamed = true;
  for (int d = 1; d < static_cast<int>(Device::Count); d++) {
    for (int b = 0; b < static_cast<int>(PadButton::Count); b++) {
      const ButtonLabel l = InputLabel(Input::Button(static_cast<PadButton>(b)), static_cast<Device>(d));
      allNamed &= !l.text.empty() && l.glyph.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_") == std::string::npos;
    }
  }
  check(allNamed, "every button of every controller has a name and a clean icon name");
}

void test_tokens() {
  bool ok = true;
  for (int b = 0; b < static_cast<int>(PadButton::Count); b++) {
    const Input in = Input::Button(static_cast<PadButton>(b));
    ok &= ParsePadToken(InputToken(in)) == in;
  }
  for (int sc = 0; sc < 256; sc++) {
    if (const auto k = ParseKeyToken(InputToken(Input::Key(static_cast<uint16_t>(sc)))); k) {
      ok &= k->code == sc;
    }
  }
  check(ok, "tokens read back as the same input");
  std::vector<Input> keys;
  std::optional<std::vector<Input>> pad;
  check(ParseKeyList(" x ,c", keys) && keys.size() == 2 && KeyListText(keys) == "x, c", "key list read and written");
  check(ParseKeyList("none", keys) && keys.empty(), "none: no keys");
  check(!ParseKeyList("x,,c", keys) && !ParseKeyList("", keys) && !ParseKeyList("south", keys), "bad key lists refused");
  check(ParsePadList("default", pad) && !pad, "default: the controller's default");
  check(ParsePadList("a, ltrigger", pad) && pad && pad->size() == 2, "a pad list");
  check(!ParsePadList("x, banana", pad) && !ParsePadList("enter", pad), "bad pad lists refused");
}

void test_ports_and_presets() {
  const std::string xbox = "030000005e040000e002000000007200", adapter = "0300000057e000003703000000000000";
  ControllerChoice c;
  check(ParseChoice("auto", c) && c.mode == ControllerChoice::Mode::Auto && ChoiceText(c) == "auto", "choice: auto");
  check(ParseChoice(" none ", c) && c.mode == ControllerChoice::Mode::None, "choice: none");
  check(ParseChoice(adapter + "#2|WUP-028", c) && c.mode == ControllerChoice::Mode::Pinned && c.guid == adapter &&
            c.nth == 2 && c.name == "WUP-028" && ChoiceText(c) == adapter + "#2|WUP-028",
        "choice: a pinned controller (model, which one, name) reads and writes back");
  check(ParseChoice("030000005E040000E002000000007200", c) && c.guid == xbox && c.nth == 0, "choice: upper-case GUID");
  check(!ParseChoice("xbox", c) && !ParseChoice(xbox + "#x", c) && !ParseChoice("", c) && !ParseChoice("12ab", c),
        "bad choices refused");
  const std::vector<ConnectedPad> pads = {{adapter, "GC 1"}, {xbox, "Xbox"}, {adapter, "GC 2"}};
  check(NthOfModel(pads, 0) == 0 && NthOfModel(pads, 1) == 0 && NthOfModel(pads, 2) == 1, "which one of a model");
  std::array<ControllerChoice, 4> choices = {DefaultChoice(0), DefaultChoice(1), DefaultChoice(2), DefaultChoice(3)};
  check(AssignControllers(choices, pads) == std::array<int, 4>{0, -1, -1, -1},
        "defaults: controller 1 gets the first one, 2-4 none (Aurora's defaults)");
  choices[0] = ControllerChoice::Pin(xbox, 0, "Xbox");
  choices[1] = ControllerChoice::Auto();
  choices[2] = ControllerChoice::Pin(adapter, 1, "");
  check(AssignControllers(choices, pads) == std::array<int, 4>{1, 0, 2, -1},
        "pinned ones first, then Auto takes the first left");
  choices[3] = ControllerChoice::Pin(adapter, 5, "");
  check(AssignControllers(choices, pads)[3] == -1, "a pinned controller that isn't plugged in: none");

  GamepadMap m;
  m.buttons[static_cast<size_t>(GcInput::A)] = std::vector<Input>{Input::Button(PadButton::North)};
  m.leftStickDeadzone = 0.3f;
  m.rumble = false;
  const GamepadMap gc = ApplyPadPreset(m, PadPreset::GameCubeAdapter);
  check(gc.kind == Device::GameCube && gc.layout == Layout::Position && !gc.buttons[0] && gc.leftStickDeadzone == 0.3f &&
            !gc.rumble,
        "GameCube adapter preset: kind GameCube, standard buttons, deadzone and rumble kept");
  check(!UsesDefaultButtons(gc), "a preset's kind is a change (it is put into Aurora)");
  bool gcB = false, gcL = false;
  for (const Binding& b : ResolveGamepad(gc, EffectiveDevice(gc, Device::Generic), true)) {
    gcB |= b.target == Target::B && b.input == Input::Button(PadButton::West);
    gcL |= b.target == Target::L && b.input == Input::Button(PadButton::Misc3);
  }
  check(gcB && gcL, "a controller that says 'generic' plays as a GameCube controller with that preset (B left, L click)");
  check(EffectiveDevice(DefaultGamepad(), Device::PlayStation) == Device::PlayStation, "auto: the detected kind");
  const GamepadMap sw = ApplyPadPreset(DefaultGamepad(), PadPreset::SwitchPro);
  check(sw.kind == Device::Switch && sw.layout == Layout::Label, "Switch Pro preset: by label");
  check(PadPresetOf(sw) == PadPreset::SwitchPro && PadPresetOf(ApplyPadPreset(m, PadPreset::Xbox)) == PadPreset::Xbox &&
            !PadPresetOf(DefaultGamepad()),
        "the preset in use is recognised");
  check(GcInputLabel(PortMap{DefaultKeyboard(0), ApplyPadPreset(DefaultGamepad(), PadPreset::PlayStation), {}},
                     GcInput::B, EffectiveDevice(ApplyPadPreset(DefaultGamepad(), PadPreset::PlayStation), Device::Xbox))
                .text == "Circle",
        "PlayStation preset: B is Circle, also on another controller");
}

std::string read_file(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

void test_settings(const fs::path& dir) {
  Registry r;
  setup(r);
  check(sWarnings.empty(), "controls register without warnings");
  bool defaultsRead = true;
  for (int port = 0; port < kPorts; port++) {
    defaultsRead &= ReadPort(r, port) == DefaultPort(port);
  }
  check(defaultsRead, "a fresh registry reads as the defaults");

  PortMap m = DefaultPort(1);
  m.keyboard.keys[static_cast<size_t>(GcInput::A)] = {*ParseKeyToken("space"), *ParseKeyToken("kp_0")};
  m.keyboard.keys[static_cast<size_t>(GcInput::StickUp)] = {*ParseKeyToken("w")};
  m.keyboard.walk = *ParseKeyToken("lctrl");
  m.keyboard.walkPercent = 40;
  m.gamepad.layout = Layout::Label;
  m.gamepad.buttons[static_cast<size_t>(GcInput::Z)] = std::vector<Input>{*ParsePadToken("lshoulder")};
  m.gamepad.buttons[static_cast<size_t>(GcInput::Y)] = std::vector<Input>{};
  m.gamepad.leftStickDeadzone = 0.1f + 0.05f;
  m.gamepad.rClickThreshold = 0.6f;
  m.gamepad.kind = Device::PlayStation;
  m.gamepad.rumble = false;
  m.controller = ControllerChoice::Pin("030000004c050000e60c000000006800", 1, "DualSense Wireless Controller");
  const size_t ff = static_cast<size_t>(Action::FastForward);
  check(DefaultActions(0).keys[ff] == std::vector<Input>{*ParseKeyToken("tab")} && DefaultActions(1).keys[ff].empty() &&
            DefaultActions(1).pad[ff] == std::vector<Input>{Input::Button(PadButton::RightStick)},
        "fast-forward: Tab on controller 1, the right stick's click on every controller");
  check(ActionFromId("fast_forward") == Action::FastForward && std::string(ActionName(Action::FastForward)) == "Fast-forward",
        "action names");
  m.actions.keys[ff] = {*ParseKeyToken("lshift")};
  m.actions.pad[ff] = {*ParsePadToken("back"), *ParsePadToken("y")};
  check(WritePort(r, 1, m), "remapping stored");
  check(r.CurrentPreset() == Preset::Original && r.IsOriginal(), "remapping keeps the preset Original");
  r.ApplyPreset(Preset::Enhanced);
  check(ReadPort(r, 1) == m, "presets leave the controls alone");
  r.ApplyPreset(Preset::Original);
  const fs::path folder = dir / "controls";
  check(r.Save(folder), "saved");
  const std::string text = read_file(folder / Registry::kFileName);
  check(text.find("\n[keyboard2]\n") != std::string::npos && text.find("\na = space, kp_0\n") != std::string::npos &&
            text.find("\nwalk = lctrl\n") != std::string::npos,
        "file has [keyboard2] with the keys as words");
  check(text.find("\n[gamepad2]\n") != std::string::npos && text.find("\nlayout = label\n") != std::string::npos &&
            text.find("\nz = lshoulder\n") != std::string::npos && text.find("\ny = none\n") != std::string::npos,
        "file has [gamepad2]");
  check(text.find("\nkind = playstation\n") != std::string::npos && text.find("\nrumble = off\n") != std::string::npos &&
            text.find("\ncontroller = 030000004c050000e60c000000006800#1|DualSense Wireless Controller\n") !=
                std::string::npos,
        "file has the controller type, rumble and which controller");
  check(text.find("\nfast_forward = lshift\n") != std::string::npos &&
            text.find("\nfast_forward = back, y\n") != std::string::npos && text.find("\nfast_forward = tab\n") != std::string::npos &&
            text.find("\nfast_forward = rstick_click\n") != std::string::npos,
        "file has the fast-forward keys and buttons");

  Registry b;
  setup(b);
  sWarnings.clear();
  b.Load(folder);
  check(sWarnings.empty(), "no warnings reading our own file");
  check(ReadPort(b, 1) == m, "remapping read back exactly (incl. the floats)");
  check(ReadPort(b, 0) == DefaultPort(0), "controller 1 still the defaults");
  check(b.CurrentPreset() == Preset::Original && b.IsOriginal(), "loaded: still Original");

  std::ofstream(folder / Registry::kFileName, std::ios::binary) << "[keyboard1]\n"
                                                                    "a = banana\n"
                                                                    "b = c\n"
                                                                    "walk = lshift, rshift\n"
                                                                    "jump = space\n"
                                                                    "fast_forward = none\n"
                                                                    "[gamepad1]\n"
                                                                    "fast_forward = default\n"
                                                                    "a = enter\n"
                                                                    "layout = sideways\n"
                                                                    "l_click = 2\n";
  Registry c;
  setup(c);
  sWarnings.clear();
  c.Load(folder);
  for (const std::string& w : sWarnings) {
    std::printf("     warning: %s\n", w.c_str());
  }
  check(sWarnings.size() == 5, "one warning per bad value");
  const PortMap p = ReadPort(c, 0);
  check(p.keyboard.keys[static_cast<size_t>(GcInput::A)] == DefaultKeyboard(0).keys[static_cast<size_t>(GcInput::A)],
        "bad key: the default (X)");
  check(p.keyboard.keys[static_cast<size_t>(GcInput::B)] == std::vector<Input>{*ParseKeyToken("c")}, "good key applied");
  check(p.keyboard.walk.kind == Input::Kind::None && p.gamepad == DefaultGamepad(), "the other bad values: defaults");
  const std::vector<std::string> unknown = UnknownEntries(c);
  for (const std::string& u : unknown) {
    std::printf("     warning: %s\n", u.c_str());
  }
  check(unknown.size() == 1 && unknown[0].find("\"jump\"") != std::string::npos, "misspelt name reported");
  check(p.actions.keys[static_cast<size_t>(Action::FastForward)].empty() &&
            p.actions.pad[static_cast<size_t>(Action::FastForward)] == DefaultActions(0).pad[static_cast<size_t>(Action::FastForward)],
        "fast-forward: none = no key, default = the standard button");
}

}

int main(int argc, char** argv) {
  const fs::path dir = argc > 1 ? fs::path(argv[1]) : fs::temp_directory_path() / "engine_controls_test";
  std::error_code ec;
  fs::remove_all(dir, ec);
  fs::create_directories(dir);

  test_keyboard_defaults();
  test_gamepad_defaults();
  test_position_and_label();
  test_reading();
  test_names();
  test_tokens();
  test_ports_and_presets();
  test_settings(dir);

  fs::remove_all(dir, ec);
  std::printf("%s\n", sOk ? "all passed" : "FAILED");
  return sOk ? 0 : 1;
}
