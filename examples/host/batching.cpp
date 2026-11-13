/**
 * @file batching.cpp
 * @brief Let the hub collect samples for 100 ms and deliver them together; flush to get them early.
 */
#include "host_common.hpp"

int main() {
  rig::Rig<rig::I2cFull> r;
  if (!r.Start(ex::DefaultConfig())) {
    return 1;
  }
  bno08x::FeatureConfig fc{};
  fc.interval_us = 10000;
  fc.batch_us = 100000;
  bool ok = ex::Finish(r, r.drv.SetFeature(bno08x::SensorId::GyroscopeCalibrated, fc),
                       "enable batched gyroscope");
  r.RunMs(250);
  ok = ex::Finish(r, r.drv.Flush(bno08x::SensorId::GyroscopeCalibrated), "flush") && ok;
  static_cast<void>(std::printf("%zu samples, every one stamped with its own time\n",
                                r.Count(bno08x::EventKind::Sensor)));
  return ok && (r.Count(bno08x::EventKind::Sensor) > 0U) ? 0 : 1;
}
