#include "disc_drive.h"

#include <algorithm>
#include <cmath>

namespace port_disc {
namespace {

constexpr uint64_t kReadLatencyUs = 600;
constexpr uint64_t kStreamingBufferSize = 1024 * 1024;
constexpr uint64_t kEccBlock = 16 * 0x800;
constexpr uint64_t kBufferTransferRate = 32 * 1024 * 1024;
constexpr uint64_t kBackwardSeekLimit = kEccBlock * 2;

constexpr uint64_t kWiiLayerSize = 0x118240000ull;
constexpr double kInnerRadius = 0.024;
constexpr double kWiiOuterRadius = 0.058;
constexpr double kGcOuterRadius = 0.038;
constexpr double kTrackPitch = 0.00000074;
constexpr double kGcInnerSpeed = 1024 * 1024 * 2.1;
constexpr double kGcOuterSpeed = 1024 * 1024 * 3.325;
constexpr double kGcRotationsPerSecond = 28.5;
constexpr double kShortSeekMax = 0.001;
constexpr double kShortSeekConstant = 0.035;
constexpr double kShortSeekVelocityInverse = 50;
constexpr double kLongSeekConstant = 0.075;
constexpr double kLongSeekVelocityInverse = 4.5;

uint64_t align_down(uint64_t v, uint64_t a) { return v - v % a; }
uint64_t align_up(uint64_t v, uint64_t a) { return align_down(v + a - 1, a); }

}

double DriveModel::PhysicalPosition(uint64_t offset) {
  offset %= kWiiLayerSize * 2;
  if (offset > kWiiLayerSize) offset = kWiiLayerSize * 2 - offset;
  return std::sqrt(static_cast<double>(offset) / kWiiLayerSize *
                       (kWiiOuterRadius * kWiiOuterRadius - kInnerRadius * kInnerRadius) +
                   kInnerRadius * kInnerRadius);
}

double DriveModel::SeekTime(uint64_t from, uint64_t to) {
  const double distance = std::fabs(PhysicalPosition(from) - PhysicalPosition(to));
  if (distance < kShortSeekMax) return distance * kShortSeekVelocityInverse + kShortSeekConstant;
  return distance * kLongSeekVelocityInverse + kLongSeekConstant;
}

double DriveModel::RotationalLatency(uint64_t offset, double time) {
  constexpr double kMaxAngle = 1;
  const double target = std::fmod(PhysicalPosition(offset) / kTrackPitch * kMaxAngle, kMaxAngle);
  const double start = std::fmod(time * kGcRotationsPerSecond * kMaxAngle, kMaxAngle);
  const double diff = std::fmod(target + kMaxAngle - start, kMaxAngle);
  return diff / kMaxAngle / kGcRotationsPerSecond;
}

double DriveModel::RawReadTime(uint64_t offset, uint64_t length) {
  const double position = PhysicalPosition(offset + length / 2);
  const double speed =
      (position - kInnerRadius) / (kGcOuterRadius - kInnerRadius) * (kGcOuterSpeed - kGcInnerSpeed) + kGcInnerSpeed;
  return length / speed;
}

uint64_t DriveModel::Read(uint64_t now, uint64_t offset, uint32_t length) {
  const uint64_t tps = kCpuHz;
  bool seek = false;
  uint64_t head = 0, bufferStart = 0, bufferEnd = 0;
  uint64_t dvdOffset = align_down(offset, kEccBlock);
  const uint64_t firstBlock = dvdOffset;

  if (buffered_) {
    bufferStart = 0;
    bufferEnd = UINT64_MAX;
    head = 0;
  } else if (bufStartTime_ == bufEndTime_) {
    bufferStart = bufferEnd = head = 0;
  } else {
    bufferStart = bufEndOffset_ > kStreamingBufferSize ? bufEndOffset_ - kStreamingBufferSize : 0;
    if (now >= bufEndTime_) {
      bufferEnd = bufEndOffset_;
    } else {
      bufferEnd = bufStartOffset_ + align_down((now - bufStartTime_) * (bufEndOffset_ - bufStartOffset_) /
                                                   (bufEndTime_ - bufStartTime_),
                                               kEccBlock);
    }
    head = bufferEnd;
    if (dvdOffset < bufferStart) bufferStart = bufferEnd = 0;
  }

  int64_t ticks = static_cast<int64_t>(kReadLatencyUs * (tps / 1000000));
  lastBuffered = lastUnbuffered = 0;
  do {
    uint32_t chunk = static_cast<uint32_t>(align_up(offset + 1, kEccBlock) - offset);
    chunk = std::min(chunk, length);
    if (dvdOffset >= bufferStart && dvdOffset < bufferEnd) {
      ticks += static_cast<int64_t>(static_cast<uint64_t>(chunk) * tps / kBufferTransferRate);
      lastBuffered++;
    } else {
      if (dvdOffset != head) {
        seek = true;
        ticks += static_cast<int64_t>(static_cast<double>(tps) * SeekTime(head, dvdOffset));
        const double timeAfterSeek = static_cast<double>((now + static_cast<uint64_t>(ticks)) / tps);
        ticks = static_cast<int64_t>(static_cast<double>(ticks) +
                                     static_cast<double>(tps) * RotationalLatency(dvdOffset, timeAfterSeek));
      } else {
        ticks += static_cast<int64_t>(static_cast<double>(tps) * RawReadTime(dvdOffset, kEccBlock));
      }
      lastUnbuffered++;
      head = dvdOffset + kEccBlock;
    }
    offset += chunk;
    length -= chunk;
    dvdOffset += kEccBlock;
  } while (length > 0);
  lastSeek = seek;

  if (!buffered_) {
    const uint64_t lastBlock = dvdOffset;
    if (lastBlock - bufferStart <= kBackwardSeekLimit && bufferStart != bufferEnd) {
    } else {
      bufStartOffset_ = lastBlock >= bufferEnd ? lastBlock : bufferEnd;
      bufEndOffset_ = lastBlock + kStreamingBufferSize - kBackwardSeekLimit;
      if (seek) bufEndOffset_ = std::max(bufEndOffset_, firstBlock + kStreamingBufferSize);
      bufStartTime_ = now + static_cast<uint64_t>(ticks);
      bufEndTime_ = bufStartTime_ +
                    static_cast<uint64_t>(static_cast<double>(tps) *
                                          RawReadTime(bufStartOffset_, bufEndOffset_ - bufStartOffset_));
    }
  }
  return static_cast<uint64_t>(ticks);
}

}
