/**
 * @file bno08x.hpp
 * @brief BNO08x driver: the one header to include.
 * @copyright Copyright (c) 2026 The driver-bno08x authors. SPDX-License-Identifier: MIT
 *
 * @details
 * `bno08x::Bno08x<Port>` drives one CEVA BNO080, BNO085 or BNO086 smart sensor hub through a
 * platform port you write (see `bno08x_interface.hpp`): SHTP framing and the SH-2 protocol over
 * I2C, SPI or UART, or the stand-alone UART-RVC frames. The driver has no platform code, allocates
 * nothing, throws nothing, takes no lock and is not thread-safe: give each object one owner, or
 * guard it with one mutex. No call waits on the hub: `Begin()` starts a session, `Poll()` advances
 * it, and every exchange with the hub is an operation that finishes later with an `OpDone` event.
 */
#ifndef BNO08X_HPP
#define BNO08X_HPP

#include <algorithm>
#include <array>
#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <span>

#include "bno08x_detail.hpp"
#include "bno08x_euler.hpp"
#include "bno08x_interface.hpp"
#include "bno08x_records.hpp"
#include "bno08x_registers.hpp"
#include "bno08x_reports.hpp"
#include "bno08x_rvc.hpp"
#include "bno08x_shtp.hpp"
#include "bno08x_types.hpp"
#include "bno08x_version.h"

namespace bno08x {

/** @brief The sizes of the driver's fixed buffers; pass your own as the second template argument.
 */
struct DefaultLimits {
  static constexpr std::size_t kRxCargo =
      1024;                                    ///< Largest cargo received, in bytes (at least 256).
  static constexpr std::size_t kTxCargo = 128; ///< Largest cargo sent, header included.
  static constexpr std::size_t kOpQueue = 8;   ///< Pending operations.
  static constexpr std::size_t kEventQueue = 32; ///< Events waiting to be popped.
};

/// The transport a port names (used in constraints before the class is complete).
template <typename Port>
inline constexpr Transport kTransportOf = Port::kTransport;

/// What a limits type must provide.
template <typename L>
concept LimitsLike = requires {
  { L::kRxCargo } -> std::convertible_to<std::size_t>;
  { L::kTxCargo } -> std::convertible_to<std::size_t>;
  { L::kOpQueue } -> std::convertible_to<std::size_t>;
  { L::kEventQueue } -> std::convertible_to<std::size_t>;
} && (L::kRxCargo >= 256U) && (L::kTxCargo >= 24U) && (L::kOpQueue >= 2U) && (L::kEventQueue >= 4U);

namespace detail {

/// The port declares something called `SetReset`.
template <typename Port>
concept DeclaresSetReset = requires(Port& port) { port.SetReset(true); };
/// The port drives NRST: `bool SetReset(bool level) noexcept`.
template <typename Port>
concept HasSetReset = requires { ExactHook<Port, bool(bool) noexcept>::Take(&Port::SetReset); };
/// The port declares something called `SetBootn`.
template <typename Port>
concept DeclaresSetBootn = requires(Port& port) { port.SetBootn(true); };
/// The port drives BOOTN: `bool SetBootn(bool level) noexcept`.
template <typename Port>
concept HasSetBootn = requires { ExactHook<Port, bool(bool) noexcept>::Take(&Port::SetBootn); };
/// The port declares something called `TakeIntTimestamp`.
template <typename Port>
concept DeclaresTakeIntTimestamp =
    requires(Port& port, uint32_t& us) { port.TakeIntTimestamp(us); };
/// The port captures H_INTN edges: `bool TakeIntTimestamp(uint32_t&) noexcept`.
template <typename Port>
concept HasTakeIntTimestamp =
    requires { ExactHook<Port, bool(uint32_t&) noexcept>::Take(&Port::TakeIntTimestamp); };
/// The port declares something called `IntAsserted`.
template <typename Port>
concept DeclaresIntAsserted = requires(Port& port) { port.IntAsserted(); };
/// The port reads H_INTN: `bool IntAsserted() noexcept`.
template <typename Port>
concept HasIntAsserted = requires { ExactHook<Port, bool() noexcept>::Take(&Port::IntAsserted); };
/// The port declares something called `Log`.
template <typename Port>
concept DeclaresLog = requires(Port& port) { port.Log(LogLevel::Info, ""); };
/// The port takes log lines: `void Log(LogLevel, const char*) noexcept`.
template <typename Port>
concept HasLog =
    requires { ExactHook<Port, void(LogLevel, const char*) noexcept>::Take(&Port::Log); };
/// The port states that it paces UART bytes itself.
template <typename Port>
concept PacesUartTx = requires { requires Port::kPortPacesUartTx; };

/// Log through the port, or nothing when the port has no `Log()`. The driver never formats text.
template <typename Port>
void Log(Port& port, LogLevel level, const char* message) noexcept {
  if constexpr (HasLog<Port>) {
    port.Log(level, message);
  } else {
    static_cast<void>(port);
    static_cast<void>(level);
    static_cast<void>(message);
  }
}

/// What a pending operation does (and so how its response is matched).
enum class OpKind : uint8_t {
  Advertise,  ///< SHTP get-advertisement command (channel 0).
  Executable, ///< Executable channel: reset, on, sleep; completes when written.
  SetFeature, ///< Set Feature; completes when written (or on the 0xFC with `set_feature_confirm`).
  GetFeature, ///< Get Feature; completes on the matching 0xFC.
  Flush,      ///< Force flush; completes on the 0xEF.
  ProductId,  ///< Product ID; completes on the 0xF8.
  Command,    ///< An SH-2 command; completion depends on the command.
  FrsRead,    ///< FRS read into a buffer.
  FrsWrite,   ///< FRS write with its data steps and the read-back.
};

/// One queued operation.
struct Op {
  OpKind kind{OpKind::Executable}; ///< What it does.
  bool silent{false};              ///< Internal: no `OpDone` event.
  bool no_response{false};         ///< Completes when written.
  bool sent{false};                ///< The request is on the wire.
  bool started{false};             ///< The first attempt to send it was made (the time limit runs).
  bool stop{
      false}; ///< A stop request (sensor off, reset, sleep): never refused once `Begin()` ran.
  uint8_t channel{reg::kChanControl};             ///< Channel of the request.
  uint8_t req_len{0};                             ///< Request length.
  uint8_t sensor{0};                              ///< Sensor ID (feature, flush operations).
  uint8_t cmd{0};                                 ///< Command ID (Command).
  uint8_t cmd_seq{0};                             ///< Command sequence number (Command).
  uint8_t phase{0};                               ///< Step of a multi-step operation.
  uint8_t responses{0};                           ///< Responses seen so far.
  OpId id{0};                                     ///< The operation number.
  uint16_t frs_type{0};                           ///< FRS record (FrsRead, FrsWrite).
  uint32_t start_us{0};                           ///< When its time limit started.
  uint32_t limit_us{0};                           ///< Its time limit, from `start_us`.
  std::array<uint8_t, reg::kLenGetFeature> req{}; ///< The request bytes.
};

/// Whether two operations set the same thing: a sensor's feature, or the hub's power state
/// (reset, on, sleep). The newest such request is the one that holds.
[[nodiscard]] constexpr bool SameTarget(const Op& a, const Op& b) noexcept {
  const bool features =
      (a.kind == OpKind::SetFeature) && (b.kind == OpKind::SetFeature) && (a.sensor == b.sensor);
  const bool power = (a.kind == OpKind::Executable) && (b.kind == OpKind::Executable);
  return features || power;
}

} // namespace detail

// clang-format off: the table below must keep one row per line.
/**
 * @brief Driver for one BNO080, BNO085 or BNO086.
 *
 * Lifecycle: construct with the port (no I/O), `Begin(Config)` to start a session, call `Poll(now_us)`
 * from your loop or task (at least once per tenth of the fastest report period, DS p19), and
 * `PopEvent()` to take what arrived. Every call that touches the hub returns
 * `Error::NotInitialized` until `Begin()` has succeeded; no call starts the hub as a side effect.
 *
 * Optional port hooks, found at compile time:
 *
 * | Hook | Used for | Without it |
 * | --- | --- | --- |
 * | `bool SetReset(bool)` | NRST: hardware reset in `Begin()` and `HardReset()` | `HardReset()` returns `Unsupported`; `Begin()` joins a running hub |
 * | `bool SetBootn(bool)` | BOOTN held high around a reset, so a stray low level cannot start the bootloader | Not driven |
 * | `bool TakeIntTimestamp(uint32_t&)` | The time of the H_INTN edge, for exact sample times | The time of the `Poll()` that saw H_INTN |
 * | `bool IntAsserted()` (UART transports) | The H_INTN level | The first received byte is the time reference |
 * | `void Log(LogLevel, const char*)` | One line per lifecycle event or failure | Nothing is logged |
 *
 * A hook declared with the wrong signature is a compile error.
 *
 * @tparam Port Your class derived from `HubInterface<Port>` that names its `kTransport`.
 * @tparam Limits The sizes of the fixed buffers; `DefaultLimits` unless you tune them.
 */
// clang-format on
template <HubPort Port, LimitsLike Limits = DefaultLimits>
class Bno08x {
  static_assert(!detail::DeclaresSetReset<Port> || detail::HasSetReset<Port>,
                "bno08x: the optional SetReset hook must be `bool SetReset(bool level) noexcept`");
  static_assert(!detail::DeclaresSetBootn<Port> || detail::HasSetBootn<Port>,
                "bno08x: the optional SetBootn hook must be `bool SetBootn(bool level) noexcept`");
  static_assert(!detail::DeclaresTakeIntTimestamp<Port> || detail::HasTakeIntTimestamp<Port>,
                "bno08x: the optional TakeIntTimestamp hook must be `bool "
                "TakeIntTimestamp(uint32_t&) noexcept`");
  static_assert(!detail::DeclaresIntAsserted<Port> || detail::HasIntAsserted<Port>,
                "bno08x: the IntAsserted hook must be `bool IntAsserted() noexcept`");
  static_assert(!detail::DeclaresLog<Port> || detail::HasLog<Port>,
                "bno08x: the optional Log hook must be `void Log(LogLevel, const char*) noexcept`");
  static_assert(!detail::DeclaresUartWriteByte<Port> || detail::HasUartWriteByte<Port>,
                "bno08x: the UartWriteByte hook must be `bool UartWriteByte(uint8_t) noexcept`");
  static_assert((kTransportOf<Port> != Transport::UartShtp) || detail::HasUartWriteByte<Port> ||
                    detail::PacesUartTx<Port>,
                "bno08x: a UART-SHTP port without UartWriteByte must set `static constexpr bool "
                "kPortPacesUartTx = true`");
  static_assert(
      !detail::DeclaresUartWrite<Port> || detail::HasUartWrite<Port>,
      "bno08x: the UartWrite hook must be `bool UartWrite(const uint8_t*, std::size_t) noexcept`");

public:
  /** @brief The transport of this port. */
  static constexpr Transport kTransport = Port::kTransport;

  /**
   * @brief Bind the driver to a port. No I/O, cannot fail.
   * @param port The port; it must outlive the driver.
   */
  explicit Bno08x(Port& port) noexcept
      : port_(port), rx_(stats_, cfg_), uart_(stats_), rvc_(stats_, cfg_),
        adv_(detail::DefaultAdvert()) {}

  /**
   * @brief Start a session: validate `cfg`, bring the bus up, reset the hub and begin waiting for
   * its advertisement. Returns at once; `Poll()` does the rest and `Ready()` turns true. Called
   * again, it ends every waiting operation with `NotInitialized` and starts a new session.
   * @param cfg The configuration; `InvalidArgument` (and nothing sent) when `cfg.Valid()` is false.
   * @return `Bus` when the port could not start; `InvalidArgument` for a bad configuration.
   */
  Result<void> Begin(const Config& cfg) noexcept;

  /**
   * @brief Advance the session: read what the hub offers, send the next request, check the time
   * limits. Never waits on the hub; bounded work per call.
   * @param now_us The current time of the port's `NowUs()` counter.
   */
  void Poll(uint32_t now_us) noexcept;

  /** @return The state of the session. */
  [[nodiscard]] State GetState() const noexcept {
    return state_;
  }
  /** @return true when operations and events flow (state `Ready`). */
  [[nodiscard]] bool Ready() const noexcept {
    return state_ == State::Ready;
  }

  /**
   * @brief Pulse NRST (needs the `SetReset` hook) and restart the session; pending operations
   * complete with `HubReset`.
   * @return `Unsupported` without the hook, `NotInitialized` before `Begin()`, `Bus` on a pin
   * failure.
   */
  Result<void> HardReset() noexcept;

  /** @brief Reset the hub with the executable-channel command (DS p23). @return The operation. */
  Result<OpId> Reset() noexcept;
  /** @brief Executable channel "on": leave sleep, enable the configured sensors. @return The
   * operation. */
  Result<OpId> HubOn() noexcept;
  /** @brief Executable channel "sleep": only always-on and wake sensors keep running. @return The
   * operation. */
  Result<OpId> HubSleep() noexcept;

  /**
   * @brief Configure a sensor (Set Feature, RM S6.5.4). Completes when the request is written (or
   * on the hub's reply with `Config::set_feature_confirm`).
   * @param id The sensor.
   * @param fc The common feature report; `interval_us` 0 turns the sensor off.
   * @return The operation, or `Variant`, `Unsupported`, `InvalidArgument`, `Refused`,
   * `NotInitialized`, `Busy`.
   */
  Result<OpId> SetFeature(SensorId id, const FeatureConfig& fc) noexcept;
  /**
   * @brief Ask the hub for a sensor's feature report (RM S6.5.3); the answer updates the shadow.
   * @param id The sensor.
   * @return The operation, or an error as for `SetFeature()`.
   */
  Result<OpId> GetFeature(SensorId id) noexcept;
  /**
   * @brief The driver's shadow of a sensor's feature report (what the hub was last told or said).
   * @param id The sensor.
   * @return The shadow, or `InvalidArgument` for an ID the driver does not know.
   */
  [[nodiscard]] Result<FeatureConfig> Feature(SensorId id) const noexcept;
  /**
   * @brief Force a sensor to flush its batch (RM S6.5.6); completes on the Flush Completed report.
   * @param id The sensor.
   * @return The operation.
   */
  Result<OpId> Flush(SensorId id) noexcept;

  /** @brief Request the product ID (RM S6.3.1). @return The operation. */
  Result<OpId> ReadProductId() noexcept;
  /**
   * @return Entry 0 of the last Product ID request (the first response, RM S6.3.2); `valid` is
   * false before one arrived. Later entries of the same request are in `ProductEntries()`.
   */
  [[nodiscard]] const ProductId& Product() const noexcept {
    return product_;
  }
  /**
   * @return The entries of the last Product ID request, in the order they arrived: at most
   * `Config::product_id_entries` (ambiguity A33). A response that arrives while no request waits is
   * counted in `Stats::product_id_extra` and dropped.
   */
  [[nodiscard]] std::span<const ProductId> ProductEntries() const noexcept {
    return std::span<const ProductId>{product_entries_}.first(product_count_);
  }
  /**
   * @brief Read a sensor's metadata record (RM S5.1); the result is available from `GetMetadata()`.
   * @param id The sensor.
   * @return The operation; `Unsupported` for a sensor that has no record.
   */
  Result<OpId> ReadMetadata(SensorId id) noexcept;
  /**
   * @param id The sensor.
   * @return The cached metadata of a sensor, or `nullptr` when it was not read (or has none).
   */
  [[nodiscard]] const Metadata* GetMetadata(SensorId id) const noexcept;

  /**
   * @brief Read an FRS record into `out` (RM S6.3.6).
   * @param record The record type (`reg::kFrs*`).
   * @param out Receives the words; it must outlive the operation.
   * @return The operation.
   */
  Result<OpId> ReadFrs(uint16_t record, FrsBuffer& out) noexcept;
  /**
   * @brief Write an FRS record (RM S6.3.3 to S6.3.5), then read it back and compare unless
   * `Config::frs_verify` is false.
   * @param record The record type.
   * @param words The record; at most `kFrsMaxWords`. The driver copies it.
   * @return The operation; the `OpDone` carries `Refused` and the hub's status in `detail` on
   * failure.
   */
  Result<OpId> WriteFrs(uint16_t record, std::span<const uint32_t> words) noexcept;
  /**
   * @brief Erase an FRS record (a write of length 0).
   * @param record The record type.
   * @return The operation.
   */
  Result<OpId> EraseFrs(uint16_t record) noexcept;

  /**
   * @brief Tare now (RM S6.4.3.1).
   * @param axes The axes.
   * @param basis The rotation vector it is based on.
   * @return The operation, or `InvalidArgument` for a mask other than All and ZOnly (see
   * `Config::tare_any_mask`).
   */
  Result<OpId> Tare(AxisMask axes, TareBasis basis) noexcept;
  /** @brief Persist the last tare to flash (RM S6.4.3.2). @return The operation. */
  Result<OpId> PersistTare() noexcept;
  /**
   * @brief Set the run-time reorientation (RM S6.4.3.3).
   * @param q The quaternion, Q14.
   * @return The operation.
   */
  Result<OpId> SetReorientation(const Quat14& q) noexcept;
  /** @brief Clear the run-time reorientation (all-zero quaternion). @return The operation. */
  Result<OpId> ClearReorientation() noexcept;

  /**
   * @brief Configure the motion engine's calibration (RM S6.4.6.1).
   * @param cal The enables.
   * @return The operation.
   */
  Result<OpId> ConfigureMeCal(const MeCal& cal) noexcept;
  /**
   * @brief Read the calibration enables (RM S6.4.6.2); they come back in the `OpDone` values.
   * @return The operation.
   */
  Result<OpId> GetMeCal() noexcept;
  /** @brief Save the dynamic calibration data (RM S6.4.5). @return The operation. */
  Result<OpId> SaveDcd() noexcept;
  /**
   * @brief Turn the periodic DCD save on or off (RM S6.4.7).
   * @param enable true to enable.
   * @return The operation.
   */
  Result<OpId> SetPeriodicDcd(bool enable) noexcept;
  /** @brief Clear the DCD and reset the hub (RM S6.4.9). @return The operation. */
  Result<OpId> ClearDcdAndReset() noexcept;
  /**
   * @brief Ask for the oscillator type (RM S6.4.8): 0 internal, 1 crystal, 2 external clock.
   * @return The operation.
   */
  Result<OpId> GetOscillator() noexcept;
  /** @brief Re-initialise the sensor hub (RM S6.4.4). @return The operation. */
  Result<OpId> Initialize() noexcept;
  /**
   * @brief Read a sensor's counters (RM S6.4.2.1); four counts in the `OpDone` values.
   * @param id The sensor.
   * @return The operation.
   */
  Result<OpId> GetCounts(SensorId id) noexcept;
  /**
   * @brief Clear a sensor's counters (RM S6.4.2.2).
   * @param id The sensor.
   * @return The operation.
   */
  Result<OpId> ClearCounts(SensorId id) noexcept;
  /**
   * @brief Ask for the SH-2 error queue (RM S6.4.1).
   * @param severity Errors of this severity and higher.
   * @return The operation.
   */
  Result<OpId> ReportErrors(uint8_t severity) noexcept;
  /**
   * @brief Start the simple calibration (RM S6.4.10).
   * @param interval_us Sample interval in microseconds; 0 means the default 10000.
   * @return The operation.
   */
  Result<OpId> StartSimpleCal(uint32_t interval_us) noexcept;
  /** @brief Finish the simple calibration; the status is in `OpDone.detail`. @return The operation.
   */
  Result<OpId> FinishSimpleCal() noexcept;
  /**
   * @brief Tell the interactive calibration the motion intent (BNO086, RM S6.4.12).
   * @param intent The intent.
   * @return The operation, or `Variant` on another chip when `Config::strict_variant` is set.
   */
  Result<OpId> SetMotionIntent(MotionIntent intent) noexcept;
  /**
   * @brief Report a wheel measurement for dead reckoning (RM S6.4.13).
   * @param s The sample.
   * @return The operation.
   */
  Result<OpId> SendWheel(const WheelSample& s) noexcept;

  /**
   * @brief UART-RVC host input, guarded (ambiguity A8): the `$MIB` commands of RM S3.1.2 are not
   * verified. `Unsupported` unless `Config::rvc_inputs_experimental` is set.
   * @param text The ASCII command including its checksum, for example `"$MIB,RESET*87"`.
   * @return `Unsupported`, or `Bus` when the port refused a byte.
   */
  Result<void> RvcSend(std::span<const uint8_t> text) noexcept;

  /**
   * @brief Take the oldest event.
   * @param[out] out The event.
   * @return true when one was available.
   */
  bool PopEvent(Event& out) noexcept;
  /** @return The counters. */
  [[nodiscard]] const Stats& GetStats() const noexcept {
    return stats_;
  }
  /**
   * @brief The most recent sample of a sensor.
   * @param id The sensor.
   * @return The sample, or `NotAdvertised` when the sensor never reported.
   */
  [[nodiscard]] Result<Event> Latest(SensorId id) const noexcept;
  /**
   * @brief The age of a sample.
   * @param ev A sample.
   * @return Microseconds between the sample and the last `Poll()` time, modulo 2^32: an age above
   * 71.6 minutes reads short.
   */
  [[nodiscard]] uint32_t AgeUs(const Event& ev) const noexcept {
    return now_us_ - ev.t_us;
  }
  /** @brief Bytes of the last transfer that `TraceLastTransfer()` keeps. */
  static constexpr std::size_t kTraceBytes = 32;
  /**
   * @brief Copy the first bytes of the most recent SHTP transfer the hub sent (the 4-byte header
   * first), for bring-up and fault finding. No I/O: the driver never reads the bus on its own
   * account, because a raw read would race the framing engine (handbook KB-CPP-129).
   * @param[out] out Receives at most `kTraceBytes` bytes.
   * @return The number of bytes copied; 0 before the first transfer.
   */
  [[nodiscard]] std::size_t TraceLastTransfer(std::span<uint8_t> out) const noexcept {
    const std::size_t n = std::min({out.size(), trace_len_, kTraceBytes});
    std::copy_n(trace_.begin(), n, out.begin());
    return n;
  }
  /** @return The full length of the most recent transfer in bytes (it may exceed `kTraceBytes`); 0
   * before the first. */
  [[nodiscard]] std::size_t GetLastTransferLength() const noexcept {
    return trace_total_;
  }
  /** @return The limits the hub advertised (zero for a field it did not advertise). */
  [[nodiscard]] const detail::Advert& Advertised() const noexcept {
    return adv_;
  }

private:
  static constexpr std::size_t kFrameBuf =
      (kTransport == Transport::UartShtp) ? ((2U * Limits::kRxCargo) + 8U) : 8U;
  static constexpr std::size_t kUartTxBuf =
      (kTransport == Transport::UartShtp) ? ((2U * Limits::kTxCargo) + 8U) : 8U;
  static constexpr std::size_t kReadBuf = Limits::kRxCargo + reg::kShtpHeaderLen;
  static constexpr std::size_t kZeroBuf = (kTransport == Transport::Spi) ? kReadBuf : 1U;
  static constexpr std::size_t kMaxReadsPerPoll = 8;
  static constexpr uint32_t kHubUnknownCause = 0xFF;
  static constexpr uint32_t kRestoreTries = 3; ///< Failed restore writes tried again per restart.

  // session
  Result<void> startReset() noexcept;
  void stepReset() noexcept;
  void beginWaitAdvert() noexcept;
  void checkStartup() noexcept;
  void checkOpTimeout() noexcept;
  void becomeReady() noexcept;
  [[nodiscard]] bool inStartupWindow() const noexcept;
  void hubRestarted(uint8_t trigger) noexcept;
  void noteViolation() noexcept;
  void expireWindows() noexcept;
  void recover() noexcept;
  void serviceRestore() noexcept;
  void restoreFailed(uint8_t sensor) noexcept;
  void maybeReady() noexcept;
  void serviceFaulted() noexcept;
  void releaseWake() noexcept;
  void resetTxPath() noexcept;
  void abortUartTx() noexcept;
  // receive
  void pumpRx() noexcept;
  void pumpRxBus() noexcept;
  void pumpRxUart() noexcept;
  void pumpRxRvc() noexcept;
  bool intActive() noexcept;
  uint32_t takeIntTime() noexcept;
  bool readTransfer() noexcept;
  void onTransfer(std::span<const uint8_t> transfer) noexcept;
  void onUartFrame(std::span<const uint8_t> frame) noexcept;
  void dispatch(uint8_t channel, std::span<const uint8_t> cargo) noexcept;
  void onCommandChannel(std::span<const uint8_t> cargo) noexcept;
  void onExecutable(std::span<const uint8_t> cargo) noexcept;
  void onControl(std::span<const uint8_t> cargo) noexcept;
  void onControlReport(std::span<const uint8_t> rep) noexcept;
  void onInput(std::span<const uint8_t> cargo, bool wake) noexcept;
  void onGyroRv(std::span<const uint8_t> cargo) noexcept;
  void emitSensor(std::span<const uint8_t> rep, bool wake, int64_t ref_ticks) noexcept;
  void onFeatureResponse(std::span<const uint8_t> rep) noexcept;
  void onProductId(std::span<const uint8_t> rep) noexcept;
  void onFrsWriteResponse(std::span<const uint8_t> rep) noexcept;
  void onFrsReadResponse(std::span<const uint8_t> rep) noexcept;
  void onCommandResponse(std::span<const uint8_t> rep) noexcept;
  void onFlushDone(std::span<const uint8_t> rep) noexcept;
  void onSh2Initialize(std::span<const uint8_t> rep) noexcept;
  void fillCommandResult(const detail::Op& op, std::span<const uint8_t> rep) noexcept;
  [[nodiscard]] static bool commandIsFinal(const detail::Op& op,
                                           std::span<const uint8_t> rep) noexcept;
  [[nodiscard]] static Error commandError(const detail::Op& op, std::span<const uint8_t> rep,
                                          uint32_t& detail_word) noexcept;
  void parseMetadata(uint16_t record, uint8_t sensor) noexcept;
  void clearSession() noexcept;
  [[nodiscard]] bool frsBusy() const noexcept;
  // events
  void pushEvent(const Event& ev) noexcept;
  void emitOpDone(const detail::Op& op, Error err, uint32_t detail_word,
                  const std::array<int32_t, kEventValues>* vals) noexcept;
  // operations
  Result<OpId> enqueue(const detail::Op& op) noexcept;
  [[nodiscard]] OpId queuedStop(const detail::Op& op) const noexcept;
  bool evictForStop() noexcept;
  void removeAt(std::size_t index, Error err) noexcept;
  [[nodiscard]] Result<void> checkStop() const noexcept;
  Result<OpId> enqueueStopExec(uint8_t code) noexcept;
  Result<OpId> enqueueCommand(uint8_t cmd, const std::array<uint8_t, 9>& p,
                              bool no_response) noexcept;
  Result<OpId> enqueueExec(uint8_t code, bool silent = false) noexcept;
  Result<void> checkReady(bool needs_shtp) const noexcept;
  /// The interval a sensor has or is about to have: the last queued request, else the confirmed
  /// one.
  [[nodiscard]] uint32_t wantedInterval(std::size_t sensor) const noexcept;
  [[nodiscard]] bool userFeatureQueued(std::size_t sensor) const noexcept;
  /// The error a `SetFeature` request would get before anything is queued, `Ok` when it may go.
  [[nodiscard]] Error checkFeature(SensorId id, const FeatureConfig& fc, bool off) const noexcept;
  [[nodiscard]] Error checkFeatureOptions(SensorId id, const FeatureConfig& fc,
                                          bool off) const noexcept;
  [[nodiscard]] bool headWaits(detail::OpKind kind) const noexcept;
  void startHead() noexcept;
  void completeHead(Error err, uint32_t detail_word) noexcept;
  void completeHeadWith(Error err, uint32_t detail_word,
                        const std::array<int32_t, kEventValues>* vals) noexcept;
  void failAll(Error err, bool keep_stops = false) noexcept;
  void afterComplete(const detail::Op& op, Error err) noexcept;
  void enterFaulted(Error err) noexcept;
  void toFront() noexcept;
  void restartFailed() noexcept;
  void afterSent() noexcept;
  void frsReadFinished(Error err, uint32_t detail_word) noexcept;
  void frsWriteStep() noexcept;
  void frsStartVerify() noexcept;
  // transmit
  Result<void> queueTx(uint8_t channel, std::span<const uint8_t> payload) noexcept;
  void pumpTx() noexcept;
  Error txBus() noexcept;
  Error txSpi() noexcept;
  Error txUart() noexcept;
  void uartPump() noexcept;
  bool uartTokenOk(std::size_t total_len) noexcept;
  void uartSendBsq() noexcept;
  void txFailed(Error err) noexcept;
  void log(LogLevel level, const char* msg) noexcept {
    detail::Log(port_, level, msg);
  }
  [[nodiscard]] std::size_t maxWrite() const noexcept;
  [[nodiscard]] std::size_t maxRead() const noexcept;
  [[nodiscard]] uint8_t roleChannel(std::size_t role) const noexcept {
    return adv_.channel[role];
  }

  Port& port_;
  Config cfg_{};
  std::array<uint8_t, kTraceBytes> trace_{};
  std::size_t trace_len_{0};
  std::size_t trace_total_{0};
  Stats stats_{};
  detail::RxEngine<Limits::kRxCargo> rx_;
  detail::UartFramer<kFrameBuf> uart_;
  detail::RvcParser rvc_;
  detail::Advert adv_;
  State state_{State::Idle};
  uint32_t now_us_{0};
  uint32_t since_us_{0};
  uint32_t t_int_us_{0};
  uint32_t last_poll_us_{0};
  bool reset_low_{false};
  bool pin_reset_{false};
  bool advert_seen_{false};
  bool reset_seen_{false};
  bool init_seen_{false};
  bool restarting_{false};
  bool advert_asked_{false};
  uint32_t advert_at_us_{0}; ///< When the advertisement was last asked for (or the wait began).
  bool reset_heard_{false};  ///< A reset-complete arrived in this session (not only assumed).
  bool init_heard_{false};   ///< An initialise response arrived in this session.
  uint32_t ready_us_{0};     ///< When the session became ready.
  bool restart_event_pending_{false};
  std::array<uint8_t, reg::kChannelCount> tx_seq_{};
  // transmit
  std::array<uint8_t, Limits::kTxCargo> tx_buf_{};
  std::size_t tx_len_{0};
  std::size_t duplex_len_{0}; ///< SPI: bytes the last write's transfer read, not yet used.
  bool tx_pending_{false};
  bool wake_active_{false};
  uint32_t wake_start_us_{0};
  std::array<uint8_t, kUartTxBuf> uart_tx_{};
  std::size_t uart_tx_len_{0};
  bool uart_frame_open_{false}; ///< UART: a frame the hub has begun is not closed yet.
  std::size_t uart_tx_pos_{0};
  bool uart_tx_cargo_{false};
  uint32_t uart_last_byte_us_{0};
  bool token_valid_{false};
  bool bsq_out_{false};
  uint32_t bsq_us_{0};
  uint16_t token_bytes_{0};
  uint32_t token_us_{0};
  // receive scratch
  std::array<uint8_t, kReadBuf> rd_buf_{};
  std::array<uint8_t, kZeroBuf> zeros_{};
  static constexpr std::size_t kRvcChunk = 32; ///< bytes asked of the port per UART-RVC read
  /// UART receive chunk: a member, so a DMA-filled read never lands in a dead stack frame
  /// (KB-HW-019).
  std::array<uint8_t, 64> uart_rx_chunk_{};
  // violations
  uint32_t viol_start_us_{0};
  uint32_t viol_count_{0};
  // operations
  std::array<detail::Op, Limits::kOpQueue> ops_{};
  std::size_t op_head_{0};
  std::size_t op_count_{0};
  OpId next_op_{1};
  uint8_t cmd_seq_{0};
  // FRS
  std::array<uint32_t, kFrsMaxWords> frs_wr_{};
  std::size_t frs_wr_len_{0};
  std::size_t frs_wr_pos_{0};
  FrsBuffer scratch_{};
  FrsBuffer* frs_dst_{nullptr};
  // features, samples, metadata
  std::array<FeatureConfig, kSensorSlots> features_{};
  std::array<FeatureConfig, kSensorSlots> desired_{};
  std::array<int32_t, kEventValues> op_v_{};
  std::size_t restore_next_{kSensorSlots};
  uint32_t restore_tries_{0}; ///< Restore writes tried again since the last restart.
  std::array<Event, kSensorSlots> latest_{};
  std::array<bool, kSensorSlots> have_latest_{};
  std::array<uint8_t, kSensorSlots> last_report_seq_{};
  std::array<Metadata, kSensorSlots> metadata_{};
  ProductId product_{};
  std::array<ProductId, 5> product_entries_{};
  std::size_t product_count_{0};
  // events
  std::array<Event, Limits::kEventQueue> events_{};
  std::size_t ev_head_{0};
  std::size_t ev_count_{0};
  uint32_t pending_dropped_{0};
  // RVC
  Event rvc_event_{};
};

} // namespace bno08x

// codeql::misra_deviation_next_line(MD-CPP-009)
#include "../src/bno08x.ipp" // IWYU pragma: export

#endif // BNO08X_HPP
