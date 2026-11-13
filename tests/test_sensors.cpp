/**
 * @file test_sensors.cpp
 * @brief Decoding of every sensor report from hand-built bytes, accuracy, latest samples, the event
 * queue.
 */
#include "test_support.hpp"

using namespace ts;

namespace {

struct Small {
  static constexpr std::size_t kRxCargo = 1024;
  static constexpr std::size_t kTxCargo = 128;
  static constexpr std::size_t kOpQueue = 8;
  static constexpr std::size_t kEventQueue = 4;
};

struct Ready {
  rig::Rig<rig::I2cFull> r;
  Ready() {
    static_cast<void>(r.Start(Cfg()));
  }
  /// Injects one report on channel 3 behind a zero base timestamp and returns the new sensor event.
  bool Feed(const Bytes& report, Event& out, bool wake = false) {
    const std::size_t before = r.events.size();
    r.wire.hub.Inject(wake ? 4 : 3, Cat(Base(0), report));
    r.RunMs(10);
    for (std::size_t i = before; i < r.events.size(); ++i) {
      if (r.events[i].kind == EventKind::Sensor) {
        out = r.events[i];
        return true;
      }
    }
    return false;
  }
};

Bytes Pad(Bytes b, std::size_t len) {
  b.resize(len, 0);
  return b;
}

} // namespace

TEST_CASE("sensors: vector sensors decode three signed Q-point values") {
  Ready f;
  REQUIRE(f.r.drv.Ready());
  Event e{};
  for (const uint8_t id : std::initializer_list<uint8_t>{0x01, 0x02, 0x03, 0x04, 0x06}) {
    REQUIRE(f.Feed(Report(id, 1, 0x02, 0, {0x00, 0x01, 0x00, 0xFF, 0x80, 0x00}), e));
    CHECK_EQ(static_cast<int32_t>(e.sensor), static_cast<int32_t>(id));
    CHECK_EQ(e.v[0], 256);
    CHECK_EQ(e.v[1], -256);
    CHECK_EQ(e.v[2], 128);
    CHECK_EQ(static_cast<int32_t>(e.accuracy), 2);
    CHECK_EQ(e.report_seq, 1U);
  }
}

TEST_CASE("sensors: rotation vectors carry four components and the accuracy estimate") {
  Ready f;
  Event e{};
  REQUIRE(f.Feed(
      Report(0x05, 0, 0x03, 0, {0x00, 0x40, 0x00, 0x00, 0x00, 0xC0, 0xFF, 0x3F, 0x00, 0x08}), e));
  CHECK_EQ(e.v[0], 0x4000);
  CHECK_EQ(e.v[1], 0);
  CHECK_EQ(e.v[2], -16384);
  CHECK_EQ(e.v[3], 0x3FFF);
  CHECK_EQ(e.v[4], 0x0800);
  REQUIRE(f.Feed(Report(0x09, 0, 0x01, 0, {1, 0, 2, 0, 3, 0, 4, 0, 5, 0}), e));
  CHECK_EQ(e.v[4], 5);
  REQUIRE(f.Feed(Report(0x28, 0, 0x01, 0, {1, 0, 2, 0, 3, 0, 4, 0, 6, 0}), e));
  CHECK_EQ(e.v[4], 6);
  REQUIRE(f.Feed(Report(0x08, 0, 0x01, 0, {1, 0, 2, 0, 3, 0, 4, 0}), e));
  CHECK_EQ(e.v[3], 4);
  REQUIRE(f.Feed(Report(0x29, 0, 0x01, 0, {9, 0, 8, 0, 7, 0, 6, 0}), e));
  CHECK_EQ(e.v[0], 9);
}

TEST_CASE("sensors: uncalibrated gyroscope and magnetometer carry the bias") {
  Ready f;
  Event e{};
  REQUIRE(f.Feed(Report(0x07, 0, 0, 0, {1, 0, 2, 0, 3, 0, 4, 0, 5, 0, 6, 0}), e));
  for (int32_t i = 0; i < 6; ++i) {
    CHECK_EQ(e.v[static_cast<std::size_t>(i)], i + 1);
  }
  REQUIRE(f.Feed(Report(0x0F, 0, 0, 0, {0xFF, 0xFF, 0, 0x80, 3, 0, 4, 0, 5, 0, 6, 0}), e));
  CHECK_EQ(e.v[0], -1);
  CHECK_EQ(e.v[1], -32768);
}

TEST_CASE("sensors: environmental sensors") {
  Ready f;
  Event e{};
  REQUIRE(f.Feed(Report(0x0A, 0, 0, 0, {0x00, 0x00, 0x10, 0x00}), e)); // pressure, Q20
  CHECK_EQ(e.v[0], 0x100000);
  REQUIRE(f.Feed(Report(0x0B, 0, 0, 0, {0x00, 0x01, 0x00, 0x00}), e)); // light
  CHECK_EQ(e.v[0], 256);
  REQUIRE(f.Feed(Report(0x0C, 0, 0, 0, {0x00, 0x32}), e)); // humidity
  CHECK_EQ(e.v[0], 0x3200);
  REQUIRE(f.Feed(Report(0x0D, 0, 0, 0, {0x10, 0x00}), e)); // proximity
  CHECK_EQ(e.v[0], 16);
  REQUIRE(f.Feed(Report(0x0E, 0, 0, 0, {0x00, 0xF0}), e)); // temperature, negative
  CHECK_EQ(e.v[0], -4096);
}

TEST_CASE("sensors: event reports use the manual's widths (A18, A19)") {
  Ready f;
  Event e{};
  REQUIRE(f.Feed(Report(0x10, 0, 0, 0, {0x05}), e)); // tap detector, one byte
  CHECK_EQ(e.v[0], 5);
  REQUIRE(f.Feed(Report(0x11, 0, 0, 0, {0x10, 0x00, 0x00, 0x00, 0x2A, 0x01, 0xEE, 0xEE}),
                 e)); // step counter
  CHECK_EQ(e.v[0], 16);
  CHECK_EQ(e.v[1], 0x012A); // 16 bits: the upper two bytes are not part of it
  REQUIRE(f.Feed(Report(0x12, 0, 0, 0, {0x01, 0x00}), e));
  CHECK_EQ(e.v[0], 1);
  REQUIRE(f.Feed(Report(0x13, 0, 0, 0, {0x03, 0x00}), e));
  CHECK_EQ(e.v[0], 3);
  REQUIRE(f.Feed(Report(0x18, 0, 0, 0, {0x20, 0x4E, 0x00, 0x00}), e));
  CHECK_EQ(e.v[0], 20000);
  for (const uint8_t id :
       std::initializer_list<uint8_t>{0x19, 0x1A, 0x1B, 0x1C, 0x20, 0x21, 0x22}) {
    REQUIRE(f.Feed(Report(id, 0, 0, 0, {0x07, 0x00}), e));
    CHECK_EQ(e.v[0], 7);
  }
  REQUIRE(f.Feed(Report(0x1F, 0, 0, 0, {0x01, 0x00}), e));
  CHECK_EQ(e.v[0], 1);
  REQUIRE(f.Feed(Report(0x23, 0, 0, 0, {0x48, 0x00}), e));
  CHECK_EQ(e.v[0], 72);
  const uint32_t before = f.r.drv.GetStats().payload_upper_nonzero;
  REQUIRE(f.Feed(Report(0x12, 0, 0, 0, {0x01, 0x80}), e));
  CHECK_EQ(f.r.drv.GetStats().payload_upper_nonzero, before + 1U);
}

TEST_CASE("sensors: raw sensors carry the hub's own timestamp") {
  Ready f;
  Event e{};
  REQUIRE(f.Feed(Report(0x14, 0, 0, 0, {1, 0, 2, 0, 3, 0, 0, 0, 0x10, 0x27, 0, 0}), e));
  CHECK_EQ(e.v[0], 1);
  CHECK_EQ(e.v[2], 3);
  CHECK_EQ(e.hub_t_us, 10000U);
  REQUIRE(f.Feed(Report(0x15, 0, 0, 0, {1, 0, 2, 0, 3, 0, 0x34, 0x12, 0x20, 0x4E, 0, 0, 0, 0}), e));
  CHECK_EQ(e.v[3], 0x1234); // temperature
  CHECK_EQ(e.hub_t_us, 20000U);
  REQUIRE(f.Feed(Report(0x16, 0, 0, 0, {1, 0, 2, 0, 3, 0, 0, 0, 0x30, 0x75, 0, 0}), e));
  CHECK_EQ(e.hub_t_us, 30000U);
}

TEST_CASE("sensors: personal activity classifier, motion request, optical flow and pose") {
  Ready f;
  Event e{};
  REQUIRE(f.Feed(Report(0x1E, 0, 0, 0, {0x81, 3, 90, 80, 70, 60, 50, 40, 30, 20, 10, 5}), e));
  CHECK_EQ(e.v[0], 0x81);
  CHECK_EQ(e.v[1], 3);
  CHECK_EQ(e.v[11], 5);
  REQUIRE(f.Feed(Report(0x2B, 0, 0, 0, {0x02, 0x03}), e));
  CHECK_EQ(e.v[0], 2);
  CHECK_EQ(e.v[1], 3);
  Bytes flow = Pad(Report(0x2C, 0, 0, 0, {1, 0, 2, 0, 3, 0}), 24);
  REQUIRE(f.Feed(flow, e));
  CHECK_EQ(e.v[0], 1);
  CHECK_EQ(e.v[2], 3);
  Bytes pose = Pad(Report(0x2D, 0, 0, 0, {0x10, 0x27, 0, 0, 0xFF, 0xFF, 0xFF, 0xFF}), 60);
  REQUIRE(f.Feed(pose, e));
  CHECK_EQ(e.v[0], 10000);
  CHECK_EQ(e.hub_t_us, 10000U);
  CHECK_EQ(e.v[1], -1);
}

TEST_CASE("sensors: the gyro-integrated rotation vector arrives on channel 5 without a report ID") {
  Ready f;
  f.r.wire.hub.Inject(5, B({0, 0x40, 0, 0, 0, 0, 0xFF, 0x3F, 1, 0, 2, 0, 3, 0}));
  f.r.RunMs(10);
  const Result<Event> ev = f.r.drv.Latest(SensorId::GyroIntegratedRotationVector);
  REQUIRE(ev.ok());
  CHECK_EQ(ev.value.v[0], 0x4000);
  CHECK_EQ(ev.value.v[3], 0x3FFF);
  CHECK_EQ(ev.value.v[6], 3);
  f.r.wire.hub.Inject(5, B({1, 2, 3}));
  f.r.RunMs(10);
  CHECK_EQ(f.r.drv.GetStats().bad_gyrorv, 1U);
}

TEST_CASE("sensors: Latest and AgeUs") {
  Ready f;
  CHECK_EQ(f.r.drv.Latest(SensorId::Accelerometer).error, Error::NotAdvertised);
  Event e{};
  REQUIRE(f.Feed(Report(0x01, 0, 0, 0, {1, 0, 2, 0, 3, 0}), e));
  const Result<Event> l = f.r.drv.Latest(SensorId::Accelerometer);
  REQUIRE(l.ok());
  CHECK_EQ(l.value.v[1], 2);
  f.r.RunMs(10);
  CHECK(f.r.drv.AgeUs(l.value) >= 9000U);
}

TEST_CASE("sensors: unknown and truncated reports are counted, not decoded") {
  Ready f;
  Event e{};
  f.r.wire.hub.Inject(3, Cat(Base(0), B({0x17, 0, 0, 0, 0, 0})));
  f.r.RunMs(10);
  CHECK_EQ(f.r.drv.GetStats().unknown_report, 1U);
  f.r.wire.hub.Inject(3, Cat(Base(0), B({0x01, 0, 0, 0, 1, 0})));
  f.r.RunMs(10);
  CHECK_EQ(f.r.drv.GetStats().short_report, 1U);
  CHECK(!f.Feed(Report(0x2A, 0, 0, 0, {}), e)); // 0x2A is never an input report ID
}

TEST_CASE("sensors: several reports in one cargo and the sequence tracker (A16)") {
  Ready f;
  Bytes cargo = Base(0);
  cargo = Cat(cargo, Report(0x01, 1, 0, 0, {1, 0, 0, 0, 0, 0}));
  cargo = Cat(cargo, Report(0x02, 7, 0, 10, {2, 0, 0, 0, 0, 0}));
  cargo = Cat(cargo, Report(0x01, 2, 0, 20, {3, 0, 0, 0, 0, 0}));
  cargo = Cat(cargo, Report(0x01, 9, 0, 30, {4, 0, 0, 0, 0, 0}));
  f.r.wire.hub.Inject(3, cargo);
  f.r.RunMs(10);
  CHECK_EQ(f.r.Count(EventKind::Sensor), 4U);
  CHECK_EQ(f.r.drv.GetStats().report_seq_gaps, 1U); // 2 then 9 on the accelerometer
}

TEST_CASE("sensors: a full event queue drops the newest and leaves one marker") {
  rig::Rig<rig::I2cFull, Small> r;
  REQUIRE(r.Start(Cfg()));
  // the driver's own queue is only drained by PopEvent: fill it without draining
  for (int32_t i = 0; i < 8; ++i) {
    r.wire.hub.Inject(
        3, Cat(Base(0), Report(0x01, static_cast<uint8_t>(i), 0, 0, {1, 0, 2, 0, 3, 0})));
  }
  const uint32_t end = r.wire.now_us + 20000U;
  while (r.wire.now_us < end) {
    r.wire.now_us += 200;
    r.wire.hub.Tick(r.wire.now_us);
    r.drv.Poll(r.wire.now_us);
  }
  CHECK(r.drv.GetStats().events_dropped >= 4U);
  Event e{};
  std::size_t markers = 0;
  while (r.drv.PopEvent(e)) {
    markers += (e.kind == EventKind::Dropped) ? 1U : 0U;
  }
  CHECK_EQ(markers, 0U); // nothing has been dropped since: the marker is queued with the next event
  r.wire.hub.Inject(3, Cat(Base(0), Report(0x01, 99, 0, 0, {1, 0, 2, 0, 3, 0})));
  r.RunMs(10);
  CHECK(r.Count(EventKind::Dropped) == 1U);
}

TEST_CASE("sensors: accuracy values and wake flag") {
  Ready f;
  Event e{};
  for (uint8_t a = 0; a < 4; ++a) {
    REQUIRE(f.Feed(Report(0x01, a, a, 0, {1, 0, 2, 0, 3, 0}), e, a == 3));
    CHECK_EQ(static_cast<int32_t>(e.accuracy), static_cast<int32_t>(a));
    CHECK_EQ(e.wake, a == 3);
  }
}

TEST_CASE("sensors: with one slot free and a drop pending, the next event is dropped too") {
  rig::Rig<rig::I2cFull, Small> r;
  REQUIRE(r.Start(Cfg()));
  const auto poll = [&r](uint32_t us) {
    const uint32_t end = r.wire.now_us + us;
    while (r.wire.now_us < end) {
      r.wire.now_us += 200;
      r.wire.hub.Tick(r.wire.now_us);
      r.drv.Poll(r.wire.now_us);
    }
  };
  for (int32_t i = 0; i < 8; ++i) {
    r.wire.hub.Inject(
        3, Cat(Base(0), Report(0x01, static_cast<uint8_t>(i), 0, 0, {1, 0, 2, 0, 3, 0})));
  }
  poll(20000U);
  const uint32_t dropped = r.drv.GetStats().events_dropped;
  REQUIRE(dropped >= 1U);
  Event e{};
  REQUIRE(r.drv.PopEvent(e)); // one slot free: the marker and an event need two
  r.wire.hub.Inject(3, Cat(Base(0), Report(0x01, 50, 0, 0, {1, 0, 2, 0, 3, 0})));
  poll(10000U);
  CHECK_EQ(r.drv.GetStats().events_dropped, dropped + 1U);
  std::size_t left = 0;
  while (r.drv.PopEvent(e)) {
    ++left;
  }
  CHECK_EQ(left, Small::kEventQueue - 1U);
}
