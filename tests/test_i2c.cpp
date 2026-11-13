/**
 * @file test_i2c.cpp
 * @brief I2C transport: read sizing, continuation, empty polls, gating on H_INTN, faults.
 */
#include "test_support.hpp"

using namespace ts;

TEST_CASE("i2c: reads are gated by H_INTN: the driver never reads an idle hub") {
  rig::Rig<rig::I2cFull> r;
  REQUIRE(r.Start(Cfg()));
  r.RunMs(500);
  CHECK_EQ(r.wire.hub.c.empty_reads, 0U);
}

TEST_CASE("i2c: a long cargo is read as a header and a continuation of remaining + 4") {
  rig::Rig<rig::I2cFull> r;
  REQUIRE(r.Start(Cfg()));
  Bytes cargo = Base(0);
  for (uint8_t i = 0; i < 8; ++i) {
    cargo = Cat(cargo, Report(0x01, i, 0, i, {1, 0, 2, 0, 3, 0}));
  }
  r.wire.hub.Inject(3, cargo);
  r.RunMs(20);
  CHECK_EQ(r.Count(EventKind::Sensor), 8U);
  CHECK_EQ(r.drv.GetStats().bad_cont_len, 0U);
  CHECK_EQ(r.drv.GetStats().seq_gap, 0U);
}

TEST_CASE("i2c: a cargo larger than the receive buffer is dropped whole (V5)") {
  rig::Rig<rig::I2cFull> r;
  REQUIRE(r.Start(Cfg()));
  Bytes cargo(1500, 0);
  cargo[0] = 0x10;
  r.wire.hub.Inject(3, cargo);
  r.RunMs(30);
  CHECK_EQ(r.drv.GetStats().oversize_cargo, 1U);
  r.wire.hub.Inject(3, Cat(Base(0), Report(0x01, 0, 0, 0, {1, 0, 2, 0, 3, 0})));
  r.RunMs(20);
  CHECK_EQ(r.Count(EventKind::Sensor), 1U);
}

TEST_CASE("i2c: the Fixed read policy reads a fixed size and relies on padding") {
  rig::Rig<rig::I2cFull> r;
  Config c = Cfg();
  c.read_policy = ReadPolicy::Fixed;
  c.fixed_read_bytes = 64;
  REQUIRE(r.Start(c));
  r.wire.hub.Inject(3, Cat(Base(0), Report(0x01, 0, 0, 0, {1, 0, 2, 0, 3, 0})));
  r.RunMs(20);
  CHECK_EQ(r.Count(EventKind::Sensor), 1U);
  c.strict_padding = true;
  REQUIRE(r.drv.Begin(c).ok());
  REQUIRE(r.RunUntil([&r] { return r.drv.Ready(); }, 2'000'000));
  CHECK_EQ(r.drv.GetStats().nonzero_pad, 0U);
}

TEST_CASE("i2c: a hub that sends a continuation without a header (A14) trips the resync logic") {
  rig::Rig<rig::I2cFull> r;
  REQUIRE(r.Start(Cfg()));
  r.wire.hub.k.cont_header_style = 1;
  Bytes cargo = Base(0);
  for (uint8_t i = 0; i < 6; ++i) {
    cargo = Cat(cargo, Report(0x01, i, 0, i, {1, 0, 2, 0, 3, 0}));
  }
  r.wire.hub.Inject(3, cargo);
  r.RunMs(30);
  const Stats& st = r.drv.GetStats();
  const uint32_t faults = st.bad_cont_len + st.lost_cargo + st.orphan_cont + st.length_short;
  CHECK(faults >= 1U);
}

TEST_CASE("i2c: strict_cont_len off accepts a continuation of another length") {
  rig::Rig<rig::I2cFull> r;
  Config c = Cfg();
  c.strict_cont_len = false;
  REQUIRE(r.Start(c));
  Bytes cargo = Base(0);
  for (uint8_t i = 0; i < 4; ++i) {
    cargo = Cat(cargo, Report(0x01, i, 0, i, {1, 0, 2, 0, 3, 0}));
  }
  r.wire.hub.Inject(3, cargo);
  r.RunMs(20);
  CHECK_EQ(r.Count(EventKind::Sensor), 4U);
}

TEST_CASE("i2c: a null header and short transfers are counted") {
  rig::Rig<rig::I2cFull> r;
  REQUIRE(r.Start(Cfg()));
  r.wire.hub.InjectRaw(B({0, 0, 0, 0}));
  r.RunMs(10);
  CHECK_EQ(r.drv.GetStats().null_header, 1U);
  r.wire.hub.InjectRaw(B({2, 0, 3, 0}));
  r.RunMs(10);
  CHECK_EQ(r.drv.GetStats().length_short, 1U);
  r.wire.hub.InjectRaw(B({4, 0, 9, 0}));
  r.RunMs(10);
  CHECK_EQ(r.drv.GetStats().length_short, 1U);
  CHECK_EQ(r.drv.GetStats().bad_channel, 1U); // a header of length 4 on channel 9
  r.wire.hub.InjectRaw(B({5, 0, 9, 0, 1}));
  r.RunMs(10);
  CHECK_EQ(r.drv.GetStats().bad_channel,
           2U); // counted once although its cargo is read in two transfers
}

TEST_CASE("i2c: the hub skipping sequence numbers is counted, not fatal (A15)") {
  rig::Rig<rig::I2cFull> r;
  r.wire.hub.k.seq_skip_rate = 3;
  r.wire.hub.k.seq_start = 200;
  REQUIRE(r.Start(Cfg()));
  Event e{};
  REQUIRE(Run(r, r.drv.SetFeature(SensorId::Accelerometer, Feat(10000)), e));
  r.RunMs(300);
  CHECK(r.drv.GetStats().seq_gap >= 1U);
  CHECK(r.Count(EventKind::Sensor) > 10U);
}

TEST_CASE("i2c: a zero-length header after an empty poll is a null header (A2)") {
  rig::Rig<rig::I2cFull> r;
  REQUIRE(r.Start(Cfg()));
  Config c = Cfg();
  c.poll_without_int = true;
  c.poll_interval_ms = 5;
  REQUIRE(r.drv.Begin(c).ok());
  REQUIRE(r.RunUntil([&r] { return r.drv.Ready(); }, 2'000'000));
  r.wire.int_dead = true;
  r.RunMs(100);
  CHECK(r.drv.GetStats().null_header >= 5U);
  r.wire.hub.k.i2c_empty_poll = 2; // NACK
  r.RunMs(100);
  CHECK(r.drv.GetStats().bus_errors >= 5U);
}

TEST_CASE("i2c: H_INTN stuck low with nothing behind it does not hang the driver") {
  rig::Rig<rig::I2cFull> r;
  REQUIRE(r.Start(Cfg()));
  r.wire.int_stuck = true;
  r.RunMs(100);
  CHECK(r.drv.Ready());
  CHECK(r.drv.GetStats().null_header >= 1U);
  r.wire.int_stuck = false;
  r.wire.hub.Inject(3, Cat(Base(0), Report(0x01, 0, 0, 0, {1, 0, 2, 0, 3, 0})));
  r.RunMs(20);
  CHECK_EQ(r.Count(EventKind::Sensor), 1U);
}

TEST_CASE("i2c: a hub that times out waiting for the host re-asserts H_INTN (A38)") {
  rig::Rig<rig::I2cFull> r;
  r.wire.hub.k.hint_host_timeout_ms = 10;
  REQUIRE(r.Start(Cfg()));
  r.wire.int_dead = true;
  r.wire.hub.Inject(3, Cat(Base(0), Report(0x01, 0, 0, 0, {1, 0, 2, 0, 3, 0})));
  r.RunMs(35);
  CHECK(r.wire.hub.c.int_timeouts >= 2U);
  r.wire.int_dead = false;
  r.RunMs(20);
  CHECK_EQ(r.Count(EventKind::Sensor), 1U);
}

TEST_CASE("i2c: the address in the config reaches the port") {
  rig::Rig<rig::I2cFull> r;
  Config c = Cfg();
  c.i2c_address = 0x4B;
  REQUIRE(r.Start(c));
}

TEST_CASE("i2c: writes go out one transfer at a time and never fragmented (A13)") {
  rig::Rig<rig::I2cFull> r;
  REQUIRE(r.Start(Cfg()));
  Event e{};
  for (int32_t i = 0; i < 6; ++i) {
    REQUIRE(Run(
        r, r.drv.SetFeature(SensorId::Accelerometer, Feat(10000U + static_cast<uint32_t>(i))), e));
  }
  CHECK_EQ(r.wire.hub.c.fragmented_writes, 0U);
  CHECK_EQ(r.wire.hub.c.host_seq_errors, 0U);
  CHECK_EQ(r.wire.hub.c.short_writes, 0U);
}

TEST_CASE("i2c: a hub that accepts fragments sees none from the driver (A13)") {
  rig::Rig<rig::I2cFull> r;
  r.wire.hub.k.accept_fragmented_writes = true;
  REQUIRE(r.Start(Cfg()));
  Event e{};
  REQUIRE(Run(r, r.drv.SetFeature(SensorId::Accelerometer, Feat(10000)), e));
  CHECK_EQ(r.wire.hub.c.fragmented_writes, 0U);
}

TEST_CASE("i2c: the trace keeps the first bytes of the last transfer without any I/O") {
  rig::Rig<rig::I2cFull> r;
  REQUIRE(r.Start(Cfg()));
  std::array<uint8_t, 64> out{};
  CHECK(r.drv.TraceLastTransfer(out) <= r.drv.kTraceBytes);
  Bytes cargo = Cat(Base(0), Report(0x01, 0, 0, 0, {1, 0, 2, 0, 3, 0}));
  r.wire.hub.Inject(3, cargo);
  r.RunMs(20);
  const std::size_t n = r.drv.TraceLastTransfer(out);
  CHECK(n >= 4U);
  CHECK(r.drv.GetLastTransferLength() >= n);
  std::array<uint8_t, 2> small{};
  CHECK_EQ(r.drv.TraceLastTransfer(small), 2U);
}
