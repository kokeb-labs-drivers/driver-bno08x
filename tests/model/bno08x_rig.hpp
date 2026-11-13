/**
 * @file bno08x_rig.hpp
 * @brief Ports for every transport that connect the driver to the hub model, a fault injector and
 * the `Rig` that steps time, the model and the driver together.
 */
#ifndef BNO08X_RIG_HPP
#define BNO08X_RIG_HPP

#include <bit>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "bno08x.hpp"
#include "bno08x_hub_model.hpp"

namespace rig {

/// Everything the ports share: the hub, the clock, the fault injector and the call log.
struct Wire {
  hubmodel::Hub hub;
  uint32_t now_us{0};
  uint32_t fail_writes{0};  ///< The next N writes return false.
  uint32_t fail_reads{0};   ///< The next N reads and transfers return false.
  bool int_stuck{false};    ///< H_INTN reads as asserted whatever the hub says.
  bool int_dead{false};     ///< H_INTN never reads as asserted.
  bool uart_drop_tx{false}; ///< UartWriteByte returns false (transmitter busy).
  int32_t uart_tx_budget{
      -1};                 ///< UartWriteByte accepts this many more bytes, then fails (-1 always).
  bool ensure_ok{true};    ///< EnsureInitialized result.
  bool reset_fails{false}; ///< SetReset returns false.
  int32_t fail_xfer_after{-1}; ///< SPI: this many transfers succeed, then one fails (-1 never).
  uint32_t reads{0};
  uint32_t writes{0};
  uint32_t xfers{0};
  uint32_t wake_calls{0};
  uint32_t reset_edges{0};
  uint32_t bootn_calls{0};
  uint32_t wake_low_us{0};  ///< When WAKE last went low.
  uint32_t wake_high_us{0}; ///< When WAKE last went high.
  bool nrst{true};
  uint32_t nrst_rise_us{0};  ///< When NRST last went high.
  uint32_t first_read_us{0}; ///< The first bus read after `nrst_rise_us` (0 none yet).
  std::vector<std::string> log;
  uint32_t last_ts{0};
  bool have_ts{false};
};

/// A tuple of the optional hooks a port declares.
template <bool Reset, bool Bootn, bool Ts, bool Logging>
class I2cPortT : public bno08x::HubInterface<I2cPortT<Reset, Bootn, Ts, Logging>> {
public:
  static constexpr bno08x::Transport kTransport = bno08x::Transport::I2c;
  explicit I2cPortT(Wire& w) noexcept : w_(w) {}
  bool EnsureInitialized() noexcept {
    return w_.ensure_ok;
  }
  uint32_t NowUs() noexcept {
    return w_.now_us;
  }
  bool I2cWrite(uint8_t /*addr*/, const uint8_t* d, std::size_t n) noexcept {
    ++w_.writes;
    if (w_.fail_writes > 0U) {
      --w_.fail_writes;
      return false;
    }
    return w_.hub.HostWrite(d, n);
  }
  bool I2cRead(uint8_t /*addr*/, uint8_t* d, std::size_t n) noexcept {
    ++w_.reads;
    if (w_.first_read_us == 0U) {
      w_.first_read_us = w_.now_us;
    }
    if (w_.fail_reads > 0U) {
      --w_.fail_reads;
      return false;
    }
    return w_.hub.HostRead(d, n);
  }
  bool IntAsserted() noexcept {
    return w_.int_stuck || (!w_.int_dead && w_.hub.IntActive());
  }
  bool SetReset(bool level) noexcept
    requires(Reset)
  {
    if (w_.reset_fails) {
      return false;
    }
    w_.hub.Nrst(level, w_.now_us);
    w_.nrst = level;
    ++w_.reset_edges;
    if (level) {
      w_.nrst_rise_us = w_.now_us;
      w_.first_read_us = 0U;
    }
    return true;
  }
  bool SetBootn(bool level) noexcept
    requires(Bootn)
  {
    ++w_.bootn_calls;
    w_.hub.Bootn(level);
    return true;
  }
  bool TakeIntTimestamp(uint32_t& us) noexcept
    requires(Ts)
  {
    if (!w_.hub.IntActive() || (w_.have_ts && (w_.last_ts == w_.hub.IntEdgeUs()))) {
      return false;
    }
    w_.last_ts = w_.hub.IntEdgeUs();
    w_.have_ts = true;
    us = w_.last_ts;
    return true;
  }
  void Log(bno08x::LogLevel /*level*/, const char* m) noexcept
    requires(Logging)
  {
    w_.log.emplace_back(m);
  }

private:
  Wire& w_;
};

template <bool Reset, bool Ts>
class SpiPortT : public bno08x::HubInterface<SpiPortT<Reset, Ts>> {
public:
  static constexpr bno08x::Transport kTransport = bno08x::Transport::Spi;
  explicit SpiPortT(Wire& w) noexcept : w_(w) {}
  bool EnsureInitialized() noexcept {
    return w_.ensure_ok;
  }
  uint32_t NowUs() noexcept {
    return w_.now_us;
  }
  bool SpiTransfer(const uint8_t* tx, uint8_t* rx, std::size_t n) noexcept {
    ++w_.xfers;
    if (w_.fail_xfer_after == 0) {
      w_.fail_xfer_after = -1;
      return false;
    }
    if (w_.fail_xfer_after > 0) {
      --w_.fail_xfer_after;
    }
    if (w_.fail_reads > 0U) {
      --w_.fail_reads;
      return false;
    }
    return w_.hub.SpiTransfer(tx, rx, n);
  }
  bool SetWake(bool level) noexcept {
    ++w_.wake_calls;
    (level ? w_.wake_high_us : w_.wake_low_us) = w_.now_us;
    w_.hub.SetWake(level);
    return true;
  }
  bool IntAsserted() noexcept {
    return w_.int_stuck || (!w_.int_dead && w_.hub.IntActive());
  }
  bool SetReset(bool level) noexcept
    requires(Reset)
  {
    w_.hub.Nrst(level, w_.now_us);
    ++w_.reset_edges;
    return true;
  }
  bool TakeIntTimestamp(uint32_t& us) noexcept
    requires(Ts)
  {
    if (!w_.hub.IntActive() || (w_.have_ts && (w_.last_ts == w_.hub.IntEdgeUs()))) {
      return false;
    }
    w_.last_ts = w_.hub.IntEdgeUs();
    w_.have_ts = true;
    us = w_.last_ts;
    return true;
  }

private:
  Wire& w_;
};

/// UART-SHTP with a one-byte transmit hook (the driver paces the bytes).
template <bool Reset, bool Int>
class UartPortT : public bno08x::HubInterface<UartPortT<Reset, Int>> {
public:
  static constexpr bno08x::Transport kTransport = bno08x::Transport::UartShtp;
  explicit UartPortT(Wire& w) noexcept : w_(w) {}
  bool EnsureInitialized() noexcept {
    return w_.ensure_ok;
  }
  uint32_t NowUs() noexcept {
    return w_.now_us;
  }
  std::size_t UartRead(uint8_t* d, std::size_t cap) noexcept {
    ++w_.reads;
    return w_.hub.UartTake(d, cap);
  }
  bool UartWriteByte(uint8_t b) noexcept {
    ++w_.writes;
    if (w_.uart_drop_tx || (w_.uart_tx_budget == 0)) {
      return false;
    }
    if (w_.uart_tx_budget > 0) {
      --w_.uart_tx_budget;
    }
    w_.hub.now = w_.now_us;
    w_.hub.UartByteIn(b);
    return true;
  }
  bool IntAsserted() noexcept
    requires(Int)
  {
    return w_.hub.UartAvailable() > 0U;
  }
  bool SetReset(bool level) noexcept
    requires(Reset)
  {
    w_.hub.Nrst(level, w_.now_us);
    ++w_.reset_edges;
    return true;
  }

private:
  Wire& w_;
};

/// UART-SHTP whose port paces a block write itself.
class UartBlockPort : public bno08x::HubInterface<UartBlockPort> {
public:
  static constexpr bno08x::Transport kTransport = bno08x::Transport::UartShtp;
  static constexpr bool kPortPacesUartTx = true;
  explicit UartBlockPort(Wire& w) noexcept : w_(w) {}
  bool EnsureInitialized() noexcept {
    return true;
  }
  uint32_t NowUs() noexcept {
    return w_.now_us;
  }
  std::size_t UartRead(uint8_t* d, std::size_t cap) noexcept {
    return w_.hub.UartTake(d, cap);
  }
  bool UartWrite(const uint8_t* d, std::size_t n) noexcept {
    ++w_.writes;
    for (std::size_t i = 0; i < n; ++i) {
      w_.hub.now = w_.now_us + static_cast<uint32_t>(i * 100U);
      w_.hub.UartByteIn(d[i]);
    }
    return true;
  }

private:
  Wire& w_;
};

/// UART-RVC: receive only, plus a byte transmit hook for the guarded `$MIB` inputs.
class RvcPort : public bno08x::HubInterface<RvcPort> {
public:
  static constexpr bno08x::Transport kTransport = bno08x::Transport::UartRvc;
  explicit RvcPort(Wire& w) noexcept : w_(w) {}
  bool EnsureInitialized() noexcept {
    return w_.ensure_ok;
  }
  uint32_t NowUs() noexcept {
    return w_.now_us;
  }
  std::size_t UartRead(uint8_t* d, std::size_t cap) noexcept {
    return w_.hub.UartTake(d, cap);
  }
  bool UartWriteByte(uint8_t b) noexcept {
    if (w_.uart_drop_tx) {
      return false;
    }
    mib.push_back(std::bit_cast<char>(b));
    return true;
  }
  std::string mib;

private:
  Wire& w_;
};

using I2cFull = I2cPortT<true, true, true, true>;
using I2cMin = I2cPortT<false, false, false, false>;
using I2cReset = I2cPortT<true, false, false, false>;
using SpiFull = SpiPortT<true, true>;
using SpiMin = SpiPortT<false, false>;
using UartFull = UartPortT<true, true>;
using UartMin = UartPortT<false, false>;

/// Steps the clock, the hub and the driver together.
template <typename PortT, typename Limits = bno08x::DefaultLimits>
struct Rig {
  Wire wire;
  PortT port{wire};
  bno08x::Bno08x<PortT, Limits> drv{port};
  uint32_t step_us{200};
  std::vector<bno08x::Event> events;

  explicit Rig(hubmodel::Chip chip = hubmodel::Chip::Bno085) {
    wire.hub.chip = chip;
  }

  void Run(uint32_t us) {
    const uint32_t end = wire.now_us + us;
    while (static_cast<int32_t>(end - wire.now_us) > 0) {
      wire.now_us += step_us;
      wire.hub.Tick(wire.now_us);
      drv.Poll(wire.now_us);
      Collect();
    }
  }
  void RunMs(uint32_t ms) {
    Run(ms * 1000U);
  }
  void Collect() {
    bno08x::Event e{};
    while (drv.PopEvent(e)) {
      events.push_back(e);
    }
  }
  bool RunUntil(const std::function<bool()>& pred, uint32_t max_us) {
    const uint32_t end = wire.now_us + max_us;
    while (static_cast<int32_t>(end - wire.now_us) > 0) {
      if (pred()) {
        return true;
      }
      Run(step_us);
    }
    return pred();
  }
  /// Begin and run to Ready. Without an NRST hook the hub is powered first and joined while
  /// running.
  bool Start(const bno08x::Config& cfg, uint32_t max_ms = 800) {
    if constexpr (bno08x::detail::HasSetReset<PortT>) {
      wire.hub.PowerOn(wire.now_us);
    } else {
      wire.hub.PowerOn(wire.now_us);
      RunMs(120);
    }
    if (!drv.Begin(cfg).ok()) {
      return false;
    }
    return RunUntil([this] { return drv.Ready(); }, max_ms * 1000U);
  }
  [[nodiscard]] std::size_t Count(bno08x::EventKind kind) const {
    std::size_t n = 0;
    for (const bno08x::Event& e : events) {
      n += (e.kind == kind) ? 1U : 0U;
    }
    return n;
  }
  /// Waits for the `OpDone` of `id` and returns it (kind Sensor with err Timeout when none came).
  bool Await(bno08x::OpId id, bno08x::Event& out, uint32_t max_ms = 3000) {
    const auto find = [this, id, &out]() {
      for (const bno08x::Event& e : events) {
        if ((e.kind == bno08x::EventKind::OpDone) && (e.op == id)) {
          out = e;
          return true;
        }
      }
      return false;
    };
    return RunUntil(find, max_ms * 1000U);
  }
};

} // namespace rig

#endif // BNO08X_RIG_HPP
