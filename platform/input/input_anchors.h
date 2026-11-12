#pragma once

#include "input_record_format.h"

#include <cstdint>
#include <string>
#include <vector>

namespace port_input_record {

bool IsHardAnchor(AnchorKind kind);
const char* AnchorKindName(AnchorKind kind);
bool AnchorKindFromName(const std::string& name, AnchorKind& kind);

std::string FormatFormatLine(uint32_t format);
std::string FormatTickChange(const TickChange& change);
std::string FormatTickRun(const TickRun& run);
std::string FormatAnchor(const Anchor& anchor);
std::string FormatTickCount(uint32_t ticks);
bool ParseAnchoredLine(const std::vector<std::string>& w, Recording& into);

bool Equivalent(const PadState& a, const PadState& b);

class TickWriter {
public:
  void Begin(const PadState pads[kPorts], std::vector<std::string>& lines);
  void End(bool used, std::vector<std::string>& lines);
  std::string Flush();
  std::string PendingRun() const { return mRunCount == 0 ? "" : FormatTickRun(TickRun{mRunFrom, mRunCount}); }
  uint32_t ticks() const { return mTicks; }
  bool started() const { return mStarted; }

private:
  ChangeFilter mFilter;
  uint32_t mTicks = 0, mRunFrom = 0, mRunCount = 0;
  bool mStarted = false;
};

struct AnchorMismatch {
  uint32_t anchor = 0;
  std::string field, expected, actual;
  std::string Describe() const;
};

class TickPlayer {
public:
  enum class Mode { Strict, Resync };
  struct Stats {
    uint64_t early = 0, late = 0, phantom = 0, overflow = 0, unused = 0, resyncs = 0;
    uint64_t skipped = 0, extra = 0;
  };
  struct Check {
    bool ok = true;
    AnchorMismatch mismatch;
    bool restoreSeed = false;
    uint32_t seed = 0;
    const Anchor* recorded = nullptr;
    bool beyond = false;
    bool extra = false;
  };

  TickPlayer(const Recording& recording, Mode mode);
  bool Usable(std::string* why) const;
  const PadState* Next();
  const PadState* Current() const;
  void EndTick(bool used);
  Check OnAnchor(AnchorKind kind, uint32_t id, const AnchorState& actual);
  bool Finished() const;
  bool TakeMissing(AnchorMismatch* out);
  static constexpr uint32_t kSearchAhead = 6;
  static constexpr uint32_t kLateTicks = 600;
  uint32_t segment() const { return mSegment; }
  uint32_t segmentLength() const { return SegEnd(mSegment) - SegBegin(mSegment); }
  uint32_t anchors() const { return static_cast<uint32_t>(mAnchors.size()); }
  const std::vector<Anchor>& recordedAnchors() const { return mAnchors; }
  uint32_t ticks() const { return mTicks; }
  uint32_t position() const { return mPos; }
  const Stats& stats() const { return mStats; }
  Mode mode() const { return mMode; }

private:
  uint32_t SegBegin(uint32_t segment) const;
  uint32_t SegEnd(uint32_t segment) const;
  bool Used(uint32_t tick) const { return tick < mUsed.size() && mUsed[tick]; }
  const PadState* SampleAt(uint32_t tick) const;
  uint32_t NextUsed(uint32_t from) const;

  std::vector<Anchor> mAnchors;
  bool mFormat2 = false;
  Mode mMode;
  uint32_t mTotal = 0;
  std::vector<uint32_t> mSampleOf;
  std::vector<PadState> mSamples;
  std::vector<bool> mUsed;
  uint32_t mSegment = 0, mPos = 0, mTicks = 0;
  uint32_t mWaitSegment = UINT32_MAX, mWaitFrom = 0, mWaitEnd = 0, mWaitHeld = 0;
  uint32_t mFed = 0, mLastFed = 0, mHoldSample = 0;
  bool mHold = false, mFedEntry = false, mEndedEarly = false, mStarted = false;
  Stats mStats;
  struct Pending {
    uint32_t index;
    uint32_t deadline;
  };
  std::vector<Pending> mPending;
  struct Carried {
    bool on = false;
    uint64_t expected = 0, actual = 0;
  };
  Carried mPartyDiff, mProgressDiff;
  Check Compare(const Anchor& expected, AnchorKind kind, uint32_t id, const AnchorState& actual);
};

}
