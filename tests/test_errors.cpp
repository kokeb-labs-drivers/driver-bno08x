/**
 * @file test_errors.cpp
 * @brief The error set: `DeviceIdMismatch` and `Crc` exist as base values and are never returned.
 */
#include <set>

#include "test_support.hpp"

using namespace ts;

namespace {

struct Spy {
  std::set<int32_t> seen;
  void Note(Error e) {
    seen.insert(static_cast<int32_t>(e));
  }
  void Note(const Result<OpId>& r) {
    Note(r.error);
  }
  void Note(const Event& e) {
    if (e.kind == EventKind::OpDone) {
      Note(e.error);
    }
  }
  [[nodiscard]] bool Has(Error e) const {
    return seen.contains(static_cast<int32_t>(e));
  }
};

uint32_t Next(uint32_t& s) {
  s = (s * 1664525U) + 1013904223U;
  return s >> 8U;
}

template <typename R>
void Storm(R& r, Spy& spy, uint32_t seed) {
  uint32_t s = seed;
  static FrsBuffer buf{}; // outlives every operation, as the API requires
  for (int32_t step = 0; step < 600; ++step) {
    switch (Next(s) % 14U) {
    case 0:
      spy.Note(r.drv.SetFeature(SensorId::Accelerometer, Feat(10000U * (1U + (Next(s) % 5U)))));
      break;
    case 1:
      spy.Note(r.drv.GetFeature(SensorId::Gravity));
      break;
    case 2:
      spy.Note(r.drv.Tare(AxisMask::All, TareBasis::RotationVector));
      break;
    case 3:
      spy.Note(r.drv.GetMeCal());
      break;
    case 4:
      r.wire.fail_reads = Next(s) % 3U;
      break;
    case 5:
      r.wire.fail_writes = Next(s) % 3U;
      break;
    case 6:
      r.wire.hub.InjectRaw(Bytes{static_cast<uint8_t>(Next(s)), static_cast<uint8_t>(Next(s)),
                                 static_cast<uint8_t>(Next(s) % 8U),
                                 static_cast<uint8_t>(Next(s))});
      break;
    case 7:
      spy.Note(r.drv.Reset());
      break;
    case 8:
      spy.Note(r.drv.ReadProductId());
      break;
    case 9:
      spy.Note(r.drv.StartSimpleCal(0));
      break;
    case 10:
      spy.Note(r.drv.SaveDcd());
      break;
    case 11:
      spy.Note(r.drv.ReadFrs(0x2D3E, buf));
      break;
    case 12:
      spy.Note(r.drv.Flush(SensorId::Accelerometer));
      break;
    default:
      break;
    }
    r.Run(5000);
    for (const Event& e : r.events) {
      spy.Note(e);
    }
    r.events.clear();
  }
}

} // namespace

TEST_CASE("errors: the base values exist and print") {
  CHECK(std::string(ToString(Error::DeviceIdMismatch)) == "DeviceIdMismatch");
  CHECK(std::string(ToString(Error::Crc)) == "Crc");
  CHECK_EQ(static_cast<int32_t>(Error::DeviceIdMismatch), 5);
  CHECK_EQ(static_cast<int32_t>(Error::Crc), 6);
}

TEST_CASE("errors: no call or operation ever returns DeviceIdMismatch or Crc (I2C storm)") {
  rig::Rig<rig::I2cFull> r;
  REQUIRE(r.Start(Cfg()));
  Spy spy;
  Storm(r, spy, 1);
  CHECK(!spy.Has(Error::DeviceIdMismatch));
  CHECK(!spy.Has(Error::Crc));
  CHECK(spy.Has(Error::Ok));
}

TEST_CASE("errors: ... nor on SPI") {
  rig::Rig<rig::SpiFull> r;
  REQUIRE(r.Start(Cfg()));
  Spy spy;
  Storm(r, spy, 7);
  CHECK(!spy.Has(Error::DeviceIdMismatch));
  CHECK(!spy.Has(Error::Crc));
}

TEST_CASE("errors: ... nor on UART-SHTP") {
  rig::Rig<rig::UartFull> r;
  r.step_us = 100;
  REQUIRE(r.Start(Cfg()));
  Spy spy;
  Storm(r, spy, 13);
  CHECK(!spy.Has(Error::DeviceIdMismatch));
  CHECK(!spy.Has(Error::Crc));
}

TEST_CASE("errors: the stress run leaves the session usable") {
  rig::Rig<rig::I2cFull> r;
  REQUIRE(r.Start(Cfg()));
  Spy spy;
  Storm(r, spy, 99);
  r.wire.fail_reads = 0;
  r.wire.fail_writes = 0;
  r.RunMs(3000);
  CHECK(r.drv.GetState() != State::Idle);
}

TEST_CASE("errors: an ID that is not a SensorId is refused, never used as an index") {
  rig::Rig<rig::I2cFull> r;
  REQUIRE(r.Start(ts::Cfg()));
  const uint32_t writes = r.wire.writes;
  for (const uint32_t raw : {0U, 0x17U, 0x1DU, 0x24U, 0x2FU, 0x7BU, 0xFFU}) {
    const auto id = static_cast<bno08x::SensorId>(raw);
    CHECK_EQ(r.drv.SetFeature(id, bno08x::FeatureConfig{}).error, bno08x::Error::InvalidArgument);
    CHECK_EQ(r.drv.GetFeature(id).error, bno08x::Error::InvalidArgument);
    CHECK_EQ(r.drv.Flush(id).error, bno08x::Error::InvalidArgument);
    CHECK_EQ(r.drv.ReadMetadata(id).error, bno08x::Error::InvalidArgument);
    CHECK_EQ(r.drv.GetCounts(id).error, bno08x::Error::InvalidArgument);
    CHECK_EQ(r.drv.ClearCounts(id).error, bno08x::Error::InvalidArgument);
    CHECK_EQ(r.drv.Latest(id).error, bno08x::Error::InvalidArgument);
    CHECK_EQ(r.drv.Feature(id).error, bno08x::Error::InvalidArgument);
    CHECK(r.drv.GetMetadata(id) == nullptr);
    CHECK_EQ(bno08x::MetadataRecord(id), 0U);
  }
  r.RunMs(20);
  CHECK_EQ(r.wire.writes, writes); // nothing was sent
}
