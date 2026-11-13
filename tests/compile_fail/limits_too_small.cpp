/**
 * @file limits_too_small.cpp
 * @brief Must NOT compile: `Limits` below the minimum sizes is rejected.
 *
 * EXPECT: LimitsLike|constraints not satisfied
 */
#include <cstddef>
#include <cstdint>

#include "bno08x.hpp"

class P : public bno08x::HubInterface<P> {
public:
  static constexpr bno08x::Transport kTransport = bno08x::Transport::I2c;
  bool EnsureInitialized() noexcept {
    return true;
  }
  uint32_t NowUs() noexcept {
    return 0U;
  }
  bool I2cWrite(uint8_t, const uint8_t*, std::size_t) noexcept {
    return true;
  }
  bool I2cRead(uint8_t, uint8_t*, std::size_t) noexcept {
    return true;
  }
  bool IntAsserted() noexcept {
    return false;
  }
};

struct Tiny {
  static constexpr std::size_t kRxCargo = 16;
  static constexpr std::size_t kTxCargo = 128;
  static constexpr std::size_t kOpQueue = 8;
  static constexpr std::size_t kEventQueue = 32;
};

int main() {
  P port;
  bno08x::Bno08x<P, Tiny> hub(port);
  return hub.Ready() ? 1 : 0;
}
