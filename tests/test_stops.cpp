/**
 * @file test_stops.cpp
 * @brief Stop requests (a sensor switched off, `Reset()`, `HubSleep()`): never refused once
 * `Begin()` ran, whatever the queue or the session state holds (RD-05).
 */
#include "test_support.hpp"

using namespace ts;

namespace {

using I2cRig = rig::Rig<rig::I2cFull>;

/// Fills the queue with Get Feature requests until it refuses one; returns how many it took.
std::size_t FillQueue(I2cRig& r) {
  std::size_t n = 0;
  while (n < 32U) {
    const Result<OpId> res = r.drv.GetFeature(SensorId::Gravity);
    if (!res.ok()) {
      CHECK_EQ(res.error, Error::Busy);
      break;
    }
    ++n;
  }
  return n;
}

/// Number of `OpDone` events of `id` with `err`.
std::size_t DoneWith(const I2cRig& r, OpId id, Error err) {
  std::size_t n = 0;
  for (const Event& e : r.events) {
    if ((e.kind == EventKind::OpDone) && (e.op == id) && (e.error == err)) {
      ++n;
    }
  }
  return n;
}

/// A hub that boots and advertises but never sends the initialise response: the driver faults
/// while the hub keeps running.
bool StartFaulted(I2cRig& r) {
  r.wire.hub.k.omit_init_response = true;
  Config c = Cfg();
  c.startup_timeout_ms = 100;
  c.set_feature_confirm = true; // in Faulted nothing is read: a stop completes when written
  r.wire.hub.PowerOn(r.wire.now_us);
  if (!r.drv.Begin(c).ok()) {
    return false;
  }
  return r.RunUntil([&r] { return r.drv.GetState() == State::Faulted; }, 1'000'000);
}

} // namespace

TEST_CASE("stops: a full queue keeps its last slot for a switch-off") {
  I2cRig r;
  REQUIRE(r.Start(Cfg()));
  r.RunMs(20); // the product ID request of the start-up is answered
  CHECK_EQ(FillQueue(r), DefaultLimits::kOpQueue - 1U);
  const Result<OpId> off = r.drv.SetFeature(SensorId::Accelerometer, FeatureConfig{});
  REQUIRE(off.ok());
  Event e{};
  REQUIRE(r.Await(off.value, e));
  CHECK_EQ(e.error, Error::Ok);
}

TEST_CASE("stops: further stops displace the newest waiting operation, which ends Busy") {
  I2cRig r;
  REQUIRE(r.Start(Cfg()));
  r.RunMs(20);
  Event e{};
  REQUIRE(Run(r, r.drv.SetFeature(SensorId::Accelerometer, Feat(10000)), e));
  REQUIRE(Run(r, r.drv.SetFeature(SensorId::GyroscopeCalibrated, Feat(10000)), e));
  std::array<OpId, 8> ids{};
  std::size_t n = 0;
  while (n < ids.size()) {
    const Result<OpId> res = r.drv.GetFeature(SensorId::Gravity);
    if (!res.ok()) {
      break;
    }
    ids[n] = res.value;
    ++n;
  }
  REQUIRE(n == (DefaultLimits::kOpQueue - 1U));
  const Result<OpId> a = r.drv.SetFeature(SensorId::Accelerometer, FeatureConfig{});
  const Result<OpId> g = r.drv.SetFeature(SensorId::GyroscopeCalibrated, FeatureConfig{});
  const Result<OpId> m = r.drv.SetFeature(SensorId::MagneticFieldCalibrated, FeatureConfig{});
  REQUIRE(a.ok());
  REQUIRE(g.ok());
  REQUIRE(m.ok());
  REQUIRE(r.Await(m.value, e));
  CHECK_EQ(DoneWith(r, ids[n - 1U], Error::Busy), 1U); // the newest was displaced first
  CHECK_EQ(DoneWith(r, ids[n - 2U], Error::Busy), 1U);
  CHECK_EQ(DoneWith(r, ids[0], Error::Ok), 1U); // the one on the wire was left alone
  CHECK_EQ(DoneWith(r, a.value, Error::Ok), 1U);
  CHECK_EQ(DoneWith(r, g.value, Error::Ok), 1U);
  CHECK_EQ(r.wire.hub.feat[0x01].interval_us, 0U);
  CHECK_EQ(r.wire.hub.feat[0x02].interval_us, 0U);
}

TEST_CASE("stops: the same stop asked again while it waits returns the same operation") {
  I2cRig r;
  REQUIRE(r.Start(Cfg()));
  r.RunMs(20);
  CHECK_EQ(FillQueue(r), DefaultLimits::kOpQueue - 1U);
  const Result<OpId> first = r.drv.SetFeature(SensorId::Accelerometer, FeatureConfig{});
  const Result<OpId> again = r.drv.SetFeature(SensorId::Accelerometer, FeatureConfig{});
  const Result<OpId> reset = r.drv.Reset();
  const Result<OpId> reset_again = r.drv.Reset();
  REQUIRE(first.ok());
  REQUIRE(again.ok());
  REQUIRE(reset.ok());
  REQUIRE(reset_again.ok());
  CHECK_EQ(again.value, first.value);
  CHECK_EQ(reset_again.value, reset.value);
  CHECK(reset.value != first.value);
  // another sensor's stop between them does not matter: only requests for the same sensor do
  const Result<OpId> gravity = r.drv.SetFeature(SensorId::Gravity, FeatureConfig{});
  const Result<OpId> third = r.drv.SetFeature(SensorId::Accelerometer, FeatureConfig{});
  REQUIRE(gravity.ok());
  REQUIRE(third.ok());
  CHECK_EQ(third.value, first.value);
}

TEST_CASE("stops: a queue holding only stops refuses a new one with Busy") {
  I2cRig r;
  REQUIRE(r.Start(Cfg()));
  r.RunMs(20);
  r.wire.fail_writes = 1000; // nothing completes: every stop stays queued
  std::size_t accepted = 0;
  bool refused = false;
  for (uint8_t id = 1U; (id < 0x20U) && !refused; ++id) {
    SensorId sid{};
    if (!ToSensorId(id, sid) || (sid == SensorId::WheelEncoder)) {
      continue;
    }
    const Result<OpId> res = r.drv.SetFeature(sid, FeatureConfig{});
    if (res.ok()) {
      ++accepted;
    } else {
      CHECK_EQ(res.error, Error::Busy);
      refused = true;
    }
  }
  CHECK(refused);
  CHECK_EQ(accepted, DefaultLimits::kOpQueue);
}

TEST_CASE("stops: switch-off, Reset and HubSleep are sent while the session is Faulted") {
  I2cRig r;
  REQUIRE(StartFaulted(r));
  r.wire.hub.feat[0x01].interval_us = 10000; // the hub runs a sensor the host forgot
  const uint32_t writes = r.wire.hub.c.writes;
  Event e{};
  REQUIRE(Run(r, r.drv.SetFeature(SensorId::Accelerometer, FeatureConfig{}), e));
  CHECK_EQ(e.error, Error::Ok);
  CHECK_EQ(r.wire.hub.feat[0x01].interval_us, 0U);
  REQUIRE(Run(r, r.drv.HubSleep(), e));
  CHECK_EQ(e.error, Error::Ok);
  CHECK(!r.wire.hub.awake);
  const uint32_t boots = r.wire.hub.c.reboots;
  REQUIRE(Run(r, r.drv.Reset(), e));
  CHECK_EQ(e.error, Error::Ok);
  CHECK_EQ(r.wire.hub.c.reboots, boots + 1U);
  CHECK_EQ(r.wire.hub.c.writes, writes + 3U);
  r.RunMs(200); // the hub restarted: its sequence numbers start again from 0
  REQUIRE(Run(r, r.drv.SetFeature(SensorId::Gravity, FeatureConfig{}), e));
  REQUIRE(Run(r, r.drv.Reset(), e));
  CHECK_EQ(r.wire.hub.c.host_seq_first_not_zero, 0U);
  CHECK_EQ(r.drv.GetState(), State::Faulted); // only Begin() or HardReset() leave it
  CHECK_EQ(r.drv.GetFeature(SensorId::Accelerometer).error, Error::NotInitialized);
}

TEST_CASE("stops: a stop queued before a start-up time-out still goes out in Faulted") {
  for (const bool confirm : {false, true}) {
    I2cRig r;
    r.wire.hub.k.omit_init_response = true;
    Config c = Cfg();
    c.startup_timeout_ms = 100;
    c.set_feature_confirm = confirm; // queued as waiting for a response; in Faulted none is read
    r.wire.hub.PowerOn(r.wire.now_us);
    REQUIRE(r.drv.Begin(c).ok());
    REQUIRE(r.RunUntil([&r] { return r.drv.GetState() == State::WaitReady; }, 1'000'000));
    r.wire.hub.feat[0x01].interval_us = 10000;
    const Result<OpId> off = r.drv.SetFeature(SensorId::Accelerometer, FeatureConfig{});
    REQUIRE(off.ok());
    Event e{};
    REQUIRE(r.Await(off.value, e));
    CHECK_EQ(e.error, Error::Ok);
    CHECK_EQ(r.drv.GetState(), State::Faulted);
    CHECK_EQ(r.wire.hub.feat[0x01].interval_us, 0U);
  }
}

TEST_CASE("stops: Reset and HubSleep are accepted before the session is ready") {
  I2cRig r;
  r.wire.hub.PowerOn(r.wire.now_us);
  REQUIRE(r.drv.Begin(Cfg()).ok());
  CHECK_EQ(r.drv.GetFeature(SensorId::Accelerometer).error, Error::NotInitialized);
  const Result<OpId> sleep = r.drv.HubSleep();
  REQUIRE(sleep.ok());
  Event e{};
  REQUIRE(r.Await(sleep.value, e));
  CHECK_EQ(e.error, Error::Ok);
  CHECK(!r.wire.hub.awake);
  CHECK(r.drv.Ready());
}

TEST_CASE("stops: nothing is accepted before Begin(), and UART-RVC has no stops") {
  I2cRig r;
  CHECK_EQ(r.drv.SetFeature(SensorId::Accelerometer, FeatureConfig{}).error, Error::NotInitialized);
  CHECK_EQ(r.drv.Reset().error, Error::NotInitialized);
  CHECK_EQ(r.drv.HubSleep().error, Error::NotInitialized);
  rig::Rig<rig::RvcPort> v;
  REQUIRE(v.drv.Begin(Cfg()).ok());
  CHECK_EQ(v.drv.Reset().error, Error::Unsupported);
  CHECK_EQ(v.drv.HubSleep().error, Error::Unsupported);
}

TEST_CASE("stops: UART-SHTP and SPI also write a switch-off in Faulted") {
  rig::Rig<rig::UartFull> u;
  u.step_us = 100;
  u.wire.hub.k.omit_init_response = true;
  Config c = Cfg();
  c.startup_timeout_ms = 100;
  u.wire.hub.PowerOn(u.wire.now_us);
  REQUIRE(u.drv.Begin(c).ok());
  REQUIRE(u.RunUntil([&u] { return u.drv.GetState() == State::Faulted; }, 1'000'000));
  u.wire.hub.feat[0x01].interval_us = 10000;
  Event e{};
  REQUIRE(Run(u, u.drv.SetFeature(SensorId::Accelerometer, FeatureConfig{}), e));
  CHECK_EQ(e.error, Error::Ok);
  CHECK_EQ(u.wire.hub.feat[0x01].interval_us, 0U);
  CHECK_EQ(u.drv.GetState(), State::Faulted);

  rig::Rig<rig::SpiFull> s;
  s.wire.hub.k.omit_init_response = true;
  s.wire.hub.PowerOn(s.wire.now_us);
  REQUIRE(s.drv.Begin(c).ok());
  REQUIRE(s.RunUntil([&s] { return s.drv.GetState() == State::Faulted; }, 1'000'000));
  s.wire.hub.feat[0x01].interval_us = 10000;
  s.wire.hub.feat[0x01].next_us = s.wire.now_us;
  s.RunMs(5); // one report waits in the hub; the switch-off transfer reads it whole
  REQUIRE(Run(s, s.drv.SetFeature(SensorId::Accelerometer, FeatureConfig{}), e));
  CHECK_EQ(s.Count(EventKind::Sensor), 0U); // in Faulted what is read is not used
  CHECK_EQ(e.error, Error::Ok);
  CHECK_EQ(s.wire.hub.feat[0x01].interval_us, 0U);
  CHECK(!s.wire.hub.WakeLow());
  CHECK_EQ(s.drv.GetState(), State::Faulted);
}

TEST_CASE("stops: off, on, off leaves the sensor off; the newest request for a target wins") {
  I2cRig r;
  REQUIRE(r.Start(Cfg()));
  r.RunMs(20);
  const Result<OpId> off1 = r.drv.SetFeature(SensorId::Accelerometer, FeatureConfig{});
  const Result<OpId> on = r.drv.SetFeature(SensorId::Accelerometer, Feat(10000));
  const Result<OpId> off2 = r.drv.SetFeature(SensorId::Accelerometer, FeatureConfig{});
  REQUIRE(off1.ok());
  REQUIRE(on.ok());
  REQUIRE(off2.ok());
  CHECK(off2.value != off1.value); // an enable waits between them: the second stop is its own
  Event e{};
  REQUIRE(r.Await(off2.value, e));
  CHECK_EQ(r.wire.hub.feat[0x01].interval_us, 0U);
  REQUIRE(Run(r, r.drv.Reset(), e));
  REQUIRE(r.RunUntil([&r] { return r.Count(EventKind::Reset) == 1U; }, 1'000'000));
  r.RunMs(100);
  CHECK_EQ(r.wire.hub.feat[0x01].interval_us, 0U); // auto-restore does not re-enable it

  const Result<OpId> sleep1 = r.drv.HubSleep();
  const Result<OpId> wake = r.drv.HubOn();
  const Result<OpId> sleep2 = r.drv.HubSleep();
  REQUIRE(sleep1.ok());
  REQUIRE(wake.ok());
  REQUIRE(sleep2.ok());
  CHECK(sleep2.value != sleep1.value);
  REQUIRE(r.Await(sleep2.value, e));
  CHECK(!r.wire.hub.awake);
}

TEST_CASE("stops: a stop already written is not merged with a new one") {
  I2cRig r;
  Config c = Cfg();
  c.set_feature_confirm = true;
  REQUIRE(r.Start(c));
  r.RunMs(20);
  r.wire.hub.set_feature_reply = false; // the first stop stays on the wire, waiting for its answer
  const Result<OpId> first = r.drv.SetFeature(SensorId::Accelerometer, FeatureConfig{});
  REQUIRE(first.ok());
  r.RunMs(5);
  const Result<OpId> second = r.drv.SetFeature(SensorId::Accelerometer, FeatureConfig{});
  REQUIRE(second.ok());
  CHECK(second.value != first.value);
}

namespace {

/// Queues switch-offs for sensors that were never enabled, without polling, until the queue
/// refuses one; returns how many were accepted. IDs the driver does not know are skipped.
template <typename R>
std::size_t QueueStops(R& r, uint8_t first_id) {
  std::size_t n = 0;
  bool busy = false;
  for (uint8_t id = first_id; (id < 0x2EU) && !busy; ++id) {
    const Result<OpId> res = r.drv.SetFeature(static_cast<SensorId>(id), FeatureConfig{});
    if (res.ok()) {
      ++n;
    } else {
      busy = (res.error == Error::Busy);
    }
  }
  CHECK(busy);
  return n;
}

} // namespace

TEST_CASE("stops: a write that has started is never displaced") {
  rig::Rig<rig::SpiFull> r;
  Config c = Cfg();
  c.op_timeout_ms = 1000;
  c.wake_timeout_us = 900'000;
  REQUIRE(r.Start(c));
  r.RunMs(20);
  r.wire.int_dead = true;
  const Result<OpId> get = r.drv.GetFeature(SensorId::Gravity);
  REQUIRE(get.ok());
  r.RunMs(1); // WAKE is low: the write waits for H_INTN
  REQUIRE(r.wire.hub.WakeLow());
  CHECK_EQ(QueueStops(r, 0x10U), DefaultLimits::kOpQueue - 1U);
  r.wire.int_dead = false;
  Event e{};
  REQUIRE(r.Await(get.value, e));
  CHECK_EQ(e.error, Error::Ok);
}

TEST_CASE("stops: the driver's own product ID request is not displaced") {
  I2cRig r;
  r.wire.hub.PowerOn(r.wire.now_us);
  REQUIRE(r.drv.Begin(Cfg()).ok());
  // two stops wait for the session; the product ID request is queued behind them
  REQUIRE(r.drv.SetFeature(SensorId::Accelerometer, FeatureConfig{}).ok());
  REQUIRE(r.drv.SetFeature(SensorId::Gravity, FeatureConfig{}).ok());
  REQUIRE(r.RunUntil([&r] { return r.drv.Ready(); }, 1'000'000));
  static_cast<void>(QueueStops(r, 0x10U));
  r.RunMs(100);
  CHECK(r.drv.Product().valid);
}

TEST_CASE("stops: a displaced restore request is restored later") {
  rig::Rig<rig::SpiFull> r;
  Config c = Cfg();
  c.op_timeout_ms = 1000;
  c.wake_timeout_us = 900'000;
  REQUIRE(r.Start(c));
  const std::array<SensorId, 5> on{SensorId::Accelerometer, SensorId::GyroscopeCalibrated,
                                   SensorId::LinearAcceleration, SensorId::RotationVector,
                                   SensorId::Gravity};
  Event e{};
  for (const SensorId id : on) {
    REQUIRE(Run(r, r.drv.SetFeature(id, Feat(10000)), e));
  }
  REQUIRE(r.drv.Reset().ok());
  REQUIRE(
      r.RunUntil([&r] { return (r.drv.GetStats().hub_resets == 1U) && r.drv.Ready(); }, 1'000'000));
  r.wire.int_dead = true; // nothing goes out: the restore requests pile up
  r.RunMs(2);
  static_cast<void>(QueueStops(r, 0x10U));
  r.wire.int_dead = false;
  r.RunMs(500);
  for (const SensorId id : on) {
    CHECK(r.wire.hub.feat[static_cast<uint8_t>(id)].interval_us != 0U);
  }
}

TEST_CASE("stops: Reset after a queued HubSleep is its own request and resets the hub") {
  I2cRig r;
  REQUIRE(r.Start(Cfg()));
  r.RunMs(20);
  const uint32_t boots = r.wire.hub.c.reboots;
  const Result<OpId> sleep = r.drv.HubSleep();
  const Result<OpId> reset = r.drv.Reset();
  REQUIRE(sleep.ok());
  REQUIRE(reset.ok());
  CHECK(reset.value != sleep.value);
  Event e{};
  REQUIRE(r.Await(reset.value, e));
  r.RunMs(300);
  CHECK_EQ(r.wire.hub.c.reboots, boots + 1U);
}

TEST_CASE("stops: an enable that completes before a failed switch-off does not come back") {
  // on, then off; the on is written, the off fails: the newest request was off. The sensor was
  // already on before, so the restore list held it.
  I2cRig r;
  REQUIRE(r.Start(Cfg()));
  r.RunMs(20);
  Event before{};
  REQUIRE(Run(r, r.drv.SetFeature(SensorId::Accelerometer, Feat(20000)), before));
  const Result<OpId> on = r.drv.SetFeature(SensorId::Accelerometer, Feat(10000));
  const Result<OpId> off = r.drv.SetFeature(SensorId::Accelerometer, FeatureConfig{});
  REQUIRE(on.ok());
  REQUIRE(off.ok());
  Event e{};
  REQUIRE(r.Await(on.value, e));
  CHECK_EQ(e.error, Error::Ok);
  r.wire.fail_writes = 1U; // the switch-off write fails
  REQUIRE(r.Await(off.value, e));
  CHECK_EQ(e.error, Error::Bus);
  REQUIRE(r.drv.HardReset().ok());
  REQUIRE(r.RunUntil([&r] { return r.Count(EventKind::Reset) >= 1U; }, 1'000'000));
  r.RunMs(200);
  CHECK_EQ(r.wire.hub.feat[0x01].interval_us, 0U); // auto-restore does not enable it again
}

TEST_CASE("stops: a hub restart between an enable and its switch-off leaves the sensor off") {
  for (const bool confirm : {false, true}) {
    I2cRig r;
    Config c = Cfg();
    c.set_feature_confirm = confirm;
    REQUIRE(r.Start(c));
    r.RunMs(20);
    const Result<OpId> on = r.drv.SetFeature(SensorId::Accelerometer, Feat(10000));
    const Result<OpId> off = r.drv.SetFeature(SensorId::Accelerometer, FeatureConfig{});
    REQUIRE(on.ok());
    REQUIRE(off.ok());
    Event e{};
    REQUIRE(r.Await(on.value, e));
    CHECK_EQ(e.error, Error::Ok);
    r.wire.hub.PowerOn(r.wire.now_us); // the hub restarts by itself (watchdog)
    REQUIRE(r.Await(off.value, e));
    r.RunMs(400);
    CHECK_EQ(r.drv.GetStats().hub_resets, 1U);
    CHECK_EQ(r.wire.hub.feat[0x01].interval_us, 0U);
  }
}

TEST_CASE("stops: a restore waits behind a request the caller queued for the same sensor") {
  rig::Rig<rig::SpiFull> r;
  Config c = Cfg();
  c.op_timeout_ms = 1000;
  c.wake_timeout_us = 900'000;
  REQUIRE(r.Start(c));
  Event e{};
  for (uint8_t id = 0x01U; id <= 0x09U; ++id) { // nine sensors on at 20 ms
    REQUIRE(Run(r, r.drv.SetFeature(static_cast<SensorId>(id), Feat(20000)), e));
  }
  r.wire.hub.PowerOn(r.wire.now_us); // the hub restarts; the driver restores them
  REQUIRE(
      r.RunUntil([&r] { return (r.drv.GetStats().hub_resets == 1U) && r.drv.Ready(); }, 1'000'000));
  r.wire.int_dead = true; // nothing goes out for a while: the restore reaches gravity meanwhile
  const Result<OpId> on = r.drv.SetFeature(SensorId::Gravity, Feat(10000));
  REQUIRE(on.ok());
  r.RunMs(2);
  r.wire.int_dead = false;
  REQUIRE(r.Await(on.value, e));
  CHECK_EQ(e.error, Error::Ok);
  r.RunMs(300);
  CHECK_EQ(r.wire.hub.feat[0x06].interval_us, 10000U); // the caller's newer request holds
  CHECK_EQ(r.wire.hub.feat[0x09].interval_us, 20000U); // the others came back
}

TEST_CASE("stops: Begin() again ends every waiting operation with NotInitialized") {
  I2cRig r;
  REQUIRE(r.Start(Cfg()));
  r.RunMs(20);
  r.wire.fail_writes = 1000; // nothing completes before the new session
  const Result<OpId> get = r.drv.GetFeature(SensorId::Accelerometer);
  const Result<OpId> off = r.drv.SetFeature(SensorId::Gravity, FeatureConfig{});
  REQUIRE(get.ok());
  REQUIRE(off.ok());
  r.wire.fail_writes = 0;
  REQUIRE(r.drv.Begin(Cfg()).ok());
  REQUIRE(r.RunUntil([&r] { return r.drv.Ready(); }, 2'000'000));
  r.RunMs(100);
  CHECK_EQ(DoneWith(r, get.value, Error::NotInitialized), 1U);
  CHECK_EQ(DoneWith(r, off.value, Error::NotInitialized), 1U);
  CHECK_EQ(r.Count(EventKind::OpDone), 2U);
}
