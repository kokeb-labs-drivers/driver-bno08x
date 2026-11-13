/**
 * @file bno08x_reports.hpp
 * @brief The sensor input report layouts as data: one table drives every decode.
 * @copyright Copyright (c) 2026 The driver-bno08x authors. SPDX-License-Identifier: MIT
 *
 * @details
 * Each sensor report (RM S6.5, Figures 82 to 122) is a list of fields at fixed offsets. The table
 * below lists them in the order of the reference manual; `DecodeSensor()` turns a report into an
 * `Event`. Names, Q points and the metadata record of every sensor are tables too, so no function
 * here branches per sensor.
 */
#ifndef BNO08X_REPORTS_HPP
#define BNO08X_REPORTS_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "bno08x_detail.hpp"
#include "bno08x_registers.hpp"
#include "bno08x_shtp.hpp"
#include "bno08x_types.hpp"

namespace bno08x {

namespace detail {

/// How a field is read.
enum class FieldKind : uint8_t {
  U8,  ///< One unsigned byte.
  S16, ///< Two bytes, signed.
  U16, ///< Two bytes, unsigned.
  U32, ///< Four bytes, unsigned.
  S32, ///< Four bytes, signed.
};

/// One field of a report: its byte offset from the report ID and its kind.
struct FieldDef {
  uint8_t off{0};                ///< Offset from the first byte of the report.
  FieldKind kind{FieldKind::U8}; ///< Width and signedness.
};

/// Most fields any report has (dead reckoning pose: 14, activity classifier: 12).
inline constexpr std::size_t kMaxFields = 14;

/// A sensor's layout.
struct SensorSpec {
  uint8_t count{0};     ///< Number of fields.
  uint8_t hub_ts{0xFF}; ///< Index of the field that is the hub timestamp, 0xFF for none.
  bool lsb_only{false}; ///< A 16-bit field of which the manual uses the low byte only.
  std::array<FieldDef, kMaxFields> f{}; ///< The fields in manual order.
};

/// `n` consecutive fields of one kind from `first`, `step` bytes apart.
constexpr SensorSpec Run(FieldKind kind, uint8_t first, uint8_t step, uint8_t n) noexcept {
  SensorSpec s{};
  s.count = n;
  for (uint8_t i = 0; i < n; ++i) {
    s.f[i] = FieldDef{.off = static_cast<uint8_t>(first + (i * step)), .kind = kind};
  }
  return s;
}

/// `base` followed by one more field.
constexpr SensorSpec Plus(SensorSpec base, uint8_t off, FieldKind kind) noexcept {
  base.f[base.count] = FieldDef{.off = off, .kind = kind};
  ++base.count;
  return base;
}

/// The layouts of the AR/VR rotation vectors, the motion request, optical flow and the dead
/// reckoning pose.
constexpr void FillRare(std::array<SensorSpec, reg::detail::kSensorTableSize>& t) noexcept {
  const SensorSpec quat = Run(FieldKind::S16, 4, 2, 4);
  const SensorSpec quat_acc = Plus(quat, 12, FieldKind::S16);
  t[0x28] = quat_acc;
  t[0x29] = quat;
  t[0x2B] = Run(FieldKind::U8, 4, 1, 2);
  SensorSpec flow = Run(FieldKind::S16, 4, 2, 3);
  flow = Plus(
      Plus(
          Plus(Plus(Plus(Plus(Plus(flow, 10, FieldKind::U8), 11, FieldKind::U8), 12, FieldKind::U8),
                    13, FieldKind::U8),
               14, FieldKind::U8),
          15, FieldKind::U8),
      16, FieldKind::U8);
  t[0x2C] = Plus(Plus(flow, 18, FieldKind::U16), 20, FieldKind::U32);
  SensorSpec pose = Run(FieldKind::U32, 4, 4, 1);
  pose.hub_ts = 0;
  SensorSpec words = Run(FieldKind::S32, 8, 4, 13);
  for (uint8_t i = 0; i < 13; ++i) {
    pose = Plus(pose, words.f[i].off, FieldKind::S32);
  }
  t[0x2D] = pose;
}

/// The layout table, indexed by report ID (RM Figures 82 to 122).
constexpr std::array<SensorSpec, reg::detail::kSensorTableSize> MakeSpecs() noexcept {
  std::array<SensorSpec, reg::detail::kSensorTableSize> t{};
  const SensorSpec vec3 = Run(FieldKind::S16, 4, 2, 3);
  const SensorSpec quat = Run(FieldKind::S16, 4, 2, 4);
  const SensorSpec quat_acc = Plus(quat, 12, FieldKind::S16);
  const SensorSpec six = Run(FieldKind::S16, 4, 2, 6);
  const SensorSpec word16 = Run(FieldKind::U16, 4, 2, 1);
  SensorSpec lsb16 = word16;
  lsb16.lsb_only = true;
  SensorSpec raw = Plus(vec3, 12, FieldKind::U32);
  raw.hub_ts = 3;
  SensorSpec raw_gyro = Plus(Plus(vec3, 10, FieldKind::S16), 12, FieldKind::U32);
  raw_gyro.hub_ts = 4;
  for (const std::size_t id : std::array<std::size_t, 5>{0x01, 0x02, 0x03, 0x04, 0x06}) {
    t[id] = vec3;
  }
  t[0x05] = quat_acc;
  t[0x07] = six;
  t[0x08] = quat;
  t[0x09] = quat_acc;
  t[0x0A] = Run(FieldKind::U32, 4, 4, 1);
  t[0x0B] = Run(FieldKind::U32, 4, 4, 1);
  for (const std::size_t id : std::array<std::size_t, 3>{0x0C, 0x0D, 0x0E}) {
    t[id] = Run(FieldKind::S16, 4, 2, 1);
  }
  t[0x0F] = six;
  t[0x10] = Run(FieldKind::U8, 4, 1, 1);
  t[0x11] = Plus(Run(FieldKind::U32, 4, 4, 1), 8, FieldKind::U16);
  t[0x12] = lsb16;
  t[0x13] = Run(FieldKind::U8, 4, 1, 1);
  t[0x14] = raw;
  t[0x15] = raw_gyro;
  t[0x16] = raw;
  t[0x18] = Run(FieldKind::U32, 4, 4, 1);
  for (const std::size_t id :
       std::array<std::size_t, 7>{0x19, 0x1A, 0x1B, 0x1C, 0x20, 0x21, 0x22}) {
    t[id] = lsb16;
  }
  t[0x1E] = Run(FieldKind::U8, 4, 1, 12);
  t[0x1F] = Run(FieldKind::U8, 4, 1, 1);
  t[0x23] = word16;
  FillRare(t);
  return t;
}

/// The layout table.
inline constexpr std::array<SensorSpec, reg::detail::kSensorTableSize> kSpecs = MakeSpecs();

/// The Q point of the first value of each sensor, 0 when the values are not fixed point.
constexpr std::array<uint8_t, kSensorSlots> MakeQPoints() noexcept {
  std::array<uint8_t, kSensorSlots> q{};
  const std::array<std::array<uint8_t, 2>, 17> rows{{{{0x01, 8}},
                                                     {{0x02, 9}},
                                                     {{0x03, 4}},
                                                     {{0x04, 8}},
                                                     {{0x05, 14}},
                                                     {{0x06, 8}},
                                                     {{0x07, 9}},
                                                     {{0x08, 14}},
                                                     {{0x09, 14}},
                                                     {{0x0A, 20}},
                                                     {{0x0B, 8}},
                                                     {{0x0C, 8}},
                                                     {{0x0D, 4}},
                                                     {{0x0E, 7}},
                                                     {{0x0F, 4}},
                                                     {{0x28, 14}},
                                                     {{0x29, 14}}}};
  for (const auto& r : rows) {
    q[r[0]] = r[1];
  }
  q[0x2A] = 14;
  q[0x2D] = 15;
  return q;
}

/// Q points by report ID.
inline constexpr std::array<uint8_t, kSensorSlots> kQPoints = MakeQPoints();

/// Metadata record of each sensor (RM Figure 27 p30), 0 for sensors without one.
constexpr std::array<uint16_t, kSensorSlots> MakeMetadataIds() noexcept {
  std::array<uint16_t, kSensorSlots> m{};
  const std::array<std::array<uint16_t, 2>, 37> rows{{
      {{0x01, 0xE302}}, {{0x02, 0xE306}}, {{0x03, 0xE309}}, {{0x04, 0xE303}}, {{0x05, 0xE30B}},
      {{0x06, 0xE304}}, {{0x07, 0xE307}}, {{0x08, 0xE30C}}, {{0x09, 0xE30D}}, {{0x0A, 0xE30E}},
      {{0x0B, 0xE30F}}, {{0x0C, 0xE310}}, {{0x0D, 0xE311}}, {{0x0E, 0xE312}}, {{0x0F, 0xE30A}},
      {{0x10, 0xE313}}, {{0x11, 0xE315}}, {{0x12, 0xE316}}, {{0x13, 0xE317}}, {{0x14, 0xE301}},
      {{0x15, 0xE305}}, {{0x16, 0xE308}}, {{0x18, 0xE314}}, {{0x19, 0xE318}}, {{0x1A, 0xE319}},
      {{0x1B, 0xE31A}}, {{0x1C, 0xE31B}}, {{0x1E, 0xE31C}}, {{0x1F, 0xE31D}}, {{0x20, 0xE31E}},
      {{0x21, 0xE31F}}, {{0x22, 0xE320}}, {{0x23, 0xE321}}, {{0x28, 0xE322}}, {{0x29, 0xE323}},
      {{0x2A, 0xE324}}, {{0x2B, 0xE325}},
  }};
  for (const auto& r : rows) {
    m[r[0]] = r[1];
  }
  return m;
}

/// Metadata record IDs by report ID.
inline constexpr std::array<uint16_t, kSensorSlots> kMetadataIds = MakeMetadataIds();

/// Names by report ID, for logs and docs; `nullptr` for IDs that are not sensors.
inline constexpr std::array<const char*, kSensorSlots> kSensorNames{nullptr,
                                                                    "Accelerometer",
                                                                    "GyroscopeCalibrated",
                                                                    "MagneticFieldCalibrated",
                                                                    "LinearAcceleration",
                                                                    "RotationVector",
                                                                    "Gravity",
                                                                    "GyroscopeUncalibrated",
                                                                    "GameRotationVector",
                                                                    "GeomagneticRotationVector",
                                                                    "Pressure",
                                                                    "AmbientLight",
                                                                    "Humidity",
                                                                    "Proximity",
                                                                    "Temperature",
                                                                    "MagneticFieldUncalibrated",
                                                                    "TapDetector",
                                                                    "StepCounter",
                                                                    "SignificantMotion",
                                                                    "StabilityClassifier",
                                                                    "RawAccelerometer",
                                                                    "RawGyroscope",
                                                                    "RawMagnetometer",
                                                                    nullptr,
                                                                    "StepDetector",
                                                                    "ShakeDetector",
                                                                    "FlipDetector",
                                                                    "PickupDetector",
                                                                    "StabilityDetector",
                                                                    nullptr,
                                                                    "PersonalActivityClassifier",
                                                                    "SleepDetector",
                                                                    "TiltDetector",
                                                                    "PocketDetector",
                                                                    "CircleDetector",
                                                                    "HeartRateMonitor",
                                                                    nullptr,
                                                                    nullptr,
                                                                    nullptr,
                                                                    nullptr,
                                                                    "ArvrRotationVector",
                                                                    "ArvrGameRotationVector",
                                                                    "GyroIntegratedRotationVector",
                                                                    "MotionRequest",
                                                                    "OpticalFlow",
                                                                    "DeadReckoningPose",
                                                                    "WheelEncoder"};

/// One field of `report`, widened to a signed 32-bit value; the caller has checked the length.
[[nodiscard]] constexpr int32_t ReadField(std::span<const uint8_t> report, std::size_t base,
                                          const FieldDef& fld) noexcept {
  const std::size_t o = static_cast<std::size_t>(fld.off) - base;
  int32_t v = 0;
  switch (fld.kind) {
  case FieldKind::U8:
    v = static_cast<int32_t>(report[o]);
    break;
  case FieldKind::S16:
    v = S16(Le16(report, o));
    break;
  case FieldKind::U16:
    v = static_cast<int32_t>(Le16(report, o));
    break;
  case FieldKind::U32:
    v = static_cast<int32_t>(
        Le32(report, o)); // bit pattern; read back with `static_cast<uint32_t>`
    break;
  case FieldKind::S32:
    v = S32(Le32(report, o));
    break;
  default:
    break;
  }
  return v;
}

/// Bytes a field spans.
[[nodiscard]] constexpr std::size_t FieldWidth(FieldKind kind) noexcept {
  std::size_t w = 1;
  if ((kind == FieldKind::S16) || (kind == FieldKind::U16)) {
    w = 2;
  } else if ((kind == FieldKind::U32) || (kind == FieldKind::S32)) {
    w = 4;
  } else {
    // one byte
  }
  return w;
}

} // namespace detail

/**
 * @brief The Q point of the first value of a sensor's event (0 when not fixed point).
 * @param id The sensor.
 * @return Fractional bits of `Event::v[0]`.
 */
[[nodiscard]] constexpr uint8_t QPoint(SensorId id) noexcept {
  const auto slot = static_cast<std::size_t>(detail::ToUnderlying(id));
  return (slot < kSensorSlots) ? detail::kQPoints[slot] : uint8_t{0};
}

/**
 * @brief The metadata record that describes a sensor (RM Figure 27).
 * @param id The sensor.
 * @return The FRS record ID 0xE301 to 0xE325, or 0 for a sensor that has none.
 */
[[nodiscard]] constexpr uint16_t MetadataRecord(SensorId id) noexcept {
  const auto slot = static_cast<std::size_t>(detail::ToUnderlying(id));
  return (slot < kSensorSlots) ? detail::kMetadataIds[slot] : uint16_t{0};
}

/**
 * @brief The name of a sensor.
 * @param id The sensor.
 * @return A static string such as `"RotationVector"`.
 */
[[nodiscard]] constexpr const char* SensorName(SensorId id) noexcept {
  const auto slot = static_cast<std::size_t>(detail::ToUnderlying(id));
  const char* n = (slot < kSensorSlots) ? detail::kSensorNames[slot] : nullptr;
  return (n != nullptr) ? n : "Unknown";
}

/**
 * @brief Whether the data sheet itself describes the sensor ("D" in the coverage matrix); the
 * others come from the reference manual only and may not exist on a given chip (ambiguity A31).
 * @param id The sensor.
 * @return true when the BNO080/085/086 data sheet lists the sensor.
 */
[[nodiscard]] constexpr bool InDatasheet(SensorId id) noexcept {
  const auto v = static_cast<uint32_t>(detail::ToUnderlying(id));
  const bool manual_only = (v == 0x1AU) || (v == 0x1BU) || ((v >= 0x1FU) && (v <= 0x23U)) ||
                           (v == 0x2BU) || (v == 0x2CU) || (v == 0x2DU) || (v == 0x2EU);
  return !manual_only;
}

/**
 * @brief Whether the sensor delivers raw ADC data (it needs its base sensor enabled, DS p30).
 * @param id The sensor.
 * @return true for the raw accelerometer, gyroscope and magnetometer.
 */
[[nodiscard]] constexpr bool IsRawSensor(SensorId id) noexcept {
  return (id == SensorId::RawAccelerometer) || (id == SensorId::RawGyroscope) ||
         (id == SensorId::RawMagnetometer);
}

namespace detail {

/// Fills `ev` from a sensor report whose first byte is at `report[0]` (the common four bytes
/// included). `base` is 4 for a gyro-integrated report that has no header. Returns false when the
/// report is too short for its layout.
[[nodiscard]] inline bool DecodeFields(const SensorSpec& spec, std::span<const uint8_t> report,
                                       std::size_t base, Event& ev) noexcept {
  for (std::size_t i = 0; i < spec.count; ++i) {
    const FieldDef& fld = spec.f[i];
    if (((static_cast<std::size_t>(fld.off) - base) + FieldWidth(fld.kind)) > report.size()) {
      return false;
    }
    ev.v[i] = ReadField(report, base, fld);
  }
  if (spec.hub_ts != 0xFFU) {
    ev.hub_t_us = static_cast<uint32_t>(ev.v[spec.hub_ts]);
  }
  return true;
}

} // namespace detail

} // namespace bno08x

#endif // BNO08X_REPORTS_HPP
