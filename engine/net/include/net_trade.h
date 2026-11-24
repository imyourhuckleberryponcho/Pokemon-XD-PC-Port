#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace xdnet::trade {

constexpr uint8_t kVersion = 2;
constexpr size_t kMaxRecord = 1000;

struct Verdict {
  bool ok = true;
  std::string text;
};

struct Hooks {
  std::function<Verdict(const std::vector<uint8_t>& theirs)> check;
  std::function<Verdict(const std::vector<uint8_t>& mine, const std::vector<uint8_t>& theirs)> prepare;
  std::function<Verdict(const std::vector<uint8_t>& mine, const std::vector<uint8_t>& theirs)> commit;
};

enum class State {
  Negotiating,
  Confirming,
  Committing,
  Done,
  Ended,
};
enum class Outcome {
  None,
  Traded,
  NothingChanged,
  Unknown,
  CommitFailed,
};

struct Offer {
  uint32_t seq = 0;
  std::vector<uint8_t> record;
};

class TradeSession {
 public:
  TradeSession(bool low, Hooks hooks);

  bool offer(const std::vector<uint8_t>& record);
  bool withdraw();
  bool setReady(bool ready);
  bool confirm(bool yes);
  void cancel();

  std::vector<std::vector<uint8_t>> takeOutgoing();
  void onMessage(const std::vector<uint8_t>& message);
  void onDisconnected();
  void tick(uint64_t nowMs);

  State state() const { return state_; }
  Outcome outcome() const { return outcome_; }
  const Offer& mine() const { return mine_; }
  const Offer& theirs() const { return theirs_; }
  const Verdict& theirsVerdict() const { return theirsVerdict_; }
  bool myOfferJudged() const { return friendVerdictSeq_ == mine_.seq && mine_.seq != 0; }
  const Verdict& myOfferVerdict() const { return friendVerdict_; }
  bool iAmReady() const;
  bool friendIsReady() const;
  bool iConfirmed() const { return state_ >= State::Confirming && myYes_; }
  bool friendConfirmed() const { return state_ >= State::Confirming && peerYes_; }
  bool canReady() const;
  std::string status() const;
  const std::string& endText() const { return endText_; }
  const std::string& why() const { return why_; }

 private:
  struct Pair { uint32_t own = 0, other = 0; };
  void send(uint8_t type, const std::vector<uint8_t>& payload);
  void sendReady(bool ready);
  void advance();
  void end(Outcome o, const std::string& text);
  uint64_t digest() const;
  bool pairMatches(const Pair& mineSent, const Pair& peerSent) const;

  bool low_;
  Hooks hooks_;
  State state_ = State::Negotiating;
  Outcome outcome_ = Outcome::None;
  Offer mine_, theirs_;
  uint32_t nextSeq_ = 1;
  Verdict theirsVerdict_;
  Verdict friendVerdict_;
  uint32_t friendVerdictSeq_ = 0;
  Pair myReady_, peerReady_;
  uint64_t peerYesDigest_ = 0;
  Pair peerYesPair_;
  bool myYes_ = false, peerYes_ = false;
  bool sentAck_ = false, peerAck_ = false;
  std::vector<std::vector<uint8_t>> out_;
  std::string why_, endText_;
  uint64_t lastProgressMs_ = 0, nowMs_ = 0;
};

}
