/**
 * @file i2c_quickstart.cpp
 * @brief Start a BNO085 on I2C, ask for the rotation vector at 50 Hz and print the angles.
 */
#include "host_common.hpp"

int main() {
  rig::Rig<rig::I2cFull> r;
  if (!r.Start(ex::DefaultConfig())) {
    static_cast<void>(std::printf("the hub did not become ready\n"));
    return 1;
  }
  bno08x::FeatureConfig fc{};
  fc.interval_us = 20000;
  if (!ex::Finish(r, r.drv.SetFeature(bno08x::SensorId::RotationVector, fc),
                  "enable rotation vector")) {
    return 1;
  }
  r.RunMs(300);
  ex::PrintEuler(r, bno08x::SensorId::RotationVector);
  return r.Count(bno08x::EventKind::Sensor) > 0U ? 0 : 1;
}
