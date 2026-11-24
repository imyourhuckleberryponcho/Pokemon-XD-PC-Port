#ifndef XD_SAVEEDIT_PAGE_H
#define XD_SAVEEDIT_PAGE_H

#include <launcher.h>

#include <filesystem>
#include <functional>

namespace xd::saveeditpage {

void Install(launcher::GameInfo& game, std::function<std::filesystem::path(const std::filesystem::path& cardRoot)> saveFileOf,
             std::function<bool(const std::filesystem::path& cardRoot)> cardIsFree);

}

#endif
