#ifndef XD_PC_TRADE_H
#define XD_PC_TRADE_H

#include <xd_trade.h>

#include <array>
#include <memory>
#include <string>
#include <vector>

std::unique_ptr<xd::trade::Trader> PortXD_MakeLiveTrader(const xd::trade::Config& cfg);

struct PortXD_Owned {
  std::string nickname;
  int level = 0;
  int box = -1;
  int slot = 0;
  int nature = 0;
  std::array<uint16_t, 6> stats{};
  std::array<uint16_t, 6> evs{};
  std::array<uint8_t, 6> ivs{};
};
std::vector<PortXD_Owned> PortXD_OwnedOf(uint16_t species);

std::string PortXD_LiveMessage(uint32_t key);

bool PortXD_PcTradeSwallowInput();

bool PortXD_PcTradeAvailable();
bool PortXD_OpenOnlineTradeLive();
bool PortXD_ShowRuleMessage(const std::string& text);

#endif
