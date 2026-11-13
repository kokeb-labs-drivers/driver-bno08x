/**
 * @file bno08x_types.hpp
 * @brief Public types of the BNO08x driver: errors, `Result<T>`, configuration, events and the
 * integer unit helpers.
 * @copyright Copyright (c) 2026 The driver-bno08x authors. SPDX-License-Identifier: MIT
 *
 * @details
 * Everything here is plain data with no platform dependency. The driver is integer-only: sensor
 * values keep the Q point the SH-2 reference manual gives them (RM S6.5), and the helpers at the
 * end of this file convert them to whole units (millimetres per second squared, milliradians per
 * second, nanotesla, microradians, centidegrees).
 */
#ifndef BNO08X_TYPES_HPP
#define BNO08X_TYPES_HPP

#include <array>
#include <cstddef>
#include <cstdint>

#include "bno08x_detail.hpp"
#include "bno08x_registers.hpp"
#include "bno08x_version.h"

/** @brief The BNO08x driver: everything it declares lives here; internals in `detail`. */
namespace bno08x {

/**
 * @defgroup bno08x_types Driver types
 * @brief Errors, results, configuration, events and unit helpers.
 * @{
 */

/**
 * @brief Why a call failed. Every call of the driver returns one of these in its `Result`.
 *
 * Values 0 to 9 are the shared base set; chip-specific values start at 16. `DeviceIdMismatch` and
 * `Crc` exist for the shared base set only: this chip has no ID register to check and its links
 * carry no CRC, so the driver never returns them.
 */
enum class [[nodiscard]] Error : uint8_t {
  Ok = 0,               ///< The call succeeded.
  NotInitialized = 1,   ///< `Begin()` has not been called, or the session faulted.
  InvalidArgument = 2,  ///< An argument or a `Config` field is out of range; nothing was sent.
  Bus = 3,              ///< A port hook returned `false`.
  Timeout = 4,          ///< The hub did not answer in the allowed time.
  DeviceIdMismatch = 5, ///< Base value; never returned (the BNO08x has no ID register).
  Crc = 6,              ///< Base value; never returned (no link of this chip carries a CRC).
  Busy = 7,             ///< The operation queue is full, or a transmit is still pending.
  DeviceFault = 8,      ///< The hub reported a failure inside a response (a status or error code).
  Unsupported = 9,      ///< The hook or silicon feature this call needs is absent.
  Framing = 16,         ///< A frame rule was broken and the engine resynchronised.
  HubReset = 17,        ///< The hub restarted; the operation was abandoned.
  Overflow = 18,        ///< The event queue or a cargo buffer was full.
  Refused = 19,         ///< The hub answered with a failure status (for example an FRS status).
  Oversize = 20,        ///< A request does not fit one write transfer; nothing was sent.
  NotAdvertised = 21,   ///< The channel or report was never advertised by the hub.
  Variant = 22,         ///< The feature is not available on the configured chip.
};

namespace detail {

/// The name of each error, indexed by value; `nullptr` marks a value that is not an enumerator.
inline constexpr std::array<const char*, 23> kErrorNames{
    "Ok",       "NotInitialized", "InvalidArgument", "Bus",           "Timeout", "DeviceIdMismatch",
    "Crc",      "Busy",           "DeviceFault",     "Unsupported",   nullptr,   nullptr,
    nullptr,    nullptr,          nullptr,           nullptr,         "Framing", "HubReset",
    "Overflow", "Refused",        "Oversize",        "NotAdvertised", "Variant"};

} // namespace detail

/**
 * @brief The name of an error, for logs.
 * @param error The error.
 * @return A static string such as `"Timeout"`; `"Unknown"` for a value that is not an enumerator.
 */
constexpr const char* ToString(Error error) noexcept {
  const auto index = static_cast<std::size_t>(detail::ToUnderlying(error));
  if (index < detail::kErrorNames.size()) {
    const char* name = detail::kErrorNames[index];
    if (name != nullptr) {
      return name;
    }
  }
  return "Unknown";
}

/**
 * @brief The value of a call, or why there is none.
 *
 * Check `ok()` (or the `bool` conversion) before reading `value`; on failure `value` is `T{}`.
 *
 * @tparam T The value type. `Result<void>` carries only the error.
 */
template <typename T>
struct [[nodiscard]] Result {
  T value{};              ///< The value; `T{}` when the call failed.
  Error error{Error::Ok}; ///< `Error::Ok` on success.

  /** @return true when the call succeeded. */
  [[nodiscard]] constexpr bool ok() const noexcept {
    return error == Error::Ok;
  }
  /** @return true when the call succeeded. */
  constexpr explicit operator bool() const noexcept {
    return ok();
  }
  /**
   * @brief A successful result.
   * @param v The value.
   * @return `{v, Error::Ok}`.
   */
  static constexpr Result Success(T v) noexcept {
    return {v, Error::Ok};
  }
  /**
   * @brief A failed result.
   * @param e Why it failed; never `Error::Ok`.
   * @return `{T{}, e}`.
   */
  static constexpr Result Failure(Error e) noexcept {
    Result r{};
    r.error = e;
    return r;
  }
};

/** @brief The result of a call that returns no value. */
template <>
struct [[nodiscard]] Result<void> {
  Error error{Error::Ok}; ///< `Error::Ok` on success.

  /** @return true when the call succeeded. */
  [[nodiscard]] constexpr bool ok() const noexcept {
    return error == Error::Ok;
  }
  /** @return true when the call succeeded. */
  constexpr explicit operator bool() const noexcept {
    return ok();
  }
  /** @return A successful result. */
  static constexpr Result Success() noexcept {
    return {Error::Ok};
  }
  /**
   * @brief A failed result.
   * @param e Why it failed; never `Error::Ok`.
   * @return `{e}`.
   */
  static constexpr Result Failure(Error e) noexcept {
    return {e};
  }
};

/** @brief Severity passed to the optional `Log()` hook. */
enum class LogLevel : uint8_t {
  Error = 0, ///< A call failed.
  Warning,   ///< Something unexpected that the driver recovered from.
  Info,      ///< Lifecycle events: session ready, hub reset.
  Debug,     ///< Detail for bring-up.
};

/** @brief How the host reaches the hub (strapped by PS1 and PS0 at reset, DS p9 Figure 1-5). */
enum class Transport : uint8_t {
  I2c = 0,      ///< PS1 = 0, PS0 = 0: SHTP over I2C.
  Spi = 1,      ///< PS1 = 1, PS0 = 1: SHTP over SPI with H_INTN and WAKE.
  UartShtp = 2, ///< PS1 = 1, PS0 = 0: SHTP over UART (3 Mbit/s, framed).
  UartRvc = 3,  ///< PS1 = 0, PS0 = 1: the stand-alone UART-RVC frames (115200 baud).
};

/** @brief The chip of the family the driver talks to. It selects behaviour; it is never detected.
 */
enum class Variant : uint8_t {
  Bno080 = 0, ///< BNO080 (end of life): the base MotionEngine.
  Bno085 = 1, ///< BNO085: enhanced fusion; I2C polling stretches the clock.
  Bno086 = 2, ///< BNO086: interactive calibration, RVC MI/MR bytes, zero-length I2C packets.
};

/** @brief How an I2C or SPI read sizes its transfers. */
enum class ReadPolicy : uint8_t {
  HeaderThenRest =
      0,     ///< Read the 4-byte header, then exactly the rest as a continuation (default).
  Fixed = 1, ///< Read `Config::fixed_read_bytes` per transfer; the hub zero-pads.
};

/** @brief State of the session. */
enum class State : uint8_t {
  Idle = 0,       ///< Before `Begin()`.
  Resetting = 1,  ///< The reset sequence runs; the hub needs t1 + t2 (DS p47).
  WaitAdvert = 2, ///< Waiting for the SHTP advertisement.
  WaitReady = 3,  ///< Advertisement seen; waiting for reset-complete and the initialise response.
  Ready = 4,      ///< Operations and events flow.
  Faulted = 5,    ///< A start-up step timed out; only `Begin()` leaves this state.
};

/** @brief Accuracy of a sensor sample (status bits 1:0 of every input report, RM S6.5.1). */
enum class Accuracy : uint8_t {
  Unreliable = 0, ///< Unreliable.
  Low = 1,        ///< Low accuracy.
  Medium = 2,     ///< Medium accuracy.
  High = 3,       ///< High accuracy.
};

/** @brief Every sensor report ID any CEVA document defines (RM S6.5; matrix section 1.8 of the
 * design). */
enum class SensorId : uint8_t {
  Accelerometer = 0x01,              ///< Accelerometer, Q8 m/s2.
  GyroscopeCalibrated = 0x02,        ///< Calibrated gyroscope, Q9 rad/s.
  MagneticFieldCalibrated = 0x03,    ///< Calibrated magnetic field, Q4 uT.
  LinearAcceleration = 0x04,         ///< Linear acceleration, Q8 m/s2.
  RotationVector = 0x05,             ///< Rotation vector, Q14, accuracy Q12 rad.
  Gravity = 0x06,                    ///< Gravity, Q8 m/s2.
  GyroscopeUncalibrated = 0x07,      ///< Uncalibrated gyroscope with bias, Q9 rad/s.
  GameRotationVector = 0x08,         ///< Game rotation vector, Q14.
  GeomagneticRotationVector = 0x09,  ///< Geomagnetic rotation vector, Q14, accuracy Q12 rad.
  Pressure = 0x0A,                   ///< Pressure, Q20 hPa (needs the environmental sensor).
  AmbientLight = 0x0B,               ///< Ambient light, Q8 lux (needs the environmental sensor).
  Humidity = 0x0C,                   ///< Relative humidity, Q8 percent (environmental sensor).
  Proximity = 0x0D,                  ///< Proximity, Q4 cm (environmental sensor).
  Temperature = 0x0E,                ///< Temperature, Q7 degrees C (environmental sensor).
  MagneticFieldUncalibrated = 0x0F,  ///< Uncalibrated magnetic field with hard-iron bias, Q4 uT.
  TapDetector = 0x10,                ///< Tap detector flags.
  StepCounter = 0x11,                ///< Step counter: latency and 16-bit step count.
  SignificantMotion = 0x12,          ///< Significant motion.
  StabilityClassifier = 0x13,        ///< Stability classifier enumeration.
  RawAccelerometer = 0x14,           ///< Raw accelerometer ADC counts with hub timestamp.
  RawGyroscope = 0x15,               ///< Raw gyroscope ADC counts, temperature and hub timestamp.
  RawMagnetometer = 0x16,            ///< Raw magnetometer ADC counts with hub timestamp.
  StepDetector = 0x18,               ///< Step detector latency.
  ShakeDetector = 0x19,              ///< Shake detector axis bits.
  FlipDetector = 0x1A,               ///< Flip detector (manual only).
  PickupDetector = 0x1B,             ///< Pickup detector (manual only).
  StabilityDetector = 0x1C,          ///< Stability detector bits.
  PersonalActivityClassifier = 0x1E, ///< Personal activity classifier with 10 confidences.
  SleepDetector = 0x1F,              ///< Sleep detector (manual only).
  TiltDetector = 0x20,               ///< Tilt detector (manual only).
  PocketDetector = 0x21,             ///< Pocket detector (manual only).
  CircleDetector = 0x22,             ///< Circle detector (manual only).
  HeartRateMonitor = 0x23,           ///< Heart rate monitor (manual only).
  ArvrRotationVector = 0x28,         ///< AR/VR stabilised rotation vector.
  ArvrGameRotationVector = 0x29,     ///< AR/VR stabilised game rotation vector.
  GyroIntegratedRotationVector =
      0x2A,                 ///< Gyro-integrated rotation vector (channel 5, no report ID).
  MotionRequest = 0x2B,     ///< Motion request of the interactive calibration (BNO086).
  OpticalFlow = 0x2C,       ///< Optical flow (manual only; several fields undocumented).
  DeadReckoningPose = 0x2D, ///< Dead reckoning pose (manual only).
  WheelEncoder = 0x2E,      ///< Wheel encoder input: named by one library only, no layout.
};

/** @brief Number of slots of the per-sensor tables: sensor IDs 0x01 to 0x2E. */
inline constexpr std::size_t kSensorSlots = 0x2F;

/**
 * @brief Whether `raw` is one of the sensor IDs of `SensorId`, and if so which.
 * @param raw A report ID.
 * @param[out] out The sensor, set only when the function returns true.
 * @return true for an ID of `SensorId`.
 */
[[nodiscard]] constexpr bool ToSensorId(uint8_t raw, SensorId& out) noexcept {
  const auto v = static_cast<uint32_t>(raw);
  const bool known = ((v >= 0x01U) && (v <= 0x16U)) || ((v >= 0x18U) && (v <= 0x1CU)) ||
                     ((v >= 0x1EU) && (v <= 0x23U)) || ((v >= 0x28U) && (v <= 0x2EU));
  if (known) {
    out = static_cast<SensorId>(raw);
  }
  return known;
}

/** @brief What an `Event` reports. */
enum class EventKind : uint8_t {
  Sensor = 0, ///< A sensor sample: `sensor`, `accuracy`, `t_us` and the values `v`.
  OpDone = 1, ///< An operation finished: `op`, `error`, `detail` and the response bytes in `v`.
  FeatureChanged = 2, ///< The hub sent a feature response nobody asked for (a rate change).
  Reset = 3,          ///< The hub restarted; `detail` is the reset cause when known.
  FlushDone = 4,      ///< A flush completed for `sensor`.
  HubError = 5,       ///< The SHTP error list: `detail` is the first code, `v` all of them.
  Dropped = 6,        ///< Events were lost to a full queue: `detail` is how many.
  Rvc = 7, ///< A UART-RVC frame: `v` holds the `RvcFrame` fields in order (index, yaw, pitch, roll,
           ///< ax, ay, az, mi, mr, mi_mr_valid).
  Sh2Error = 8, ///< An SH-2 error queue entry: `v[0..5]` severity, error sequence, source, error,
                ///< module, code.
};

/** @brief An operation number returned by every call that completes later with an `OpDone` event.
 */
using OpId = uint16_t;

/** @brief Number of values an `Event` carries. */
inline constexpr std::size_t kEventValues = 16;

/**
 * @brief One thing the driver reports, taken with `Bno08x::PopEvent()`.
 *
 * For a sensor sample `v` holds the report's fields in the order of the reference manual's table,
 * as signed 32-bit integers in the report's own units (its Q point is `QPoint()`): for example
 * `v[0..2]` = x, y, z of an accelerometer, `v[0..3]` = i, j, k, real and `v[4]` = accuracy of a
 * rotation vector. `docs/sensors.md` lists every layout.
 */
struct Event {
  EventKind kind{EventKind::Sensor};        ///< What this event is.
  SensorId sensor{SensorId::Accelerometer}; ///< The sensor (Sensor, FeatureChanged, FlushDone).
  Accuracy accuracy{Accuracy::Unreliable};  ///< The sample's accuracy (Sensor).
  bool wake{false};                         ///< The sample came on the wake-up channel.
  uint8_t report_seq{0};                    ///< The report's own sequence number (Sensor).
  OpId op{0};                               ///< The operation (OpDone).
  Error error{Error::Ok};                   ///< The operation's outcome (OpDone).
  uint32_t t_us{0};                      ///< Host time of the sample, `NowUs()` domain, wrap-safe.
  uint32_t hub_t_us{0};                  ///< Raw sensors and pose: the hub's own timestamp.
  uint32_t detail{0};                    ///< Kind-specific extra word.
  std::array<int32_t, kEventValues> v{}; ///< The values.
};

/** @brief Counters of everything the driver dropped, repaired or noticed. Never reset by the
 * driver. */
struct Stats {
  uint32_t short_transfer{0};        ///< Transfers shorter than a header (V1).
  uint32_t length_ffff{0};           ///< Headers with length 0xFFFF (V2).
  uint32_t null_header{0};           ///< Null headers, length 0 (V3).
  uint32_t length_short{0};          ///< Headers with length 1 to 3 (V4).
  uint32_t oversize_cargo{0};        ///< Cargoes larger than the receive buffer (V5).
  uint32_t bad_channel{0};           ///< Cargoes on a channel above 5 (V6).
  uint32_t lost_cargo{0};            ///< Cargoes abandoned by a new header or channel (V7, V9).
  uint32_t orphan_cont{0};           ///< Continuations with no cargo open (V8).
  uint32_t bad_cont_len{0};          ///< Continuations with the wrong length (V10).
  uint32_t dup_seq{0};               ///< Duplicate SHTP sequence numbers (V11).
  uint32_t seq_gap{0};               ///< Gaps in the SHTP sequence numbers (V11).
  uint32_t nonzero_pad{0};           ///< Non-zero bytes in the padding (V12).
  uint32_t unknown_report{0};        ///< Reports with an ID the table does not know (V13).
  uint32_t short_report{0};          ///< Reports cut short by the cargo end (V14).
  uint32_t bad_gyrorv{0};            ///< Channel 5 cargoes of the wrong length (V15).
  uint32_t early_cargo{0};           ///< Cargoes before the advertisement (V16).
  uint32_t bad_advert{0};            ///< Advertisements with a malformed TLV.
  uint32_t bad_protocol_id{0};       ///< UART frames with a protocol ID other than 0 or 1.
  uint32_t bad_escape{0};            ///< UART frames with a bad escape sequence.
  uint32_t oversize_frame{0};        ///< UART frames longer than the receive buffer (dropped).
  uint32_t base_overflow{0};         ///< Base timestamps that were 0x7FFFFFFF.
  uint32_t bsq_retries{0};           ///< UART-SHTP: Buffer Status Queries sent again (no BSN came).
  uint32_t product_id_extra{0};      ///< Product ID responses that no request was waiting for.
  uint32_t frs_foreign{0};           ///< FRS read responses for another record type (ignored).
  uint32_t report_seq_gaps{0};       ///< Gaps in a sensor's own sequence number.
  uint32_t payload_upper_nonzero{0}; ///< Narrow payloads whose upper byte was not zero.
  uint32_t resyncs{0};               ///< Resynchronisations of the frame engine.
  uint32_t hub_resets{0};            ///< Hub restarts seen while ready.
  uint32_t restore_failed{0};        ///< Auto-restore writes given up after their retries.
  uint32_t hub_errors{0};            ///< SHTP error lists received.
  uint32_t bus_errors{0};            ///< Port hooks that returned false while reading.
  uint32_t events_dropped{0};        ///< Events lost to a full queue.
  uint32_t rvc_frames{0};            ///< UART-RVC frames accepted.
  uint32_t rvc_bad_csum{0};          ///< UART-RVC frames with a wrong checksum.
  uint32_t rvc_index_gaps{0};        ///< UART-RVC frames whose index skipped.
  uint32_t rvc_banner_bytes{
      0}; ///< Bytes skipped before the first UART-RVC frame (the ASCII banner).
  uint32_t rvc_out_of_range{0};    ///< UART-RVC angles outside the documented ranges.
  uint32_t rvc_csum_with_mi{0};    ///< UART-RVC checksums that matched when MI and MR are included.
  uint32_t rvc_csum_without_mi{0}; ///< UART-RVC checksums that matched only without MI and MR.
};

/** @brief The common feature report of one sensor (RM S6.5.2). */
struct FeatureConfig {
  bool relative{false};          ///< Change sensitivity is relative (flag bit 0).
  bool change_enable{false};     ///< Change sensitivity is enabled (flag bit 1).
  bool wake{false};              ///< The sensor wakes the host (flag bit 2).
  bool always_on{false};         ///< The sensor stays on in sleep (flag bit 3).
  int16_t change_sensitivity{0}; ///< Change sensitivity, in the report's Q point.
  uint32_t interval_us{0};       ///< Report interval in microseconds; 0 turns the sensor off.
  uint32_t batch_us{0};          ///< Batch interval in microseconds; 0 none, 0xFFFFFFFF never.
  uint32_t sensor_config{0};     ///< Sensor-specific configuration word.
  /// Equality of all fields, as a hidden friend.
  friend constexpr bool operator==(const FeatureConfig& lhs,
                                   const FeatureConfig& rhs) noexcept = default;
};

/** @brief Product ID response (RM S6.3.2). */
struct ProductId {
  uint8_t reset_cause{0}; ///< 0 n/a, 1 power on, 2 internal reset, 3 watchdog, 4 external, 5 other.
  uint8_t version_major{0};  ///< Software version, major.
  uint8_t version_minor{0};  ///< Software version, minor.
  uint16_t version_patch{0}; ///< Software version, patch.
  uint32_t part_number{0};   ///< Software part number.
  uint32_t build{0};         ///< Software build number.
  bool valid{false};         ///< A response has been received.
};

/** @brief Sensor metadata record (RM Figures 28 and 29, record IDs 0xE301 to 0xE325). */
struct Metadata {
  bool valid{false};                ///< The record has been read.
  uint8_t version_me{0};            ///< MotionEngine version byte.
  uint8_t version_mh{0};            ///< MotionHub version byte.
  uint8_t version_sh{0};            ///< SensorHub version byte.
  uint32_t range{0};                ///< Range, in the sensor's Q point.
  uint32_t resolution{0};           ///< Resolution, in the sensor's Q point.
  uint16_t power_q10{0};            ///< Power in mA, Q10.
  uint16_t revision{0};             ///< Record revision (3 or 4).
  uint32_t min_period_us{0};        ///< Shortest report period.
  uint32_t max_period_us{0};        ///< Longest report period (revision 4; 0 in revision 3).
  uint16_t fifo_max{0};             ///< Most reports the hub's FIFO holds.
  uint16_t fifo_reserved{0};        ///< Reports reserved for this sensor.
  uint16_t batch_bytes{0};          ///< Batch buffer size in bytes.
  uint16_t vendor_id_len{0};        ///< Length of the vendor ID string.
  uint16_t q1{0};                   ///< Q point 1.
  uint16_t q2{0};                   ///< Q point 2.
  uint16_t q3{0};                   ///< Q point 3.
  uint16_t specific_len{0};         ///< Sensor-specific metadata length in bytes.
  std::array<char, 32> vendor_id{}; ///< The vendor ID string, NUL-terminated, truncated to 31.
  uint32_t activity_classes{0};     ///< Activity classifier: bit n set means class n is supported.
};

/** @brief Largest FRS record the driver reads or writes, in 32-bit words (Kconfig-free constant).
 */
inline constexpr std::size_t kFrsMaxWords = 128;

/** @brief A caller-owned buffer for `ReadFrs()`; it must outlive the operation. */
struct FrsBuffer {
  std::array<uint32_t, kFrsMaxWords> words{}; ///< The record's words.
  std::size_t length{0};                      ///< Words received so far; final at `OpDone`.
  uint8_t last_status{0};                     ///< The last status the hub sent.
};

/** @brief The motion engine's calibration enables (RM S6.4.6). */
struct MeCal {
  bool accel{false};    ///< Accelerometer calibration.
  bool gyro{false};     ///< Gyroscope calibration.
  bool mag{false};      ///< Magnetometer calibration.
  bool planar{false};   ///< Planar accelerometer calibration.
  bool on_table{false}; ///< On-table calibration.
};

/** @brief A quaternion in Q14 (RM S6.4.3.3): the set-reorientation argument. */
struct Quat14 {
  int16_t x{0}; ///< X component.
  int16_t y{0}; ///< Y component.
  int16_t z{0}; ///< Z component.
  int16_t w{0}; ///< W component.
};

/** @brief The rotation vector a tare is based on (RM S6.4.3.1, P2). */
enum class TareBasis : uint8_t {
  RotationVector = 0,     ///< Rotation vector: reorients every motion output.
  GameRotationVector = 1, ///< Game rotation vector: reorients only that vector; not persistent.
  GeomagneticRotationVector = 2, ///< Geomagnetic rotation vector: reorients every motion output.
  GyroIntegrated = 3,            ///< Gyro-integrated rotation vector (tares its underlying vector).
  ArvrRotationVector = 4, ///< AR/VR stabilised rotation vector (tares its underlying vector).
  ArvrGameRotationVector =
      5, ///< AR/VR stabilised game rotation vector (tares its underlying vector).
};

/** @brief Axes of a tare (RM S6.4.3.1): 0x07 all axes, 0x04 Z only. */
enum class AxisMask : uint8_t {
  All = 0x07,   ///< X, Y and Z: reorients every motion output.
  ZOnly = 0x04, ///< Z only: changes the heading, not the tilt.
  X = 0x01,     ///< X only; refused unless `Config::tare_any_mask` is set.
  Y = 0x02,     ///< Y only; refused unless `Config::tare_any_mask` is set.
  XY = 0x03,    ///< X and Y; refused unless `Config::tare_any_mask` is set.
  XZ = 0x05,    ///< X and Z; refused unless `Config::tare_any_mask` is set.
  YZ = 0x06,    ///< Y and Z; refused unless `Config::tare_any_mask` is set.
};

/** @brief The motion intent of the interactive calibration (RM S6.4.12.1; BNO086). */
enum class MotionIntent : uint8_t {
  Unknown = 0,                 ///< Unknown.
  StationaryNoVibration = 1,   ///< Stationary without vibration.
  StationaryWithVibration = 2, ///< Stationary with vibration.
  InMotion = 3,                ///< In motion.
  InMotionAccelerating = 4,    ///< In motion and accelerating.
};

/** @brief Result of the simple calibration finish (RM S6.4.10.2, R1). */
enum class SimpleCalStatus : uint8_t {
  Ok = 0,                  ///< Calibration succeeded.
  NoGyroOffset = 1,        ///< No gyro offset found.
  NoStationary = 2,        ///< No stationary detection.
  RotationOutOfSpec = 3,   ///< Rotation out of specification.
  GyroOffsetOutOfSpec = 4, ///< Gyro offset out of specification.
  AccelOffset = 5,         ///< Accelerometer offset out of specification.
  GyroGain = 6,            ///< Gyro gain out of specification.
  GyroPeriod = 7,          ///< Gyro period out of specification.
  GyroSampleDrops = 8,     ///< Gyro sample drops.
};

/** @brief One wheel measurement for dead reckoning (RM S6.4.13). */
struct WheelSample {
  uint8_t wheel{0};         ///< 0 left, 1 right.
  uint32_t timestamp_us{0}; ///< Time of the measurement, microseconds.
  uint16_t data{0};         ///< Position (uint16) or velocity (int16 bit pattern).
  bool velocity{false};     ///< false: `data` is a position, true: a velocity.
};

/** @brief A decoded UART-RVC frame (DS p21). */
struct RvcFrame {
  uint8_t index{0};        ///< Frame index, increments per frame.
  int16_t yaw{0};          ///< Yaw in 0.01 degree.
  int16_t pitch{0};        ///< Pitch in 0.01 degree.
  int16_t roll{0};         ///< Roll in 0.01 degree.
  int16_t ax{0};           ///< Acceleration x in milli-g.
  int16_t ay{0};           ///< Acceleration y in milli-g.
  int16_t az{0};           ///< Acceleration z in milli-g.
  uint8_t mi{0};           ///< Motion intent (BNO086 only).
  uint8_t mr{0};           ///< Motion request (BNO086 only).
  bool mi_mr_valid{false}; ///< `mi` and `mr` carry data (variant BNO086).
};

/** @brief The configuration of a session. `Config{}` is the recommended setup for a BNO085. */
struct Config {
  Variant variant{Variant::Bno085};                   ///< Chip of the family; selects behaviour.
  ReadPolicy read_policy{ReadPolicy::HeaderThenRest}; ///< How I2C and SPI reads are sized.
  uint8_t i2c_address{reg::kI2cAddrLow};              ///< I2C: 0x4A (SA0 low) or 0x4B (SA0 high).
  uint16_t fixed_read_bytes{64};    ///< Bytes per read for `ReadPolicy::Fixed` (header included).
  bool strict_variant{true};        ///< Refuse features the chip lacks with `Variant`.
  bool auto_restore{true};          ///< Re-send enabled features after a hub restart.
  bool poll_without_int{false};     ///< Guarded: read on a cadence, ignoring H_INTN (I2C only).
  bool require_init_response{true}; ///< Wait for the unsolicited initialise response at start.
  bool frs_verify{true};            ///< Read an FRS record back after writing it.
  bool set_feature_confirm{false};  ///< Complete `SetFeature` on the hub's reply, not when written.
  bool strict_raw{false};           ///< Refuse a raw sensor whose base sensor is off.
  bool strict_cont_len{true};       ///< Require continuation length = remaining + 4.
  bool strict_padding{false};       ///< Count non-zero padding bytes.
  bool tare_any_mask{false};        ///< Allow tare axis masks other than 0x07 and 0x04.
  bool rvc_strict{false};           ///< UART-RVC: also require the index to increment by one.
  bool rvc_inputs_experimental{false}; ///< Guarded: allow the unverified `$MIB` host inputs.
  uint16_t startup_timeout_ms{500};    ///< Limit for each start-up wait (advertisement, ready).
  uint16_t op_timeout_ms{1000};        ///< Limit for one operation.
  uint16_t frs_write_timeout_ms{5000}; ///< Limit for one FRS write step.
  uint32_t wake_timeout_us{10000};     ///< SPI: limit for H_INTN after WAKE.
  uint32_t reset_pulse_us{1000};       ///< NRST low time; the datasheet minimum is 10 ns.
  uint16_t advert_retry_ms{100};       ///< Without NRST: wait before asking for the advertisement.
  uint16_t uart_token_timeout_ms{
      1000};                      ///< UART-SHTP: BSN validity when the hub did not advertise one.
  uint16_t uart_bsq_retry_ms{20}; ///< UART-SHTP: wait for a BSN before the query is sent again.
  uint16_t poll_interval_ms{10};  ///< `poll_without_int`: read cadence.
  uint8_t resync_limit{4};        ///< Violations within the window that force a recovery.
  uint16_t resync_window_ms{100}; ///< The window of `resync_limit`.
  uint8_t product_id_entries{1};  ///< Product ID responses accepted (1, up to 5).

  /**
   * @brief Whether every time limit is non-zero and the SPI WAKE wait ends before an operation's
   * limit, so WAKE is always released first (DS p18).
   * @return true when the time fields are valid.
   */
  [[nodiscard]] constexpr bool TimesValid() const noexcept {
    const bool non_zero = (startup_timeout_ms > 0U) && (op_timeout_ms > 0U) &&
                          (frs_write_timeout_ms > 0U) && (wake_timeout_us > 0U) &&
                          (resync_window_ms > 0U) && (poll_interval_ms > 0U);
    const bool pulses = (reset_pulse_us > 0U) && (uart_bsq_retry_ms > 0U);
    const bool order = wake_timeout_us < (static_cast<uint32_t>(op_timeout_ms) * 1000U);
    return non_zero && pulses && order;
  }

  /**
   * @brief Whether every field is inside its documented range.
   * @return true when `Begin()` would accept the configuration.
   */
  [[nodiscard]] constexpr bool Valid() const noexcept {
    const bool enums =
        (detail::ToUnderlying(variant) <= detail::ToUnderlying(Variant::Bno086)) &&
        (detail::ToUnderlying(read_policy) <= detail::ToUnderlying(ReadPolicy::Fixed));
    const bool sizes = (fixed_read_bytes >= 4U) && (fixed_read_bytes <= 1024U);
    const bool counts =
        (resync_limit > 0U) && (product_id_entries >= 1U) && (product_id_entries <= 5U);
    const bool addr = (i2c_address == reg::kI2cAddrLow) || (i2c_address == reg::kI2cAddrHigh);
    return enums && sizes && TimesValid() && counts && addr;
  }
};

/** @} */

/**
 * @defgroup bno08x_units Unit helpers
 * @brief Integer conversions from the reports' Q points to whole units.
 * @{
 */

/**
 * @brief A Q8 acceleration (m/s2) in millimetres per second squared.
 * @param q8 A Q8 value.
 * @return The converted value.
 */
[[nodiscard]] constexpr int32_t ToMmPerS2(int32_t q8) noexcept {
  return static_cast<int32_t>((static_cast<int64_t>(q8) * 1000) / 256);
}
/**
 * @brief A Q9 angular rate (rad/s) in milliradians per second.
 * @param q9 A Q9 value.
 * @return The converted value.
 */
[[nodiscard]] constexpr int32_t ToMilliRadPerS(int32_t q9) noexcept {
  return static_cast<int32_t>((static_cast<int64_t>(q9) * 1000) / 512);
}
/**
 * @brief A Q4 magnetic field (uT) in nanotesla.
 * @param q4 A Q4 value.
 * @return The converted value.
 */
[[nodiscard]] constexpr int32_t ToNanoTesla(int32_t q4) noexcept {
  return static_cast<int32_t>((static_cast<int64_t>(q4) * 1000) / 16);
}
/**
 * @brief A Q12 angle (rad), such as a rotation vector's accuracy, in microradians.
 * @param q12 A Q12 value.
 * @return The converted value.
 */
[[nodiscard]] constexpr int32_t ToMicroRad(int32_t q12) noexcept {
  return static_cast<int32_t>((static_cast<int64_t>(q12) * 1'000'000) / 4096);
}
/**
 * @brief A Q14 quaternion component in parts per million of unity.
 * @param q14 A Q14 value.
 * @return The converted value.
 */
[[nodiscard]] constexpr int32_t ToPpm(int32_t q14) noexcept {
  return static_cast<int32_t>((static_cast<int64_t>(q14) * 1'000'000) / 16384);
}

/** @brief Yaw, pitch and roll in centidegrees. */
struct EulerCentiDeg {
  int32_t yaw{0};   ///< Rotation about Z, -18000 to 18000.
  int32_t pitch{0}; ///< Rotation about Y, -9000 to 9000.
  int32_t roll{0};  ///< Rotation about X, -18000 to 18000.
};

/** @return The driver version, e.g. `"0.1.0"`, from the generated `bno08x_version.h`. No I/O. */
constexpr const char* GetDriverVersion() noexcept {
  return BNO08X_VERSION_STRING;
}
/** @return The major version. No I/O. */
constexpr uint32_t GetDriverVersionMajor() noexcept {
  return BNO08X_VERSION_MAJOR;
}
/** @return The minor version. No I/O. */
constexpr uint32_t GetDriverVersionMinor() noexcept {
  return BNO08X_VERSION_MINOR;
}
/** @return The patch version. No I/O. */
constexpr uint32_t GetDriverVersionPatch() noexcept {
  return BNO08X_VERSION_PATCH;
}

/** @} */

} // namespace bno08x

#endif // BNO08X_TYPES_HPP
