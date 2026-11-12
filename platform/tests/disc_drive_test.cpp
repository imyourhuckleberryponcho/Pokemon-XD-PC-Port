#include "../sdk/disc_drive.h"

#include <cmath>
#include <cstdio>

namespace {
int gFailures = 0;
void check(bool ok, const char* what) {
  std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
  if (!ok) gFailures++;
}
}

int main() {
  using port_disc::DriveModel;
  constexpr uint64_t kLatency = 600 * 486;

  DriveModel fast(true);
  check(fast.Read(0, 0x206083a0, 0x20) == kLatency + 32ull * 486000000 / 33554432, "buffered: 32 bytes");
  check(fast.Read(123456789, 0x10000000, 0x8000) == kLatency + 474609, "buffered: one aligned block");
  const uint64_t four = kLatency + 0x7f00ull * 486000000 / 33554432 + 2 * (0x8000ull * 486000000 / 33554432) +
                        0x100ull * 486000000 / 33554432;
  check(fast.Read(0, 0x10000100, 0x18000) == four, "buffered: four blocks, unaligned");
  check(DriveModel::Seek() == 300 * 486, "seek command: 300 us");

  check(std::fabs(DriveModel::PhysicalPosition(0) - 0.024) < 1e-12, "position of the first byte: 24 mm");
  check(std::fabs(DriveModel::SeekTime(0, 0) - 0.035) < 1e-12, "seek of no distance: 35 ms (short seek)");
  const double longSeek = DriveModel::SeekTime(0, 0x40000000);
  check(longSeek > 0.075 && longSeek < 0.2, "a long seek: 75 ms + 4.5 s/m");
  const double inner = 0x8000 / DriveModel::RawReadTime(0, 0x8000) / (1024 * 1024);
  check(inner > 2.1 && inner < 2.11, "read speed at the centre: ~2.1 MiB/s");
  check(DriveModel::RotationalLatency(0, 0.0) >= 0 && DriveModel::RotationalLatency(0, 0.0) < 1 / 28.5,
        "rotational wait: under one turn");

  DriveModel drive(false);
  const uint64_t off = 0x10000000;
  const uint64_t first = drive.Read(0, off, 0x8000);
  check(drive.lastSeek && drive.lastUnbuffered == 1, "accurate: first read seeks");
  const double seekPart = 486000000.0 * DriveModel::SeekTime(0, off);
  check(first >= kLatency + static_cast<uint64_t>(seekPart) &&
            first < kLatency + static_cast<uint64_t>(seekPart) + 486000000 / 28 + 2,
        "accurate: first read = latency + seek + rotational wait");
  const uint64_t second = drive.Read(first, off + 0x8000, 0x8000);
  check(!drive.lastSeek && drive.lastUnbuffered == 1 &&
            second == kLatency + static_cast<uint64_t>(486000000.0 * DriveModel::RawReadTime(off + 0x8000, 0x8000)),
        "accurate: the next block at once: from the disc, no seek");
  const uint64_t third = drive.Read(first + second + 486000000, off + 0x10000, 0x8000);
  check(drive.lastBuffered == 1 && third == kLatency + 474609, "accurate: a second later: from the read-ahead buffer");
  drive.Read(first + second + 2 * 486000000ull, 0x1000000, 0x8000);
  check(drive.lastSeek, "accurate: reading before the buffer seeks again");

  std::printf("%s\n", gFailures == 0 ? "all passed" : "FAILURES");
  return gFailures == 0 ? 0 : 1;
}
