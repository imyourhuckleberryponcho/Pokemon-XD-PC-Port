#ifndef XD_MOD_ASSETS_H
#define XD_MOD_ASSETS_H

#include "pack_match.h"

#include "i18n.h"
#include <mods.h>

#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace xd::modassets {

struct Context {
  std::filesystem::path assets;
  bool playthrough = false;
  bool gen4On = false, gen57On = false;
};

inline std::string Reason(const std::string& id, const Context& c) {
  using engine::i18n::Tr;
  switch (xd::packmatch::StateOfAsset(id, c.assets)) {
    case xd::packmatch::AssetState::NotMade: return Tr("not made yet");
    case xd::packmatch::AssetState::OtherVersion: return Tr("made by another version of the game, make it again");
    case xd::packmatch::AssetState::DoesNotFit: return Tr("does not fit the Gen 4 files that are installed, make it again");
    case xd::packmatch::AssetState::UnknownId: return Tr("not known to this game");
    case xd::packmatch::AssetState::Ready: break;
  }
  if (c.playthrough && id == "gen4_pack" && !c.gen4On) return Tr("this playthrough was set up without Gen 4 Pokemon");
  if (c.playthrough && id == "gen57_pack" && !c.gen57On) return Tr("this playthrough was set up without Gen 5-7 Pokemon");
  return std::string();
}

inline std::vector<engine::mods::LoadResult::AssetBlock>& Blocked() {
  static std::vector<engine::mods::LoadResult::AssetBlock> blocked;
  return blocked;
}
inline std::map<std::string, std::string>& NeededBy() {
  static std::map<std::string, std::string> needed;
  return needed;
}

inline engine::mods::AssetCheck Checker(const Context& c) {
  return [c](const std::string& id) { return Reason(id, c); };
}

}

#endif
