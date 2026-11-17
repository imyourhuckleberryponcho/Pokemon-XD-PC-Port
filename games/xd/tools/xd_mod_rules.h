#ifndef XD_MOD_RULES_H
#define XD_MOD_RULES_H

#include <modcheck.h>

#include <string>

namespace xd::modrules {

engine::modcheck::GameRules Rules();

bool OpenDisc(const std::string& discPath, engine::mods::LoadOptions& load, std::string& error);
void CloseDisc();

}

#endif
