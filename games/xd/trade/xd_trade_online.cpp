#include <xd_trade_online.h>

#include <gc_memcard.h>

#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>

namespace fs = std::filesystem;
namespace mc = engine::memcard;

namespace xd::trade {

namespace {

std::vector<uint8_t> FingerprintOf(bool xg) { return {'X', static_cast<uint8_t>(xg ? 'G' : 'D'), 'T', '1'}; }
bool IsEditionFingerprint(const std::vector<uint8_t>& f) { return f.size() == 4 && f[0] == 'X' && (f[1] == 'D' || f[1] == 'G') && f[2] == 'T' && f[3] == '1'; }

bool ReadBytes(const fs::path& p, Bytes& out) {
  std::ifstream in(p, std::ios::binary);
  if (!in) return false;
  out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  return true;
}
bool WriteBytes(const fs::path& p, const Bytes& b) {
  std::ofstream out(p, std::ios::binary | std::ios::trunc);
  if (!out) return false;
  out.write(reinterpret_cast<const char*>(b.data()), static_cast<std::streamsize>(b.size()));
  out.close();
  return static_cast<bool>(out);
}
std::string Stamp() {
  std::time_t t = std::time(nullptr);
  char buf[32];
  std::tm tmv{};
#ifdef _WIN32
  localtime_s(&tmv, &t);
#else
  localtime_r(&t, &tmv);
#endif
  std::strftime(buf, sizeof(buf), "%Y%m%d_%H%M%S", &tmv);
  return buf;
}

}

bool LoadCardSave(const fs::path& folderOrFile, CardSave& out, std::string& error) {
  std::error_code ec;
  std::vector<fs::path> candidates;
  if (fs::is_directory(folderOrFile, ec)) {
    for (const fs::path& sub : {folderOrFile / "USA" / "Card A", folderOrFile})
      for (const auto& e : fs::directory_iterator(sub, ec))
        if (e.is_regular_file(ec) && e.path().extension() == ".gci") candidates.push_back(e.path());
  } else {
    candidates.push_back(folderOrFile);
  }
  for (const fs::path& f : candidates) {
    Bytes bytes;
    mc::CardFile card;
    std::string why;
    if (!ReadBytes(f, bytes) || !mc::ReadGci(bytes, card, why)) continue;
    if (card.entry.fileName != "PokemonXD" || card.entry.gameCode != "GXXE") continue;
    if (card.data.size() < xd::save::kFileSize) continue;
    out.file = f;
    out.cardRoot = fs::is_directory(folderOrFile, ec) ? folderOrFile : f.parent_path();
    out.data.assign(card.data.begin(), card.data.begin() + static_cast<std::ptrdiff_t>(xd::save::kFileSize));
    out.gci = std::move(bytes);
    return true;
  }
  error = "There is no Pokemon XD save here yet. Play and save once first.";
  return false;
}

bool WriteCardSave(const CardSave& save, const Bytes& newData, std::string& error) {
  if (newData.size() != xd::save::kFileSize) { error = "The new save has the wrong size."; return false; }
  mc::CardFile card;
  std::string why;
  if (!mc::ReadGci(save.gci, card, why)) { error = "The old save file can't be read again: " + why; return false; }
  std::copy(newData.begin(), newData.end(), card.data.begin());
  std::error_code ec;
  const fs::path backup = save.cardRoot / "save_backups" / ("online_trade_" + Stamp());
  fs::create_directories(backup, ec);
  if (ec || !fs::copy_file(save.file, backup / save.file.filename(), fs::copy_options::overwrite_existing, ec)) {
    error = "The backup copy of your save could not be made, so nothing was changed.";
    return false;
  }
  fs::path tmp = save.file;
  tmp += ".trade.tmp";
  if (!WriteBytes(tmp, mc::MakeGci(card))) {
    fs::remove(tmp, ec);
    error = "The new save could not be written.";
    return false;
  }
  fs::rename(tmp, save.file, ec);
  if (ec) {
    fs::remove(tmp, ec);
    error = "The save could not be replaced.";
    return false;
  }
  return true;
}

OnlineTrade::OnlineTrade(Trader& trader, Writer writer, bool useDefaultNet)
    : trader_(trader), writer_(std::move(writer)), cfg_(xdnet::Connection::defaultConfig()), useDefault_(useDefaultNet) {}

OnlineTrade::~OnlineTrade() = default;

std::string OnlineTrade::StartHost() {
  const std::string code = xdnet::newRoomCode();
  Begin(code);
  return code_;
}

bool OnlineTrade::StartJoin(const std::string& typed) {
  Begin(typed);
  return phase_ == Phase::Connecting;
}

void OnlineTrade::Begin(const std::string& code) {
  entries_ = trader_.List();
  offered_ = -1;
  refusal_.clear();
  session_.reset();
  conn_ = std::make_unique<xdnet::Connection>();
  xdnet::ConnectionConfig cfg = useDefault_ ? xdnet::Connection::defaultConfig() : cfg_;
  cfg.session.fingerprint = FingerprintOf(xg_);
  if (!conn_->start(code, cfg)) {
    phase_ = Phase::Over;
    code_ = code;
    return;
  }
  code_ = conn_->status().roomCode.empty() ? xdnet::formatRoomCode(xdnet::normalizeRoomCode(code)) : conn_->status().roomCode;
  phase_ = Phase::Connecting;
}

std::string OnlineTrade::connectionText() const {
  if (!refusal_.empty()) return refusal_;
  if (!conn_) return "";
  const xdnet::ConnectionStatus s = conn_->status();
  std::string t = s.text;
  if (!s.detail.empty()) t += " " + s.detail;
  return t;
}

void OnlineTrade::Update() {
  if (!conn_ || phase_ == Phase::Idle) return;
  const uint64_t now = xdnet::nowMs();
  const xdnet::ConnectionStatus st = conn_->status();
  if (phase_ == Phase::Connecting) {
    if (st.phase == xdnet::Phase::Connected) {
      if (st.peerFingerprint != FingerprintOf(xg_)) {
        if (IsEditionFingerprint(st.peerFingerprint)) {
          refusal_ = xg_ ? "Your friend plays Pokemon XD; you play Pokemon XG. XD and XG players cannot trade with each other."
                         : "Your friend plays Pokemon XG; you play Pokemon XD. XD and XG players cannot trade with each other.";
        }
        Log("[trade] the friend is not in the trade screen (other fingerprint)");
        conn_->close();
        phase_ = Phase::Over;
        return;
      }
      xdnet::trade::Hooks h;
      h.check = [this](const Bytes& w) { return trader_.CheckIncoming(w); };
      h.prepare = [this](const Bytes& give, const Bytes& get) { return trader_.Prepare(give, get); };
      h.commit = [this](const Bytes&, const Bytes&) { return trader_.Commit(writer_); };
      session_ = std::make_unique<xdnet::trade::TradeSession>(st.low, h);
      phase_ = Phase::Trading;
      Log("[trade] connected; trade session started");
    } else if (st.phase == xdnet::Phase::Failed || st.phase == xdnet::Phase::Closed) {
      Log("[trade] connection ended: " + st.text);
      phase_ = Phase::Over;
      return;
    }
  }
  if (phase_ == Phase::Trading || phase_ == Phase::Over) {
    if (!session_) return;
    session_->tick(now);
    std::vector<uint8_t> m;
    while (conn_->receive(m)) session_->onMessage(m);
    if ((st.phase == xdnet::Phase::Closed || st.phase == xdnet::Phase::Failed)) session_->onDisconnected();
    for (auto& out : session_->takeOutgoing()) conn_->send(out.data(), out.size(), true);
    const auto state = session_->state();
    if (phase_ == Phase::Trading && (state == xdnet::trade::State::Done || state == xdnet::trade::State::Ended)) {
      phase_ = Phase::Over;
      overAt_ = now;
      Log("[trade] over: " + session_->endText());
    }
    if (phase_ == Phase::Over && overAt_ != 0 && now - overAt_ > 1200) {
      conn_->close();
      overAt_ = 0;
    }
  }
}

void OnlineTrade::Leave() {
  if (session_) {
    session_->cancel();
    for (auto& out : session_->takeOutgoing()) if (conn_) conn_->send(out.data(), out.size(), true);
  }
  if (conn_) conn_->close();
  if (phase_ != Phase::Over) phase_ = Phase::Over;
}

bool OnlineTrade::Offer(int i) {
  if (!session_ || i < 0 || i >= static_cast<int>(entries_.size()) || !entries_[static_cast<size_t>(i)].ok) return false;
  if (!session_->offer(entries_[static_cast<size_t>(i)].wire)) return false;
  offered_ = i;
  return true;
}
bool OnlineTrade::Withdraw() {
  if (!session_ || !session_->withdraw()) return false;
  offered_ = -1;
  return true;
}
bool OnlineTrade::Ready(bool r) { return session_ && session_->setReady(r); }
bool OnlineTrade::Answer(bool yes) { return session_ && session_->confirm(yes); }

Card OnlineTrade::friendCard() const {
  if (!session_ || session_->theirs().record.empty()) return Card{};
  return trader_.DescribeWire(session_->theirs().record);
}

}
