/**
 * @file uart_port_without_transmit.cpp
 * @brief Must NOT compile: a UART-SHTP port with no transmit hook does not satisfy `HubPort`.
 *
 * EXPECT: HubPort
 */
#include <cstddef>
#include <cstdint>

#include "bno08x.hpp"

class P : public bno08x::HubInterface<P> {
public:
  static constexpr bno08x::Transport kTransport = bno08x::Transport::UartShtp;
  bool EnsureInitialized() noexcept {
    return true;
  }
  uint32_t NowUs() noexcept {
    return 0U;
  }
  std::size_t UartRead(uint8_t*, std::size_t) noexcept {
    return 0U;
  }
};

int main() {
  P port;
  bno08x::Bno08x<P> hub(port);
  return hub.Ready() ? 1 : 0;
}
