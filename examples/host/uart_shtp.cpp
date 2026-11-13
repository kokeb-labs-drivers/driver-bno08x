/**
 * @file uart_shtp.cpp
 * @brief SHTP over UART (3 Mbit/s): the driver frames, escapes and paces the bytes itself.
 */
#include "host_common.hpp"

int main() {
  rig::Rig<rig::UartFull> r;
  if (!r.Start(ex::DefaultConfig())) {
    return 1;
  }
  bno08x::FeatureConfig fc{};
  fc.interval_us = 10000;
  if (!ex::Finish(r, r.drv.SetFeature(bno08x::SensorId::Accelerometer, fc),
                  "enable accelerometer")) {
    return 1;
  }
  r.RunMs(100);
  const auto latest = r.drv.Latest(bno08x::SensorId::Accelerometer);
  if (!latest.ok()) {
    return 1;
  }
  static_cast<void>(std::printf("ax %" PRId32 " mm/s2\n", bno08x::ToMmPerS2(latest.value.v[0])));
  return 0;
}
