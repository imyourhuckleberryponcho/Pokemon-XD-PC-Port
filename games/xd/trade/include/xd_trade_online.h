#ifndef XD_TRADE_ONLINE_H
#define XD_TRADE_ONLINE_H

#include <net_connection.h>
#include <net_crypto.h>
#include <net_trade.h>
#include <xd_trade.h>

#include <filesystem>
#include <functional>
#include <memory>
#include <string>

namespace xd::trade {

enum class Phase {
  Idle,
  Connecting,
  Trading,
  Over,
};

using Writer = std::function<bool(const Bytes& newFile, std::string& error)>;

class OnlineTrade {
 public:
  OnlineTrade(Trader& trader, Writer writer, bool useDefaultNet = true);
  ~OnlineTrade();
  void SetNetConfig(const xdnet::ConnectionConfig& cfg) { cfg_ = cfg; useDefault_ = false; }
  void SetEdition(bool xg) { xg_ = xg; }
  void SetLog(std::function<void(const std::string&)> log) { log_ = std::move(log); }

  std::string StartHost();
  bool StartJoin(const std::string& typed);
  void Update();
  void Leave();

  Phase phase() const { return phase_; }
  xdnet::ConnectionStatus connection() const { return conn_ ? conn_->status() : xdnet::ConnectionStatus{}; }
  xdnet::trade::TradeSession* session() { return session_.get(); }
  const std::vector<Entry>& entries() const { return entries_; }
  const Trader& trader() const { return trader_; }
  const std::string& code() const { return code_; }
  std::string connectionText() const;
  const Result& result() const { return trader_.result(); }

  bool Offer(int entryIndex);
  bool Withdraw();
  bool Ready(bool r);
  bool Answer(bool yes);
  int offeredIndex() const { return offered_; }
  Card friendCard() const;

 private:
  void Begin(const std::string& code);
  void Log(const std::string& s) { if (log_) log_(s); }
  Trader& trader_;
  Writer writer_;
  xdnet::ConnectionConfig cfg_;
  bool useDefault_;
  bool xg_ = false;
  std::string refusal_;
  std::unique_ptr<xdnet::Connection> conn_;
  std::unique_ptr<xdnet::trade::TradeSession> session_;
  std::vector<Entry> entries_;
  std::string code_;
  Phase phase_ = Phase::Idle;
  int offered_ = -1;
  uint64_t overAt_ = 0;
  std::function<void(const std::string&)> log_;
};

struct CardSave {
  std::filesystem::path file;
  std::filesystem::path cardRoot;
  Bytes data;
  Bytes gci;
};
bool LoadCardSave(const std::filesystem::path& folderOrFile, CardSave& out, std::string& error);
bool WriteCardSave(const CardSave& save, const Bytes& newData, std::string& error);

}

#endif
