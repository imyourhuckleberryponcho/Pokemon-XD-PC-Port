#ifndef XD_MODS_RELOAD_H
#define XD_MODS_RELOAD_H

#include <string>
#include <vector>

#include "xd_hd_packs.h"

namespace engine::mods {
struct LoadOptions;
struct LoadResult;
}

namespace xd::modreload {

void Remember(const engine::mods::LoadOptions& options, const engine::mods::LoadResult& result);
std::vector<xd::hd::Pack> TexturePacksOf(const engine::mods::LoadResult& result, const engine::mods::LoadOptions& options);

struct Outcome {
  bool ran = false;
  bool scripts = false;
  bool textures = false;
  bool restart = false;
  std::string restartWhat;
  std::string notice;
  std::string waiting;
};

bool Enabled();
bool QuietNow();
Outcome ReloadNow(const char* why);
void Request(const char* why);

}

#endif
