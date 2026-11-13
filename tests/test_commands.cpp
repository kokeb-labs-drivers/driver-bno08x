/**
 * @file test_commands.cpp
 * @brief SH-2 commands: tare, calibration, DCD, counters, errors, oscillator, initialise, wheel,
 * intent.
 */
#include "test_support.hpp"

using namespace ts;

namespace {
struct Up {
  rig::Rig<rig::I2cFull> r;
  explicit Up(Variant v = Variant::Bno086)
      : r(v == Variant::Bno086 ? hubmodel::Chip::Bno086 : hubmodel::Chip::Bno085) {
    static_cast<void>(r.Start(Cfg(v)));
  }
};
} // namespace

TEST_CASE("commands: tare now with axes and basis") {
  Up u;
  Event e{};
  REQUIRE(Run(u.r, u.r.drv.Tare(AxisMask::All, TareBasis::GameRotationVector), e));
  CHECK_EQ(u.r.wire.hub.tares, 1U);
  CHECK_EQ(u.r.wire.hub.last_tare_axes, 7U);
  CHECK_EQ(u.r.wire.hub.last_tare_basis, 1U);
  REQUIRE(Run(u.r, u.r.drv.Tare(AxisMask::ZOnly, TareBasis::ArvrGameRotationVector), e));
  CHECK_EQ(u.r.wire.hub.last_tare_axes, 4U);
  CHECK_EQ(u.r.wire.hub.last_tare_basis, 5U);
}

TEST_CASE("commands: tare masks other than 0x07 and 0x04 are refused unless tare_any_mask (A37)") {
  Up u;
  CHECK_EQ(u.r.drv.Tare(AxisMask::X, TareBasis::RotationVector).error, Error::InvalidArgument);
  CHECK_EQ(u.r.drv.Tare(AxisMask::Y, TareBasis::RotationVector).error, Error::InvalidArgument);
  // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange): an invalid value on purpose
  CHECK_EQ(u.r.drv.Tare(AxisMask::All, static_cast<TareBasis>(6)).error, Error::InvalidArgument);
  rig::Rig<rig::I2cFull> r2;
  Config c = Cfg();
  c.tare_any_mask = true;
  REQUIRE(r2.Start(c));
  Event e{};
  REQUIRE(Run(r2, r2.drv.Tare(AxisMask::X, TareBasis::RotationVector), e));
  CHECK_EQ(r2.wire.hub.last_tare_axes, 1U);
}

TEST_CASE("commands: persist tare and reorientation") {
  Up u;
  Event e{};
  REQUIRE(Run(u.r, u.r.drv.PersistTare(), e));
  CHECK(u.r.wire.hub.last_tare_persist);
  Quat14 q{};
  q.x = 1;
  q.y = -2;
  q.z = 3;
  q.w = 0x4000;
  REQUIRE(Run(u.r, u.r.drv.SetReorientation(q), e));
  const Bytes& ro = u.r.wire.hub.last_reorient;
  REQUIRE(ro.size() == 8U);
  CHECK_EQ(ro[0], 1U);
  CHECK_EQ(ro[2], 0xFEU);
  CHECK_EQ(ro[3], 0xFFU);
  CHECK_EQ(ro[7], 0x40U);
  REQUIRE(Run(u.r, u.r.drv.ClearReorientation(), e));
  for (const uint8_t b : u.r.wire.hub.last_reorient) {
    CHECK_EQ(b, 0U);
  }
}

TEST_CASE("commands: ME calibration configure and get") {
  Up u;
  MeCal cal{};
  cal.accel = true;
  cal.gyro = false;
  cal.mag = true;
  cal.planar = true;
  cal.on_table = true;
  Event e{};
  REQUIRE(Run(u.r, u.r.drv.ConfigureMeCal(cal), e));
  CHECK_EQ(u.r.wire.hub.me_cal[0], 1U);
  CHECK_EQ(u.r.wire.hub.me_cal[1], 0U);
  CHECK_EQ(u.r.wire.hub.me_cal[2], 1U);
  CHECK_EQ(u.r.wire.hub.me_cal[4], 1U);
  CHECK_EQ(u.r.wire.hub.me_cal[5], 1U);
  REQUIRE(Run(u.r, u.r.drv.GetMeCal(), e));
  CHECK_EQ(e.v[1], 1);
  CHECK_EQ(e.v[2], 0);
  CHECK_EQ(e.v[3], 1);
  CHECK_EQ(e.v[4], 1);
  CHECK_EQ(e.v[5], 1);
  u.r.wire.hub.me_status = 1;
  REQUIRE(Run(u.r, u.r.drv.GetMeCal(), e));
  CHECK_EQ(e.error, Error::DeviceFault);
}

TEST_CASE("commands: save DCD, periodic DCD polarity (A35), clear DCD and reset") {
  Up u;
  Event e{};
  REQUIRE(Run(u.r, u.r.drv.SaveDcd(), e));
  CHECK_EQ(u.r.wire.hub.dcd_saves, 1U);
  u.r.wire.hub.save_dcd_status = 1;
  REQUIRE(Run(u.r, u.r.drv.SaveDcd(), e));
  CHECK_EQ(e.error, Error::DeviceFault);
  REQUIRE(Run(u.r, u.r.drv.SetPeriodicDcd(true), e));
  CHECK_EQ(u.r.wire.hub.periodic_dcd_p0, 0U);
  REQUIRE(Run(u.r, u.r.drv.SetPeriodicDcd(false), e));
  CHECK_EQ(u.r.wire.hub.periodic_dcd_p0, 1U);
  REQUIRE(Run(u.r, u.r.drv.ClearDcdAndReset(), e));
  CHECK(u.r.wire.hub.dcd_cleared);
  u.r.RunMs(400);
  CHECK(u.r.drv.Ready());
}

TEST_CASE("commands: oscillator and initialise") {
  Up u;
  u.r.wire.hub.oscillator = 1;
  Event e{};
  REQUIRE(Run(u.r, u.r.drv.GetOscillator(), e));
  CHECK_EQ(e.v[0], 1);
  REQUIRE(Run(u.r, u.r.drv.Initialize(), e));
  CHECK_EQ(e.error, Error::Ok);
  CHECK_EQ(e.v[1], 1);
  u.r.wire.hub.init_status = 1;
  REQUIRE(Run(u.r, u.r.drv.Initialize(), e));
  CHECK_EQ(e.error, Error::DeviceFault);
}

TEST_CASE("commands: counters") {
  Up u;
  Event e{};
  REQUIRE(Run(u.r, u.r.drv.GetCounts(SensorId::Accelerometer), e));
  CHECK_EQ(e.error, Error::Ok);
  CHECK_EQ(e.v[0], 1);
  CHECK_EQ(e.v[1], 1);
  CHECK_EQ(e.v[2], 1000);
  CHECK_EQ(e.v[3], 900);
  CHECK_EQ(e.v[4], 800);
  CHECK_EQ(e.v[5], 700);
  REQUIRE(Run(u.r, u.r.drv.ClearCounts(SensorId::Accelerometer), e));
  REQUIRE(Run(u.r, u.r.drv.GetCounts(SensorId::Accelerometer), e));
  CHECK_EQ(e.v[2], 0);
}

TEST_CASE("commands: the error queue arrives as events and ends with source 255") {
  Up u;
  u.r.wire.hub.sh2_errors.push_back(B({1, 7, 2, 40, 3, 4}));
  u.r.wire.hub.sh2_errors.push_back(B({0, 8, 1, 41, 5, 6}));
  Event e{};
  REQUIRE(Run(u.r, u.r.drv.ReportErrors(1), e));
  CHECK_EQ(e.error, Error::Ok);
  CHECK_EQ(u.r.Count(EventKind::Sh2Error), 2U);
  for (const Event& ev : u.r.events) {
    if (ev.kind == EventKind::Sh2Error) {
      CHECK((ev.v[2] == 2) || (ev.v[2] == 1));
    }
  }
  u.r.events.clear();
  REQUIRE(Run(u.r, u.r.drv.ReportErrors(0), e));
  CHECK_EQ(u.r.Count(EventKind::Sh2Error),
           1U); // only severity 0 and above... the filter is the hub's
}

TEST_CASE("commands: simple calibration start and finish") {
  Up u;
  Event e{};
  REQUIRE(Run(u.r, u.r.drv.StartSimpleCal(0), e));
  CHECK(u.r.wire.hub.simple_cal_started);
  CHECK_EQ(u.r.wire.hub.simple_cal_interval, 10000U);
  REQUIRE(Run(u.r, u.r.drv.StartSimpleCal(25000), e));
  CHECK_EQ(u.r.wire.hub.simple_cal_interval, 25000U);
  REQUIRE(Run(u.r, u.r.drv.FinishSimpleCal(), e));
  CHECK_EQ(e.error, Error::Ok);
  u.r.wire.hub.simple_cal_status = 3;
  REQUIRE(Run(u.r, u.r.drv.FinishSimpleCal(), e));
  CHECK_EQ(e.error, Error::Refused);
  CHECK_EQ(e.detail, 3U);
}

TEST_CASE("commands: motion intent needs a BNO086 unless strict_variant is off (A3, A4)") {
  {
    Up u(Variant::Bno086);
    Event e{};
    REQUIRE(Run(u.r, u.r.drv.SetMotionIntent(MotionIntent::InMotion), e));
    CHECK_EQ(u.r.wire.hub.last_motion_intent, 3U);
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange): an invalid value on purpose
    CHECK_EQ(u.r.drv.SetMotionIntent(static_cast<MotionIntent>(9)).error, Error::InvalidArgument);
  }
  {
    Up u(Variant::Bno085);
    CHECK_EQ(u.r.drv.SetMotionIntent(MotionIntent::InMotion).error, Error::Variant);
  }
  {
    rig::Rig<rig::I2cFull> r;
    Config c = Cfg();
    c.strict_variant = false;
    REQUIRE(r.Start(c));
    Event e{};
    REQUIRE(Run(r, r.drv.SetMotionIntent(MotionIntent::StationaryNoVibration), e));
    CHECK_EQ(r.wire.hub.last_motion_intent, 0xFFU); // the hub decides: a BNO085 ignores it
  }
}

TEST_CASE("commands: wheel samples") {
  Up u;
  WheelSample s{};
  s.wheel = 1;
  s.timestamp_us = 0x01020304U;
  s.data = 0xBEEF;
  s.velocity = true;
  Event e{};
  REQUIRE(Run(u.r, u.r.drv.SendWheel(s), e));
  const Bytes& w = u.r.wire.hub.last_wheel;
  REQUIRE(w.size() == 8U);
  CHECK_EQ(w[0], 1U);
  CHECK_EQ(w[1], 0x04U);
  CHECK_EQ(w[4], 0x01U);
  CHECK_EQ(w[5], 0xEFU);
  CHECK_EQ(w[6], 0xBEU);
  CHECK_EQ(w[7], 1U);
  s.wheel = 2;
  CHECK_EQ(u.r.drv.SendWheel(s).error, Error::InvalidArgument);
}

TEST_CASE("commands: a late response of an old command is ignored") {
  Up u;
  Bytes late(16, 0);
  late[0] = 0xF1;
  late[2] = 0x06;
  late[3] = 77;
  u.r.wire.hub.Inject(2, late);
  Event e{};
  REQUIRE(Run(u.r, u.r.drv.SaveDcd(), e));
  CHECK_EQ(e.error, Error::Ok);
}

TEST_CASE("commands: an autonomous initialise response while ready is a hub restart") {
  Up u;
  Bytes init(16, 0);
  init[0] = 0xF1;
  init[2] = 0x84;
  init[5] = 0;
  init[6] = 1;
  u.r.wire.hub.Inject(2, init);
  u.r.RunMs(50);
  CHECK_EQ(u.r.drv.GetStats().hub_resets, 1U);
}
