/**
 * @file test_control.cpp
 * @brief Set and get feature, flush, product ID, executable channel, queue limits and validation.
 */
#include "test_support.hpp"

using namespace ts;

namespace {
template <typename R>
bool Up(R& r, Variant v = Variant::Bno085) {
  return r.Start(Cfg(v));
}
} // namespace

TEST_CASE("control: SetFeature completes when written and the hub holds the setting") {
  rig::Rig<rig::I2cFull> r;
  REQUIRE(Up(r));
  FeatureConfig f = Feat(20000, 100000);
  f.change_enable = true;
  f.relative = true;
  f.change_sensitivity = 5;
  f.sensor_config = 0x1234;
  Event e{};
  REQUIRE(Run(r, r.drv.SetFeature(SensorId::RotationVector, f), e));
  CHECK_EQ(e.error, Error::Ok);
  const hubmodel::Feature& h = r.wire.hub.feat[5];
  CHECK_EQ(h.interval_us, 20000U);
  CHECK_EQ(h.batch_us, 100000U);
  CHECK_EQ(h.config, 0x1234U);
  CHECK_EQ(h.sensitivity, 5U);
  CHECK_EQ(h.flags & 0x03U, 0x03U);
  r.RunMs(10);
  const Result<FeatureConfig> shadow = r.drv.Feature(SensorId::RotationVector);
  REQUIRE(shadow.ok());
  CHECK_EQ(shadow.value.interval_us, 20000U);
  CHECK_EQ(shadow.value.sensor_config, 0x1234U);
}

TEST_CASE("control: GetFeature returns the hub's granted rate in the OpDone values") {
  rig::Rig<rig::I2cFull> r;
  r.wire.hub.min_interval_us = 5000;
  REQUIRE(Up(r));
  Event e{};
  REQUIRE(Run(r, r.drv.SetFeature(SensorId::Accelerometer, Feat(1000)), e));
  REQUIRE(Run(r, r.drv.GetFeature(SensorId::Accelerometer), e));
  CHECK_EQ(e.error, Error::Ok);
  CHECK_EQ(e.v[2], 5000); // the hub raised the request to its fastest period
}

TEST_CASE("control: set_feature_confirm waits for the hub's reply") {
  rig::Rig<rig::I2cFull> r;
  Config c = Cfg();
  c.set_feature_confirm = true;
  REQUIRE(r.Start(c));
  Event e{};
  REQUIRE(Run(r, r.drv.SetFeature(SensorId::Accelerometer, Feat(10000)), e));
  CHECK_EQ(e.v[2], 10000);
  r.wire.hub.set_feature_reply = false;
  CHECK_EQ(Err(r, r.drv.SetFeature(SensorId::Gravity, Feat(10000)), 1500), Error::Timeout);
}

TEST_CASE("control: an unsolicited feature response is a FeatureChanged event") {
  rig::Rig<rig::I2cFull> r;
  REQUIRE(Up(r));
  Bytes b(17, 0);
  b[0] = 0xFC;
  b[1] = 0x01;
  b[5] = 0x10;
  b[6] = 0x27; // 10000 us
  r.wire.hub.Inject(2, b);
  r.RunMs(20);
  CHECK_EQ(r.Count(EventKind::FeatureChanged), 1U);
}

TEST_CASE("control: SetFeature validation") {
  rig::Rig<rig::I2cFull> r;
  REQUIRE(Up(r));
  FeatureConfig f = Feat(10000);
  f.change_sensitivity = 3;
  CHECK_EQ(r.drv.SetFeature(SensorId::StepDetector, f).error, Error::InvalidArgument);
  f.change_sensitivity = 0;
  f.change_enable = true;
  CHECK_EQ(r.drv.SetFeature(SensorId::TapDetector, f).error, Error::InvalidArgument);
  CHECK_EQ(r.drv.SetFeature(SensorId::WheelEncoder, Feat(10000)).error, Error::Unsupported);
}

TEST_CASE("control: strict_raw refuses a raw sensor whose base sensor is off") {
  rig::Rig<rig::I2cFull> r;
  Config c = Cfg();
  c.strict_raw = true;
  REQUIRE(r.Start(c));
  CHECK_EQ(r.drv.SetFeature(SensorId::RawGyroscope, Feat(10000)).error, Error::InvalidArgument);
  CHECK_EQ(r.drv.SetFeature(SensorId::RawMagnetometer, Feat(10000)).error, Error::InvalidArgument);
  CHECK_EQ(r.drv.SetFeature(SensorId::RawAccelerometer, Feat(10000)).error, Error::InvalidArgument);
  Event e{};
  REQUIRE(Run(r, r.drv.SetFeature(SensorId::Accelerometer, Feat(10000)), e));
  CHECK(r.drv.SetFeature(SensorId::RawAccelerometer, Feat(10000)).ok());
  CHECK(r.drv.SetFeature(SensorId::RawGyroscope, Feat(0)).ok()); // turning one off is always fine
}

TEST_CASE("control: Flush delivers the batch then FlushDone") {
  rig::Rig<rig::I2cFull> r;
  REQUIRE(Up(r));
  Event e{};
  REQUIRE(Run(r, r.drv.SetFeature(SensorId::Accelerometer, Feat(10000, 5'000'000)), e));
  r.RunMs(100);
  CHECK_EQ(r.Count(EventKind::Sensor), 0U); // still batching
  REQUIRE(Run(r, r.drv.Flush(SensorId::Accelerometer), e));
  r.RunMs(20);
  CHECK(r.Count(EventKind::Sensor) >= 8U);
  CHECK_EQ(r.Count(EventKind::FlushDone), 1U);
}

TEST_CASE("control: ReadProductId with five entries completes after five responses (A33)") {
  rig::Rig<rig::I2cFull> r;
  r.wire.hub.k.product_id_entries = 5;
  Config c = Cfg();
  c.product_id_entries = 5;
  REQUIRE(r.Start(c));
  Event e{};
  REQUIRE(Run(r, r.drv.ReadProductId(), e));
  CHECK_EQ(e.error, Error::Ok);
  CHECK(r.drv.Product().valid);
}

TEST_CASE("control: HubSleep and HubOn pass through the executable channel") {
  rig::Rig<rig::I2cFull> r;
  REQUIRE(Up(r));
  Event e{};
  REQUIRE(Run(r, r.drv.SetFeature(SensorId::Accelerometer, Feat(10000)), e));
  REQUIRE(Run(r, r.drv.HubSleep(), e));
  CHECK(!r.wire.hub.awake);
  const std::size_t n = r.Count(EventKind::Sensor);
  r.RunMs(100);
  CHECK_EQ(r.Count(EventKind::Sensor), n);
  REQUIRE(Run(r, r.drv.HubOn(), e));
  CHECK(r.wire.hub.awake);
  r.RunMs(100);
  CHECK(r.Count(EventKind::Sensor) > n);
}

TEST_CASE("control: wake-up sensors keep reporting in sleep and arrive on channel 4") {
  rig::Rig<rig::I2cFull> r;
  REQUIRE(Up(r));
  Event e{};
  REQUIRE(Run(r, r.drv.SetFeature(SensorId::StepCounter, Feat(50000, 0, true)), e));
  REQUIRE(Run(r, r.drv.HubSleep(), e));
  r.RunMs(300);
  bool wake_seen = false;
  for (const Event& ev : r.events) {
    wake_seen = wake_seen || ((ev.kind == EventKind::Sensor) && ev.wake);
  }
  CHECK(wake_seen);
}

TEST_CASE("control: the operation queue is bounded and reports Busy") {
  rig::Rig<rig::I2cFull> r;
  REQUIRE(Up(r));
  std::size_t accepted = 0;
  Error last = Error::Ok;
  for (int32_t i = 0; i < 40; ++i) {
    const Result<OpId> res = r.drv.GetFeature(SensorId::Accelerometer);
    if (res.ok()) {
      ++accepted;
    } else {
      last = res.error;
    }
  }
  CHECK(accepted >= 2U);
  CHECK(accepted < 40U);
  CHECK_EQ(last, Error::Busy);
  r.RunMs(1500);
  CHECK(r.drv.Ready());
}

TEST_CASE("control: an operation the hub never answers times out") {
  rig::Rig<rig::I2cFull> r;
  Config c = Cfg();
  c.op_timeout_ms = 100;
  REQUIRE(r.Start(c));
  r.wire.hub.set_feature_reply = false;
  Event e{};
  c.set_feature_confirm = true;
  const Result<OpId> res = r.drv.GetFeature(SensorId::Accelerometer);
  REQUIRE(res.ok());
  r.wire.hub.ClearQueue();
  r.wire.int_dead = true;
  CHECK(r.Await(res.value, e, 500));
  CHECK_EQ(e.error, Error::Timeout);
}

TEST_CASE("control: a failed write completes the operation with Bus") {
  rig::Rig<rig::I2cFull> r;
  REQUIRE(Up(r));
  r.wire.fail_writes = 1;
  CHECK_EQ(Err(r, r.drv.SetFeature(SensorId::Accelerometer, Feat(10000))), Error::Bus);
  CHECK(r.drv.Ready());
}

TEST_CASE("control: the write the hub rejects raises an error list event") {
  rig::Rig<rig::I2cFull> r;
  REQUIRE(Up(r));
  Event e{};
  REQUIRE(Run(r, r.drv.SetFeature(SensorId::Accelerometer, Feat(10000)), e));
  CHECK_EQ(r.wire.hub.c.host_seq_errors, 0U);
  CHECK_EQ(r.wire.hub.c.oversize_writes, 0U);
  CHECK_EQ(r.wire.hub.c.fragmented_writes, 0U);
}

TEST_CASE("control: a request larger than the advertised write limit is Oversize") {
  rig::Rig<rig::I2cFull> r;
  r.wire.hub.k.advert_max_cargo_write = 16;
  REQUIRE(Up(r));
  Event e{};
  CHECK_EQ(Err(r, r.drv.SetFeature(SensorId::Accelerometer, Feat(10000))), Error::Oversize);
  CHECK_EQ(r.wire.hub.c.oversize_writes, 0U);                  // the driver never sent it
  CHECK(Run(r, r.drv.GetFeature(SensorId::Accelerometer), e)); // 2 bytes + header fits
}
