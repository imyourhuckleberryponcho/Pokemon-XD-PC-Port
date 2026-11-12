#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace port_input_record {

struct PadState {
  uint16_t button = 0;
  int8_t stickX = 0, stickY = 0, substickX = 0, substickY = 0;
  uint8_t triggerL = 0, triggerR = 0, analogA = 0, analogB = 0;
  int8_t err = -1;
  uint32_t extButton = 0;
  bool operator==(const PadState&) const = default;
};
constexpr int kPorts = 4;
constexpr int8_t kErrNone = 0, kErrNoController = -1;

struct When {
  uint32_t frame = 0, poll = 0;
  bool operator<=(const When& o) const { return frame < o.frame || (frame == o.frame && poll <= o.poll); }
};

struct Change {
  When when;
  uint32_t field = 0;
  int port = 0;
  PadState state;
};
struct Mark {
  uint32_t frame = 0, field = 0, number = 0;
};
struct DevMenuLine {
  uint32_t frame = 0, field = 0;
  std::string action;
};
struct ClockSite {
  uint32_t number = 0;
  uint64_t address = 0;
};
struct ClockRead {
  uint32_t frame = 0, site = 0;
  int64_t ticks = 0;
};
struct TickChange {
  uint32_t tick = 0;
  int port = 0;
  PadState state;
};
struct TickRun {
  uint32_t from = 0, count = 0;
};
enum class AnchorKind : uint8_t { Start = 0, Floor, Message, Battle, Menu, Fade, Movie, Save };
struct AnchorState {
  uint32_t seed = 0;
  uint32_t floor = 0;
  uint64_t party = 0;
  uint64_t progress = 0;
  bool operator==(const AnchorState&) const = default;
};
struct Anchor {
  uint32_t number = 0;
  AnchorKind kind = AnchorKind::Start;
  uint32_t id = 0;
  uint32_t tick = 0;
  uint32_t frame = 0;
  AnchorState state;
  std::vector<uint32_t> clock;
};

struct IrqSiteLine {
  uint32_t number = 0;
  bool alarm = false;
  uint64_t address = 0;
};
struct IrqEntry {
  uint64_t count = 0;
  uint32_t site = 0, seq = 0;
  char kind = 'p';
};
std::string FormatIrqSite(const IrqSiteLine& site);
class IrqWriter {
public:
  std::string Line(const IrqEntry* entries, size_t count);

private:
  uint64_t mLast = 0;
  std::vector<uint32_t> mSeq;
};

struct Recording {
  std::vector<std::string> header;
  std::vector<IrqSiteLine> irqSites;
  std::vector<IrqEntry> irq;
  uint64_t irqLast = 0;
  std::vector<uint32_t> irqSeq;
  uint32_t format = 1;
  std::vector<TickChange> tickChanges;
  std::vector<TickRun> uses;
  std::vector<Anchor> anchors;
  bool hasTicks = false;
  uint32_t tickCount = 0;
  std::vector<Change> changes;
  std::vector<Mark> marks;
  std::vector<DevMenuLine> devMenu;
  std::vector<DevMenuLine> settings;
  bool settingsSinceMenu = false;
  bool hasEnd = false;
  uint32_t endFrame = 0, endField = 0;
  std::vector<ClockSite> clockSites;
  std::vector<ClockRead> clockReads;
  bool clockStarted = false;
  std::vector<std::pair<uint32_t, uint32_t>> fsys;
};

struct FieldTimeline {
  uint32_t site = 0;
  std::vector<int32_t> fields;
};
constexpr int64_t kFieldTicks = 675675;
bool BuildFieldTimeline(const Recording& recording, FieldTimeline& out, std::string* why);

std::string FormatChange(const Change& change);
std::string FormatMark(const Mark& mark);
std::string FormatDevMenu(const DevMenuLine& line);
std::string FormatPhoto(uint32_t frame, uint32_t field, const std::string& what);
std::string FormatEnhanced(uint32_t frame, uint32_t field, const std::string& what);
std::string FormatMenu(uint32_t frame, uint32_t field, const std::string& what);
std::string FormatSettings(uint32_t frame, uint32_t field, const std::string& list);
std::string FormatPlaythrough(uint32_t frame, uint32_t field, const std::string& what);
std::string FormatEnd(uint32_t frame, uint32_t field);
std::string FormatClockSite(const ClockSite& site);
std::string FormatPadState(const PadState& state);
bool ParsePadState(const std::vector<std::string>& w, size_t i, PadState& state);

Recording ParseRecording(const std::string& text, std::string* problems);
bool ParseLine(const std::string& line, Recording& into);

class ChangeFilter {
public:
  bool Changed(int port, const PadState& state);

private:
  PadState mLast[kPorts];
  bool mSeen[kPorts] = {};
};

class Player {
public:
  explicit Player(Recording recording) : mRec(std::move(recording)) {}
  void Apply(When now, PadState out[kPorts]);
  const PadState& Current(int port);
  bool Finished(When now) const;
  std::vector<Mark> MarksReached(uint32_t frame);
  size_t applied() const { return mNext; }
  const Recording& recording() const { return mRec; }

private:
  void Start();
  Recording mRec;
  PadState mState[kPorts];
  size_t mNext = 0, mNextMark = 0;
  bool mStarted = false;
};

class ClockWriter {
public:
  uint32_t Site(uint64_t address, std::string* declaration);
  std::string Add(uint32_t frame, uint32_t site, int64_t ticks);
  std::string Flush();
  const std::string& Pending() const { return mLine; }
  const std::vector<uint32_t>& Counts() const { return mCounts; }

private:
  std::map<uint64_t, uint32_t> mSites;
  std::vector<uint32_t> mCounts;
  std::vector<int64_t> mSiteLast;
  std::string mLine;
  uint32_t mFrame = 0;
  int64_t mLast = 0;
  bool mStarted = false;
};

class ClockPlayer {
public:
  static constexpr uint32_t kFrameWindow = 600;
  explicit ClockPlayer(const Recording& recording);
  bool HasData() const { return !mSites.empty(); }
  int64_t Read(uint64_t address, uint32_t frame, int64_t live);
  void GoLive(int64_t live, const std::string& why);
  bool live() const { return mLive; }
  bool NextRead(uint32_t site, size_t* index) const {
    if (mLive || site == 0 || site > mSites.size()) return false;
    *index = mSites[site - 1].next;
    return *index < mSites[site - 1].reads.size();
  }
  uint64_t exact() const { return mExact; }
  std::string TakeNote();
  int64_t offset() const { return mOffset; }
  void Realign(const std::vector<uint32_t>& counts, int64_t frameShift);
  void SetLimit(const std::vector<uint32_t>& counts);
  bool OrderWait(uint64_t address, uint32_t timeline, size_t* need, size_t* have);

private:
  struct Site {
    uint64_t address = 0;
    std::vector<ClockRead> reads;
    std::vector<uint32_t> seq;
    size_t next = 0;
    int64_t extra = 0;
    int64_t liveBase = 0;
    int64_t offset = 0;
    int64_t last = INT64_MIN;
    int family = 0;
  };
  int64_t mFamilyLast[2] = {INT64_MIN, INT64_MIN};
  int Map(uint64_t address);
  void AddNote(const std::string& text);
  std::vector<Site> mSites;
  enum class Mode { Undecided, ByAddress, ByOrder } mMode = Mode::Undecided;
  std::map<uint64_t, int> mMap;
  size_t mNextByOrder = 0;
  bool mLive = false, mGiven = false, mRealigned = false;
  std::vector<size_t> mLimit;
  int64_t mOffset = 0, mMax = 0, mShift = 0;
  uint64_t mExact = 0;
  std::string mNote;
};

}
