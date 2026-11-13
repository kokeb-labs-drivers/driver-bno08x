/**
 * @file test_rvc.cpp
 * @brief UART-RVC: frames, checksum rules, the banner, MI and MR, index continuity, guarded host
 * inputs.
 */
#include "test_support.hpp"

using namespace ts;

namespace {
struct Rvc {
  rig::Rig<rig::RvcPort> r;
  explicit Rvc(Variant v = Variant::Bno085, bool strict = false)
      : r(v == Variant::Bno086 ? hubmodel::Chip::Bno086 : hubmodel::Chip::Bno085) {
    Config c = Cfg(v);
    c.rvc_strict = strict;
    r.wire.hub.k.rvc_banner = true;
    static_cast<void>(r.drv.Begin(c));
  }
  void Push(const Bytes& b) {
    r.wire.hub.RvcPush(b);
    r.RunMs(30);
  }
  [[nodiscard]] std::vector<Event> Frames() const {
    std::vector<Event> f;
    for (const Event& e : r.events) {
      if (e.kind == EventKind::Rvc) {
        f.push_back(e);
      }
    }
    return f;
  }
};
} // namespace

TEST_CASE("rvc: a frame decodes yaw, pitch, roll and acceleration") {
  Rvc u;
  REQUIRE(u.r.drv.Ready());
  u.Push(u.r.wire.hub.RvcFrame(7, 1800, -900, 100, 10, -20, 981, 0, 0));
  const std::vector<Event> f = u.Frames();
  REQUIRE(f.size() == 1U);
  CHECK_EQ(f[0].v[0], 7);
  CHECK_EQ(f[0].v[1], 1800);
  CHECK_EQ(f[0].v[2], -900);
  CHECK_EQ(f[0].v[3], 100);
  CHECK_EQ(f[0].v[4], 10);
  CHECK_EQ(f[0].v[5], -20);
  CHECK_EQ(f[0].v[6], 981);
  CHECK_EQ(f[0].v[9], 0);
  CHECK_EQ(u.r.drv.GetStats().rvc_frames, 1U);
}

TEST_CASE("rvc: BNO086 frames carry MI and MR (A5)") {
  Rvc u(Variant::Bno086);
  u.Push(u.r.wire.hub.RvcFrame(1, 0, 0, 0, 0, 0, 0, 3, 2));
  const std::vector<Event> f = u.Frames();
  REQUIRE(f.size() == 1U);
  CHECK_EQ(f[0].v[7], 3);
  CHECK_EQ(f[0].v[8], 2);
  CHECK_EQ(f[0].v[9], 1);
}

TEST_CASE("rvc: both checksum rules are accepted and the one that matched is counted (A6)") {
  Rvc u(Variant::Bno086);
  u.Push(u.r.wire.hub.RvcFrame(1, 0, 0, 0, 0, 0, 0, 3, 2));
  CHECK_EQ(u.r.drv.GetStats().rvc_csum_with_mi, 1U);
  u.r.wire.hub.k.rvc_csum_includes_mi_mr = false;
  u.Push(u.r.wire.hub.RvcFrame(2, 0, 0, 0, 0, 0, 0, 3, 2));
  CHECK_EQ(u.r.drv.GetStats().rvc_csum_without_mi, 1U);
  CHECK_EQ(u.Frames().size(), 2U);
  CHECK_EQ(u.r.drv.GetStats().rvc_bad_csum, 0U);
}

TEST_CASE("rvc: the ASCII banner is skipped and counted (A9)") {
  Rvc u;
  u.r.wire.hub.RvcStart();
  u.Push(u.r.wire.hub.RvcFrame(1, 0, 0, 0, 0, 0, 0, 0, 0));
  CHECK(u.r.drv.GetStats().rvc_banner_bytes >= 10U);
  CHECK_EQ(u.Frames().size(), 1U);
}

TEST_CASE("rvc: a bad checksum drops the frame, a false header inside a frame does not confuse the "
          "scanner") {
  Rvc u;
  Bytes bad = u.r.wire.hub.RvcFrame(1, 0, 0, 0, 0, 0, 0, 0, 0);
  bad[18] = static_cast<uint8_t>(bad[18] + 1U);
  u.Push(bad);
  CHECK_EQ(u.r.drv.GetStats().rvc_bad_csum, 1U);
  CHECK_EQ(u.Frames().size(), 0U);
  // yaw = 0xAAAA inside the frame
  u.Push(u.r.wire.hub.RvcFrame(2, static_cast<int16_t>(0xAAAA), 0, 0, 0, 0, 0, 0, 0));
  u.Push(u.r.wire.hub.RvcFrame(3, 5, 6, 7, 0, 0, 0, 0, 0));
  const std::vector<Event> f = u.Frames();
  REQUIRE(!f.empty());
  CHECK_EQ(f.back().v[0], 3);
  CHECK_EQ(f.back().v[1], 5);
}

TEST_CASE("rvc: noise between frames is skipped") {
  Rvc u;
  u.Push(B({1, 2, 3, 0xAA, 4, 5}));
  u.Push(u.r.wire.hub.RvcFrame(9, 1, 2, 3, 4, 5, 6, 0, 0));
  CHECK_EQ(u.Frames().size(), 1U);
}

TEST_CASE("rvc: index gaps are counted, strict mode also rejects the frame") {
  {
    Rvc u;
    u.Push(u.r.wire.hub.RvcFrame(1, 0, 0, 0, 0, 0, 0, 0, 0));
    u.Push(u.r.wire.hub.RvcFrame(2, 0, 0, 0, 0, 0, 0, 0, 0));
    u.Push(u.r.wire.hub.RvcFrame(9, 0, 0, 0, 0, 0, 0, 0, 0));
    CHECK_EQ(u.r.drv.GetStats().rvc_index_gaps, 1U);
    CHECK_EQ(u.Frames().size(), 3U);
  }
  {
    Rvc u(Variant::Bno085, true);
    u.Push(u.r.wire.hub.RvcFrame(1, 0, 0, 0, 0, 0, 0, 0, 0));
    u.Push(u.r.wire.hub.RvcFrame(9, 0, 0, 0, 0, 0, 0, 0, 0));
    u.Push(u.r.wire.hub.RvcFrame(10, 0, 0, 0, 0, 0, 0, 0, 0));
    CHECK(u.Frames().size() >= 2U);
  }
}

TEST_CASE("rvc: the index wraps from 255 to 0 without a gap") {
  Rvc u;
  u.Push(u.r.wire.hub.RvcFrame(255, 0, 0, 0, 0, 0, 0, 0, 0));
  u.Push(u.r.wire.hub.RvcFrame(0, 0, 0, 0, 0, 0, 0, 0, 0));
  CHECK_EQ(u.r.drv.GetStats().rvc_index_gaps, 0U);
}

TEST_CASE("rvc: angles outside the documented range are counted but delivered (A7)") {
  Rvc u;
  u.Push(u.r.wire.hub.RvcFrame(1, 0, 12000, 0, 0, 0, 0, 0, 0)); // pitch 120 degrees
  u.Push(u.r.wire.hub.RvcFrame(2, 19000, 0, 0, 0, 0, 0, 0, 0)); // yaw 190 degrees
  CHECK_EQ(u.r.drv.GetStats().rvc_out_of_range, 2U);
  CHECK_EQ(u.Frames().size(), 2U);
}

TEST_CASE("rvc: SHTP operations are not available") {
  Rvc u;
  CHECK_EQ(u.r.drv.SetFeature(SensorId::Accelerometer, Feat(10000)).error, Error::Unsupported);
  CHECK_EQ(u.r.drv.Reset().error, Error::Unsupported);
  CHECK_EQ(u.r.drv.Tare(AxisMask::All, TareBasis::RotationVector).error, Error::Unsupported);
  CHECK_EQ(u.r.drv.HardReset().error, Error::Unsupported);
  CHECK_EQ(u.r.drv.ReadProductId().error, Error::Unsupported);
}

TEST_CASE("rvc: the $MIB host inputs are guarded (A8)") {
  Rvc u;
  const Bytes text = AsBytes("$MIB,RESET*3F");
  CHECK_EQ(u.r.drv.RvcSend(text).error, Error::Unsupported);
  CHECK(u.r.port.mib.empty());
  Config c = Cfg();
  c.rvc_inputs_experimental = true;
  REQUIRE(u.r.drv.Begin(c).ok());
  CHECK(u.r.drv.RvcSend(text).ok());
  CHECK(u.r.port.mib == "$MIB,RESET*3F");
  u.r.wire.uart_drop_tx = true;
  CHECK_EQ(u.r.drv.RvcSend(text).error, Error::Bus);
}

TEST_CASE("rvc: the model's checksum for $MIB inputs follows its knob (A8)") {
  hubmodel::Hub h;
  h.k.rvc_input_cs = 0;
  CHECK(h.MibCheck("$MIB,RESET*3F"));
  CHECK(!h.MibCheck("$MIB,RESET*87"));
  h.k.rvc_input_cs = 1;
  CHECK(h.MibCheck("$MIB,RESET*87"));
  h.k.rvc_input_cs = 2;
  CHECK(h.MibCheck("$MIB,CLRDCD*00"));
  CHECK(!h.MibCheck("RESET"));
}

TEST_CASE("rvc: the event queue keeps the newest frames' counters when full") {
  Rvc u;
  for (uint8_t i = 0; i < 40; ++i) {
    u.r.wire.hub.RvcPush(u.r.wire.hub.RvcFrame(i, 0, 0, 0, 0, 0, 0, 0, 0));
  }
  u.r.RunMs(5);
  CHECK(u.r.drv.GetStats().rvc_frames >= 30U);
}

TEST_CASE("rvc: a bad frame ending in 0xAA keeps it as the start of the next frame") {
  Rvc u;
  Bytes bad{0xAA, 0xAA};
  bad.insert(bad.end(), 16U, uint8_t{0x01});
  bad.push_back(0xAA); // a wrong checksum that is also the first header byte of the next frame
  const Bytes good = u.r.wire.hub.RvcFrame(5, 100, 0, 0, 0, 0, 0, 0, 0);
  Bytes stream = bad;
  stream.insert(stream.end(), good.begin() + 1, good.end()); // its first 0xAA was the checksum
  u.Push(stream);
  const std::vector<Event> f = u.Frames();
  REQUIRE(f.size() == 1U);
  CHECK_EQ(f[0].v[0], 5);
}
