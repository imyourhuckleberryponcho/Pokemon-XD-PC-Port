#include "../input/input_anchors.h"
#include "../input/input_record_format.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace port_input_record;

namespace {
bool ok = true;
void check(bool cond, const char* what) {
  std::printf("%s %s\n", cond ? "ok  " : "FAIL", what);
  ok &= cond;
}

PadState Pad(uint16_t buttons, int8_t sx = 0, int8_t sy = 0) {
  PadState p;
  p.err = kErrNone;
  p.button = buttons;
  p.stickX = sx;
  p.stickY = sy;
  return p;
}
PadState NoPad() { return PadState{}; }
}

int main() {
  {
    PadState s = Pad(0x1100, -128, 127);
    s.substickX = 5, s.substickY = -7, s.triggerL = 255, s.triggerR = 30;
    Change c{{812, 1}, 1630, 0, s};
    const std::string line = FormatChange(c);
    check(line == "frame 812 poll 1 field 1630 P1 buttons A+START stick -128 127 cstick 5 -7 triggers 255 30",
          "a change is written as expected");
    Recording r;
    check(ParseLine(line, r) && r.changes.size() == 1, "and reads back");
    const Change& back = r.changes[0];
    check(back.when.frame == 812 && back.when.poll == 1 && back.field == 1630 && back.port == 0 && back.state == s,
          "with the same values");
  }
  {
    IrqWriter writer;
    const IrqEntry a[] = {{5, 1, 1, 'i'}, {5, 2, 1, 'j'}, {9, 1, 1, 'p'}, {9, 3, 7, 'w'}, {12, 3, 8, 'b'}};
    const IrqEntry b[] = {{12, 3, 8, 'p'}, {40, 2, 3, 'i'}};
    const std::string la = writer.Line(a, 5), lb = writer.Line(b, 2);
    check(la == "IRQ 5:1+i 0:2+j 4:1 0:3.7w 3:3+b" && lb == "IRQ 0:3 28:2.3i", "interrupt deliveries are written as expected");
    check(writer.Line(a, 0).empty(), "no deliveries, no line");
    Recording r;
    check(ParseLine(FormatIrqSite({1, true, 0x1234}), r) && ParseLine("IRQSITE 2 raise at 0xabc", r) &&
              r.irqSites.size() == 2 && r.irqSites[0].alarm && r.irqSites[0].address == 0x1234 && !r.irqSites[1].alarm,
          "interrupt sites read back");
    check(ParseLine(la, r) && ParseLine(lb, r) && r.irq.size() == 7, "and the deliveries");
    bool same = r.irq.size() == 7;
    for (size_t i = 0; same && i < 7; i++) {
      const IrqEntry& want = i < 5 ? a[i] : b[i - 5];
      same = r.irq[i].count == want.count && r.irq[i].site == want.site && r.irq[i].seq == want.seq &&
             r.irq[i].kind == want.kind;
    }
    check(same, "with the same values");
    check(!ParseLine("IRQ 3:1 x:2", r) && r.irq.size() == 7 && !ParseLine("IRQ 1:2q", r) && r.irq.size() == 7,
          "a broken line adds nothing");
  }
  {
    PadState s = Pad(0x0080 | 0x0001);
    s.analogA = 12, s.analogB = 200, s.extButton = 0x2000, s.err = -3;
    Change c{{1, 0}, 2, 3, s};
    const std::string line = FormatChange(c);
    Recording r;
    check(ParseLine(line, r) && r.changes.size() == 1 && r.changes[0].state == s && r.changes[0].port == 3,
          "unnamed buttons, analog, ext and err round-trip");
    std::printf("     (%s)\n", line.c_str());
    Change none{{0, 0}, 1, 1, NoPad()};
    check(FormatChange(none) == "frame 0 poll 0 field 1 P2 none", "no controller is 'none'");
    Recording r2;
    check(ParseLine(FormatChange(none), r2) && r2.changes[0].state == NoPad(), "and reads back");
  }
  {
    ChangeFilter f;
    check(f.Changed(0, Pad(0)) && f.Changed(1, NoPad()), "first state of each controller is written");
    check(!f.Changed(0, Pad(0)) && !f.Changed(1, NoPad()), "the same state again is not");
    check(f.Changed(0, Pad(0x100)), "a button press is");
    check(f.Changed(0, Pad(0x100, 1)), "a stick move is");
    check(!f.Changed(1, NoPad()), "other controllers are independent");
  }
  std::string text;
  text += "# Input recording\n# disc F:\\some.iso\n";
  text += FormatChange({{0, 0}, 1, 0, Pad(0)}) + "\n";
  for (int p = 1; p < 4; p++) text += FormatChange({{0, 0}, 1, p, NoPad()}) + "\n";
  text += FormatChange({{10, 2}, 21, 0, Pad(0x1000)}) + "\r\n";
  text += FormatMark({11, 22, 1}) + "\n";
  text += "garbage line\n";
  text += FormatChange({{12, 0}, 24, 0, Pad(0)}) + "\n";
  text += FormatChange({{12, 1}, 25, 1, Pad(0x0100)}) + "\n";
  text += FormatMark({15, 30, 2}) + "\n";
  text += FormatEnd(20, 40) + "\n";
  std::string problems;
  Recording rec = ParseRecording(text, &problems);
  check(rec.header.size() == 2 && rec.header[1] == "disc F:\\some.iso", "header lines");
  check(rec.changes.size() == 7, "all change lines");
  check(rec.marks.size() == 2 && rec.marks[0].frame == 11 && rec.marks[0].field == 22 && rec.marks[1].number == 2,
        "marks");
  check(rec.hasEnd && rec.endFrame == 20 && rec.endField == 40, "END line");
  check(problems.find("line 9 not understood: garbage line") != std::string::npos, "bad line reported");
  check(FormatMark({11, 22, 1}) == "MARK frame 11 field 22 (#1)", "mark text");
  {
    const std::string line = FormatDevMenu({30, 61, "warp 0x64 0"});
    check(line == "DEVMENU frame 30 field 61 warp 0x64 0", "developer-menu line text");
    Recording d;
    check(ParseLine(line, d) && d.devMenu.size() == 1 && d.devMenu[0].frame == 30 && d.devMenu[0].action == "warp 0x64 0",
          "developer-menu line reads back");
    check(!ParseLine("DEVMENU frame 30 field 61", d), "a developer-menu line needs an action");
  }
  {
    const std::string line = FormatPhoto(40, 81, "off (2 photos)");
    check(line == "PHOTO frame 40 field 81 off (2 photos)", "photo-mode line text");
    Recording p;
    check(ParseLine(line, p) && p.changes.empty() && p.marks.empty() && p.devMenu.empty(), "photo-mode line skipped");
  }

  {
    Player player(rec);
    check(player.Current(0) == Pad(0) && player.Current(1) == NoPad(),
          "plugged-in controllers known before the first read");
    PadState pads[kPorts];
    player.Apply({0, 0}, pads);
    check(pads[0] == Pad(0) && pads[1] == NoPad() && pads[3] == NoPad(), "starting state");
    player.Apply({10, 1}, pads);
    check(pads[0] == Pad(0), "not yet at frame 10 poll 1");
    player.Apply({10, 2}, pads);
    check(pads[0] == Pad(0x1000), "Start at frame 10 poll 2");
    check(player.MarksReached(10).empty() && player.MarksReached(11).size() == 1, "mark 1 reached at frame 11");
    check(player.MarksReached(11).empty(), "and reported once");
    player.Apply({12, 0}, pads);
    check(pads[0] == Pad(0) && pads[1] == NoPad(), "frame 12 poll 0 only");
    player.Apply({13, 0}, pads);
    check(pads[1] == Pad(0x0100), "frame 12 poll 1 delivered late, not lost");
    check(player.Current(1) == Pad(0x0100), "controller 2 now plugged in");
    check(!player.Finished({19, 5}), "not finished before END");
    check(player.Finished({20, 0}), "finished at END");
    check(player.applied() == rec.changes.size(), "every change applied");
  }
  {
    const std::string cut = text.substr(0, text.find("frame 12 poll 1") + 20);
    std::string cutProblems;
    Recording r = ParseRecording(cut, &cutProblems);
    check(!r.hasEnd && r.changes.size() == 6, "cut-off file: whole lines kept");
    Player player(r);
    PadState pads[kPorts];
    player.Apply({0, 0}, pads);
    check(!player.Finished({1, 0}), "not finished while changes remain");
    player.Apply({12, 0}, pads);
    check(player.Finished({12, 0}), "finished after its last change");
  }
  {
    Player player(rec);
    PadState pads[kPorts];
    player.Apply({11, 0}, pads);
    check(pads[0] == Pad(0x1000) && pads[1] == NoPad(), "late start applies everything up to now");
  }

  {
    check(rec.clockReads.empty() && rec.clockSites.empty(), "old recording: no clock reads");
    ClockPlayer clock(rec);
    check(!clock.HasData(), "nothing to replay");
    check(clock.Read(0x1000, 0, 123456789) == 123456789 && clock.Read(0x2000, 5, 987654321) == 987654321,
          "the live clock is handed out unchanged");
    clock.GoLive(5, "test");
    check(clock.Read(0x1000, 6, 1111) == 1111 && clock.offset() == 0, "also after the replay ends");
    check(clock.TakeNote().empty(), "and nothing to say about it");
  }
  const uint64_t kSiteA = 0x1a2b30, kSiteB = 0x1a2c00, kSiteC = 0x2f0010;
  const int64_t t0 = 812345678901234LL;
  std::string clockText;
  {
    ClockWriter w;
    std::string decl;
    std::vector<std::string> lines;
    auto add = [&](uint32_t frame, uint64_t address, int64_t ticks) {
      const uint32_t site = w.Site(address, &decl);
      if (!decl.empty()) lines.push_back(decl);
      const std::string done = w.Add(frame, site, ticks);
      if (!done.empty()) lines.push_back(done);
      return site;
    };
    check(add(0, kSiteA, t0) == 1, "first place is site 1");
    check(decl == "TIME site 1 at 0x1a2b30", "with its address");
    check(add(0, kSiteB, t0 + 9312) == 2 && add(0, kSiteA, t0 + 9400) == 1, "a new place is site 2, site 1 stays 1");
    check(decl.empty(), "a known site isn't declared again");
    check(lines.size() == 2, "nothing written until the frame is over");
    add(1, kSiteB, t0 + 683000);
    check(lines.size() == 3 && lines[2] == "TIME frame 0 1 812345678901234 2 +9312 1 +88",
          "frame 0's reads in one line, differences after the first value");
    add(1, kSiteA, t0 + 683100);
    add(1, kSiteC, t0 + 683050);
    add(3, kSiteB, t0 + 2 * 683000);
    lines.push_back(w.Flush());
    check(w.Flush().empty(), "flushed once");
    check(lines[3] == "TIME site 3 at 0x2f0010" && lines[4] == "TIME frame 1 2 +673600 1 +100 3 -50",
          "negative differences");
    std::printf("     (%s)\n     (%s)\n", lines[4].c_str(), lines[5].c_str());
    for (const std::string& l : lines) clockText += l + "\n";
  }
  std::string withClock = text;
  withClock.insert(withClock.find("frame 10 poll 2"), clockText);
  Recording crec = ParseRecording(withClock, &problems);
  check(crec.changes.size() == rec.changes.size() && crec.marks.size() == 2 && crec.hasEnd,
        "controller lines read as before");
  check(crec.clockSites.size() == 3 && crec.clockSites[2].number == 3 && crec.clockSites[2].address == kSiteC,
        "clock sites read back");
  const int64_t expect[] = {t0, t0 + 9312, t0 + 9400, t0 + 683000, t0 + 683100, t0 + 683050, t0 + 2 * 683000};
  bool same = crec.clockReads.size() == 7;
  for (size_t i = 0; same && i < 7; i++) same = crec.clockReads[i].ticks == expect[i];
  check(same, "clock values read back exactly (differences added up)");
  check(crec.clockReads[3].frame == 1 && crec.clockReads[3].site == 2 && crec.clockReads[6].frame == 3,
        "with their frame and site");
  {
    Recording bad;
    check(!ParseLine("TIME frame 0 1 +5", bad), "a file can't start with a difference");
    check(!ParseLine("TIME frame 0 1", bad) && !ParseLine("TIME frame 0 0 5", bad) && !ParseLine("TIME site 0 at 0x1", bad),
          "malformed TIME lines are refused");
    check(ParseLine("TIME frame 0 1 100", bad) && !ParseLine("TIME frame 1 1 200", bad) &&
              ParseLine("TIME frame 1 1 +100", bad) && bad.clockReads.back().ticks == 200,
          "after the first value only differences");
  }
  {
    ClockPlayer clock(crec);
    check(clock.HasData(), "has clock data");
    const int64_t live = 5000;
    check(clock.Read(kSiteA, 0, live) == t0, "the seed read gets the recorded value");
    check(clock.TakeNote().find("same build") != std::string::npos, "same build detected");
    check(clock.Read(kSiteA, 0, live + 1) == t0 + 9400, "site 1's second read");
    check(clock.Read(kSiteB, 0, live + 2) == t0 + 9312, "site 2's first read, though it came later this time");
    check(clock.Read(kSiteC, 1, live + 3) == t0 + 683050 && clock.Read(kSiteB, 1, live + 4) == t0 + 683000 &&
              clock.Read(kSiteA, 1, live + 5) == t0 + 683100,
          "frame 1 in another order");
    check(clock.Read(kSiteB, 2, live + 6) == t0 + 2 * 683000, "a frame early is still the same read");
    check(clock.exact() == 7 && !clock.live(), "all seven as recorded");
    const int64_t next = clock.Read(kSiteB, 4, live + 100);
    check(clock.live() && next == t0 + 2 * 683000 + 1, "an extra read: live clock carries on just after");
    check(clock.Read(kSiteA, 4, live + 150) == next + 50, "and runs on at the PC's speed");
    check(clock.TakeNote().find("more often than recorded") != std::string::npos, "and says why");
  }
  {
    ClockPlayer clock(crec);
    check(clock.Read(0x9000, 0, 1) == t0 && clock.Read(0x9100, 0, 2) == t0 + 9312 && clock.Read(0x9000, 0, 3) == t0 + 9400,
          "other build: matched by order of first read");
    check(clock.TakeNote().find("another build") != std::string::npos, "another build detected");
    check(clock.Read(0x9200, 1, 4) == t0 + 683050, "third new place = site 3");
    const int64_t v = clock.Read(0x9300, 1, 10);
    check(clock.live() && v == t0 + 683050 + 1, "a fourth place the recording doesn't have: live from there");
  }
  {
    ClockPlayer clock(crec);
    check(clock.Read(kSiteA, 0, 1) == t0, "first read");
    const int64_t v = clock.Read(kSiteA, ClockPlayer::kFrameWindow + 1, 50);
    check(clock.live() && v == t0 + 1, "too far from the recorded frame: live clock");
  }
  {
    ClockPlayer clock(crec);
    clock.Read(kSiteA, 0, 1);
    clock.Read(kSiteB, 0, 2);
    clock.GoLive(1000, "replay finished at game frame 0");
    check(clock.Read(kSiteA, 0, 1000) == t0 + 9312 + 1 && clock.Read(kSiteA, 0, 1010) == t0 + 9312 + 11,
          "after the replay: live clock from just after the last value");
    check(clock.TakeNote().find("replay finished") != std::string::npos, "and says so");
  }
  {
    const std::string cut = withClock.substr(0, withClock.find("frame 10 poll 2"));
    Recording r = ParseRecording(cut, nullptr);
    check(!r.hasEnd && r.changes.size() == 4 && r.clockReads.back().frame == 3, "no END, clock reads up to frame 3");
    Player player(r);
    PadState pads[kPorts];
    player.Apply({1, 0}, pads);
    check(!player.Finished({2, 0}), "not finished before the last clock read");
    check(player.Finished({3, 0}), "finished at it");
  }
  {
    Anchor a;
    a.number = 4, a.kind = AnchorKind::Fade, a.id = 2, a.tick = 130, a.frame = 801;
    a.state = {0x1234abcd, 0x8f, 0x1122334455667788ull, 0x99ull};
    a.clock = {3, 1, 1, 90};
    const std::string line = FormatAnchor(a);
    Recording r;
    check(ParseLine(line, r) && r.anchors.size() == 1 && r.anchors[0].state == a.state && r.anchors[0].clock == a.clock &&
              r.anchors[0].kind == AnchorKind::Fade && r.anchors[0].tick == 130 && r.anchors[0].number == 4,
          "an ANCHOR line reads back");
    check(ParseLine("FORMAT 2", r) && r.format == 2 && ParseLine("TICKS 9000", r) && r.hasTicks && r.tickCount == 9000,
          "FORMAT and TICKS lines");
    check(ParseLine("TICK 120 P1 " + FormatPadState(Pad(0x0100)), r) && r.tickChanges.size() == 1 &&
              r.tickChanges[0].state == Pad(0x0100) && ParseLine("USE 118 3", r) && r.uses[0].count == 3,
          "TICK and USE lines");
    check(!ParseLine("USE 5 0", r), "an empty USE run is refused");
  }
  {
    TickWriter w;
    std::vector<std::string> lines;
    PadState pads[kPorts] = {Pad(0), NoPad(), NoPad(), NoPad()};
    const bool used[] = {false, true, true, false, true};
    for (int t = 0; t < 5; t++) {
      pads[0] = Pad(t == 2 ? 0x0100 : 0);
      w.Begin(pads, lines);
      w.End(used[t], lines);
    }
    lines.push_back(w.Flush());
    std::string all;
    for (const auto& l : lines) all += l + "\n";
    check(all.find("TICK 0 P2 none") != std::string::npos && all.find("TICK 2 P1 buttons A") != std::string::npos &&
              all.find("TICK 3 P1 buttons -") != std::string::npos && all.find("USE 1 2\n") != std::string::npos &&
              all.find("USE 4 1\n") != std::string::npos && w.ticks() == 5,
          "ticks: the starting state, changes only, using runs");
  }
  auto makeRec = [&]() {
    std::string text = "FORMAT 2\n";
    text += "TICK 0 P1 " + FormatPadState(Pad(0)) + "\n";
    for (int p = 2; p <= 4; p++) text += "TICK 0 P" + std::to_string(p) + " none\n";
    text += "TICK 2 P1 " + FormatPadState(Pad(0x0400)) + "\nTICK 3 P1 " + FormatPadState(Pad(0)) + "\n";
    text += "TICK 6 P1 " + FormatPadState(Pad(0x0100)) + "\nTICK 8 P1 " + FormatPadState(Pad(0)) + "\n";
    text += "USE 6 4\n";
    Anchor a;
    a.number = 1, a.kind = AnchorKind::Menu, a.id = 0x33, a.tick = 10, a.state = {7, 0x8f, 1, 2};
    text += FormatAnchor(a) + "\n";
    text += "TICK 12 P1 " + FormatPadState(Pad(0x0200)) + "\nTICK 13 P1 " + FormatPadState(Pad(0)) + "\n";
    text += "USE 12 2\nTICKS 20\n";
    return ParseRecording(text, nullptr);
  };
  const Recording trec = makeRec();
  const AnchorState kSame{7, 0x8f, 1, 2};
  {
    TickPlayer p(trec, TickPlayer::Mode::Strict);
    check(p.Usable(nullptr), "a format-2 recording is usable");
    bool good = true;
    for (uint32_t t = 0; t < 10; t++) {
      const uint16_t want = (t >= 6 && t <= 7) ? 0x0100 : 0;
      good &= p.Next()[0].button == want;
      p.EndTick(t >= 6);
    }
    check(good, "segment 0 at equal timing: A at the recorded reads, the press in the load not replayed");
    const TickPlayer::Check c = p.OnAnchor(AnchorKind::Menu, 0x33, kSame);
    check(c.ok && c.recorded != nullptr && c.recorded->number == 1, "the anchor matches");
    good = true;
    for (uint32_t t = 10; t < 20; t++) {
      good &= p.Next()[0].button == (t == 12 ? 0x0200 : 0);
      p.EndTick(t == 12 || t == 13);
    }
    check(good && p.Finished(), "segment 1, then finished");
  }
  {
    TickPlayer p(trec, TickPlayer::Mode::Resync);
    p.Next();
    p.EndTick(false);
    p.Next();
    p.EndTick(false);
    const uint16_t first = p.Next()[0].button;
    p.EndTick(true);
    const uint16_t second = p.Next()[0].button;
    check(first == 0 && second == 0x0100 && p.stats().early == 1 && p.stats().phantom == 1,
          "shorter wait: the recorded read follows on the next tick (its press edge kept)");
  }
  {
    TickPlayer p(trec, TickPlayer::Mode::Resync);
    for (int t = 0; t < 6; t++) {
      p.Next();
      p.EndTick(false);
    }
    const uint16_t a = p.Next()[0].button;
    p.EndTick(false);
    const uint16_t held = p.Next()[0].button;
    p.EndTick(true);
    const uint16_t now = p.Next()[0].button;
    check(a == 0x0100 && held == 0 && now == 0x0100 && p.stats().late == 1,
          "longer wait: the state before the read is held until the game reads, then the press");
  }
  {
    TickPlayer s(trec, TickPlayer::Mode::Strict), r(trec, TickPlayer::Mode::Resync);
    AnchorState other = kSame;
    other.seed = 8;
    const TickPlayer::Check cs = s.OnAnchor(AnchorKind::Menu, 0x33, other);
    const TickPlayer::Check cr = r.OnAnchor(AnchorKind::Menu, 0x33, other);
    check(!cs.ok && cs.mismatch.field == "seed" && cs.mismatch.Describe().find("anchor 1 field seed") == 0,
          "strict: a different random state at anchor 1 is reported as 'anchor 1 field seed'");
    check(cr.ok && cr.restoreSeed && cr.seed == 7, "resync: the recorded random state is put back");
    TickPlayer r2(trec, TickPlayer::Mode::Resync);
    other = kSame;
    other.party = 5;
    check(!r2.OnAnchor(AnchorKind::Menu, 0x33, other).ok, "resync: a different party is still a difference");
  }
  {
    TickPlayer r(trec, TickPlayer::Mode::Resync);
    const TickPlayer::Check extra = r.OnAnchor(AnchorKind::Fade, 3, kSame);
    check(extra.ok && extra.extra && r.segment() == 0, "resync: an extra fade is ignored, the cursor stays");
    check(!r.OnAnchor(AnchorKind::Floor, 0x90, kSame).ok, "resync: an extra map change is a difference");
    TickPlayer s(trec, TickPlayer::Mode::Strict);
    check(!s.OnAnchor(AnchorKind::Fade, 3, kSame).ok, "strict: an extra fade is a difference");
  }
  std::printf(ok ? "all passed\n" : "FAILED\n");
  return ok ? 0 : 1;
}
