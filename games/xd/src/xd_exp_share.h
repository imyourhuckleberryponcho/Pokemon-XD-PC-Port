#ifndef XD_EXP_SHARE_H
#define XD_EXP_SHARE_H

#include <string>
#include <vector>

namespace xd::expshare {

inline constexpr const char* kOption = "exp_share_all=on";

std::vector<std::string> OptionsOf(bool on);
bool OnIn(const std::vector<std::string>& options);

std::vector<std::string> TestSwitch();

void SetActive(const std::vector<std::string>& options, const char* why);
bool Active();

}

#endif
