#include "input_record_format.h"
#include "input_anchors.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace port_input_record {
namespace {

const struct {
  const char* name;
  uint16_t bit;
} kButtons[] = {
    {"A", 0x0100},  {"B", 0x0200},    {"X", 0x0400},     {"Y", 0x0800},  {"START", 0x1000},
    {"Z", 0x0010},  {"L", 0x0040},    {"R", 0x0020},     {"UP", 0x0008}, {"DOWN", 0x0004},
    {"LEFT", 0x0001}, {"RIGHT", 0x0002},
};

std::string ButtonNames(uint16_t buttons) {
  if (buttons == 0) {
    return "-";
  }
  std::string out;
  for (const auto& b : kButtons) {
    if (buttons & b.bit) {
      out += out.empty() ? "" : "+";
      out += b.name;
      buttons &= ~b.bit;
    }
  }
  for (int bit = 0; bit < 16; bit++) {
    if (buttons & (1u << bit)) {
      char hex[8];
      std::snprintf(hex, sizeof hex, "0x%04x", 1u << bit);
      out += out.empty() ? "" : "+";
      out += hex;
    }
  }
  return out;
}

bool ParseButtons(const std::string& text, uint16_t& out) {
  out = 0;
  if (text == "-") {
    return true;
  }
  size_t start = 0;
  while (start <= text.size()) {
    size_t end = text.find('+', start);
    const std::string name = text.substr(start, end == std::string::npos ? std::string::npos : end - start);
    bool known = false;
    for (const auto& b : kButtons) {
      if (name == b.name) {
        out |= b.bit;
        known = true;
      }
    }
    if (!known) {
      char* stop = nullptr;
      const unsigned long value = std::strtoul(name.c_str(), &stop, 16);
      if (name.size() < 3 || name.compare(0, 2, "0x") != 0 || *stop != '\0' || value > 0xFFFF) {
        return false;
      }
      out |= static_cast<uint16_t>(value);
    }
    if (end == std::string::npos) {
      return true;
    }
    start = end + 1;
  }
  return true;
}

bool ParseInt(const std::string& text, long lo, long hi, long& out) {
  if (text.empty()) {
    return false;
  }
  char* stop = nullptr;
  out = std::strtol(text.c_str(), &stop, 10);
  return *stop == '\0' && out >= lo && out <= hi;
}

bool ParseU32(const std::string& text, uint32_t& out) {
  if (text.empty() || text[0] == '-') {
    return false;
  }
  char* stop = nullptr;
  const unsigned long long value = std::strtoull(text.c_str(), &stop, 0);
  out = static_cast<uint32_t>(value);
  return *stop == '\0' && value <= 0xFFFFFFFFull;
}

std::vector<std::string> Words(const std::string& line) {
  std::vector<std::string> words;
  std::istringstream in(line);
  std::string w;
  while (in >> w) {
    words.push_back(w);
  }
  return words;
}

bool ParseU64(const std::string& text, uint64_t& out) {
  if (text.empty() || text[0] == '-') {
    return false;
  }
  char* stop = nullptr;
  out = std::strtoull(text.c_str(), &stop, 0);
  return *stop == '\0';
}

bool ParseTicks(const std::string& text, bool& relative, int64_t& out) {
  if (text.empty()) {
    return false;
  }
  relative = text[0] == '+' || text[0] == '-';
  if (!relative && (text[0] < '0' || text[0] > '9')) {
    return false;
  }
  char* stop = nullptr;
  out = std::strtoll(text.c_str(), &stop, 10);
  return *stop == '\0';
}

bool ParseTime(const std::vector<std::string>& w, Recording& into) {
  if (w.size() == 5 && w[1] == "site" && w[3] == "at") {
    ClockSite site;
    if (!ParseU32(w[2], site.number) || site.number == 0 || !ParseU64(w[4], site.address)) {
      return false;
    }
    into.clockSites.push_back(site);
    return true;
  }
  uint32_t frame = 0;
  if (w.size() < 5 || w.size() % 2 != 1 || w[1] != "frame" || !ParseU32(w[2], frame)) {
    return false;
  }
  std::vector<ClockRead> reads;
  bool started = into.clockStarted;
  int64_t last = into.clockReads.empty() ? 0 : into.clockReads.back().ticks;
  for (size_t i = 3; i + 1 < w.size(); i += 2) {
    ClockRead r;
    r.frame = frame;
    bool relative = false;
    int64_t value = 0;
    if (!ParseU32(w[i], r.site) || r.site == 0 || !ParseTicks(w[i + 1], relative, value) || relative != started) {
      return false;
    }
    r.ticks = relative ? last + value : value;
    last = r.ticks;
    started = true;
    reads.push_back(r);
  }
  into.clockStarted = started;
  into.clockReads.insert(into.clockReads.end(), reads.begin(), reads.end());
  return true;
}

bool ParseFrameField(const std::vector<std::string>& w, uint32_t& frame, uint32_t& field) {
  return w.size() >= 5 && w[1] == "frame" && ParseU32(w[2], frame) && w[3] == "field" && ParseU32(w[4], field);
}

bool ParseChange(const std::vector<std::string>& w, Change& c) {
  if (w.size() < 8 || w[0] != "frame" || !ParseU32(w[1], c.when.frame) || w[2] != "poll" ||
      !ParseU32(w[3], c.when.poll) || w[4] != "field" || !ParseU32(w[5], c.field) || w[6].size() != 2 ||
      w[6][0] != 'P' || w[6][1] < '1' || w[6][1] > '4') {
    return false;
  }
  c.port = w[6][1] - '1';
  return ParsePadState(w, 7, c.state);
}

}

bool ParsePadState(const std::vector<std::string>& w, size_t i, PadState& state) {
  state = PadState{};
  if (i >= w.size()) {
    return false;
  }
  if (w[i] == "none") {
    state.err = kErrNoController;
    return w.size() == i + 1;
  }
  state.err = kErrNone;
  auto pair = [&](const char* name, long lo, long hi, long& a, long& b) {
    if (i + 2 < w.size() && w[i] == name && ParseInt(w[i + 1], lo, hi, a) && ParseInt(w[i + 2], lo, hi, b)) {
      i += 3;
      return true;
    }
    return false;
  };
  long a = 0, b = 0;
  if (!(i + 1 < w.size() && w[i] == "buttons" && ParseButtons(w[i + 1], state.button))) {
    return false;
  }
  i += 2;
  if (!pair("stick", -128, 127, a, b)) return false;
  state.stickX = static_cast<int8_t>(a), state.stickY = static_cast<int8_t>(b);
  if (!pair("cstick", -128, 127, a, b)) return false;
  state.substickX = static_cast<int8_t>(a), state.substickY = static_cast<int8_t>(b);
  if (!pair("triggers", 0, 255, a, b)) return false;
  state.triggerL = static_cast<uint8_t>(a), state.triggerR = static_cast<uint8_t>(b);
  while (i < w.size()) {
    if (pair("analog", 0, 255, a, b)) {
      state.analogA = static_cast<uint8_t>(a), state.analogB = static_cast<uint8_t>(b);
    } else if (w[i] == "ext" && i + 1 < w.size() && ParseU32(w[i + 1], state.extButton)) {
      i += 2;
    } else if (w[i] == "err" && i + 1 < w.size() && ParseInt(w[i + 1], -128, 127, a)) {
      state.err = static_cast<int8_t>(a);
      i += 2;
    } else {
      return false;
    }
  }
  return true;
}

std::string FormatPadState(const PadState& s) {
  if (s.err == kErrNoController) {
    return "none";
  }
  char line[224];
  int n = std::snprintf(line, sizeof line, "buttons %s stick %d %d cstick %d %d triggers %u %u",
                        ButtonNames(s.button).c_str(), s.stickX, s.stickY, s.substickX, s.substickY, s.triggerL,
                        s.triggerR);
  if (s.analogA != 0 || s.analogB != 0) {
    n += std::snprintf(line + n, sizeof line - n, " analog %u %u", s.analogA, s.analogB);
  }
  if (s.extButton != 0) {
    n += std::snprintf(line + n, sizeof line - n, " ext 0x%x", s.extButton);
  }
  if (s.err != kErrNone) {
    n += std::snprintf(line + n, sizeof line - n, " err %d", s.err);
  }
  return line;
}

std::string FormatChange(const Change& c) {
  char line[64];
  std::snprintf(line, sizeof line, "frame %u poll %u field %u P%d ", c.when.frame, c.when.poll, c.field, c.port + 1);
  return line + FormatPadState(c.state);
}

std::string FormatMark(const Mark& m) {
  char line[96];
  std::snprintf(line, sizeof line, "MARK frame %u field %u (#%u)", m.frame, m.field, m.number);
  return line;
}

std::string FormatDevMenu(const DevMenuLine& d) {
  char line[64];
  std::snprintf(line, sizeof line, "DEVMENU frame %u field %u ", d.frame, d.field);
  return line + d.action;
}

std::string FormatPhoto(uint32_t frame, uint32_t field, const std::string& what) {
  char line[64];
  std::snprintf(line, sizeof line, "PHOTO frame %u field %u ", frame, field);
  return line + what;
}

std::string FormatEnhanced(uint32_t frame, uint32_t field, const std::string& what) {
  char line[64];
  std::snprintf(line, sizeof line, "ENHANCED frame %u field %u ", frame, field);
  return line + what;
}

std::string FormatMenu(uint32_t frame, uint32_t field, const std::string& what) {
  char line[64];
  std::snprintf(line, sizeof line, "MENU frame %u field %u ", frame, field);
  return line + what;
}

std::string FormatSettings(uint32_t frame, uint32_t field, const std::string& list) {
  char line[64];
  std::snprintf(line, sizeof line, "SETTINGS frame %u field %u ", frame, field);
  return line + list;
}

std::string FormatPlaythrough(uint32_t frame, uint32_t field, const std::string& what) {
  char line[64];
  std::snprintf(line, sizeof line, "PLAYTHROUGH frame %u field %u ", frame, field);
  return line + what;
}

std::string FormatEnd(uint32_t frame, uint32_t field) {
  char line[64];
  std::snprintf(line, sizeof line, "END frame %u field %u", frame, field);
  return line;
}

std::string FormatIrqSite(const IrqSiteLine& site) {
  char line[96];
  std::snprintf(line, sizeof line, "IRQSITE %u %s at 0x%llx", site.number, site.alarm ? "alarm" : "raise",
                static_cast<unsigned long long>(site.address));
  return line;
}

std::string IrqWriter::Line(const IrqEntry* entries, size_t count) {
  if (count == 0) return "";
  std::string line = "IRQ";
  for (size_t i = 0; i < count; i++) {
    const IrqEntry& e = entries[i];
    if (mSeq.size() < e.site) mSeq.resize(e.site, 0);
    uint32_t& seq = mSeq[e.site - 1];
    line += ' ';
    line += std::to_string(e.count - mLast);
    line += ':';
    line += std::to_string(e.site);
    if (e.seq == seq + 1) {
      line += '+';
    } else if (e.seq != seq) {
      line += '.';
      line += std::to_string(e.seq);
    }
    if (e.kind != 'p') line += e.kind;
    seq = e.seq;
    mLast = e.count;
  }
  return line;
}

std::string FormatClockSite(const ClockSite& site) {
  char line[64];
  std::snprintf(line, sizeof line, "TIME site %u at 0x%llx", site.number, static_cast<unsigned long long>(site.address));
  return line;
}

bool ParseLine(const std::string& line, Recording& into) {
  size_t first = line.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) {
    return true;
  }
  if (line[first] == '#') {
    std::string text = line.substr(first + 1);
    while (!text.empty() && (text.back() == '\r' || text.back() == '\n')) text.pop_back();
    if (!text.empty() && text[0] == ' ') text.erase(0, 1);
    into.header.push_back(text);
    return true;
  }
  const std::vector<std::string> w = Words(line);
  if (w[0] == "MARK") {
    Mark m;
    if (!ParseFrameField(w, m.frame, m.field)) {
      return false;
    }
    m.number = static_cast<uint32_t>(into.marks.size() + 1);
    into.marks.push_back(m);
    return true;
  }
  if (w[0] == "DEVMENU") {
    DevMenuLine d;
    if (!ParseFrameField(w, d.frame, d.field) || w.size() < 6) {
      return false;
    }
    for (size_t i = 5; i < w.size(); i++) {
      d.action += (i > 5 ? " " : "") + w[i];
    }
    into.devMenu.push_back(d);
    return true;
  }
  if (w[0] == "SETTINGS") {
    DevMenuLine d;
    if (!ParseFrameField(w, d.frame, d.field) || w.size() < 6) {
      return false;
    }
    for (size_t i = 5; i < w.size(); i++) {
      d.action += (i > 5 ? " " : "") + w[i];
    }
    into.settings.push_back(d);
    into.settingsSinceMenu = true;
    return true;
  }
  if (w[0] == "MENU") {
    into.settingsSinceMenu = false;
    return true;
  }
  if (w[0] == "ENHANCED") {
    DevMenuLine d;
    if (!into.settingsSinceMenu && w.size() == 10 && w[5] == "first" && w[6] == "use:" && w[7] == "text" &&
        w[8] == "speed" && ParseFrameField(w, d.frame, d.field)) {
      d.action = "gameplay.text_speed=" + w[9];
      into.settings.push_back(d);
    }
    return true;
  }
  if (w[0] == "PHOTO" || w[0] == "PLAYTHROUGH") {
    return true;
  }
  if (w[0] == "IRQSITE") {
    if (w.size() < 5 || w[3] != "at" || (w[2] != "alarm" && w[2] != "raise")) return false;
    IrqSiteLine site;
    site.number = static_cast<uint32_t>(std::stoul(w[1]));
    site.alarm = w[2] == "alarm";
    site.address = std::stoull(w[4], nullptr, 16);
    if (site.number != into.irqSites.size() + 1) return false;
    into.irqSites.push_back(site);
    return true;
  }
  if (w[0] == "IRQ") {
    std::vector<IrqEntry> got;
    uint64_t last = into.irqLast;
    std::vector<uint32_t> seqs = into.irqSeq;
    for (size_t i = 1; i < w.size(); i++) {
      const std::string& t = w[i];
      size_t pos = 0;
      IrqEntry e;
      const size_t colon = t.find(':');
      if (colon == std::string::npos || colon == 0) return false;
      for (size_t k = 0; k < colon; k++) {
        if (t[k] < '0' || t[k] > '9') return false;
      }
      e.count = last + std::stoull(t.substr(0, colon));
      pos = colon + 1;
      size_t digits = 0;
      while (pos + digits < t.size() && t[pos + digits] >= '0' && t[pos + digits] <= '9') digits++;
      if (digits == 0) return false;
      e.site = static_cast<uint32_t>(std::stoul(t.substr(pos, digits)));
      pos += digits;
      if (e.site == 0 || e.site > 100000) return false;
      if (seqs.size() < e.site) seqs.resize(e.site, 0);
      e.seq = seqs[e.site - 1];
      if (pos < t.size() && t[pos] == '+') {
        e.seq++;
        pos++;
      } else if (pos < t.size() && t[pos] == '.') {
        pos++;
        digits = 0;
        while (pos + digits < t.size() && t[pos + digits] >= '0' && t[pos + digits] <= '9') digits++;
        if (digits == 0) return false;
        e.seq = static_cast<uint32_t>(std::stoul(t.substr(pos, digits)));
        pos += digits;
      }
      if (pos < t.size()) {
        e.kind = t[pos++];
        if (e.kind != 'w' && e.kind != 'b' && e.kind != 'i' && e.kind != 'j') return false;
      }
      if (pos != t.size()) return false;
      seqs[e.site - 1] = e.seq;
      last = e.count;
      got.push_back(e);
    }
    into.irq.insert(into.irq.end(), got.begin(), got.end());
    into.irqLast = last;
    into.irqSeq = std::move(seqs);
    return true;
  }
  if (w[0] == "FSYS") {
    if (w.size() < 5 || w[1] != "pass" || w[3] != "blocks") return false;
    into.fsys.emplace_back(static_cast<uint32_t>(std::stoul(w[2])), static_cast<uint32_t>(std::stoul(w[4])));
    return true;
  }
  if (w[0] == "TIME") {
    return ParseTime(w, into);
  }
  if (w[0] == "FORMAT" || w[0] == "TICK" || w[0] == "USE" || w[0] == "ANCHOR" || w[0] == "TICKS") {
    return ParseAnchoredLine(w, into);
  }
  if (w[0] == "END") {
    if (!ParseFrameField(w, into.endFrame, into.endField)) {
      return false;
    }
    into.hasEnd = true;
    return true;
  }
  Change c;
  if (!ParseChange(w, c)) {
    return false;
  }
  into.changes.push_back(c);
  return true;
}

Recording ParseRecording(const std::string& text, std::string* problems) {
  Recording rec;
  std::istringstream in(text);
  std::string line;
  int number = 0;
  while (std::getline(in, line)) {
    number++;
    if (!ParseLine(line, rec) && problems != nullptr) {
      *problems += "line " + std::to_string(number) + " not understood: " + line + "\n";
    }
  }
  return rec;
}

bool ChangeFilter::Changed(int port, const PadState& state) {
  if (mSeen[port] && mLast[port] == state) {
    return false;
  }
  mSeen[port] = true;
  mLast[port] = state;
  return true;
}

void Player::Start() {
  const std::vector<Change>& changes = mRec.changes;
  if (!mStarted && !changes.empty()) {
    const When first = changes[0].when;
    while (mNext < changes.size() && changes[mNext].when.frame == first.frame &&
           changes[mNext].when.poll == first.poll) {
      mState[changes[mNext].port] = changes[mNext].state;
      mNext++;
    }
  }
  mStarted = true;
}

const PadState& Player::Current(int port) {
  Start();
  return mState[port];
}

void Player::Apply(When now, PadState out[kPorts]) {
  const std::vector<Change>& changes = mRec.changes;
  Start();
  while (mNext < changes.size() && changes[mNext].when <= now) {
    mState[changes[mNext].port] = changes[mNext].state;
    mNext++;
  }
  for (int i = 0; i < kPorts; i++) {
    out[i] = mState[i];
  }
}

bool Player::Finished(When now) const {
  if (mRec.hasEnd) {
    return now.frame >= mRec.endFrame && mNext >= mRec.changes.size();
  }
  const uint32_t lastRead = mRec.clockReads.empty() ? 0 : mRec.clockReads.back().frame;
  return mStarted && mNext >= mRec.changes.size() && now.frame >= lastRead;
}

std::vector<Mark> Player::MarksReached(uint32_t frame) {
  std::vector<Mark> out;
  while (mNextMark < mRec.marks.size() && mRec.marks[mNextMark].frame <= frame) {
    out.push_back(mRec.marks[mNextMark++]);
  }
  return out;
}

uint32_t ClockWriter::Site(uint64_t address, std::string* declaration) {
  if (declaration != nullptr) {
    declaration->clear();
  }
  auto it = mSites.find(address);
  if (it != mSites.end()) {
    return it->second;
  }
  const uint32_t number = static_cast<uint32_t>(mSites.size() + 1);
  mSites[address] = number;
  if (declaration != nullptr) {
    *declaration = FormatClockSite(ClockSite{number, address});
  }
  return number;
}

std::string ClockWriter::Add(uint32_t frame, uint32_t site, int64_t ticks) {
  if (site >= 1) {
    if (mCounts.size() < site) mCounts.resize(site, 0);
    mCounts[site - 1]++;
  }
  std::string done;
  if (!mLine.empty() && frame != mFrame) {
    done = Flush();
  }
  if (mLine.empty()) {
    mFrame = frame;
    mLine = "TIME frame " + std::to_string(frame);
  }
  char value[48];
  if (site >= 1) {
    if (mSiteLast.size() < site) mSiteLast.resize(site, INT64_MIN);
    const int64_t before = mSiteLast[site - 1];
    if (before != INT64_MIN && ticks < before) {
      static bool warned = false;
      if (!warned) {
        warned = true;
        std::fprintf(stderr, "[record] WARNING: the game's clock went backwards at game frame %u (by %lld ticks, "
                             "clock site %u); this recording will not replay faithfully past here\n",
                     frame, static_cast<long long>(before - ticks), site);
      }
    }
    mSiteLast[site - 1] = ticks;
  }
  if (mStarted) {
    std::snprintf(value, sizeof value, " %u %+lld", site, static_cast<long long>(ticks - mLast));
  } else {
    std::snprintf(value, sizeof value, " %u %lld", site, static_cast<long long>(ticks));
  }
  mLine += value;
  mLast = ticks;
  mStarted = true;
  return done;
}

std::string ClockWriter::Flush() {
  std::string line;
  line.swap(mLine);
  return line;
}

ClockPlayer::ClockPlayer(const Recording& rec) {
  uint32_t sites = 0;
  for (const ClockSite& s : rec.clockSites) {
    sites = std::max(sites, s.number);
  }
  for (const ClockRead& r : rec.clockReads) {
    sites = std::max(sites, r.site);
  }
  mSites.resize(sites);
  for (const ClockSite& s : rec.clockSites) {
    mSites[s.number - 1].address = s.address;
  }
  uint32_t seq = 0;
  const ClockRead* before = nullptr;
  for (const ClockRead& r : rec.clockReads) {
    mSites[r.site - 1].reads.push_back(r);
    mSites[r.site - 1].seq.push_back(seq++);
    if (before != nullptr && before->site != r.site && r.ticks < before->ticks) {
      mSites[r.site - 1].family = 1;
    }
    before = &r;
  }
}

bool ClockPlayer::OrderWait(uint64_t address, uint32_t timeline, size_t* need, size_t* have) {
  if (mLive || mSites.empty() || timeline == 0 || timeline > mSites.size()) return false;
  const int index = Map(address);
  if (index < 0 || static_cast<uint32_t>(index) + 1 == timeline) return false;
  const Site& site = mSites[index];
  if (site.next >= site.seq.size()) return false;
  if (mRealigned && index < static_cast<int>(mLimit.size()) && site.next >= mLimit[index]) return false;
  const Site& t = mSites[timeline - 1];
  *need = static_cast<size_t>(std::lower_bound(t.seq.begin(), t.seq.end(), site.seq[site.next]) - t.seq.begin());
  *have = t.next;
  return *have < *need;
}

int ClockPlayer::Map(uint64_t address) {
  if (mMode == Mode::Undecided) {
    mMode = !mSites.empty() && mSites[0].address == address ? Mode::ByAddress : Mode::ByOrder;
    AddNote(mMode == Mode::ByAddress
                ? "clock: same build as recorded, clock reads matched by their place in the program"
                : "clock: another build than recorded, clock reads matched in the order they first happen");
  }
  auto it = mMap.find(address);
  if (it != mMap.end()) {
    return it->second;
  }
  int index = -1;
  if (mMode == Mode::ByAddress) {
    for (size_t i = 0; i < mSites.size(); i++) {
      if (mSites[i].address == address) {
        index = static_cast<int>(i);
      }
    }
  } else if (mNextByOrder < mSites.size()) {
    index = static_cast<int>(mNextByOrder++);
  }
  mMap[address] = index;
  return index;
}

int64_t ClockPlayer::Read(uint64_t address, uint32_t frame, int64_t live) {
  if (!mLive && !mSites.empty()) {
    const int index = Map(address);
    std::string why;
    if (index < 0) {
      char text[96];
      std::snprintf(text, sizeof text, "a clock read the recording doesn't have (at 0x%llx)",
                    static_cast<unsigned long long>(address));
      why = text;
    } else if (mRealigned && index < static_cast<int>(mLimit.size()) && mSites[index].next >= mLimit[index] &&
               mSites[index].next > 0) {
      Site& site = mSites[index];
      const int64_t last = site.reads[site.next - 1].ticks + site.offset;
      if (site.extra++ == 0) site.liveBase = live;
      int64_t value = last + std::max<int64_t>(site.extra, live - site.liveBase);
      if (site.last != INT64_MIN) value = std::max(value, site.last + 1);
      if (mFamilyLast[site.family] != INT64_MIN) value = std::max(value, mFamilyLast[site.family]);
      site.last = value;
      mFamilyLast[site.family] = value;
      mMax = std::max(mMax, value);
      mGiven = true;
      return value;
    } else if (mSites[index].next >= mSites[index].reads.size()) {
      why = "site " + std::to_string(index + 1) + " reads the clock more often than recorded";
    } else {
      const ClockRead& r = mSites[index].reads[mSites[index].next];
      const int64_t moved = static_cast<int64_t>(frame) - mShift;
      const int64_t apart = moved > r.frame ? moved - r.frame : r.frame - moved;
      if (apart <= kFrameWindow || mRealigned) {
        Site& site = mSites[index];
        site.next++;
        site.extra = 0;
        mExact++;
        int64_t value = r.ticks + site.offset;
        if (mRealigned && site.last != INT64_MIN) value = std::max(value, site.last);
        if (mRealigned && mFamilyLast[site.family] != INT64_MIN) value = std::max(value, mFamilyLast[site.family]);
        site.last = value;
        mFamilyLast[site.family] = std::max(mFamilyLast[site.family], value);
        mMax = mGiven ? std::max(mMax, value) : value;
        mGiven = true;
        return value;
      }
      why = "site " + std::to_string(index + 1) + " read the clock at game frame " + std::to_string(frame) +
            ", recorded at " + std::to_string(r.frame);
    }
    GoLive(live, why + " at game frame " + std::to_string(frame));
  }
  return live + mOffset;
}

void ClockPlayer::AddNote(const std::string& text) {
  if (!mNote.empty()) {
    mNote += '\n';
  }
  mNote += text;
}

void ClockPlayer::GoLive(int64_t live, const std::string& why) {
  if (mLive) {
    return;
  }
  if (!mSites.empty()) {
    AddNote("clock: " + why + "; the game gets the live clock from here (" + std::to_string(mExact) +
            " reads were as recorded)");
  }
  mLive = true;
  mOffset = mGiven ? mMax + 1 - live : 0;
}

void ClockPlayer::Realign(const std::vector<uint32_t>& counts, int64_t frameShift) {
  if (mLive) {
    return;
  }
  for (size_t i = 0; i < mSites.size() && i < counts.size(); i++) {
    Site& site = mSites[i];
    site.next = std::min<size_t>(counts[i], site.reads.size());
    site.extra = 0;
  }
  constexpr int64_t kRecent = 2 * kFieldTicks;
  for (int family = 0; family < 2; family++) {
    int64_t offset = INT64_MIN;
    for (size_t i = 0; i < mSites.size() && i < counts.size(); i++) {
      const Site& site = mSites[i];
      if (site.family != family || site.last == INT64_MIN || site.next == 0 || site.next >= site.reads.size()) continue;
      if (mFamilyLast[family] != INT64_MIN && site.last < mFamilyLast[family] - kRecent) continue;
      offset = std::max(offset, site.last - site.reads[site.next - 1].ticks);
    }
    if (offset == INT64_MIN) continue;
    for (size_t i = 0; i < mSites.size() && i < counts.size(); i++) {
      if (mSites[i].family == family) mSites[i].offset = offset;
    }
  }
  mShift = frameShift;
  mRealigned = true;
}

void ClockPlayer::SetLimit(const std::vector<uint32_t>& counts) {
  mLimit.assign(mSites.size(), SIZE_MAX);
  for (size_t i = 0; i < mSites.size() && i < counts.size(); i++) mLimit[i] = counts[i];
}

std::string ClockPlayer::TakeNote() {
  std::string note;
  note.swap(mNote);
  return note;
}

bool BuildFieldTimeline(const Recording& rec, FieldTimeline& out, std::string* why) {
  out = FieldTimeline{};
  if (rec.clockReads.empty()) {
    if (why) *why = "no clock reads in the recording";
    return false;
  }
  constexpr int64_t kSlack = 4096;
  struct Stat {
    uint64_t reads = 0, aligned = 0;
    int64_t first = 0;
    bool seen = false;
  };
  std::vector<Stat> stats;
  for (const ClockRead& r : rec.clockReads) {
    if (r.site == 0) continue;
    if (stats.size() < r.site) stats.resize(r.site);
    Stat& s = stats[r.site - 1];
    if (!s.seen) {
      s.seen = true;
      s.first = r.ticks;
    }
    s.reads++;
    const int64_t d = r.ticks - s.first;
    if (d >= 0 && d % kFieldTicks <= kSlack) s.aligned++;
  }
  uint32_t best = 0;
  for (uint32_t i = 0; i < stats.size(); i++) {
    const Stat& s = stats[i];
    if (s.reads >= 100 && s.aligned * 100 >= s.reads * 98 && (best == 0 || s.reads > stats[best - 1].reads)) {
      best = i + 1;
    }
  }
  if (best == 0) {
    if (why) *why = "no clock site in whole video fields (recorded with system.game_clock = pc?)";
    return false;
  }
  out.site = best;
  const int64_t first = stats[best - 1].first;
  int32_t last = 0;
  out.fields.reserve(stats[best - 1].reads);
  for (const ClockRead& r : rec.clockReads) {
    if (r.site != best) continue;
    int32_t f = static_cast<int32_t>((r.ticks - first + kSlack) / kFieldTicks);
    if (f < last) f = last;
    out.fields.push_back(last = f);
  }
  return true;
}

}
