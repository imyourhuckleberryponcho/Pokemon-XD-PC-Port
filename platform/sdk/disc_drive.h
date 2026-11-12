#pragma once

#include <cstdint>

namespace port_disc {

constexpr uint64_t kCpuHz = 486000000u;

class DriveModel {
public:
  explicit DriveModel(bool buffered = false) : buffered_(buffered) {}
  void SetBuffered(bool buffered) { buffered_ = buffered; }

  uint64_t Read(uint64_t now, uint64_t offset, uint32_t length);
  static uint64_t Seek() { return 300u * (kCpuHz / 1000000u); }

  uint32_t lastBuffered = 0, lastUnbuffered = 0;
  bool lastSeek = false;

  static double PhysicalPosition(uint64_t offset);
  static double SeekTime(uint64_t from, uint64_t to);
  static double RotationalLatency(uint64_t offset, double time);
  static double RawReadTime(uint64_t offset, uint64_t length);

private:
  bool buffered_;
  uint64_t bufStartTime_ = 0, bufEndTime_ = 0;
  uint64_t bufStartOffset_ = 0, bufEndOffset_ = 0;
};

}
