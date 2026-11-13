/**
 * @file test_variants.cpp
 * @brief BNO080, BNO085 and BNO086: what the configured variant changes (and what it does not).
 */
#include "test_support.hpp"

using namespace ts;

TEST_CASE("variants: the product ID is recorded but never chooses behaviour (A1)") {
  rig::Rig<rig::I2cFull> r;
  r.wire.hub.k.product_part_number = 10003608; // a number no document maps to a variant
  REQUIRE(r.Start(Cfg(Variant::Bno080)));
  r.RunMs(50);
  CHECK_EQ(r.drv.Product().part_number, 10003608U);
  CHECK_EQ(r.drv.SetMotionIntent(MotionIntent::InMotion).error, Error::Variant);
}

TEST_CASE("variants: MotionRequest and interactive calibration need a BNO086 (A3, A4)") {
  for (const Variant v : {Variant::Bno080, Variant::Bno085}) {
    rig::Rig<rig::I2cFull> r;
    REQUIRE(r.Start(Cfg(v)));
    CHECK_EQ(r.drv.SetFeature(SensorId::MotionRequest, Feat(10000)).error, Error::Variant);
    CHECK_EQ(r.drv.SetMotionIntent(MotionIntent::InMotion).error, Error::Variant);
  }
  rig::Rig<rig::I2cFull> r(hubmodel::Chip::Bno086);
  REQUIRE(r.Start(Cfg(Variant::Bno086)));
  Event e{};
  REQUIRE(Run(r, r.drv.SetFeature(SensorId::MotionRequest, Feat(10000)), e));
  CHECK_EQ(r.wire.hub.feat[0x2B].interval_us, 10000U);
}

TEST_CASE("variants: strict_variant off lets the hub decide (A3)") {
  rig::Rig<rig::I2cFull> r; // a BNO085 model that accepts the BNO086 items
  r.wire.hub.k.bno086_features_on_085 = true;
  Config c = Cfg(Variant::Bno085);
  c.strict_variant = false;
  REQUIRE(r.Start(c));
  Event e{};
  REQUIRE(Run(r, r.drv.SetFeature(SensorId::MotionRequest, Feat(10000)), e));
  REQUIRE(Run(r, r.drv.SetMotionIntent(MotionIntent::InMotion), e));
  CHECK_EQ(r.wire.hub.last_motion_intent, 3U);
}

TEST_CASE("variants: a sensor the hub does not serve never answers (A31)") {
  rig::Rig<rig::I2cFull> r;
  r.wire.hub.k.sensor_mask = ~(uint64_t{1} << 0x23);
  Config c = Cfg();
  c.set_feature_confirm = true;
  c.op_timeout_ms = 200;
  REQUIRE(r.Start(c));
  CHECK_EQ(Err(r, r.drv.SetFeature(SensorId::HeartRateMonitor, Feat(100000)), 1000),
           Error::Timeout);
  CHECK(!InDatasheet(SensorId::HeartRateMonitor));
}

TEST_CASE(
    "variants: an environmental sensor with no part on the secondary bus never reports (A20)") {
  rig::Rig<rig::I2cFull> r;
  REQUIRE(r.Start(Cfg()));
  Event e{};
  REQUIRE(Run(r, r.drv.SetFeature(SensorId::Pressure, Feat(100000)), e));
  r.RunMs(500);
  CHECK_EQ(r.Count(EventKind::Sensor), 0U);
  r.wire.hub.k.env_sensors = uint64_t{1} << 0x0A;
  r.RunMs(500);
  CHECK(r.Count(EventKind::Sensor) > 0U);
}

TEST_CASE("variants: the reset cause the hub reports after an NRST pulse and a software reset") {
  rig::Rig<rig::I2cFull> r;
  REQUIRE(r.Start(Cfg()));
  r.RunMs(50);
  CHECK_EQ(r.drv.Product().reset_cause, 4U);
  REQUIRE(r.drv.Reset().ok());
  r.RunMs(400);
  Event e{};
  REQUIRE(Run(r, r.drv.ReadProductId(), e));
  CHECK_EQ(r.drv.Product().reset_cause, 2U);
}

TEST_CASE("variants: I2C BNO085 stretching and BNO086 zero-length packets do not matter when gated "
          "(A2)") {
  for (const int32_t style : {0, 1, 2}) {
    rig::Rig<rig::I2cFull> r(style == 1 ? hubmodel::Chip::Bno086 : hubmodel::Chip::Bno085);
    r.wire.hub.k.i2c_empty_poll = style;
    REQUIRE(r.Start(Cfg(style == 1 ? Variant::Bno086 : Variant::Bno085)));
    Event e{};
    REQUIRE(Run(r, r.drv.SetFeature(SensorId::Accelerometer, Feat(10000)), e));
    r.RunMs(100);
    CHECK(r.Count(EventKind::Sensor) > 3U);
    CHECK_EQ(r.wire.hub.c.empty_reads, 0U);
  }
}
