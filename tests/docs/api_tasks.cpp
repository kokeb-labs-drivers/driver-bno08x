/**
 * @file api_tasks.cpp
 * @brief The API guide's examples, compiled by the host build (never run).
 */
#include <array>
#include <cinttypes>
#include <cstdint>
#include <cstdio>

#include "bno08x.hpp"
#include "my_port.hpp"

using Hub = bno08x::Bno08x<MyPort>;

// --8<-- [start:lifecycle]
bno08x::Error BringUp(Hub& hub, const bno08x::Config& cfg) {
  // Starts the session and returns at once. Poll() then runs the reset, the advertisement and the
  // initialise handshake; hub.Ready() turns true when it is done, and hub.GetState() says where it
  // is.
  const auto r = hub.Begin(cfg);
  if (!r.ok()) {
    std::printf("BNO08x begin: %s\n", bno08x::ToString(r.error));
  }
  return r.error;
}
// --8<-- [end:lifecycle]

// --8<-- [start:sensors]
bno08x::Error EnableBatched(Hub& hub) {
  bno08x::FeatureConfig fc{};
  fc.interval_us = 10000; // 100 Hz
  fc.batch_us = 100000;   // deliver in groups every 100 ms
  return hub.SetFeature(bno08x::SensorId::GyroscopeCalibrated, fc).error;
}
// --8<-- [end:sensors]

// --8<-- [start:events]
void Drain(Hub& hub, bno08x::OpId waiting_for) {
  bno08x::Event e{};
  while (hub.PopEvent(e)) {
    switch (e.kind) {
    case bno08x::EventKind::Sensor:
      std::printf("sensor 0x%02" PRIX32 " at %" PRIu32 " us\n", static_cast<uint32_t>(e.sensor),
                  e.t_us);
      break;
    case bno08x::EventKind::OpDone:
      if (e.op == waiting_for) {
        std::printf("operation finished: %s\n", bno08x::ToString(e.error));
      }
      break;
    case bno08x::EventKind::Reset:
      std::printf("the hub restarted: features are sent again\n");
      break;
    default:
      break;
    }
  }
}
// --8<-- [end:events]

// --8<-- [start:calibration]
bno08x::Error CalibrateAndSave(Hub& hub) {
  bno08x::MeCal cal{};
  cal.accel = true;
  cal.gyro = true;
  cal.mag = true;
  if (const auto r = hub.ConfigureMeCal(cal); !r.ok()) {
    return r.error;
  }
  // ... wait for its OpDone, rotate the board until the accuracy reaches High, then:
  return hub.SaveDcd().error;
}
// --8<-- [end:calibration]

// --8<-- [start:tare]
bno08x::Error TareHeading(Hub& hub) {
  if (const auto r = hub.Tare(bno08x::AxisMask::ZOnly, bno08x::TareBasis::RotationVector);
      !r.ok()) {
    return r.error;
  }
  return hub.PersistTare().error; // optional: keep it over a restart
}
// --8<-- [end:tare]

// --8<-- [start:frs]
bno08x::Error ReadOrientationRecord(Hub& hub) {
  static bno08x::FrsBuffer buffer;          // must stay valid until the OpDone event
  return hub.ReadFrs(0x2D3E, buffer).error; // the words are in `buffer` when the operation finishes
}
// --8<-- [end:frs]

// --8<-- [start:errors]
const char* Describe(bno08x::Error e) {
  switch (e) {
  case bno08x::Error::Bus:
    return "a port hook returned false: check the wiring, the address and the bus speed";
  case bno08x::Error::Timeout:
    return "the hub did not answer: check power, the reset pin and H_INTN";
  case bno08x::Error::Variant:
    return "the configured chip does not have this feature";
  case bno08x::Error::Refused:
    return "the hub answered with a failure status";
  default:
    return bno08x::ToString(e);
  }
}
// --8<-- [end:errors]
