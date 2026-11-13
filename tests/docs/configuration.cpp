/**
 * @file configuration.cpp
 * @brief The Configuration page's examples, compiled by the host build (never run).
 */
#include "bno08x.hpp"
#include "bno08x_kconfig.hpp"
#include "my_port.hpp"

// --8<-- [start:custom]
bno08x::Config BnoOnSa0High() {
  bno08x::Config c{};                        // the recommended setup for a BNO085
  c.variant = bno08x::Variant::Bno086;       // chosen by you: the chip is never detected
  c.i2c_address = bno08x::reg::kI2cAddrHigh; // SA0 tied high
  c.set_feature_confirm = true;              // SetFeature finishes on the hub's own reply
  c.op_timeout_ms = 400;
  return c; // c.Valid() checks it without the chip
}

struct SmallLimits {
  static constexpr std::size_t kRxCargo = 512; // at least 256
  static constexpr std::size_t kTxCargo = 64;  // at least 24
  static constexpr std::size_t kOpQueue = 4;
  static constexpr std::size_t kEventQueue = 16;
};

bno08x::Error StartSmall(MyPort& port) {
  static bno08x::Bno08x<MyPort, SmallLimits> hub(port); // the buffers live inside the object
  return hub.Begin(BnoOnSa0High()).error; // InvalidArgument if a field is out of range
}
// --8<-- [end:custom]

// --8<-- [start:kconfig]
bno08x::Error StartFromMenuconfig(MyPort& port) {
  static bno08x::Bno08x<MyPort, bno08x::KconfigLimits> hub(port);
  return hub.Begin(bno08x::KconfigDefaults()).error;
}
// --8<-- [end:kconfig]
