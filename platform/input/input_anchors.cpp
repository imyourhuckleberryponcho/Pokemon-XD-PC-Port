#include "input_anchors.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace port_input_record {
namespace {

const char* const kKindNames[] = {"start", "floor", "message", "battle", "menu", "fade", "movie", "save"};

bool U32(const std::string& text, uint32_t& out) {
  if (text.empty() || text[0] == '-') return false;
  char* stop = nullptr;
  const unsigned long long v = std::strtoull(text.c_str(), &stop, 0);
  out = static_cast<uint32_t>(v);
  return *stop == '\0' && v <= 0xFFFFFFFFull;
}

bool U64(const std::string& text, uint64_t& out) {
  if (text.empty() || text[0] == '-') return false;
  char* stop = nullptr;
  out = std::strtoull(text.c_str(), &stop, 0);
  return *stop == '\0';
}

bool Near(int a, int b) { return std::abs(a - b) <= 2; }

std::string Hex32(uint32_t v) {
  char t[16];
  std::snprintf(t, sizeof t, "0x%x", v);
  return t;
}
std::string Hex64(uint64_t v) {
  char t[24];
  std::snprintf(t, sizeof t, "0x%016llx", static_cast<unsigned long long>(v));
  return t;
}

}

bool IsHardAnchor(AnchorKind kind) {
  return kind == AnchorKind::Start || kind == AnchorKind::Floor || kind == AnchorKind::Battle ||
         kind == AnchorKind::Save;
}

const char* AnchorKindName(AnchorKind kind) {
  const size_t i = static_cast<size_t>(kind);
  return i < sizeof kKindNames / sizeof kKindNames[0] ? kKindNames[i] : "unknown";
}

bool AnchorKindFromName(const std::string& name, AnchorKind& kind) {
  for (size_t i = 0; i < sizeof kKindNames / sizeof kKindNames[0]; i++) {
    if (name == kKindNames[i]) {
      kind = static_cast<AnchorKind>(i);
      return true;
    }
  }
  return false;
}

std::string FormatFormatLine(uint32_t format) { return "FORMAT " + std::to_string(format); }

std::string FormatTickChange(const TickChange& c) {
  return "TICK " + std::to_string(c.tick) + " P" + std::to_string(c.port + 1) + " " + FormatPadState(c.state);
}

std::string FormatTickRun(const TickRun& r) { return "USE " + std::to_string(r.from) + " " + std::to_string(r.count); }

std::string FormatTickCount(uint32_t ticks) { return "TICKS " + std::to_string(ticks); }

std::string FormatAnchor(const Anchor& a) {
  char line[256];
  std::snprintf(line, sizeof line, "ANCHOR %u %s id 0x%x tick %u frame %u seed 0x%08x floor 0x%x party 0x%016llx "
                                   "progress 0x%016llx clock ",
                a.number, AnchorKindName(a.kind), a.id, a.tick, a.frame, a.state.seed, a.state.floor,
                static_cast<unsigned long long>(a.state.party), static_cast<unsigned long long>(a.state.progress));
  std::string out = line;
  if (a.clock.empty()) {
    out += "-";
  }
  for (size_t i = 0; i < a.clock.size(); i++) {
    out += (i > 0 ? "," : "") + std::to_string(a.clock[i]);
  }
  return out;
}

bool ParseAnchoredLine(const std::vector<std::string>& w, Recording& into) {
  if (w[0] == "FORMAT") {
    return w.size() == 2 && U32(w[1], into.format) && into.format >= 1;
  }
  if (w[0] == "TICKS") {
    if (w.size() != 2 || !U32(w[1], into.tickCount)) return false;
    into.hasTicks = true;
    return true;
  }
  if (w[0] == "USE") {
    TickRun r;
    if (w.size() != 3 || !U32(w[1], r.from) || !U32(w[2], r.count) || r.count == 0) return false;
    into.uses.push_back(r);
    return true;
  }
  if (w[0] == "TICK") {
    TickChange c;
    if (w.size() < 4 || !U32(w[1], c.tick) || w[2].size() != 2 || w[2][0] != 'P' || w[2][1] < '1' || w[2][1] > '4') {
      return false;
    }
    c.port = w[2][1] - '1';
    if (!ParsePadState(w, 3, c.state)) return false;
    into.tickChanges.push_back(c);
    return true;
  }
  if (w[0] == "ANCHOR") {
    Anchor a;
    if (w.size() != 19 || !U32(w[1], a.number) || !AnchorKindFromName(w[2], a.kind) || w[3] != "id" ||
        !U32(w[4], a.id) || w[5] != "tick" || !U32(w[6], a.tick) || w[7] != "frame" || !U32(w[8], a.frame) ||
        w[9] != "seed" || !U32(w[10], a.state.seed) || w[11] != "floor" || !U32(w[12], a.state.floor) ||
        w[13] != "party" || !U64(w[14], a.state.party) || w[15] != "progress" || !U64(w[16], a.state.progress) ||
        w[17] != "clock") {
      return false;
    }
    if (w[18] != "-") {
      size_t start = 0;
      while (start <= w[18].size()) {
        const size_t comma = w[18].find(',', start);
        uint32_t n = 0;
        if (!U32(w[18].substr(start, comma == std::string::npos ? std::string::npos : comma - start), n)) return false;
        a.clock.push_back(n);
        if (comma == std::string::npos) break;
        start = comma + 1;
      }
    }
    into.anchors.push_back(a);
    return true;
  }
  return false;
}

bool Equivalent(const PadState& a, const PadState& b) {
  return a.err == b.err && a.button == b.button && a.extButton == b.extButton && Near(a.stickX, b.stickX) &&
         Near(a.stickY, b.stickY) && Near(a.substickX, b.substickX) && Near(a.substickY, b.substickY) &&
         Near(a.triggerL, b.triggerL) && Near(a.triggerR, b.triggerR) && Near(a.analogA, b.analogA) &&
         Near(a.analogB, b.analogB);
}

void TickWriter::Begin(const PadState pads[kPorts], std::vector<std::string>& lines) {
  for (int i = 0; i < kPorts; i++) {
    if (mFilter.Changed(i, pads[i])) {
      lines.push_back(FormatTickChange(TickChange{mTicks, i, pads[i]}));
    }
  }
  mTicks++;
  mStarted = true;
}

void TickWriter::End(bool used, std::vector<std::string>& lines) {
  if (!mStarted) return;
  mStarted = false;
  const uint32_t tick = mTicks - 1;
  if (used) {
    if (mRunCount > 0 && mRunFrom + mRunCount == tick) {
      mRunCount++;
    } else {
      const std::string done = Flush();
      if (!done.empty()) lines.push_back(done);
      mRunFrom = tick;
      mRunCount = 1;
    }
  } else {
    const std::string done = Flush();
    if (!done.empty()) lines.push_back(done);
  }
}

std::string TickWriter::Flush() {
  if (mRunCount == 0) return "";
  const std::string line = FormatTickRun(TickRun{mRunFrom, mRunCount});
  mRunCount = 0;
  return line;
}

std::string AnchorMismatch::Describe() const {
  return "anchor " + std::to_string(anchor) + " field " + field + ": recorded " + expected + ", replay " + actual;
}

TickPlayer::TickPlayer(const Recording& rec, Mode mode) : mAnchors(rec.anchors), mMode(mode) {
  mFormat2 = rec.format >= 2;
  uint32_t total = rec.hasTicks ? rec.tickCount : 0;
  for (const TickChange& c : rec.tickChanges) total = std::max(total, c.tick + 1);
  for (const TickRun& r : rec.uses) total = std::max(total, r.from + r.count);
  for (const Anchor& a : rec.anchors) total = std::max(total, a.tick + 1);
  mTotal = total;
  mUsed.assign(total, false);
  for (const TickRun& r : rec.uses) {
    for (uint32_t t = r.from; t < r.from + r.count && t < total; t++) mUsed[t] = true;
  }
  PadState current[kPorts];
  mSampleOf.assign(total, 0);
  size_t next = 0;
  for (uint32_t t = 0; t < total; t++) {
    bool changed = t == 0;
    while (next < rec.tickChanges.size() && rec.tickChanges[next].tick <= t) {
      current[rec.tickChanges[next].port] = rec.tickChanges[next].state;
      next++;
      changed = true;
    }
    if (changed) {
      mSamples.insert(mSamples.end(), current, current + kPorts);
    }
    mSampleOf[t] = static_cast<uint32_t>(mSamples.size() / kPorts - 1);
  }
  if (mSamples.empty()) mSamples.insert(mSamples.end(), current, current + kPorts);
}

bool TickPlayer::Usable(std::string* why) const {
  if (!mFormat2) {
    if (why) *why = "a format-1 recording (no FORMAT 2 line): replayed by game frame";
    return false;
  }
  if (mTotal == 0) {
    if (why) *why = "no ticks in this recording";
    return false;
  }
  return true;
}

uint32_t TickPlayer::SegBegin(uint32_t s) const { return s == 0 ? 0 : std::min(mAnchors[s - 1].tick, mTotal); }
uint32_t TickPlayer::SegEnd(uint32_t s) const { return s < mAnchors.size() ? std::min(mAnchors[s].tick, mTotal) : mTotal; }

const PadState* TickPlayer::SampleAt(uint32_t tick) const {
  return &mSamples[static_cast<size_t>(mSampleOf[std::min(tick, mTotal - 1)]) * kPorts];
}

uint32_t TickPlayer::NextUsed(uint32_t from) const {
  const uint32_t end = SegEnd(mSegment);
  for (uint32_t t = from; t < end; t++) {
    if (mUsed[t]) return t;
  }
  return end;
}

const PadState* TickPlayer::Current() const {
  if (!mStarted) return SampleAt(0);
  return &mSamples[static_cast<size_t>(mFed) * kPorts];
}

const PadState* TickPlayer::Next() {
  mStarted = true;
  mFedEntry = false;
  const uint32_t end = SegEnd(mSegment);
  if (mHold) {
    mFed = mHoldSample;
  } else if (mPos < end && Used(mPos)) {
    mFed = mSampleOf[mPos];
    mFedEntry = true;
  } else if (mPos < end) {
    const uint32_t begin = SegBegin(mSegment);
    if (mWaitSegment != mSegment || mPos < mWaitFrom || mPos >= mWaitEnd) {
      mWaitSegment = mSegment;
      mWaitFrom = mPos;
      while (mWaitFrom > begin && !mUsed[mWaitFrom - 1]) mWaitFrom--;
      mWaitEnd = NextUsed(mPos);
      mWaitHeld = mWaitFrom;
      if (mWaitFrom > begin) {
        while (mWaitHeld < mWaitEnd && mSampleOf[mWaitHeld] == mSampleOf[mWaitFrom - 1]) mWaitHeld++;
      }
    }
    mFed = mPos < mWaitHeld ? mSampleOf[mWaitFrom - 1] : mSampleOf[mWaitEnd - 1];
    mFedEntry = true;
  } else {
    const uint32_t begin = SegBegin(mSegment);
    mFed = end > begin ? mSampleOf[end - 1] : mLastFed;
  }
  return &mSamples[static_cast<size_t>(mFed) * kPorts];
}

namespace {
bool SameSample(const PadState* a, const PadState* b) {
  for (int i = 0; i < kPorts; i++) {
    if (!Equivalent(a[i], b[i])) return false;
  }
  return true;
}
}

void TickPlayer::EndTick(bool used) {
  if (!mStarted) return;
  mTicks++;
  const uint32_t previous = mLastFed;
  mLastFed = mFed;
  const PadState* fed = &mSamples[static_cast<size_t>(mFed) * kPorts];
  if (mHold) {
    if (!used) return;
    mHold = false;
    if (SameSample(fed, SampleAt(mPos))) {
      mPos++;
    } else {
      mStats.phantom++;
    }
    return;
  }
  if (!mFedEntry) {
    mStats.overflow++;
    return;
  }
  if (used == Used(mPos)) {
    mPos++;
    return;
  }
  if (used) {
    mStats.early++;
    const uint32_t next = NextUsed(mPos + 1);
    if (next < SegEnd(mSegment)) {
      if (SameSample(fed, SampleAt(next))) {
        mPos = next + 1;
      } else {
        mStats.phantom++;
        mPos = next;
      }
    } else {
      mPos++;
    }
    return;
  }
  mStats.late++;
  mHold = true;
  mHoldSample = mPos > SegBegin(mSegment) ? mSampleOf[mPos - 1] : previous;
}

TickPlayer::Check TickPlayer::Compare(const Anchor& expected, AnchorKind kind, uint32_t id,
                                      const AnchorState& actual) {
  Check check;
  check.recorded = &expected;
  const bool strict = mMode == Mode::Strict;
  auto differ = [&](const char* field, std::string e, std::string a) {
    check.ok = false;
    check.mismatch = {expected.number, field, std::move(e), std::move(a)};
  };
  if (expected.kind != kind) {
    differ("kind", AnchorKindName(expected.kind) + std::string(" ") + Hex32(expected.id),
           AnchorKindName(kind) + std::string(" ") + Hex32(id));
  } else if (expected.id != id) {
    differ("id", Hex32(expected.id), Hex32(id));
  } else if (strict && expected.state.seed != actual.seed) {
    differ("seed", Hex32(expected.state.seed), Hex32(actual.seed));
  } else if (expected.state.floor != actual.floor) {
    differ("floor", Hex32(expected.state.floor), Hex32(actual.floor));
  } else {
    auto digest = [&](const char* field, uint64_t e, uint64_t a, Carried& c) {
      if (e == a) {
        c.on = false;
        return;
      }
      const bool same = c.on && c.expected == e && c.actual == a;
      c = {true, e, a};
      if (check.ok && (strict || !same)) differ(field, Hex64(e), Hex64(a));
    };
    digest("party", expected.state.party, actual.party, mPartyDiff);
    digest("progress", expected.state.progress, actual.progress, mProgressDiff);
  }
  if (!strict && expected.kind == kind && expected.id == id) {
    check.restoreSeed = true;
    check.seed = expected.state.seed;
    if (expected.state.seed != actual.seed) mStats.resyncs++;
  }
  return check;
}

TickPlayer::Check TickPlayer::OnAnchor(AnchorKind kind, uint32_t id, const AnchorState& actual) {
  for (size_t i = 0; i < mPending.size(); i++) {
    const Anchor& p = mAnchors[mPending[i].index];
    if (p.kind == kind && p.id == id) {
      mPending.erase(mPending.begin() + static_cast<long>(i));
      Check check = Compare(p, kind, id, actual);
      check.restoreSeed = false;
      return check;
    }
  }
  const uint32_t end = SegEnd(mSegment);
  const bool strict = mMode == Mode::Strict;
  const bool soft = !IsHardAnchor(kind);
  if (mSegment >= mAnchors.size()) {
    Check check;
    check.beyond = true;
    if (!strict && soft) {
      mStats.extra++;
      check.extra = true;
      return check;
    }
    for (uint32_t t = mPos; t < end; t++) {
      if (mUsed[t]) mStats.unused++;
    }
    if (!strict && mPos >= end && !mHold) {
      mEndedEarly = true;
    } else {
      check.ok = false;
      check.mismatch = {static_cast<uint32_t>(mAnchors.size() + 1), "anchor", "none (the recording ends)",
                        std::string(AnchorKindName(kind)) + " " + Hex32(id)};
    }
    mPos = end;
    mHold = false;
    return check;
  }
  uint32_t match = UINT32_MAX;
  for (uint32_t j = mSegment; j < mAnchors.size() && j <= mSegment + kSearchAhead; j++) {
    if (mAnchors[j].kind == kind && mAnchors[j].id == id) {
      match = j;
      break;
    }
    if (IsHardAnchor(mAnchors[j].kind)) break;
  }
  if (match != UINT32_MAX && match > mSegment && !strict && soft) {
    uint32_t left = 0;
    for (uint32_t t = mPos; t < end; t++) {
      if (mUsed[t]) left++;
    }
    if (left > 2) match = UINT32_MAX;
  }
  if (match == UINT32_MAX && !strict && soft) {
    mStats.extra++;
    Check check;
    check.extra = true;
    return check;
  }
  if (match == UINT32_MAX) {
    match = mSegment;
  }
  for (uint32_t t = mPos; t < end; t++) {
    if (mUsed[t]) mStats.unused++;
  }
  for (uint32_t k = mSegment; k < match; k++) {
    if (strict) {
      mPending.push_back({k, mTicks + kLateTicks});
    } else {
      mStats.skipped++;
    }
  }
  Check check = Compare(mAnchors[match], kind, id, actual);
  mSegment = match + 1;
  mPos = SegBegin(mSegment);
  mHold = false;
  return check;
}

bool TickPlayer::TakeMissing(AnchorMismatch* out) {
  for (size_t i = 0; i < mPending.size(); i++) {
    if (mTicks > mPending[i].deadline) {
      const Anchor& a = mAnchors[mPending[i].index];
      *out = {a.number, "anchor", std::string(AnchorKindName(a.kind)) + " " + Hex32(a.id), "none (it did not come)"};
      mPending.erase(mPending.begin() + static_cast<long>(i));
      return true;
    }
  }
  return false;
}

bool TickPlayer::Finished() const {
  return mEndedEarly ||
         (mSegment >= mAnchors.size() && !mHold && mPos >= SegEnd(mSegment) && mStarted && mPending.empty());
}

}
