#include "discord_presence.h"

#include "i18n.h"
#include "online_battle.h"
#include "presence.h"
#include "settings.h"
#include "xd_battles_won.h"
#include "xd_challenges.h"
#include "xd_languages.h"
#include "xd_playthrough.h"
#include "xd_presence.h"
#include "xd_randomizer.h"
#include "xd_speedrun.h"
#include "xd_xg.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <memory>
#include <string>

int PortXD_OnlineScreenKind(void);

using engine::i18n::Tr;

namespace xd::discord {

namespace {
namespace es = engine::settings;
using Clock = std::chrono::steady_clock;

std::unique_ptr<engine::presence::Client> sClient;
Clock::time_point sLast{};
int64_t sStart = 0;
bool sAtExit = false;
bool sAllowed = true;
bool sLog = false;

bool SettingOn() {
  const es::Registry& reg = es::Global();
  const size_t i = reg.Find("system.discord_presence");
  return i != es::kNoSetting && reg.ValueOf(i).i != 0;
}

engine::presence::Activity Current() {
  engine::presence::Activity a;
  const bool inFight = xd::battles::InFight();
  const int screen = PortXD_OnlineScreenKind();
  if (screen == 2) {
    a.details = Tr("Online trade");
  } else if (screen == 1 || (inFight && xd::online::LobbyLink() != nullptr)) {
    a.details = Tr("Online battle");
  } else if (inFight) {
    a.details = Tr("In a battle");
  } else if (xd::speedrun::PlaythroughChosen()) {
    a.details = Tr("Story mode");
  } else {
    a.details = Tr("On the title screen");
  }
  const bool xg = xd::xg::On();
  a.state = xg ? "Pokemon XG" : "Pokemon XD";
  const xd::languages::Language* lang = xd::languages::Active();
  if (lang != nullptr && std::string(lang->id) == xd::languages::kJpId) a.state += " \xC2\xB7 " + std::string(Tr("Japanese"));
  if (!xd::pt::LoadedRandomizer().empty()) a.state += " \xC2\xB7 " + std::string(Tr("Randomizer"));
  if (xd::challenges::Active("nuzlocke_faint") || xd::challenges::Active("nuzlocke_one_per_area")) {
    a.state += " \xC2\xB7 " + std::string(Tr("Nuzlocke"));
  }
  a.largeImage = xd::presence_info::kLargeImage;
  a.largeText = xg ? "Pokemon XG (PC port)" : "Pokemon XD: Gale of Darkness (PC port)";
  a.startSeconds = sStart;
  return a;
}
}

void Shutdown() {
  if (sClient) {
    sClient->Stop();
    sClient.reset();
  }
}

void Frame() {
  const auto now = Clock::now();
  if (sLast != Clock::time_point{} && now - sLast < std::chrono::seconds(1)) return;
  sLast = now;
  static bool first = true;
  if (first) {
    first = false;
    sAllowed = xd::presence_info::Allowed();
    sStart = static_cast<int64_t>(std::time(nullptr));
    const char* l = std::getenv("XD_DISCORD_LOG");
    sLog = l != nullptr && l[0] == '1';
    if (!sAllowed) std::fprintf(stderr, "[presence] off: this is a test run\n");
  }
  if (!sAllowed) return;
  const bool want = SettingOn();
  if (want && !sClient) {
    engine::presence::Config cfg;
    cfg.clientId = xd::presence_info::kClientId;
    cfg.endpointOverride = xd::presence_info::TestEndpoint();
    cfg.log = sLog;
    if (!cfg.endpointOverride.empty()) {
      cfg.minIntervalMs = 1000;
      cfg.retryIntervalMs = 1000;
    }
    cfg.stopWaitMs = 1500;
    sClient = std::make_unique<engine::presence::Client>();
    sClient->Start(cfg);
    if (!sAtExit) {
      sAtExit = true;
      std::atexit([] { Shutdown(); });
    }
  } else if (!want && sClient) {
    Shutdown();
  }
  if (sClient) sClient->Set(Current());
}

}
