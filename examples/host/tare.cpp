/**
 * @file tare.cpp
 * @brief Tare the heading now, then make it survive a restart.
 */
#include "host_common.hpp"

int main() {
  rig::Rig<rig::I2cFull> r;
  if (!r.Start(ex::DefaultConfig())) {
    return 1;
  }
  bool ok = ex::Finish(r, r.drv.Tare(bno08x::AxisMask::ZOnly, bno08x::TareBasis::RotationVector),
                       "tare heading");
  ok = ex::Finish(r, r.drv.PersistTare(), "persist tare") && ok;
  return ok ? 0 : 1;
}
