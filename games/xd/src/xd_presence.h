#ifndef XD_PRESENCE_H
#define XD_PRESENCE_H

#include "xd_test_run.h"

#include <cstdlib>
#include <string>

namespace xd::presence_info {

inline constexpr const char* kClientId = "1572149104006529045";
inline constexpr const char* kLargeImage = "logo";

inline std::string TestEndpoint() {
  const char* e = std::getenv("XD_DISCORD_TEST");
  return e != nullptr ? e : "";
}

inline bool Allowed() { return !TestEndpoint().empty() || !xd::TestRun(); }

}

#endif
