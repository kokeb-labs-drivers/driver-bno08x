/**
 * @file test_frs.cpp
 * @brief FRS read, write, erase, verify, statuses and metadata.
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

TEST_CASE("frs: read a record of several words") {
  Up u;
  u.r.wire.hub.frs[0x2D3E] = {0x11111111U, 0x22222222U, 0x33333333U, 0x44444444U};
  FrsBuffer buf{};
  Event e{};
  REQUIRE(Run(u.r, u.r.drv.ReadFrs(0x2D3E, buf), e));
  CHECK_EQ(e.error, Error::Ok);
  CHECK_EQ(buf.length, 4U);
  CHECK_EQ(buf.words[0], 0x11111111U);
  CHECK_EQ(buf.words[3], 0x44444444U);
  CHECK_EQ(buf.last_status, 3U);
}

TEST_CASE("frs: read a record of odd length") {
  Up u;
  u.r.wire.hub.frs[0xD3E2] = {1, 2, 3};
  FrsBuffer buf{};
  Event e{};
  REQUIRE(Run(u.r, u.r.drv.ReadFrs(0xD3E2, buf), e));
  CHECK_EQ(buf.length, 3U);
  CHECK_EQ(buf.words[2], 3U);
}

TEST_CASE("frs: an empty record completes with length 0, an unknown one is refused") {
  Up u;
  FrsBuffer buf{};
  Event e{};
  REQUIRE(Run(u.r, u.r.drv.ReadFrs(0x1F1F, buf), e));
  CHECK_EQ(e.error, Error::Ok);
  CHECK_EQ(buf.length, 0U);
  CHECK_EQ(buf.last_status, 5U);
  u.r.wire.hub.frs_unknown.push_back(0x9999);
  REQUIRE(Run(u.r, u.r.drv.ReadFrs(0x9999, buf), e));
  CHECK_EQ(e.error, Error::Refused);
  CHECK_EQ(e.detail, 1U);
}

TEST_CASE("frs: a second FRS request while one is queued is Busy") {
  Up u;
  FrsBuffer buf{};
  const Result<OpId> a = u.r.drv.ReadFrs(0x1F1F, buf);
  REQUIRE(a.ok());
  CHECK_EQ(u.r.drv.ReadFrs(0x1F1F, buf).error, Error::Busy);
  CHECK_EQ(u.r.drv.EraseFrs(0x1F1F).error, Error::Busy);
  CHECK_EQ(u.r.drv.ReadMetadata(SensorId::Accelerometer).error, Error::Busy);
  CHECK_EQ(Err(u.r, a), Error::Ok);
}

TEST_CASE("frs: write, then read back and compare") {
  Up u;
  const std::array<uint32_t, 4> q{0x10000000U, 0, 0, 0x20000000U};
  Event e{};
  REQUIRE(Run(u.r, u.r.drv.WriteFrs(0x2D3E, q), e));
  CHECK_EQ(e.error, Error::Ok);
  REQUIRE(u.r.wire.hub.frs.count(0x2D3E) == 1U);
  CHECK_EQ(u.r.wire.hub.frs[0x2D3E].size(), 4U);
  CHECK_EQ(u.r.wire.hub.frs[0x2D3E][3], 0x20000000U);
}

TEST_CASE("frs: write of an odd number of words") {
  Up u;
  const std::array<uint32_t, 3> w{5, 6, 7};
  Event e{};
  REQUIRE(Run(u.r, u.r.drv.WriteFrs(0xD3E2, w), e));
  CHECK_EQ(e.error, Error::Ok);
  CHECK_EQ(u.r.wire.hub.frs[0xD3E2][2], 7U);
}

TEST_CASE("frs: 8 then 3 completion (A27)") {
  Up u;
  u.r.wire.hub.k.frs_final_8_then_3 = true;
  const std::array<uint32_t, 2> w{5, 6};
  Event e{};
  REQUIRE(Run(u.r, u.r.drv.WriteFrs(0xD3E2, w), e));
  CHECK_EQ(e.error, Error::Ok);
}

TEST_CASE("frs: write statuses that fail map to Refused with the status in detail") {
  const uint8_t statuses[] = {5, 7, 9, 10, 11, 12, 13};
  for (const uint8_t s : statuses) {
    Up u;
    u.r.wire.hub.frs_fail_status = s;
    const std::array<uint32_t, 2> w{5, 6};
    Event e{};
    REQUIRE(Run(u.r, u.r.drv.WriteFrs(0xD3E2, w), e));
    CHECK_EQ(e.error, Error::Refused);
    CHECK_EQ(e.detail, static_cast<uint32_t>(s));
  }
}

TEST_CASE("frs: a read-only record and an unrecognised one are refused at the request") {
  Up u;
  const std::array<uint32_t, 1> w{1};
  Event e{};
  REQUIRE(Run(u.r, u.r.drv.WriteFrs(0x4D4D, w), e));
  CHECK_EQ(e.error, Error::Refused);
  CHECK_EQ(e.detail, 11U);
  u.r.wire.hub.frs_unknown.push_back(0x7777);
  REQUIRE(Run(u.r, u.r.drv.WriteFrs(0x7777, w), e));
  CHECK_EQ(e.error, Error::Refused);
  CHECK_EQ(e.detail, 1U);
}

TEST_CASE("frs: erase is a write of length 0") {
  Up u;
  u.r.wire.hub.frs[0x1F1F] = {1, 2};
  Event e{};
  REQUIRE(Run(u.r, u.r.drv.EraseFrs(0x1F1F), e));
  CHECK_EQ(e.error, Error::Ok);
  CHECK_EQ(u.r.wire.hub.frs.count(0x1F1F), 0U);
}

TEST_CASE("frs: without verification the write completes on status 3") {
  Config c = Cfg();
  c.frs_verify = false;
  Up u(c);
  const std::array<uint32_t, 2> w{5, 6};
  Event e{};
  REQUIRE(Run(u.r, u.r.drv.WriteFrs(0xD3E2, w), e));
  CHECK_EQ(e.error, Error::Ok);
}

TEST_CASE("frs: a too large record is rejected before anything is sent") {
  Up u;
  std::vector<uint32_t> big(kFrsMaxWords + 1U, 1U);
  CHECK_EQ(u.r.drv.WriteFrs(0xD3E2, big).error, Error::InvalidArgument);
}

TEST_CASE("frs: ReadMetadata parses revision 4 and revision 3 records (A30)") {
  for (const uint16_t rev : {static_cast<uint16_t>(4), static_cast<uint16_t>(3)}) {
    Up u;
    u.r.wire.hub.k.metadata_revision = rev;
    Event e{};
    REQUIRE(Run(u.r, u.r.drv.ReadMetadata(SensorId::RotationVector), e));
    CHECK_EQ(e.error, Error::Ok);
    const Metadata* m = u.r.drv.GetMetadata(SensorId::RotationVector);
    REQUIRE(m != nullptr);
    CHECK(m->valid);
    CHECK_EQ(m->revision, rev);
    CHECK_EQ(m->version_me, 1U);
    CHECK_EQ(m->version_mh, 2U);
    CHECK_EQ(m->version_sh, 3U);
    CHECK_EQ(m->range, 0x800U);
    CHECK_EQ(m->min_period_us, 10000U);
    CHECK_EQ(m->q3, u.r.wire.hub.k.rv_q3);
    CHECK_EQ(m->max_period_us, (rev >= 4U) ? 1'000'000U : 0U);
    const std::string_view vendor(m->vendor_id.data(), 1U);
    CHECK(vendor == "V");
  }
}

TEST_CASE("frs: ReadMetadata of the activity classifier keeps the class mask") {
  Up u;
  u.r.wire.hub.frs[0xE31C] = {0x00030201U, 0x800U,      0x100U,      0x00040080U,
                              1000U,       0x00100040U, 0x00000060U, 0U,
                              0x00040004U, 1000000U,    0x000003FFU};
  Event e{};
  REQUIRE(Run(u.r, u.r.drv.ReadMetadata(SensorId::PersonalActivityClassifier), e));
  const Metadata* m = u.r.drv.GetMetadata(SensorId::PersonalActivityClassifier);
  REQUIRE(m != nullptr);
  CHECK_EQ(m->activity_classes, 0x3FFU);
}

TEST_CASE(
    "frs: metadata of a sensor without a record is Unsupported and a short record is not valid") {
  Up u;
  CHECK_EQ(u.r.drv.ReadMetadata(SensorId::DeadReckoningPose).error, Error::Unsupported);
  CHECK_EQ(u.r.drv.ReadMetadata(SensorId::OpticalFlow).error, Error::Unsupported);
  u.r.wire.hub.frs[0xE302] = {1, 2};
  Event e{};
  REQUIRE(Run(u.r, u.r.drv.ReadMetadata(SensorId::Accelerometer), e));
  const Metadata* m = u.r.drv.GetMetadata(SensorId::Accelerometer);
  CHECK((m == nullptr) || !m->valid);
}

TEST_CASE("frs: every sensor's metadata record is the one the manual names (Figure 27)") {
  const struct {
    SensorId id;
    uint16_t rec;
  } map_rows[] = {{SensorId::Accelerometer, 0xE302},
                  {SensorId::GyroscopeCalibrated, 0xE306},
                  {SensorId::MagneticFieldCalibrated, 0xE309},
                  {SensorId::RotationVector, 0xE30B},
                  {SensorId::RawAccelerometer, 0xE301},
                  {SensorId::StepCounter, 0xE315},
                  {SensorId::PersonalActivityClassifier, 0xE31C},
                  {SensorId::GameRotationVector, 0xE30C}};
  for (const auto& m : map_rows) {
    CHECK_EQ(MetadataRecord(m.id), m.rec);
  }
}

TEST_CASE("frs: the hub is silent mid-read: the operation times out") {
  Config c = Cfg();
  c.op_timeout_ms = 100;
  Up u(c);
  FrsBuffer buf{};
  const Result<OpId> res = u.r.drv.ReadFrs(0x2D3E, buf);
  REQUIRE(res.ok());
  u.r.wire.int_dead = true;
  Event e{};
  REQUIRE(u.r.Await(res.value, e, 500));
  CHECK_EQ(e.error, Error::Timeout);
}
