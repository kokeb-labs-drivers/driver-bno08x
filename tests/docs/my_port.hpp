/**
 * @file my_port.hpp
 * @brief The I2C port shown on the Quickstart and Porting pages, compiled by the host build.
 *
 * Every platform call goes through the `platform_*` functions declared below; a real port replaces
 * them with its SDK (ESP-IDF, STM32 HAL, Zephyr, Linux i2c-dev).
 */
#ifndef BNO08X_MY_PORT_HPP
#define BNO08X_MY_PORT_HPP

#include <cstddef>
#include <cstdint>

#include "bno08x.hpp"

// Your SDK's calls. Declared only: the docs build checks that the port compiles.
bool platform_i2c_init() noexcept; // 400 kHz
bool platform_i2c_write(uint8_t addr7, const uint8_t* data,
                        std::size_t len) noexcept;                              // START .. STOP
bool platform_i2c_read(uint8_t addr7, uint8_t* data, std::size_t len) noexcept; // START .. STOP
bool platform_int_low() noexcept;       // H_INTN is an input; true while it is low
void platform_nrst(bool high) noexcept; // drive the reset pin
uint32_t platform_micros() noexcept;    // free-running, wraps at 2^32

// --8<-- [start:port]
class MyPort : public bno08x::HubInterface<MyPort> {
public:
  static constexpr bno08x::Transport kTransport = bno08x::Transport::I2c;

  // Bring the bus and the pins up once; later calls return true at once.
  bool EnsureInitialized() noexcept {
    if (!ready_) {
      ready_ = platform_i2c_init();
    }
    return ready_;
  }

  // A free-running microsecond counter. The driver only subtracts times, so a wrap is fine.
  uint32_t NowUs() noexcept {
    return platform_micros();
  }

  // One write transaction: START, address + W, the bytes, STOP.
  bool I2cWrite(uint8_t addr, const uint8_t* data, std::size_t len) noexcept {
    return platform_i2c_write(addr, data, len);
  }

  // One read transaction: START, address + R, `len` bytes, STOP. Never blocks on a clock stretch
  // you can avoid: the driver reads only after H_INTN went low.
  bool I2cRead(uint8_t addr, uint8_t* data, std::size_t len) noexcept {
    return platform_i2c_read(addr, data, len);
  }

  // true while H_INTN is low: the hub has a cargo waiting.
  bool IntAsserted() noexcept {
    return platform_int_low();
  }

  // Optional: the driver resets the hub with this pin when you provide it.
  bool SetReset(bool level) noexcept {
    platform_nrst(level);
    return true;
  }

private:
  bool ready_ = false;
};
// --8<-- [end:port]

#endif // BNO08X_MY_PORT_HPP
