/**
 * @file test_round5.cpp
 * @brief The duplex read of an SPI write that restarts the session, sequence numbers that start
 * again after a hub restart, the start-up window, 32-bit time windows and the UART closing flag.
 */
#include "test_support.hpp"

using namespace ts;

namespace {

const Bytes kLengthFfff{0xFF, 0xFF, 0x00, 0x00};

/// Queues a Get Feature, lets its write wait for H_INTN, then makes the hub hold `cargo` so the
/// write's duplex transfer reads it.
template <typename R, typename F>
OpId DuplexCarries(R& r, F inject) {
  const Result<OpId> g = r.drv.GetFeature(SensorId::Gravity);
  CHECK(g.ok());
  r.Run(r.step_us); // WAKE is low: the write waits for H_INTN
  inject();
  return g.value;
}

std::size_t DoneCount(const std::vector<Event>& events, OpId id) {
  std::size_t n = 0;
  for (const Event& e : events) {
    n += ((e.kind == EventKind::OpDone) && (e.op == id)) ? 1U : 0U;
  }
  return n;
}

} // namespace

TEST_CASE("round5: SPI without NRST, a resync read by a write still sends its reset") {
  rig::Rig<rig::SpiMin> r;
  Config c = Cfg();
  c.resync_limit = 1;
  REQUIRE(r.Start(c));
  r.RunMs(20);
  const uint32_t boots = r.wire.hub.c.reboots;
  const OpId id = DuplexCarries(r, [&r] { r.wire.hub.InjectRaw(kLengthFfff); });
  REQUIRE(r.RunUntil([&r] { return r.drv.GetStats().resyncs == 1U; }, 100'000));
  r.RunMs(300);
  CHECK_EQ(r.wire.hub.c.reboots, boots + 1U); // the recovery reset was written, not just marked
  CHECK_EQ(DoneCount(r.events, id), 1U);
  REQUIRE(r.RunUntil([&r] { return r.drv.Ready(); }, 1'000'000));
}

TEST_CASE("round5: SPI with NRST, a resync read by a write ends that write once") {
  rig::Rig<rig::SpiFull> r;
  Config c = Cfg();
  c.resync_limit = 1;
  REQUIRE(r.Start(c));
  r.RunMs(20);
  const OpId id = DuplexCarries(r, [&r] { r.wire.hub.InjectRaw(kLengthFfff); });
  REQUIRE(r.RunUntil([&r] { return r.drv.GetStats().resyncs == 1U; }, 100'000));
  REQUIRE(r.RunUntil([&r] { return r.drv.Ready(); }, 1'000'000));
  r.RunMs(100);
  CHECK_EQ(DoneCount(r.events, id), 1U);
  CHECK(r.drv.Product().valid); // the new session's product ID request went out and was answered
}

TEST_CASE("round5: SPI, an advertisement read by a write restarts the session cleanly") {
  rig::Rig<rig::SpiFull> r;
  REQUIRE(r.Start(Cfg()));
  r.RunMs(20);
  const OpId id = DuplexCarries(r, [&r] {
    r.wire.hub.Inject(0, r.wire.hub.AdvertCargo()); // a restart: advertisement, reset-complete,
    r.wire.hub.Inject(1, B({0x01}));                // initialise
    r.wire.hub.Inject(2, Bytes{0xF1, 0, 0x84, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0});
  });
  REQUIRE(r.RunUntil([&r] { return r.drv.GetStats().hub_resets == 1U; }, 100'000));
  REQUIRE(r.RunUntil([&r] { return r.Count(EventKind::Reset) == 1U; }, 1'000'000));
  CHECK_EQ(DoneCount(r.events, id), 1U);
  for (const Event& e : r.events) {
    if (e.kind == EventKind::Reset) {
      CHECK(e.detail != 0xFFU); // the product ID request of the new session was answered
    }
  }
}

TEST_CASE("round5: a hub restart is seen although its numbers repeat the last ones") {
  // the first boot's advertisement and reset-complete were numbered 0: so are the restart's
  {
    rig::Rig<rig::UartFull> r;
    r.step_us = 100;
    r.wire.hub.k.omit_init_response = true;
    Config c = Cfg();
    c.require_init_response = false;
    REQUIRE(r.Start(c));
    Event e{};
    REQUIRE(Run(r, r.drv.SetFeature(SensorId::Accelerometer, Feat(10000)), e, 500));
    r.wire.hub.PowerOn(r.wire.now_us); // the hub restarts by itself
    REQUIRE(r.RunUntil([&r] { return r.drv.GetStats().hub_resets == 1U; }, 1'000'000));
    r.RunMs(300);
    CHECK_EQ(r.wire.hub.feat[0x01].interval_us, 10000U); // restored
  }
  {
    rig::Rig<rig::I2cFull> r;
    r.wire.hub.k.omit_init_response = true;
    Config c = Cfg();
    c.require_init_response = false;
    REQUIRE(r.Start(c));
    r.wire.hub.PowerOn(r.wire.now_us);
    REQUIRE(r.RunUntil([&r] { return r.drv.GetStats().hub_resets == 1U; }, 1'000'000));
  }
}

TEST_CASE("round5: without NRST the advertisement request goes out behind seven waiting stops") {
  rig::Rig<rig::I2cMin> r;
  r.wire.hub.PowerOn(r.wire.now_us);
  r.RunMs(120);
  r.wire.hub.ClearQueue(); // the advertisement went out before the driver listened
  REQUIRE(r.drv.Begin(Cfg()).ok());
  for (uint8_t id = 0x01U; id <= 0x07U; ++id) {
    REQUIRE(r.drv.SetFeature(static_cast<SensorId>(id), FeatureConfig{}).ok());
  }
  REQUIRE(r.RunUntil([&r] { return r.drv.Ready(); }, 1'000'000));
}

TEST_CASE("round5: a reset-complete long after an answered advertisement is a restart") {
  rig::Rig<rig::I2cMin> r;
  r.wire.hub.PowerOn(r.wire.now_us);
  r.RunMs(120);
  r.wire.hub.ClearQueue();
  const Config c = Cfg();
  REQUIRE(r.drv.Begin(c).ok());
  REQUIRE(r.RunUntil([&r] { return r.drv.Ready(); }, 1'000'000));
  r.RunMs(static_cast<uint32_t>(c.startup_timeout_ms) + 100U); // past the start-up window
  r.wire.hub.Inject(1, B({0x01})); // a restart whose advertisement was lost
  r.RunMs(20);
  CHECK_EQ(r.drv.GetStats().hub_resets, 1U);
}

TEST_CASE("round5: a restart seen through its reset-complete makes the next one a restart too") {
  rig::Rig<rig::I2cMin> r;
  r.wire.hub.PowerOn(r.wire.now_us);
  r.RunMs(120);
  r.wire.hub.ClearQueue();
  const Config c = Cfg();
  REQUIRE(r.drv.Begin(c).ok());
  REQUIRE(r.RunUntil([&r] { return r.drv.Ready(); }, 1'000'000));
  r.RunMs(static_cast<uint32_t>(c.startup_timeout_ms) + 100U);
  r.wire.hub.Inject(1, B({0x01}));
  r.wire.hub.Inject(2, Bytes{0xF1, 0, 0x84, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0}); // initialise
  REQUIRE(
      r.RunUntil([&r] { return (r.drv.GetStats().hub_resets == 1U) && r.drv.Ready(); }, 1'000'000));
  r.wire.hub.Inject(1, B({0x01})); // soon after: inside the window, but its first was heard
  r.RunMs(20);
  CHECK_EQ(r.drv.GetStats().hub_resets, 2U);
}

TEST_CASE("round5: a notification left over an idle while the clock wraps is not used") {
  rig::Rig<rig::UartFull> r;
  r.step_us = 100;
  REQUIRE(r.Start(Cfg()));
  r.RunMs(20);
  r.wire.hub.RvcPush(B({0x7E, 0x00, 0x00, 0x01, 0x7E})); // a notification nobody asked for
  r.RunMs(300);                                          // it expires (200 ms) while polled
  r.wire.now_us -= 250'000U; // the clock wraps (2^32 - 250 ms later): its 32-bit age is 50 ms
  const uint32_t queries = r.wire.hub.c.bsq_in;
  REQUIRE(r.drv.GetFeature(SensorId::Accelerometer).ok());
  r.RunMs(2);
  CHECK(r.wire.hub.c.bsq_in > queries);
}

TEST_CASE("round5: a violation from before a clock wrap does not count with a new one") {
  rig::Rig<rig::I2cFull> r;
  Config c = Cfg();
  c.resync_limit = 2;
  c.resync_window_ms = 20;
  REQUIRE(r.Start(c));
  r.RunMs(20);
  r.wire.hub.InjectRaw(kLengthFfff);
  r.RunMs(50);                             // the window passes while polled
  r.wire.now_us += 0xFFFF'FFFFU - 45'000U; // the 32-bit age of the first looks recent again
  r.wire.hub.InjectRaw(kLengthFfff);
  r.RunMs(5);
  CHECK_EQ(r.drv.GetStats().length_ffff, 2U);
  CHECK_EQ(r.drv.GetStats().resyncs, 0U);
}

TEST_CASE("round5: a closing flag that could not go out survives a time-out and Begin()") {
  rig::Rig<rig::UartMin> r;
  r.step_us = 100;
  Config c = Cfg();
  c.op_timeout_ms = 20;
  REQUIRE(r.Start(c));
  r.RunMs(20);
  r.wire.uart_tx_budget = 6; // the query, then 3 bytes of the request frame; then nothing
  Event e{};
  REQUIRE(Run(r, r.drv.GetFeature(SensorId::Accelerometer), e, 200));
  CHECK_EQ(e.error, Error::Timeout); // the closing flag is pending
  REQUIRE(r.drv.Begin(c).ok());      // and is still owed after a new session starts
  const uint32_t closed = r.wire.hub.c.uart_frames_closed;
  r.wire.uart_tx_budget = -1;
  r.RunMs(2);
  CHECK_EQ(r.wire.hub.c.uart_frames_closed, closed + 1U);
}

TEST_CASE("round5: a restore write is tried exactly kRestoreTries more times") {
  rig::Rig<rig::I2cFull> r;
  REQUIRE(r.Start(Cfg()));
  Event e{};
  REQUIRE(Run(r, r.drv.SetFeature(SensorId::Accelerometer, Feat(10000)), e));
  r.wire.hub.PowerOn(r.wire.now_us);
  REQUIRE(r.RunUntil([&r] { return r.drv.GetStats().hub_resets == 1U; }, 500'000));
  REQUIRE(r.RunUntil([&r] { return r.drv.Ready(); }, 500'000)); // the product ID request is out
  const uint32_t writes = r.wire.writes;
  r.wire.fail_writes = 1000U;
  r.RunMs(500);
  CHECK_EQ(r.wire.writes - writes, 4U); // the first write and three more
  CHECK_EQ(r.drv.GetStats().restore_failed, 1U);
}

TEST_CASE("round5: an initialise response long after an answered advertisement is a restart") {
  rig::Rig<rig::I2cMin> r;
  r.wire.hub.PowerOn(r.wire.now_us);
  r.RunMs(120);
  r.wire.hub.ClearQueue();
  const Config c = Cfg();
  REQUIRE(r.drv.Begin(c).ok());
  REQUIRE(r.RunUntil([&r] { return r.drv.Ready(); }, 1'000'000));
  r.RunMs(static_cast<uint32_t>(c.startup_timeout_ms) + 100U);
  r.wire.hub.Inject(2, Bytes{0xF1, 0, 0x84, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0});
  r.RunMs(20);
  CHECK_EQ(r.drv.GetStats().hub_resets, 1U);
}

TEST_CASE("round5: a closing flag still owed survives a second time-out") {
  rig::Rig<rig::UartMin> r;
  r.step_us = 100;
  Config c = Cfg();
  c.op_timeout_ms = 20;
  REQUIRE(r.Start(c));
  r.RunMs(20);
  r.wire.uart_tx_budget = 6; // the query, then 3 bytes of the request frame; then nothing
  Event e{};
  REQUIRE(Run(r, r.drv.GetFeature(SensorId::Accelerometer), e, 200));
  REQUIRE(e.error == Error::Timeout); // the closing flag is owed
  REQUIRE(Run(r, r.drv.GetFeature(SensorId::Accelerometer), e, 200));
  REQUIRE(e.error == Error::Timeout); // the next write could not even start
  const uint32_t closed = r.wire.hub.c.uart_frames_closed;
  r.wire.uart_tx_budget = -1;
  r.RunMs(2); // nothing is queued: only the flag goes out
  CHECK_EQ(r.wire.hub.c.uart_frames_closed, closed + 1U);
}

TEST_CASE("round5: a restore ended by another restart is not counted as given up") {
  rig::Rig<rig::I2cFull> r;
  Config c = Cfg();
  c.set_feature_confirm = true; // a restore waits for the hub's answer
  REQUIRE(r.Start(c));
  Event e{};
  REQUIRE(Run(r, r.drv.SetFeature(SensorId::Accelerometer, Feat(10000)), e));
  REQUIRE(Run(r, r.drv.SetFeature(SensorId::GyroscopeCalibrated, Feat(10000)), e));
  r.wire.hub.PowerOn(r.wire.now_us);
  REQUIRE(
      r.RunUntil([&r] { return (r.drv.GetStats().hub_resets == 1U) && r.drv.Ready(); }, 1'000'000));
  r.wire.fail_writes = 4U;              // the accelerometer's restore and its three retries fail
  r.wire.hub.set_feature_reply = false; // the gyroscope's restore waits for an answer
  r.RunMs(50);
  REQUIRE(r.drv.GetStats().restore_failed == 1U);
  r.wire.hub.set_feature_reply = true;
  r.wire.hub.PowerOn(r.wire.now_us); // the next restart ends the waiting restore with HubReset
  REQUIRE(r.RunUntil([&r] { return r.drv.GetStats().hub_resets == 2U; }, 1'000'000));
  r.RunMs(300);
  CHECK_EQ(r.drv.GetStats().restore_failed, 1U);
  CHECK_EQ(r.wire.hub.feat[0x02].interval_us, 10000U); // restored by the next restart
}
