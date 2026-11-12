#include "settings_ui.h"

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <optional>
#include <utility>
#include <cfloat>
#include <string>
#include <unordered_map>

namespace engine::settings_ui {

namespace es = engine::settings;
namespace ec = engine::controls;

namespace {

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

std::string Join(const std::vector<ec::Input>& inputs, ec::Device device) {
  std::string out;
  for (const ec::Input& in : inputs) {
    out += (out.empty() ? "" : ", ") + ec::InputLabel(in, device).text;
  }
  return out.empty() ? std::string("none") : out;
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
  case es::Preset::Original: return "Original";
  case es::Preset::Enhanced: return "Enhanced";
  case es::Preset::Custom: return "Custom";
  }
  return "";
}

const char* PresetExplanation(es::Preset preset) {
  switch (preset) {
  case es::Preset::Original:
    return "Exactly the GameCube game: same speed, same loading times, same picture, same bugs. Nothing extra "
           "appears in the game.";
  case es::Preset::Enhanced:
    return "The recommended improvements: a sharper, wider picture, smoother movement and shorter waits. The "
           "game's own rules (battles, bugs, purification) stay as they were. Some of these don't work yet.";
  case es::Preset::Custom:
    return "Your own choice: starts from Original or Enhanced, and you change single settings on the "
           "Settings page.";
  }
  return "";
}

std::string ValueForPeople(const es::Registry& registry, size_t index, const es::Value& value) {
  const es::Definition& d = registry.Get(index);
  switch (d.type) {
  case es::Type::Bool: return value.i != 0 ? "On" : "Off";
  case es::Type::Enum:
    return value.i >= 0 && value.i < static_cast<int32_t>(d.choices.size()) ? d.choices[value.i].name : "?";
  case es::Type::Text: return value.s.empty() ? "(empty)" : value.s;
  case es::Type::Int:
    if (const std::string name = d.IntName(value.i); !name.empty()) {
      return name;
    }
    return registry.TextOf(index, value);
  default: return registry.TextOf(index, value);
  }
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
      label += "  (the GameCube game)";
    } else if (p == es::Preset::Enhanced) {
      label += "  (recommended improvements)";
    } else if (now == es::Preset::Custom) {
      label += std::string("  (started from ") + PresetName(registry.BasePreset()) + ")";
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
  if (ask) {
    ImGui::OpenPopup("Switch preset");
  }
  if (ImGui::BeginPopupModal("Switch preset", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::Text("Switch to %s?", PresetName(sPending));
    ImGui::TextUnformatted("Your own changes to the game settings are replaced by its settings.");
    ImGui::TextUnformatted("(Volumes, window and controls stay as you set them.)");
    ImGui::Spacing();
    if (ImGui::Button("Switch")) {
      registry.ApplyPreset(sPending);
      changed = true;
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Keep my settings")) {
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }

  ImGui::Separator();
  const std::vector<size_t> differ = registry.NotOriginal();
  if (differ.empty()) {
    Heading(options, "Nothing is changed");
    Wrapped(options.colors.dim, "This is the game exactly as on the GameCube.");
  } else {
    Heading(options, "What is different from the GameCube game");
    for (size_t i : differ) {
      const es::Definition& d = registry.Get(i);
      if (!options.gameId.empty() && !d.AppliesTo(options.gameId)) {
        continue;
      }
      ImGui::BulletText("%s: %s (GameCube: %s)%s", d.name.c_str(),
                        ValueForPeople(registry, i, registry.ValueOf(i)).c_str(),
                        ValueForPeople(registry, i, registry.DefaultOf(i, es::Preset::Original)).c_str(),
                        d.implemented ? "" : "  - doesn't work yet");
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
  ImGui::TextUnformatted(d.name.c_str());
  if (!d.implemented) {
    ImGui::SameLine();
    ImGui::TextColored(Col(options.colors.faint), "(doesn't work yet)");
  }
  Wrapped(options.colors.dim, d.description);
  const es::Value value = registry.ValueOf(index);
  const es::Value original = registry.DefaultOf(index, es::Preset::Original);
  if (!d.preference && !SameAs(registry, index, value, original)) {
    ImGui::TextColored(Col(options.colors.accent), "Changed: on the GameCube this is %s",
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
    if (ImGui::Checkbox(on ? "On###v" : "Off###v", &on)) {
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
          if (ImGui::Selectable(name.c_str(), number == v.i) && number != v.i) {
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
        if (ImGui::Selectable(d.choices[c].name.c_str(), c == v.i) && c != v.i) {
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
    ImGui::InputText("##v", &edit);
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
      ImGui::TextColored(Col(options.colors.bad), "Not accepted");
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
    Wrapped(options.colors.dim, "There are no settings here yet.");
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
  case ec::Device::Keyboard: return "the keyboard";
  case ec::Device::Xbox: return "an Xbox controller";
  case ec::Device::PlayStation: return "a PlayStation controller";
  case ec::Device::Switch: return "a Switch controller";
  case ec::Device::GameCube: return "a GameCube controller";
  case ec::Device::NsoGameCube: return "a Switch Online GameCube controller";
  default: return "a controller of no known make (buttons named as on Xbox)";
  }
}

const char* PresetMeaning(std::optional<ec::PadPreset> preset, bool autoKind) {
  if (!preset) {
    return autoKind ? "Auto: the standard buttons for whatever controller this is, by place (the GameCube's A is "
                      "the bottom face button). Changed buttons are marked in the table."
                    : "Your own buttons (changed in the table).";
  }
  switch (*preset) {
  case ec::PadPreset::GameCubeAdapter:
    return "A GameCube controller through Nintendo's adapter (WUP-028) or a Mayflash adapter in Wii U mode: every "
           "button where it is on the controller, L and R with their click. (A Mayflash in PC mode reports itself "
           "as an ordinary controller and already maps A, B, X, Y right with Auto.)";
  case ec::PadPreset::Xbox:
    return "Xbox: GameCube buttons by place: A is A, B is X (left), X is B (right), Y is Y; Z is RB; L and R are "
           "the triggers.";
  case ec::PadPreset::PlayStation:
    return "PlayStation: A is Cross, B is Circle, X is Square, Y is Triangle; Z is R1; L and R are L2 and R2.";
  case ec::PadPreset::SwitchPro:
    return "Switch Pro: by the letters printed on it, as Nintendo's GameCube games on Switch do: A is A, B is B, X "
           "is X, Y is Y; Z is R; L and R are ZL and ZR.";
  default: return "";
  }
}

bool ChoiceButton(const char* label, bool active, const Options& o) {
  if (active) {
    const ImVec4 a = Col(o.colors.accent);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(a.x, a.y, a.z, 0.55f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(a.x, a.y, a.z, 0.70f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(a.x, a.y, a.z, 0.85f));
  }
  const bool clicked = ImGui::Button(label);
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
    const std::string label = "Controller " + std::to_string(p + 1);
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
  const std::string controller = "controller " + std::to_string(port + 1);

  std::vector<ec::ConnectedPad> pads;
  for (const Pad& p : pads_) {
    pads.push_back({p.guid, p.name});
  }
  const auto padLabel = [&](size_t i) {
    std::string s = pads_[i].name.empty() ? "Controller" : pads_[i].name;
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
  std::string preview = choice.mode == ec::ControllerChoice::Mode::Auto   ? "Automatic: the first free controller"
                        : choice.mode == ec::ControllerChoice::Mode::None ? "None: keyboard only"
                        : pinnedIndex >= 0                                ? padLabel(static_cast<size_t>(pinnedIndex))
                                                                          : (choice.name.empty() ? std::string("A controller")
                                                                                                 : choice.name) +
                                                                                " (not plugged in)";
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted("PC controller");
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
    if (ImGui::Selectable("Automatic: the first free controller", choice.mode == ec::ControllerChoice::Mode::Auto)) {
      store(ec::ControllerChoice::Auto());
    }
    if (ImGui::Selectable("None: keyboard only", choice.mode == ec::ControllerChoice::Mode::None)) {
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
    Wrapped(options.colors.dim, "Plays now: " + padLabel(static_cast<size_t>(assigned)) + " (it says it is " +
                                    DeviceName(pad.device) + ").");
  } else if (choice.mode == ec::ControllerChoice::Mode::None) {
    Wrapped(options.colors.dim, "No PC controller: " + controller + " is played with the keyboard only" +
                                    (port == 0 ? "." : " (if you give it keys below)."));
  } else {
    Wrapped(options.colors.dim, pads_.empty() ? std::string("No PC controller is plugged in. Plug one in: it shows "
                                                            "up here at once.")
                                              : "No free PC controller for it right now.");
  }
  if (pads_.size() > 0) {
    std::string list = "Plugged in:";
    for (size_t i = 0; i < pads_.size(); i++) {
      list += (i == 0 ? " " : ", ") + padLabel(i);
    }
    Wrapped(options.colors.faint, list);
  }

  ImGui::Spacing();
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted("Controller preset");
  const std::optional<ec::PadPreset> preset = ec::PadPresetOf(map.gamepad);
  const bool autoKind = !map.gamepad.kind && map.gamepad.layout == ec::Layout::Position &&
                        ec::UsesDefaultButtons(map.gamepad);
  ImGui::SameLine();
  if (ChoiceButton("Auto", autoKind, options)) {
    ec::GamepadMap g = map.gamepad;
    g.kind.reset();
    g.layout = ec::Layout::Position;
    for (auto& b : g.buttons) {
      b.reset();
    }
    map.gamepad = g;
    changed |= ec::WritePort(registry, port, map);
  }
  for (int p = 0; p < static_cast<int>(ec::PadPreset::Count); p++) {
    const ec::PadPreset pp = static_cast<ec::PadPreset>(p);
    ImGui::SameLine();
    if (ChoiceButton(ec::PadPresetName(pp), preset == pp, options)) {
      map.gamepad = ec::ApplyPadPreset(map.gamepad, pp);
      changed |= ec::WritePort(registry, port, map);
    }
  }
  Wrapped(options.colors.dim, PresetMeaning(preset, autoKind));

  ImGui::Spacing();
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted("Keyboard");
  ImGui::SameLine();
  const bool standardKeys = map.keyboard == ec::DefaultKeyboard(port);
  if (ChoiceButton(port == 0 ? "Keyboard default" : "No keys (default)", standardKeys, options)) {
    map.keyboard = ec::DefaultKeyboard(port);
    changed |= ec::WritePort(registry, port, map);
  }
  if (port != 0) {
    ImGui::SameLine();
    if (ImGui::Button("Copy controller 1's keys")) {
      map.keyboard = ec::ReadPort(registry, 0).keyboard;
      changed |= ec::WritePort(registry, port, map);
    }
  }
  Wrapped(options.colors.dim, port == 0 ? "Keyboard default: Dolphin's keys. X = A, Z = B, C = X, S = Y, D = Z, Q = L, "
                                          "W = R, Enter = Start, arrow keys = main stick, I J K L = C-stick, "
                                          "T F G H = D-pad."
                                        : "Controllers 2-4 have no keys unless you give them some (all keyboards are "
                                          "one keyboard: a key used twice presses both).");

  ImGui::Spacing();
  bool rumble = map.gamepad.rumble;
  if (ImGui::Checkbox("Rumble", &rumble)) {
    map.gamepad.rumble = rumble;
    changed |= ec::WritePort(registry, port, map);
  }
  ImGui::SameLine();
  const bool canTest = assigned >= 0 && map.gamepad.rumble;
  if (!canTest) {
    ImGui::BeginDisabled();
  }
  if (ImGui::Button("Test rumble")) {
    rumbleRequest_ = true;
  }
  if (!canTest) {
    ImGui::EndDisabled();
  }
  Wrapped(options.colors.dim, "When the game rumbles the controller (the game's own Rumble option in its menu still "
                              "decides when). Off: the controller never rumbles.");
  return changed;
}

bool ControlsPanel::DrawMapping(es::Registry& registry, const Options& options) {
  bool changed = false;
  const int port = port_;
  ec::PortMap map = ec::ReadPort(registry, port);
  const ec::Device device = Device(registry);

  const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_PadOuterX;
  Wrapped(options.colors.dim, "Click a key or button to change it, then press the new one (Esc: cancel). \"...\": "
                              "add a second one, none, or the standard one. Violet: changed from the standard.");
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
    if (ImGui::Button(listening ? listenText : text.c_str(), ImVec2(width, 0))) {
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
      if (!walk && ImGui::Selectable(keyboard ? "Add another key" : "Add another button")) {
        listen_ = {true, keyboard, true, false, port_, g, action};
      }
      if (ImGui::Selectable(keyboard ? "No key" : "No button")) {
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
      if (!walk && ImGui::Selectable(keyboard ? "Standard key" : "Standard button")) {
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
    ImGui::TableSetupColumn("GameCube", ImGuiTableColumnFlags_WidthStretch, 0.8f);
    ImGui::TableSetupColumn("Keyboard", ImGuiTableColumnFlags_WidthStretch, 1.0f);
    ImGui::TableSetupColumn("PC controller", ImGuiTableColumnFlags_WidthStretch, 1.2f);
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
      ImGui::TextUnformatted(walkRow ? "Walk (hold)" : ec::GcInputName(input));

      ImGui::TableSetColumnIndex(1);
      ImGui::PushID("k");
      if (walkRow) {
        cell(true, g, true, -1,
             map.keyboard.walk.kind == ec::Input::Kind::None ? "none"
                                                             : ec::InputLabel(map.keyboard.walk, ec::Device::Keyboard).text,
             map.keyboard.walk.kind != ec::Input::Kind::None, listeningTo(true, g, true), "Press a key...");
      } else {
        cell(true, g, false, -1, Join(map.keyboard.keys[g], ec::Device::Keyboard),
             map.keyboard.keys[g] != ec::DefaultKeyboard(port).keys[g], listeningTo(true, g, false), "Press a key...");
      }
      ImGui::PopID();

      ImGui::TableSetColumnIndex(2);
      if (!walkRow) {
        ImGui::PushID("p");
        cell(false, g, false, -1, Join(ec::PadInputs(map.gamepad, input, device), device), map.gamepad.buttons[g].has_value(),
             listeningTo(false, g, false), "Press a button...");
        ImGui::PopID();
      } else {
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(Col(options.colors.faint), "(keyboard only: arrow keys tilt part way)");
      }
      ImGui::PopID();
    }
    const ec::ActionMap defActions = ec::DefaultActions(port);
    for (size_t a = 0; a < ec::kActionCount; a++) {
      ImGui::PushID(static_cast<int>(1000 + a));
      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      ImGui::AlignTextToFramePadding();
      ImGui::TextUnformatted(ec::ActionName(static_cast<ec::Action>(a)));
      ImGui::TableSetColumnIndex(1);
      ImGui::PushID("k");
      cell(true, 0, false, static_cast<int>(a), Join(map.actions.keys[a], ec::Device::Keyboard),
           map.actions.keys[a] != defActions.keys[a], listeningToAction(true, a), "Press a key...");
      ImGui::PopID();
      ImGui::TableSetColumnIndex(2);
      ImGui::PushID("p");
      cell(false, 0, false, static_cast<int>(a), Join(map.actions.pad[a], device), map.actions.pad[a] != defActions.pad[a],
           listeningToAction(false, a), "Press a button...");
      ImGui::PopID();
      ImGui::PopID();
    }
    ImGui::EndTable();
  }
  if (ec::kActionCount > 0) {
    Wrapped(options.colors.faint, "Fast-forward is the PC version's own key, not a GameCube button: it works only while "
                                  "the Fast-forward setting is on (Settings, Gameplay; never in Original).");
  }

  ImGui::Spacing();
  if (ImGui::Button("Reset this controller to the defaults")) {
    ec::PortMap def = ec::DefaultPort(port);
    def.controller = map.controller;
    changed |= ec::WritePort(registry, port, def);
    listen_ = {};
  }
  ImGui::SameLine();
  Wrapped(options.colors.dim, "Keys, buttons, preset, deadzones, click points and rumble as they come.");
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

  if (ChoiceButton(testing_ ? "Stop the keyboard test (Esc)" : "Try the keyboard", testing_, options)) {
    testing_ = !testing_;
    listen_ = {};
  }
  ImGui::SameLine();
  Wrapped(options.colors.dim,
          testing_ ? "The keyboard plays controller " + std::to_string(port_ + 1) +
                         " here now (keys don't work on this page meanwhile; Esc ends it)."
                   : std::string(pad >= 0 ? "Move the sticks and press buttons: this is what the game reads. "
                                          : "No PC controller plays as this one. ") +
                         "Click \"Try the keyboard\" to try the keys.");

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
    std::snprintf(text, sizeof(text), "%s  %d%s", name, value, click ? "  click" : "");
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
        pad >= 0 ? map.gamepad.leftStickDeadzone : 0.f, "Main stick");
  stick(ImVec2(o.x + w * 0.62f, o.y + 150 * u), sr * 0.72f, 0xF2C230, r.substickX, r.substickY,
        pad >= 0 ? map.gamepad.rightStickDeadzone : 0.f, "C-stick");
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
    label(ImVec2(c.x, c.y + g + 20 * u), "D-pad", dim);
  }
  button(ImVec2(o.x + w * 0.47f, o.y + 100 * u), 10 * u, 0xC8C8D0, "", on(0x1000));
  label(ImVec2(o.x + w * 0.47f, o.y + 122 * u), "Start", dim);
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
    const char* name;
  } kNames[] = {{0x0100, "A"},  {0x0200, "B"},  {0x0400, "X"},     {0x0800, "Y"},         {0x0010, "Z"},
                {0x0040, "L"},  {0x0020, "R"},  {0x1000, "Start"}, {0x0008, "D-pad up"}, {0x0004, "D-pad down"},
                {0x0001, "D-pad left"}, {0x0002, "D-pad right"}};
  for (const auto& n : kNames) {
    if (on(n.bit)) {
      pressed += (pressed.empty() ? "" : ", ") + std::string(n.name);
    }
  }
  char line[160];
  std::snprintf(line, sizeof(line), "Game reads: main stick %d, %d   C-stick %d, %d   L %d   R %d   buttons: %s", r.stickX,
                r.stickY, r.substickX, r.substickY, r.triggerLeft, r.triggerRight, pressed.empty() ? "none" : pressed.c_str());
  Wrapped(options.colors.dim, line);
}

bool ControlsPanel::Draw(es::Registry& registry, int port, const Options& options) {
  SetPort(port);
  bool changed = false;
  changed |= DrawController(registry, options);
  ImGui::Spacing();
  changed |= DrawMapping(registry, options);
  ImGui::Spacing();
  Heading(options, "Sticks and triggers");
  changed |= DrawSticks(registry, options);
  ImGui::Spacing();
  Heading(options, "Try it");
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
