/**
 * @file bno08x_records.hpp
 * @brief Typed codecs of the FRS records whose layout the documents give (RM S4), the 24 mounting
 * quaternions of the data sheet and the calibration procedure helper.
 * @copyright Copyright (c) 2026 The driver-bno08x authors. SPDX-License-Identifier: MIT
 *
 * @details
 * Every codec turns a record into its 32-bit words and back, with the Q points of the reference
 * manual. Records whose layout is proprietary (DCD, static calibration, simple calibration
 * configuration) or not yet transcribed (the detector configurations RM S4.3.13 to S4.3.20) stay
 * raw words: read and write them with `Bno08x::ReadFrs()` and `Bno08x::WriteFrs()`.
 */
#ifndef BNO08X_RECORDS_HPP
#define BNO08X_RECORDS_HPP

#include <array>
#include <cstddef>
#include <cstdint>

#include "bno08x_detail.hpp"
#include "bno08x_registers.hpp"
#include "bno08x_types.hpp"

namespace bno08x {

/** @brief A unit quaternion in Q30 (the orientation records, RM S4.1). */
struct Quat30 {
  int32_t x{0}; ///< X component, Q30.
  int32_t y{0}; ///< Y component, Q30.
  int32_t z{0}; ///< Z component, Q30.
  int32_t w{0}; ///< W component, Q30.
  /// Equality of all fields, as a hidden friend.
  friend constexpr bool operator==(const Quat30& lhs, const Quat30& rhs) noexcept = default;
};

/** @brief MotionEngine power management and stability classifier record 0xD3E2 (RM S4.3.1). */
struct PowerManagement {
  int32_t delta_orientation_q28{0}; ///< Stability: orientation change in radians, Q28.
  int32_t stable_threshold_q25{0};  ///< Stability: angular rate threshold in rad/s, Q25.
  uint8_t stable_duration_s{0};     ///< Stability: seconds the device must be still.
  uint8_t delta_accel_mg{0};        ///< Stability: acceleration change in milli-g.
  /// Equality of all fields, as a hidden friend.
  friend constexpr bool operator==(const PowerManagement& lhs,
                                   const PowerManagement& rhs) noexcept = default;
};

/** @brief AR/VR stabilisation records 0x3E2D and 0x3E2E (RM S4.2). */
struct ArvrStabilization {
  int32_t scaling_q30{0};      ///< Scaling, Q30.
  int32_t max_rotation_q29{0}; ///< Maximum rotation, Q29.
  int32_t max_error_q29{0};    ///< Maximum error, Q29.
  int32_t stability_q29{0};    ///< Stability magnitude, Q29.
  /// Equality of all fields, as a hidden friend.
  friend constexpr bool operator==(const ArvrStabilization& lhs,
                                   const ArvrStabilization& rhs) noexcept = default;
};

/** @brief Significant motion detector configuration record 0xC274. */
struct SignificantMotionConfig {
  int32_t accel_threshold_q24{0}; ///< Acceleration threshold in m/s2, Q24.
  uint32_t step_threshold{0};     ///< Step threshold.
  /// Equality of all fields, as a hidden friend.
  friend constexpr bool operator==(const SignificantMotionConfig& lhs,
                                   const SignificantMotionConfig& rhs) noexcept = default;
};

/** @brief Shake detector configuration record 0x7D7D. */
struct ShakeConfig {
  uint32_t min_time_us{0};  ///< Shortest shake.
  uint32_t max_time_us{0};  ///< Longest shake.
  int32_t threshold_q26{0}; ///< Acceleration threshold in m/s2, Q26.
  uint32_t count{0};        ///< Shakes needed.
  bool enable_x{false};     ///< Detect on X.
  bool enable_y{false};     ///< Detect on Y.
  bool enable_z{false};     ///< Detect on Z.
  /// Equality of all fields, as a hidden friend.
  friend constexpr bool operator==(const ShakeConfig& lhs,
                                   const ShakeConfig& rhs) noexcept = default;
};

/** @brief Environmental sensor calibration records 0x39AF, 0x4D20, 0x1AC9, 0x39B1, 0x4DA2. */
struct EnvCalibration {
  int32_t offset{0}; ///< Offset in the sensor's Q point (pressure Q20, temperature Q7, humidity Q8,
                     ///< light Q8, proximity Q4).
  int32_t scale_q15{0}; ///< Scale, Q15.
  /// Equality of all fields, as a hidden friend.
  friend constexpr bool operator==(const EnvCalibration& lhs,
                                   const EnvCalibration& rhs) noexcept = default;
};

/** @brief Gyro-integrated rotation vector configuration record 0xA1A2 (RM S4.3.12). */
struct GyroRvConfig {
  uint32_t reference_type{
      0x0207}; ///< Reference data type: 0x0207 game rotation vector, 0x0204 rotation vector.
  uint32_t sync_interval_us{10000}; ///< Synchronisation interval.
  int32_t max_error_q29{0};         ///< Maximum error, Q29 radians.
  int32_t prediction_q10{0};        ///< Prediction amount in seconds, Q10.
  int32_t alpha_q20{0};             ///< Alpha, Q20.
  int32_t beta_q20{0};              ///< Beta, Q20.
  int32_t gamma_q20{0};             ///< Gamma, Q20.
  /// Equality of all fields, as a hidden friend.
  friend constexpr bool operator==(const GyroRvConfig& lhs,
                                   const GyroRvConfig& rhs) noexcept = default;
};

/** @brief Words of each typed record. */
inline constexpr std::size_t kOrientationWords = 4;  ///< Orientation records.
inline constexpr std::size_t kPowerMgmtWords = 3;    ///< Record 0xD3E2.
inline constexpr std::size_t kArvrWords = 4;         ///< Records 0x3E2D and 0x3E2E.
inline constexpr std::size_t kSigMotionWords = 2;    ///< Record 0xC274.
inline constexpr std::size_t kShakeWords = 5;        ///< Record 0x7D7D.
inline constexpr std::size_t kEnvCalWords = 2;       ///< Environmental calibration records.
inline constexpr std::size_t kGyroRvConfigWords = 7; ///< Record 0xA1A2.

namespace detail {
/// A signed value as a record word.
[[nodiscard]] constexpr uint32_t W(int32_t v) noexcept {
  return static_cast<uint32_t>(v);
}
/// A record word as a signed value.
[[nodiscard]] constexpr int32_t I(uint32_t w) noexcept {
  return static_cast<int32_t>(static_cast<int64_t>(w ^ 0x8000'0000U) - 0x8000'0000LL);
}
} // namespace detail

/** @brief Words of an orientation record (x, y, z, w; RM S4.1). @param q The quaternion. @return
 * The words. */
[[nodiscard]] constexpr std::array<uint32_t, kOrientationWords> EncodeOrientation(
    const Quat30& q) noexcept {
  return {detail::W(q.x), detail::W(q.y), detail::W(q.z), detail::W(q.w)};
}
/** @brief The quaternion of an orientation record. @param w The words. @return The quaternion. */
[[nodiscard]] constexpr Quat30 DecodeOrientation(
    const std::array<uint32_t, kOrientationWords>& w) noexcept {
  return Quat30{
      .x = detail::I(w[0]), .y = detail::I(w[1]), .z = detail::I(w[2]), .w = detail::I(w[3])};
}
/** @brief Words of record 0xD3E2. @param p The record. @return The words. */
[[nodiscard]] constexpr std::array<uint32_t, kPowerMgmtWords> EncodePowerManagement(
    const PowerManagement& p) noexcept {
  const uint32_t packed =
      static_cast<uint32_t>(p.stable_duration_s) | (static_cast<uint32_t>(p.delta_accel_mg) << 8U);
  return {detail::W(p.delta_orientation_q28), detail::W(p.stable_threshold_q25), packed};
}
/** @brief The record 0xD3E2 of its words. @param w The words. @return The record. */
[[nodiscard]] constexpr PowerManagement DecodePowerManagement(
    const std::array<uint32_t, kPowerMgmtWords>& w) noexcept {
  return PowerManagement{.delta_orientation_q28 = detail::I(w[0]),
                         .stable_threshold_q25 = detail::I(w[1]),
                         .stable_duration_s = static_cast<uint8_t>(w[2] & 0xFFU),
                         .delta_accel_mg = static_cast<uint8_t>((w[2] >> 8U) & 0xFFU)};
}
/** @brief Words of an AR/VR stabilisation record. @param a The record. @return The words. */
[[nodiscard]] constexpr std::array<uint32_t, kArvrWords> EncodeArvr(
    const ArvrStabilization& a) noexcept {
  return {detail::W(a.scaling_q30), detail::W(a.max_rotation_q29), detail::W(a.max_error_q29),
          detail::W(a.stability_q29)};
}
/** @brief The AR/VR record of its words. @param w The words. @return The record. */
[[nodiscard]] constexpr ArvrStabilization DecodeArvr(
    const std::array<uint32_t, kArvrWords>& w) noexcept {
  return ArvrStabilization{.scaling_q30 = detail::I(w[0]),
                           .max_rotation_q29 = detail::I(w[1]),
                           .max_error_q29 = detail::I(w[2]),
                           .stability_q29 = detail::I(w[3])};
}
/** @brief Words of record 0xC274. @param s The record. @return The words. */
[[nodiscard]] constexpr std::array<uint32_t, kSigMotionWords> EncodeSignificantMotion(
    const SignificantMotionConfig& s) noexcept {
  return {detail::W(s.accel_threshold_q24), s.step_threshold};
}
/** @brief The record 0xC274 of its words. @param w The words. @return The record. */
[[nodiscard]] constexpr SignificantMotionConfig DecodeSignificantMotion(
    const std::array<uint32_t, kSigMotionWords>& w) noexcept {
  return SignificantMotionConfig{.accel_threshold_q24 = detail::I(w[0]), .step_threshold = w[1]};
}
/** @brief Words of record 0x7D7D (the enable word has bit 0 X, bit 1 Y, bit 2 Z). @param s The
 * record. @return The words. */
[[nodiscard]] constexpr std::array<uint32_t, kShakeWords> EncodeShake(
    const ShakeConfig& s) noexcept {
  const uint32_t flags = (s.enable_x ? 1U : 0U) | (s.enable_y ? 2U : 0U) | (s.enable_z ? 4U : 0U);
  return {s.min_time_us, s.max_time_us, detail::W(s.threshold_q26), s.count, flags};
}
/** @brief The record 0x7D7D of its words. @param w The words. @return The record. */
[[nodiscard]] constexpr ShakeConfig DecodeShake(
    const std::array<uint32_t, kShakeWords>& w) noexcept {
  return ShakeConfig{.min_time_us = w[0],
                     .max_time_us = w[1],
                     .threshold_q26 = detail::I(w[2]),
                     .count = w[3],
                     .enable_x = (w[4] & 1U) != 0U,
                     .enable_y = (w[4] & 2U) != 0U,
                     .enable_z = (w[4] & 4U) != 0U};
}
/** @brief Words of an environmental calibration record. @param e The record. @return The words. */
[[nodiscard]] constexpr std::array<uint32_t, kEnvCalWords> EncodeEnvCalibration(
    const EnvCalibration& e) noexcept {
  return {detail::W(e.offset), detail::W(e.scale_q15)};
}
/** @brief The environmental calibration of its words. @param w The words. @return The record. */
[[nodiscard]] constexpr EnvCalibration DecodeEnvCalibration(
    const std::array<uint32_t, kEnvCalWords>& w) noexcept {
  return EnvCalibration{.offset = detail::I(w[0]), .scale_q15 = detail::I(w[1])};
}
/** @brief Words of record 0xA1A2. @param g The record. @return The words. */
[[nodiscard]] constexpr std::array<uint32_t, kGyroRvConfigWords> EncodeGyroRvConfig(
    const GyroRvConfig& g) noexcept {
  return {g.reference_type,           g.sync_interval_us,
          detail::W(g.max_error_q29), detail::W(g.prediction_q10),
          detail::W(g.alpha_q20),     detail::W(g.beta_q20),
          detail::W(g.gamma_q20)};
}
/** @brief The record 0xA1A2 of its words. @param w The words. @return The record. */
[[nodiscard]] constexpr GyroRvConfig DecodeGyroRvConfig(
    const std::array<uint32_t, kGyroRvConfigWords>& w) noexcept {
  return GyroRvConfig{.reference_type = w[0],
                      .sync_interval_us = w[1],
                      .max_error_q29 = detail::I(w[2]),
                      .prediction_q10 = detail::I(w[3]),
                      .alpha_q20 = detail::I(w[4]),
                      .beta_q20 = detail::I(w[5]),
                      .gamma_q20 = detail::I(w[6])};
}

namespace detail {

/// Q30 value of a mounting code: 0, +-1, +-1/2 (code 2), +-sqrt(2)/2 (code 3).
constexpr int32_t MountQ30(int8_t code) noexcept {
  constexpr int32_t kOne = 1'073'741'824;
  constexpr int32_t kHalf = 536'870'912;
  constexpr int32_t kRoot = 759'250'125;
  const int32_t mag = (code < 0) ? -code : code;
  int32_t v = 0;
  if (mag == 1) {
    v = kOne;
  } else if (mag == 2) {
    v = kHalf;
  } else if (mag == 3) {
    v = kRoot;
  } else {
    // zero
  }
  return (code < 0) ? -v : v;
}

/// The 24 rows of DS Figure 4-3 as codes (Qw, Qx, Qy, Qz).
inline constexpr std::array<std::array<int8_t, 4>, 24> kMountCodes{{
    {{1, 0, 0, 0}},   {{3, 0, 0, 3}},    {{0, 0, 0, 1}},    {{3, 0, 0, -3}},    {{0, 0, -1, 0}},
    {{0, -3, -3, 0}}, {{0, -1, 0, 0}},   {{0, -3, 3, 0}},   {{0, 0, -3, 3}},    {{2, -2, -2, 2}},
    {{3, -3, 0, 0}},  {{2, -2, 2, -2}},  {{-3, -3, 0, 0}},  {{-2, -2, -2, -2}}, {{0, 0, -3, -3}},
    {{2, 2, -2, -2}}, {{-2, -2, -2, 2}}, {{0, -3, 0, 3}},   {{2, -2, 2, 2}},    {{-3, 0, -3, 0}},
    {{2, 2, -2, 2}},  {{0, -3, 0, -3}},  {{2, -2, -2, -2}}, {{3, 0, -3, 0}},
}};

} // namespace detail

/** @brief Number of mounting rows in DS Figure 4-3. */
inline constexpr std::size_t kMountingCount = 24;

/**
 * @brief The mapping quaternion of row `row` of DS Figure 4-3 (p41), the system orientation for a
 * device mounted with the listed physical axes aligned to East, North, Up.
 * @param row 0 to 23: East-North-Up first, as the data sheet lists them.
 * @return The quaternion in Q30; all zero for a row out of range.
 */
[[nodiscard]] constexpr Quat30 MountingQuaternion(std::size_t row) noexcept {
  if (row >= kMountingCount) {
    return Quat30{};
  }
  const auto& c = detail::kMountCodes[row];
  return Quat30{.x = detail::MountQ30(c[1]),
                .y = detail::MountQ30(c[2]),
                .z = detail::MountQ30(c[3]),
                .w = detail::MountQ30(c[0])};
}

/** @brief Steps of the calibration procedure (CAL 1000-4044, DS S3.1). */
enum class CalStep : uint8_t {
  EnableSensors = 0, ///< Enable accelerometer and magnetometer calibration; Game Rotation Vector
                     ///< and Magnetic Field at 50 Hz or faster.
  MovePoses = 1,     ///< Move the device through 4 to 6 orientations, about 1 s each.
  SetDown = 2,       ///< Set the device down for 2 to 3 s so the gyroscope calibrates.
  RotateAxes = 3,    ///< Rotate about 180 degrees around each axis, about 2 s each.
  WaitAccuracy = 4,  ///< Wait until the magnetometer accuracy reads medium or high.
  Save = 5,          ///< Save the DCD.
  Done = 6,          ///< The calibration is stored.
};

/**
 * @brief A state object that turns the calibration procedure into steps. It sends nothing: it
 * follows what you tell it (`Advance()`, `Observe()`) and says what to do next.
 */
class CalibrationHelper {
public:
  /** @return The current step. */
  [[nodiscard]] CalStep Step() const noexcept {
    return step_;
  }
  /** @brief The user finished the current step; go to the next one (`WaitAccuracy` needs
   * `Observe()`). */
  void Advance() noexcept {
    const uint8_t s = detail::ToUnderlying(step_);
    if ((step_ == CalStep::WaitAccuracy) && !accurate_) {
      return;
    }
    if (step_ != CalStep::Done) {
      step_ = static_cast<CalStep>(s + 1U); // cast of a value proven below `Done`
    }
  }
  /** @brief Tell the helper the latest magnetometer accuracy (the status of a magnetic field
   * sample). @param a The accuracy. */
  void Observe(Accuracy a) noexcept {
    accurate_ = (a == Accuracy::Medium) || (a == Accuracy::High);
    if ((step_ == CalStep::WaitAccuracy) && accurate_) {
      step_ = CalStep::Save;
    }
  }
  /** @return true once the magnetometer accuracy is medium or high. */
  [[nodiscard]] bool ReadyToSave() const noexcept {
    return accurate_;
  }
  /** @brief Start again. */
  void Reset() noexcept {
    step_ = CalStep::EnableSensors;
    accurate_ = false;
  }
  /** @brief The slowest report rate the procedure allows, in Hz (DS p38). @return 50. */
  [[nodiscard]] static constexpr uint32_t MinRateHz() noexcept {
    return 50;
  }

private:
  CalStep step_{CalStep::EnableSensors};
  bool accurate_{false};
};

} // namespace bno08x

#endif // BNO08X_RECORDS_HPP
