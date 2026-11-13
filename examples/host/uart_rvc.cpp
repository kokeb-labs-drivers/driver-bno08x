/**
 * @file uart_rvc.cpp
 * @brief UART-RVC: the hub streams 19-byte frames on its own; the driver only listens.
 */
#include "host_common.hpp"

int main() {
  rig::Rig<rig::RvcPort> r;
  r.wire.hub.RvcStart(); // the banner the chip prints first
  if (!r.Start(ex::DefaultConfig())) {
    return 1;
  }
  for (uint8_t i = 0; i < 5; ++i) {
    r.wire.hub.RvcPush(r.wire.hub.RvcFrame(i, 1500, -250, 30, 100, -100, 981, 0, 0));
    r.RunMs(10);
  }
  static_cast<void>(std::printf("%zu frames\n", r.Count(bno08x::EventKind::Rvc)));
  return r.Count(bno08x::EventKind::Rvc) == 5U ? 0 : 1;
}
