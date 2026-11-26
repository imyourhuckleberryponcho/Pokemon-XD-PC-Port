#include <SDL3/SDL.h>

#include <pad_sdl.h>
#include <pad_setup.h>
#include <pad_windows.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace ps = engine::controls::padsetup;
namespace pd = engine::controls::padsdl;
using ps::Role;
using ps::Source;

static int g_bad = 0;
static void Check(bool ok, const std::string& what) {
  std::printf("  %s %s\n", ok ? "ok  " : "FAIL", what.c_str());
  if (!ok) {
    g_bad++;
  }
}

static void PureTests() {
  std::printf("pure:\n");
  Source b;
  b.kind = Source::Kind::Button;
  b.index = 3;
  Source hat;
  hat.kind = Source::Kind::Hat;
  hat.index = 0;
  hat.hatMask = 4;
  Source axPos;
  axPos.kind = Source::Kind::Axis;
  axPos.index = 4;
  axPos.direction = 1;
  axPos.rest = -1.0f;
  Source axHalf = axPos;
  axHalf.rest = 0.0f;
  Source axNeg = axPos;
  axNeg.direction = -1;
  axNeg.rest = 0.0f;
  Source axInv = axPos;
  axInv.direction = -1;
  axInv.rest = 1.0f;
  Source stickL;
  stickL.kind = Source::Kind::Axis;
  stickL.index = 0;
  stickL.direction = -1;
  Source stickLInv = stickL;
  stickLInv.direction = 1;

  ps::Answers a;
  a.source[static_cast<int>(Role::A)] = b;
  a.source[static_cast<int>(Role::DpadDown)] = hat;
  a.source[static_cast<int>(Role::L)] = axPos;
  a.source[static_cast<int>(Role::R)] = axHalf;
  a.source[static_cast<int>(Role::StickLeft)] = stickL;
  a.source[static_cast<int>(Role::CStickLeft)] = stickLInv;
  const std::string line = ps::BuildMapping("030000001234000056780000ffff0000", "My, Pad", "Windows", a);
  std::printf("  %s\n", line.c_str());
  Check(line.find("030000001234000056780000ffff0000,My  Pad,") == 0, "guid, name (comma removed)");
  Check(line.find(",a:b3") != std::string::npos, "button");
  Check(line.find(",dpdown:h0.4") != std::string::npos, "hat");
  Check(line.find(",lefttrigger:a4,") != std::string::npos, "trigger resting at -1: the whole axis");
  Check(line.find(",righttrigger:+a4,") != std::string::npos, "trigger resting at 0: the positive half");
  Check(line.find(",leftx:a0,") != std::string::npos, "stick pushed left = negative");
  Check(line.find(",rightx:a0~,") != std::string::npos, "stick that goes positive when pushed left: inverted");
  Check(line.size() > 6 && line.find(",platform:Windows") == line.size() - 17, "platform last");
  ps::Answers b2;
  b2.source[static_cast<int>(Role::L)] = axNeg;
  b2.source[static_cast<int>(Role::R)] = axInv;
  b2.source[static_cast<int>(Role::Z)] = b;
  const std::string l2 = ps::BuildMapping("g", "n", "", b2);
  Check(l2.find("lefttrigger:-a4") != std::string::npos, "trigger resting at 0 pulled negative: -a");
  Check(l2.find("righttrigger:a4~") != std::string::npos, "trigger resting at +1 pulled to -1: inverted");
  Check(l2.find("rightshoulder:b3") != std::string::npos, "Z is SDL's right shoulder");
  Check(ps::BuildMapping("g", "n", "x", ps::Answers{}).empty(), "nothing answered = no mapping");
  Check(!ps::CheckSource(Role::StickLeft, b).empty(), "a stick needs an axis");
  Check(ps::CheckSource(Role::StickLeft, stickL).empty(), "an axis is a stick");
  Check(ps::TakenBy(a, Role::B, b) == "a", "a control is not given to two roles");
  Check(ps::TakenBy(a, Role::A, b).empty(), "a role may be answered again");
  ps::Answers dp;
  Source up = axNeg;
  up.index = 1;
  Source down = up;
  down.direction = 1;
  dp.source[static_cast<int>(Role::DpadUp)] = up;
  dp.source[static_cast<int>(Role::DpadDown)] = down;
  Check(ps::TakenBy(dp, Role::DpadDown, down).empty(), "one axis may carry up and down (other directions)");
  const std::string l3 = ps::BuildMapping("g", "n", "", dp);
  Check(l3.find("dpup:-a1") != std::string::npos && l3.find("dpdown:+a1") != std::string::npos, "D-pad on an axis");

  ps::Snapshot rest;
  rest.axes = {0.0f, 0.0f, -1.0f};
  rest.buttons = {false, false, false};
  rest.hats = {0};
  ps::Detector d;
  d.Begin(rest);
  Check(!d.Feed(rest).has_value(), "detector: nothing pressed");
  ps::Snapshot now = rest;
  now.axes[0] = 0.3f;
  Check(!d.Feed(now).has_value(), "detector: a small move is no press");
  now.axes[2] = 1.0f;
  auto s = d.Feed(now);
  Check(s && s->kind == Source::Kind::Axis && s->index == 2 && s->direction == 1 && s->rest < -0.9f, "detector: trigger at -1 pulled");
  Check(!d.Released(now), "detector: not released while held");
  Check(d.Released(rest), "detector: released");
  now = rest;
  now.hats[0] = 3;
  Check(!d.Feed(now).has_value(), "detector: a diagonal hat is waited out");
  now.hats[0] = 1;
  s = d.Feed(now);
  Check(s && s->kind == Source::Kind::Hat && s->hatMask == 1, "detector: hat up");
  now = rest;
  now.buttons[1] = true;
  s = d.Feed(now);
  Check(s && s->kind == Source::Kind::Button && s->index == 1, "detector: button");
  ps::Snapshot held = rest;
  held.buttons[0] = true;
  ps::Detector d2;
  d2.Begin(held);
  Check(!d2.Feed(held).has_value(), "detector: a button already held at the start is no press");

  {
    ps::Session se;
    se.Start("g", "Pad", "Windows");
    ps::Snapshot r0;
    r0.axes = {0, 0, 0, 0};
    r0.buttons = {false, false, false, false, false};
    r0.hats = {0};
    ps::Snapshot x = r0;
    se.Feed(r0);
    Check(se.Current() == Role::A && se.Step() == 0, "session: starts at A");
    x.buttons[2] = true;
    se.Feed(x);
    Check(se.LastEvent() == ps::Session::Event::Answered && se.Current() == Role::B, "session: A answered, now B");
    se.Feed(x);
    Check(se.Current() == Role::B, "session: a held button answers nothing more");
    se.Feed(r0);
    se.Feed(x);
    Check(se.LastEvent() == ps::Session::Event::Taken && se.TakenByRole() == "a" && se.Current() == Role::B, "session: the same button for B is refused");
    se.Feed(r0);
    se.Skip();
    Check(se.Current() == Role::X && !se.GetAnswers().Has(Role::B), "session: skip B");
    se.Back();
    Check(se.Current() == Role::B, "session: back to B");
    se.Back();
    Check(se.Current() == Role::A && !se.GetAnswers().Has(Role::A), "session: back to A drops its answer");
    se.Back();
    Check(se.Current() == Role::A, "session: back at the start stays");
    se.Restart();
    for (int i = 0; i < ps::kRoleCount; i++) {
      se.Skip();
    }
    Check(se.Finished() && se.Mapping().empty(), "session: all skipped = nothing to save");
    se.Restart();
    se.Feed(r0);
    for (int i = 0; i < static_cast<int>(Role::StickLeft); i++) {
      se.Skip();
    }
    se.Feed(r0);
    ps::Snapshot b = r0;
    b.buttons[1] = true;
    se.Feed(b);
    Check(se.LastEvent() == ps::Session::Event::WrongKind && se.Current() == Role::StickLeft, "session: a button is no stick direction");
  }

  const std::filesystem::path dir = std::filesystem::temp_directory_path() / "xd_pad_setup_test";
  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
  std::filesystem::create_directories(dir, ec);
  Check(ps::LoadMappings(dir).empty(), "file: none yet");
  std::string err;
  Check(ps::StoreMapping(dir, "AAAA,pad one,a:b0,platform:Windows", &err), "file: store one");
  Check(ps::StoreMapping(dir, "BBBB,pad two,a:b1,platform:Windows", &err), "file: store two");
  Check(ps::StoreMapping(dir, "aaaa,pad one again,a:b5,platform:Windows", &err), "file: store the first again");
  auto lines = ps::LoadMappings(dir);
  Check(lines.size() == 2, "file: the same GUID replaces its line (2 lines)");
  Check(lines.size() == 2 && lines[1].find("b5") != std::string::npos, "file: the new line won");
  Check(!ps::ForgetMapping(dir, "CCCC"), "file: forgetting an unknown one does nothing");
  Check(ps::ForgetMapping(dir, "bbbb"), "file: forget");
  Check(ps::LoadMappings(dir).size() == 1, "file: one left");
  Check(!ps::HasDatabase(dir), "file: no database");
  std::filesystem::remove_all(dir, ec);

  unsigned short v = 0, p = 0;
  Check(engine::controls::ParseHardwareId("HID\\VID_0F0D&PID_00C1&REV_0100", &v, &p) && v == 0x0F0D && p == 0x00C1, "windows: hardware id");
  Check(!engine::controls::ParseHardwareId("ACPI\\PNP0303", &v, &p), "windows: no VID");
  Check(engine::controls::LooksLikeGameController({"HID\\VID_0F0D&PID_00C1"}, {"HID_DEVICE_SYSTEM_GAME", "HID_DEVICE"}), "windows: game controller");
  Check(!engine::controls::LooksLikeGameController({"HID\\VID_046D&PID_C52B"}, {"HID_DEVICE_SYSTEM_KEYBOARD", "HID_DEVICE"}), "windows: a keyboard is none");
}

struct Layout {
  const char* name;
  Uint16 vendor, product, naxes, nbuttons, nhats;
  std::vector<std::pair<Role, ps::Source>> physical;
  std::vector<std::pair<int, Sint16>> axisRest;
};

static Source Btn(int i) {
  Source s;
  s.kind = Source::Kind::Button;
  s.index = i;
  return s;
}
static Source Hat(int i, int mask) {
  Source s;
  s.kind = Source::Kind::Hat;
  s.index = i;
  s.hatMask = mask;
  return s;
}
static Source Ax(int i, int dir) {
  Source s;
  s.kind = Source::Kind::Axis;
  s.index = i;
  s.direction = dir;
  return s;
}

static void Press(SDL_Joystick* j, const Source& s, const Layout& l, bool down) {
  switch (s.kind) {
  case Source::Kind::Button: SDL_SetJoystickVirtualButton(j, s.index, down); break;
  case Source::Kind::Hat: SDL_SetJoystickVirtualHat(j, s.index, down ? static_cast<Uint8>(s.hatMask) : 0); break;
  case Source::Kind::Axis: {
    Sint16 rest = 0;
    for (const auto& [a, v] : l.axisRest) {
      if (a == s.index) {
        rest = v;
      }
    }
    Sint16 pressed = s.direction > 0 ? 32767 : -32768;
    if (rest == 32767) {
      pressed = -32768;
    }
    SDL_SetJoystickVirtualAxis(j, s.index, down ? pressed : rest);
    break;
  }
  default: break;
  }
  SDL_UpdateJoysticks();
}

static void Settle() {
  SDL_PumpEvents();
  SDL_Event e;
  while (SDL_PollEvent(&e)) {
  }
}

static SDL_JoystickID Attach(const Layout& l) {
  SDL_VirtualJoystickDesc desc;
  SDL_INIT_INTERFACE(&desc);
  desc.type = SDL_JOYSTICK_TYPE_UNKNOWN;
  desc.vendor_id = l.vendor;
  desc.product_id = l.product;
  desc.naxes = l.naxes;
  desc.nbuttons = l.nbuttons;
  desc.nhats = l.nhats;
  desc.name = l.name;
  return SDL_AttachVirtualJoystick(&desc);
}

static SDL_Joystick* OpenAtRest(SDL_JoystickID id, const Layout& l) {
  SDL_Joystick* j = SDL_OpenJoystick(id);
  for (const auto& [a, v] : l.axisRest) {
    SDL_SetJoystickVirtualAxis(j, a, v);
  }
  SDL_UpdateJoysticks();
  return j;
}

static ps::Answers RunSetup(SDL_Joystick* j, const Layout& l) {
  ps::Session session;
  session.Start("g", l.name, "Test");
  session.Feed(pd::ReadSnapshot(j));
  for (int r = 0; r < ps::kRoleCount; r++) {
    const Role role = static_cast<Role>(r);
    const Source* phys = nullptr;
    for (const auto& [rr, s] : l.physical) {
      if (rr == role) {
        phys = &s;
      }
    }
    if (session.Current() != role) {
      std::printf("  (step %d is not %s)\n", session.Step(), ps::RoleId(role));
      g_bad++;
    }
    if (phys == nullptr) {
      session.Skip();
      session.Feed(pd::ReadSnapshot(j));
      continue;
    }
    Press(j, *phys, l, true);
    session.Feed(pd::ReadSnapshot(j));
    if (session.LastEvent() != ps::Session::Event::Answered) {
      std::printf("  (no answer for %s, event %d)\n", ps::RoleId(role), static_cast<int>(session.LastEvent()));
    }
    Press(j, *phys, l, false);
    session.Feed(pd::ReadSnapshot(j));
  }
  Check(session.Finished(), "the session went through every step");
  return session.GetAnswers();
}

static void VerifyGamepad(SDL_Joystick* j, SDL_Gamepad* g, const Layout& l, const std::string& label) {
  struct Expect {
    Role role;
    bool axis;
    int sdl;
    int sign;
  };
  const Expect expects[] = {
      {Role::A, false, SDL_GAMEPAD_BUTTON_SOUTH, 0},        {Role::B, false, SDL_GAMEPAD_BUTTON_EAST, 0},
      {Role::X, false, SDL_GAMEPAD_BUTTON_WEST, 0},         {Role::Y, false, SDL_GAMEPAD_BUTTON_NORTH, 0},
      {Role::Start, false, SDL_GAMEPAD_BUTTON_START, 0},    {Role::Z, false, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, 0},
      {Role::L, true, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, 1},    {Role::R, true, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 1},
      {Role::DpadUp, false, SDL_GAMEPAD_BUTTON_DPAD_UP, 0}, {Role::DpadDown, false, SDL_GAMEPAD_BUTTON_DPAD_DOWN, 0},
      {Role::DpadLeft, false, SDL_GAMEPAD_BUTTON_DPAD_LEFT, 0}, {Role::DpadRight, false, SDL_GAMEPAD_BUTTON_DPAD_RIGHT, 0},
      {Role::StickLeft, true, SDL_GAMEPAD_AXIS_LEFTX, -1},  {Role::StickUp, true, SDL_GAMEPAD_AXIS_LEFTY, -1},
      {Role::CStickLeft, true, SDL_GAMEPAD_AXIS_RIGHTX, -1}, {Role::CStickUp, true, SDL_GAMEPAD_AXIS_RIGHTY, -1},
  };
  for (const Expect& e : expects) {
    const Source* phys = nullptr;
    for (const auto& [rr, s] : l.physical) {
      if (rr == e.role) {
        phys = &s;
      }
    }
    if (phys == nullptr) {
      continue;
    }
    SDL_UpdateJoysticks();
    const bool restOk = e.axis ? std::abs(SDL_GetGamepadAxis(g, static_cast<SDL_GamepadAxis>(e.sdl))) < 3000
                               : !SDL_GetGamepadButton(g, static_cast<SDL_GamepadButton>(e.sdl));
    Press(j, *phys, l, true);
    bool on;
    if (e.axis) {
      const int v = SDL_GetGamepadAxis(g, static_cast<SDL_GamepadAxis>(e.sdl));
      on = e.sign > 0 ? v > 16000 : v < -16000;
    } else {
      on = SDL_GetGamepadButton(g, static_cast<SDL_GamepadButton>(e.sdl));
    }
    bool otherWay = true;
    if (e.axis && e.sign < 0) {
      Press(j, *phys, l, false);
      Source opposite = *phys;
      opposite.direction = -opposite.direction;
      Press(j, opposite, l, true);
      otherWay = SDL_GetGamepadAxis(g, static_cast<SDL_GamepadAxis>(e.sdl)) > 16000;
      Press(j, opposite, l, false);
    }
    Press(j, *phys, l, false);
    Check(restOk && on && otherWay, label + ": " + ps::RoleId(e.role) + " works as SDL's gamepad" + (restOk ? "" : " (rest wrong)") +
                                       (otherWay ? "" : " (other way wrong)"));
  }
}

static std::string FindGuid(SDL_JoystickID id) {
  char guid[33]{};
  SDL_GUIDToString(SDL_GetJoystickGUIDForID(id), guid, sizeof(guid));
  return guid;
}

static void OnePad(const Layout& l, int count) {
  std::printf("%s (%d connected):\n", l.name, count);
  std::vector<SDL_JoystickID> ids;
  for (int i = 0; i < count; i++) {
    ids.push_back(Attach(l));
  }
  Settle();
  Check(ids[0] != 0, "virtual joystick attached");
  Check(!SDL_IsGamepad(ids[0]), "starts as NOT a gamepad (unrecognised)");
  const std::vector<pd::JoyInfo> all = pd::ListJoysticks(true);
  size_t unrecognised = pd::Unrecognised(all).size();
  Check(unrecognised >= static_cast<size_t>(count), "the unrecognised ones are listed (by name)");
  bool named = false;
  for (const pd::JoyInfo& j : pd::Unrecognised(all)) {
    named |= j.name == l.name && j.axes == l.naxes && j.buttons == l.nbuttons && j.hats == l.nhats;
  }
  Check(named, "name, buttons, axes, hats are right in the list");
  if (count == 2) {
    int second = 0;
    for (const pd::JoyInfo& j : all) {
      second += j.name == l.name && j.nth == 1 ? 1 : 0;
    }
    Check(second == 1, "two identical pads: the second is told apart (nth)");
  }
  const std::string block = pd::DescribeJoysticks("test", false);
  Check(block.find(std::string("\"") + l.name + "\"") != std::string::npos && block.find("NOT recognised") != std::string::npos, "the log block names it and says not recognised");

  SDL_Joystick* j = OpenAtRest(ids[0], l);
  const ps::Answers answers = RunSetup(j, l);
  int answered = 0;
  for (const Source& s : answers.source) {
    answered += s.Valid() ? 1 : 0;
  }
  Check(answered == static_cast<int>(l.physical.size()), "every control the pad has was found (" + std::to_string(answered) + ")");
  const std::string guid = FindGuid(ids[0]);
  const std::string line = ps::BuildMapping(guid, l.name, SDL_GetPlatform(), answers);
  std::printf("  mapping: %s\n", line.c_str());
  const std::filesystem::path dir = std::filesystem::temp_directory_path() / "xd_pad_setup_test2";
  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
  std::string err;
  Check(pd::ApplyAndStore(dir, line, &err), "SDL took the mapping and it was saved " + err);
  Settle();
  Check(SDL_IsGamepad(ids[0]), "now a gamepad");
  if (count == 2) {
    Check(SDL_IsGamepad(ids[1]), "the second identical pad is a gamepad too (same GUID)");
  }
  SDL_Gamepad* g = SDL_OpenGamepad(ids[0]);
  Check(g != nullptr, "opened as a gamepad");
  if (g != nullptr) {
    VerifyGamepad(j, g, l, l.name);
    SDL_CloseGamepad(g);
  }
  SDL_CloseJoystick(j);
  for (SDL_JoystickID id : ids) {
    SDL_DetachVirtualJoystick(id);
  }
  Settle();
  {
    bool gone = true;
    for (const pd::JoyInfo& x : pd::ListJoysticks(false)) {
      gone &= x.name != l.name;
    }
    Check(gone, "unplugged: gone from the list");
  }
  const SDL_JoystickID again = Attach(l);
  Settle();
  Check(SDL_IsGamepad(again), "plugged in again: recognised at once");
  Check(ps::LoadMappings(dir).size() == 1, "the file holds the set-up");
  std::string ferr;
  Check(pd::ForgetAndRemove(dir, guid, &ferr), "forget this set-up: line removed from the file");
  Settle();
  Check(ps::LoadMappings(dir).empty(), "the file is empty again");
  {
    bool listedUnrecognised = false;
    for (const pd::JoyInfo& x : pd::ListJoysticks(false)) {
      listedUnrecognised |= x.name == l.name && !x.gamepad;
    }
    Check(listedUnrecognised, "after forgetting: listed as not recognised again (SDL keeps its mapping until the next start)");
  }
  SDL_DetachVirtualJoystick(again);
  Settle();
  std::filesystem::remove_all(dir, ec);
}

int main() {
  PureTests();
  SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
  if (!SDL_Init(SDL_INIT_JOYSTICK | SDL_INIT_GAMEPAD | SDL_INIT_EVENTS)) {
    std::printf("pad_setup_test: SDL part SKIPPED, SDL_Init failed: %s\n", SDL_GetError());
    std::printf(g_bad == 0 ? "pad_setup_test: OK (pure part only)\n" : "pad_setup_test: FAILED\n");
    return g_bad == 0 ? 0 : 1;
  }
  {
    Layout l{"Odd Pad A (hat, triggers -1)", 0x1234, 0x0001, 6, 12, 1, {}, {}};
    l.physical = {{Role::A, Btn(1)},  {Role::B, Btn(0)},  {Role::X, Btn(3)},  {Role::Y, Btn(2)},
                  {Role::Start, Btn(9)}, {Role::Z, Btn(5)}, {Role::L, Ax(4, 1)}, {Role::R, Ax(5, 1)},
                  {Role::DpadUp, Hat(0, 1)}, {Role::DpadDown, Hat(0, 4)}, {Role::DpadLeft, Hat(0, 8)}, {Role::DpadRight, Hat(0, 2)},
                  {Role::StickLeft, Ax(0, -1)}, {Role::StickUp, Ax(1, -1)}, {Role::CStickLeft, Ax(2, -1)}, {Role::CStickUp, Ax(3, -1)}};
    l.axisRest = {{4, -32768}, {5, -32768}};
    OnePad(l, 1);
  }
  {
    Layout l{"Odd Pad B (triggers 0, D-pad axes, inverted C)", 0x1234, 0x0002, 8, 10, 0, {}, {}};
    l.physical = {{Role::A, Btn(0)}, {Role::B, Btn(1)}, {Role::X, Btn(2)}, {Role::Y, Btn(3)}, {Role::Start, Btn(7)},
                  {Role::Z, Btn(5)}, {Role::L, Ax(4, 1)}, {Role::R, Ax(5, 1)},
                  {Role::DpadUp, Ax(7, -1)}, {Role::DpadDown, Ax(7, 1)}, {Role::DpadLeft, Ax(6, -1)}, {Role::DpadRight, Ax(6, 1)},
                  {Role::StickLeft, Ax(0, -1)}, {Role::StickUp, Ax(1, 1)}, {Role::CStickLeft, Ax(2, 1)}, {Role::CStickUp, Ax(3, -1)}};
    l.axisRest = {{4, 0}, {5, 0}};
    OnePad(l, 1);
  }
  {
    Layout l{"Odd Pad C (triggers are buttons, rest +1 trigger)", 0x1234, 0x0003, 6, 14, 1, {}, {}};
    l.physical = {{Role::A, Btn(2)}, {Role::B, Btn(1)}, {Role::X, Btn(3)}, {Role::Y, Btn(0)}, {Role::Start, Btn(11)},
                  {Role::Z, Btn(5)}, {Role::L, Btn(6)}, {Role::R, Btn(7)},
                  {Role::DpadUp, Hat(0, 1)}, {Role::DpadDown, Hat(0, 4)}, {Role::DpadLeft, Hat(0, 8)}, {Role::DpadRight, Hat(0, 2)},
                  {Role::StickLeft, Ax(0, -1)}, {Role::StickUp, Ax(1, -1)}, {Role::CStickLeft, Ax(2, -1)}, {Role::CStickUp, Ax(3, -1)}};
    OnePad(l, 1);
  }
  {
    Layout l{"Twin Pad (two the same)", 0x1234, 0x0004, 4, 12, 1, {}, {}};
    l.physical = {{Role::A, Btn(0)}, {Role::B, Btn(1)}, {Role::X, Btn(2)}, {Role::Y, Btn(3)}, {Role::Start, Btn(9)},
                  {Role::Z, Btn(5)}, {Role::L, Btn(4)}, {Role::R, Btn(6)},
                  {Role::DpadUp, Hat(0, 1)}, {Role::DpadDown, Hat(0, 4)}, {Role::DpadLeft, Hat(0, 8)}, {Role::DpadRight, Hat(0, 2)},
                  {Role::StickLeft, Ax(0, -1)}, {Role::StickUp, Ax(1, -1)}, {Role::CStickLeft, Ax(2, -1)}, {Role::CStickUp, Ax(3, -1)}};
    OnePad(l, 2);
  }
  {
    std::printf("hints and the database file:\n");
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "xd_pad_setup_test3";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
    Layout l{"Database Pad", 0x1234, 0x0005, 2, 4, 0, {}, {}};
    const SDL_JoystickID id = Attach(l);
    const std::string guid = FindGuid(id);
    {
      FILE* f = std::fopen((dir / "gamecontrollerdb.txt").string().c_str(), "wb");
      std::fprintf(f, "# a comment\n%s,Database Pad,a:b0,b:b1,x:b2,y:b3,leftx:a0,lefty:a1,platform:%s,\n", guid.c_str(), SDL_GetPlatform());
      std::fclose(f);
    }
    Settle();
    Check(!SDL_IsGamepad(id), "database: not a gamepad before");
    const pd::LoadResult r = pd::AddMappings(dir);
    std::printf("  %s\n", r.text.c_str());
    Settle();
    Check(r.database == 1, "database: one mapping read from gamecontrollerdb.txt");
    Check(SDL_IsGamepad(id), "database: the pad is a gamepad now");
    SDL_DetachVirtualJoystick(id);
    std::filesystem::remove_all(dir, ec);
  }
  {
    std::printf("hints before SDL starts (the game):\n");
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "xd_pad_setup_test4";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
    Layout own{"Hint Pad", 0x1234, 0x0006, 2, 4, 0, {}, {}};
    Layout db{"Hint DB Pad", 0x1234, 0x0007, 2, 4, 0, {}, {}};
    const SDL_JoystickID a = Attach(own);
    const SDL_JoystickID b = Attach(db);
    ps::Answers ans;
    ans.source[static_cast<int>(Role::A)] = Btn(0);
    ans.source[static_cast<int>(Role::B)] = Btn(1);
    std::string err;
    Check(ps::StoreMapping(dir, ps::BuildMapping(FindGuid(a), own.name, SDL_GetPlatform(), ans), &err), "hints: pad_mappings.txt written");
    {
      FILE* f = std::fopen((dir / "gamecontrollerdb.txt").string().c_str(), "wb");
      std::fprintf(f, "%s,Hint DB Pad,a:b0,b:b1,platform:%s,\n", FindGuid(b).c_str(), SDL_GetPlatform());
      std::fclose(f);
    }
    SDL_DetachVirtualJoystick(a);
    SDL_DetachVirtualJoystick(b);
    SDL_Quit();
    pd::SetMappingHints(dir);
    if (SDL_Init(SDL_INIT_JOYSTICK | SDL_INIT_GAMEPAD | SDL_INIT_EVENTS)) {
      const SDL_JoystickID a2 = Attach(own);
      const SDL_JoystickID b2 = Attach(db);
      Settle();
      Check(SDL_IsGamepad(a2), "hints: the player's set-up is a gamepad from the start (no AddMappings call)");
      Check(SDL_IsGamepad(b2), "hints: gamecontrollerdb.txt too");
      SDL_DetachVirtualJoystick(a2);
      SDL_DetachVirtualJoystick(b2);
    } else {
      Check(false, std::string("hints: SDL_Init again failed: ") + SDL_GetError());
    }
    std::filesystem::remove_all(dir, ec);
  }
  SDL_Quit();
  if (g_bad == 0) {
    std::printf("pad_setup_test: OK\n");
    return 0;
  }
  std::printf("pad_setup_test: %d FAILED\n", g_bad);
  return 1;
}
