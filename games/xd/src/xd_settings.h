#ifndef XD_SETTINGS_H
#define XD_SETTINGS_H

#include <settings.h>

#include <functional>
#include <vector>
#include <string>

namespace xd_settings {

inline constexpr const char* kGameId = "xd";

void Register(engine::settings::Registry& registry);

std::vector<std::string> ApplyEnvironment(engine::settings::Registry& registry,
                                          const std::function<const char*(const char*)>& getenv);

std::string DocsIntro();

}

#endif
