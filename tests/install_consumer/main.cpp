/**
 * @file main.cpp
 * @brief Builds the driver from the installed package only: headers, version header and .ipp.
 */
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "bno08x.hpp"

namespace {

// A port whose bus never carries data: enough to instantiate the whole driver template.
class NullPort : public bno08x::HubInterface<NullPort> {
public:
  static constexpr bno08x::Transport kTransport = bno08x::Transport::I2c;
  bool EnsureInitialized() noexcept {
    return true;
  }
  uint32_t NowUs() noexcept {
    return now_;
  }
  bool I2cWrite(uint8_t /*addr*/, const uint8_t* /*data*/, std::size_t /*len*/) noexcept {
    return true;
  }
  bool I2cRead(uint8_t /*addr*/, uint8_t* data, std::size_t len) noexcept {
    for (std::size_t i = 0; i < len; ++i) {
      data[i] = 0;
    }
    return true;
  }
  bool IntAsserted() noexcept {
    return false;
  }
  void SetNow(uint32_t t) noexcept {
    now_ = t;
  }

private:
  uint32_t now_{0};
};

} // namespace

int main() {
  NullPort port;
  bno08x::Bno08x<NullPort> hub(port);
  // No hub answers: Begin() accepts the configuration, and Poll() must run the start-up until it
  // times out into Faulted. That proves the template compiled and ran.
  if (!hub.Begin(bno08x::Config{}).ok()) {
    return 1;
  }
  for (uint32_t t = 0; t < 3'000'000U; t += 1000U) {
    port.SetNow(t);
    hub.Poll(t);
  }
  return ((hub.GetState() == bno08x::State::Faulted) &&
          !std::string_view{bno08x::GetDriverVersion()}.empty())
             ? 0
             : 1;
}
