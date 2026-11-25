#include "settings_ui.h"

#include "i18n.h"

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <optional>
#include <utility>
#include <cfloat>
#include <cstdarg>
#include <string>
#include <unordered_map>

namespace engine::settings_ui {

namespace es = engine::settings;
namespace ec = engine::controls;

namespace {

using engine::i18n::Tr;

std::string Lbl(const char* translated, const char* english) { return std::string(translated) + "##" + english; }
std::string Lbl(const std::string& english) { return std::string(Tr(english)) + "##" + english; }

std::string Fmt(const char* format, ...) {
  char buf[1024];
  va_list args;
  va_start(args, format);
  std::vsnprintf(buf, sizeof(buf), format, args);
  va_end(args);
  return buf;
}

ImVec4 Col(const float c[4]) { return ImVec4(c[0], c[1], c[2], c[3]); }

bool StartsWith(const std::string& s, const std::string& prefix) { return s.compare(0, prefix.size(), prefix) == 0; }

es::Preset BaseOf(const es::Registry& r) {
  return r.CurrentPreset() == es::Preset::Custom ? r.BasePreset() : r.CurrentPreset();
}

bool SameAs(const es::Registry& r, size_t i, const es::Value& a, const es::Value& b) {
  return r.TextOf(i, a) == r.TextOf(i, b);
}

void Relabel(es::Registry& r) {
  if (r.CurrentPreset() != es::Preset::Custom) {
    return;
  }
  const es::Preset base = r.BasePreset();
  for (size_t i = 0; i < r.Count(); i++) {
    const es::Definition& d = r.Get(i);
    if (!d.preference && !d.testing && !SameAs(r, i, r.ValueOf(i), r.DefaultOf(i, base))) {
      return;
    }
  }
  r.ApplyPreset(base);
}

void Heading(const Options& o, const char* text) {
  if (o.headingFont != nullptr) {
    ImGui::PushFont(static_cast<ImFont*>(o.headingFont));
  }
  ImGui::TextUnformatted(text);
  if (o.headingFont != nullptr) {
    ImGui::PopFont();
  }
}

void Wrapped(const float color[4], const std::string& text) {
  ImGui::PushStyleColor(ImGuiCol_Text, Col(color));
  ImGui::TextWrapped("%s", text.c_str());
  ImGui::PopStyleColor();
}

std::string KeyWord(const std::string& n) {
  if (n == "Enter") return Tr("Enter");
  if (n == "Esc") return Tr("Esc");
  if (n == "Backspace") return Tr("Backspace");
  if (n == "Tab") return Tr("Tab");
  if (n == "Space") return Tr("Space");
  if (n == "Caps Lock") return Tr("Caps Lock");
  if (n == "Insert") return Tr("Insert");
  if (n == "Home") return Tr("Home");
  if (n == "Page Up") return Tr("Page Up");
  if (n == "Delete") return Tr("Delete");
  if (n == "End") return Tr("End");
  if (n == "Page Down") return Tr("Page Down");
  if (n == "Right Arrow") return Tr("Right Arrow");
  if (n == "Left Arrow") return Tr("Left Arrow");
  if (n == "Down Arrow") return Tr("Down Arrow");
  if (n == "Up Arrow") return Tr("Up Arrow");
  if (n == "Num Enter") return Tr("Num Enter");
  if (n == "Left Ctrl") return Tr("Left Ctrl");
  if (n == "Left Shift") return Tr("Left Shift");
  if (n == "Left Alt") return Tr("Left Alt");
  if (n == "Right Ctrl") return Tr("Right Ctrl");
  if (n == "Right Shift") return Tr("Right Shift");
  if (n == "Right Alt") return Tr("Right Alt");
  return Tr(n);
}

std::string InputName(const ec::Input& in, ec::Device device) { return KeyWord(ec::InputLabel(in, device).text); }

std::string GcName(ec::GcInput g) {
  switch (g) {
  case ec::GcInput::A: return Tr("A");
  case ec::GcInput::B: return Tr("B");
  case ec::GcInput::X: return Tr("X");
  case ec::GcInput::Y: return Tr("Y");
  case ec::GcInput::Z: return Tr("Z");
  case ec::GcInput::L: return Tr("L");
  case ec::GcInput::R: return Tr("R");
  case ec::GcInput::Start: return Tr("Start");
  case ec::GcInput::DpadUp: return Tr("D-pad up");
  case ec::GcInput::DpadDown: return Tr("D-pad down");
  case ec::GcInput::DpadLeft: return Tr("D-pad left");
  case ec::GcInput::DpadRight: return Tr("D-pad right");
  case ec::GcInput::StickUp: return Tr("Main stick up");
  case ec::GcInput::StickDown: return Tr("Main stick down");
  case ec::GcInput::StickLeft: return Tr("Main stick left");
  case ec::GcInput::StickRight: return Tr("Main stick right");
  case ec::GcInput::CStickUp: return Tr("C-stick up");
  case ec::GcInput::CStickDown: return Tr("C-stick down");
  case ec::GcInput::CStickLeft: return Tr("C-stick left");
  case ec::GcInput::CStickRight: return Tr("C-stick right");
  default: return ec::GcInputName(g);
  }
}

std::string Join(const std::vector<ec::Input>& inputs, ec::Device device) {
  std::string out;
  for (const ec::Input& in : inputs) {
    out += (out.empty() ? "" : ", ") + InputName(in, device);
  }
  return out.empty() ? std::string(Tr("none")) : out;
}

}

std::vector<size_t> VisibleSettings(const es::Registry& registry, es::Category category, const Options& options) {
  std::vector<size_t> out;
  for (size_t i = 0; i < registry.Count(); i++) {
    const es::Definition& d = registry.Get(i);
    if (d.category != category || d.testing || (!options.gameId.empty() && !d.AppliesTo(options.gameId))) {
      continue;
    }
    if (std::find(options.hidden.begin(), options.hidden.end(), d.id) != options.hidden.end()) {
      continue;
    }
    if (std::any_of(options.hiddenPrefixes.begin(), options.hiddenPrefixes.end(),
                    [&](const std::string& p) { return StartsWith(d.id, p); })) {
      continue;
    }
    out.push_back(i);
  }
  return out;
}

const char* PresetName(es::Preset preset) {
  switch (preset) {
  case es::Preset::Original: return Tr("Original");
  case es::Preset::Enhanced: return Tr("Enhanced");
  case es::Preset::Custom: return Tr("Custom");
  }
  return "";
}

const char* PresetExplanation(es::Preset preset) {
  switch (preset) {
  case es::Preset::Original:
    return Tr("Exactly the GameCube game: same speed, same loading times, same picture, same bugs. Nothing extra "
              "appears in the game.");
  case es::Preset::Enhanced:
    return Tr("The recommended improvements: a sharper, wider picture, smoother movement and shorter waits. The "
              "game's own rules (battles, bugs, purification) stay as they were. Some of these don't work yet.");
  case es::Preset::Custom:
    return Tr("Your own choice: starts from Original or Enhanced, and you change single settings on the "
              "Settings page.");
  }
  return "";
}

std::string ValueForPeople(const es::Registry& registry, size_t index, const es::Value& value) {
  const es::Definition& d = registry.Get(index);
  switch (d.type) {
  case es::Type::Bool: return value.i != 0 ? Tr("On") : Tr("Off");
  case es::Type::Enum:
    return value.i >= 0 && value.i < static_cast<int32_t>(d.choices.size()) ? Tr(d.choices[value.i].name)
                                                                            : std::string("?");
  case es::Type::Text: return value.s.empty() ? std::string(Tr("(empty)")) : value.s;
  case es::Type::Int:
    if (const std::string name = d.IntName(value.i); !name.empty()) {
      return Tr(name);
    }
    return registry.TextOf(index, value);
  default: return registry.TextOf(index, value);
  }
}

int ApplyLookPreset(es::Registry& registry, LookPreset preset) {
  const bool best = preset == LookPreset::BestLooks;
  int changed = 0;
  const auto count = [&](bool ok, std::string_view id) { changed += ok ? 1 : 0; (void)id; };
  const auto setInt = [&](const char* id, int32_t v) {
    const size_t i = registry.Find(id);
    if (i == es::kNoSetting || registry.ValueOf(i).i == v) return;
    count(registry.SetInt(id, v), id);
  };
  const auto setEnum = [&](const char* id, const char* choice) {
    const size_t i = registry.Find(id);
    if (i == es::kNoSetting || registry.ValueOf(i).i == registry.ChoiceIndex(id, choice)) return;
    count(registry.SetEnum(id, choice), id);
  };
  const auto setBool = [&](const char* id, bool v) {
    const size_t i = registry.Find(id);
    if (i == es::kNoSetting || (registry.ValueOf(i).i != 0) == v) return;
    count(registry.SetBool(id, v), id);
  };
  setInt("video.resolution_scale", best ? 3 : 1);
  setEnum("video.frame_rate", best ? "monitor" : "original");
  setEnum("video.anti_aliasing", best ? "msaa4_fxaa" : "off");
  setEnum("video.texture_filtering", best ? "16x" : "original");
  return changed;
}

bool DrawPresetChooser(es::Registry& registry, const Options& options) {
  bool changed = false;
  static es::Preset sPending = es::Preset::Original;
  bool ask = false;
  const es::Preset now = registry.CurrentPreset();
  const float indent = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x;
  for (es::Preset p : {es::Preset::Original, es::Preset::Enhanced, es::Preset::Custom}) {
    ImGui::PushID(static_cast<int>(p));
    std::string label = PresetName(p);
    if (p == es::Preset::Original) {
      label = Tr("Original  (the GameCube game)");
    } else if (p == es::Preset::Enhanced) {
      label = Tr("Enhanced  (recommended improvements)");
    } else if (now == es::Preset::Custom) {
      label = Fmt(Tr("Custom  (started from %s)"), PresetName(registry.BasePreset()));
    }
    if (options.headingFont != nullptr) {
      ImGui::PushFont(static_cast<ImFont*>(options.headingFont));
    }
    const bool clicked = ImGui::RadioButton(label.c_str(), now == p) && now != p;
    if (options.headingFont != nullptr) {
      ImGui::PopFont();
    }
    if (clicked) {
      if (p == es::Preset::Custom) {
        registry.ApplyPreset(p);
        changed = true;
      } else if (now == es::Preset::Custom) {
        sPending = p;
        ask = true;
      } else {
        registry.ApplyPreset(p);
        changed = true;
      }
    }
    ImGui::Indent(indent);
    Wrapped(options.colors.dim, PresetExplanation(p));
    ImGui::Unindent(indent);
    ImGui::Spacing();
    ImGui::PopID();
  }
  ImGui::Spacing();
  Wrapped(options.colors.dim,
          Tr("Look presets: set resolution, frame rate, anti-aliasing and texture filtering "
             "together. Only how the picture is drawn changes, never the game."));
  if (ImGui::Button(Lbl(Tr("Best looks (fast PC)"), "Best looks (fast PC)").c_str())) {
    changed |= ApplyLookPreset(registry, LookPreset::BestLooks) > 0;
  }
  ImGui::SameLine();
  if (ImGui::Button(Lbl(Tr("Smooth on a weak PC"), "Smooth on a weak PC").c_str())) {
    changed |= ApplyLookPreset(registry, LookPreset::SmoothOnWeakPC) > 0;
  }
  ImGui::Spacing();
  if (ask) {
    ImGui::OpenPopup(Lbl(Tr("Switch preset"), "Switch preset").c_str());
  }
  if (ImGui::BeginPopupModal(Lbl(Tr("Switch preset"), "Switch preset").c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::Text(Tr("Switch to %s?"), PresetName(sPending));
    ImGui::TextUnformatted(Tr("Your own changes to the game settings are replaced by its settings."));
    ImGui::TextUnformatted(Tr("(Volumes, window and controls stay as you set them.)"));
    ImGui::Spacing();
    if (ImGui::Button(Lbl(Tr("Switch"), "Switch").c_str())) {
      registry.ApplyPreset(sPending);
      changed = true;
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button(Lbl(Tr("Keep my settings"), "Keep my settings").c_str())) {
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }

  ImGui::Separator();
  const std::vector<size_t> differ = registry.NotOriginal();
  if (differ.empty()) {
    Heading(options, Tr("Nothing is changed"));
    Wrapped(options.colors.dim, Tr("This is the game exactly as on the GameCube."));
  } else {
    Heading(options, Tr("What is different from the GameCube game"));
    for (size_t i : differ) {
      const es::Definition& d = registry.Get(i);
      if (!options.gameId.empty() && !d.AppliesTo(options.gameId)) {
        continue;
      }
      ImGui::BulletText(d.implemented ? Tr("%s: %s (GameCube: %s)") : Tr("%s: %s (GameCube: %s)  - doesn't work yet"),
                        Tr(d.name).c_str(), ValueForPeople(registry, i, registry.ValueOf(i)).c_str(),
                        ValueForPeople(registry, i, registry.DefaultOf(i, es::Preset::Original)).c_str());
    }
  }
  return changed;
}

bool DrawSettingRow(es::Registry& registry, size_t index, const Options& options) {
  static std::unordered_map<std::string, std::string> sEdits;
  const es::Definition& d = registry.Get(index);
  bool changed = false;
  ImGui::PushID(d.id.c_str());
  ImGui::TableNextRow();
  ImGui::TableSetColumnIndex(0);
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted(Tr(d.name).c_str());
  if (!d.implemented) {
    ImGui::SameLine();
    ImGui::TextColored(Col(options.colors.faint), "%s", Tr("(doesn't work yet)"));
  }
  Wrapped(options.colors.dim, Tr(d.description));
  const es::Value value = registry.ValueOf(index);
  const es::Value original = registry.DefaultOf(index, es::Preset::Original);
  if (!d.preference && !SameAs(registry, index, value, original)) {
    ImGui::TextColored(Col(options.colors.accent), Tr("Changed: on the GameCube this is %s"),
                       ValueForPeople(registry, index, original).c_str());
  }

  ImGui::TableSetColumnIndex(1);
  if (!d.implemented) {
    ImGui::BeginDisabled();
  }
  ImGui::SetNextItemWidth(-FLT_MIN);
  es::Value v = value;
  switch (d.type) {
  case es::Type::Bool: {
    bool on = v.i != 0;
    if (ImGui::Checkbox((std::string(on ? Tr("On") : Tr("Off")) + "###v").c_str(), &on)) {
      v.i = on ? 1 : 0;
      changed = registry.Set(index, v);
    }
    break;
  }
  case es::Type::Int:
    if (!d.intNames.empty()) {
      const std::string preview = ValueForPeople(registry, index, v);
      if (ImGui::BeginCombo("##v", preview.c_str())) {
        for (const auto& [number, name] : d.intNames) {
          if (ImGui::Selectable(Lbl(name).c_str(), number == v.i) && number != v.i) {
            v.i = number;
            changed = registry.Set(index, v);
          }
        }
        ImGui::EndCombo();
      }
    } else if (ImGui::SliderInt("##v", &v.i, d.minInt, d.maxInt, "%d", ImGuiSliderFlags_AlwaysClamp)) {
      changed = registry.Set(index, v);
    }
    break;
  case es::Type::Float:
    if (ImGui::SliderFloat("##v", &v.f, d.minFloat, d.maxFloat, "%.2f", ImGuiSliderFlags_AlwaysClamp)) {
      changed = registry.Set(index, v);
    }
    break;
  case es::Type::Enum: {
    const std::string preview = ValueForPeople(registry, index, v);
    if (ImGui::BeginCombo("##v", preview.c_str())) {
      for (int32_t c = 0; c < static_cast<int32_t>(d.choices.size()); c++) {
        if (ImGui::Selectable(Lbl(d.choices[c].name).c_str(), c == v.i) && c != v.i) {
          v.i = c;
          changed = registry.Set(index, v);
        }
      }
      ImGui::EndCombo();
    }
    break;
  }
  case es::Type::Text: {
    static std::string sEditing, sRefused;
    std::string& edit = sEdits[d.id];
    if (sEditing != d.id && sRefused != d.id) {
      edit = v.s;
    }
    if (options.textInput != nullptr) {
      options.textInput("##v", &edit);
    } else {
      ImGui::InputText("##v", &edit);
    }
    if (ImGui::IsItemActive()) {
      sEditing = d.id;
    } else if (sEditing == d.id) {
      sEditing.clear();
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) {
      if (registry.SetText(d.id, edit)) {
        changed = true;
        sRefused.clear();
      } else {
        sRefused = d.id;
      }
    }
    if (sRefused == d.id) {
      ImGui::TextColored(Col(options.colors.bad), "%s", Tr("Not accepted"));
    }
    break;
  }
  }
  if (!d.implemented) {
    ImGui::EndDisabled();
  }
  ImGui::PopID();
  return changed;
}

bool DrawCategory(es::Registry& registry, es::Category category, const Options& options) {
  bool changed = false;
  const std::vector<size_t> rows = VisibleSettings(registry, category, options);
  if (rows.empty()) {
    Wrapped(options.colors.dim, Tr("There are no settings here yet."));
    return false;
  }
  if (ImGui::BeginTable("##settings", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                                             ImGuiTableFlags_PadOuterX)) {
    ImGui::TableSetupColumn("what", ImGuiTableColumnFlags_WidthStretch, 3.0f);
    ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch, 1.4f);
    for (size_t i : rows) {
      changed |= DrawSettingRow(registry, i, options);
    }
    ImGui::EndTable();
  }
  return changed;
}

void ResetCategory(es::Registry& registry, es::Category category, const Options& options) {
  const es::Preset preset = registry.CurrentPreset();
  for (size_t i : VisibleSettings(registry, category, options)) {
    registry.Set(i, registry.DefaultOf(i, preset));
  }
  Relabel(registry);
}

void ResetToPreset(es::Registry& registry) { registry.ApplyPreset(BaseOf(registry)); }

namespace {

std::string PortId(const char* device, int port, const char* key) {
  return std::string(device) + std::to_string(port + 1) + "." + key;
}

const char* DeviceName(ec::Device d) {
  switch (d) {
  case ec::Device::Keyboard: return Tr("the keyboard");
  case ec::Device::Xbox: return Tr("an Xbox controller");
  case ec::Device::PlayStation: return Tr("a PlayStation controller");
  case ec::Device::Switch: return Tr("a Switch controller");
  case ec::Device::GameCube: return Tr("a GameCube controller");
  case ec::Device::NsoGameCube: return Tr("a Switch Online GameCube controller");
  default: return Tr("a controller of no known make (buttons named as on Xbox)");
  }
}

const char* PresetMeaning(std::optional<ec::PadPreset> preset, bool autoKind) {
  if (!preset) {
    return autoKind ? Tr("Auto: the standard buttons for whatever controller this is, by place (the GameCube's A is "
                         "the bottom face button). Changed buttons are marked in the table.")
                    : Tr("Your own buttons (changed in the table).");
  }
  switch (*preset) {
  case ec::PadPreset::GameCubeAdapter:
    return Tr("A GameCube controller through Nintendo's adapter (WUP-028) or a Mayflash adapter in Wii U mode: every "
              "button where it is on the controller, L and R with their click. (A Mayflash in PC mode reports itself "
              "as an ordinary controller and already maps A, B, X, Y right with Auto.)");
  case ec::PadPreset::Xbox:
    return Tr("Xbox: GameCube buttons by place: A is A, B is X (left), X is B (right), Y is Y; Z is RB; L and R are "
              "the triggers.");
  case ec::PadPreset::PlayStation:
    return Tr("PlayStation: A is Cross, B is Circle, X is Square, Y is Triangle; Z is R1; L and R are L2 and R2.");
  case ec::PadPreset::SwitchPro:
    return Tr("Switch Pro: by the letters printed on it, as Nintendo's GameCube games on Switch do: A is A, B is B, X "
              "is X, Y is Y; Z is R; L and R are ZL and ZR.");
  default: return "";
  }
}

std::string PadPresetLabel(ec::PadPreset p) {
  switch (p) {
  case ec::PadPreset::GameCubeAdapter: return Tr("GameCube controller (adapter)");
  case ec::PadPreset::Xbox: return Tr("Xbox");
  case ec::PadPreset::PlayStation: return Tr("PlayStation");
  case ec::PadPreset::SwitchPro: return Tr("Switch Pro");
  default: return ec::PadPresetName(p);
  }
}

std::string ActionLabel(ec::Action a) {
  return a == ec::Action::FastForward ? std::string(Tr("Fast-forward")) : Tr(ec::ActionName(a));
}

bool ChoiceButton(const std::string& shown, const std::string& id, bool active, const Options& o) {
  if (active) {
    const ImVec4 a = Col(o.colors.accent);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(a.x, a.y, a.z, 0.55f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(a.x, a.y, a.z, 0.70f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(a.x, a.y, a.z, 0.85f));
  }
  const bool clicked = ImGui::Button((shown + "##" + id).c_str());
  if (active) {
    ImGui::PopStyleColor(3);
  }
  return clicked;
}

ImU32 Rgb(uint32_t rgb, float alpha = 1.0f) {
  return ImGui::GetColorU32(ImVec4(((rgb >> 16) & 0xFF) / 255.0f, ((rgb >> 8) & 0xFF) / 255.0f, (rgb & 0xFF) / 255.0f,
                                   alpha));
}

}

bool ControlsPanel::AdapterPadShown() const {
  return std::any_of(pads_.begin(), pads_.end(), [](const Pad& p) { return p.device == ec::Device::GameCube; });
}

int ControlsPanel::AssignedPad(const es::Registry& registry, int port) const {
  if (port < 0 || port >= ec::kPorts) {
    return -1;
  }
  std::array<ec::ControllerChoice, 4> choices;
  for (int p = 0; p < ec::kPorts; p++) {
    choices[static_cast<size_t>(p)] = ec::ReadPort(registry, p).controller;
  }
  std::vector<ec::ConnectedPad> pads;
  for (const Pad& p : pads_) {
    pads.push_back({p.guid, p.name});
  }
  return ec::AssignControllers(choices, pads)[static_cast<size_t>(port)];
}

ec::Device ControlsPanel::DetectedDevice(const es::Registry& registry) const {
  const int pad = AssignedPad(registry, port_);
  return pad >= 0 ? pads_[static_cast<size_t>(pad)].device : device_;
}

ec::Device ControlsPanel::Device(const es::Registry& registry) const {
  return ec::EffectiveDevice(ec::ReadPort(registry, port_).gamepad, DetectedDevice(registry));
}

ec::PadReading ControlsPanel::LiveReading(const es::Registry& registry) const {
  const ec::PortMap map = ec::ReadPort(registry, port_);
  std::vector<ec::Binding> bindings;
  if (testing_) {
    bindings = ec::ResolveKeyboard(map.keyboard);
  }
  const int pad = AssignedPad(registry, port_);
  if (pad >= 0) {
    const ec::Device detected = pads_[static_cast<size_t>(pad)].device;
    const bool emulate = detected != ec::Device::GameCube && detected != ec::Device::NsoGameCube;
    const std::vector<ec::Binding> padBindings =
        ec::ResolveGamepad(map.gamepad, ec::EffectiveDevice(map.gamepad, detected), emulate);
    bindings.insert(bindings.end(), padBindings.begin(), padBindings.end());
  }
  return ec::Evaluate(bindings, raw_);
}

bool ControlsPanel::DrawPortTabs(es::Registry& registry, const Options& options) {
  (void)registry;
  (void)options;
  if (!ImGui::BeginTabBar("##ports")) {
    return false;
  }
  for (int p = 0; p < ec::kPorts; p++) {
    const std::string label = Fmt(Tr("Controller %d"), p + 1) + "##Controller " + std::to_string(p + 1);
    const ImGuiTabItemFlags flags = shownTab_ != port_ && p == port_ ? ImGuiTabItemFlags_SetSelected : 0;
    if (ImGui::BeginTabItem(label.c_str(), nullptr, flags)) {
      if (shownTab_ != port_) {
        if (p == port_) {
          shownTab_ = port_;
        }
      } else if (p != port_) {
        port_ = shownTab_ = p;
        Cancel();
      }
      ImGui::EndTabItem();
    }
  }
  ImGui::EndTabBar();
  return false;
}

bool ControlsPanel::DrawController(es::Registry& registry, const Options& options) {
  bool changed = false;
  const int port = port_;
  ec::PortMap map = ec::ReadPort(registry, port);

  std::vector<ec::ConnectedPad> pads;
  for (const Pad& p : pads_) {
    pads.push_back({p.guid, p.name});
  }
  const auto padLabel = [&](size_t i) {
    std::string s = pads_[i].name.empty() ? std::string(Tr("Controller")) : pads_[i].name;
    bool twin = false;
    for (size_t j = 0; j < pads_.size(); j++) {
      twin |= j != i && pads_[j].guid == pads_[i].guid;
    }
    if (twin) {
      s += " (" + std::to_string(ec::NthOfModel(pads, i) + 1) + ")";
    }
    return s;
  };
  const int assigned = AssignedPad(registry, port);
  const ec::ControllerChoice& choice = map.controller;
  int pinnedIndex = -1;
  if (choice.mode == ec::ControllerChoice::Mode::Pinned) {
    for (size_t i = 0; i < pads.size(); i++) {
      if (pads[i].guid == choice.guid && ec::NthOfModel(pads, i) == choice.nth) {
        pinnedIndex = static_cast<int>(i);
      }
    }
  }
  std::string preview = choice.mode == ec::ControllerChoice::Mode::Auto   ? std::string(Tr("Automatic: the first free controller"))
                        : choice.mode == ec::ControllerChoice::Mode::None ? std::string(Tr("None: keyboard only"))
                        : pinnedIndex >= 0 ? padLabel(static_cast<size_t>(pinnedIndex))
                                           : Fmt(Tr("%s (not plugged in)"),
                                                 (choice.name.empty() ? std::string(Tr("A controller")) : choice.name).c_str());
  ImGui::AlignTextToFramePadding();
  const char* pcLabel = Tr("PC controller");
  ImGui::TextUnformatted(pcLabel);
  ImGui::SameLine();
  ImGui::SetNextItemWidth(ImGui::GetFontSize() * 24);
  if (ImGui::BeginCombo("##pc", preview.c_str())) {
    const auto store = [&](const ec::ControllerChoice& c) {
      if (c.mode == ec::ControllerChoice::Mode::Pinned) {
        for (int other = 0; other < ec::kPorts; other++) {
          if (other == port) {
            continue;
          }
          ec::PortMap o = ec::ReadPort(registry, other);
          if (o.controller.mode == ec::ControllerChoice::Mode::Pinned && o.controller.guid == c.guid &&
              o.controller.nth == c.nth) {
            o.controller = {};
            changed |= ec::WritePort(registry, other, o);
          }
        }
      }
      map.controller = c;
      changed |= ec::WritePort(registry, port, map);
    };
    if (ImGui::Selectable(Lbl(Tr("Automatic: the first free controller"), "Automatic: the first free controller").c_str(), choice.mode == ec::ControllerChoice::Mode::Auto)) {
      store(ec::ControllerChoice::Auto());
    }
    if (ImGui::Selectable(Lbl(Tr("None: keyboard only"), "None: keyboard only").c_str(), choice.mode == ec::ControllerChoice::Mode::None)) {
      store({});
    }
    if (choice.mode == ec::ControllerChoice::Mode::Pinned && pinnedIndex < 0) {
      ImGui::Selectable(preview.c_str(), true);
    }
    for (size_t i = 0; i < pads_.size(); i++) {
      ImGui::PushID(static_cast<int>(i));
      if (ImGui::Selectable(padLabel(i).c_str(), static_cast<int>(i) == pinnedIndex)) {
        store(ec::ControllerChoice::Pin(pads[i].guid, ec::NthOfModel(pads, i), pads_[i].name));
      }
      ImGui::PopID();
    }
    ImGui::EndCombo();
  }
  if (assigned >= 0) {
    const Pad& pad = pads_[static_cast<size_t>(assigned)];
    Wrapped(options.colors.dim, Fmt(Tr("Plays now: %s (it says it is %s)."), padLabel(static_cast<size_t>(assigned)).c_str(),
                                    DeviceName(pad.device)));
  } else if (choice.mode == ec::ControllerChoice::Mode::None) {
    Wrapped(options.colors.dim,
            Fmt(port == 0 ? Tr("No PC controller: controller %d is played with the keyboard only.")
                          : Tr("No PC controller: controller %d is played with the keyboard only (if you give it keys below)."),
                port + 1));
  } else {
    Wrapped(options.colors.dim, pads_.empty() ? std::string(Tr("No PC controller is plugged in. Plug one in: it shows "
                                                               "up here at once."))
                                              : std::string(Tr("No free PC controller for it right now.")));
  }
  if (!AdapterPadShown()) {
    switch (adapterState_) {
    case ec::AdapterState::NoDriver:
      Wrapped(options.colors.accent,
              Tr("A GameCube adapter is plugged in, but Windows has no driver for it that this program can use. "
                 "Install the WinUSB driver for it with the free program Zadig (zadig.akeo.ie): choose the "
                 "adapter (WUP-028), pick WinUSB, click Replace Driver. The switch on the adapter must be on "
                 "\"Wii U\". The Read me has the steps."));
      break;
    case ec::AdapterState::NoPermission:
      Wrapped(options.colors.accent,
              Tr("A GameCube adapter is plugged in, but this user may not open it. Add the udev rule from the Read "
                 "me (one small file), then unplug the adapter and plug it in again."));
      break;
    case ec::AdapterState::InUse:
      Wrapped(options.colors.accent,
              Tr("A GameCube adapter is plugged in, but it is in use by another program (for example Dolphin, or "
                 "another copy of the game). Close that program: the adapter is tried again by itself in a few "
                 "seconds (if not, unplug it and plug it in again). If no other program runs, plug a controller "
                 "into one of the adapter's four ports."));
      break;
    case ec::AdapterState::WrongDriver:
      Wrapped(options.colors.accent,
              Fmt(Tr("A GameCube adapter is plugged in, but Windows has the driver \"%s\" on it, and this program "
                     "can only use WinUSB (Dolphin may work with other drivers). Start the free program Zadig "
                     "(zadig.akeo.ie), choose the adapter (WUP-028), pick WinUSB and click Replace Driver. Then "
                     "unplug the adapter and plug it in again."),
                  adapterDriver_.c_str()));
      break;
    case ec::AdapterState::NotListed:
      Wrapped(options.colors.accent,
              Tr("Windows knows a GameCube adapter, but this program's USB library does not list it. Unplug the "
                 "adapter and plug it in again. If this stays, send the launcher's log (see the Read me)."));
      break;
    case ec::AdapterState::LibraryMissing:
      Wrapped(options.colors.accent,
              Tr("A GameCube adapter is plugged in, but the USB file libusb-1.0.dll is missing next to the "
                 "program. Unpack the whole download again (all of the game folder), then start again."));
      break;
    case ec::AdapterState::Reachable:
      Wrapped(options.colors.dim,
              Tr("A GameCube adapter is plugged in and working. Plug a controller into one of its four ports: it "
                 "shows up here at once. (The switch on the adapter must be on \"Wii U\".)"));
      break;
    case ec::AdapterState::Unknown:
      Wrapped(options.colors.accent,
              Fmt(Tr("A GameCube adapter is plugged in, but the program cannot open it (error %d)."), adapterError_));
      break;
    case ec::AdapterState::NoLibrary:
    case ec::AdapterState::NotPlugged: break;
    }
  }
  if (pads_.size() > 0) {
    std::string list;
    for (size_t i = 0; i < pads_.size(); i++) {
      list += (i == 0 ? "" : ", ") + padLabel(i);
    }
    Wrapped(options.colors.faint, Fmt(Tr("Plugged in: %s"), list.c_str()));
  }

  ImGui::Spacing();
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted(Tr("Controller preset"));
  const std::optional<ec::PadPreset> preset = ec::PadPresetOf(map.gamepad);
  const bool autoKind = !map.gamepad.kind && map.gamepad.layout == ec::Layout::Auto &&
                        std::all_of(map.gamepad.buttons.begin(), map.gamepad.buttons.end(),
                                    [](const auto& b) { return !b.has_value(); });
  ImGui::SameLine();
  if (ChoiceButton(Tr("Auto"), "Auto", autoKind, options)) {
    ec::GamepadMap g = map.gamepad;
    g.kind.reset();
    g.layout = ec::Layout::Auto;
    for (auto& b : g.buttons) {
      b.reset();
    }
    map.gamepad = g;
    changed |= ec::WritePort(registry, port, map);
  }
  for (int p = 0; p < static_cast<int>(ec::PadPreset::Count); p++) {
    const ec::PadPreset pp = static_cast<ec::PadPreset>(p);
    const float buttonWidth =
        ImGui::CalcTextSize(PadPresetLabel(pp).c_str()).x + ImGui::GetStyle().FramePadding.x * 2;
    const float rightEdge = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
    if (ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + buttonWidth <= rightEdge) {
      ImGui::SameLine();
    }
    if (ChoiceButton(PadPresetLabel(pp), ec::PadPresetName(pp), preset == pp, options)) {
      map.gamepad = ec::ApplyPadPreset(map.gamepad, pp);
      changed |= ec::WritePort(registry, port, map);
    }
  }
  Wrapped(options.colors.dim, PresetMeaning(preset, autoKind));

  ImGui::Spacing();
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted(Tr("Keyboard"));
  ImGui::SameLine();
  const bool standardKeys = map.keyboard == ec::DefaultKeyboard(port);
  if (ChoiceButton(port == 0 ? Tr("Keyboard default") : Tr("No keys (default)"),
                   port == 0 ? "Keyboard default" : "No keys (default)", standardKeys, options)) {
    map.keyboard = ec::DefaultKeyboard(port);
    changed |= ec::WritePort(registry, port, map);
  }
  if (port != 0) {
    ImGui::SameLine();
    if (ImGui::Button(Lbl(Tr("Copy controller 1's keys"), "Copy controller 1's keys").c_str())) {
      map.keyboard = ec::ReadPort(registry, 0).keyboard;
      changed |= ec::WritePort(registry, port, map);
    }
  }
  Wrapped(options.colors.dim,
          port == 0 ? Tr("Keyboard default: Dolphin's keys. X = A, Z = B, C = X, S = Y, D = Z, Q = L, "
                         "W = R, Enter = Start, arrow keys = main stick, I J K L = C-stick, "
                         "T F G H = D-pad.")
                    : Tr("Controllers 2-4 have no keys unless you give them some (all keyboards are "
                         "one keyboard: a key used twice presses both)."));

  ImGui::Spacing();
  bool rumble = map.gamepad.rumble;
  if (ImGui::Checkbox(Lbl(Tr("Rumble"), "Rumble").c_str(), &rumble)) {
    map.gamepad.rumble = rumble;
    changed |= ec::WritePort(registry, port, map);
  }
  ImGui::SameLine();
  const bool canTest = assigned >= 0 && map.gamepad.rumble;
  if (!canTest) {
    ImGui::BeginDisabled();
  }
  if (ImGui::Button(Lbl(Tr("Test rumble"), "Test rumble").c_str())) {
    rumbleRequest_ = true;
  }
  if (!canTest) {
    ImGui::EndDisabled();
  }
  Wrapped(options.colors.dim, Tr("When the game rumbles the controller (the game's own Rumble option in its menu still "
                                 "decides when). Off: the controller never rumbles."));
  return changed;
}

bool ControlsPanel::DrawMapping(es::Registry& registry, const Options& options) {
  bool changed = false;
  const int port = port_;
  ec::PortMap map = ec::ReadPort(registry, port);
  const ec::Device device = Device(registry);

  const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_PadOuterX;
  Wrapped(options.colors.dim, Tr("Click a key or button to change it, then press the new one (Esc: cancel). \"...\": "
                                 "add a second one, none, or the standard one. Violet: changed from the standard."));
  const auto cell = [&](bool keyboard, size_t g, bool walk, int action, const std::string& text, bool changedHere,
                        bool listening, const char* listenText) {
    const float more = ImGui::CalcTextSize("...").x + ImGui::GetStyle().FramePadding.x * 2;
    const float width = ImGui::GetContentRegionAvail().x - more - ImGui::GetStyle().ItemSpacing.x;
    if (listening) {
      const ImVec4 a = Col(options.colors.accent);
      ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(a.x, a.y, a.z, 0.35f));
    } else if (changedHere) {
      ImGui::PushStyleColor(ImGuiCol_Text, Col(options.colors.accent));
    }
    if (ImGui::Button(((listening ? std::string(listenText) : text) + "##cell").c_str(), ImVec2(width, 0))) {
      listen_ = listening ? Listen{} : Listen{true, keyboard, false, walk, port_, g, action};
    }
    if (listening || changedHere) {
      ImGui::PopStyleColor();
    }
    ImGui::SameLine();
    if (ImGui::Button("...")) {
      ImGui::OpenPopup("more");
    }
    bool edited = false;
    if (ImGui::BeginPopup("more")) {
      ec::PortMap map = ec::ReadPort(registry, port_);
      if (!walk && ImGui::Selectable((keyboard ? Lbl(Tr("Add another key"), "Add another key") : Lbl(Tr("Add another button"), "Add another button")).c_str())) {
        listen_ = {true, keyboard, true, false, port_, g, action};
      }
      if (ImGui::Selectable((keyboard ? Lbl(Tr("No key"), "No key") : Lbl(Tr("No button"), "No button")).c_str())) {
        if (action >= 0) {
          (keyboard ? map.actions.keys : map.actions.pad)[static_cast<size_t>(action)].clear();
        } else if (walk) {
          map.keyboard.walk = {};
        } else if (keyboard) {
          map.keyboard.keys[g].clear();
        } else {
          map.gamepad.buttons[g] = std::vector<ec::Input>();
        }
        edited = true;
      }
      if (!walk && ImGui::Selectable((keyboard ? Lbl(Tr("Standard key"), "Standard key") : Lbl(Tr("Standard button"), "Standard button")).c_str())) {
        if (action >= 0) {
          const ec::ActionMap def = ec::DefaultActions(port_);
          const size_t a = static_cast<size_t>(action);
          (keyboard ? map.actions.keys : map.actions.pad)[a] = (keyboard ? def.keys : def.pad)[a];
        } else if (keyboard) {
          map.keyboard.keys[g] = ec::DefaultKeyboard(port_).keys[g];
        } else {
          map.gamepad.buttons[g].reset();
        }
        edited = true;
      }
      if (edited) {
        changed |= ec::WritePort(registry, port_, map);
        listen_ = {};
      }
      ImGui::EndPopup();
    }
  };
  if (ImGui::BeginTable("##map", 3, flags)) {
    ImGui::TableSetupColumn(Lbl(Tr("GameCube"), "GameCube").c_str(), ImGuiTableColumnFlags_WidthStretch, 0.8f);
    ImGui::TableSetupColumn(Lbl(Tr("Keyboard"), "Keyboard").c_str(), ImGuiTableColumnFlags_WidthStretch, 1.0f);
    ImGui::TableSetupColumn(Lbl(Tr("PC controller"), "PC controller").c_str(), ImGuiTableColumnFlags_WidthStretch, 1.2f);
    ImGui::TableHeadersRow();
    const auto listeningTo = [&](bool keyboard, size_t input, bool walk) {
      return listen_.active && listen_.port == port && listen_.keyboard == keyboard && listen_.walk == walk &&
             listen_.action < 0 && (walk || listen_.input == input);
    };
    const auto listeningToAction = [&](bool keyboard, size_t action) {
      return listen_.active && listen_.port == port && listen_.keyboard == keyboard &&
             listen_.action == static_cast<int>(action);
    };
    for (size_t g = 0; g <= ec::kGcInputCount; g++) {
      const bool walkRow = g == ec::kGcInputCount;
      const ec::GcInput input = static_cast<ec::GcInput>(walkRow ? 0 : g);
      ImGui::PushID(static_cast<int>(g));
      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      ImGui::AlignTextToFramePadding();
      ImGui::TextUnformatted(walkRow ? Tr("Walk (hold)") : GcName(input).c_str());

      ImGui::TableSetColumnIndex(1);
      ImGui::PushID("k");
      if (walkRow) {
        cell(true, g, true, -1,
             map.keyboard.walk.kind == ec::Input::Kind::None ? std::string(Tr("none"))
                                                             : InputName(map.keyboard.walk, ec::Device::Keyboard),
             map.keyboard.walk.kind != ec::Input::Kind::None, listeningTo(true, g, true), Tr("Press a key..."));
      } else {
        cell(true, g, false, -1, Join(map.keyboard.keys[g], ec::Device::Keyboard),
             map.keyboard.keys[g] != ec::DefaultKeyboard(port).keys[g], listeningTo(true, g, false), Tr("Press a key..."));
      }
      ImGui::PopID();

      ImGui::TableSetColumnIndex(2);
      if (!walkRow) {
        ImGui::PushID("p");
        cell(false, g, false, -1, Join(ec::PadInputs(map.gamepad, input, device), device), map.gamepad.buttons[g].has_value(),
             listeningTo(false, g, false), Tr("Press a button..."));
        ImGui::PopID();
      } else {
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(Col(options.colors.faint), "%s", Tr("(keyboard only: arrow keys tilt part way)"));
      }
      ImGui::PopID();
    }
    const ec::ActionMap defActions = ec::DefaultActions(port);
    for (size_t a = 0; a < ec::kActionCount; a++) {
      ImGui::PushID(static_cast<int>(1000 + a));
      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      ImGui::AlignTextToFramePadding();
      ImGui::TextUnformatted(ActionLabel(static_cast<ec::Action>(a)).c_str());
      ImGui::TableSetColumnIndex(1);
      ImGui::PushID("k");
      cell(true, 0, false, static_cast<int>(a), Join(map.actions.keys[a], ec::Device::Keyboard),
           map.actions.keys[a] != defActions.keys[a], listeningToAction(true, a), Tr("Press a key..."));
      ImGui::PopID();
      ImGui::TableSetColumnIndex(2);
      ImGui::PushID("p");
      cell(false, 0, false, static_cast<int>(a), Join(map.actions.pad[a], device), map.actions.pad[a] != defActions.pad[a],
           listeningToAction(false, a), Tr("Press a button..."));
      ImGui::PopID();
      ImGui::PopID();
    }
    ImGui::EndTable();
  }
  if (ec::kActionCount > 0) {
    Wrapped(options.colors.faint, Tr("Fast-forward is the PC version's own key, not a GameCube button: it works only while "
                                     "the Fast-forward setting is on (Settings, Gameplay; never in Original)."));
  }

  ImGui::Spacing();
  if (ImGui::Button(Lbl(Tr("Reset this controller to the defaults"), "Reset this controller to the defaults").c_str())) {
    ec::PortMap def = ec::DefaultPort(port);
    def.controller = map.controller;
    changed |= ec::WritePort(registry, port, def);
    listen_ = {};
  }
  ImGui::SameLine();
  Wrapped(options.colors.dim, Tr("Keys, buttons, preset, deadzones, click points and rumble as they come."));
  return changed;
}

bool ControlsPanel::DrawSticks(es::Registry& registry, const Options& options) {
  bool changed = false;
  const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_PadOuterX;
  if (ImGui::BeginTable("##sticks", 2, flags)) {
    ImGui::TableSetupColumn("what", ImGuiTableColumnFlags_WidthStretch, 3.0f);
    ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch, 1.4f);
    for (const auto& [device, key] : {std::pair{"gamepad", "left_stick_deadzone"},
                                      std::pair{"gamepad", "right_stick_deadzone"}, std::pair{"gamepad", "l_click"},
                                      std::pair{"gamepad", "r_click"}, std::pair{"gamepad", "layout"},
                                      std::pair{"keyboard", "walk_tilt"}}) {
      const size_t i = registry.Find(PortId(device, port_, key));
      if (i != es::kNoSetting) {
        changed |= DrawSettingRow(registry, i, options);
      }
    }
    ImGui::EndTable();
  }
  return changed;
}

void ControlsPanel::DrawLiveTest(const es::Registry& registry, const Options& options) {
  const ec::PortMap map = ec::ReadPort(registry, port_);
  const ec::PadReading r = LiveReading(registry);
  const int pad = AssignedPad(registry, port_);

  if (ChoiceButton(testing_ ? Tr("Stop the keyboard test (Esc)") : Tr("Try the keyboard"),
                   testing_ ? "Stop the keyboard test (Esc)" : "Try the keyboard", testing_, options)) {
    testing_ = !testing_;
    listen_ = {};
  }
  ImGui::SameLine();
  Wrapped(options.colors.dim,
          testing_ ? Fmt(Tr("The keyboard plays controller %d here now (keys don't work on this page meanwhile; Esc ends it)."),
                         port_ + 1)
          : pad >= 0 ? std::string(Tr("Move the sticks and press buttons: this is what the game reads. Click \"Try the "
                                      "keyboard\" to try the keys."))
                     : std::string(Tr("No PC controller plays as this one. Click \"Try the keyboard\" to try the keys.")));

  const float u = ImGui::GetFontSize() / 16.0f;
  const float w = std::max(ImGui::GetContentRegionAvail().x, 420 * u), h = 236 * u;
  const ImVec2 o = ImGui::GetCursorScreenPos();
  ImGui::Dummy(ImVec2(w, h));
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const ImVec4 acc = Col(options.colors.accent);
  const ImU32 ink = ImGui::GetColorU32(ImGuiCol_Text), dim = ImGui::GetColorU32(Col(options.colors.dim));
  const auto on = [&](uint16_t bit) { return (r.buttons & bit) != 0; };
  const auto label = [&](ImVec2 c, const char* text, ImU32 col) {
    const ImVec2 ts = ImGui::CalcTextSize(text);
    dl->AddText(ImVec2(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f), col, text);
  };
  const auto button = [&](ImVec2 c, float radius, uint32_t rgb, const char* text, bool pressed) {
    dl->AddCircleFilled(c, radius, Rgb(rgb, pressed ? 1.0f : 0.16f));
    dl->AddCircle(c, radius, Rgb(rgb, pressed ? 1.0f : 0.65f), 0, 1.5f * u);
    label(c, text, pressed ? Rgb(0x15101F) : ink);
  };
  const auto stick = [&](ImVec2 c, float radius, uint32_t rgb, int8_t x, int8_t y, float deadzone, const char* name) {
    dl->AddNgonFilled(c, radius, Rgb(rgb, 0.10f), 8);
    dl->AddNgon(c, radius, Rgb(rgb, 0.55f), 8, 1.5f * u);
    if (deadzone > 0.0f) {
      dl->AddCircle(c, radius * deadzone, Rgb(rgb, 0.35f), 0, 1.0f * u);
    }
    dl->AddLine(ImVec2(c.x - radius, c.y), ImVec2(c.x + radius, c.y), Rgb(rgb, 0.18f));
    dl->AddLine(ImVec2(c.x, c.y - radius), ImVec2(c.x, c.y + radius), Rgb(rgb, 0.18f));
    const ImVec2 p(c.x + radius * x / 127.0f, c.y - radius * y / 127.0f);
    const bool moved = x != 0 || y != 0;
    dl->AddLine(c, p, Rgb(rgb, moved ? 0.8f : 0.0f), 2.0f * u);
    dl->AddCircleFilled(p, radius * 0.30f, Rgb(rgb, moved ? 1.0f : 0.55f));
    label(ImVec2(c.x, c.y + radius + 12 * u), name, dim);
    char nums[32];
    std::snprintf(nums, sizeof(nums), "%d, %d", x, y);
    label(ImVec2(c.x, c.y + radius + 28 * u), nums, moved ? ink : dim);
  };
  const auto trigger = [&](ImVec2 a, float width, uint8_t value, bool click, float clickAt, const char* name) {
    const float bh = 16 * u;
    const ImVec2 b(a.x + width, a.y + bh);
    dl->AddRectFilled(a, b, Rgb(0xC8C8D0, 0.10f), 5 * u);
    if (value > 0) {
      dl->AddRectFilled(a, ImVec2(a.x + width * value / 255.0f, b.y), ImGui::GetColorU32(ImVec4(acc.x, acc.y, acc.z, 0.85f)),
                        5 * u);
    }
    if (clickAt > 0.0f) {
      const float x = a.x + width * clickAt;
      dl->AddLine(ImVec2(x, a.y - 3 * u), ImVec2(x, b.y + 3 * u), dim, 1.5f * u);
    }
    dl->AddRect(a, b, click ? ImGui::GetColorU32(acc) : Rgb(0xC8C8D0, 0.5f), 5 * u, 0, click ? 2.5f * u : 1.0f * u);
    char text[48];
    std::snprintf(text, sizeof(text), "%s  %d%s", name, value, click ? Tr("  click") : "");
    dl->AddText(ImVec2(a.x, b.y + 4 * u), click ? ImGui::GetColorU32(acc) : dim, text);
  };

  const bool emulate = pad >= 0 && pads_[static_cast<size_t>(pad)].device != ec::Device::GameCube &&
                       pads_[static_cast<size_t>(pad)].device != ec::Device::NsoGameCube;
  const float tw = w * 0.30f;
  trigger(ImVec2(o.x + w * 0.06f, o.y + 6 * u), tw, r.triggerLeft, on(0x0040), emulate ? map.gamepad.lClickThreshold : 0.f,
          "L");
  trigger(ImVec2(o.x + w * 0.94f - tw, o.y + 6 * u), tw, r.triggerRight, on(0x0020),
          emulate ? map.gamepad.rClickThreshold : 0.f, "R");
  {
    const ImVec2 a(o.x + w * 0.94f - tw * 0.55f, o.y + 46 * u), b(o.x + w * 0.94f, o.y + 64 * u);
    dl->AddRectFilled(a, b, Rgb(0x8A63E6, on(0x0010) ? 1.0f : 0.16f), 9 * u);
    dl->AddRect(a, b, Rgb(0x8A63E6, on(0x0010) ? 1.0f : 0.65f), 9 * u, 0, 1.5f * u);
    label(ImVec2((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f), "Z", on(0x0010) ? Rgb(0x15101F) : ink);
  }
  const float sr = 44 * u;
  stick(ImVec2(o.x + w * 0.17f, o.y + 112 * u), sr, 0xC8C8D0, r.stickX, r.stickY,
        pad >= 0 ? map.gamepad.leftStickDeadzone : 0.f, Tr("Main stick"));
  stick(ImVec2(o.x + w * 0.62f, o.y + 150 * u), sr * 0.72f, 0xF2C230, r.substickX, r.substickY,
        pad >= 0 ? map.gamepad.rightStickDeadzone : 0.f, Tr("C-stick"));
  {
    const ImVec2 c(o.x + w * 0.36f, o.y + 160 * u);
    const float s = 11 * u, g = 13 * u;
    const struct {
      float dx, dy;
      uint16_t bit;
    } arms[] = {{0, -1, 0x0008}, {0, 1, 0x0004}, {-1, 0, 0x0001}, {1, 0, 0x0002}};
    dl->AddRectFilled(ImVec2(c.x - s * 0.5f, c.y - s * 0.5f), ImVec2(c.x + s * 0.5f, c.y + s * 0.5f), Rgb(0xC8C8D0, 0.16f));
    for (const auto& arm : arms) {
      const ImVec2 m(c.x + arm.dx * g, c.y + arm.dy * g);
      const bool p = on(arm.bit);
      dl->AddRectFilled(ImVec2(m.x - s * 0.5f, m.y - s * 0.5f), ImVec2(m.x + s * 0.5f, m.y + s * 0.5f),
                        Rgb(0xC8C8D0, p ? 1.0f : 0.16f), 2 * u);
      dl->AddRect(ImVec2(m.x - s * 0.5f, m.y - s * 0.5f), ImVec2(m.x + s * 0.5f, m.y + s * 0.5f), Rgb(0xC8C8D0, 0.65f),
                  2 * u);
    }
    label(ImVec2(c.x, c.y + g + 20 * u), Tr("D-pad"), dim);
  }
  button(ImVec2(o.x + w * 0.47f, o.y + 100 * u), 10 * u, 0xC8C8D0, "", on(0x1000));
  label(ImVec2(o.x + w * 0.47f, o.y + 122 * u), Tr("Start"), dim);
  {
    const ImVec2 a(o.x + w * 0.83f, o.y + 120 * u);
    button(a, 22 * u, 0x3FBF6A, "A", on(0x0100));
    button(ImVec2(a.x - 40 * u, a.y + 26 * u), 13 * u, 0xE0524A, "B", on(0x0200));
    button(ImVec2(a.x + 40 * u, a.y - 8 * u), 14 * u, 0xC8C8D0, "X", on(0x0400));
    button(ImVec2(a.x - 8 * u, a.y - 40 * u), 14 * u, 0xC8C8D0, "Y", on(0x0800));
  }

  std::string pressed;
  const struct {
    uint16_t bit;
    ec::GcInput input;
  } kNames[] = {{0x0100, ec::GcInput::A},       {0x0200, ec::GcInput::B},        {0x0400, ec::GcInput::X},
                {0x0800, ec::GcInput::Y},       {0x0010, ec::GcInput::Z},        {0x0040, ec::GcInput::L},
                {0x0020, ec::GcInput::R},       {0x1000, ec::GcInput::Start},    {0x0008, ec::GcInput::DpadUp},
                {0x0004, ec::GcInput::DpadDown}, {0x0001, ec::GcInput::DpadLeft}, {0x0002, ec::GcInput::DpadRight}};
  for (const auto& n : kNames) {
    if (on(n.bit)) {
      pressed += (pressed.empty() ? "" : ", ") + GcName(n.input);
    }
  }
  const std::string line =
      Fmt(Tr("Game reads: main stick %d, %d   C-stick %d, %d   L %d   R %d   buttons: %s"), r.stickX, r.stickY, r.substickX,
          r.substickY, r.triggerLeft, r.triggerRight, pressed.empty() ? Tr("none") : pressed.c_str());
  Wrapped(options.colors.dim, line);
}

bool ControlsPanel::Draw(es::Registry& registry, int port, const Options& options) {
  SetPort(port);
  bool changed = false;
  changed |= DrawController(registry, options);
  ImGui::Spacing();
  changed |= DrawMapping(registry, options);
  ImGui::Spacing();
  Heading(options, Tr("Sticks and triggers"));
  changed |= DrawSticks(registry, options);
  ImGui::Spacing();
  Heading(options, Tr("Try it"));
  DrawLiveTest(registry, options);
  return changed;
}

void ControlsPanel::Store(es::Registry& registry, const ec::Input& in) {
  ec::PortMap map = ec::ReadPort(registry, listen_.port);
  const size_t g = listen_.input;
  if (listen_.action >= 0 && static_cast<size_t>(listen_.action) < ec::kActionCount) {
    std::vector<ec::Input>& list =
        (listen_.keyboard ? map.actions.keys : map.actions.pad)[static_cast<size_t>(listen_.action)];
    if (!listen_.append) {
      list.clear();
    }
    if (std::find(list.begin(), list.end(), in) == list.end()) {
      list.push_back(in);
    }
  } else if (listen_.keyboard && listen_.walk) {
    map.keyboard.walk = in;
  } else if (listen_.keyboard) {
    std::vector<ec::Input>& keys = map.keyboard.keys[g];
    if (!listen_.append) {
      keys.clear();
    }
    if (std::find(keys.begin(), keys.end(), in) == keys.end()) {
      keys.push_back(in);
    }
  } else {
    const ec::Device device = ec::EffectiveDevice(map.gamepad, DetectedDevice(registry));
    std::vector<ec::Input> list =
        listen_.append ? ec::PadInputs(map.gamepad, static_cast<ec::GcInput>(g), device) : std::vector<ec::Input>();
    if (std::find(list.begin(), list.end(), in) == list.end()) {
      list.push_back(in);
    }
    map.gamepad.buttons[g] = list;
  }
  ec::WritePort(registry, listen_.port, map);
  listen_ = {};
}

void ControlsPanel::FeedKey(es::Registry& registry, uint16_t scancode) {
  if (!listen_.active) {
    return;
  }
  constexpr uint16_t kEscape = 41;
  if (scancode == kEscape) {
    listen_ = {};
    return;
  }
  if (listen_.keyboard) {
    Store(registry, ec::Input::Key(scancode));
  }
}

void ControlsPanel::FeedPadButton(es::Registry& registry, int button) {
  if (listen_.active && !listen_.keyboard && button >= 0 && button < static_cast<int>(ec::PadButton::Count)) {
    Store(registry, ec::Input::Button(static_cast<ec::PadButton>(button)));
  }
}

void ControlsPanel::FeedPadAxis(es::Registry& registry, int axis, float value) {
  if (!listen_.active || listen_.keyboard || axis < 0 || axis >= static_cast<int>(ec::PadAxis::Count)) {
    return;
  }
  if (value > 0.6f || value < -0.6f) {
    Store(registry, ec::Input::Axis(static_cast<ec::PadAxis>(axis), value > 0 ? 1 : -1));
  }
}

}
