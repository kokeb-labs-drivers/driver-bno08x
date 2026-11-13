/**
 * @file test_matching.cpp
 * @brief Response matching, restart handling, FRS verification and the remembered feature list:
 * each test pins one promise that a wrong edit of the driver would break.
 */
#include "test_support.hpp"

using namespace ts;

namespace {

using I2cRig = rig::Rig<rig::I2cFull>;

constexpr uint8_t kControl = reg::kChanControl;

/// Index of the first event matching `pred`, or the size of the list.
template <typename Pred>
std::size_t IndexOf(const I2cRig& r, Pred pred) {
  for (std::size_t i = 0; i < r.events.size(); ++i) {
    if (pred(r.events[i])) {
      return i;
    }
  }
  return r.events.size();
}

/// An FRS read response (RM Figure 39); bytes 12 and 13 echo the record type.
Bytes FrsReadResponse(uint8_t count_status, uint16_t offset, uint16_t type = 0x2D3EU) {
  Bytes b(16, 0);
  b[0] = reg::kRepFrsReadRsp;
  b[1] = count_status;
  b[2] = static_cast<uint8_t>(offset & 0xFFU);
  b[3] = static_cast<uint8_t>(offset >> 8U);
  b[12] = static_cast<uint8_t>(type & 0xFFU);
  b[13] = static_cast<uint8_t>(type >> 8U);
  return b;
}

/// Runs until the driver has written once more to the hub.
bool WaitForWrite(I2cRig& r) {
  const uint32_t before = r.wire.hub.c.writes;
  return r.RunUntil([&r, before] { return r.wire.hub.c.writes > before; }, 1'000'000);
}

/// Replaces whatever the hub queued for the host by `response`, once the driver's next write went
/// out.
bool AnswerNextWriteWith(I2cRig& r, const Bytes& response) {
  if (!WaitForWrite(r)) {
    return false;
  }
  r.wire.hub.ClearQueue();
  r.wire.hub.Inject(kControl, response);
  return true;
}

/// Puts `response` ahead of the hub's own answer to the driver's next write.
bool PrecedeNextAnswerWith(I2cRig& r, const Bytes& response) {
  if (!WaitForWrite(r)) {
    return false;
  }
  r.wire.hub.InjectFirst(kControl, response);
  return true;
}

} // namespace

TEST_CASE("matching: a command response with another command sequence is not ours") {
  I2cRig r;
  REQUIRE(r.Start(Cfg()));
  Bytes foreign(16, 0);
  foreign[0] = reg::kRepCommandRsp;
  foreign[2] = reg::kCmdSaveDcd;
  foreign[3] = 0xEEU; // a sequence number the driver never sent
  foreign[5] = 1U;    // status: failed
  const Result<OpId> res = r.drv.SaveDcd();
  REQUIRE(res.ok());
  REQUIRE(PrecedeNextAnswerWith(r, foreign));
  Event e{};
  REQUIRE(r.Await(res.value, e));
  CHECK_EQ(e.error, Error::Ok); // the hub's own response, not the foreign one
}

TEST_CASE("matching: a Get Feature response for another sensor does not complete the request") {
  I2cRig r;
  REQUIRE(r.Start(Cfg()));
  Bytes other(17, 0);
  other[0] = reg::kRepGetFeatureRsp;
  other[1] = 0x02; // the gyroscope
  other[5] = 0x40;
  other[6] = 0x42;
  other[7] = 0x0F; // 1 s
  const Result<OpId> res = r.drv.GetFeature(SensorId::Accelerometer);
  REQUIRE(res.ok());
  REQUIRE(PrecedeNextAnswerWith(r, other));
  Event e{};
  REQUIRE(r.Await(res.value, e));
  CHECK_EQ(e.error, Error::Ok);
  CHECK_EQ(e.v[2], 0); // the accelerometer is off, whatever the other sensor reported
}

TEST_CASE("matching: a Flush Completed for another sensor does not complete the flush") {
  I2cRig r;
  REQUIRE(r.Start(Cfg()));
  const Result<OpId> flush = r.drv.Flush(SensorId::Accelerometer);
  REQUIRE(flush.ok());
  REQUIRE(PrecedeNextAnswerWith(r, Bytes{reg::kRepFlushDone, 0x02})); // the gyroscope's
  Event e{};
  REQUIRE(r.Await(flush.value, e));
  CHECK_EQ(e.error, Error::Ok);
  const std::size_t own = IndexOf(r, [](const Event& ev) {
    return (ev.kind == EventKind::FlushDone) && (ev.sensor == SensorId::Accelerometer);
  });
  const std::size_t done = IndexOf(r, [&flush](const Event& ev) {
    return (ev.kind == EventKind::OpDone) && (ev.op == flush.value);
  });
  REQUIRE(own < r.events.size());
  CHECK(done > own); // it ended with the accelerometer's report, not the gyroscope's
}

TEST_CASE("restart: an observed hub restart ends a running operation with HubReset") {
  I2cRig r;
  Config c = Cfg();
  c.set_feature_confirm = true;
  REQUIRE(r.Start(c));
  r.wire.hub.set_feature_reply = false; // the request is written and never answered
  const Result<OpId> pending = r.drv.SetFeature(SensorId::Accelerometer, Feat(10000));
  REQUIRE(pending.ok());
  r.RunMs(30);
  r.wire.hub.PowerOn(r.wire.now_us); // the hub reboots by itself
  r.RunMs(10);
  CHECK_EQ(Err(r, pending, 300), Error::HubReset);
}

TEST_CASE("restart: the host's sequence numbers start again after an observed restart") {
  I2cRig r;
  REQUIRE(r.Start(Cfg()));
  Event e{};
  REQUIRE(Run(r, r.drv.GetFeature(SensorId::Accelerometer), e));
  REQUIRE(Run(r, r.drv.GetFeature(SensorId::GyroscopeCalibrated), e));
  CHECK_EQ(r.wire.hub.c.host_seq_first_not_zero, 0U);
  r.wire.hub.PowerOn(r.wire.now_us);
  r.RunMs(400);
  REQUIRE(r.drv.Ready());
  REQUIRE(Run(r, r.drv.GetFeature(SensorId::Accelerometer), e));
  CHECK_EQ(r.wire.hub.c.host_seq_errors, 0U);
  CHECK_EQ(r.wire.hub.c.host_seq_first_not_zero, 0U);
}

TEST_CASE("restart: an advertisement alone restarts a ready session") {
  I2cRig r;
  REQUIRE(r.Start(Cfg()));
  r.RunMs(20);
  r.wire.hub.Inject(reg::kChanCommand, Bytes{reg::kShtpAdvertise});
  r.RunMs(5);
  CHECK_EQ(r.drv.GetStats().hub_resets, 1U);
  CHECK_EQ(r.drv.GetState(), State::WaitReady);
}

TEST_CASE("restart: a reset-complete alone restarts a ready session") {
  I2cRig r;
  REQUIRE(r.Start(Cfg()));
  r.RunMs(20);
  r.wire.hub.Inject(reg::kChanExecutable, Bytes{reg::kExecResetComplete});
  r.RunMs(5);
  CHECK_EQ(r.drv.GetStats().hub_resets, 1U);
  CHECK_EQ(r.drv.GetState(), State::WaitReady);
}

TEST_CASE("restart: a recovery from framing faults ends running operations with Framing") {
  I2cRig r;
  Config c = Cfg();
  c.set_feature_confirm = true;
  REQUIRE(r.Start(c));
  r.wire.hub.set_feature_reply = false;
  const Result<OpId> pending = r.drv.SetFeature(SensorId::Accelerometer, Feat(10000));
  REQUIRE(pending.ok());
  r.RunMs(10);
  for (int32_t i = 0; i < 6; ++i) {
    r.wire.hub.InjectRaw(B({0xFF, 0xFF, 0x03, 0x00}));
    r.RunMs(2);
  }
  CHECK(r.drv.GetStats().resyncs >= 1U);
  CHECK_EQ(Err(r, pending, 300), Error::Framing);
}

TEST_CASE("frs: a write whose read-back differs is refused") {
  I2cRig r;
  REQUIRE(r.Start(Cfg()));
  const std::array<uint32_t, 4> words{0x10000000U, 1U, 2U, 0x20000000U};
  const Result<OpId> res = r.drv.WriteFrs(0x2D3E, words);
  REQUIRE(res.ok());
  bool tampered = false;
  REQUIRE(r.RunUntil(
      [&r,
       &tampered] { // the record is stored when the last word arrived: damage it before it is read
        const auto it = r.wire.hub.frs.find(0x2D3E);
        if (!tampered && (it != r.wire.hub.frs.end())) {
          it->second[1] ^= 0x1U;
          tampered = true;
        }
        return tampered;
      },
      1'000'000));
  Event e{};
  REQUIRE(r.Await(res.value, e));
  CHECK_EQ(e.error, Error::Refused);
  CHECK((e.detail & 0x100U) != 0U);
}

TEST_CASE("frs: a read response past the end of the buffer is an overflow") {
  I2cRig r;
  REQUIRE(r.Start(Cfg()));
  FrsBuffer buf{};
  const Result<OpId> res = r.drv.ReadFrs(0x2D3E, buf);
  REQUIRE(res.ok());
  REQUIRE(AnswerNextWriteWith(r, FrsReadResponse(0x23U, static_cast<uint16_t>(kFrsMaxWords - 1U))));
  Event e{};
  REQUIRE(r.Await(res.value, e));
  CHECK_EQ(e.error, Error::Overflow);
}

TEST_CASE("frs: read statuses map to Busy, DeviceFault, Refused and an empty record") {
  struct Row {
    uint8_t status;
    Error error;
  };
  const Row rows[] = {
      {2U, Error::Busy}, {8U, Error::DeviceFault}, {1U, Error::Refused}, {5U, Error::Ok}};
  for (const Row& row : rows) {
    I2cRig r;
    REQUIRE(r.Start(Cfg()));
    FrsBuffer buf{};
    buf.length = 7U;
    const Result<OpId> res = r.drv.ReadFrs(0x2D3E, buf);
    REQUIRE(res.ok());
    REQUIRE(AnswerNextWriteWith(r, FrsReadResponse(row.status, 0U)));
    Event e{};
    REQUIRE(r.Await(res.value, e));
    CHECK_EQ(e.error, row.error);
    CHECK_EQ(e.detail, static_cast<uint32_t>(row.status));
    if (row.error == Error::Ok) {
      CHECK_EQ(buf.length, 0U);
    }
  }
}

TEST_CASE("feature: every flag bit and the signed sensitivity survive the wire both ways") {
  I2cRig r;
  REQUIRE(r.Start(Cfg()));
  FeatureConfig f = Feat(20000, 100000, true);
  f.relative = true;
  f.change_enable = true;
  f.always_on = true;
  f.change_sensitivity = -5;
  f.sensor_config = 0xA5A5A5A5U;
  Event e{};
  REQUIRE(Run(r, r.drv.SetFeature(SensorId::RotationVector, f), e));
  CHECK_EQ(r.wire.hub.feat[5].flags, 0x0FU);
  REQUIRE(Run(r, r.drv.GetFeature(SensorId::RotationVector), e));
  const Result<FeatureConfig> shadow = r.drv.Feature(SensorId::RotationVector);
  REQUIRE(shadow.ok());
  CHECK(shadow.value == f);
  CHECK(shadow.value.always_on);
  CHECK(shadow.value.relative);
  CHECK_EQ(shadow.value.change_sensitivity, -5);
}

TEST_CASE("restore: a failed enable is not re-sent after a hub restart") {
  I2cRig r;
  REQUIRE(r.Start(Cfg()));
  r.wire.fail_writes = 1;
  const Result<OpId> enable = r.drv.SetFeature(SensorId::Accelerometer, Feat(10000));
  REQUIRE(enable.ok());
  CHECK_EQ(Err(r, enable), Error::Bus);
  Event e{};
  REQUIRE(Run(r, r.drv.Reset(), e));
  r.RunMs(600);
  REQUIRE(r.drv.Ready());
  CHECK_EQ(r.wire.hub.feat[1].interval_us, 0U);
}

TEST_CASE("restore: an enable the hub confirmed is re-sent, a later disable is forgotten") {
  I2cRig r;
  Config c = Cfg();
  c.set_feature_confirm = true;
  REQUIRE(r.Start(c));
  Event e{};
  REQUIRE(Run(r, r.drv.SetFeature(SensorId::Accelerometer, Feat(10000)), e));
  REQUIRE(Run(r, r.drv.SetFeature(SensorId::GyroscopeCalibrated, Feat(10000)), e));
  REQUIRE(Run(r, r.drv.SetFeature(SensorId::GyroscopeCalibrated, Feat(0)), e));
  REQUIRE(Run(r, r.drv.Reset(), e));
  r.RunMs(600);
  REQUIRE(r.drv.Ready());
  CHECK_EQ(r.wire.hub.feat[1].interval_us, 10000U);
  CHECK_EQ(r.wire.hub.feat[2].interval_us, 0U);
}

TEST_CASE("restore: an enable the hub never answered is not remembered") {
  I2cRig r;
  Config c = Cfg();
  c.set_feature_confirm = true;
  c.op_timeout_ms = 100;
  REQUIRE(r.Start(c));
  r.wire.hub.set_feature_reply = false;
  CHECK_EQ(Err(r, r.drv.SetFeature(SensorId::Accelerometer, Feat(10000)), 1000), Error::Timeout);
  r.wire.hub.set_feature_reply = true;
  Event e{};
  REQUIRE(Run(r, r.drv.Reset(), e));
  r.RunMs(600);
  REQUIRE(r.drv.Ready());
  r.RunMs(100);
  CHECK_EQ(r.wire.hub.feat[1].interval_us, 0U); // the restart cleared the hub; nothing is re-sent
}

TEST_CASE("disable: switching a sensor off is accepted while the session waits to be ready") {
  I2cRig r;
  REQUIRE(r.Start(Cfg()));
  Event e{};
  REQUIRE(Run(r, r.drv.SetFeature(SensorId::Accelerometer, Feat(10000)), e));
  r.RunMs(30);
  r.wire.hub.k.omit_init_response = true;
  r.wire.hub.Nrst(false, r.wire.now_us);
  r.RunMs(1);
  r.wire.hub.Nrst(true, r.wire.now_us);
  r.RunMs(150);
  REQUIRE(r.drv.GetState() == State::WaitReady);
  const Result<OpId> off = r.drv.SetFeature(SensorId::Accelerometer, Feat(0));
  REQUIRE(off.ok());
  Bytes init(16, 0);
  init[0] = reg::kRepCommandRsp;
  init[2] = 0x80U | reg::kCmdInitialize; // the unsolicited initialise response
  r.wire.hub.Inject(kControl, init);
  r.RunMs(300);
  REQUIRE(r.drv.Ready());
  CHECK_EQ(r.wire.hub.feat[1].interval_us, 0U); // not restored, and the disable went out
}

TEST_CASE("disable: an off request carries no options and is never an invalid argument") {
  I2cRig r;
  REQUIRE(r.Start(Cfg()));
  FeatureConfig off{};
  off.change_enable = true;
  off.change_sensitivity = 3;
  Event e{};
  REQUIRE(Run(r, r.drv.SetFeature(SensorId::Gravity, off), e)); // a sensor without sensitivity
  CHECK_EQ(e.error, Error::Ok);
  CHECK_EQ(r.wire.hub.feat[6].interval_us, 0U);
}

TEST_CASE("frs: a read response with a length nibble of 3 still carries two words") {
  I2cRig r;
  REQUIRE(r.Start(Cfg()));
  FrsBuffer buf{};
  const Result<OpId> res = r.drv.ReadFrs(0x2D3E, buf);
  REQUIRE(res.ok());
  Bytes rsp = FrsReadResponse(0x33U, 0U); // three words claimed, status 3 (completed)
  rsp[4] = 0x11U;
  rsp[8] = 0x22U;
  REQUIRE(AnswerNextWriteWith(r, rsp));
  Event e{};
  REQUIRE(r.Await(res.value, e));
  CHECK_EQ(e.error, Error::Ok);
  CHECK_EQ(buf.length, 2U); // Data0 and Data1 only (RM Figure 39)
  CHECK_EQ(buf.words[0], 0x11U);
  CHECK_EQ(buf.words[1], 0x22U);
}

TEST_CASE("frs: a read response for another record type is ignored") {
  I2cRig r;
  REQUIRE(r.Start(Cfg()));
  FrsBuffer buf{};
  const Result<OpId> res = r.drv.ReadFrs(0x2D3E, buf); // not stored in the model: empty
  REQUIRE(res.ok());
  Bytes late = FrsReadResponse(0x13U, 0U, 0x1F1FU); // one word, completed, of another record
  late[4] = 0x55U;
  REQUIRE(PrecedeNextAnswerWith(r, late));
  Event e{};
  REQUIRE(r.Await(res.value, e));
  CHECK_EQ(e.error, Error::Ok);
  CHECK_EQ(buf.length, 0U);
  CHECK_EQ(r.drv.GetStats().frs_foreign, 1U);
}

TEST_CASE("matching: an operation without an answer ends at op_timeout_ms, not later") {
  rig::Rig<rig::I2cFull> r;
  Config c = Cfg();
  c.set_feature_confirm = true;
  c.op_timeout_ms = 50;
  REQUIRE(r.Start(c));
  r.RunMs(20);
  r.wire.hub.set_feature_reply = false;
  const uint32_t t0 = r.wire.now_us;
  const Result<OpId> res = r.drv.SetFeature(SensorId::Accelerometer, Feat(10000));
  REQUIRE(res.ok());
  Event e{};
  REQUIRE(r.Await(res.value, e, 500));
  CHECK_EQ(e.error, Error::Timeout);
  const uint32_t took = e.t_us - t0; // written in the first Poll(), then the limit runs
  CHECK_EQ(took, (static_cast<uint32_t>(c.op_timeout_ms) * 1000U) + r.step_us); // at the limit
}
