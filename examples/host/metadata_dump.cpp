/**
 * @file metadata_dump.cpp
 * @brief Read the per-sensor metadata record: ranges, resolution and the fastest and slowest rates.
 */
#include "host_common.hpp"

int main() {
  rig::Rig<rig::I2cFull> r;
  if (!r.Start(ex::DefaultConfig())) {
    return 1;
  }
  bool ok = true;
  for (const bno08x::SensorId id :
       {bno08x::SensorId::Accelerometer, bno08x::SensorId::GyroscopeCalibrated,
        bno08x::SensorId::RotationVector}) {
    ok = ex::Finish(r, r.drv.ReadMetadata(id), "read metadata") && ok;
  }
  return ok ? 0 : 1;
}
