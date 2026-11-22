#ifndef XD_ONLINE_TEAM_H
#define XD_ONLINE_TEAM_H

#include "online_room.h"

#include <rulesets.h>
#include <xd_legality.h>
#include <xd_save.h>

#include <string>
#include <vector>

namespace xd::online {

constexpr size_t kMaxTeam = 6;

std::string EncodeTeam(const std::vector<xd::save::Pokemon>& team);
bool DecodeTeam(const std::string& line, std::vector<xd::save::Pokemon>& out, std::string* why);

std::vector<std::string> CheckTeam(const std::vector<xd::save::Pokemon>& team, const xd::legality::Rules& rules, const RoomRules& room);
bool ApplyFormat(RoomRules& room, std::string* error);
std::vector<std::string> CheckTeamFormat(const std::vector<xd::save::Pokemon>& team, const xd::legality::Rules& rules, const RoomRules& room);

std::vector<engine::rulesets::MemberProblem> CheckTeamMembers(const std::vector<xd::save::Pokemon>& team, const xd::legality::Rules& rules, const RoomRules& room);

}

#endif
