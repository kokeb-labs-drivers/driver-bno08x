/**
 * @file test_boundaries.cpp
 * @brief Every bound at its exact edge: the time limits at the Poll() that reaches them, buffers
 * filled to the last byte, lengths one off, and the first H_INTN of a segmented cargo.
 */
#include "test_support.hpp"

using namespace ts;

namespace {

using I2cRig = rig::Rig<rig::I2cFull>;
using UartRig = rig::Rig<rig::UartFull>;

const Bytes kLengthFfff{0xFF, 0xFF, 0x00, 0x00};

/// Steps one Poll() at a time until `pred` holds; returns the time it held.
template <typename R, typename P>
uint32_t StepUntil(R& r, P pred, uint32_t max_us = 1'000'000) {
  const uint32_t end = r.wire.now_us + max_us;
  while (!pred() && (static_cast<int32_t>(end - r.wire.now_us) > 0)) {
    r.Run(r.step_us);
  }
  return r.wire.now_us;
}

} // namespace

TEST_CASE("boundaries: a gyro-integrated rotation vector cargo one byte too long is refused") {
  I2cRig r;
  REQUIRE(r.Start(Cfg()));
  r.wire.hub.Inject(5, Bytes(15, 0)); // the report is exactly 14 bytes (RM S6.5.44)
  r.RunMs(10);
  CHECK_EQ(r.drv.GetStats().bad_gyrorv, 1U);
  CHECK(!r.drv.Latest(SensorId::GyroIntegratedRotationVector).ok());
}

TEST_CASE("boundaries: an FRS record that fills the buffer exactly is read whole") {
  I2cRig r;
  REQUIRE(r.Start(Cfg()));
  std::vector<uint32_t> words(kFrsMaxWords);
  for (std::size_t i = 0; i < words.size(); ++i) {
    words[i] = static_cast<uint32_t>(i + 1U);
  }
  r.wire.hub.frs[0x2D3E] = words;
  FrsBuffer buf{};
  Event e{};
  REQUIRE(Run(r, r.drv.ReadFrs(0x2D3E, buf), e));
  CHECK_EQ(e.error, Error::Ok);
  CHECK_EQ(buf.length, kFrsMaxWords);
  CHECK_EQ(buf.words[kFrsMaxWords - 1U], static_cast<uint32_t>(kFrsMaxWords));
}

TEST_CASE("boundaries: Feature() holds what was written once the write is out") {
  I2cRig r;
  REQUIRE(r.Start(Cfg()));
  r.wire.hub.set_feature_reply = false; // no Get Feature Response updates it
  Event e{};
  REQUIRE(Run(r, r.drv.SetFeature(SensorId::Gravity, Feat(20000)), e));
  CHECK_EQ(e.error, Error::Ok);
  CHECK_EQ(r.drv.Feature(SensorId::Gravity).value.interval_us, 20000U);
}

TEST_CASE("boundaries: a protocol-0 UART frame longer than a notification is not a notification") {
  UartRig r;
  r.step_us = 100;
  REQUIRE(r.Start(Cfg()));
  r.RunMs(20);
  r.wire.hub.RvcPush(B({0x7E, 0x00, 0x00, 0x01, 0x00, 0x7E})); // three bytes after the ID
  r.RunMs(5);
  Event e{};
  REQUIRE(Run(r, r.drv.GetFeature(SensorId::Accelerometer), e, 500));
  CHECK_EQ(e.error, Error::Ok);
  CHECK_EQ(r.wire.hub.c.uart_no_token_writes, 0U);
}

TEST_CASE("boundaries: a cargo read in two transfers takes the time of its first H_INTN") {
  I2cRig r;
  REQUIRE(r.Start(Cfg()));
  r.RunMs(20);
  Bytes cargo = Base(0);
  for (uint8_t i = 0; i < 12; ++i) { // more than one transfer of the model (cargo_split)
    cargo = Cat(cargo, Report(0x01, i, 0, 0, {1, 0, 2, 0, 3, 0}));
  }
  r.wire.hub.Inject(3, cargo);
  const uint32_t edge = r.wire.hub.IntEdgeUs();
  const std::size_t before = r.events.size();
  r.RunMs(20);
  std::size_t n = 0;
  for (std::size_t i = before; i < r.events.size(); ++i) {
    if (r.events[i].kind == EventKind::Sensor) {
      CHECK_EQ(r.events[i].t_us, edge); // base 0 and delay 0: the time of the first H_INTN
      ++n;
    }
  }
  CHECK_EQ(n, 12U);
}

TEST_CASE("boundaries: UART without NRST, a resync while a query waits sends the reset at once") {
  rig::Rig<rig::UartMin> r;
  r.step_us = 100;
  Config c = Cfg();
  c.resync_limit = 1;
  c.uart_bsq_retry_ms = 500; // a query believed to be out would wait this long
  REQUIRE(r.Start(c));
  r.RunMs(20);
  r.wire.hub.bsn_drop = 1; // the query of the next write is never answered
  REQUIRE(r.drv.GetFeature(SensorId::Accelerometer).ok());
  r.RunMs(5);
  const uint32_t boots = r.wire.hub.c.reboots;
  r.wire.hub.RvcPush(B({0x7E, 0x01, 0xFF, 0xFF, 0x00, 0x00, 0x7E})); // length 0xFFFF: a resync
  r.RunMs(50);
  REQUIRE(r.drv.GetStats().resyncs == 1U);
  CHECK_EQ(r.wire.hub.c.reboots, boots + 1U);
}

TEST_CASE("boundaries: without H_INTN the reads come exactly poll_interval_ms apart") {
  I2cRig r;
  Config c = Cfg();
  c.poll_without_int = true;
  c.poll_interval_ms = 20;
  REQUIRE(r.Start(c));
  r.wire.int_dead = true;
  r.RunMs(50);
  uint32_t reads = r.wire.reads;
  std::vector<uint32_t> at;
  while (at.size() < 4U) {
    r.Run(r.step_us);
    if (r.wire.reads != reads) {
      reads = r.wire.reads;
      at.push_back(r.wire.now_us);
    }
  }
  for (std::size_t i = 1; i < at.size(); ++i) {
    CHECK_EQ(at[i] - at[i - 1U], 20'000U);
  }
}

TEST_CASE("boundaries: a violation exactly resync_window_ms after the first still counts") {
  I2cRig r;
  Config c = Cfg();
  c.resync_limit = 2;
  c.resync_window_ms = 20;
  REQUIRE(r.Start(c));
  r.RunMs(20);
  r.wire.hub.InjectRaw(kLengthFfff);
  const uint32_t first = StepUntil(r, [&r] { return r.drv.GetStats().length_ffff == 1U; });
  r.Run((20'000U - r.step_us) - (r.wire.now_us - first));
  r.wire.hub.InjectRaw(kLengthFfff); // read at the next Poll(): exactly the window later
  r.Run(r.step_us);
  REQUIRE(r.drv.GetStats().length_ffff == 2U);
  CHECK_EQ(r.drv.GetStats().resyncs, 1U);
}

TEST_CASE("boundaries: a notification exactly as old as the UART timeout is still used") {
  UartRig r;
  r.step_us = 100;
  REQUIRE(r.Start(Cfg()));
  r.RunMs(20);
  r.wire.hub.RvcPush(B({0x7E, 0x00, 0x00, 0x01, 0x7E})); // read at the next Poll()
  r.Run(r.step_us);
  r.Run(200'000U - r.step_us); // the model advertises 200 ms
  const uint32_t queries = r.wire.hub.c.bsq_in;
  REQUIRE(r.drv.GetFeature(SensorId::Accelerometer).ok());
  r.Run(r.step_us); // the write starts exactly 200 ms after the notification
  r.Run(1000);      // a query would have arrived by now (three bytes, 100 us apart)
  CHECK_EQ(r.wire.hub.c.bsq_in, queries);
}

TEST_CASE("boundaries: a query is sent again exactly uart_bsq_retry_ms after the last") {
  UartRig r;
  r.step_us = 100;
  Config c = Cfg();
  c.uart_bsq_retry_ms = 20;
  REQUIRE(r.Start(c));
  r.RunMs(20);
  r.wire.hub.bsn_drop = 1;
  const uint32_t q0 = r.wire.hub.c.bsq_in;
  REQUIRE(r.drv.GetFeature(SensorId::Accelerometer).ok());
  const uint32_t t1 = StepUntil(r, [&r, q0] { return r.wire.hub.c.bsq_in == (q0 + 1U); });
  const uint32_t t2 = StepUntil(r, [&r, q0] { return r.wire.hub.c.bsq_in == (q0 + 2U); });
  CHECK_EQ(t2 - t1, 20'000U);
}

TEST_CASE("boundaries: Begin() again on UART without NRST closes a half-sent frame") {
  rig::Rig<rig::UartMin> r;
  r.step_us = 100;
  REQUIRE(r.Start(Cfg()));
  r.RunMs(20);
  r.wire.uart_tx_budget = 6; // the query, then 3 bytes of the request frame
  REQUIRE(r.drv.GetFeature(SensorId::Accelerometer).ok());
  r.RunMs(5);
  const uint32_t closed = r.wire.hub.c.uart_frames_closed;
  r.wire.uart_tx_budget = -1;
  REQUIRE(r.drv.Begin(Cfg()).ok());
  r.RunMs(2);
  CHECK_EQ(r.wire.hub.c.uart_frames_closed, closed + 1U); // a flag byte ends it
}

TEST_CASE("boundaries: a start-up time-out with nothing queued still closes a half-sent frame") {
  rig::Rig<rig::UartMin> r;
  r.step_us = 100;
  Config c = Cfg();
  c.startup_timeout_ms = 300;
  c.op_timeout_ms = 1000;
  r.wire.hub.PowerOn(r.wire.now_us);
  r.RunMs(120);
  std::array<uint8_t, 64> junk{};
  while (r.wire.hub.UartTake(junk.data(), junk.size()) > 0U) {
    // the driver missed the advertisement, so it asks for it
  }
  r.wire.uart_tx_budget = 6;
  REQUIRE(r.drv.Begin(c).ok());
  REQUIRE(r.RunUntil([&r] { return r.drv.GetState() == State::Faulted; }, 1'000'000));
  const uint32_t closed = r.wire.hub.c.uart_frames_closed;
  r.wire.uart_tx_budget = -1;
  r.RunMs(2);
  CHECK_EQ(r.wire.hub.c.uart_frames_closed, closed + 1U);
}

TEST_CASE("boundaries: an executable-channel cargo before the advertisement is early (V16)") {
  rig::Rig<rig::I2cMin> r;
  r.wire.hub.PowerOn(r.wire.now_us);
  r.RunMs(120);
  r.wire.hub.ClearQueue(); // the advertisement went out before the driver listened
  REQUIRE(r.drv.Begin(Cfg()).ok());
  r.wire.hub.Inject(1, B({0x01})); // a reset-complete, before any advertisement
  r.RunMs(10);
  CHECK_EQ(r.drv.GetState(), State::WaitAdvert);
  CHECK_EQ(r.drv.GetStats().early_cargo, 1U);
}

TEST_CASE("boundaries: SPI: a write that carries a cargo's continuation keeps its first H_INTN") {
  rig::Rig<rig::SpiFull> r;
  REQUIRE(r.Start(Cfg()));
  r.RunMs(20);
  Bytes cargo = Base(0);
  for (uint8_t i = 0; i < 12; ++i) {
    cargo = Cat(cargo, Report(0x01, i, 0, 0, {1, 0, 2, 0, 3, 0}));
  }
  r.wire.hub.Inject(3, cargo);
  const uint32_t edge = r.wire.hub.IntEdgeUs();
  r.wire.fail_xfer_after = 1; // the header is read, the read of the rest fails
  REQUIRE(r.drv.GetFeature(SensorId::Gravity).ok()); // so the write's transfer brings the rest
  const std::size_t before = r.events.size();
  r.RunMs(20);
  std::size_t n = 0;
  for (std::size_t i = before; i < r.events.size(); ++i) {
    if (r.events[i].kind == EventKind::Sensor) {
      CHECK_EQ(r.events[i].t_us, edge);
      ++n;
    }
  }
  CHECK_EQ(n, 12U);
  CHECK_EQ(r.drv.GetStats().bus_errors, 1U);
}

TEST_CASE("boundaries: without NRST a failed request for the advertisement is asked again later") {
  rig::Rig<rig::I2cMin> r;
  r.wire.hub.PowerOn(r.wire.now_us);
  r.RunMs(120);
  r.wire.hub.ClearQueue(); // the advertisement went out before the driver listened
  r.wire.fail_writes = 1;  // the first request fails
  const Config c = Cfg();
  REQUIRE(r.drv.Begin(c).ok());
  const uint32_t t1 = StepUntil(r, [&r] { return r.wire.writes == 1U; });
  const uint32_t t2 = StepUntil(r, [&r] { return r.wire.writes == 2U; });
  CHECK_EQ(t2 - t1, static_cast<uint32_t>(c.advert_retry_ms) * 1000U);
  REQUIRE(r.RunUntil([&r] { return r.drv.Ready(); }, 1'000'000));
  CHECK_EQ(r.drv.GetStats().hub_resets, 0U);
}

TEST_CASE(
    "boundaries: without NRST an unanswered boot advertisement still needs its reset-complete") {
  rig::Rig<rig::I2cMin> r;
  r.wire.hub.boot_delay_us = 300'000; // the request fails while it boots
  r.wire.hub.k.omit_reset_complete = true;
  Config c = Cfg();
  c.startup_timeout_ms = 1000;
  r.wire.hub.PowerOn(r.wire.now_us);
  REQUIRE(r.drv.Begin(c).ok());
  r.RunMs(1500);
  CHECK(!r.drv.Ready()); // the advertisement was the boot's, not an answer: the start-up waits
  CHECK_EQ(r.drv.GetState(), State::Faulted);
}

TEST_CASE("boundaries: a command's results do not carry over to the next command") {
  I2cRig r;
  REQUIRE(r.Start(Cfg()));
  r.RunMs(20);
  r.wire.hub.k.cmd_reserved_fill = 0x5A; // the oscillator response fills all its eleven bytes
  Event e{};
  REQUIRE(Run(r, r.drv.GetOscillator(), e));
  REQUIRE(e.v[6] == 0x5A);
  r.wire.hub.k.cmd_reserved_fill = 0;
  REQUIRE(Run(r, r.drv.GetCounts(SensorId::Accelerometer), e)); // fills v[0] to v[5] only
  CHECK_EQ(e.error, Error::Ok);
  CHECK_EQ(e.v[2], 1000);
  for (std::size_t i = 6; i < 11U; ++i) {
    CHECK_EQ(e.v[i], 0);
  }
}
