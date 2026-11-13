/**
 * @file test_edges.cpp
 * @brief Boundaries the other tests do not reach: the resync threshold, cargo before the
 * advertisement, advertised lengths for sensors without a layout, the reset timing, the Product ID
 * entries and the configuration limits.
 */
#include "test_support.hpp"

using namespace ts;

namespace {

using I2cRig = rig::Rig<rig::I2cFull>;

/// A transfer whose header carries the error length 0xFFFF: one framing violation (rule V2).
const Bytes kViolation{0xFF, 0xFF, 0x00, 0x00};

std::size_t SensorEvents(const I2cRig& r, SensorId id) {
  std::size_t n = 0;
  for (const Event& e : r.events) {
    n += ((e.kind == EventKind::Sensor) && (e.sensor == id)) ? 1U : 0U;
  }
  return n;
}

} // namespace

TEST_CASE("edges: resync_limit violations inside the window resynchronise, one fewer does not") {
  I2cRig r;
  Config c = Cfg();
  c.resync_limit = 3;
  c.resync_window_ms = 100;
  REQUIRE(r.Start(c));
  r.RunMs(20);
  r.wire.hub.InjectRaw(kViolation);
  r.wire.hub.InjectRaw(kViolation);
  r.RunMs(10);
  CHECK_EQ(r.drv.GetStats().length_ffff, 2U);
  CHECK_EQ(r.drv.GetStats().resyncs, 0U);
  r.wire.hub.InjectRaw(kViolation);
  r.RunMs(10);
  CHECK_EQ(r.drv.GetStats().length_ffff, 3U);
  CHECK_EQ(r.drv.GetStats().resyncs, 1U);
}

TEST_CASE("edges: violations further apart than the window never resynchronise") {
  I2cRig r;
  Config c = Cfg();
  c.resync_limit = 2;
  c.resync_window_ms = 20;
  REQUIRE(r.Start(c));
  r.RunMs(20);
  for (uint32_t i = 0; i < 4U; ++i) {
    r.wire.hub.InjectRaw(kViolation);
    r.RunMs(30);
  }
  CHECK_EQ(r.drv.GetStats().length_ffff, 4U);
  CHECK_EQ(r.drv.GetStats().resyncs, 0U);
}

TEST_CASE("edges: a sensor report before the advertisement is counted and not emitted (V16)") {
  I2cRig r;
  r.wire.hub.PowerOn(r.wire.now_us);
  REQUIRE(r.drv.Begin(Cfg()).ok());
  // the hub has booted while the driver still waits out the start-up time (DS p47)
  REQUIRE(r.RunUntil([&r] { return r.wire.hub.Booted(); }, 1'000'000));
  REQUIRE(r.drv.GetState() == State::Resetting);
  r.wire.hub.InjectFirst(3, Cat(Base(0), Report(0x01, 0, 0, 0, {1, 0, 2, 0, 3, 0})));
  REQUIRE(r.RunUntil([&r] { return r.drv.Ready(); }, 1'000'000));
  CHECK_EQ(r.drv.GetStats().early_cargo, 1U);
  CHECK_EQ(SensorEvents(r, SensorId::Accelerometer), 0U);
}

TEST_CASE("edges: an advertised length for a sensor without a layout skips its report (A42)") {
  I2cRig r;
  r.wire.hub.k.advertise_report_lengths = true;
  REQUIRE(r.Start(Cfg()));
  CHECK_EQ(r.drv.Advertised().report_len[0x2E], 12U);
  r.wire.hub.feat[0x2E].interval_us = 10000; // the hub runs the wheel encoder on its own
  r.wire.hub.feat[0x2E].next_us = r.wire.now_us;
  r.RunMs(55);
  CHECK(r.drv.GetStats().unknown_report >= 4U);
  CHECK_EQ(SensorEvents(r, SensorId::WheelEncoder), 0U);
  CHECK(!r.drv.Latest(SensorId::WheelEncoder).ok());
}

TEST_CASE("edges: an advertised pickup length of 8 (vendor library) parses every report (A41)") {
  I2cRig r;
  r.wire.hub.k.advertise_report_lengths = true;
  r.wire.hub.k.pickup_report_len = 8;
  REQUIRE(r.Start(Cfg()));
  CHECK_EQ(r.drv.Advertised().report_len[0x1B], 8U);
  Event e{};
  REQUIRE(Run(r, r.drv.SetFeature(SensorId::PickupDetector, Feat(10000, 45000)), e));
  r.RunMs(200);
  REQUIRE(Run(r, r.drv.SetFeature(SensorId::PickupDetector, FeatureConfig{}), e)); // flushes
  r.RunMs(20);
  const std::size_t n = SensorEvents(r, SensorId::PickupDetector);
  CHECK(n >= 15U);
  CHECK_EQ(n, static_cast<std::size_t>(r.wire.hub.feat[0x1B].samples));
  CHECK_EQ(r.drv.GetStats().unknown_report, 0U);
  CHECK_EQ(r.drv.GetStats().short_report, 0U);
}

TEST_CASE("edges: NRST is held low for reset_pulse_us") {
  I2cRig r;
  Config c = Cfg();
  c.reset_pulse_us = 5000;
  REQUIRE(r.Start(c));
  CHECK(r.wire.hub.c.nrst_low_us >= 5000U);
  CHECK(r.wire.hub.c.nrst_low_us < 6000U);
}

TEST_CASE("edges: nothing is read until t1 + t2 after NRST, even from a hub that is up earlier") {
  I2cRig r;
  r.wire.hub.boot_delay_us = 10000; // the hub is up long before the datasheet's start-up time
  REQUIRE(r.Start(Cfg()));
  REQUIRE(r.wire.first_read_us != 0U);
  CHECK((r.wire.first_read_us - r.wire.nrst_rise_us) >= 94000U); // t1 90 ms + t2 4 ms (DS p47)
}

TEST_CASE("edges: Product() is entry 0; later entries are kept per request or counted (A33)") {
  I2cRig r; // the model answers with four entries, other part numbers after the first
  REQUIRE(r.Start(Cfg()));
  r.RunMs(20);
  CHECK_EQ(r.drv.Product().part_number, 10004095U);
  CHECK_EQ(r.drv.ProductEntries().size(), 1U);
  CHECK_EQ(r.drv.GetStats().product_id_extra, 3U);
  CHECK_EQ(r.Count(EventKind::Reset), 0U); // the start-up itself is not a restart
  I2cRig f;
  Config c = Cfg();
  c.product_id_entries = 4;
  REQUIRE(f.Start(c));
  Event e{};
  REQUIRE(Run(f, f.drv.ReadProductId(), e));
  CHECK_EQ(e.error, Error::Ok);
  REQUIRE(f.drv.ProductEntries().size() == 4U);
  CHECK_EQ(f.drv.ProductEntries()[0].part_number, 10004095U);
  CHECK_EQ(f.drv.ProductEntries()[3].part_number, 10003003U);
  CHECK_EQ(f.drv.Product().part_number, 10004095U);
  CHECK_EQ(f.drv.GetStats().product_id_extra, 0U);
}

TEST_CASE("edges: the reset cause of a restart comes from entry 0") {
  I2cRig r;
  REQUIRE(r.Start(Cfg()));
  r.RunMs(20);
  REQUIRE(r.drv.HardReset().ok());
  REQUIRE(r.RunUntil([&r] { return r.Count(EventKind::Reset) == 1U; }, 1'000'000));
  for (const Event& e : r.events) {
    if (e.kind == EventKind::Reset) {
      CHECK_EQ(e.detail, 4U); // external reset (RM S6.3.2)
    }
  }
  r.RunMs(20);
  CHECK_EQ(r.drv.Product().part_number, 10004095U);
}

TEST_CASE("edges: Config::Valid refuses a zero reset pulse, a WAKE wait as long as an operation") {
  Config c{};
  CHECK(c.Valid());
  c.reset_pulse_us = 0;
  CHECK(!c.Valid());
  c = Config{};
  c.reset_pulse_us = 1;
  CHECK(c.Valid());
  c = Config{};
  c.op_timeout_ms = 10;
  c.wake_timeout_us = 10000;
  CHECK(!c.Valid());
  c.wake_timeout_us = 9999;
  CHECK(c.Valid());
  c = Config{};
  c.uart_bsq_retry_ms = 0;
  CHECK(!c.Valid());
}

TEST_CASE("edges: an advertised report length shorter than the header is refused") {
  for (const uint8_t len : {uint8_t{1}, uint8_t{2}, uint8_t{3}}) {
    I2cRig r;
    r.wire.hub.k.advertise_report_lengths = true;
    r.wire.hub.k.pickup_report_len = len;
    REQUIRE(r.Start(Cfg()));
    CHECK_EQ(r.drv.Advertised().report_len[0x1B], 6U); // the built-in length is kept
    CHECK_EQ(r.drv.GetStats().bad_advert, 1U);
  }
  bno08x::detail::Advert adv = bno08x::detail::DefaultAdvert();
  bool bad = false;
  // GUID 2, then a ReportLengths TLV giving the accelerometer 2 bytes and the gravity sensor 10
  const std::array<uint8_t, 12> tlv{0x01, 0x04, 0x02, 0x00, 0x00, 0x00,
                                    0x81, 0x04, 0x01, 0x02, 0x06, 0x0A};
  bno08x::detail::ParseAdvertisement(tlv, adv, bad);
  CHECK(bad);
  CHECK_EQ(adv.report_len[0x01], bno08x::detail::DefaultAdvert().report_len[0x01]);
  CHECK_EQ(adv.report_len[0x06], 10U);
}

TEST_CASE("edges: BOOTN is high when NRST rises, so the application starts") {
  I2cRig r;
  REQUIRE(r.Start(Cfg()));
  CHECK_EQ(r.wire.hub.c.bootloader_starts, 0U);
  REQUIRE(r.drv.HardReset().ok());
  REQUIRE(r.RunUntil([&r] { return r.drv.Ready(); }, 1'000'000));
  CHECK_EQ(r.wire.hub.c.bootloader_starts, 0U);
}

TEST_CASE("edges: reset pins that cannot be driven leave the session Faulted") {
  I2cRig r;
  REQUIRE(r.Start(Cfg()));
  r.wire.reset_fails = true;
  CHECK_EQ(r.drv.HardReset().error, Error::Bus);
  CHECK_EQ(r.drv.GetState(), State::Faulted);
  r.RunMs(200);
  CHECK_EQ(r.Count(EventKind::Reset), 0U); // no restart is reported that never happened

  I2cRig v;
  Config c = Cfg();
  c.resync_limit = 1;
  REQUIRE(v.Start(c));
  v.RunMs(20);
  v.wire.reset_fails = true;
  v.wire.hub.InjectRaw(kViolation); // the resynchronisation needs NRST, which fails
  v.RunMs(10);
  CHECK_EQ(v.drv.GetStats().resyncs, 1U);
  CHECK_EQ(v.drv.GetState(), State::Faulted);

  I2cRig n;
  n.wire.hub.PowerOn(n.wire.now_us);
  REQUIRE(n.drv.Begin(Cfg()).ok());
  n.wire.reset_fails = true; // NRST went low, but cannot be released
  REQUIRE(n.RunUntil([&n] { return n.drv.GetState() == State::Faulted; }, 100'000));
}

TEST_CASE("edges: without NRST a resynchronisation sends the reset command as its own request") {
  rig::Rig<rig::I2cMin> r;
  Config c = Cfg();
  c.resync_limit = 1;
  REQUIRE(r.Start(c));
  r.RunMs(20);
  const uint32_t boots = r.wire.hub.c.reboots;
  r.wire.hub.InjectRaw(kViolation);
  REQUIRE(r.RunUntil([&r] { return r.drv.GetStats().hub_resets == 1U; }, 1'000'000));
  CHECK_EQ(r.drv.GetStats().resyncs, 1U);
  CHECK_EQ(r.wire.hub.c.reboots, boots + 1U);
  CHECK_EQ(r.Count(EventKind::OpDone), 0U); // no OpDone for an operation the caller never had
}

TEST_CASE("edges: a ReportLengths pair for a non-sensor ID is ignored, not stored") {
  for (const uint8_t id : {uint8_t{0x2F}, uint8_t{0x30}, uint8_t{0xF1}, uint8_t{0xFF}}) {
    bno08x::detail::Advert adv = bno08x::detail::DefaultAdvert();
    bool bad = false;
    const std::array<uint8_t, 10> tlv{0x01, 0x04, 0x02, 0x00, 0x00, 0x00, 0x81, 0x02, id, 0x0A};
    bno08x::detail::ParseAdvertisement(tlv, adv, bad);
    CHECK(!bad);
    CHECK(adv.report_len == bno08x::detail::DefaultAdvert().report_len);
  }
}

TEST_CASE(
    "edges: advertisement edges: a channel number of 6 or more, a 5-byte UART timeout, length 4") {
  const bno08x::detail::Advert def = bno08x::detail::DefaultAdvert();
  {
    bno08x::detail::Advert adv = def;
    bool bad = false;
    const std::array<uint8_t, 13> tlv{0x06, 0x01, 0x07, // NormalChannel 7: there is no such channel
                                      0x09, 0x08, 'c',  'o', 'n', 't', 'r', 'o', 'l', 0x00};
    bno08x::detail::ParseAdvertisement(tlv, adv, bad);
    CHECK(adv.channel == def.channel);
  }
  {
    bno08x::detail::Advert adv = def;
    bool bad = false;
    const std::array<uint8_t, 7> tlv{0x81, 0x05, 0x0A, 0x00, 0x00, 0x00, 0x00}; // GUID 0, 5 bytes
    bno08x::detail::ParseAdvertisement(tlv, adv, bad);
    CHECK_EQ(adv.uart_timeout_ms, def.uart_timeout_ms);
  }
  {
    bno08x::detail::Advert adv = def;
    bool bad = false;
    const std::array<uint8_t, 10> tlv{0x01, 0x04, 0x02, 0x00, 0x00, 0x00, 0x81, 0x02, 0x01, 0x04};
    bno08x::detail::ParseAdvertisement(tlv, adv, bad); // a report of the header alone is valid
    CHECK(!bad);
    CHECK_EQ(adv.report_len[0x01], 4U);
  }
}
