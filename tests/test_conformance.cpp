/**
 * @file test_conformance.cpp
 * @brief Ambiguity sweep: the knobs of the ambiguities that other files do not exercise, in both
 * settings where the driver has a choice (docs/ambiguities.md).
 */
#include "test_support.hpp"

using namespace ts;

namespace {
struct Up {
  rig::Rig<rig::I2cFull> r;
  explicit Up(Config c = Cfg()) {
    static_cast<void>(r.Start(c));
  }
};
} // namespace

TEST_CASE("A26: a write status above 11 is a refusal, whatever its value") {
  for (const uint8_t status : {uint8_t{12}, uint8_t{13}, uint8_t{200}, uint8_t{255}}) {
    Up u;
    u.r.wire.hub.frs_fail_status = status;
    const std::array<uint32_t, 2> w{1, 2};
    Event e{};
    REQUIRE(Run(u.r, u.r.drv.WriteFrs(0xD3E2, w), e));
    CHECK_EQ(e.error, Error::Refused);
    CHECK_EQ(e.detail, static_cast<uint32_t>(status));
  }
}

TEST_CASE("A28: the reserved bytes of an FRS read request are zero") {
  Up u; // the model refuses a read whose reserved bytes are not zero (default knob)
  u.r.wire.hub.frs[0x1F1F] = {7, 8, 9};
  static FrsBuffer buf;
  buf = FrsBuffer{};
  Event e{};
  REQUIRE(Run(u.r, u.r.drv.ReadFrs(0x1F1F, buf), e));
  CHECK_EQ(e.error, Error::Ok);
  CHECK_EQ(buf.length, 3U);
}

TEST_CASE("A29: one FRS operation at a time; the second is Busy, and the first still completes") {
  for (const bool interleave : {true, false}) {
    Up u;
    u.r.wire.hub.k.frs_busy_interleave = interleave;
    u.r.wire.hub.frs[0x1F1F] = {7, 8, 9};
    static FrsBuffer a;
    static FrsBuffer b;
    a = FrsBuffer{};
    b = FrsBuffer{};
    const Result<OpId> first = u.r.drv.ReadFrs(0x1F1F, a);
    const Result<OpId> second = u.r.drv.ReadFrs(0x1F1F, b);
    REQUIRE(first.ok());
    CHECK_EQ(second.error, Error::Busy);
    Event e{};
    REQUIRE(u.r.Await(first.value, e));
    CHECK_EQ(e.error, Error::Ok);
    CHECK_EQ(a.length, 3U);
  }
}

TEST_CASE("A32: the metadata record, when read, tells the Q point of the change sensitivity") {
  for (const uint16_t q : {uint16_t{13}, uint16_t{12}}) {
    Up u;
    u.r.wire.hub.k.rv_q3 = q;
    Event e{};
    REQUIRE(Run(u.r, u.r.drv.ReadMetadata(SensorId::RotationVector), e));
    const Metadata* m = u.r.drv.GetMetadata(SensorId::RotationVector);
    REQUIRE(m != nullptr);
    CHECK_EQ(m->q3, q);
  }
}

TEST_CASE("A34: the step counter's wake flag reaches the hub, and its samples then use channel 4") {
  Up u;
  FeatureConfig fc{};
  fc.interval_us = 20000;
  fc.wake = true;
  Event e{};
  REQUIRE(Run(u.r, u.r.drv.SetFeature(SensorId::StepCounter, fc), e));
  CHECK((u.r.wire.hub.feat[0x11].flags & 0x04U) != 0U);
  u.r.RunMs(100);
  bool wake_sample = false;
  for (const Event& ev : u.r.events) {
    wake_sample = wake_sample || ((ev.kind == EventKind::Sensor) && ev.wake);
  }
  CHECK(wake_sample);
}

TEST_CASE("A31: the data-sheet sensors are marked, the manual-only ones are not") {
  CHECK(InDatasheet(SensorId::RotationVector));
  CHECK(InDatasheet(SensorId::Accelerometer));
  CHECK(!InDatasheet(SensorId::OpticalFlow));
  CHECK(!InDatasheet(SensorId::DeadReckoningPose));
}
