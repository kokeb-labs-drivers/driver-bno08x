/**
 * @file calibration.cpp
 * @brief Choose which calibrations run, read the choice back and save the result.
 */
#include "host_common.hpp"

int main() {
  rig::Rig<rig::I2cFull> r;
  if (!r.Start(ex::DefaultConfig())) {
    return 1;
  }
  bno08x::MeCal cal{};
  cal.accel = true;
  cal.gyro = true;
  cal.mag = true;
  bool ok = ex::Finish(r, r.drv.ConfigureMeCal(cal), "configure calibration");
  ok = ex::Finish(r, r.drv.GetMeCal(), "read calibration") && ok;
  ok = ex::Finish(r, r.drv.SaveDcd(), "save calibration") && ok;
  return ok ? 0 : 1;
}
