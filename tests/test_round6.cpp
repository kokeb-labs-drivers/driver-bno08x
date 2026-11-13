/**
 * @file test_round6.cpp
 * @brief The start-up window across a clock wrap, a stale notification in Faulted, each half of
 * the restart-signal exception, and the edges of the restart and resync bookkeeping.
 */
#include "test_support.hpp"

using namespace ts;

namespace {

const Bytes kLengthFfff{0xFF, 0xFF, 0x00, 0x00};

/// One transfer with an explicit header: `cargo` on `channel` numbered `seq`.
Bytes Transfer(uint8_t channel, uint8_t seq, const Bytes& cargo) {
  const auto len = static_cast<uint16_t>(cargo.size() + 4U);
  Bytes t{static_cast<uint8_t>(len & 0xFFU), static_cast<uint8_t>(len >> 8U), channel, seq};
  t.insert(t.end(), cargo.begin(), cargo.end());
  return t;
}

/// A hub that was running before the driver joined: the advertisement was asked for and answered.
void JoinRunningHub(rig::Rig<rig::I2cMin>& r, const Config& c) {
  r.wire.hub.PowerOn(r.wire.now_us);
  r.RunMs(120);
  r.wire.hub.ClearQueue();
  REQUIRE(r.drv.Begin(c).ok());
  REQUIRE(r.RunUntil([&r] { return r.drv.Ready(); }, 1'000'000));
}

} // namespace

TEST_CASE("round6: the start-up window does not reopen when the clock wraps") {
  rig::Rig<rig::I2cMin> r;
  const Config c = Cfg();
  JoinRunningHub(r, c);
  r.RunMs(static_cast<uint32_t>(c.startup_timeout_ms) + 100U); // the window closes while polled
  r.wire.now_us -= static_cast<uint32_t>(c.startup_timeout_ms) * 1000U; // 2^32 us later
  r.wire.hub.Inject(1, B({0x01})); // a restart whose advertisement was lost
  r.RunMs(20);
  CHECK_EQ(r.drv.GetStats().hub_resets, 1U);
}

TEST_CASE("round6: a notification from before a fault is not used after a clock wrap") {
  rig::Rig<rig::UartFull> r;
  r.step_us = 100;
  r.wire.hub.k.omit_init_response = true; // the start-up never finishes
  Config c = Cfg();
  c.startup_timeout_ms = 100;
  r.wire.hub.PowerOn(r.wire.now_us);
  REQUIRE(r.drv.Begin(c).ok());
  REQUIRE(r.RunUntil([&r] { return r.drv.GetState() == State::WaitReady; }, 1'000'000));
  r.wire.hub.RvcPush(B({0x7E, 0x00, 0x00, 0x01, 0x7E})); // a notification nobody asked for
  REQUIRE(r.RunUntil([&r] { return r.drv.GetState() == State::Faulted; }, 1'000'000));
  r.RunMs(300);              // it expires (200 ms) while Faulted is polled
  r.wire.now_us -= 250'000U; // the clock wraps: its 32-bit age looks fresh again
  const uint32_t queries = r.wire.hub.c.bsq_in;
  REQUIRE(r.drv.Reset().ok());
  r.RunMs(2);
  CHECK(r.wire.hub.c.bsq_in > queries); // a fresh query, not the stale notification
}

TEST_CASE("round6: an advertisement repeating the last number on channel 0 is a restart") {
  rig::Rig<rig::I2cFull> r;
  Config c = Cfg();
  c.read_policy = ReadPolicy::Fixed; // the whole transfer in one read, header and cargo together
  c.fixed_read_bytes = 300;
  REQUIRE(r.Start(c));
  r.RunMs(20);
  r.wire.hub.InjectRaw(Transfer(0, 0, r.wire.hub.AdvertCargo())); // the boot's was numbered 0
  r.RunMs(10);
  CHECK_EQ(r.drv.GetStats().hub_resets, 1U);
}

TEST_CASE("round6: a reset-complete repeating the last number on channel 1 is a restart") {
  rig::Rig<rig::I2cFull> r;
  Config c = Cfg();
  c.read_policy = ReadPolicy::Fixed; // the whole transfer in one read, header and cargo together
  c.fixed_read_bytes = 300;
  REQUIRE(r.Start(c)); // the boot's reset-complete was numbered 0 on channel 1
  r.RunMs(static_cast<uint32_t>(c.startup_timeout_ms) + 100U);
  r.wire.hub.InjectRaw(Transfer(1, 0, B({0x01}))); // a restart whose advertisement was lost
  r.RunMs(10);
  CHECK_EQ(r.drv.GetStats().hub_resets, 1U);
}

TEST_CASE("round6: the initialise response of a restart is not a second restart") {
  rig::Rig<rig::I2cFull> r;
  Config c = Cfg();
  c.require_init_response = false; // Ready at the reset-complete; the initialise response follows
  REQUIRE(r.Start(c));
  r.wire.hub.PowerOn(r.wire.now_us);
  REQUIRE(r.RunUntil([&r] { return r.drv.GetStats().hub_resets >= 1U; }, 1'000'000));
  r.RunMs(300);
  CHECK_EQ(r.drv.GetStats().hub_resets, 1U);
}

TEST_CASE("round6: a restore ended by Begin() is not counted as given up") {
  rig::Rig<rig::I2cFull> r;
  Config c = Cfg();
  c.set_feature_confirm = true;
  REQUIRE(r.Start(c));
  Event e{};
  REQUIRE(Run(r, r.drv.SetFeature(SensorId::Accelerometer, Feat(10000)), e));
  REQUIRE(Run(r, r.drv.SetFeature(SensorId::GyroscopeCalibrated, Feat(10000)), e));
  r.wire.hub.PowerOn(r.wire.now_us);
  REQUIRE(
      r.RunUntil([&r] { return (r.drv.GetStats().hub_resets == 1U) && r.drv.Ready(); }, 1'000'000));
  r.wire.fail_writes = 4U;              // the accelerometer's restore and its retries fail
  r.wire.hub.set_feature_reply = false; // the gyroscope's restore waits for an answer
  r.RunMs(50);
  REQUIRE(r.drv.GetStats().restore_failed == 1U);
  REQUIRE(r.drv.Begin(c).ok()); // ends the waiting restore with NotInitialized
  CHECK_EQ(r.drv.GetStats().restore_failed, 1U);
}

TEST_CASE("round6: violations after a resync count from the first new one") {
  rig::Rig<rig::I2cMin> r; // without NRST the hub keeps answering for a while after the reset
  Config c = Cfg();
  c.resync_limit = 2;
  c.resync_window_ms = 20;
  REQUIRE(r.Start(c));
  r.RunMs(20);
  r.wire.hub.InjectRaw(kLengthFfff);
  r.RunMs(1);
  r.wire.fail_writes = 1U; // the reset command fails: the hub keeps answering
  r.wire.hub.InjectRaw(kLengthFfff);
  REQUIRE(r.RunUntil([&r] { return r.drv.GetStats().resyncs == 1U; }, 10'000));
  r.RunMs(13);
  r.wire.hub.InjectRaw(kLengthFfff); // 15 ms after the first: a new window starts here
  r.RunMs(15);
  r.wire.hub.InjectRaw(kLengthFfff); // 15 ms after that, 30 ms after the first
  r.RunMs(2);
  REQUIRE(r.drv.GetStats().length_ffff == 4U);
  CHECK_EQ(r.drv.GetStats().resyncs, 2U);
}

TEST_CASE("round6: the closing flag Begin() queued survives a time-out before it goes out") {
  rig::Rig<rig::UartMin> r;
  r.step_us = 100;
  Config c = Cfg();
  c.op_timeout_ms = 50;
  c.startup_timeout_ms = 5000;
  c.advert_retry_ms = 2000; // nothing else goes out while the flag is checked
  REQUIRE(r.Start(c, 3000));
  r.RunMs(20);
  r.wire.uart_tx_budget = 6; // the query, then 3 bytes of a request frame; then nothing
  REQUIRE(r.drv.GetFeature(SensorId::Accelerometer).ok());
  r.RunMs(5);
  REQUIRE(r.drv.Begin(c).ok()); // owes a closing flag; nothing can go out yet
  REQUIRE(r.RunUntil([&r] { return r.Count(EventKind::OpDone) >= 1U; }, 10'000));
  r.RunMs(2100); // the advertisement request (at 2 s) times out too, the flag still pending
  const uint32_t closed = r.wire.hub.c.uart_frames_closed;
  r.wire.uart_tx_budget = -1;
  r.RunMs(1);
  CHECK_EQ(r.wire.hub.c.uart_frames_closed, closed + 1U);
}
