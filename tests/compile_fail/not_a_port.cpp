/**
 * @file not_a_port.cpp
 * @brief Must NOT compile: a class that does not derive from `HubInterface` is not a port.
 *
 * EXPECT: HubPort
 */
#include <cstddef>
#include <cstdint>

#include "bno08x.hpp"

struct NotAPort {
  bool EnsureInitialized() noexcept {
    return true;
  }
};

int main() {
  NotAPort port;
  bno08x::Bno08x<NotAPort> hub(port);
  return hub.Ready() ? 1 : 0;
}
