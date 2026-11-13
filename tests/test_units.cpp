/**
 * @file test_units.cpp
 * @brief Helpers that need no hub: Euler angles, typed FRS records, mounting quaternions, the
 * calibration helper, names, Q points, unit conversions, error strings and configuration checks.
 */
#include <cmath>
#include <numbers>
#include <string>

#include "bno08x.hpp"
#include "test_harness.hpp"

using namespace bno08x;

namespace {
int32_t Abs(int32_t v) {
  return (v < 0) ? -v : v;
}
} // namespace

TEST_CASE("euler: known rotations") {
  const EulerCentiDeg id = ToEulerCentiDeg(0, 0, 0, 16384);
  CHECK((Abs(id.yaw) <= 3) && (Abs(id.pitch) <= 3) && (Abs(id.roll) <= 3));
  const EulerCentiDeg yaw90 = ToEulerCentiDeg(0, 0, 11585, 11585);
  CHECK(Abs(yaw90.yaw - 9000) <= 10);
  CHECK((Abs(yaw90.pitch) <= 10) && (Abs(yaw90.roll) <= 10));
  const EulerCentiDeg roll90 = ToEulerCentiDeg(11585, 0, 0, 11585);
  CHECK(Abs(roll90.roll - 9000) <= 10);
  const EulerCentiDeg pitch45 = ToEulerCentiDeg(0, 6270, 0, 15137); // sin(22.5), cos(22.5)
  CHECK(Abs(pitch45.pitch - 4500) <= 15);
  const EulerCentiDeg pitch90 = ToEulerCentiDeg(0, 11585, 0, 11585);
  CHECK(Abs(pitch90.pitch - 9000) <=
        80); // within a degree of the pole the Q14 input limits the accuracy
  const EulerCentiDeg neg = ToEulerCentiDeg(0, 0, -11585, 11585);
  CHECK(Abs(neg.yaw + 9000) <= 10);
  const EulerCentiDeg flip = ToEulerCentiDeg(0, 0, 16384, 0);
  CHECK(Abs(Abs(flip.yaw) - 18000) <= 10);
  const EulerCentiDeg over = ToEulerCentiDeg(0, 16384, 0, 3000); // not unit: the pitch is clamped
  CHECK((over.pitch <= 9000) && (over.pitch >= -9000));
}

TEST_CASE("euler: sweep against floating point") {
  int32_t worst = 0;
  for (int32_t deg = -170; deg <= 170; deg += 10) {
    const double a = static_cast<double>(deg) * std::numbers::pi / 180.0 / 2.0;
    const auto z = static_cast<int32_t>(std::lround(std::sin(a) * 16384.0));
    const auto w = static_cast<int32_t>(std::lround(std::cos(a) * 16384.0));
    const EulerCentiDeg e = ToEulerCentiDeg(0, 0, z, w);
    worst = std::max<int32_t>(worst, Abs(e.yaw - (deg * 100)));
  }
  CHECK(worst <= 12);
}

TEST_CASE("records: every typed record round trips") {
  const Quat30 q{1, -2, 3, 1 << 30};
  CHECK(DecodeOrientation(EncodeOrientation(q)) == q);
  const PowerManagement pm{123456, -654321, 7, 9};
  CHECK(DecodePowerManagement(EncodePowerManagement(pm)) == pm);
  const ArvrStabilization av{1 << 30, 5, -6, 7};
  CHECK(DecodeArvr(EncodeArvr(av)) == av);
  const SignificantMotionConfig sm{-5, 77};
  CHECK(DecodeSignificantMotion(EncodeSignificantMotion(sm)) == sm);
  const ShakeConfig sk{1000, 2000, -3, 4, true, false, true};
  CHECK(DecodeShake(EncodeShake(sk)) == sk);
  const ShakeConfig none{};
  CHECK(DecodeShake(EncodeShake(none)) == none);
  const EnvCalibration ev{-100, 32768};
  CHECK(DecodeEnvCalibration(EncodeEnvCalibration(ev)) == ev);
  const GyroRvConfig g{0x0204, 5000, 1, -2, 3, -4, 5};
  CHECK(DecodeGyroRvConfig(EncodeGyroRvConfig(g)) == g);
}

TEST_CASE("records: the 24 mounting quaternions are unit quaternions and row 0 is the identity") {
  const Quat30 id = MountingQuaternion(0);
  CHECK_EQ(id.w, 1 << 30);
  CHECK_EQ(id.x, 0);
  for (std::size_t row = 0; row < kMountingCount; ++row) {
    const Quat30 m = MountingQuaternion(row);
    const double n = std::pow(static_cast<double>(m.w) / 1073741824.0, 2) +
                     std::pow(static_cast<double>(m.x) / 1073741824.0, 2) +
                     std::pow(static_cast<double>(m.y) / 1073741824.0, 2) +
                     std::pow(static_cast<double>(m.z) / 1073741824.0, 2);
    CHECK(std::fabs(n - 1.0) < 1e-6);
  }
  CHECK(MountingQuaternion(24) == Quat30{});
  CHECK(MountingQuaternion(2) == (Quat30{0, 0, 1 << 30, 0}));
}

TEST_CASE("records: the calibration helper follows the procedure") {
  CalibrationHelper h;
  CHECK(h.Step() == CalStep::EnableSensors);
  CHECK_EQ(CalibrationHelper::MinRateHz(), 50U);
  h.Advance();
  h.Advance();
  h.Advance();
  h.Advance();
  CHECK(h.Step() == CalStep::WaitAccuracy);
  h.Advance();
  CHECK(h.Step() == CalStep::WaitAccuracy); // needs the accuracy first
  h.Observe(Accuracy::Low);
  CHECK(!h.ReadyToSave());
  h.Observe(Accuracy::High);
  CHECK(h.ReadyToSave());
  CHECK(h.Step() == CalStep::Save);
  h.Advance();
  CHECK(h.Step() == CalStep::Done);
  h.Advance();
  CHECK(h.Step() == CalStep::Done);
  h.Reset();
  CHECK(h.Step() == CalStep::EnableSensors);
  CHECK(!h.ReadyToSave());
  h.Observe(Accuracy::Medium);
  CHECK(h.ReadyToSave());
}

TEST_CASE("types: error strings, sensor names, Q points and datasheet membership") {
  CHECK(std::string(ToString(Error::Ok)) == "Ok");
  CHECK(std::string(ToString(Error::Refused)) == "Refused");
  CHECK(std::string(ToString(Error::Variant)) == "Variant");
  // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange): invalid values on purpose
  CHECK(std::string(ToString(static_cast<Error>(12))) == "Unknown");
  // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange): invalid values on purpose
  CHECK(std::string(ToString(static_cast<Error>(200))) == "Unknown");
  for (uint8_t raw = 0; raw < 0x40; ++raw) {
    SensorId id{};
    if (ToSensorId(raw, id)) {
      CHECK(std::string(SensorName(id)) != "Unknown");
    }
  }
  SensorId out{};
  CHECK(!ToSensorId(0x00, out));
  CHECK(!ToSensorId(0x17, out));
  CHECK(!ToSensorId(0x2F, out));
  CHECK(ToSensorId(0x2E, out));
  CHECK_EQ(QPoint(SensorId::Accelerometer), 8U);
  CHECK_EQ(QPoint(SensorId::RotationVector), 14U);
  CHECK_EQ(QPoint(SensorId::Pressure), 20U);
  CHECK_EQ(QPoint(SensorId::StepCounter), 0U);
  CHECK(InDatasheet(SensorId::RotationVector));
  CHECK(!InDatasheet(SensorId::OpticalFlow));
  CHECK(IsRawSensor(SensorId::RawGyroscope));
  CHECK(!IsRawSensor(SensorId::Accelerometer));
}

TEST_CASE("types: unit conversions") {
  CHECK_EQ(ToMmPerS2(256), 1000);
  CHECK_EQ(ToMilliRadPerS(512), 1000);
  CHECK_EQ(ToNanoTesla(16), 1000);
  CHECK_EQ(ToMicroRad(4096), 1'000'000);
  CHECK_EQ(ToPpm(16384), 1'000'000);
  CHECK_EQ(ToMmPerS2(-256), -1000);
}

TEST_CASE("types: Config validation") {
  Config c{};
  CHECK(c.Valid());
  c.startup_timeout_ms = 0;
  CHECK(!c.Valid());
  c = Config{};
  c.resync_limit = 0;
  CHECK(!c.Valid());
  c = Config{};
  c.i2c_address = 0x4B;
  CHECK(c.Valid());
  // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange): an invalid value on purpose
  c.variant = static_cast<Variant>(9);
  CHECK(!c.Valid());
  c = Config{};
  // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange): an invalid value on purpose
  c.read_policy = static_cast<ReadPolicy>(9);
  CHECK(!c.Valid());
  c = Config{};
  c.fixed_read_bytes = 2000;
  CHECK(!c.Valid());
  c = Config{};
  c.product_id_entries = 0;
  CHECK(!c.Valid());
  c = Config{};
  c.wake_timeout_us = 0;
  CHECK(!c.Valid());
}

TEST_CASE("types: Result carries a value or an error") {
  const Result<int32_t> ok = Result<int32_t>::Success(5);
  CHECK(ok.ok());
  CHECK(static_cast<bool>(ok));
  CHECK_EQ(ok.value, 5);
  const Result<int32_t> bad = Result<int32_t>::Failure(Error::Busy);
  CHECK(!bad.ok());
  CHECK_EQ(bad.value, 0);
  const Result<void> v = Result<void>::Success();
  CHECK(v.ok());
  CHECK(static_cast<bool>(v));
  CHECK(!Result<void>::Failure(Error::Bus).ok());
}

TEST_CASE("types: the report length table matches the manual and covers the datasheet set") {
  CHECK_EQ(reg::detail::kReportLengths[0x01], 10U);
  CHECK_EQ(reg::detail::kReportLengths[0x05], 14U);
  CHECK_EQ(reg::detail::kReportLengths[0x10], 5U);
  CHECK_EQ(reg::detail::kReportLengths[0x13],
           6U); // the stability classifier is 6 bytes (design note error)
  CHECK_EQ(reg::detail::kReportLengths[0x1F], 6U);
  CHECK_EQ(reg::detail::kReportLengths[0x2A], 0U);
}
