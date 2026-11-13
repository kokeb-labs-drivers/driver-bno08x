/**
 * @file spi_gyro_rv.cpp
 * @brief SPI with H_INTN and WAKE: the gyro-integrated rotation vector arrives on its own channel.
 */
#include <cinttypes>
#include <cstdint>

#include "host_common.hpp"

int main() {
  rig::Rig<rig::SpiFull> r;
  if (!r.Start(ex::DefaultConfig())) {
    return 1;
  }
  bno08x::FeatureConfig fc{};
  fc.interval_us = 5000; // 200 Hz
  if (!ex::Finish(r, r.drv.SetFeature(bno08x::SensorId::GyroIntegratedRotationVector, fc),
                  "enable gyro RV")) {
    return 1;
  }
  // The hub sends this report on its own channel (5), without a report ID: i, j, k, real, then the
  // rates.
  r.wire.hub.Inject(5, {0, 0x40, 0, 0, 0, 0, 0xFF, 0x3F, 1, 0, 2, 0, 3, 0});
  r.RunMs(50);
  const auto latest = r.drv.Latest(bno08x::SensorId::GyroIntegratedRotationVector);
  static_cast<void>(std::printf("%" PRIu32 " wake pulses, rotation vector i = %" PRId32 " (Q14)\n",
                                r.wire.wake_calls, latest.ok() ? latest.value.v[0] : 0));
  return latest.ok() ? 0 : 1;
}
