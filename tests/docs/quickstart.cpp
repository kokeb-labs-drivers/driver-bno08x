/**
 * @file quickstart.cpp
 * @brief The Quickstart page's example, compiled by the host build (never run).
 */
#include <cinttypes>
#include <cstdint>
#include <cstdio>

#include "bno08x.hpp"
#include "bno08x_euler.hpp"
#include "my_port.hpp"

// --8<-- [start:quickstart]
void RunSensor() {
  static MyPort port;
  static bno08x::Bno08x<MyPort> hub(port); // no I/O yet

  const bno08x::Config cfg{}; // BNO085 on I2C 0x4A
  if (const auto r = hub.Begin(cfg); !r.ok()) {
    std::printf("Begin: %s\n", bno08x::ToString(r.error));
    return;
  }

  bool requested = false;
  for (;;) {
    hub.Poll(port.NowUs()); // every few milliseconds: reads, writes and time-outs all happen here
    if (hub.Ready() && !requested) {
      bno08x::FeatureConfig fc{};
      fc.interval_us = 20000; // 50 Hz
      requested = hub.SetFeature(bno08x::SensorId::RotationVector, fc).ok();
    }
    bno08x::Event e{};
    while (hub.PopEvent(e)) {
      if (e.kind == bno08x::EventKind::Sensor) {
        const auto eu = bno08x::ToEulerCentiDeg(e.v[0], e.v[1], e.v[2], e.v[3]);
        std::printf("yaw %" PRId32 " pitch %" PRId32 " roll %" PRId32 " (centi-degrees)\n",
                    static_cast<int32_t>(eu.yaw), static_cast<int32_t>(eu.pitch),
                    static_cast<int32_t>(eu.roll));
      }
    }
  }
}
// --8<-- [end:quickstart]
