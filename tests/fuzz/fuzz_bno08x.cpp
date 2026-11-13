/**
 * @file fuzz_bno08x.cpp
 * @brief Fuzz target: the driver over a link whose bytes the fuzzer controls, on every transport.
 *
 * What the driver reads comes from the hub, so the bytes it reads are the untrusted input. The
 * fuzzer's bytes choose a transport and a configuration, then a sequence of driver calls. On every
 * read they also choose what the link does: pass the hub's bytes, flip a bit, replace the data by
 * fuzzer bytes, or fail. The hub model (tests/model) answers in between, so the driver also gets
 * well-formed traffic to build on.
 *
 * After every step the target aborts, naming the line, when a promise of the driver breaks:
 *
 * - F1 a failed call returns an `Error` the enum defines and a value-initialised result;
 * - F2 an operation is accepted only in state `Ready` (UART-RVC: never), except a stop request
 * (a feature switched off, `Reset()`, `HubSleep()`): once `Begin()` ran it is never refused with
 * `NotInitialized`, and with `Busy` only while at most one other operation of the caller waits
 * (the head, whose write may have started; the rest of the queue is stops and the driver's own
 * requests); `InvalidArgument` sends nothing;
 * - F3 every accepted operation yields exactly one `OpDone`, never for an unknown or finished id;
 * - F4 the events popped between two polls never exceed the queue plus the `Dropped` marker;
 * - F5 every event has a defined kind, and a sensor event a defined sensor;
 * - F6 the statistics counters never decrease;
 * - F7 `Faulted` is left only by `Begin()` or `HardReset()`;
 * - F8 `Latest()` and `Feature()` never fail for a defined sensor once Ready, and never read out of
 * bounds;
 * - F9 the Euler conversion of any quaternion stays inside its documented ranges;
 * - F10 an FRS read never reports more words than the buffer holds;
 * - F11 the time may wrap: every call works with a clock that starts near the wrap point;
 * - F12 SPI: PS0/WAKE is high at every NRST edge and until the first H_INTN after it (DS p18), and
 * in `Faulted` while nothing is queued;
 * - F13 UART-SHTP: Buffer Status Notifications may be lost (the hub model drops them on request);
 * - F14 a stop that answers with an operation already queued is the newest request still waiting
 * for its target (the sensor's feature, or the hub's power state) and asks for the same thing: the
 * newest request wins;
 * - F15 the advertisement may give any report length, shorter than the report header included,
 * for any report ID, those that are not sensors included;
 * - F16 a sensor whose newest request was a switch-off stays off after a hub restart that followed
 * that request's end: auto-restore never re-enables it;
 * - `Begin()` may be called again at any time: every waiting operation still gets its `OpDone`.
 *
 * Built two ways (tests/fuzz/CMakeLists.txt): with libFuzzer (Clang, `-DBNO08X_BUILD_FUZZERS=ON`),
 * and as a plain executable that replays tests/fuzz/corpus/ as a CTest on any compiler.
 */
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>
#include <vector>

#include "bno08x.hpp"
#include "bno08x_euler.hpp"
#include "model/bno08x_rig.hpp"

namespace {

using bno08x::Error;
using bno08x::EventKind;
using bno08x::OpId;

#define FUZZ_REQUIRE(cond)                                                                         \
  do {                                                                                             \
    const bool fz_ok = static_cast<bool>(cond);                                                    \
    if (!fz_ok) {                                                                                  \
      static_cast<void>(                                                                           \
          std::fprintf(stderr, "fuzz invariant broken at line %d: %s\n", __LINE__, #cond));        \
      std::abort();                                                                                \
    }                                                                                              \
  } while (false)

/// The fuzzer's bytes, read front to back; zeros once they run out.
class Cursor {
public:
  Cursor(const uint8_t* d, std::size_t n) : d_(d), n_(n) {}
  uint8_t U8() {
    return (i_ < n_) ? d_[i_++] : 0U;
  }
  uint16_t U16() {
    const uint16_t lo = U8();
    return static_cast<uint16_t>(lo | (static_cast<uint32_t>(U8()) << 8U));
  }
  uint32_t U32() {
    const uint32_t lo = U16();
    return lo | (static_cast<uint32_t>(U16()) << 16U);
  }
  [[nodiscard]] bool More() const {
    return i_ < n_;
  }

private:
  const uint8_t* d_;
  std::size_t n_;
  std::size_t i_{0};
};

/// What the link does to one read. Shared by the four ports.
struct Link {
  Cursor* in{nullptr};
  /// @return 0 pass, 1 fail; otherwise `d` was changed.
  int32_t Mangle(uint8_t* d, std::size_t n) const {
    const uint8_t c = in->U8();
    switch (c & 7U) {
    case 5U:
      if (n > 0U) {
        d[in->U8() % n] ^= in->U8();
      }
      return 0;
    case 6U:
      for (std::size_t i = 0; i < n; ++i) {
        d[i] = in->U8();
      }
      return 0;
    case 7U:
      return 1;
    default:
      return 0;
    }
  }
};

struct FWire : rig::Wire {
  Link link;
};

class FI2c : public bno08x::HubInterface<FI2c> {
public:
  static constexpr bno08x::Transport kTransport = bno08x::Transport::I2c;
  explicit FI2c(FWire& w) noexcept : w_(w) {}
  bool EnsureInitialized() noexcept {
    return true;
  }
  uint32_t NowUs() noexcept {
    return w_.now_us;
  }
  bool I2cWrite(uint8_t /*addr*/, const uint8_t* d, std::size_t n) noexcept {
    return w_.hub.HostWrite(d, n);
  }
  bool I2cRead(uint8_t /*addr*/, uint8_t* d, std::size_t n) noexcept {
    if (!w_.hub.HostRead(d, n)) {
      return false;
    }
    return w_.link.Mangle(d, n) == 0;
  }
  bool IntAsserted() noexcept {
    return w_.hub.IntActive();
  }
  bool SetReset(bool level) noexcept {
    w_.hub.Nrst(level, w_.now_us);
    return true;
  }

private:
  FWire& w_;
};

/// SPI, with the NRST hook when `HasNrst` (without it the driver joins a running hub and asks for
/// its advertisement, with WAKE).
template <bool HasNrst>
class FSpi : public bno08x::HubInterface<FSpi<HasNrst>> {
public:
  static constexpr bno08x::Transport kTransport = bno08x::Transport::Spi;
  explicit FSpi(FWire& w) noexcept : w_(w) {}
  bool EnsureInitialized() noexcept {
    return true;
  }
  uint32_t NowUs() noexcept {
    return w_.now_us;
  }
  bool SpiTransfer(const uint8_t* tx, uint8_t* rx, std::size_t n) noexcept {
    if (!w_.hub.SpiTransfer(tx, rx, n)) {
      return false;
    }
    return w_.link.Mangle(rx, n) == 0;
  }
  bool SetWake(bool level) noexcept {
    w_.hub.SetWake(level);
    return true;
  }
  bool IntAsserted() noexcept {
    return !w_.int_dead && w_.hub.IntActive();
  }
  bool SetReset(bool level) noexcept
    requires(HasNrst)
  {
    w_.hub.Nrst(level, w_.now_us);
    return true;
  }

private:
  FWire& w_;
};

/// UART-SHTP or UART-RVC, with the NRST hook when `HasNrst`.
template <bno08x::Transport T, bool HasNrst>
class FUart : public bno08x::HubInterface<FUart<T, HasNrst>> {
public:
  static constexpr bno08x::Transport kTransport = T;
  explicit FUart(FWire& w) noexcept : w_(w) {}
  bool EnsureInitialized() noexcept {
    return true;
  }
  uint32_t NowUs() noexcept {
    return w_.now_us;
  }
  std::size_t UartRead(uint8_t* d, std::size_t cap) noexcept {
    const std::size_t n = w_.hub.UartTake(d, cap);
    if (n == 0U) {
      return 0U;
    }
    return (w_.link.Mangle(d, n) == 0) ? n : 0U;
  }
  bool UartWriteByte(uint8_t b) noexcept {
    w_.hub.now = w_.now_us;
    w_.hub.UartByteIn(b);
    return true;
  }
  bool SetReset(bool level) noexcept
    requires(HasNrst)
  {
    w_.hub.Nrst(level, w_.now_us);
    return true;
  }

private:
  FWire& w_;
};

/// One fuzz session over one port type.
template <typename PortT>
class Session {
public:
  Session(Cursor& in, const bno08x::Config& cfg) : in_(in), cfg_(cfg), port_(wire_), drv_(port_) {
    wire_.link.in = &in_;
    wire_.hub.chip = static_cast<hubmodel::Chip>(in_.U8() % 3U);
    wire_.now_us = 0xFFFFF000U; // F11: the clock wraps early
    wire_.hub.PowerOn(wire_.now_us);
    if constexpr (PortT::kTransport == bno08x::Transport::UartRvc) {
      wire_.hub.RvcStart();
    } else if constexpr (!bno08x::detail::HasSetReset<PortT>) {
      // without NRST the driver joins a running hub (its contract): the hub boots first
      for (uint32_t t = 0; t < 120'000U; t += 250U) {
        wire_.now_us += 250U;
        wire_.hub.Tick(wire_.now_us);
      }
    } else {
      // with NRST the driver resets the hub itself
    }
    started_ = drv_.Begin(cfg).ok();
  }

  void Run() {
    while (in_.More()) {
      step(in_.U8());
      check();
    }
    drain(4000U);
    check();
    checkSwitchedOff();
  }

private:
  using Sensor = bno08x::SensorId;

  void advance(uint32_t ms) {
    const uint32_t end = wire_.now_us + (ms * 1000U);
    uint32_t polls = 0;
    while (static_cast<int32_t>(end - wire_.now_us) > 0) {
      wire_.now_us += 250U;
      wire_.hub.Tick(wire_.now_us);
      drv_.Poll(wire_.now_us);
      ++polls;
      if ((polls % 8U) == 0U) {
        pump();
      }
    }
    pump();
  }
  void drain(uint32_t ms) {
    advance(ms);
  }

  /// F3, F4, F5: pop the events and account for them.
  void pump() {
    bno08x::Event e{};
    std::size_t popped = 0;
    while (drv_.PopEvent(e)) {
      ++popped;
      FUZZ_REQUIRE(popped <= bno08x::DefaultLimits::kEventQueue); // F4
      switch (e.kind) {
      case EventKind::Sensor:
      case EventKind::FeatureChanged:
      case EventKind::FlushDone:
        FUZZ_REQUIRE((bno08x::detail::ToUnderlying(e.sensor) >= 1U) &&
                     (bno08x::detail::ToUnderlying(e.sensor) <= 0x2EU));
        if (e.kind == EventKind::Sensor) {
          checkEuler(e);
        }
        break;
      case EventKind::OpDone:
        FUZZ_REQUIRE(pending_.erase(e.op) == 1U);
        static_cast<void>(stops_.erase(e.op));
        for (auto& [target, live] : live_) {
          static_cast<void>(target);
          static_cast<void>(std::erase_if(live, [&e](const Live& l) { return l.id == e.op; }));
        }
        for (auto& [sensor, req] : newest_) { // F16
          static_cast<void>(sensor);
          if ((req.id == e.op) && !req.done) {
            req.done = true;
            req.reboots_at_done = wire_.hub.c.reboots;
          }
        }
        FUZZ_REQUIRE(static_cast<uint32_t>(e.error) <= 22U);
        break;
      case EventKind::Dropped:
        dropped_ = true; // an OpDone may be among the lost events
        break;
      case EventKind::Reset:
      case EventKind::HubError:
      case EventKind::Rvc:
      case EventKind::Sh2Error:
        break;
      default:
        FUZZ_REQUIRE(false);
      }
    }
    if (pending_.empty()) {
      dropped_ = false; // every OpDone arrived: the bookkeeping is exact again
    }
  }

  /// F16, after the final drain: only when the session is up and nothing waits.
  void checkSwitchedOff() {
    if (!drv_.Ready() || !pending_.empty() || dropped_) {
      return;
    }
    for (const auto& [sensor, req] : newest_) {
      if (req.off && req.done && (wire_.hub.c.reboots > req.reboots_at_done)) {
        FUZZ_REQUIRE(wire_.hub.feat[sensor].interval_us == 0U);
      }
    }
  }

  void checkEuler(const bno08x::Event& e) {
    if ((e.sensor == Sensor::RotationVector) || (e.sensor == Sensor::GameRotationVector)) {
      const auto eu = bno08x::ToEulerCentiDeg(e.v[0], e.v[1], e.v[2], e.v[3]);
      FUZZ_REQUIRE((eu.yaw >= -18100) && (eu.yaw <= 18100));
      FUZZ_REQUIRE((eu.pitch >= -9100) && (eu.pitch <= 9100));
      FUZZ_REQUIRE((eu.roll >= -18100) && (eu.roll <= 18100));
    }
  }

  /// F1, F2, F3 for a call that returns a `Result<OpId>`. `stop` is true for a stop request (a
  /// feature switched off, `Reset()`, `HubSleep()`), which the driver accepts in any state after
  /// `Begin()`; the same stop asked again while it waits returns the id it already has.
  /// `target` names what a feature or power request sets (F14): the sensor ID, `kPower` for
  /// `Reset()`, `HubSleep()` and `HubOn()`, `kNoTarget` for any other request; `key` says what it
  /// asks for (0 a switch-off; the power request: 1 sleep, 2 reset, 3 on).
  void submit(const bno08x::Result<OpId>& r, bool stop = false, uint32_t target = kNoTarget,
              uint32_t key = 0U) {
    constexpr bool kRvc = PortT::kTransport == bno08x::Transport::UartRvc;
    if (r.ok()) {
      FUZZ_REQUIRE(stop || drv_.Ready());
      FUZZ_REQUIRE(!kRvc);
      const bool fresh = pending_.insert(r.value).second;
      FUZZ_REQUIRE(fresh || (stop && (stops_.count(r.value) == 1U)));
      if (target != kNoTarget) {
        std::vector<Live>& live = live_[target];
        if (fresh) {
          live.push_back({r.value, key});
        } else {  // F14
          pump(); // a request displaced by an earlier call has its OpDone waiting
          FUZZ_REQUIRE(dropped_ ||
                       (!live.empty() && (live.back().id == r.value) && (live.back().key == key)));
        }
      }
      if (stop) {
        static_cast<void>(stops_.insert(r.value));
      }
      FUZZ_REQUIRE(pending_.size() <= 64U);
    } else {
      FUZZ_REQUIRE(static_cast<uint32_t>(r.error) <= 22U);
      FUZZ_REQUIRE(r.value == OpId{});
      FUZZ_REQUIRE(r.error != Error::Ok);
      if (stop && started_ && !kRvc) { // F2: a stop is never refused for the state
        FUZZ_REQUIRE(r.error != Error::NotInitialized);
        if (r.error == Error::Busy) {
          pump(); // operations displaced by earlier calls have their OpDone waiting
          FUZZ_REQUIRE(dropped_ || ((pending_.size() - stops_.size()) <= 1U));
        }
      }
    }
  }

  Sensor pickSensor() {
    const uint8_t b = in_.U8();
    // The fuzzer builds arbitrary values on purpose.
    const uint8_t raw = ((b % 3U) == 0U) ? b : static_cast<uint8_t>(1U + (b % 0x2DU));
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
    return static_cast<Sensor>(raw);
  }

  void step(uint8_t op) {
    switch (op % 26U) {
    case 0U:
    case 1U:
    case 2U:
      advance(1U + (in_.U8() % 40U));
      break;
    case 3U: {
      bno08x::FeatureConfig fc{};
      fc.interval_us = in_.U32() % 2000000U;
      fc.batch_us = ((in_.U8() & 1U) != 0U) ? in_.U32() % 3000000U : 0U;
      fc.change_enable = (in_.U8() & 1U) != 0U;
      fc.sensor_config = in_.U32();
      const Sensor id = pickSensor();
      const bool off = fc.interval_us == 0U;
      const bno08x::Result<OpId> r = drv_.SetFeature(id, fc);
      const uint8_t slot = bno08x::detail::ToUnderlying(id);
      if (r.ok() && !pending_.contains(r.value)) {
        newest_[slot] = Newest{r.value, off, false, 0U}; // F16: a new request, not a merged stop
      }
      submit(r, off, slot, off ? 0U : 1U);
      break;
    }
    case 4U:
      submit(drv_.GetFeature(pickSensor()));
      break;
    case 5U:
      submit(drv_.Flush(pickSensor()));
      break;
    case 6U:
      submit(drv_.ReadProductId());
      break;
    case 7U:
      submit(drv_.ReadMetadata(pickSensor()));
      break;
    case 8U:
      submit(drv_.ReadFrs(in_.U16(), frs_));
      FUZZ_REQUIRE(frs_.length <= frs_.words.size());
      break;
    case 9U: {
      std::array<uint32_t, 4> w{in_.U32(), in_.U32(), in_.U32(), in_.U32()};
      const uint16_t record = in_.U16(); // one cursor read per statement: the same input means
      const std::size_t words = 1U + (in_.U8() % 4U); // the same calls on every compiler
      submit(drv_.WriteFrs(record, std::span<const uint32_t>(w.data(), words)));
      break;
    }
    case 10U:
      submit(drv_.EraseFrs(in_.U16()));
      break;
    case 11U: {
      const uint8_t axes = in_.U8() % 9U;
      const uint8_t basis = in_.U8() % 7U;
      // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange): arbitrary values on purpose
      submit(drv_.Tare(static_cast<bno08x::AxisMask>(axes), static_cast<bno08x::TareBasis>(basis)));
      break;
    }
    case 12U:
      submit(drv_.PersistTare());
      break;
    case 13U: {
      const bno08x::Quat14 q{static_cast<int16_t>(in_.U16()), static_cast<int16_t>(in_.U16()),
                             static_cast<int16_t>(in_.U16()), static_cast<int16_t>(in_.U16())};
      submit(drv_.SetReorientation(q));
      break;
    }
    case 14U:
      submit(drv_.ConfigureMeCal(
          {(in_.U8() & 1U) != 0U, (in_.U8() & 1U) != 0U, (in_.U8() & 1U) != 0U, false, false}));
      break;
    case 15U:
      submit(drv_.SaveDcd());
      break;
    case 16U:
      submit(drv_.GetCounts(pickSensor()));
      break;
    case 17U:
      submit(drv_.StartSimpleCal(in_.U32() % 200000U));
      break;
    case 18U:
      submit(drv_.SetMotionIntent(static_cast<bno08x::MotionIntent>(in_.U8() % 6U)));
      break;
    case 19U: {
      const uint8_t which = in_.U8() % 3U;
      if (which == 0U) {
        submit(drv_.HubSleep(), true, kPower, 1U);
      } else if (which == 1U) {
        submit(drv_.Reset(), true, kPower, 2U);
      } else {
        submit(drv_.HubOn(), false, kPower, 3U);
      }
      break;
    }
    case 20U:
      submit(drv_.SendWheel(
          {static_cast<uint8_t>(in_.U8() % 3U), in_.U32(), in_.U16(), (in_.U8() & 1U) != 0U}));
      break;
    case 21U: {
      static constexpr std::array<uint8_t, 11> kText{'$', 'M', 'I', 'B', ',', '0',
                                                     ',', '1', '*', '0', '0'};
      static_cast<void>(drv_.RvcSend(std::span<const uint8_t>(kText).first(1U + (in_.U8() % 11U))));
      break;
    }
    case 22U:
      if ((in_.U8() & 1U) == 0U) {
        static_cast<void>(drv_.HardReset());
      } else {
        started_ = drv_.Begin(cfg_).ok(); // a new session: the waiting operations end
      }
      was_faulted_ = false;
      break;
    case 23U: { // an over-long UART frame: start flag, protocol ID, many bytes, maybe an end flag
      const std::size_t n = 8U + (static_cast<std::size_t>(in_.U16()) % 3000U);
      hubmodel::Bytes frame(n, in_.U8());
      frame.front() = 0x7EU;
      frame[1] = static_cast<uint8_t>(in_.U8() & 1U);
      if ((in_.U8() & 1U) != 0U) {
        frame.push_back(0x7EU);
      }
      wire_.hub.RvcPush(frame);
      break;
    }
    case 24U: {
      const uint8_t k = in_.U8();
      if ((k & 0xC0U) == 0x40U) { // SPI: H_INTN stops answering WAKE, or answers again
        wire_.int_dead = !wire_.int_dead;
      } else if ((k & 0x80U) == 0U) { // F13: the next Buffer Status Notifications are lost
        wire_.hub.bsn_drop = 1U + (k % 3U);
      } else { // F15: the next advertisement gives these lengths, 0 to 15 bytes, for 0x1B, 0x2E
        wire_.hub.k.advertise_report_lengths = true; // and one ID from 0x2F to 0xFF
        wire_.hub.k.pickup_report_len = static_cast<uint8_t>(k & 0x0FU);
        wire_.hub.k.wheel_encoder_len =
            static_cast<uint8_t>((static_cast<uint32_t>(k) >> 3U) & 0x0FU);
        const uint8_t x = in_.U8();
        wire_.hub.k.extra_len_id = static_cast<uint8_t>(0x2FU + (x % 0xD1U));
        wire_.hub.k.extra_len = static_cast<uint8_t>(in_.U8() % 16U);
      }
      break;
    }
    default:
      submit(drv_.GetMeCal());
      break;
    }
  }

  void check() {
    const bno08x::Stats& s = drv_.GetStats();
    const uint32_t sum = s.short_transfer + s.length_ffff + s.null_header + s.length_short +
                         s.oversize_cargo + s.bad_channel + s.lost_cargo + s.orphan_cont +
                         s.bad_cont_len + s.dup_seq + s.seq_gap + s.nonzero_pad + s.unknown_report +
                         s.short_report + s.bad_gyrorv;
    FUZZ_REQUIRE((sum >= last_stats_sum_) ||
                 (sum < 8U)); // F6 (the sum may wrap only after 2^32 events)
    last_stats_sum_ = sum;
    const auto st = drv_.GetState();
    if (was_faulted_) {
      FUZZ_REQUIRE(st == bno08x::State::Faulted); // F7
    }
    was_faulted_ = (st == bno08x::State::Faulted);
    FUZZ_REQUIRE(wire_.hub.c.bad_strap == 0U); // F12
    if constexpr (PortT::kTransport == bno08x::Transport::Spi) {
      if ((st == bno08x::State::Faulted) && pending_.empty() && !dropped_) {
        FUZZ_REQUIRE(!wire_.hub.WakeLow()); // F12: no stop is being written
      }
    }
    if (st == bno08x::State::Ready) { // F8
      const auto l = drv_.Latest(Sensor::Accelerometer);
      FUZZ_REQUIRE(l.ok() || (l.error == Error::NotAdvertised));
      FUZZ_REQUIRE(drv_.Feature(Sensor::Accelerometer).ok());
    }
    // Any byte may be cast to a SensorId: the accessors answer, never index out of range.
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange): arbitrary values on purpose
    const auto any = static_cast<Sensor>(in_.U8());
    const bool known = bno08x::ToSensorId(static_cast<uint8_t>(any), probe_);
    FUZZ_REQUIRE(drv_.Feature(any).ok() == known);
    FUZZ_REQUIRE((drv_.Latest(any).error == Error::InvalidArgument) == !known);
    FUZZ_REQUIRE(known || (drv_.GetMetadata(any) == nullptr));
    static_cast<void>(bno08x::SensorName(any));
    static_cast<void>(bno08x::MetadataRecord(any));
  }

  Cursor& in_;
  bno08x::Config cfg_;
  FWire wire_;
  PortT port_;
  bno08x::Bno08x<PortT> drv_;
  bool started_{false};
  Sensor probe_{};
  bool was_faulted_{false};
  uint32_t last_stats_sum_{0};
  static constexpr uint32_t kNoTarget = 0xFFFF'FFFFU;
  static constexpr uint32_t kPower = 0x100U;
  bool dropped_{false};
  std::set<OpId> pending_;
  std::set<OpId> stops_;
  /// F14: one waiting request and what it asks for.
  struct Live {
    OpId id;
    uint32_t key;
  };
  /// F16: the newest feature request accepted for a sensor.
  struct Newest {
    OpId id;
    bool off;
    bool done;
    uint32_t reboots_at_done;
  };
  std::map<uint32_t, std::vector<Live>> live_; ///< F14: waiting requests per target, in order.
  std::map<uint8_t, Newest> newest_;           ///< F16, per sensor slot.
  bno08x::FrsBuffer frs_{};
};

template <typename PortT>
void Fuzz(Cursor& in) {
  bno08x::Config cfg{};
  cfg.variant = static_cast<bno08x::Variant>(in.U8() % 3U);
  cfg.strict_variant = (in.U8() & 1U) != 0U;
  cfg.auto_restore = (in.U8() & 1U) != 0U;
  cfg.set_feature_confirm = (in.U8() & 1U) != 0U;
  cfg.strict_raw = (in.U8() & 1U) != 0U;
  cfg.strict_padding = (in.U8() & 1U) != 0U;
  cfg.strict_cont_len = (in.U8() & 1U) != 0U;
  cfg.rvc_strict = (in.U8() & 1U) != 0U;
  const uint8_t t = in.U8();
  cfg.startup_timeout_ms = static_cast<uint16_t>(1U + ((t % 40U) * 25U));
  cfg.op_timeout_ms = static_cast<uint16_t>(11U + ((in.U8() % 40U) * 25U)); // above the WAKE limit
  cfg.uart_bsq_retry_ms = static_cast<uint16_t>(1U + (in.U8() % 50U));
  Session<PortT> s(in, cfg);
  s.Run();
}

} // namespace

// NOLINTNEXTLINE(google-runtime-int): libFuzzer declares the entry point with int
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, std::size_t size) {
  if (size < 2U) {
    return 0;
  }
  Cursor in(data, size);
  const uint8_t pick = in.U8();
  const bool nrst = (pick & 0x80U) == 0U; // bit 7: SPI and UART-SHTP without the NRST hook
  switch (pick % 4U) {
  case 0U:
    Fuzz<FI2c>(in);
    break;
  case 1U:
    if (nrst) {
      Fuzz<FSpi<true>>(in);
    } else {
      Fuzz<FSpi<false>>(in);
    }
    break;
  case 2U:
    if (nrst) {
      Fuzz<FUart<bno08x::Transport::UartShtp, true>>(in);
    } else {
      Fuzz<FUart<bno08x::Transport::UartShtp, false>>(in);
    }
    break;
  default:
    Fuzz<FUart<bno08x::Transport::UartRvc, true>>(in);
    break;
  }
  return 0;
}
