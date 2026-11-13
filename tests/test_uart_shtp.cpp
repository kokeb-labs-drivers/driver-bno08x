/**
 * @file test_uart_shtp.cpp
 * @brief UART-SHTP: flags, escapes, protocol IDs, the BSN token and the 100 us byte gap.
 */
#include "test_support.hpp"

using namespace ts;

TEST_CASE("uart: start-up, sensors, operations with the paced byte hook") {
  rig::Rig<rig::UartFull> r;
  r.step_us = 100;
  REQUIRE(r.Start(Cfg()));
  Event e{};
  REQUIRE(Run(r, r.drv.SetFeature(SensorId::Accelerometer, Feat(10000)), e));
  r.RunMs(200);
  CHECK(r.Count(EventKind::Sensor) > 10U);
  CHECK_EQ(r.wire.hub.c.uart_gap_violations, 0U);
  CHECK_EQ(r.wire.hub.c.uart_no_token_writes, 0U);
  CHECK_EQ(r.wire.hub.c.host_seq_errors, 0U);
  CHECK_EQ(r.drv.GetStats().bad_protocol_id, 0U);
}

TEST_CASE("uart: a port without an interrupt line still receives") {
  rig::Rig<rig::UartMin> r;
  r.step_us = 100;
  REQUIRE(r.Start(Cfg()));
  Event e{};
  REQUIRE(Run(r, r.drv.SetFeature(SensorId::Accelerometer, Feat(10000)), e));
  r.RunMs(100);
  CHECK(r.Count(EventKind::Sensor) > 3U);
}

TEST_CASE("uart: the port paces a block write itself") {
  rig::Rig<rig::UartBlockPort> r;
  r.step_us = 100;
  REQUIRE(r.Start(Cfg()));
  Event e{};
  REQUIRE(Run(r, r.drv.SetFeature(SensorId::Accelerometer, Feat(10000)), e));
  CHECK_EQ(r.wire.hub.c.uart_gap_violations, 0U);
  r.RunMs(100);
  CHECK(r.Count(EventKind::Sensor) > 3U);
}

TEST_CASE("uart: payload bytes equal to the flag and escape bytes are stuffed") {
  rig::Rig<rig::UartFull> r;
  r.step_us = 100;
  REQUIRE(r.Start(Cfg()));
  FeatureConfig f = Feat(10000);
  f.sensor_config = 0x7D7E7D7EU;
  Event e{};
  REQUIRE(Run(r, r.drv.SetFeature(SensorId::Accelerometer, f), e));
  CHECK_EQ(r.wire.hub.feat[1].config, 0x7D7E7D7EU);
  r.wire.hub.Inject(3, Cat(Base(0x7E7D), Report(0x01, 0, 0, 0x7E, {0x7E, 0, 0x7D, 0, 0x7E, 0x7D})));
  r.RunMs(20);
  bool found = false;
  for (const Event& ev : r.events) {
    if ((ev.kind == EventKind::Sensor) && (ev.v[0] == 0x7E) && (ev.v[1] == 0x7D)) {
      found = true;
    }
  }
  CHECK(found);
}

TEST_CASE("uart: a cargo is not written without a valid token; the token expires") {
  rig::Rig<rig::UartFull> r;
  r.step_us = 100;
  r.wire.hub.uart_timeout_ms = 50;
  REQUIRE(r.Start(Cfg()));
  Event e{};
  for (int32_t i = 0; i < 4; ++i) {
    REQUIRE(Run(r, r.drv.GetFeature(SensorId::Accelerometer), e));
    r.RunMs(60);
  }
  CHECK_EQ(r.wire.hub.c.uart_no_token_writes, 0U);
}

TEST_CASE("uart: the hub ignoring writes (no token granted) makes the operation time out") {
  rig::Rig<rig::UartFull> r;
  r.step_us = 100;
  Config c = Cfg();
  c.op_timeout_ms = 100;
  REQUIRE(r.Start(c));
  r.wire.hub.uart_buffer_bytes = 2; // too small for any request
  const Result<OpId> res = r.drv.GetFeature(SensorId::Accelerometer);
  REQUIRE(res.ok());
  Event e{};
  REQUIRE(r.Await(res.value, e, 1000));
  CHECK((e.error == Error::Timeout) || (e.error == Error::Busy));
}

TEST_CASE("uart: a failing byte hook is retried, a dead one times out the operation") {
  rig::Rig<rig::UartFull> r;
  r.step_us = 100;
  Config c = Cfg();
  c.op_timeout_ms = 100;
  REQUIRE(r.Start(c));
  r.wire.uart_drop_tx = true;
  const Result<OpId> res = r.drv.GetFeature(SensorId::Accelerometer);
  REQUIRE(res.ok());
  r.RunMs(20);
  r.wire.uart_drop_tx = false;
  Event e{};
  REQUIRE(r.Await(res.value, e, 1000));
  CHECK(r.drv.Ready());
}

TEST_CASE("uart: bad protocol IDs and bad escapes are counted") {
  rig::Rig<rig::UartFull> r;
  r.step_us = 100;
  REQUIRE(r.Start(Cfg()));
  r.wire.hub.RvcPush(B({0x7E, 0x09, 1, 2, 3, 0x7E}));
  r.wire.hub.RvcPush(B({0x7E, 0x01, 0x7D, 0x7E, 0x7E}));
  r.RunMs(20);
  CHECK(r.drv.GetStats().bad_protocol_id >= 1U);
  CHECK(r.drv.GetStats().bad_escape >= 1U);
}

TEST_CASE("uart: an over-long frame is dropped and the link keeps working") {
  rig::Rig<rig::UartFull> r;
  r.step_us = 100;
  REQUIRE(r.Start(Cfg()));
  std::vector<uint8_t> noise(3003U, 0x55U); // a start flag, protocol ID 1, 3000 bytes, an end flag
  noise.front() = 0x7E;
  noise[1] = 0x01;
  noise.back() = 0x7E;
  r.wire.hub.RvcPush(noise);
  r.RunMs(50);
  CHECK(r.drv.GetStats().oversize_frame >= 1U);
  CHECK(r.drv.Ready());
  Event e{};
  const Result<OpId> res = r.drv.GetFeature(SensorId::Accelerometer);
  REQUIRE(res.ok());
  REQUIRE(r.Await(res.value, e, 1000));
}

TEST_CASE("uart: two flags in a row are one start flag") {
  rig::Rig<rig::UartFull> r;
  r.step_us = 100;
  REQUIRE(r.Start(Cfg()));
  Bytes frame{0x7E, 0x7E, 0x7E, 0x01};
  const Bytes cargo =
      Cat(B({0x0D, 0x00, 0x03, 0x00}), Cat(Base(0), Report(0x01, 0, 0, 0, {1, 0, 2, 0, 3, 0})));
  // a hand-made frame: header length 4 + 5 + 10 = 19 bytes
  Bytes full = B({0x13, 0x00, 0x03, 0x00});
  full = Cat(full, Cat(Base(0), Report(0x01, 0, 0, 0, {1, 0, 2, 0, 3, 0})));
  frame.insert(frame.end(), full.begin(), full.end());
  frame.push_back(0x7E);
  static_cast<void>(cargo);
  r.wire.hub.RvcPush(frame);
  r.RunMs(20);
  CHECK_EQ(r.Count(EventKind::Sensor), 1U);
}

TEST_CASE("uart: the hub restarts and the session recovers") {
  rig::Rig<rig::UartFull> r;
  r.step_us = 100;
  REQUIRE(r.Start(Cfg()));
  REQUIRE(r.drv.Reset().ok());
  r.RunMs(500);
  CHECK(r.drv.Ready());
}

TEST_CASE("uart: an inaccurate clock garbles bytes and the driver survives (A10)") {
  rig::Rig<rig::UartFull> r;
  r.step_us = 100;
  REQUIRE(r.Start(Cfg()));
  r.wire.hub.k.clock_internal = true;
  Event e{};
  REQUIRE(Run(r, r.drv.SetFeature(SensorId::Accelerometer, Feat(10000)), e));
  r.RunMs(800);
  CHECK(r.drv.GetState() != State::Idle);
}

TEST_CASE("uart: a lost Buffer Status Notification is asked for again and the write goes out") {
  rig::Rig<rig::UartFull> r;
  r.step_us = 100;
  REQUIRE(r.Start(Cfg()));
  r.RunMs(20); // the start-up's own product ID request is answered first
  r.wire.hub.bsn_drop = 1;
  Event e{};
  REQUIRE(Run(r, r.drv.GetFeature(SensorId::Accelerometer), e, 500));
  CHECK_EQ(e.error, Error::Ok);
  CHECK_EQ(r.wire.hub.c.bsn_dropped, 1U);
  CHECK_EQ(r.drv.GetStats().bsq_retries, 1U);
  r.wire.hub.bsn_loss_rate = 2; // every second one lost from now on
  for (uint32_t i = 0; i < 4U; ++i) {
    REQUIRE(Run(r, r.drv.GetFeature(SensorId::Accelerometer), e, 500));
    CHECK_EQ(e.error, Error::Ok);
  }
  REQUIRE(Run(r, r.drv.SetFeature(SensorId::Accelerometer, FeatureConfig{}), e, 500));
  CHECK_EQ(e.error, Error::Ok);
}

TEST_CASE("uart: the query waits uart_bsq_retry_ms for the notification before asking again") {
  rig::Rig<rig::UartFull> r;
  r.step_us = 100;
  Config c = Cfg();
  c.uart_bsq_retry_ms = 30;
  REQUIRE(r.Start(c));
  r.RunMs(20);
  r.wire.hub.bsn_drop = 1;
  const uint32_t before = r.wire.hub.c.bsq_in;
  const Result<OpId> res = r.drv.GetFeature(SensorId::Accelerometer);
  REQUIRE(res.ok());
  r.RunMs(25);
  CHECK_EQ(r.wire.hub.c.bsq_in, before + 1U);
  r.RunMs(10);
  CHECK_EQ(r.wire.hub.c.bsq_in, before + 2U);
  Event e{};
  REQUIRE(r.Await(res.value, e, 200));
  CHECK_EQ(e.error, Error::Ok);
}

TEST_CASE("uart: a notification granting exactly the transfer length lets it through") {
  rig::Rig<rig::UartFull> r;
  r.step_us = 100;
  REQUIRE(r.Start(Cfg()));
  r.wire.hub.uart_buffer_bytes = 6; // header 4 plus the 2-byte Get Feature request
  Event e{};
  REQUIRE(Run(r, r.drv.GetFeature(SensorId::Accelerometer), e, 500));
  CHECK_EQ(e.error, Error::Ok);
  CHECK_EQ(r.wire.hub.c.uart_no_token_writes, 0U);
}

TEST_CASE("uart: a write that timed out half-sent is closed and never reaches the hub") {
  rig::Rig<rig::UartFull> r;
  r.step_us = 100;
  Config c = Cfg();
  c.op_timeout_ms = 50;
  REQUIRE(r.Start(c));
  r.RunMs(20);
  const uint32_t writes = r.wire.hub.c.writes;
  r.wire.uart_tx_budget = 6; // the query (3 bytes), then 3 bytes of the request frame
  const Result<OpId> res = r.drv.GetFeature(SensorId::Accelerometer);
  REQUIRE(res.ok());
  Event e{};
  REQUIRE(r.Await(res.value, e, 500));
  CHECK_EQ(e.error, Error::Timeout);
  const uint32_t closed = r.wire.hub.c.uart_frames_closed;
  r.wire.uart_tx_budget = -1;
  r.RunMs(50);
  CHECK_EQ(r.wire.hub.c.uart_frames_closed, closed + 1U); // a flag byte ends the half-sent frame
  CHECK_EQ(r.wire.hub.c.writes, writes);
  CHECK_EQ(r.Count(EventKind::FeatureChanged), 0U);
  REQUIRE(Run(r, r.drv.GetFeature(SensorId::Accelerometer), e, 500));
  CHECK_EQ(e.error, Error::Ok);
}

TEST_CASE("uart: HardReset drops a half-sent frame, so the next session writes cleanly") {
  rig::Rig<rig::UartFull> r;
  r.step_us = 100;
  REQUIRE(r.Start(Cfg()));
  r.RunMs(20);
  r.wire.uart_tx_budget = 6;
  REQUIRE(r.drv.GetFeature(SensorId::Accelerometer).ok());
  r.RunMs(5);
  REQUIRE(r.drv.HardReset().ok());
  r.wire.uart_tx_budget = -1;
  REQUIRE(r.RunUntil([&r] { return r.Count(EventKind::Reset) == 1U; }, 1'000'000));
  for (const Event& ev : r.events) {
    if (ev.kind == EventKind::Reset) {
      CHECK_EQ(ev.detail, 4U); // the product ID request of the new session was answered
    }
  }
  CHECK_EQ(r.wire.hub.c.uart_stray_bytes, 0U); // the rest of the old frame was never sent
  Event e{};
  REQUIRE(Run(r, r.drv.GetFeature(SensorId::Accelerometer), e, 500));
  CHECK_EQ(e.error, Error::Ok);
}

TEST_CASE("uart: a resync drops a half-sent frame too") {
  rig::Rig<rig::UartFull> r;
  r.step_us = 100;
  Config c = Cfg();
  c.resync_limit = 1;
  REQUIRE(r.Start(c));
  r.RunMs(20);
  r.wire.uart_tx_budget = 6;
  REQUIRE(r.drv.GetFeature(SensorId::Accelerometer).ok());
  r.RunMs(5);
  r.wire.hub.RvcPush(B({0x7E, 0x01, 0xFF, 0xFF, 0x00, 0x00, 0x7E})); // length 0xFFFF: a resync
  r.RunMs(5);
  REQUIRE(r.drv.GetStats().resyncs == 1U);
  r.wire.uart_tx_budget = -1;
  REQUIRE(r.RunUntil([&r] { return r.Count(EventKind::Reset) == 1U; }, 1'000'000));
  for (const Event& ev : r.events) {
    if (ev.kind == EventKind::Reset) {
      CHECK_EQ(ev.detail, 4U);
    }
  }
}

TEST_CASE("uart: an advertised BSN timeout above 4294967 ms does not wrap") {
  rig::Rig<rig::UartFull> r;
  r.step_us = 100;
  r.wire.hub.uart_timeout_ms = 4294968U; // times 1000 wraps to 704 us in 32 bits
  REQUIRE(r.Start(Cfg()));
  REQUIRE(r.drv.Advertised().uart_timeout_ms == 4294968U);
  r.RunMs(20);
  r.wire.hub.RvcPush(B({0x7E, 0x00, 0x00, 0x01, 0x7E})); // a notification: 256 bytes
  r.RunMs(2);
  const uint32_t queries = r.wire.hub.c.bsq_in;
  REQUIRE(r.drv.GetFeature(SensorId::Accelerometer).ok());
  r.RunMs(5);
  CHECK_EQ(r.wire.hub.c.bsq_in, queries); // the 2 ms old token is still valid: no new query
}

TEST_CASE("uart: a start-up time-out closes a half-sent frame, so Reset() in Faulted reboots") {
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
  r.wire.uart_tx_budget = 6; // the query, then 3 bytes of the advertisement request
  REQUIRE(r.drv.Begin(c).ok());
  REQUIRE(r.RunUntil([&r] { return r.drv.GetState() == State::Faulted; }, 1'000'000));
  r.wire.uart_tx_budget = -1;
  const uint32_t boots = r.wire.hub.c.reboots;
  Event e{};
  REQUIRE(Run(r, r.drv.Reset(), e, 500));
  CHECK_EQ(e.error, Error::Ok);
  r.RunMs(10);
  CHECK_EQ(r.wire.hub.c.reboots, boots + 1U);
}

TEST_CASE("uart: without NRST a resync during a half-sent frame still resets the hub") {
  rig::Rig<rig::UartMin> r;
  r.step_us = 100;
  Config c = Cfg();
  c.resync_limit = 1;
  REQUIRE(r.Start(c));
  r.RunMs(20);
  const uint32_t boots = r.wire.hub.c.reboots;
  r.wire.uart_tx_budget = 6;
  REQUIRE(r.drv.GetFeature(SensorId::Accelerometer).ok());
  r.RunMs(5);
  r.wire.hub.RvcPush(B({0x7E, 0x01, 0xFF, 0xFF, 0x00, 0x00, 0x7E})); // length 0xFFFF: a resync
  r.RunMs(5);
  REQUIRE(r.drv.GetStats().resyncs == 1U);
  const uint32_t closed = r.wire.hub.c.uart_frames_closed;
  r.wire.uart_tx_budget = -1;
  r.RunMs(1000);
  CHECK_EQ(r.wire.hub.c.reboots, boots + 1U);
  CHECK(r.wire.hub.c.uart_frames_closed > closed); // the half-sent frame was closed first
}

TEST_CASE("uart: a notification older than the advertised UART timeout is not used") {
  rig::Rig<rig::UartFull> r;
  r.step_us = 100;
  REQUIRE(r.Start(Cfg()));
  r.RunMs(20);
  r.wire.hub.RvcPush(B({0x7E, 0x00, 0x00, 0x01, 0x7E})); // a notification nobody asked for: 256 B
  r.RunMs(1);
  r.RunMs(300); // the model advertises 200 ms: the token has expired
  const uint32_t queries = r.wire.hub.c.bsq_in;
  Event e{};
  REQUIRE(Run(r, r.drv.GetFeature(SensorId::Accelerometer), e, 500));
  CHECK_EQ(e.error, Error::Ok);
  CHECK(r.wire.hub.c.bsq_in > queries); // a fresh query, not the stale token
  CHECK_EQ(r.wire.hub.c.uart_no_token_writes, 0U);
}

TEST_CASE("uart: bytes are paced at least 100 microseconds apart whatever the poll rate") {
  rig::Rig<rig::UartFull> r;
  r.step_us = 50; // Poll() runs twice per byte time
  REQUIRE(r.Start(Cfg()));
  Event e{};
  REQUIRE(Run(r, r.drv.GetFeature(SensorId::Accelerometer), e, 500));
  CHECK_EQ(e.error, Error::Ok);
  CHECK_EQ(r.wire.hub.c.uart_gap_violations, 0U);
}

TEST_CASE("uart: a query cut after one byte is not waited for: the next operation asks again") {
  rig::Rig<rig::UartFull> r;
  r.step_us = 100;
  Config c = Cfg();
  c.op_timeout_ms = 20;
  c.uart_bsq_retry_ms = 500; // a query believed to be out would wait this long
  REQUIRE(r.Start(c));
  r.RunMs(20);
  r.wire.uart_tx_budget = 1; // the query's start flag only
  Event e{};
  REQUIRE(Run(r, r.drv.GetFeature(SensorId::Accelerometer), e, 200));
  CHECK_EQ(e.error, Error::Timeout);
  r.wire.uart_tx_budget = -1;
  REQUIRE(Run(r, r.drv.GetFeature(SensorId::Accelerometer), e, 200));
  CHECK_EQ(e.error, Error::Ok);
}

TEST_CASE("uart: with NRST a resync drops a half-sent frame before the reset") {
  rig::Rig<rig::UartFull> r;
  r.step_us = 100;
  Config c = Cfg();
  c.resync_limit = 1;
  REQUIRE(r.Start(c));
  r.RunMs(20);
  r.wire.uart_tx_budget = 6;
  REQUIRE(r.drv.GetFeature(SensorId::Accelerometer).ok());
  r.RunMs(5);
  r.wire.hub.RvcPush(B({0x7E, 0x01, 0xFF, 0xFF, 0x00, 0x00, 0x7E})); // length 0xFFFF: a resync
  r.RunMs(5);
  REQUIRE(r.drv.GetStats().resyncs == 1U);
  r.wire.uart_tx_budget = -1;
  REQUIRE(r.RunUntil([&r] { return r.drv.Ready(); }, 1'000'000));
  r.RunMs(100);
  CHECK_EQ(r.wire.hub.c.uart_stray_bytes, 0U); // the rest of the old frame never went out
}
