/**
 * @file fault_tolerance.cpp
 * @brief A failing bus and a silent hub: the driver reports, counts and recovers; it never blocks.
 */
#include "host_common.hpp"

int main() {
  rig::Rig<rig::I2cFull> r;
  if (!r.Start(ex::DefaultConfig())) {
    return 1;
  }
  bno08x::FeatureConfig fc{};
  fc.interval_us = 20000;
  static_cast<void>(
      ex::Finish(r, r.drv.SetFeature(bno08x::SensorId::Accelerometer, fc), "enable accelerometer"));
  r.wire.fail_reads = 5; // five bus reads fail
  r.RunMs(300);
  const std::size_t after_fault = r.Count(bno08x::EventKind::Sensor);
  static_cast<void>(std::printf("%zu samples survived the faults; state %" PRId32 "\n", after_fault,
                                static_cast<int32_t>(r.drv.GetState())));
  return (after_fault > 0U) && r.drv.Ready() ? 0 : 1;
}
