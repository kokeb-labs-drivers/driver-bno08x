/**
 * @file step_counter.cpp
 * @brief The step counter reports a 16-bit count that wraps; compare counts with a difference.
 */
#include "host_common.hpp"

int main() {
  rig::Rig<rig::I2cFull> r;
  if (!r.Start(ex::DefaultConfig())) {
    return 1;
  }
  bno08x::FeatureConfig fc{};
  fc.interval_us = 100000;
  fc.change_enable = true;
  if (!ex::Finish(r, r.drv.SetFeature(bno08x::SensorId::StepCounter, fc), "enable step counter")) {
    return 1;
  }
  r.RunMs(300);
  const auto latest = r.drv.Latest(bno08x::SensorId::StepCounter);
  // v[0] is the latency in microseconds, v[1] the count (16 bits, wraps at 65536).
  static_cast<void>(
      std::printf("steps: %" PRId32 "\n", latest.ok() ? (latest.value.v[1] % 65536) : int32_t{-1}));
  return 0;
}
