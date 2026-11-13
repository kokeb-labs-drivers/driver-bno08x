/**
 * @file hook_narrowed_parameter.cpp
 * @brief Must NOT compile: a required hook with a narrowed parameter (`uint8_t` length) does not
 * satisfy `HubPort`.
 *
 * EXPECT: HubPort
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
  bool I2cWrite(uint8_t, const uint8_t*, uint8_t) noexcept {
    return true;
  }
  bool I2cRead(uint8_t, uint8_t*, std::size_t) noexcept {
    return true;
  }
  bool IntAsserted() noexcept {
    return false;
  }
};

int main() {
  P port;
  bno08x::Bno08x<P> hub(port);
  return hub.Ready() ? 1 : 0;
}
