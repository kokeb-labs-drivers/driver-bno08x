/**
 * @file test_regressions.cpp
 * @brief Faults found by the independent reviews, each kept as a directed test: long Poll() gaps,
 * stops queued before the session without NRST, command results, slow cold boots and restore
 * writes that fail.
 */
#include "test_support.hpp"

using namespace ts;

TEST_CASE("regressions: without NRST a stop queued right after Begin does not block the start-up") {
  for (int32_t which = 0; which < 3; ++which) {
    rig::Rig<rig::I2cMin> r;
    r.wire.hub.PowerOn(r.wire.now_us);
    r.RunMs(120);
    r.wire.hub.ClearQueue(); // the advertisement went out before the driver listened
    REQUIRE(r.drv.Begin(Cfg()).ok());
    const auto stop = [&r, which]() {
      if (which == 0) {
        return r.drv.SetFeature(SensorId::Accelerometer, {});
      }
      return (which == 1) ? r.drv.HubSleep() : r.drv.Reset();
    };
    const Result<OpId> s = stop();
    REQUIRE(s.ok());
    REQUIRE(r.RunUntil([&r] { return r.drv.Ready(); }, 1'000'000));
    Event e{};
    REQUIRE(r.Await(s.value, e, 500));
    CHECK_EQ(e.error, Error::Ok); // the stop went out once the session was up
  }
}

TEST_CASE("regressions: an operation ends after its limit even across a 2^31 us gap in Poll()") {
  rig::Rig<rig::I2cFull> r;
  REQUIRE(r.Start(Cfg()));
  r.RunMs(20);            // the product ID request of the start-up is answered
  r.wire.int_dead = true; // from now on no response is read
  const uint32_t writes = r.wire.hub.c.writes;
  const Result<OpId> g = r.drv.GetFeature(SensorId::Gravity);
  REQUIRE(g.ok());
  r.Run(400);
  REQUIRE(r.wire.hub.c.writes == (writes + 1U)); // written, waiting for its answer
  r.wire.now_us += 0x8000'0000U + 5'000'000U;    // the application slept for 36 minutes
  const uint32_t resumed = r.wire.now_us;
  Event e{};
  REQUIRE(r.Await(g.value, e, 5));
  CHECK_EQ(e.error, Error::Timeout);
  CHECK((e.t_us - resumed) <= r.step_us); // at the first Poll() after the gap
}

TEST_CASE("regressions: a command queued behind another does not wipe its results") {
  for (uint32_t extra = 1U; extra <= 4U; ++extra) {
    rig::Rig<rig::I2cFull> r;
    Config c = Cfg();
    c.poll_without_int = true;
    c.poll_interval_ms = 20;
    REQUIRE(r.Start(c));
    r.wire.int_dead = true; // the responses arrive one read at a time
    r.RunMs(100);
    const Result<OpId> g = r.drv.GetCounts(SensorId::Accelerometer);
    REQUIRE(g.ok());
    r.Run(200);
    const uint32_t base = r.wire.reads;
    while (r.wire.reads < (base + (2U * extra))) {
      r.Run(200);
    }
    REQUIRE(r.drv.GetOscillator().ok()); // queued between the counter responses
    Event e{};
    REQUIRE(r.Await(g.value, e, 3000));
    CHECK_EQ(e.error, Error::Ok);
    CHECK_EQ(e.v[2], 1000);
    CHECK_EQ(e.v[3], 900);
    CHECK_EQ(e.v[4], 800);
    CHECK_EQ(e.v[5], 700);
  }
}

TEST_CASE("regressions: UART without NRST, a slow cold boot is not a restart") {
  for (const bool init : {true, false}) {
    rig::Rig<rig::UartMin> r;
    r.wire.hub.boot_delay_us = 300'000; // the request for the advertisement reaches it booting
    Config c = Cfg();
    c.startup_timeout_ms = 1000;
    c.require_init_response = init;
    r.wire.hub.PowerOn(r.wire.now_us);
    REQUIRE(r.drv.Begin(c).ok());
    REQUIRE(r.RunUntil([&r] { return r.drv.Ready(); }, 2'000'000));
    r.RunMs(500);
    CHECK_EQ(r.drv.GetStats().hub_resets, 0U);
    CHECK_EQ(r.Count(EventKind::Reset), 0U);
  }
}

TEST_CASE("regressions: a restore write that fails is tried again") {
  rig::Rig<rig::I2cFull> r;
  REQUIRE(r.Start(Cfg()));
  Event e{};
  REQUIRE(Run(r, r.drv.SetFeature(SensorId::Accelerometer, Feat(10000)), e));
  r.RunMs(50);
  r.wire.hub.PowerOn(r.wire.now_us); // the hub restarts by itself
  REQUIRE(r.RunUntil([&r] { return r.drv.GetStats().hub_resets == 1U; }, 500'000));
  REQUIRE(r.RunUntil([&r] { return r.drv.Ready(); }, 500'000));
  r.wire.fail_writes = 1U; // the restore write fails once
  r.RunMs(500);
  CHECK_EQ(r.wire.hub.feat[1].interval_us, 10000U);
  CHECK_EQ(r.drv.GetStats().restore_failed, 0U);
}

TEST_CASE("regressions: a restore write that keeps failing is given up and counted") {
  rig::Rig<rig::I2cFull> r;
  REQUIRE(r.Start(Cfg()));
  Event e{};
  REQUIRE(Run(r, r.drv.SetFeature(SensorId::Accelerometer, Feat(10000)), e));
  r.wire.hub.PowerOn(r.wire.now_us);
  REQUIRE(r.RunUntil([&r] { return r.drv.GetStats().hub_resets == 1U; }, 500'000));
  REQUIRE(r.RunUntil([&r] { return r.drv.Ready(); }, 500'000));
  r.wire.fail_writes = 1000U;
  r.RunMs(500);
  r.wire.fail_writes = 0U;
  r.RunMs(200);
  CHECK_EQ(r.drv.GetStats().restore_failed, 1U);
  CHECK_EQ(r.wire.hub.feat[1].interval_us, 0U); // bounded: it is not sent forever
}

TEST_CASE("regressions: a hub restart forgets samples, report numbers and metadata") {
  rig::Rig<rig::I2cFull> r;
  REQUIRE(r.Start(Cfg()));
  r.RunMs(20);
  Event e{};
  REQUIRE(Run(r, r.drv.ReadMetadata(SensorId::RotationVector), e));
  REQUIRE(r.drv.GetMetadata(SensorId::RotationVector) != nullptr);
  r.wire.hub.Inject(3, Cat(Base(0), Report(0x01, 40, 0, 0, {1, 0, 2, 0, 3, 0})));
  r.RunMs(10);
  REQUIRE(r.drv.Latest(SensorId::Accelerometer).ok());
  r.wire.hub.PowerOn(r.wire.now_us); // the hub restarts by itself
  REQUIRE(
      r.RunUntil([&r] { return (r.drv.GetStats().hub_resets == 1U) && r.drv.Ready(); }, 1'000'000));
  CHECK(!r.drv.Latest(SensorId::Accelerometer).ok());
  CHECK(r.drv.GetMetadata(SensorId::RotationVector) == nullptr);
  const uint32_t gaps = r.drv.GetStats().report_seq_gaps;
  r.wire.hub.Inject(3, Cat(Base(0), Report(0x01, 0, 0, 0, {1, 0, 2, 0, 3, 0}))); // numbers restart
  r.RunMs(10);
  CHECK_EQ(r.drv.GetStats().report_seq_gaps, gaps);
  REQUIRE(Run(r, r.drv.ReadMetadata(SensorId::RotationVector), e));
  REQUIRE(r.drv.Begin(Cfg()).ok()); // a new session forgets the metadata too
  CHECK(r.drv.GetMetadata(SensorId::RotationVector) == nullptr);
}
