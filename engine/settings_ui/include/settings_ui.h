#ifndef ENGINE_SETTINGS_UI_H
#define ENGINE_SETTINGS_UI_H

#include <controls.h>
#include <settings.h>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace engine::settings_ui {

struct Colors {
  float accent[4] = {0.70f, 0.55f, 1.00f, 1.00f};
  float dim[4] = {0.68f, 0.70f, 0.78f, 1.00f};
  float faint[4] = {0.50f, 0.52f, 0.60f, 1.00f};
  float bad[4] = {1.00f, 0.45f, 0.40f, 1.00f};
};

struct Options {
  std::string gameId;
  std::vector<std::string> hidden;
  std::vector<std::string> hiddenPrefixes;
  Colors colors;
  void* headingFont = nullptr;
};

std::vector<size_t> VisibleSettings(const settings::Registry& registry, settings::Category category,
                                    const Options& options);

const char* PresetName(settings::Preset preset);
const char* PresetExplanation(settings::Preset preset);

std::string ValueForPeople(const settings::Registry& registry, size_t index, const settings::Value& value);

bool DrawPresetChooser(settings::Registry& registry, const Options& options);

bool DrawCategory(settings::Registry& registry, settings::Category category, const Options& options);

bool DrawSettingRow(settings::Registry& registry, size_t index, const Options& options);

void ResetCategory(settings::Registry& registry, settings::Category category, const Options& options);

void ResetToPreset(settings::Registry& registry);

class ControlsPanel {
 public:
  struct Pad {
    std::string guid;
    std::string name;
    controls::Device device = controls::Device::Generic;
  };
  void SetPads(std::vector<Pad> pads) { pads_ = std::move(pads); }
  const std::vector<Pad>& Pads() const { return pads_; }
  int AssignedPad(const settings::Registry& registry, int port) const;

  int Port() const { return port_; }
  void SetPort(int port) { port_ = port < 0 ? 0 : port > 3 ? 3 : port; }

  void SetLiveInputs(const controls::RawInputs& raw) { raw_ = raw; }
  bool Testing() const { return testing_; }
  void SetTesting(bool on) { testing_ = on; }
  bool TakeRumbleRequest() {
    const bool r = rumbleRequest_;
    rumbleRequest_ = false;
    return r;
  }

  bool Draw(settings::Registry& registry, int port, const Options& options);
  bool DrawPortTabs(settings::Registry& registry, const Options& options);
  bool DrawController(settings::Registry& registry, const Options& options);
  bool DrawMapping(settings::Registry& registry, const Options& options);
  bool DrawSticks(settings::Registry& registry, const Options& options);
  void DrawLiveTest(const settings::Registry& registry, const Options& options);
  controls::PadReading LiveReading(const settings::Registry& registry) const;

  bool Listening() const { return listen_.active; }
  void FeedKey(settings::Registry& registry, uint16_t scancode);
  void FeedPadButton(settings::Registry& registry, int button);
  void FeedPadAxis(settings::Registry& registry, int axis, float value);
  void Cancel() {
    listen_ = {};
    testing_ = false;
  }
  void StartListening(int port, size_t input, bool keyboard, bool append) {
    listen_ = {true, keyboard, append, false, port, input};
  }
  void StartListeningAction(int port, size_t action, bool keyboard, bool append) {
    listen_ = {true, keyboard, append, false, port, 0, static_cast<int>(action)};
  }

  void SetDevice(controls::Device device) { device_ = device; }
  controls::Device Device(const settings::Registry& registry) const;

 private:
  struct Listen {
    bool active = false;
    bool keyboard = true;
    bool append = false;
    bool walk = false;
    int port = 1;
    size_t input = 0;
    int action = -1;
  };
  Listen listen_;
  controls::Device device_ = controls::Device::Generic;
  std::vector<Pad> pads_;
  controls::RawInputs raw_;
  int port_ = 0;
  int shownTab_ = 0;
  bool testing_ = false;
  bool rumbleRequest_ = false;
  void Store(settings::Registry& registry, const controls::Input& input);
  controls::Device DetectedDevice(const settings::Registry& registry) const;
};

}

#endif
