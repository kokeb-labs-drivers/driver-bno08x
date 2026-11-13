/**
 * @file bno08x_rvc.hpp
 * @brief The UART-RVC frame parser (DS p20 to p21): a byte scanner that finds 19-byte frames,
 * checks the checksum, skips the start-up banner and slides over false synchronisation.
 * @copyright Copyright (c) 2026 The driver-bno08x authors. SPDX-License-Identifier: MIT
 */
#ifndef BNO08X_RVC_HPP
#define BNO08X_RVC_HPP

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "bno08x_detail.hpp"
#include "bno08x_registers.hpp"
#include "bno08x_shtp.hpp"
#include "bno08x_types.hpp"

namespace bno08x::detail {

/// Frame header byte, sent twice (DS p21).
inline constexpr uint8_t kRvcHeader = 0xAA;

/// Low eight bits of the sum of `bytes[first, last]` inclusive.
[[nodiscard]] constexpr uint8_t RvcSum(std::span<const uint8_t> bytes, std::size_t first,
                                       std::size_t last) noexcept {
  uint32_t sum = 0;
  for (std::size_t i = first; i <= last; ++i) {
    sum += static_cast<uint32_t>(bytes[i]);
  }
  return static_cast<uint8_t>(sum & 0xFFU);
}

/// Which checksum rule matched a frame (ambiguity A6).
enum class RvcSumRule : uint8_t {
  None,        ///< The checksum does not match either rule.
  WithMiMr,    ///< Sum of bytes 2 to 17 (index through the reserved byte), as the data sheet says.
  WithoutMiMr, ///< Sum of bytes 2 to 14 plus byte 17 (the manual's list omits MI and MR).
};

/// Checks a 19-byte frame.
[[nodiscard]] constexpr RvcSumRule RvcCheck(std::span<const uint8_t> frame) noexcept {
  const uint8_t want = frame[18];
  if (RvcSum(frame, 2U, 17U) == want) {
    return RvcSumRule::WithMiMr;
  }
  const auto alt = static_cast<uint8_t>(
      (static_cast<uint32_t>(RvcSum(frame, 2U, 14U)) + static_cast<uint32_t>(frame[17])) & 0xFFU);
  return (alt == want) ? RvcSumRule::WithoutMiMr : RvcSumRule::None;
}

/// Decodes the fields of a checked frame.
[[nodiscard]] constexpr RvcFrame RvcDecode(std::span<const uint8_t> frame, bool mi_mr) noexcept {
  RvcFrame f{};
  f.index = frame[2];
  f.yaw = static_cast<int16_t>(S16(Le16(frame, 3U)));
  f.pitch = static_cast<int16_t>(S16(Le16(frame, 5U)));
  f.roll = static_cast<int16_t>(S16(Le16(frame, 7U)));
  f.ax = static_cast<int16_t>(S16(Le16(frame, 9U)));
  f.ay = static_cast<int16_t>(S16(Le16(frame, 11U)));
  f.az = static_cast<int16_t>(S16(Le16(frame, 13U)));
  f.mi_mr_valid = mi_mr;
  if (mi_mr) {
    f.mi = frame[15];
    f.mr = frame[16];
  }
  return f;
}

/**
 * @brief Scans a byte stream for UART-RVC frames.
 *
 * Bytes before the first `0xAA 0xAA` (the ASCII banner the hub prints after reset) are counted and
 * skipped. A frame with a bad checksum slides one byte, not the whole frame, so a real frame that
 * starts inside it is still found. With `strict` the index must increment by one.
 */
class RvcParser {
public:
  /// Counts into `stats`; `cfg` supplies `rvc_strict` and the variant.
  RvcParser(Stats& stats, const Config& cfg) noexcept : stats_(stats), cfg_(cfg) {}

  /// Forget everything (a new session).
  void Reset() noexcept {
    count_ = 0;
    have_index_ = false;
  }

  /// Feed one byte; returns true when `Frame()` holds a new frame.
  bool Feed(uint8_t b) noexcept {
    if ((count_ < 2U) && (b != kRvcHeader)) {
      count_ = 0;
      ++stats_.rvc_banner_bytes;
      return false;
    }
    buf_[count_] = b;
    ++count_;
    if (count_ < reg::kRvcFrameLen) {
      return false;
    }
    const RvcSumRule rule = RvcCheck(buf_);
    if (rule == RvcSumRule::None) {
      ++stats_.rvc_bad_csum;
      slide();
      return false;
    }
    count_ = 0;
    return accept(rule);
  }

  /// The last accepted frame.
  [[nodiscard]] const RvcFrame& Frame() const noexcept {
    return frame_;
  }

private:
  /// Drops the first byte and re-aligns on the next `0xAA 0xAA` inside the buffer.
  void slide() noexcept {
    std::size_t p = 1;
    while ((p + 1U) < reg::kRvcFrameLen) {
      if ((buf_[p] == kRvcHeader) && (buf_[p + 1U] == kRvcHeader)) {
        break;
      }
      ++p;
    }
    if ((p + 1U) >= reg::kRvcFrameLen) {
      // no header pair left: keep a trailing 0xAA, which may start the next one
      count_ = 0;
      if (buf_[reg::kRvcFrameLen - 1U] == kRvcHeader) {
        buf_[0] = kRvcHeader;
        count_ = 1;
      }
      return;
    }
    std::copy(buf_.begin() + static_cast<std::ptrdiff_t>(p), buf_.end(), buf_.begin());
    count_ = reg::kRvcFrameLen - p;
  }

  /// A frame whose checksum matched: index continuity, ranges, decode.
  bool accept(RvcSumRule rule) noexcept {
    const uint8_t index = buf_[2];
    const bool gap =
        have_index_ &&
        (index != static_cast<uint8_t>((static_cast<uint32_t>(last_index_) + 1U) & 0xFFU));
    if (gap) {
      ++stats_.rvc_index_gaps;
    }
    const bool had = have_index_;
    have_index_ = true;
    last_index_ = index;
    if (cfg_.rvc_strict && had && gap) {
      return false; // one frame is lost to prove continuity again
    }
    if (rule == RvcSumRule::WithMiMr) {
      ++stats_.rvc_csum_with_mi;
    } else {
      ++stats_.rvc_csum_without_mi;
    }
    frame_ = RvcDecode(buf_, cfg_.variant == Variant::Bno086);
    const bool range = (frame_.yaw < -18000) || (frame_.yaw > 18000) || (frame_.pitch < -9000) ||
                       (frame_.pitch > 9000) || (frame_.roll < -18000) || (frame_.roll > 18000);
    if (range) {
      ++stats_.rvc_out_of_range;
    }
    ++stats_.rvc_frames;
    return true;
  }

  Stats& stats_;
  const Config& cfg_;
  std::array<uint8_t, reg::kRvcFrameLen> buf_{};
  std::size_t count_{0};
  RvcFrame frame_{};
  uint8_t last_index_{0};
  bool have_index_{false};
};

} // namespace bno08x::detail

#endif // BNO08X_RVC_HPP
