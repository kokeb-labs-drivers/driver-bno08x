/**
 * @file test_spi.cpp
 * @brief SPI transport: WAKE handshake, duplex transfers, null headers, faults.
 */
#include "test_support.hpp"

using namespace ts;

TEST_CASE("spi: start-up, sensors and operations") {
  rig::Rig<rig::SpiFull> r;
  REQUIRE(r.Start(Cfg()));
  Event e{};
  REQUIRE(Run(r, r.drv.SetFeature(SensorId::Accelerometer, Feat(10000)), e));
  r.RunMs(200);
  CHECK(r.Count(EventKind::Sensor) > 10U);
  CHECK_EQ(r.wire.hub.c.writes_without_wake, 0U);
  CHECK(r.wire.wake_calls >= 2U);
  CHECK(!r.wire.hub.WakeLow());
  CHECK_EQ(r.wire.hub.c.host_seq_errors, 0U);
}

TEST_CASE("spi: a host write answers with whatever the hub sent meanwhile") {
  rig::Rig<rig::SpiFull> r;
  REQUIRE(r.Start(Cfg()));
  r.wire.hub.Inject(3, Cat(Base(0), Report(0x01, 0, 0, 0, {1, 0, 2, 0, 3, 0})));
  Event e{};
  REQUIRE(Run(r, r.drv.GetFeature(SensorId::Gravity), e));
  r.RunMs(20);
  CHECK_EQ(r.Count(EventKind::Sensor), 1U);
}

TEST_CASE("spi: without H_INTN after WAKE the write times out and WAKE is released") {
  rig::Rig<rig::SpiFull> r;
  Config c = Cfg();
  c.wake_timeout_us = 5000;
  REQUIRE(r.Start(c));
  r.wire.int_dead = true;
  const Result<OpId> res = r.drv.GetFeature(SensorId::Gravity);
  REQUIRE(res.ok());
  Event e{};
  REQUIRE(r.Await(res.value, e, 200));
  CHECK_EQ(e.error, Error::Timeout);
  CHECK(!r.wire.hub.WakeLow());
  const uint32_t held = r.wire.wake_high_us - r.wire.wake_low_us;
  CHECK_EQ(held, c.wake_timeout_us); // released at the Poll() that reaches the limit
}

TEST_CASE("spi: a transfer error is counted") {
  rig::Rig<rig::SpiFull> r;
  REQUIRE(r.Start(Cfg()));
  r.RunMs(50);
  r.wire.hub.Inject(3, Cat(Base(0), Report(0x01, 0, 0, 0, {1, 0, 2, 0, 3, 0})));
  r.wire.fail_reads = 1;
  r.RunMs(20);
  CHECK(r.drv.GetStats().bus_errors >= 1U);
  r.wire.fail_reads = 1;
  CHECK_EQ(Err(r, r.drv.GetFeature(SensorId::Gravity)), Error::Bus);
}

TEST_CASE("spi: a port without an interrupt timestamp hook") {
  rig::Rig<rig::SpiMin> r;
  REQUIRE(r.Start(Cfg()));
  Event e{};
  REQUIRE(Run(r, r.drv.SetFeature(SensorId::Accelerometer, Feat(10000)), e));
  r.RunMs(100);
  CHECK(r.Count(EventKind::Sensor) > 3U);
}

TEST_CASE("spi: a long cargo arrives in continuation transfers") {
  rig::Rig<rig::SpiFull> r;
  REQUIRE(r.Start(Cfg()));
  Bytes cargo = Base(0);
  for (uint8_t i = 0; i < 10; ++i) {
    cargo = Cat(cargo, Report(0x01, i, 0, i, {1, 0, 2, 0, 3, 0}));
  }
  r.wire.hub.Inject(3, cargo);
  r.RunMs(30);
  CHECK_EQ(r.Count(EventKind::Sensor), 10U);
}

TEST_CASE("spi: Fixed read policy") {
  rig::Rig<rig::SpiFull> r;
  Config c = Cfg();
  c.read_policy = ReadPolicy::Fixed;
  c.fixed_read_bytes = 48;
  REQUIRE(r.Start(c));
  Event e{};
  REQUIRE(Run(r, r.drv.SetFeature(SensorId::Accelerometer, Feat(10000)), e));
  r.RunMs(100);
  CHECK(r.Count(EventKind::Sensor) > 3U);
}

TEST_CASE("spi: the hub restarts and the session recovers") {
  rig::Rig<rig::SpiFull> r;
  REQUIRE(r.Start(Cfg()));
  REQUIRE(r.drv.Reset().ok());
  r.RunMs(500);
  CHECK(r.drv.Ready());
  CHECK_EQ(r.drv.GetStats().hub_resets, 1U);
}

TEST_CASE("spi: HardReset while a write waits for H_INTN raises WAKE before NRST (DS p18)") {
  rig::Rig<rig::SpiFull> r;
  REQUIRE(r.Start(Cfg()));
  r.RunMs(20);
  r.wire.int_dead = true;
  REQUIRE(r.drv.GetFeature(SensorId::Gravity).ok());
  r.RunMs(1);
  REQUIRE(r.wire.hub.WakeLow());
  REQUIRE(r.drv.HardReset().ok());
  CHECK(!r.wire.hub.WakeLow());
  r.wire.int_dead = false;
  REQUIRE(r.RunUntil([&r] { return r.drv.Ready(); }, 1'000'000));
  CHECK_EQ(r.wire.hub.c.bad_strap, 0U);
  Event e{};
  REQUIRE(Run(r, r.drv.GetFeature(SensorId::Gravity), e));
  CHECK_EQ(e.error, Error::Ok);
  CHECK_EQ(r.wire.hub.c.host_seq_errors, 0U); // the new session numbers its writes from 0
  CHECK_EQ(r.wire.hub.c.host_seq_first_not_zero, 0U);
}

TEST_CASE("spi: Begin raises a WAKE line left low before NRST (DS p18)") {
  rig::Rig<rig::SpiFull> r;
  r.wire.hub.SetWake(false); // the pin's level before the driver owns it
  r.wire.hub.PowerOn(r.wire.now_us);
  REQUIRE(r.drv.Begin(Cfg()).ok());
  CHECK(!r.wire.hub.WakeLow());
  REQUIRE(r.RunUntil([&r] { return r.drv.Ready(); }, 1'000'000));
  CHECK_EQ(r.wire.hub.c.bad_strap, 0U);
}

TEST_CASE("spi: an operation that times out while WAKE is low releases WAKE") {
  rig::Rig<rig::SpiFull> r;
  Config c = Cfg();
  c.wake_timeout_us = 9500;
  c.op_timeout_ms = 10;
  REQUIRE(r.Start(c));
  r.RunMs(20);
  r.step_us = 5000; // the operation limit and the WAKE limit fall in the same Poll()
  r.wire.int_dead = true;
  const Result<OpId> res = r.drv.GetFeature(SensorId::Gravity);
  REQUIRE(res.ok());
  Event e{};
  REQUIRE(r.Await(res.value, e, 100));
  CHECK_EQ(e.error, Error::Timeout);
  CHECK(!r.wire.hub.WakeLow());
  r.step_us = 200;
  r.wire.int_dead = false;
  REQUIRE(Run(r, r.drv.GetFeature(SensorId::Gravity), e));
  CHECK_EQ(e.error, Error::Ok);
  CHECK_EQ(r.wire.hub.c.bad_strap, 0U);
}

TEST_CASE("spi: a start-up time-out while WAKE is low releases WAKE") {
  rig::Rig<rig::SpiMin> r;
  Config c = Cfg();
  c.startup_timeout_ms = 300;
  c.op_timeout_ms = 1000;
  c.wake_timeout_us = 900'000;
  r.wire.hub.PowerOn(r.wire.now_us);
  r.RunMs(120); // the advertisement went out before the driver listened: it asks for it
  r.wire.int_dead = true;
  REQUIRE(r.drv.Begin(c).ok());
  REQUIRE(r.RunUntil([&r] { return r.drv.GetState() == State::Faulted; }, 1'000'000));
  CHECK(!r.wire.hub.WakeLow());
}
