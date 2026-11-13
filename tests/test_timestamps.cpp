/**
 * @file test_timestamps.cpp
 * @brief Sample times: base timestamp, delay, rebase, overflow, batching and the interrupt time
 * source.
 */
#include "test_support.hpp"

using namespace ts;

namespace {
Bytes Rebase(uint32_t ticks) {
  Bytes b = Base(ticks);
  b[0] = 0xFA;
  return b;
}
Bytes Acc(uint8_t seq, uint8_t status, uint8_t delay_lo) {
  return Report(0x01, seq, status, delay_lo, {1, 0, 2, 0, 3, 0});
}
std::vector<Event> Samples(const std::vector<Event>& all) {
  std::vector<Event> s;
  for (const Event& e : all) {
    if (e.kind == EventKind::Sensor) {
      s.push_back(e);
    }
  }
  return s;
}
} // namespace

TEST_CASE("timestamps: t = interrupt time - base + delay, in 100 us ticks") {
  rig::Rig<rig::I2cFull> r;
  REQUIRE(r.Start(Cfg()));
  r.wire.hub.Inject(3, Cat(Base(1000), Acc(0, 0, 50)));
  r.RunMs(10);
  const std::vector<Event> s = Samples(r.events);
  REQUIRE(s.size() == 1U);
  CHECK_EQ(s[0].t_us, r.wire.hub.IntEdgeUs() - 100000U + 5000U);
}

TEST_CASE("timestamps: the 14-bit delay spans two bytes") {
  rig::Rig<rig::I2cFull> r;
  REQUIRE(r.Start(Cfg()));
  r.wire.hub.Inject(3, Cat(Base(0), Acc(0, static_cast<uint8_t>((0x12U << 2U) | 1U), 0x34)));
  r.RunMs(10);
  const std::vector<Event> s = Samples(r.events);
  REQUIRE(s.size() == 1U);
  CHECK_EQ(s[0].t_us, r.wire.hub.IntEdgeUs() + (0x1234U * 100U));
  CHECK_EQ(static_cast<int32_t>(s[0].accuracy), 1);
}

TEST_CASE("timestamps: a rebase adds to the base for the reports after it") {
  rig::Rig<rig::I2cFull> r;
  REQUIRE(r.Start(Cfg()));
  r.wire.hub.Inject(3, Cat(Cat(Cat(Base(1000), Acc(0, 0, 0)), Rebase(500)), Acc(1, 0, 0)));
  r.RunMs(10);
  const std::vector<Event> s = Samples(r.events);
  REQUIRE(s.size() == 2U);
  const uint32_t t = r.wire.hub.IntEdgeUs();
  CHECK_EQ(s[0].t_us, t - 100000U);
  CHECK_EQ(s[1].t_us, t - 50000U);
}

TEST_CASE("timestamps: an overflowing base timestamp is ignored and counted (A17)") {
  rig::Rig<rig::I2cFull> r;
  REQUIRE(r.Start(Cfg()));
  r.wire.hub.Inject(3, Cat(Base(0x7FFFFFFFU), Acc(0, 0, 10)));
  r.RunMs(10);
  const std::vector<Event> s = Samples(r.events);
  REQUIRE(s.size() == 1U);
  CHECK_EQ(s[0].t_us, r.wire.hub.IntEdgeUs() + 1000U);
  CHECK_EQ(r.drv.GetStats().base_overflow, 1U);
}

TEST_CASE("timestamps: a negative base (reference after the interrupt) moves the sample later") {
  rig::Rig<rig::I2cFull> r;
  REQUIRE(r.Start(Cfg()));
  r.wire.hub.Inject(3, Cat(Base(0xFFFFFF9CU), Acc(0, 0, 0))); // -100 ticks
  r.RunMs(10);
  const std::vector<Event> s = Samples(r.events);
  REQUIRE(s.size() == 1U);
  CHECK_EQ(s[0].t_us, r.wire.hub.IntEdgeUs() + 10000U);
}

TEST_CASE("timestamps: batched samples keep their spacing") {
  rig::Rig<rig::I2cFull> r;
  REQUIRE(r.Start(Cfg()));
  Event e{};
  REQUIRE(Run(r, r.drv.SetFeature(SensorId::Accelerometer, Feat(10000, 200000)), e));
  r.RunMs(1000);
  const std::vector<Event> s = Samples(r.events);
  REQUIRE(s.size() >= 40U);
  for (std::size_t i = 1; i < s.size(); ++i) {
    const auto dt = static_cast<int32_t>(s[i].t_us - s[i - 1].t_us);
    CHECK((dt >= 9800) && (dt <= 10200));
  }
  CHECK(r.drv.GetStats().seq_gap == 0U);
  CHECK_EQ(r.drv.GetStats().report_seq_gaps, 0U);
}

TEST_CASE("timestamps: without an interrupt time hook the time of the poll is used") {
  rig::Rig<rig::I2cMin> r;
  REQUIRE(r.Start(Cfg()));
  r.wire.hub.Inject(3, Cat(Base(0), Acc(0, 0, 0)));
  const uint32_t injected = r.wire.now_us;
  r.RunMs(10);
  const std::vector<Event> s = Samples(r.events);
  REQUIRE(s.size() == 1U);
  CHECK(s[0].t_us >= injected);
  CHECK(s[0].t_us <= (injected + 1000U));
}

TEST_CASE("timestamps: the sample time wraps with the clock") {
  rig::Rig<rig::I2cFull> r;
  r.wire.now_us = 0xFFFFF000U;
  REQUIRE(r.Start(Cfg()));
  r.wire.hub.Inject(3, Cat(Base(0), Acc(0, 0, 0)));
  r.RunMs(10);
  const std::vector<Event> s = Samples(r.events);
  REQUIRE(s.size() == 1U);
  CHECK(static_cast<int32_t>(r.wire.now_us - s[0].t_us) < 20000);
}
