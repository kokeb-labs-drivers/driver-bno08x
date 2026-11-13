/**
 * @file test_session.cpp
 * @brief Start-up, advertisement, product ID, hub restarts and recovery on the I2C transport.
 */
#include "test_support.hpp"

using namespace ts;

TEST_CASE("session: NRST start reaches Ready and reads the product ID") {
  rig::Rig<rig::I2cFull> r;
  REQUIRE(r.Start(Cfg()));
  CHECK(r.drv.Advertised().valid);
  CHECK_EQ(r.drv.GetState(), State::Ready);
  r.RunMs(50);
  CHECK(r.drv.Product().valid);
  CHECK_EQ(r.drv.Product().part_number, 10004095U);
  CHECK_EQ(r.drv.Product().reset_cause, 4U);
  CHECK(!r.wire.log.empty());
  CHECK_EQ(r.wire.bootn_calls, 1U);
  CHECK_EQ(r.wire.hub.c.bus_while_reset, 0U);
  CHECK_EQ(r.wire.hub.c.empty_reads, 0U);
}

TEST_CASE("session: calls before Begin return NotInitialized") {
  rig::Rig<rig::I2cFull> r;
  CHECK_EQ(r.drv.SetFeature(SensorId::Accelerometer, Feat(10000)).error, Error::NotInitialized);
  CHECK_EQ(r.drv.GetFeature(SensorId::Accelerometer).error, Error::NotInitialized);
  CHECK_EQ(r.drv.Reset().error, Error::NotInitialized);
  CHECK_EQ(r.drv.HardReset().error, Error::NotInitialized);
  CHECK_EQ(r.drv.Tare(AxisMask::All, TareBasis::RotationVector).error, Error::NotInitialized);
  CHECK_EQ(r.drv.GetState(), State::Idle);
  r.drv.Poll(1000); // harmless
  CHECK_EQ(r.drv.GetState(), State::Idle);
}

TEST_CASE("session: Begin rejects an invalid config and a failed bus init") {
  rig::Rig<rig::I2cFull> r;
  Config bad = Cfg();
  bad.i2c_address = 0x10;
  CHECK_EQ(r.drv.Begin(bad).error, Error::InvalidArgument);
  bad = Cfg();
  bad.product_id_entries = 9;
  CHECK_EQ(r.drv.Begin(bad).error, Error::InvalidArgument);
  bad = Cfg();
  bad.fixed_read_bytes = 2;
  CHECK_EQ(r.drv.Begin(bad).error, Error::InvalidArgument);
  r.wire.ensure_ok = false;
  CHECK_EQ(r.drv.Begin(Cfg()).error, Error::Bus);
}

TEST_CASE("session: a port without NRST joins a running hub") {
  rig::Rig<rig::I2cMin> r;
  r.wire.hub.PowerOn(0);
  r.RunMs(120);
  r.wire.hub.ClearQueue(); // an earlier host already took the start-up messages
  REQUIRE(r.drv.Begin(Cfg()).ok());
  REQUIRE(r.RunUntil([&r] { return r.drv.Ready(); }, 2'000'000));
  CHECK(r.drv.Advertised().valid);
  CHECK_EQ(r.drv.HardReset().error, Error::Unsupported);
}

TEST_CASE("session: a port without NRST sees the normal start-up messages") {
  rig::Rig<rig::I2cMin> r;
  REQUIRE(r.Start(Cfg()));
  CHECK(r.drv.Ready());
}

TEST_CASE("session: initialise before reset-complete (A11)") {
  rig::Rig<rig::I2cFull> r;
  r.wire.hub.k.startup_order = 1;
  REQUIRE(r.Start(Cfg()));
}

TEST_CASE("session: a missing initialise response times out, or is tolerated by the knob (A11)") {
  {
    rig::Rig<rig::I2cFull> r;
    r.wire.hub.k.omit_init_response = true;
    CHECK(!r.Start(Cfg(), 1500));
    CHECK_EQ(r.drv.GetState(), State::Faulted);
  }
  {
    rig::Rig<rig::I2cFull> r;
    r.wire.hub.k.omit_init_response = true;
    Config c = Cfg();
    c.require_init_response = false;
    CHECK(r.Start(c));
  }
}

TEST_CASE("session: a dead hub faults after the start-up limit") {
  rig::Rig<rig::I2cFull> r;
  r.wire.hub.boot_silent = true;
  CHECK(!r.Start(Cfg(), 1500));
  CHECK_EQ(r.drv.GetState(), State::Faulted);
  r.RunMs(10);
  CHECK_EQ(r.drv.GetState(), State::Faulted);
  CHECK_EQ(r.drv.SetFeature(SensorId::Accelerometer, Feat(10000)).error, Error::NotInitialized);
}

TEST_CASE("session: the advertisement sets the write limit (A12)") {
  rig::Rig<rig::I2cFull> r;
  r.wire.hub.k.advert_max_cargo_write = 32;
  REQUIRE(r.Start(Cfg()));
  CHECK_EQ(r.drv.Advertised().max_cargo_write, 32U);
  // a request of 17 bytes plus the header fits 32; a 12 word FRS write step does not exceed it
  // either
  CHECK(r.drv.SetFeature(SensorId::Accelerometer, Feat(10000)).ok());
}

TEST_CASE("session: no advertised limits fall back to the compile-time ones (A12)") {
  rig::Rig<rig::I2cFull> r;
  r.wire.hub.k.advert_without_limits = true;
  REQUIRE(r.Start(Cfg()));
  CHECK_EQ(r.drv.Advertised().max_cargo_write, 0U);
  Event e{};
  CHECK(Run(r, r.drv.SetFeature(SensorId::Accelerometer, Feat(10000)), e));
}

TEST_CASE("session: Reset through the executable channel restarts and restores features") {
  rig::Rig<rig::I2cFull> r;
  REQUIRE(r.Start(Cfg()));
  Event e{};
  REQUIRE(Run(r, r.drv.SetFeature(SensorId::Accelerometer, Feat(10000)), e));
  r.RunMs(50);
  const std::size_t sensors_before = r.Count(EventKind::Sensor);
  CHECK(sensors_before > 0U);
  REQUIRE(r.drv.Reset().ok());
  r.RunMs(300);
  CHECK(r.drv.Ready());
  CHECK_EQ(r.Count(EventKind::Reset), 1U);
  CHECK_EQ(r.drv.GetStats().hub_resets, 1U);
  r.RunMs(200);
  CHECK(r.Count(EventKind::Sensor) > sensors_before); // auto-restore re-enabled the accelerometer
  CHECK_EQ(r.wire.hub.feat[1].interval_us, 10000U);
}

TEST_CASE("session: auto_restore off leaves the hub's sensors off after a restart") {
  rig::Rig<rig::I2cFull> r;
  Config c = Cfg();
  c.auto_restore = false;
  REQUIRE(r.Start(c));
  Event e{};
  REQUIRE(Run(r, r.drv.SetFeature(SensorId::Accelerometer, Feat(10000)), e));
  REQUIRE(r.drv.Reset().ok());
  r.RunMs(400);
  CHECK(r.drv.Ready());
  CHECK_EQ(r.wire.hub.feat[1].interval_us, 0U);
}

TEST_CASE("session: HardReset pulses NRST, abandons operations and restores") {
  rig::Rig<rig::I2cFull> r;
  REQUIRE(r.Start(Cfg()));
  Event e{};
  REQUIRE(Run(r, r.drv.SetFeature(SensorId::GameRotationVector, Feat(20000)), e));
  const Result<OpId> pending = r.drv.GetFeature(SensorId::Accelerometer);
  REQUIRE(pending.ok());
  REQUIRE(r.drv.HardReset().ok());
  CHECK_EQ(Err(r, pending), Error::HubReset);
  CHECK(r.RunUntil([&r] { return r.drv.Ready(); }, 2'000'000));
  r.RunMs(100);
  CHECK_EQ(r.wire.hub.feat[8].interval_us, 20000U);
  CHECK_EQ(r.Count(EventKind::Reset), 1U);
}

TEST_CASE("session: the hub restarts on its own (watchdog): Reset event with the cause") {
  rig::Rig<rig::I2cFull> r;
  REQUIRE(r.Start(Cfg()));
  r.RunMs(50);
  r.wire.hub.PowerOn(r.wire.now_us); // the hub reboots by itself: advertisement while we are Ready
  r.RunMs(400);
  CHECK(r.drv.Ready());
  CHECK_EQ(r.drv.GetStats().hub_resets, 1U);
  CHECK_EQ(r.Count(EventKind::Reset), 1U);
}

TEST_CASE("session: a failing NRST hook is reported") {
  rig::Rig<rig::I2cFull> r;
  r.wire.reset_fails = true;
  CHECK_EQ(r.drv.Begin(Cfg()).error, Error::Bus);
}

TEST_CASE("session: SHTP error lists from the hub are events") {
  rig::Rig<rig::I2cFull> r;
  REQUIRE(r.Start(Cfg()));
  r.wire.hub.SendErrorList(5);
  r.RunMs(20);
  CHECK_EQ(r.Count(EventKind::HubError), 1U);
  CHECK_EQ(r.drv.GetStats().hub_errors, 1U);
  for (const Event& ev : r.events) {
    if (ev.kind == EventKind::HubError) {
      CHECK_EQ(ev.detail, 5U);
    }
  }
}

TEST_CASE("session: framing violations escalate to a hub reset") {
  rig::Rig<rig::I2cFull> r;
  REQUIRE(r.Start(Cfg()));
  r.RunMs(50);
  for (int32_t i = 0; i < 6; ++i) {
    r.wire.hub.InjectRaw(B({0xFF, 0xFF, 0x03, 0x00})); // length 0xFFFF: reserved as an error (V2)
    r.RunMs(2);
  }
  CHECK(r.drv.GetStats().length_ffff >= 4U);
  CHECK(r.drv.GetStats().resyncs >= 1U);
  r.RunMs(400);
  CHECK(r.drv.Ready());
}

TEST_CASE("session: a bus error while reading is counted and survived") {
  rig::Rig<rig::I2cFull> r;
  REQUIRE(r.Start(Cfg()));
  r.wire.hub.Inject(3, Base(0));
  r.wire.fail_reads = 2;
  r.RunMs(20);
  CHECK(r.drv.GetStats().bus_errors >= 1U);
  CHECK(r.drv.Ready());
}

TEST_CASE("session: Begin again restarts a session") {
  rig::Rig<rig::I2cFull> r;
  REQUIRE(r.Start(Cfg()));
  REQUIRE(r.drv.Begin(Cfg(Variant::Bno086)).ok());
  CHECK(r.RunUntil([&r] { return r.drv.Ready(); }, 2'000'000));
}

TEST_CASE("session: with NRST a slow boot is waited for, never asked for its advertisement") {
  rig::Rig<rig::I2cFull> r;
  r.wire.hub.boot_delay_us = 300'000; // longer than advert_retry_ms
  Config c = Cfg();
  c.startup_timeout_ms = 1000;
  REQUIRE(r.Start(c, 1500));
  CHECK_EQ(r.wire.hub.c.bus_while_reset, 0U); // nothing was written to the booting hub
  CHECK_EQ(r.drv.GetStats().hub_resets, 0U);
}

TEST_CASE("session: without NRST a cold boot slower than advert_retry_ms is not a restart") {
  for (const bool init : {true, false}) {
    rig::Rig<rig::I2cMin> r;
    r.wire.hub.boot_delay_us = 300'000; // the request for the advertisement fails while it boots
    Config c = Cfg();
    c.startup_timeout_ms = 1000;
    c.require_init_response = init;
    r.wire.hub.PowerOn(r.wire.now_us);
    REQUIRE(r.drv.Begin(c).ok());
    REQUIRE(r.RunUntil([&r] { return r.drv.Ready(); }, 2'000'000));
    r.RunMs(500);
    CHECK_EQ(r.drv.GetStats().hub_resets, 0U);
    CHECK_EQ(r.Count(EventKind::Reset), 0U);
    CHECK(r.drv.Ready());
  }
}

TEST_CASE("session: without NRST the advertisement is asked for once") {
  rig::Rig<rig::I2cMin> r;
  r.wire.hub.PowerOn(r.wire.now_us);
  r.RunMs(120); // the hub advertised before the driver listened
  r.wire.hub.ClearQueue();
  REQUIRE(r.drv.Begin(Cfg()).ok());
  REQUIRE(r.RunUntil([&r] { return r.drv.Ready(); }, 1'000'000));
  r.RunMs(300);
  CHECK_EQ(r.wire.hub.c.writes, 2U); // one request for the advertisement, then the product ID
  CHECK_EQ(r.drv.GetStats().hub_resets, 0U); // a second answer would look like a restart
}

TEST_CASE("session: SPI without NRST asks for the advertisement once, through WAKE") {
  rig::Rig<rig::SpiMin> r;
  r.wire.hub.PowerOn(r.wire.now_us);
  r.RunMs(120); // the hub advertised before the driver listened
  r.wire.hub.ClearQueue();
  REQUIRE(r.drv.Begin(Cfg()).ok());
  REQUIRE(r.RunUntil([&r] { return r.drv.Ready(); }, 1'000'000));
  r.RunMs(300);
  CHECK_EQ(r.drv.GetStats().hub_resets, 0U); // a second answer would look like a restart
  CHECK(r.drv.Ready());
}
